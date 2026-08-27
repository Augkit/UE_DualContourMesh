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

float UDualContourSampler::SampleNormalized(const FVector& UVW) const
{
	const UDualContour* Source = CachedDualContour.Get();
	if (!Source)
		return 0.0f;
	return Source->TrilinearDensity(UVW * FVector(Source->CellCount.X, Source->CellCount.Y, Source->CellCount.Z));
}

void UDualContourSampler::Finish() const
{
	CachedDualContour.Reset();
}
