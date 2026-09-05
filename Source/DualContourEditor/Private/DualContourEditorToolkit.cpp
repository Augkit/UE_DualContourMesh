#include "DualContourEditorToolkit.h"

#include "DualContour.h"
#include "SVTDualContour.h"
#include "VolumeSampledDualContour.h"
#include "DualContourEditorViewport.h"
#include "EditMode/DualContourEdMode.h"
#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Widgets/SDualContourEditPanel.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "AssetEditorModeManager.h"
#include "EditorModeManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "Styling/AppStyle.h"
#include "Materials/MaterialInterface.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DualContourEditor"

const FName FDualContourEditorToolkit::ViewportTabId(TEXT("DualContourEditor_Viewport"));
const FName FDualContourEditorToolkit::DetailsTabId(TEXT("DualContourEditor_Details"));

FDualContourEditorToolkit::~FDualContourEditorToolkit()
{
	SetInteractionMode(false);
	if (GenerationTickerHandle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(GenerationTickerHandle);
	if (PreviewMeshTickerHandle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewMeshTickerHandle);

	if (Asset)
		Asset->OnCellsRebuilt.RemoveAll(this);
}

void FDualContourEditorToolkit::InitEditor(EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InToolkitHost, UDualContour* InAsset)
{
	Asset = InAsset;
	if (Asset)
	{
		Asset->OnCellsRebuilt.AddSP(this, &FDualContourEditorToolkit::HandleCellsRebuilt);
	}
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
	DetailsView->OnFinishedChangingProperties().AddSP(
		this, &FDualContourEditorToolkit::HandleFinishedChangingProperties);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_DualContourEditor_v2")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		                                       ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.72f)
			                                                                         ->AddTab(ViewportTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))
		                                                                         ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.28f)
			                                                                         ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(
				                                                                         true))));

	// InitAssetEditor restores the saved layout (and therefore constructs the viewport tab)
	// before its normal late mode-manager initialization. The viewport needs that manager
	// during construction, so create the preview-scene-aware manager up front.
	CreateEditorModeManager();
	InitAssetEditor(Mode, InToolkitHost, TEXT("DualContourEditorApp"), Layout, true, true, Asset);
	// The preview world's Tick is suspended/throttled when this editor window loses focus.
	// Drive mesh application from the editor's core ticker instead, while keeping all
	// UObject/component work on the game thread.
	PreviewMeshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FDualContourEditorToolkit::TickPreviewMeshUpdates), 0.0f);
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
	TSharedRef<SDualContourEditorViewport> NewViewport =
		SNew(SDualContourEditorViewport).EditorToolkit(SharedThis(this));
	Viewport = NewViewport;
	Viewport->OnMeshComponentsUpdated.AddSP(this, &FDualContourEditorToolkit::HandlePreviewMeshComponentsUpdated);
	if (bEditModeEnabled && !Viewport->SetEditingEnabled(true))
		bEditModeEnabled = false;
	return SNew(SDockTab)
		[
			NewViewport
		];
}

