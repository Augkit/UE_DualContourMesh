#include "VolumeSampler/VolumeSampler.h"

FBox UVolumeSampler::GetBounds() const
{
	if (SamplingTransform.ContainsNaN() || SamplingTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER ||
	    VolumeSize.ContainsNaN() || VolumeSize.GetMin() <= UE_SMALL_NUMBER || Pivot.ContainsNaN())
		return FBox(ForceInit);
	const FVector PivotPosition = Pivot * VolumeSize;
	FBox Bounds(ForceInit);
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector BaseVolumeCorner(
			(CornerIndex & 1) ? VolumeSize.X : 0,
			(CornerIndex & 2) ? VolumeSize.Y : 0,
			(CornerIndex & 4) ? VolumeSize.Z : 0);
		Bounds += PivotPosition + SamplingTransform.TransformPosition(BaseVolumeCorner - PivotPosition);
	}
	return Bounds;
}

bool UVolumeSampler::TryGetNormalizedVolumePosition(
	const FVector& SamplerInputPosition, FVector& OutNormalizedVolumePosition) const
{
	const FVector PivotPosition = Pivot * VolumeSize;
	const FVector BaseVolumePosition = PivotPosition
	                                   + SamplingTransform.InverseTransformPosition(SamplerInputPosition - PivotPosition);
	OutNormalizedVolumePosition = BaseVolumePosition / VolumeSize;
	return !OutNormalizedVolumePosition.ContainsNaN()
	       && OutNormalizedVolumePosition.GetMin() >= 0
	       && OutNormalizedVolumePosition.GetMax() <= 1;
}

bool UVolumeSampler::Prepare(FText& OutError) const
{
	if (VolumeSize.ContainsNaN() || Pivot.ContainsNaN() || VolumeSize.X <= UE_SMALL_NUMBER || VolumeSize.Y <= UE_SMALL_NUMBER ||
	    VolumeSize.Z <= UE_SMALL_NUMBER)
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
