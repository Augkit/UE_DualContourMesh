#include "VolumeSampler/VolumeSampler.h"
#include "DualContour.h"
#include "VolumeSampledDualContour.h"
#include "Misc/ScopeExit.h"

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
	if (UVolumeSampledDualContour* Owner = GetTypedOuter<UVolumeSampledDualContour>(); Owner && !Owner->IsTemplate())
		Owner->NotifySamplerChanged();
}
#endif

bool UVolumeSampler::BuildDensitySamples(UDualContour* Target, const FTransform& SampleTransform,
	FVectorInt& OutSampleMin, FVectorInt& OutSampleDimensions,
	TArray<uint8>& OutSamples, FText& OutError) const
{
	OutSampleMin = FVectorInt();
	OutSampleDimensions = FVectorInt();
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

	if (!Prepare(OutError))
		return false;
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
	OutSampleMin = FVectorInt(
		FMath::Clamp(FMath::FloorToInt(ClippedMin.X / Target->CellSize), 0, Target->CellCount.X),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Y / Target->CellSize), 0, Target->CellCount.Y),
		FMath::Clamp(FMath::FloorToInt(ClippedMin.Z / Target->CellSize), 0, Target->CellCount.Z));
	const FVectorInt SampleMax(
		FMath::Clamp(FMath::CeilToInt(ClippedMax.X / Target->CellSize) + 1, 0, Target->CellCount.X + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Y / Target->CellSize) + 1, 0, Target->CellCount.Y + 1),
		FMath::Clamp(FMath::CeilToInt(ClippedMax.Z / Target->CellSize) + 1, 0, Target->CellCount.Z + 1));
	OutSampleDimensions = FVectorInt(SampleMax.X - OutSampleMin.X, SampleMax.Y - OutSampleMin.Y,
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
	for (int32 Z = 0; Z < OutSampleDimensions.Z; ++Z)
		for (int32 Y = 0; Y < OutSampleDimensions.Y; ++Y)
			for (int32 X = 0; X < OutSampleDimensions.X; ++X)
			{
				const int32 SampleX = OutSampleMin.X + X;
				const int32 SampleY = OutSampleMin.Y + Y;
				const int32 SampleZ = OutSampleMin.Z + Z;
				const FVector TargetPosition = Target->GetSampleLocalPosition(SampleX, SampleY, SampleZ);
				const FVector Untransformed =
					PivotPosition + SampleTransform.InverseTransformVector(TargetPosition - PivotPosition - Translation);
				const FVector UVW = Untransformed / VolumeSize;
				float Density = 0.0f;
				if (UVW.X >= 0.0 && UVW.X <= 1.0 && UVW.Y >= 0.0 && UVW.Y <= 1.0 && UVW.Z >= 0.0 && UVW.Z <= 1.0)
					Density = SampleNormalized(UVW);
				OutSamples[OutSampleDimensions.LinearIndex(X, Y, Z)] = static_cast<uint8>(
					FMath::RoundToInt(FMath::Clamp(Density, 0.0f, 255.0f)));
			}

	return true;
}

bool UVolumeSampler::ReplaceDualContour(UDualContour* Target, const FTransform& SampleTransform, FText& OutError)
{
	FVectorInt SampleMin;
	FVectorInt SampleDimensions;
	TArray<uint8> Samples;
	return BuildDensitySamples(Target, SampleTransform, SampleMin, SampleDimensions, Samples, OutError)
	       && Target->ReplaceDensitySamplesInRange(SampleMin, SampleDimensions, Samples);
}

bool UVolumeSampler::ModifyDualContour(UDualContour* Target, const FTransform& SampleTransform, bool bExcavate,
	FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax, FText& OutError)
{
	OutAffectedCellMin = FVectorInt();
	OutAffectedCellMax = FVectorInt();
	FVectorInt SampleMin;
	FVectorInt SampleDimensions;
	TArray<uint8> Samples;
	return BuildDensitySamples(Target, SampleTransform, SampleMin, SampleDimensions, Samples, OutError)
	       && Target->ModifyDensitySamplesInRange(SampleMin, SampleDimensions, Samples, bExcavate,
		       OutAffectedCellMin, OutAffectedCellMax);
}