TSharedRef<SDockTab> FDualContourEditorToolkit::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	EditPanelContainer = SNew(SBox);
	RefreshEditPanel();

	if (!GetSVTDualContour() && !GetVolumeSampledDualContour())
	{
		return SNew(SDockTab)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([WeakThis = TWeakPtr<FDualContourEditorToolkit>(SharedThis(this))]()
				{
					const TSharedPtr<FDualContourEditorToolkit> Pinned = WeakThis.Pin();
					return Pinned && Pinned->bEditModeEnabled ? 1 : 0;
				})
				+ SWidgetSwitcher::Slot()[DetailsView.ToSharedRef()]
				+ SWidgetSwitcher::Slot()[EditPanelContainer.ToSharedRef()]
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

	TSharedRef<SVerticalBox> AssetDetails = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				DetailsView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 8.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.HAlign(HAlign_Right)
				.Visibility(GetVolumeSampledDualContour() ? EVisibility::Visible : EVisibility::Collapsed)
				.ToolTipText(LOCTEXT("AutoGenerateTooltip",
					"Automatically sample the volume and rebuild the dual contour after an editor property change."))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AutoGenerate", "Auto Generate"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked(this, &FDualContourEditorToolkit::GetAutoGenerateCheckState)
						.OnCheckStateChanged(this, &FDualContourEditorToolkit::HandleAutoGenerateCheckStateChanged)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f)
			[
				SNew(SBox)
					.Visibility(this, &FDualContourEditorToolkit::GetGenerationProgressVisibility)
					[
						SNew(SProgressBar)
							.Percent(this, &FDualContourEditorToolkit::GetGenerationProgress)
							.ToolTipText(LOCTEXT("GenerationProgressTooltip", "Generating the dual contour..."))
					]
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
			];

	return SNew(SDockTab)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([WeakThis = TWeakPtr<FDualContourEditorToolkit>(SharedThis(this))]()
			{
				const TSharedPtr<FDualContourEditorToolkit> Pinned = WeakThis.Pin();
				return Pinned && Pinned->bEditModeEnabled ? 1 : 0;
			})
			+ SWidgetSwitcher::Slot()[AssetDetails]
			+ SWidgetSwitcher::Slot()[EditPanelContainer.ToSharedRef()]
		];
}

void FDualContourEditorToolkit::CreateEditorModeManager()
{
	EditorModeManager = MakeShared<FAssetEditorModeManager>();
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
	ToolbarBuilder.BeginSection(TEXT("InteractionMode"));
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetInteractionMode, false),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::IsInteractionModeSelected, false)),
		NAME_None, LOCTEXT("PreviewMode", "Preview"),
		LOCTEXT("PreviewModeTooltip", "Preview the asset and use normal camera controls."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visible")), EUserInterfaceActionType::RadioButton);
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetInteractionMode, true),
			FCanExecuteAction::CreateSP(this, &FDualContourEditorToolkit::CanEnableEditMode),
			FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::IsInteractionModeSelected, true)),
		NAME_None, LOCTEXT("EditMode", "Edit"),
		LOCTEXT("EditModeTooltip", "Edit the Dual Contour directly in this preview viewport."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LandscapeEditor.SculptTool")), EUserInterfaceActionType::RadioButton);
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection(TEXT("EditTools"));
	auto AddEditTool = [this, &ToolbarBuilder](EDualContourEditTool Tool, const FText& Label,
		const FText& Tooltip, const FName IconName)
	{
		const uint8 ToolValue = static_cast<uint8>(Tool);
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetActiveEditTool, ToolValue),
				FCanExecuteAction::CreateSP(this, &FDualContourEditorToolkit::CanUseEditTools),
				FIsActionChecked::CreateSP(this, &FDualContourEditorToolkit::IsEditToolSelected, ToolValue)),
			NAME_None, Label, Tooltip, FSlateIcon(FAppStyle::GetAppStyleSetName(), IconName),
			EUserInterfaceActionType::RadioButton);
	};
	AddEditTool(EDualContourEditTool::Sculpt, LOCTEXT("Sculpt", "Sculpt"),
		LOCTEXT("SculptTooltip", "Add volume. Hold Shift to remove volume."), TEXT("LandscapeEditor.SculptTool"));
	AddEditTool(EDualContourEditTool::Flatten, LOCTEXT("Flatten", "Flatten"),
		LOCTEXT("FlattenTooltip", "Pick a height on press, then raise or lower the swept surface toward it."),
		TEXT("LandscapeEditor.FlattenTool"));
	AddEditTool(EDualContourEditTool::Erase, LOCTEXT("Erase", "Erase"),
		LOCTEXT("EraseTooltip", "Restore the asset state from when Edit mode was entered."), TEXT("LandscapeEditor.EraseTool"));
	AddEditTool(EDualContourEditTool::Smooth, LOCTEXT("Smooth", "Smooth"),
		LOCTEXT("SmoothTooltip", "Smooth the sampled density field."), TEXT("LandscapeEditor.SmoothTool"));
	AddEditTool(EDualContourEditTool::Brush, LOCTEXT("BrushStamp", "Brush Stamp"),
		LOCTEXT("BrushStampTooltip", "Stamp a Volume Sampled Dual Contour. Hold Shift for difference."),
		TEXT("LandscapeEditor.BlueprintBrushTool"));
	AddEditTool(EDualContourEditTool::PaintMaterial, LOCTEXT("PaintMaterial", "Paint Material"),
		LOCTEXT("PaintMaterialTooltip", "Paint a material ID. Hold Shift to paint material ID 0."),
		TEXT("LandscapeEditor.PaintTool"));
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection(TEXT("PreviewMaterial"));
	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FDualContourEditorToolkit::MakePreviewMaterialMenu),
		TAttribute<FText>::CreateSP(this, &FDualContourEditorToolkit::GetPreviewMaterialModeLabel),
		LOCTEXT("PreviewMaterialModeTooltip",
			"Choose the temporary material used to render the preview mesh."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Material")));
	ToolbarBuilder.AddWidget(
		SNew(SBox)
		.WidthOverride(220.0f)
		.Visibility(this, &FDualContourEditorToolkit::GetCustomPreviewMaterialVisibility)
		.ToolTipText(LOCTEXT("CustomPreviewMaterialTooltip",
			"Choose a custom preview material. The selection is not saved to the asset."))
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UMaterialInterface::StaticClass())
			.ObjectPath(this, &FDualContourEditorToolkit::GetPreviewMaterialPath)
			.OnObjectChanged(this, &FDualContourEditorToolkit::HandlePreviewMaterialChanged)
			.AllowClear(true)
			.DisplayThumbnail(false)
		]);
	ToolbarBuilder.EndSection();

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
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourEditor_GenerateDualContour);

	if (bGenerationInProgress || !Asset || !CanGenerateDualContour())
		return;
	if (bEditModeEnabled)
		SetInteractionMode(false);

	bGenerationInProgress = true;
	bGenerationCompletionRequested = false;
	bContourCellsReadyForGeneration = false;
	GenerationProgress = 0.0f;
	GenerationProgressTarget = 0.0f;
	GenerationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FDualContourEditorToolkit::StartGeneration), 0.0f);
	FSlateApplication::Get().InvalidateAllWidgets(false);
	// Return immediately so the current frame can render the disabled button and the 0%
	// progress bar before the synchronous sampling work begins on the next ticker frame.
	return;
}

