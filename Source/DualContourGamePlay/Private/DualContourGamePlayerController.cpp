#include "DualContourGamePlayerController.h"
#include "DualContourGamePlay.h"
#include "DualContourInitProgressWidget.h"
#include "DualContourMeshActor.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ADualContourGamePlayerController::ADualContourGamePlayerController()
{
	bShowMouseCursor = true;
	ProgressWidgetClass = UDualContourInitProgressWidget::StaticClass();
}

void ADualContourGamePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
		ShowInitializationProgress();
}

void ADualContourGamePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ProgressWidget)
	{
		ProgressWidget->OnFinished.RemoveAll(this);
		ProgressWidget->RemoveFromParent();
		ProgressWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ADualContourGamePlayerController::ShowInitializationProgress()
{
	if (ProgressWidget || !ProgressWidgetClass)
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	TArray<TObjectPtr<ADualContourMeshActor>> MeshActors;
	for (TActorIterator<ADualContourMeshActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (IsValid(*ActorIt))
			MeshActors.Add(*ActorIt);
	}
	if (MeshActors.IsEmpty())
	{
		UE_LOG(LogDualContourGamePlay, Log,
			TEXT("Skipped the initialization overlay because the world contains no DualContour mesh actors."));
		return;
	}

	ProgressWidget = CreateWidget<UDualContourInitProgressWidget>(this, ProgressWidgetClass);
	if (!ProgressWidget)
		return;

	ProgressWidget->OnFinished.AddUObject(this, &ADualContourGamePlayerController::HandleProgressFinished);
	ProgressWidget->TrackActors(MeshActors);
	ProgressWidget->AddToViewport(ProgressWidgetZOrder);
	UE_LOG(LogDualContourGamePlay, Log,
		TEXT("Showing the initialization overlay for %d DualContour mesh actor(s)."), MeshActors.Num());
}

void ADualContourGamePlayerController::HandleProgressFinished()
{
	if (!ProgressWidget)
		return;

	UE_LOG(LogDualContourGamePlay, Log, TEXT("DualContour initialization finished; hiding the overlay."));
	ProgressWidget = nullptr;
}
