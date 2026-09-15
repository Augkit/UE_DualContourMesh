#include "DualContourInitProgressWidget.h"
#include "DualContourGamePlay.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Internationalization/Text.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "DualContourGamePlay"

UDualContourInitProgressWidget::UDualContourInitProgressWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDualContourInitProgressWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UDualContourInitProgressWidget::BuildWidgetTree()
{
	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));

	BackdropImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Backdrop"));
	BackdropImage->SetBrush(FSlateColorBrush(FLinearColor(0.01f, 0.01f, 0.02f, BackdropOpacity)));
	if (UOverlaySlot* BackdropSlot = RootOverlay->AddChildToOverlay(BackdropImage))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* PanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSizeBox"));
	PanelSizeBox->SetWidthOverride(640.0f);

	UVerticalBox* PanelBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelBox"));

	TitleTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleTextBlock->SetText(LOCTEXT("Title", "Initializing Dual Contour Mesh"));
	TitleTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18));
	TitleTextBlock->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = PanelBox->AddChildToVerticalBox(TitleTextBlock))
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));

	USizeBox* BarSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarSizeBox"));
	BarSizeBox->SetHeightOverride(20.0f);
	MainProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Progress"));
	MainProgressBar->SetFillColorAndOpacity(FLinearColor(0.05f, 0.45f, 0.95f, 1.0f));
	BarSizeBox->SetContent(MainProgressBar);
	if (UVerticalBoxSlot* BarSlot = PanelBox->AddChildToVerticalBox(BarSizeBox))
		BarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	StatusTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Status"));
	StatusTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.74f, 0.78f, 1.0f)));
	StatusTextBlock->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* StatusSlot = PanelBox->AddChildToVerticalBox(StatusTextBlock))
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));

	PercentTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Percent"));
	PercentTextBlock->SetJustification(ETextJustify::Center);
	PanelBox->AddChildToVerticalBox(PercentTextBlock);

	PanelSizeBox->SetContent(PanelBox);
	if (UOverlaySlot* PanelSlot = RootOverlay->AddChildToOverlay(PanelSizeBox))
	{
		PanelSlot->SetPadding(FMargin(24.0f));
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	WidgetTree->RootWidget = RootOverlay;
}

void UDualContourInitProgressWidget::TrackActors(const TArray<TObjectPtr<ADualContourMeshActor>>& InActors)
{
	for (const TObjectPtr<ADualContourMeshActor>& Actor : InActors)
	{
		if (!Actor)
			continue;

		FTrackedActor& Tracked = TrackedActors.AddDefaulted_GetRef();
		Tracked.Actor = Actor;
		BindActor(Tracked, TrackedActors.Num() - 1);
	}

	if (TrackedActors.IsEmpty())
	{
		UE_LOG(LogDualContourGamePlay, Warning, TEXT("Initialization progress overlay has no actors to track."));
	}
}

void UDualContourInitProgressWidget::BindActor(FTrackedActor& Tracked, int32 TrackedIndex)
{
	ADualContourMeshActor* Actor = Tracked.Actor.Get();
	if (!Actor)
	{
		Tracked.bComplete = true;
		return;
	}

	// Actors with serialized contour data already have their cells ready; actors that copy
	// InitialDualContour during BeginPlay only reach that state once OnCellsRebuilt fires.
	Tracked.bCellsReady = Actor->DualContour && Actor->DualContour->HasCurrentGeneratedData();

	TWeakObjectPtr<UDualContourInitProgressWidget> WeakThis(this);
	if (UDualContour* DualContour = Actor->DualContour)
	{
		Tracked.CellsRebuiltHandle = DualContour->OnCellsRebuilt.AddLambda(
			[WeakThis, TrackedIndex](FIntVector CellMin, FIntVector CellMax)
			{
				if (UDualContourInitProgressWidget* Widget = WeakThis.Get())
					Widget->HandleCellsRebuilt(TrackedIndex);
			});
	}

	Tracked.MeshComponentsUpdatedHandle = Actor->OnMeshComponentsUpdated.AddLambda(
		[WeakThis, TrackedIndex]()
		{
			if (UDualContourInitProgressWidget* Widget = WeakThis.Get())
				Widget->HandleMeshComponentsUpdated(TrackedIndex);
		});
}

void UDualContourInitProgressWidget::UnbindActor(FTrackedActor& Tracked)
{
	if (ADualContourMeshActor* Actor = Tracked.Actor.Get())
	{
		if (Tracked.MeshComponentsUpdatedHandle.IsValid())
		{
			Actor->OnMeshComponentsUpdated.Remove(Tracked.MeshComponentsUpdatedHandle);
			Tracked.MeshComponentsUpdatedHandle.Reset();
		}

		if (UDualContour* DualContour = Actor->DualContour)
		{
			if (Tracked.CellsRebuiltHandle.IsValid())
			{
				DualContour->OnCellsRebuilt.Remove(Tracked.CellsRebuiltHandle);
				Tracked.CellsRebuiltHandle.Reset();
			}
		}
	}
}

void UDualContourInitProgressWidget::HandleCellsRebuilt(int32 TrackedIndex)
{
	if (!TrackedActors.IsValidIndex(TrackedIndex))
		return;

	FTrackedActor& Tracked = TrackedActors[TrackedIndex];
	Tracked.bCellsReady = true;
	Tracked.ProgressTarget = FMath::Max(Tracked.ProgressTarget, 0.6f);
}

