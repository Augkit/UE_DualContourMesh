#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "DualContourSampler.generated.h"

/** Base class for normalized sampling of an existing DualContour. */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContourSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const override;

	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;

protected:
	virtual UDualContour* ResolveDualContour() const PURE_VIRTUAL(UDualContourSampler::ResolveDualContour, return nullptr;);

	mutable TWeakObjectPtr<UDualContour> CachedDualContour;
	/** Precomputed CellCount / VolumeSize so sampling only multiplies per axis. */
	mutable FVector CachedGridPerVolumeUnit = FVector::ZeroVector;
};
