#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "SVTDualContourSampler.generated.h"

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

/** Samples a baked USVTDualContour, which is itself a DualContour. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API USVTDualContourSampler : public UDualContourSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<USVTDualContour> SVTDualContour;

protected:
	virtual UDualContour* ResolveDualContour() const override;
};
