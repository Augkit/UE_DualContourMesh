#include "VolumeSampler/VolumeSampler.h"
#include "Math/TranslationMatrix.h"
#include "Math/ScaleMatrix.h"

FBox UVolumeSampler::GetSamplingBounds(const FVector& SamplingVolumeSize) const
{
	if (SamplingVolumeSize.ContainsNaN() || SamplingVolumeSize.GetMin() <= UE_SMALL_NUMBER || Pivot.ContainsNaN())
		return FBox(ForceInit);
	return FBox(FVector::ZeroVector, SamplingVolumeSize);
}

FBox UVolumeSampler::TransformBoxByPivotTransform(const FBox& Box, const FTransform& SamplerPivotTransform, const FVector& SamplingVolumeSize) const
{
	const FVector SamplerPivotPosition = Pivot * SamplingVolumeSize;
	FBox Result(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector InputCorner(
			(Corner & 1) ? Box.Max.X : Box.Min.X,
			(Corner & 2) ? Box.Max.Y : Box.Min.Y,
			(Corner & 4) ? Box.Max.Z : Box.Min.Z);
		Result += SamplerPivotTransform.TransformPosition(InputCorner - SamplerPivotPosition);
	}
	return Result;
}

FVolumeSamplerPlacement UVolumeSampler::MakePlacement(const FVector& SamplingVolumeSize, const FTransform* SamplerPivotTransform,
	float MaxTargetDensitySlope) const
{
	FVolumeSamplerPlacement Placement;
	Placement.MaxTargetDensitySlope = MaxTargetDensitySlope;
	Placement.SamplingVolumeSize = SamplingVolumeSize;
	const FVector PivotPosition = Pivot * SamplingVolumeSize;
	// The normalized volume is converted to target-local units before the placement transform.
	// Sampling then needs only one target-local -> normalized affine per sample.
	const FTransform SamplerPivot = SamplerPivotTransform
		                                ? *SamplerPivotTransform
		                                : FTransform(FQuat::Identity, PivotPosition);
	const FMatrix SamplerPivotMatrix = SamplerPivot.ToMatrixWithScale();
	const FMatrix NormalizedToVolume = FScaleMatrix(SamplingVolumeSize);
	const FMatrix ShiftFromPivot = FTranslationMatrix(-PivotPosition);
	// FMatrix/FVector use row-vector composition: v * A * B applies A, then B.
	// SamplerPivotTransform.Location is the target-space position of the sampler pivot.
	// Forward: normalized sampler space -> volume units relative to pivot -> target-local placement.
	Placement.TargetToSamplerNormalizedMatrix = (NormalizedToVolume * ShiftFromPivot * SamplerPivotMatrix).Inverse();
	return Placement;
}

bool UVolumeSampler::TryGetNormalizedPosition(
	const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
	FVector& OutNormalizedPosition) const
{
	const FVector NormalizedPosition = Placement.TargetToSamplerNormalizedMatrix.TransformPosition(TargetLocalPosition);
	OutNormalizedPosition = NormalizedPosition;
	return !NormalizedPosition.ContainsNaN()
	       && NormalizedPosition.X >= 0.0 && NormalizedPosition.Y >= 0.0 && NormalizedPosition.Z >= 0.0
	       && NormalizedPosition.X <= 1.0 && NormalizedPosition.Y <= 1.0 && NormalizedPosition.Z <= 1.0;
}

bool UVolumeSampler::Prepare(FText& OutError) const
{
	if (Pivot.ContainsNaN() || Pivot.X < 0.0 || Pivot.Y < 0.0 || Pivot.Z < 0.0 ||
	    Pivot.X > 1.0 || Pivot.Y > 1.0 || Pivot.Z > 1.0)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidPivot", "Pivot must be finite and within the normalized volume.");
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
