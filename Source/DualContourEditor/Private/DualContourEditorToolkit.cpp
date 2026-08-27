#include "DualContourEditorToolkit.h"

#include "DualContour.h"
#include "SVTDualContour.h"
#include "DualContourEditorViewport.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DualContourEditor"

const FName FDualContourEditorToolkit::ViewportTabId(TEXT("DualContourEditor_Viewport"));
const FName FDualContourEditorToolkit::DetailsTabId(TEXT("DualContourEditor_Details"));

void FDualContourEditorToolkit::InitEditor(EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InToolkitHost, UDualContour* InAsset)
{
	Asset = InAsset;
	PreviewType = Cast<USVTDualContour>(Asset)
		              ? EDualContourEditorPreviewType::SparseVolumeTexture
		              : EDualContourEditorPreviewType::DualContour;
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bLockable = false;
	Args.bUpdatesFromSelection = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditor.CreateDetailView(Args);
	DetailsView->SetObject(Asset);
	DetailsView->OnFinishedChangingProperties().AddSP(this, &FDualContourEditorToolkit::OnFinishedChangingProperties);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_DualContourEditor_v2")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		                                       ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.72f)
			                                                                         ->AddTab(ViewportTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.28f)
			                                                                         ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))));

	InitAssetEditor(Mode, InToolkitHost, TEXT("DualContourEditorApp"), Layout, true, true, Asset);
	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FDualContourEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "Dual Contour"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FDualContourEditorToolkit::SpawnViewportTab))
	            .SetDisplayName(LOCTEXT("Viewport", "Preview")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FDualContourEditorToolkit::SpawnDetailsTab))
	            .SetDisplayName(LOCTEXT("Details", "Details")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FDualContourEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(ViewportTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FDualContourEditorToolkit::SpawnViewportTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			SAssignNew(Viewport, SDualContourEditorViewport).EditorToolkit(SharedThis(this))
		];
}

TSharedRef<SDockTab> FDualContourEditorToolkit::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			DetailsView.ToSharedRef()
		];
}

void FDualContourEditorToolkit::ExtendToolbar()
{
	const TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		TEXT("Asset"),
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FDualContourEditorToolkit::FillToolbar));
	AddToolbarExtender(ToolbarExtender);
}

void FDualContourEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("DualContour"));
	if (GetSVTDualContour())
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::GenerateDualContour),
				FCanExecuteAction::CreateSP(this, &FDualContourEditorToolkit::CanGenerateDualContour)),
			NAME_None,
			LOCTEXT("GenerateDualContour", "Generate Dual Contour"),
			LOCTEXT("GenerateDualContourTooltip", "Sample the source sparse volume texture and rebuild the dual contour."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")));

		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &FDualContourEditorToolkit::MakePreviewTypeMenu),
			TAttribute<FText>::CreateSP(this, &FDualContourEditorToolkit::GetPreviewTypeLabel),
			LOCTEXT("PreviewTypeTooltip", "Choose whether the viewport previews the source SVT or generated dual-contour mesh."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visible")));
	}

	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::ToggleDualContourBounds),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::ShouldShowDualContourBounds)),
		NAME_None,
		LOCTEXT("ShowDualContourBounds", "DualContour Bounds"),
		LOCTEXT("ShowDualContourBoundsTooltip", "Toggle the UDualContour CellCount x CellSize bounds in the preview."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visible")),
		EUserInterfaceActionType::ToggleButton);
	ToolbarBuilder.EndSection();
}

void FDualContourEditorToolkit::GenerateDualContour()
{
	USVTDualContour* SVTDualContour = GetSVTDualContour();
	if (SVTDualContour && SVTDualContour->SampleSparseVolumeTexture())
	{
		PreviewType = EDualContourEditorPreviewType::DualContour;
		if (DetailsView)
			DetailsView->ForceRefresh();
		if (Viewport)
			Viewport->RefreshPreview();
	}
}

bool FDualContourEditorToolkit::CanGenerateDualContour() const
{
	const USVTDualContour* SVTDualContour = GetSVTDualContour();
	return SVTDualContour && SVTDualContour->SourceSparseVolumeTexture;
}

TSharedRef<SWidget> FDualContourEditorToolkit::MakePreviewTypeMenu()
{
	FMenuBuilder MenuBuilder(true, GetToolkitCommands());
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PreviewSVT", "Sparse Volume Texture"),
		LOCTEXT("PreviewSVTTooltip", "Preview the source sparse volume texture."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetPreviewType,
				EDualContourEditorPreviewType::SparseVolumeTexture),
			FCanExecuteAction::CreateLambda([WeakThis = TWeakPtr<FDualContourEditorToolkit>(SharedThis(this))]()
			{
				const TSharedPtr<FDualContourEditorToolkit> Pinned = WeakThis.Pin();
				return Pinned && Pinned->GetSVTDualContour();
			}),
			FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::IsPreviewTypeSelected,
				EDualContourEditorPreviewType::SparseVolumeTexture)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PreviewDualContour", "Dual Contour"),
		LOCTEXT("PreviewDualContourTooltip", "Preview the mesh generated from the dual contour."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetPreviewType,
				EDualContourEditorPreviewType::DualContour),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::IsPreviewTypeSelected,
				EDualContourEditorPreviewType::DualContour)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	return MenuBuilder.MakeWidget();
}

void FDualContourEditorToolkit::SetPreviewType(EDualContourEditorPreviewType InPreviewType)
{
	if (PreviewType == InPreviewType)
		return;

	PreviewType = InPreviewType;
	if (Viewport)
		Viewport->RefreshPreview();
}

bool FDualContourEditorToolkit::IsPreviewTypeSelected(EDualContourEditorPreviewType InPreviewType) const
{
	return PreviewType == InPreviewType;
}

FText FDualContourEditorToolkit::GetPreviewTypeLabel() const
{
	return PreviewType == EDualContourEditorPreviewType::DualContour
		       ? LOCTEXT("PreviewDualContourLabel", "Preview: Dual Contour")
		       : LOCTEXT("PreviewSVTLabel", "Preview: SVT");
}

void FDualContourEditorToolkit::ToggleDualContourBounds()
{
	bShowDualContourBounds = !bShowDualContourBounds;
	if (Viewport)
		Viewport->InvalidatePreview();
}

void FDualContourEditorToolkit::OnFinishedChangingProperties(const FPropertyChangedEvent& Event)
{
	if (Viewport)
		Viewport->RefreshPreview();
}

FName FDualContourEditorToolkit::GetToolkitFName() const { return TEXT("DualContourEditor"); }

FText FDualContourEditorToolkit::GetBaseToolkitName() const
{
	return GetSVTDualContour()
		       ? LOCTEXT("SVTAppLabel", "SVT Dual Contour Editor")
		       : LOCTEXT("DualContourAppLabel", "Dual Contour Editor");
}

FString FDualContourEditorToolkit::GetWorldCentricTabPrefix() const
{
	return GetSVTDualContour() ? TEXT("SVT Dual Contour ") : TEXT("Dual Contour ");
}

FLinearColor FDualContourEditorToolkit::GetWorldCentricTabColorScale() const { return FLinearColor(0.18f, 0.55f, 0.85f); }

USVTDualContour* FDualContourEditorToolkit::GetSVTDualContour() const
{
	return Cast<USVTDualContour>(Asset);
}

void FDualContourEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
}

#undef LOCTEXT_NAMESPACE
