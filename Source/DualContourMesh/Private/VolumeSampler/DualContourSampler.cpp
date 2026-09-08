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

bool UDualContourSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	FVector UVW;
	if (!TryGetNormalizedPosition(Position, UVW))
		return false;
	Weight = 1.0f;
	const UDualContour* Source = CachedDualContour.Get();
	Value = Source
		        ? Source->GetTrilinearDensity(UVW * FVector(Source->CellCount.X, Source->CellCount.Y, Source->CellCount.Z))
		        : 0.0f;
	return FMath::IsFinite(Value);
}
