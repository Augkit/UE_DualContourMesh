#pragma once
#include "VolumeSampler/VolumeSampler.h"
#include "DualContourMaterialRegionSampler.generated.h"
class ADualContourMaterialBrushVolume;
UCLASS()
class UDualContourMaterialRegionSampler : public UVolumeSampler
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume;
	FTransform TargetTransform;
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;
};
