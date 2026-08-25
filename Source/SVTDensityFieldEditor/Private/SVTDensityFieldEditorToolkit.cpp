#include "SVTDensityFieldEditorToolkit.h"

#include "SVTDensityField.h"
#include "SVTDensityFieldEditorViewport.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SVTDensityFieldEditor"

const FName FSVTDensityFieldEditorToolkit::ViewportTabId(TEXT("SVTDensityFieldEditor_Viewport"));
const FName FSVTDensityFieldEditorToolkit::DetailsTabId(TEXT("SVTDensityFieldEditor_Details"));

void FSVTDensityFieldEditorToolkit::InitEditor(EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InToolkitHost, USVTDensityField* InAsset)
{
	Asset = InAsset;
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bLockable = false;
	Args.bUpdatesFromSelection = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditor.CreateDetailView(Args);
	DetailsView->SetObject(Asset);
	DetailsView->OnFinishedChangingProperties().AddSP(this, &FSVTDensityFieldEditorToolkit::OnFinishedChangingProperties);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_SVTDensityFieldEditor_v2")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		                                       ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.72f)
			                                                                         ->AddTab(ViewportTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.28f)
			                                                                         ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))));

	InitAssetEditor(Mode, InToolkitHost, TEXT("SVTDensityFieldEditorApp"), Layout, true, true, Asset);
	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FSVTDensityFieldEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "SVT Density Field"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FSVTDensityFieldEditorToolkit::SpawnViewportTab))
	            .SetDisplayName(LOCTEXT("Viewport", "Preview")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FSVTDensityFieldEditorToolkit::SpawnDetailsTab))
	            .SetDisplayName(LOCTEXT("Details", "Details")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FSVTDensityFieldEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(ViewportTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FSVTDensityFieldEditorToolkit::SpawnViewportTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			SAssignNew(Viewport, SSVTDensityFieldViewport).EditorToolkit(SharedThis(this))
		];
}

TSharedRef<SDockTab> FSVTDensityFieldEditorToolkit::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			DetailsView.ToSharedRef()
		];
}

void FSVTDensityFieldEditorToolkit::ExtendToolbar()
{
	const TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		TEXT("Asset"),
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FSVTDensityFieldEditorToolkit::FillToolbar));
	AddToolbarExtender(ToolbarExtender);
}

void FSVTDensityFieldEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("DensityField"));
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FSVTDensityFieldEditorToolkit::GenerateDensityField),
			FCanExecuteAction::CreateSP(this, &FSVTDensityFieldEditorToolkit::CanGenerateDensityField)),
		NAME_None,
		LOCTEXT("GenerateDensityField", "Generate Density Field"),
		LOCTEXT("GenerateDensityFieldTooltip", "Sample the source sparse volume texture and rebuild the density field."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")));

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FSVTDensityFieldEditorToolkit::MakePreviewTypeMenu),
		TAttribute<FText>::CreateSP(this, &FSVTDensityFieldEditorToolkit::GetPreviewTypeLabel),
		LOCTEXT("PreviewTypeTooltip", "Choose whether the viewport previews the source SVT or generated density-field mesh."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visible")));

	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FSVTDensityFieldEditorToolkit::ToggleDualContourBounds),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FSVTDensityFieldEditorToolkit::ShouldShowDualContourBounds)),
		NAME_None,
		LOCTEXT("ShowDualContourBounds", "DualContour Bounds"),
		LOCTEXT("ShowDualContourBoundsTooltip", "Toggle the UDualContour CellCount x CellSize bounds in the preview."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visible")),
		EUserInterfaceActionType::ToggleButton);
	ToolbarBuilder.EndSection();
}

void FSVTDensityFieldEditorToolkit::GenerateDensityField()
{
	if (Asset && Asset->SampleSparseVolumeTexture())
	{
		PreviewType = ESVTDensityFieldPreviewType::DensityField;
		if (DetailsView)
			DetailsView->ForceRefresh();
		if (Viewport)
			Viewport->RefreshPreview();
	}
}

bool FSVTDensityFieldEditorToolkit::CanGenerateDensityField() const
{
	return Asset && Asset->DualContour && Asset->SourceSparseVolumeTexture;
}

TSharedRef<SWidget> FSVTDensityFieldEditorToolkit::MakePreviewTypeMenu()
{
	FMenuBuilder MenuBuilder(true, GetToolkitCommands());
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PreviewSVT", "Sparse Volume Texture"),
		LOCTEXT("PreviewSVTTooltip", "Preview the source sparse volume texture."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FSVTDensityFieldEditorToolkit::SetPreviewType,
				ESVTDensityFieldPreviewType::SparseVolumeTexture),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FSVTDensityFieldEditorToolkit::IsPreviewTypeSelected,
				ESVTDensityFieldPreviewType::SparseVolumeTexture)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PreviewDensityField", "Density Field"),
		LOCTEXT("PreviewDensityFieldTooltip", "Preview the mesh generated from the density field."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FSVTDensityFieldEditorToolkit::SetPreviewType,
				ESVTDensityFieldPreviewType::DensityField),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FSVTDensityFieldEditorToolkit::IsPreviewTypeSelected,
				ESVTDensityFieldPreviewType::DensityField)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	return MenuBuilder.MakeWidget();
}

void FSVTDensityFieldEditorToolkit::SetPreviewType(ESVTDensityFieldPreviewType InPreviewType)
{
	if (PreviewType == InPreviewType)
		return;

	PreviewType = InPreviewType;
	if (Viewport)
		Viewport->RefreshPreview();
}

bool FSVTDensityFieldEditorToolkit::IsPreviewTypeSelected(ESVTDensityFieldPreviewType InPreviewType) const
{
	return PreviewType == InPreviewType;
}

FText FSVTDensityFieldEditorToolkit::GetPreviewTypeLabel() const
{
	return PreviewType == ESVTDensityFieldPreviewType::DensityField
		       ? LOCTEXT("PreviewDensityFieldLabel", "Preview: Density Field")
		       : LOCTEXT("PreviewSVTLabel", "Preview: SVT");
}

void FSVTDensityFieldEditorToolkit::ToggleDualContourBounds()
{
	bShowDualContourBounds = !bShowDualContourBounds;
	if (Viewport)
		Viewport->InvalidatePreview();
}

void FSVTDensityFieldEditorToolkit::OnFinishedChangingProperties(const FPropertyChangedEvent& Event)
{
	if (Viewport)
		Viewport->RefreshPreview();
}

FName FSVTDensityFieldEditorToolkit::GetToolkitFName() const { return TEXT("SVTDensityFieldEditor"); }
FText FSVTDensityFieldEditorToolkit::GetBaseToolkitName() const { return LOCTEXT("AppLabel", "SVT Density Field Editor"); }
FString FSVTDensityFieldEditorToolkit::GetWorldCentricTabPrefix() const { return TEXT("SVT Density Field "); }
FLinearColor FSVTDensityFieldEditorToolkit::GetWorldCentricTabColorScale() const { return FLinearColor(0.18f, 0.55f, 0.85f); }

void FSVTDensityFieldEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
}

#undef LOCTEXT_NAMESPACE
