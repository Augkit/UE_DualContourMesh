#include "AssetDefinition_VolumeSampledDualContour.h"

#include "VolumeSampledDualContour.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_VolumeSampledDualContour"

FText UAssetDefinition_VolumeSampledDualContour::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Volume Sampled Dual Contour");
}

FLinearColor UAssetDefinition_VolumeSampledDualContour::GetAssetColor() const
{
	return FLinearColor(0.15f, 0.72f, 0.55f);
}

TSoftClassPtr<UObject> UAssetDefinition_VolumeSampledDualContour::GetAssetClass() const
{
	return UVolumeSampledDualContour::StaticClass();
}

#undef LOCTEXT_NAMESPACE
