#include "AssetDefinition_DualContour.h"

#include "DualContour.h"
#include "DualContourEditorToolkit.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_DualContour"

FText UAssetDefinition_DualContour::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Dual Contour");
}

FLinearColor UAssetDefinition_DualContour::GetAssetColor() const
{
	return FLinearColor(0.05f, 0.65f, 1.f);
}

TSoftClassPtr<UObject> UAssetDefinition_DualContour::GetAssetClass() const
{
	return UDualContour::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_DualContour::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = {EAssetCategoryPaths::Texture};
	return Categories;
}

FAssetOpenSupport UAssetDefinition_DualContour::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod, OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_DualContour::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UDualContour* Asset : OpenArgs.LoadObjects<UDualContour>())
	{
		TSharedRef<FDualContourEditorToolkit> Editor = MakeShared<FDualContourEditorToolkit>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
