#include "VolumeSampler/VolumeSampler.h"

FBox UVolumeSampler::GetBounds() const
{
	if (SamplingTransform.ContainsNaN() || SamplingTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER ||
	    VolumeSize.ContainsNaN() || VolumeSize.GetMin() <= UE_SMALL_NUMBER || Pivot.ContainsNaN())
		return FBox(ForceInit);
	const FVector P = Pivot * VolumeSize;
	FBox Bounds(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector C((Corner & 1) ? VolumeSize.X : 0, (Corner & 2) ? VolumeSize.Y : 0, (Corner & 4) ? VolumeSize.Z : 0);
		Bounds += P + SamplingTransform.TransformPosition(C - P);
	}
	return Bounds;
}

bool UVolumeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	const FVector P = Pivot * VolumeSize;
	const FVector UVW = (P + SamplingTransform.InverseTransformPosition(Position - P)) / VolumeSize;
	if (UVW.ContainsNaN() || UVW.GetMin() < 0 || UVW.GetMax() > 1)
		return false;
	Weight = 1.0f;
	Value = SampleNormalized(UVW);
	return FMath::IsFinite(Value);
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
