#include "AssetDefinition_SVTDensityField.h"

#include "SVTDensityField.h"
#include "SVTDensityFieldEditorToolkit.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_SVTDensityField"

FText UAssetDefinition_SVTDensityField::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "SVT Density Field");
}

FLinearColor UAssetDefinition_SVTDensityField::GetAssetColor() const
{
	return FLinearColor(0.18f, 0.55f, 0.85f);
}

TSoftClassPtr<UObject> UAssetDefinition_SVTDensityField::GetAssetClass() const
{
	return USVTDensityField::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SVTDensityField::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = {EAssetCategoryPaths::Texture};
	return Categories;
}

FAssetOpenSupport UAssetDefinition_SVTDensityField::GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
{
	return FAssetOpenSupport(OpenSupportArgs.OpenMethod, OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit,
		EToolkitMode::Standalone);
}

EAssetCommandResult UAssetDefinition_SVTDensityField::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (USVTDensityField* Asset : OpenArgs.LoadObjects<USVTDensityField>())
	{
		TSharedRef<FSVTDensityFieldEditorToolkit> Editor = MakeShared<FSVTDensityFieldEditorToolkit>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
