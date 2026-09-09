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
	FTransform TargetLocalToWorldTransform;
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& TargetLocalPosition, float& Value, float& Weight) const override;
};
