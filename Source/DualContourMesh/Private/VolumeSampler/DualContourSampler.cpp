#include "VolumeSampler/DualContourSampler.h"

#include "DualContour.h"

bool UDualContourSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	CachedDualContour = ResolveDualContour();
	if (!CachedDualContour.IsValid() || !CachedDualContour->HasCurrentGeneratedData())
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidSourceDualContour",
			"The source DualContour is missing or requires a rebuild.");
		return false;
	}
	const FVector CellCountPerVolume = FVector(CachedDualContour->CellCount) / VolumeSize;
	CachedGridPerVolumeUnit = CellCountPerVolume;
	return true;
}

void UDualContourSampler::Finish() const
{
	CachedDualContour.Reset();
	CachedGridPerVolumeUnit = FVector::ZeroVector;
}

bool UDualContourSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
	float& Value, float& Weight) const
{
	FVector BaseVolumePosition;
	if (!TryGetBaseVolumePosition(TargetLocalPosition, Placement, BaseVolumePosition))
		return false;
	Weight = 1.0f;
	const UDualContour* SourceDualContour = CachedDualContour.Get();
	Value = SourceDualContour
		        ? SourceDualContour->GetTrilinearDensity(BaseVolumePosition * CachedGridPerVolumeUnit)
		        : 0.0f;
	return FMath::IsFinite(Value);
}
