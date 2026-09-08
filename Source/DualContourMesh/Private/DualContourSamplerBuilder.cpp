#include "DualContourSamplerBuilder.h"

#include "Async/ParallelFor.h"
#include "DualContour.h"
#include "DualContourUtils.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VolumeSampler/VolumeSampler.h"

bool FDualContourSamplerBuilder::BuildDensityChunks(const UVolumeSampler& Sampler, UDualContour* Target,
	const FTransform& SampleTransform, FDualContourSampledRegion& OutRegion, FText& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourSamplerBuilder_BuildDensityChunks);
	check(IsInGameThread());
	OutRegion.Reset();
	if (!Target || Target->CellCount.X <= 0 || Target->CellCount.Y <= 0 || Target->CellCount.Z <= 0
	    || Target->CellCount.X >= MAX_int32 || Target->CellCount.Y >= MAX_int32 || Target->CellCount.Z >= MAX_int32
	    || Target->CellSize <= 0.0f)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidTarget", "The target DualContour grid settings are invalid.");
		return false;
	}
	const FVector TransformScale = SampleTransform.GetScale3D();
	if (SampleTransform.ContainsNaN() || TransformScale.GetAbs().GetMin() <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidTransformScale", "SampleTransform scale must be non-zero on every axis.");
		return false;
	}
	if (!Sampler.BeginSampling(OutError))
		return false;
	ON_SCOPE_EXIT { Sampler.EndSampling(); };

	const FVector PivotPosition = Sampler.Pivot * Sampler.VolumeSize;
	const FBox SourceBounds = Sampler.GetBounds();
	if (!SourceBounds.IsValid)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidBounds", "Sampler bounds are invalid.");
		return false;
	}
	FBox TransformedBounds(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector C((Corner & 1) ? SourceBounds.Max.X : SourceBounds.Min.X,
			(Corner & 2) ? SourceBounds.Max.Y : SourceBounds.Min.Y,
			(Corner & 4) ? SourceBounds.Max.Z : SourceBounds.Min.Z);
		TransformedBounds += PivotPosition + SampleTransform.TransformPosition(C - PivotPosition);
	}

	const FVector TargetMax = FVector(Target->CellCount) * Target->CellSize;
	if (TransformedBounds.Max.X < 0.0 || TransformedBounds.Max.Y < 0.0 || TransformedBounds.Max.Z < 0.0
	    || TransformedBounds.Min.X > TargetMax.X || TransformedBounds.Min.Y > TargetMax.Y || TransformedBounds.Min.Z > TargetMax.Z)
		return true;
	const FVector ClippedMin(FMath::Clamp(TransformedBounds.Min.X, 0.0, TargetMax.X),
		FMath::Clamp(TransformedBounds.Min.Y, 0.0, TargetMax.Y), FMath::Clamp(TransformedBounds.Min.Z, 0.0, TargetMax.Z));
	const FVector ClippedMax(FMath::Clamp(TransformedBounds.Max.X, 0.0, TargetMax.X),
		FMath::Clamp(TransformedBounds.Max.Y, 0.0, TargetMax.Y), FMath::Clamp(TransformedBounds.Max.Z, 0.0, TargetMax.Z));
	OutRegion.SampleMin = FIntVector(FMath::Clamp(FMath::FloorToInt(ClippedMin.X / Target->CellSize), 0, Target->CellCount.X),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Y / Target->CellSize), 0, Target->CellCount.Y),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Z / Target->CellSize), 0, Target->CellCount.Z));
	const FIntVector SampleMax(FMath::Clamp(FMath::CeilToInt(ClippedMax.X / Target->CellSize) + 1, 0, Target->CellCount.X + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Y / Target->CellSize) + 1, 0, Target->CellCount.Y + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Z / Target->CellSize) + 1, 0, Target->CellCount.Z + 1));
	OutRegion.SampleDimensions = SampleMax - OutRegion.SampleMin;

	const FIntVector ChunkMin(OutRegion.SampleMin.X / GDualContourChunkSize, OutRegion.SampleMin.Y / GDualContourChunkSize,
		OutRegion.SampleMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize), FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
	const FIntVector ChunkDimensions = ChunkMaxExclusive - ChunkMin;
	const int64 ChunkArea = static_cast<int64>(ChunkDimensions.X) * ChunkDimensions.Y;
	if (ChunkDimensions.X <= 0 || ChunkDimensions.Y <= 0 || ChunkDimensions.Z <= 0 || ChunkArea > MAX_int32
	    || ChunkArea > MAX_int32 / ChunkDimensions.Z)
	{
		OutError = NSLOCTEXT("VolumeSampler", "SampleRangeTooLarge", "The transformed volume's affected chunk range exceeds TArray capacity.");
		return false;
	}
	const int32 ChunkCount = static_cast<int32>(ChunkArea * ChunkDimensions.Z);
	OutRegion.Chunks.SetNum(ChunkCount);

	const FVector Translation = SampleTransform.GetTranslation();
	const float TargetCellSize = Target->CellSize;
	const auto SampleChunk = [&Sampler, &OutRegion, ChunkMin, ChunkDimensions, ChunkArea, TargetCellSize,
			PivotPosition, SampleTransform, Translation](int32 Index)
	{
		const int32 ChunkZ = static_cast<int32>(Index / ChunkArea);
		const int32 Remainder = static_cast<int32>(Index - static_cast<int64>(ChunkZ) * ChunkArea);
		const int32 ChunkY = Remainder / ChunkDimensions.X;
		const int32 ChunkX = Remainder - ChunkY * ChunkDimensions.X;
		FDualContourSampledChunk& SampledChunk = OutRegion.Chunks[Index];
		SampledChunk.ChunkCoord = ChunkMin + FIntVector(ChunkX, ChunkY, ChunkZ);
		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(SampledChunk.ChunkCoord);
		const FIntVector SampleMax = OutRegion.SampleMin + OutRegion.SampleDimensions;
		const FIntVector BuildMin(FMath::Max(OutRegion.SampleMin.X, ChunkOrigin.X), FMath::Max(OutRegion.SampleMin.Y, ChunkOrigin.Y),
			FMath::Max(OutRegion.SampleMin.Z, ChunkOrigin.Z));
		const FIntVector BuildMax(FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
			FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize), FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));
		bool bExpanded = false;
		for (int32 Z = BuildMin.Z; Z < BuildMax.Z; ++Z)
			for (int32 Y = BuildMin.Y; Y < BuildMax.Y; ++Y)
				for (int32 X = BuildMin.X; X < BuildMax.X; ++X)
				{
					const FVector TargetPosition(static_cast<double>(X) * TargetCellSize, static_cast<double>(Y) * TargetCellSize,
						static_cast<double>(Z) * TargetCellSize);
					const FVector Untransformed = PivotPosition + SampleTransform.
					                              InverseTransformVector(TargetPosition - PivotPosition - Translation);
					float LinearDensity = GDualContourMinLinearDensity, Weight = 0.0f;
					if (!Sampler.Sample(Untransformed, LinearDensity, Weight) || !FMath::IsFinite(Weight) || Weight <= 0.0f)
						continue;
					const uint16 Density = FDensityChunk::EncodeDensity(LinearDensity);
					if (Density == 0)
						continue;
					if (!bExpanded)
					{
						SampledChunk.Density.Expand();
						bExpanded = true;
					}
					SampledChunk.Density.DensitySamples[DualContourUtils::ChunkLocalIndex(X, Y, Z)] = Density;
				}
		if (bExpanded)
			SampledChunk.Density.TryCollapse();
	};

	if (Sampler.CanSampleInParallel())
		ParallelFor(TEXT("VolumeSampler.SampleDensityChunks"), ChunkCount, 1, SampleChunk, EParallelForFlags::Unbalanced);
	else
		for (int32 Index = 0; Index < ChunkCount; ++Index)
			SampleChunk(Index);

	OutRegion.Chunks.RemoveAllSwap([](const FDualContourSampledChunk& Chunk)
	{
		return Chunk.Density.IsUniform() && Chunk.Density.UniformValue == 0;
	}, EAllowShrinking::No);
	return true;
}
