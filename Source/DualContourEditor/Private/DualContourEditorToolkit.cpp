#include "DualContourEditorToolkit.h"

#include "DualContour.h"
#include "SVTDualContour.h"
#include "VolumeSampledDualContour.h"
#include "DualContourEditorViewport.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

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
	if (!GetSVTDualContour() && !GetVolumeSampledDualContour())
	{
		return SNew(SDockTab)
			[
				DetailsView.ToSharedRef()
			];
	}

	const auto MakeGenerateButton = [this](const FName ButtonStyleName, const FName TextStyleName)
	{
		return SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(ButtonStyleName))
			.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TextStyleName))
			.Text(LOCTEXT("GenerateDualContour", "Generate Dual Contour"))
			.ToolTipText(LOCTEXT("GenerateDualContourTooltip", "Sample the configured source and rebuild the dual contour."))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.IsEnabled(this, &FDualContourEditorToolkit::CanGenerateDualContour)
			.OnClicked(this, &FDualContourEditorToolkit::OnGenerateDualContourClicked);
	};

	return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				DetailsView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(32.0f)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([WeakThis = TWeakPtr<FDualContourEditorToolkit>(SharedThis(this))]()
					{
						const TSharedPtr<FDualContourEditorToolkit> Pinned = WeakThis.Pin();
						return Pinned && Pinned->Asset && Pinned->Asset->bRebuildRequired ? 1 : 0;
					})
					+ SWidgetSwitcher::Slot()
					[
						MakeGenerateButton(TEXT("Button"), TEXT("ButtonText"))
					]
					+ SWidgetSwitcher::Slot()
					[
						MakeGenerateButton(TEXT("PrimaryButton"), TEXT("PrimaryButtonText"))
					]
				]
			]
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

FReply FDualContourEditorToolkit::OnGenerateDualContourClicked()
{
	GenerateDualContour();
	return FReply::Handled();
}

void FDualContourEditorToolkit::GenerateDualContour()
{
	bool bGenerated = false;
	if (USVTDualContour* SVTDualContour = GetSVTDualContour())
		bGenerated = SVTDualContour->SampleSparseVolumeTexture();
	else if (UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour())
		bGenerated = VolumeSampledDualContour->SampleVolume();

	if (bGenerated)
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
	if (const USVTDualContour* SVTDualContour = GetSVTDualContour())
		return SVTDualContour->SourceSparseVolumeTexture != nullptr;
	if (const UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour())
		return VolumeSampledDualContour->VolumeSampler != nullptr;
	return false;
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
	if (GetSVTDualContour())
		return LOCTEXT("SVTAppLabel", "SVT Dual Contour Editor");
	if (GetVolumeSampledDualContour())
		return LOCTEXT("VolumeSampledAppLabel", "Volume Sampled Dual Contour Editor");
	return LOCTEXT("DualContourAppLabel", "Dual Contour Editor");
}

FString FDualContourEditorToolkit::GetWorldCentricTabPrefix() const
{
	if (GetSVTDualContour())
		return TEXT("SVT Dual Contour ");
	if (GetVolumeSampledDualContour())
		return TEXT("Volume Sampled Dual Contour ");
	return TEXT("Dual Contour ");
}

FLinearColor FDualContourEditorToolkit::GetWorldCentricTabColorScale() const { return FLinearColor(0.18f, 0.55f, 0.85f); }

USVTDualContour* FDualContourEditorToolkit::GetSVTDualContour() const
{
	return Cast<USVTDualContour>(Asset);
}

UVolumeSampledDualContour* FDualContourEditorToolkit::GetVolumeSampledDualContour() const
{
	return Cast<UVolumeSampledDualContour>(Asset);
}

void FDualContourEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
}

#undef LOCTEXT_NAMESPACE
