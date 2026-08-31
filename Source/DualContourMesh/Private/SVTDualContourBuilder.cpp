#include "SVTDualContourBuilder.h"

#if WITH_EDITOR

#include "SVTDualContour.h"
#include "DualContourUtils.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "SparseVolumeTexture/ISparseVolumeTextureStreamingManager.h"
#include "Async/ParallelFor.h"
#include "GlobalShader.h"
#include "GlobalRenderResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class FSampleSparseVolumeTextureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSampleSparseVolumeTextureCS);
	SHADER_USE_PARAMETER_STRUCT(FSampleSparseVolumeTextureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_SAMPLER(SamplerState, TileDataTextureSampler)
		SHADER_PARAMETER_TEXTURE(Texture3D<uint>, SparseVolumeTexturePageTable)
		SHADER_PARAMETER_TEXTURE(Texture3D, SparseVolumeTextureA)
		SHADER_PARAMETER_TEXTURE(Texture3D, SparseVolumeTextureB)
		SHADER_PARAMETER(FUintVector4, PackedSVTUniforms0)
		SHADER_PARAMETER(FUintVector4, PackedSVTUniforms1)
		SHADER_PARAMETER(FUintVector3, TargetResolution)
		SHADER_PARAMETER(FVector3f, UVScale)
		SHADER_PARAMETER(FVector3f, UVBias)
		SHADER_PARAMETER(uint32, AttributeIndex)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, DensityBias)
		SHADER_PARAMETER(FVector4f, FallbackValueA)
		SHADER_PARAMETER(FVector4f, FallbackValueB)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputDensities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSampleSparseVolumeTextureCS,
	"/Plugin/DualContourMesh/Private/SampleSparseVolumeTexture.usf", "SampleSparseVolumeTextureCS", SF_Compute);

namespace
{
void ComputeUVTransform(ESVTDualContourFit Fit, const FVector3f& TargetSize, const FVector3f& SourceSize,
	FVector3f& OutScale, FVector3f& OutBias)
{
	OutScale = FVector3f(1.f);
	OutBias = FVector3f(0.f);
	if (Fit == ESVTDualContourFit::Fill)
		return;

	const FVector3f SafeSource(FMath::Max(SourceSize.X, 1.f), FMath::Max(SourceSize.Y, 1.f), FMath::Max(SourceSize.Z, 1.f));
	const FVector3f AxisScale(TargetSize.X / SafeSource.X, TargetSize.Y / SafeSource.Y, TargetSize.Z / SafeSource.Z);
	const float UniformScale = Fit == ESVTDualContourFit::Contain
		                           ? FMath::Min3(AxisScale.X, AxisScale.Y, AxisScale.Z)
		                           : FMath::Max3(AxisScale.X, AxisScale.Y, AxisScale.Z);
	const FVector3f DisplayedSize = SafeSource * FMath::Max(UniformScale, UE_SMALL_NUMBER);
	OutScale = FVector3f(TargetSize.X / DisplayedSize.X, TargetSize.Y / DisplayedSize.Y, TargetSize.Z / DisplayedSize.Z);
	OutBias = FVector3f(0.5f) - OutScale * 0.5f;
}
}