bool FDualContourEditorToolkit::StartGeneration(float)
{
	if (!bGenerationInProgress)
		return false;

	if (!Asset->SampleSource())
	{
		bGenerationInProgress = false;
		bGenerationCompletionRequested = false;
		bContourCellsReadyForGeneration = false;
		GenerationProgress = 0.0f;
		GenerationProgressTarget = 0.0f;
		FTSTicker::GetCoreTicker().RemoveTicker(GenerationTickerHandle);
		GenerationTickerHandle.Reset();
		FSlateApplication::Get().InvalidateAllWidgets(false);
		return false;
	}

	GenerationTickerHandle.Reset();
	GenerationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FDualContourEditorToolkit::TickGenerationProgress), 0.1f);

	// Sampling is synchronous, while the contour cell build is dispatched to the worker
	// pool. Keep the progress indicator visible for that asynchronous part of generation.
	if (bGenerationInProgress && !bGenerationCompletionRequested)
		GenerationProgressTarget = 0.1f;

	PreviewType = EDualContourEditorPreviewType::DualContour;
	if (DetailsView)
		DetailsView->ForceRefresh();
	// Wait for UDualContour::OnCellsRebuilt before refreshing the preview actor. Otherwise
	// the actor can inspect CellChunks while the background contour build is still running,
	// queue an empty component update, and report completion prematurely.
	return false;
}

