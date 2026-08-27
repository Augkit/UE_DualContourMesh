#include "AssetDefinition_VolumeSampledDualContour.h"

#include "DualContourEditorToolkit.h"
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

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VolumeSampledDualContour::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = {EAssetCategoryPaths::Texture};
	return Categories;
}

FAssetOpenSupport UAssetDefinition_VolumeSampledDualContour::GetAssetOpenSupport(
	const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod, OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_VolumeSampledDualContour::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UVolumeSampledDualContour* Asset : OpenArgs.LoadObjects<UVolumeSampledDualContour>())
	{
		TSharedRef<FDualContourEditorToolkit> Editor = MakeShared<FDualContourEditorToolkit>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