bool FSVTDualContourBuilder::Sample(const USVTDualContour& SVTDualContour,
	FDualContourSampledRegion& OutRegion, FText& OutError)
{
	check(IsInGameThread());
	OutRegion.Reset();
	if (!FApp::CanEverRender())
	{
		OutError = NSLOCTEXT("SVTDualContour", "RenderingUnavailable", "Rendering is unavailable in this process.");
		return false;
	}
	if (!SVTDualContour.SourceSparseVolumeTexture)
	{
		OutError = NSLOCTEXT("SVTDualContour", "MissingInput", "A source SVT is required.");
		return false;
	}

	if (SVTDualContour.CellCount.X <= 0 || SVTDualContour.CellCount.Y <= 0 || SVTDualContour.CellCount.Z <= 0
	    || SVTDualContour.CellCount.X >= MAX_int32 || SVTDualContour.CellCount.Y >= MAX_int32
	    || SVTDualContour.CellCount.Z >= MAX_int32)
	{
		OutError = NSLOCTEXT("SVTDualContour", "InvalidResolution", "The density sample resolution is invalid or too large.");
		return false;
	}
	const FIntVector SampleDims = SVTDualContour.CellCount + FIntVector(1);
	if (SampleDims.X > MAX_int32 / SampleDims.Y)
	{
		OutError = NSLOCTEXT("SVTDualContour", "InvalidResolution", "The density sample resolution is invalid or too large.");
		return false;
	}
	const int64 SampleArea = static_cast<int64>(SampleDims.X) * SampleDims.Y;
	if (SampleArea > MAX_int32 / SampleDims.Z)
	{
		OutError = NSLOCTEXT("SVTDualContour", "InvalidResolution", "The density sample resolution is invalid or too large.");
		return false;
	}
	const int64 NumSamples64 = SampleArea * SampleDims.Z;

	UStaticSparseVolumeTexture* Source = SVTDualContour.SourceSparseVolumeTexture;
	USparseVolumeTextureFrame* Frame = USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest(
		Source, GetTypeHash(&SVTDualContour), 0.f, 0.f, 0.f, true, false);
	if (!Frame)
	{
		OutError = NSLOCTEXT("SVTDualContour", "MissingFrame", "The source SVT has no frame to sample.");
		return false;
	}
	Frame->CreateTextureRenderResources();
	UE::SVT::GetStreamingManager().Update_GameThread();
	FlushRenderingCommands();

	const FIntVector SourceResolution = Source->GetVolumeResolution();
	const FVector3f TargetSize(
		SVTDualContour.CellCount.X * SVTDualContour.CellSize,
		SVTDualContour.CellCount.Y * SVTDualContour.CellSize,
		SVTDualContour.CellCount.Z * SVTDualContour.CellSize);
	FVector3f UVScale;
	FVector3f UVBias;
	ComputeUVTransform(SVTDualContour.Fit, TargetSize, FVector3f(SourceResolution), UVScale, UVBias);

	const uint32 NumSamples = static_cast<uint32>(NumSamples64);
	const uint32 NumBytes = NumSamples * sizeof(uint32);
	TSharedRef<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback =
		MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("SVTDualContourReadback"));
	TSharedRef<bool, ESPMode::ThreadSafe> bResourcesValid = MakeShared<bool, ESPMode::ThreadSafe>(true);
	const uint32 AttributeIndex = static_cast<uint32>(SVTDualContour.DensityAttribute);
	const float DensityScale = SVTDualContour.DensityScale;
	const float DensityBias = SVTDualContour.DensityBias;
	const FVector4f FallbackA = Source->GetFallbackValue(0);
	const FVector4f FallbackB = Source->GetFallbackValue(1);

	ENQUEUE_RENDER_COMMAND(SampleSVTDualContour)(
		[Frame, SampleDims, NumSamples, NumBytes, UVScale, UVBias, AttributeIndex, DensityScale, DensityBias,
			FallbackA, FallbackB, Readback, bResourcesValid](FRHICommandListImmediate& RHICmdList)
		{
			const UE::SVT::FTextureRenderResources* Resources = Frame->GetTextureRenderResources();
			if (!Resources || !Resources->GetPageTableTexture())
			{
				*bResourcesValid = false;
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumSamples), TEXT("SVTDualContour.Output"));
			FSampleSparseVolumeTextureCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FSampleSparseVolumeTextureCS::FParameters>();
			Parameters->TileDataTextureSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->SparseVolumeTexturePageTable = Resources->GetPageTableTexture();
			Parameters->SparseVolumeTextureA = Resources->GetPhysicalTileDataATexture()
				                                   ? Resources->GetPhysicalTileDataATexture()
				                                   : GBlackVolumeTexture->TextureRHI.GetReference();
			Parameters->SparseVolumeTextureB = Resources->GetPhysicalTileDataBTexture()
				                                   ? Resources->GetPhysicalTileDataBTexture()
				                                   : GBlackVolumeTexture->TextureRHI.GetReference();
			Resources->GetPackedUniforms(Parameters->PackedSVTUniforms0, Parameters->PackedSVTUniforms1);
			Parameters->TargetResolution = FUintVector3(SampleDims.X, SampleDims.Y, SampleDims.Z);
			Parameters->UVScale = UVScale;
			Parameters->UVBias = UVBias;
			Parameters->AttributeIndex = AttributeIndex;
			Parameters->DensityScale = DensityScale;
			Parameters->DensityBias = DensityBias;
			Parameters->FallbackValueA = FallbackA;
			Parameters->FallbackValueB = FallbackB;
			Parameters->OutputDensities = GraphBuilder.CreateUAV(OutputBuffer);

			TShaderMapRef<FSampleSparseVolumeTextureCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Sample SVT Dual Contour"), ComputeShader, Parameters,
				FIntVector(FMath::DivideAndRoundUp(SampleDims.X, 4), FMath::DivideAndRoundUp(SampleDims.Y, 4),
					FMath::DivideAndRoundUp(SampleDims.Z, 4)));
			AddEnqueueCopyPass(GraphBuilder, &Readback.Get(), OutputBuffer, NumBytes);
			GraphBuilder.Execute();
		});

	FlushRenderingCommands();
	// The render command has been submitted, but the GPU fence may complete shortly afterwards.
	// This is one bounded wait for the whole bake, never a per-voxel synchronization.
	const double ReadbackDeadline = FPlatformTime::Seconds() + 10.0;
	while (*bResourcesValid && !Readback->IsReady() && FPlatformTime::Seconds() < ReadbackDeadline)
		FPlatformProcess::SleepNoStats(0.001f);
	if (!*bResourcesValid || !Readback->IsReady())
	{
		OutError = NSLOCTEXT("SVTDualContour", "GPUResourcesUnavailable", "The source SVT GPU resources are not ready.");
		return false;
	}

	TSharedRef<TArray<uint32>, ESPMode::ThreadSafe> ReadbackValues =
		MakeShared<TArray<uint32>, ESPMode::ThreadSafe>();
	ENQUEUE_RENDER_COMMAND(ReadSVTDualContour)(
		[Readback, ReadbackValues, NumBytes, NumSamples](FRHICommandListImmediate& RHICmdList)
		{
			const uint32* Values = static_cast<const uint32*>(Readback->Lock(NumBytes));
			ReadbackValues->Append(Values, NumSamples);
			Readback->Unlock();
		});
	FlushRenderingCommands();

	TRACE_CPUPROFILER_EVENT_SCOPE(SVTDualContour_PackDensityChunks);
	OutRegion.SampleMin = FIntVector::ZeroValue;
	OutRegion.SampleDimensions = SampleDims;
	const FIntVector ChunkDimensions(
		FMath::DivideAndRoundUp(SampleDims.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleDims.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleDims.Z, GDualContourChunkSize));
	const int32 ChunkArea = ChunkDimensions.X * ChunkDimensions.Y;
	const int32 ChunkCount = ChunkArea * ChunkDimensions.Z;
	OutRegion.Chunks.SetNum(ChunkCount);
	ParallelFor(TEXT("SVTDualContour.PackDensityChunks"), ChunkCount, 1,
		[&OutRegion, ReadbackValues, SampleDims, ChunkDimensions, ChunkArea](int32 Index)
		{
			const int32 ChunkZ = Index / ChunkArea;
			const int32 Remainder = Index - ChunkZ * ChunkArea;
			const int32 ChunkY = Remainder / ChunkDimensions.X;
			const int32 ChunkX = Remainder - ChunkY * ChunkDimensions.X;
			FDualContourSampledChunk& SampledChunk = OutRegion.Chunks[Index];
			SampledChunk.ChunkCoord = FIntVector(ChunkX, ChunkY, ChunkZ);

			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(SampledChunk.ChunkCoord);
			const FIntVector BuildMax(
				FMath::Min(SampleDims.X, ChunkOrigin.X + GDualContourChunkSize),
				FMath::Min(SampleDims.Y, ChunkOrigin.Y + GDualContourChunkSize),
				FMath::Min(SampleDims.Z, ChunkOrigin.Z + GDualContourChunkSize));
			bool bExpanded = false;
			for (int32 SampleZ = ChunkOrigin.Z; SampleZ < BuildMax.Z; ++SampleZ)
				for (int32 SampleY = ChunkOrigin.Y; SampleY < BuildMax.Y; ++SampleY)
					for (int32 SampleX = ChunkOrigin.X; SampleX < BuildMax.X; ++SampleX)
					{
						const int32 SourceIndex = DualContourUtils::LinearIndex(SampleDims, SampleX, SampleY, SampleZ);
						const uint8 Density = static_cast<uint8>(FMath::Min((*ReadbackValues)[SourceIndex], 255u));
						if (Density == 0)
							continue;
						if (!bExpanded)
						{
							SampledChunk.Density.Expand();
							bExpanded = true;
						}
						SampledChunk.Density.DensitySamples[DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ)] = Density;
					}
			if (bExpanded)
				SampledChunk.Density.TryCollapse();
		}, EParallelForFlags::Unbalanced);

	OutRegion.Chunks.RemoveAllSwap([](const FDualContourSampledChunk& Chunk)
	{
		return Chunk.Density.IsUniform() && Chunk.Density.UniformValue == 0;
	}, EAllowShrinking::No);
	return true;
}

#endif
