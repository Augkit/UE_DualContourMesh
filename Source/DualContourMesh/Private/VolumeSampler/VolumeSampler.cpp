#include "VolumeSampler/VolumeSampler.h"
#include "DualContour.h"
#include "DualContourUtils.h"
#include "Async/ParallelFor.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UVolumeSampler::Prepare(FText& OutError) const
{
	if (VolumeSize.X <= UE_SMALL_NUMBER || VolumeSize.Y <= UE_SMALL_NUMBER || VolumeSize.Z <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidVolumeSize", "VolumeSize must be positive on every axis.");
		return false;
	}
	return true;
}

void UVolumeSampler::Finish() const {}

#if WITH_EDITOR
void UVolumeSampler::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnPropertyChanged.Broadcast();
}

void UVolumeSampler::PostEditUndo()
{
	Super::PostEditUndo();
	OnPropertyChanged.Broadcast();
}
#endif

bool UVolumeSampler::BuildDensityChunks(UDualContour* Target, const FTransform& SampleTransform,
	FDualContourSampledRegion& OutRegion, FText& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_BuildDensityChunks);
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
	if (FMath::Abs(TransformScale.X) <= UE_SMALL_NUMBER || FMath::Abs(TransformScale.Y) <= UE_SMALL_NUMBER
	    || FMath::Abs(TransformScale.Z) <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidTransformScale", "SampleTransform scale must be non-zero on every axis.");
		return false;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_Prepare);
		if (!Prepare(OutError))
			return false;
	}
	ON_SCOPE_EXIT
	{
		Finish();
	};

	const FVector PivotPosition = Pivot * VolumeSize;
	FBox TransformedBounds(ForceInit);
	for (int32 Z = 0; Z <= 1; ++Z)
		for (int32 Y = 0; Y <= 1; ++Y)
			for (int32 X = 0; X <= 1; ++X)
			{
				const FVector Corner(X * VolumeSize.X, Y * VolumeSize.Y, Z * VolumeSize.Z);
				TransformedBounds += PivotPosition + SampleTransform.TransformPosition(Corner - PivotPosition);
			}

	const FVector TargetMax = FVector(Target->CellCount.X, Target->CellCount.Y, Target->CellCount.Z) * Target->CellSize;
	if (TransformedBounds.Max.X < 0.0 || TransformedBounds.Max.Y < 0.0 || TransformedBounds.Max.Z < 0.0
	    || TransformedBounds.Min.X > TargetMax.X || TransformedBounds.Min.Y > TargetMax.Y
	    || TransformedBounds.Min.Z > TargetMax.Z)
		return true;

	const FVector ClippedMin(
		FMath::Clamp(TransformedBounds.Min.X, 0.0, TargetMax.X),
		FMath::Clamp(TransformedBounds.Min.Y, 0.0, TargetMax.Y),
		FMath::Clamp(TransformedBounds.Min.Z, 0.0, TargetMax.Z));
	const FVector ClippedMax(
		FMath::Clamp(TransformedBounds.Max.X, 0.0, TargetMax.X),
		FMath::Clamp(TransformedBounds.Max.Y, 0.0, TargetMax.Y),
		FMath::Clamp(TransformedBounds.Max.Z, 0.0, TargetMax.Z));

	// Include at most one lattice point beyond the mathematical AABB. UVW validation
	// still leaves samples outside the actual transformed volume at zero.
	OutRegion.SampleMin = FIntVector(
		FMath::Clamp(FMath::FloorToInt(ClippedMin.X / Target->CellSize), 0, Target->CellCount.X),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Y / Target->CellSize), 0, Target->CellCount.Y),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Z / Target->CellSize), 0, Target->CellCount.Z));
	const FIntVector SampleMax(
		FMath::Clamp(FMath::CeilToInt(ClippedMax.X / Target->CellSize) + 1, 0, Target->CellCount.X + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Y / Target->CellSize) + 1, 0, Target->CellCount.Y + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Z / Target->CellSize) + 1, 0, Target->CellCount.Z + 1));
	OutRegion.SampleDimensions = SampleMax - OutRegion.SampleMin;

	const FIntVector ChunkMin(OutRegion.SampleMin.X / GDualContourChunkSize,
		OutRegion.SampleMin.Y / GDualContourChunkSize, OutRegion.SampleMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
	const FIntVector ChunkDimensions = ChunkMaxExclusive - ChunkMin;
	if (ChunkDimensions.X > MAX_int32 / ChunkDimensions.Y)
	{
		OutError = NSLOCTEXT("VolumeSampler", "SampleRangeTooLarge", "The transformed volume's affected chunk range exceeds TArray capacity.");
		return false;
	}
	const int64 ChunkArea = static_cast<int64>(ChunkDimensions.X) * ChunkDimensions.Y;
	if (ChunkArea > MAX_int32 / ChunkDimensions.Z)
	{
		OutError = NSLOCTEXT("VolumeSampler", "SampleRangeTooLarge", "The transformed volume's affected chunk range exceeds TArray capacity.");
		return false;
	}
	const int32 ChunkCount = static_cast<int32>(ChunkArea * ChunkDimensions.Z);
	OutRegion.Chunks.SetNum(ChunkCount);

	const FVector Translation = SampleTransform.GetTranslation();
	const float TargetCellSize = Target->CellSize;
	const auto SampleChunk = [this, &OutRegion, ChunkMin, ChunkDimensions, ChunkArea, TargetCellSize,
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
		const FIntVector BuildMin(
			FMath::Max(OutRegion.SampleMin.X, ChunkOrigin.X),
			FMath::Max(OutRegion.SampleMin.Y, ChunkOrigin.Y),
			FMath::Max(OutRegion.SampleMin.Z, ChunkOrigin.Z));
		const FIntVector BuildMax(
			FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
			FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
			FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

		bool bExpanded = false;
		for (int32 SampleZ = BuildMin.Z; SampleZ < BuildMax.Z; ++SampleZ)
			for (int32 SampleY = BuildMin.Y; SampleY < BuildMax.Y; ++SampleY)
				for (int32 SampleX = BuildMin.X; SampleX < BuildMax.X; ++SampleX)
				{
					const FVector TargetPosition(
						static_cast<double>(SampleX) * TargetCellSize,
						static_cast<double>(SampleY) * TargetCellSize,
						static_cast<double>(SampleZ) * TargetCellSize);
					const FVector Untransformed =
						PivotPosition + SampleTransform.InverseTransformVector(TargetPosition - PivotPosition - Translation);
					const FVector UVW = Untransformed / VolumeSize;
					float LinearDensity = GDualContourMinLinearDensity;
					if (UVW.X >= 0.0 && UVW.X <= 1.0 && UVW.Y >= 0.0 && UVW.Y <= 1.0 && UVW.Z >= 0.0 && UVW.Z <= 1.0)
						LinearDensity = SampleNormalized(UVW);
					const int32 LocalIndex = DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ);
					const uint16 Density = FDensityChunk::EncodeDensity(LinearDensity);
					if (Density == 0)
						continue;
					if (!bExpanded)
					{
						SampledChunk.Density.Expand();
						bExpanded = true;
					}
					SampledChunk.Density.DensitySamples[LocalIndex] = Density;
				}
		if (bExpanded)
			SampledChunk.Density.TryCollapse();
	};

	if (SupportsParallelSampling())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ParallelSampleDensityChunks);
		// Workers only read prepared sampler state and write their own preallocated chunk.
		ParallelFor(TEXT("VolumeSampler.SampleDensityChunks"), ChunkCount, 1, SampleChunk, EParallelForFlags::Unbalanced);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_SerialSampleDensityChunks);
		for (int32 Index = 0; Index < ChunkCount; ++Index)
			SampleChunk(Index);
	}

	// Zero chunks are neutral for both union and difference and need not survive the handoff.
	OutRegion.Chunks.RemoveAllSwap([](const FDualContourSampledChunk& Chunk)
	{
		return Chunk.Density.IsUniform() && Chunk.Density.UniformValue == 0;
	}, EAllowShrinking::No);
	return true;
}

bool UVolumeSampler::ReplaceDualContour(UDualContour* Target, const FTransform& SampleTransform, FText& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ReplaceDualContour);
	FDualContourSampledRegion SampledRegion;
	return BuildDensityChunks(Target, SampleTransform, SampledRegion, OutError) && Target->ReplaceDensityChunks(MoveTemp(SampledRegion));
}

bool UVolumeSampler::ModifyDualContour(UDualContour* Target, const FTransform& SampleTransform, bool bExcavate,
	FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax, FText& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ModifyDualContour);
	OutAffectedCellMin = FIntVector::ZeroValue;
	OutAffectedCellMax = FIntVector::ZeroValue;
	FDualContourSampledRegion SampledRegion;
	return BuildDensityChunks(Target, SampleTransform, SampledRegion, OutError)
	       && Target->ModifyDensityChunks(SampledRegion, bExcavate, OutAffectedCellMin, OutAffectedCellMax);
}
