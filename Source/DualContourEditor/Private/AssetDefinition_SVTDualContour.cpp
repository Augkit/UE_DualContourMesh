#include "AssetDefinition_SVTDualContour.h"

#include "SVTDualContour.h"
#include "DualContourEditorToolkit.h"

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

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SVTDualContour::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = {EAssetCategoryPaths::Texture};
	return Categories;
}

FAssetOpenSupport UAssetDefinition_SVTDualContour::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod, OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_SVTDualContour::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (USVTDualContour* Asset : OpenArgs.LoadObjects<USVTDualContour>())
	{
		TSharedRef<FDualContourEditorToolkit> Editor = MakeShared<FDualContourEditorToolkit>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
