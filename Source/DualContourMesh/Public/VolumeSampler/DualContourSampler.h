#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "DualContourSampler.generated.h"

/** Base class for normalized sampling of an existing DualContour. */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContourSampler : public UVolumeSampler
{
	GENERATED_BODY()

protected:
	virtual UDualContour* ResolveDualContour() const PURE_VIRTUAL(UDualContourSampler::ResolveDualContour, return nullptr;);
	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;
	virtual float SampleNormalized(const FVector& UVW) const override;

	mutable TWeakObjectPtr<UDualContour> CachedDualContour;
};
