#pragma once

#include "AssetDefinition_DualContour.h"
#include "AssetDefinition_VolumeSampledDualContour.generated.h"

UCLASS()
class UAssetDefinition_VolumeSampledDualContour : public UAssetDefinition_DualContour
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};