void UDualContourInitProgressWidget::HandleMeshComponentsUpdated(int32 TrackedIndex)
{
	if (!TrackedActors.IsValidIndex(TrackedIndex))
		return;

	FTrackedActor& Tracked = TrackedActors[TrackedIndex];
	if (Tracked.bComplete)
		return;

	// The actor consumed every queued component update, so initialization is actually
	// complete. Snap to 100% instead of slowly interpolating fake progress after the
	// work has finished.
	Tracked.bComplete = true;
	Tracked.bCellsReady = true;
	Tracked.Progress = 1.0f;
	Tracked.ProgressTarget = 1.0f;
}

bool UDualContourInitProgressWidget::IsActorInitializationComplete(const ADualContourMeshActor& Actor) const
{
	// Mesh actors only tick while component updates are queued. An actor that began play
	// with ticking disabled has no pending initialization left to wait for; this also
	// covers actors that finished before the overlay started tracking them.
	return Actor.HasActorBegunPlay() && !Actor.IsActorTickEnabled();
}

bool UDualContourInitProgressWidget::AreAllActorsComplete() const
{
	for (const FTrackedActor& Tracked : TrackedActors)
		if (!Tracked.bComplete)
			return false;
	return !TrackedActors.IsEmpty();
}

float UDualContourInitProgressWidget::GetOverallProgress() const
{
	if (TrackedActors.IsEmpty())
		return 1.0f;

	float ProgressSum = 0.0f;
	for (const FTrackedActor& Tracked : TrackedActors)
		ProgressSum += Tracked.Progress;
	return ProgressSum / static_cast<float>(TrackedActors.Num());
}

int32 UDualContourInitProgressWidget::GetCompletedActorCount() const
{
	int32 CompletedCount = 0;
	for (const FTrackedActor& Tracked : TrackedActors)
		if (Tracked.bComplete)
			++CompletedCount;
	return CompletedCount;
}

FText UDualContourInitProgressWidget::GetStatusText() const
{
	if (TrackedActors.IsEmpty() || AreAllActorsComplete())
		return LOCTEXT("StatusComplete", "Complete");

	bool bAnyCellsPending = false;
	for (const FTrackedActor& Tracked : TrackedActors)
		if (!Tracked.bCellsReady)
			bAnyCellsPending = true;

	const FText PhaseText = bAnyCellsPending
		? LOCTEXT("StatusCells", "Preparing contour cells...")
		: LOCTEXT("StatusMesh", "Building mesh components...");
	return FText::Format(LOCTEXT("StatusActorCount", "{0}  ({1} / {2} meshes)"),
		PhaseText, GetCompletedActorCount(), TrackedActors.Num());
}

void UDualContourInitProgressWidget::NativeDestruct()
{
	for (FTrackedActor& Tracked : TrackedActors)
		UnbindActor(Tracked);
	TrackedActors.Reset();
	Super::NativeDestruct();
}

void UDualContourInitProgressWidget::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	if (bRemovalRequested || TrackedActors.IsEmpty())
		return;

	// The first tick after a long synchronous generation hitch can carry all of that
	// accumulated time; limit it so the animated progress cannot jump to a phase cap.
	const float AnimationDeltaTime = FMath::Clamp(DeltaTime, 0.0f, 0.1f);
	ElapsedDisplayTime += DeltaTime;

	for (int32 TrackedIndex = 0; TrackedIndex < TrackedActors.Num(); ++TrackedIndex)
	{
		FTrackedActor& Tracked = TrackedActors[TrackedIndex];
		const ADualContourMeshActor* Actor = Tracked.Actor.Get();
		if (!Tracked.bComplete && (!Actor || IsActorInitializationComplete(*Actor)))
			HandleMeshComponentsUpdated(TrackedIndex);
		if (Tracked.bComplete)
			continue;

		// Fake progress uses diminishing returns: it moves quickly at first and slows down
		// as it approaches the end of the current phase.
		const float PhaseProgressCap = Tracked.bCellsReady ? 0.95f : 0.6f;
		Tracked.ProgressTarget = FMath::Min(
			Tracked.ProgressTarget + (PhaseProgressCap - Tracked.ProgressTarget)
				* FMath::Min(AnimationDeltaTime * 0.8f, 1.0f), PhaseProgressCap);

		// Smooth the visible value independently from the target so the bar never jumps
		// between the cell and mesh-component phases or the final completion signal.
		Tracked.Progress = FMath::Lerp(
			Tracked.Progress, Tracked.ProgressTarget, FMath::Clamp(AnimationDeltaTime * 2.0f, 0.0f, 1.0f));
	}

	const bool bAllComplete = AreAllActorsComplete();
	if (bAllComplete)
		ElapsedCompleteTime += DeltaTime;

	UpdateProgressDisplay();

	if (bAllComplete && ElapsedCompleteTime >= CompleteHoldTime && ElapsedDisplayTime >= MinimumDisplayTime)
		RequestRemoval();
}

void UDualContourInitProgressWidget::UpdateProgressDisplay()
{
	const float OverallProgress = GetOverallProgress();
	if (MainProgressBar)
		MainProgressBar->SetPercent(OverallProgress);
	if (PercentTextBlock)
		PercentTextBlock->SetText(FText::AsPercent(OverallProgress));
	if (StatusTextBlock)
		StatusTextBlock->SetText(GetStatusText());
}

void UDualContourInitProgressWidget::RequestRemoval()
{
	if (bRemovalRequested)
		return;

	bRemovalRequested = true;
	RemoveFromParent();
	OnFinished.Broadcast();
}

#undef LOCTEXT_NAMESPACE
