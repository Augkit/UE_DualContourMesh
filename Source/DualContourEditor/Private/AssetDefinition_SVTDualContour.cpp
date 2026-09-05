#include "AssetDefinition_SVTDualContour.h"

#include "SVTDualContour.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_SVTDualContour"

FText UAssetDefinition_SVTDualContour::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "SVT Dual Contour");
}

FLinearColor UAssetDefinition_SVTDualContour::GetAssetColor() const
{
	return FLinearColor(0.18f, 0.55f, 0.85f);
}

TSoftClassPtr<UObject> UAssetDefinition_SVTDualContour::GetAssetClass() const
{
	return USVTDualContour::StaticClass();
}

#undef LOCTEXT_NAMESPACE