bool FDualContourEditorToolkit::TickPreviewMeshUpdates(float)
{
	if (Viewport)
		Viewport->ProcessPendingMeshUpdates();
	return true;
}

bool FDualContourEditorToolkit::CanGenerateDualContour() const
{
	if (bGenerationInProgress)
		return false;
	if (const USVTDualContour* SVTDualContour = GetSVTDualContour())
		return SVTDualContour->SourceSparseVolumeTexture != nullptr;
	if (const UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour())
		return VolumeSampledDualContour->VolumeSampler != nullptr;
	return false;
}

void FDualContourEditorToolkit::HandleCellsRebuilt(FIntVector, FIntVector)
{
	if (!bGenerationInProgress)
		return;

	bContourCellsReadyForGeneration = true;
	GenerationProgressTarget = FMath::Max(GenerationProgressTarget, 0.6f);
	if (Viewport)
		Viewport->RefreshPreview();
}

void FDualContourEditorToolkit::HandlePreviewMeshComponentsUpdated()
{
	if (!bGenerationInProgress || !bContourCellsReadyForGeneration)
		return;

	// The actor has consumed all queued component updates, so generation is actually
	// complete. Snap to 100% instead of slowly interpolating fake progress after the
	// work has finished; the next ticker callback will hide the progress bar.
	bGenerationCompletionRequested = true;
	GenerationProgress = 1.0f;
	GenerationProgressTarget = 1.0f;
	FSlateApplication::Get().InvalidateAllWidgets(false);
}

bool FDualContourEditorToolkit::TickGenerationProgress(float DeltaTime)
{
	if (!bGenerationInProgress)
		return false;

	// The first ticker callback can contain accumulated time from the synchronous sampling
	// phase. Limit it so the visible progress cannot jump straight to a phase cap.
	const float AnimationDeltaTime = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	if (!bGenerationCompletionRequested)
	{
		const float PhaseProgressCap = bContourCellsReadyForGeneration ? 0.95f : 0.6f;
		// Fake progress uses diminishing returns: it moves quickly at first and slows down
		// as it approaches the end of the current phase.
		GenerationProgressTarget = FMath::Min(
			GenerationProgressTarget + (PhaseProgressCap - GenerationProgressTarget)
				* FMath::Min(AnimationDeltaTime * 0.8f, 1.0f), PhaseProgressCap);
	}

	// Smooth the visible value independently from the target so the bar never jumps between
	// CellChunks, MeshComponents, and the final completion signal.
	GenerationProgress = FMath::Lerp(
		GenerationProgress, GenerationProgressTarget, FMath::Clamp(AnimationDeltaTime * 2.0f, 0.0f, 1.0f));
	if (bGenerationCompletionRequested && GenerationProgress >= 0.999f)
	{
		GenerationProgress = 1.0f;
		bGenerationInProgress = false;
		bGenerationCompletionRequested = false;
		bContourCellsReadyForGeneration = false;
		FTSTicker::GetCoreTicker().RemoveTicker(GenerationTickerHandle);
		GenerationTickerHandle.Reset();
	}

	FSlateApplication::Get().InvalidateAllWidgets(false);
	return bGenerationInProgress;
}

