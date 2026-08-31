#include "VolumeSampler/VolumeSampler.h"
#include "DualContour.h"
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

bool UVolumeSampler::BuildDensitySamples(UDualContour* Target, const FTransform& SampleTransform,
	FIntVector& OutSampleMin, FIntVector& OutSampleDimensions,
	TArray<uint8>& OutSamples, FText& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_BuildDensitySamples);
	check(IsInGameThread());
	OutSampleMin = FIntVector::ZeroValue;
	OutSampleDimensions = FIntVector::ZeroValue;
	OutSamples.Reset();
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

	// Floor/ceil intentionally include at most one lattice point beyond the mathematical AABB.
	// This keeps the range conservative in the presence of transform floating-point error; UVW
	// validation below still writes zero for points outside the actual transformed volume.
	OutSampleMin = FIntVector(
		FMath::Clamp(FMath::FloorToInt(ClippedMin.X / Target->CellSize), 0, Target->CellCount.X),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Y / Target->CellSize), 0, Target->CellCount.Y),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Z / Target->CellSize), 0, Target->CellCount.Z));
	const FIntVector SampleMax(
		FMath::Clamp(FMath::CeilToInt(ClippedMax.X / Target->CellSize) + 1, 0, Target->CellCount.X + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Y / Target->CellSize) + 1, 0, Target->CellCount.Y + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Z / Target->CellSize) + 1, 0, Target->CellCount.Z + 1));
	OutSampleDimensions = FIntVector(SampleMax.X - OutSampleMin.X, SampleMax.Y - OutSampleMin.Y,
		SampleMax.Z - OutSampleMin.Z);
	if (OutSampleDimensions.X > MAX_int32 / OutSampleDimensions.Y)
	{
		OutError = NSLOCTEXT("VolumeSampler", "SampleRangeTooLarge",
			"The transformed volume's affected sample range exceeds TArray capacity.");
		return false;
	}
	const int64 SampleArea = static_cast<int64>(OutSampleDimensions.X) * OutSampleDimensions.Y;
	if (SampleArea > MAX_int32 / OutSampleDimensions.Z)
	{
		OutError = NSLOCTEXT("VolumeSampler", "SampleRangeTooLarge",
			"The transformed volume's affected sample range exceeds TArray capacity.");
		return false;
	}
	const int32 SampleCount = static_cast<int32>(SampleArea * OutSampleDimensions.Z);

	const FVector Translation = SampleTransform.GetTranslation();
	OutSamples.SetNumUninitialized(SampleCount);
	{
		const int32 SampleRowSize = OutSampleDimensions.X;
		const int32 SampleRowCount = OutSampleDimensions.Y * OutSampleDimensions.Z;
		const float TargetCellSize = Target->CellSize;
		const auto SampleRow = [this, &OutSamples, OutSampleMin, OutSampleDimensions, SampleRowSize,
			TargetCellSize, PivotPosition, SampleTransform, Translation](int32 Index)
		{
			const int32 Z = Index / OutSampleDimensions.Y;
			const int32 Y = Index - Z * OutSampleDimensions.Y;
			const int32 OutputRowStart = Index * SampleRowSize;
			for (int32 X = 0; X < SampleRowSize; ++X)
			{
				const FVector TargetPosition(
					static_cast<double>(OutSampleMin.X + X) * TargetCellSize,
					static_cast<double>(OutSampleMin.Y + Y) * TargetCellSize,
					static_cast<double>(OutSampleMin.Z + Z) * TargetCellSize);
				const FVector Untransformed =
					PivotPosition + SampleTransform.InverseTransformVector(TargetPosition - PivotPosition - Translation);
				const FVector UVW = Untransformed / VolumeSize;
				float Density = 0.0f;
				if (UVW.X >= 0.0 && UVW.X <= 1.0 && UVW.Y >= 0.0 && UVW.Y <= 1.0 && UVW.Z >= 0.0 && UVW.Z <= 1.0)
					Density = SampleNormalized(UVW);
				OutSamples[OutputRowStart + X] = static_cast<uint8>(
					FMath::RoundToInt(FMath::Clamp(Density, 0.0f, 255.0f)));
			}
		};

		if (SupportsParallelSampling())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ParallelSampleDensity);
			// As in UDualContour::RebuildCellsInRange, the calling thread stays blocked;
			// workers only read prepared sampler state and write disjoint output elements.
			ParallelFor(TEXT("VolumeSampler.SampleDensity"), SampleRowCount, 8, SampleRow, EParallelForFlags::Unbalanced);
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_SerialSampleDensity);
			for (int32 Index = 0; Index < SampleRowCount; ++Index)
				SampleRow(Index);
		}
	}

	return true;
}

bool UVolumeSampler::ReplaceDualContour(UDualContour* Target, const FTransform& SampleTransform, FText& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ReplaceDualContour);
	FIntVector SampleMin = FIntVector::ZeroValue;
	FIntVector SampleDimensions = FIntVector::ZeroValue;
	TArray<uint8> Samples;
	return BuildDensitySamples(Target, SampleTransform, SampleMin, SampleDimensions, Samples, OutError)
	       && Target->ReplaceDensitySamplesInRange(SampleMin, SampleDimensions, Samples);
}

bool UVolumeSampler::ModifyDualContour(UDualContour* Target, const FTransform& SampleTransform, bool bExcavate,
	FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax, FText& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampler_ModifyDualContour);
	OutAffectedCellMin = FIntVector::ZeroValue;
	OutAffectedCellMax = FIntVector::ZeroValue;
	FIntVector SampleMin = FIntVector::ZeroValue;
	FIntVector SampleDimensions = FIntVector::ZeroValue;
	TArray<uint8> Samples;
	return BuildDensitySamples(Target, SampleTransform, SampleMin, SampleDimensions, Samples, OutError)
	       && Target->ModifyDensitySamplesInRange(SampleMin, SampleDimensions, Samples, bExcavate,
		       OutAffectedCellMin, OutAffectedCellMax);
}
