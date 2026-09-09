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
	return true;
}

void UDualContourSampler::Finish() const
{
	CachedDualContour.Reset();
}

bool UDualContourSampler::Sample(const FVector& SamplerInputPosition, float& Value, float& Weight) const
{
	FVector NormalizedVolumePosition;
	if (!TryGetNormalizedVolumePosition(SamplerInputPosition, NormalizedVolumePosition))
		return false;
	Weight = 1.0f;
	const UDualContour* SourceDualContour = CachedDualContour.Get();
	Value = SourceDualContour
		        ? SourceDualContour->GetTrilinearDensity(
			        NormalizedVolumePosition * FVector(SourceDualContour->CellCount))
		        : 0.0f;
	return FMath::IsFinite(Value);
}