EVisibility FDualContourEditorToolkit::GetGenerationProgressVisibility() const
{
	return bGenerationInProgress ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<float> FDualContourEditorToolkit::GetGenerationProgress() const
{
	return GenerationProgress;
}

void FDualContourEditorToolkit::HandleFinishedChangingProperties(const FPropertyChangedEvent&)
{
	const UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour();
	if (VolumeSampledDualContour && VolumeSampledDualContour->bAutoGenerate
	    && VolumeSampledDualContour->bRebuildRequired
	    && CanGenerateDualContour())
		GenerateDualContour();
}

ECheckBoxState FDualContourEditorToolkit::GetAutoGenerateCheckState() const
{
	const UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour();
	return VolumeSampledDualContour && VolumeSampledDualContour->bAutoGenerate
		       ? ECheckBoxState::Checked
		       : ECheckBoxState::Unchecked;
}

void FDualContourEditorToolkit::HandleAutoGenerateCheckStateChanged(ECheckBoxState NewState)
{
	UVolumeSampledDualContour* VolumeSampledDualContour = GetVolumeSampledDualContour();
	if (!VolumeSampledDualContour)
		return;

	const bool bNewAutoGenerate = NewState == ECheckBoxState::Checked;
	if (VolumeSampledDualContour->bAutoGenerate == bNewAutoGenerate)
		return;

	const FScopedTransaction Transaction(LOCTEXT("SetAutoGenerateTransaction", "Set Auto Generate"));
	VolumeSampledDualContour->Modify();
	VolumeSampledDualContour->bAutoGenerate = bNewAutoGenerate;
	VolumeSampledDualContour->MarkPackageDirty();

	if (bNewAutoGenerate && VolumeSampledDualContour->bRebuildRequired && CanGenerateDualContour())
		GenerateDualContour();
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

	if (InPreviewType != EDualContourEditorPreviewType::DualContour && bEditModeEnabled)
		SetInteractionMode(false);
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

UMaterialInterface* FDualContourEditorToolkit::GetPreviewMaterial() const
{
	switch (PreviewMaterialMode)
	{
	case EDualContourEditorPreviewMaterialMode::MaterialId:
		return MaterialIdPreviewMaterial;
	case EDualContourEditorPreviewMaterialMode::Custom:
		return CustomPreviewMaterial;
	default:
		return nullptr;
	}
}

TSharedRef<SWidget> FDualContourEditorToolkit::MakePreviewMaterialMenu()
{
	FMenuBuilder MenuBuilder(true, GetToolkitCommands());
	const auto AddMode = [this, &MenuBuilder](EDualContourEditorPreviewMaterialMode Mode,
		const FText& Label, const FText& Tooltip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FDualContourEditorToolkit::SetPreviewMaterialMode, Mode),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this,
					&FDualContourEditorToolkit::IsPreviewMaterialModeSelected, Mode)),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	AddMode(EDualContourEditorPreviewMaterialMode::Default,
		LOCTEXT("DefaultPreviewMaterial", "Default"),
		LOCTEXT("DefaultPreviewMaterialTooltip", "Use Unreal Engine's default surface material."));
	AddMode(EDualContourEditorPreviewMaterialMode::MaterialId,
		LOCTEXT("MaterialIdPreviewMaterial", "Material ID"),
		LOCTEXT("MaterialIdPreviewMaterialTooltip", "Visualize the generated Material ID values."));
	AddMode(EDualContourEditorPreviewMaterialMode::Custom,
		LOCTEXT("CustomPreviewMaterial", "Custom"),
		LOCTEXT("CustomPreviewMaterialMenuTooltip", "Use a material selected in the adjacent asset picker."));
	return MenuBuilder.MakeWidget();
}

void FDualContourEditorToolkit::SetPreviewMaterialMode(EDualContourEditorPreviewMaterialMode InMode)
{
	if (InMode == EDualContourEditorPreviewMaterialMode::MaterialId && !MaterialIdPreviewMaterial)
	{
		MaterialIdPreviewMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/DualContourMesh/Editor/Materials/M_ShowMaterialID.M_ShowMaterialID"));
	}

	PreviewMaterialMode = InMode;
	if (Viewport)
		Viewport->RefreshPreviewMaterial();
	FSlateApplication::Get().InvalidateAllWidgets(false);
}

bool FDualContourEditorToolkit::IsPreviewMaterialModeSelected(
	EDualContourEditorPreviewMaterialMode InMode) const
{
	return PreviewMaterialMode == InMode;
}

FText FDualContourEditorToolkit::GetPreviewMaterialModeLabel() const
{
	switch (PreviewMaterialMode)
	{
	case EDualContourEditorPreviewMaterialMode::MaterialId:
		return LOCTEXT("MaterialIdPreviewMaterialLabel", "Material ID");
	case EDualContourEditorPreviewMaterialMode::Custom:
		return LOCTEXT("CustomPreviewMaterialLabel", "Custom");
	default:
		return LOCTEXT("DefaultPreviewMaterialLabel", "Default");
	}
}

EVisibility FDualContourEditorToolkit::GetCustomPreviewMaterialVisibility() const
{
	return PreviewMaterialMode == EDualContourEditorPreviewMaterialMode::Custom
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

void FDualContourEditorToolkit::ToggleDualContourBounds()
{
	bShowDualContourBounds = !bShowDualContourBounds;
	if (Viewport)
		Viewport->InvalidatePreview();
}

FString FDualContourEditorToolkit::GetPreviewMaterialPath() const
{
	return CustomPreviewMaterial ? CustomPreviewMaterial->GetPathName() : FString();
}

void FDualContourEditorToolkit::HandlePreviewMaterialChanged(const FAssetData& AssetData)
{
	CustomPreviewMaterial = Cast<UMaterialInterface>(AssetData.GetAsset());
	if (Viewport)
		Viewport->RefreshPreviewMaterial();
}

void FDualContourEditorToolkit::SetInteractionMode(bool bEnableEditing)
{
	if (bEditModeEnabled == bEnableEditing)
		return;
	if (bEnableEditing)
	{
		if (!CanEnableEditMode())
			return;
		PreviewType = EDualContourEditorPreviewType::DualContour;
		if (Viewport)
		{
			Viewport->RefreshPreview();
			if (!Viewport->SetEditingEnabled(true))
				return;
		}
		bEditModeEnabled = true;
		RefreshEditPanel();
	}
	else
	{
		if (Viewport)
			Viewport->SetEditingEnabled(false);
		bEditModeEnabled = false;
	}
	FSlateApplication::Get().InvalidateAllWidgets(false);
}

bool FDualContourEditorToolkit::CanEnableEditMode() const
{
	return Asset && Asset->HasCurrentGeneratedData() && !bGenerationInProgress;
}

bool FDualContourEditorToolkit::IsInteractionModeSelected(bool bEditing) const
{
	return bEditModeEnabled == bEditing;
}

UDualContourEdMode* FDualContourEditorToolkit::GetActiveEditMode() const
{
	return Cast<UDualContourEdMode>(
		GetEditorModeManager().GetActiveScriptableMode(UDualContourEdMode::EM_DualContourEdModeId));
}

bool FDualContourEditorToolkit::CanUseEditTools() const
{
	const UDualContourEdMode* Mode = bEditModeEnabled ? GetActiveEditMode() : nullptr;
	return Mode && Mode->HasValidTarget();
}

void FDualContourEditorToolkit::SetActiveEditTool(uint8 ToolValue)
{
	if (UDualContourEdMode* Mode = GetActiveEditMode())
		Mode->SetActiveTool(static_cast<EDualContourEditTool>(ToolValue));
}

bool FDualContourEditorToolkit::IsEditToolSelected(uint8 ToolValue) const
{
	const UDualContourEdMode* Mode = GetActiveEditMode();
	return Mode && Mode->GetSettings()
		&& Mode->GetSettings()->ActiveTool == static_cast<EDualContourEditTool>(ToolValue);
}

void FDualContourEditorToolkit::RefreshEditPanel()
{
	if (!EditPanelContainer)
		return;
	if (UDualContourEdMode* Mode = GetActiveEditMode())
		EditPanelContainer->SetContent(SNew(SDualContourEditPanel).EditMode(Mode));
	else
		EditPanelContainer->SetContent(SNew(STextBlock).Text(
			LOCTEXT("EditModeUnavailable", "Switch to Edit mode to configure the preview brush.")));
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
	Collector.AddReferencedObject(MaterialIdPreviewMaterial);
	Collector.AddReferencedObject(CustomPreviewMaterial);
}

#undef LOCTEXT_NAMESPACE
