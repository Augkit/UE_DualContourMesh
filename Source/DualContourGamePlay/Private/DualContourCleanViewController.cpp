#include "DualContourCleanViewController.h"

#include "DualContourBombActor.h"
#include "DualContourBombRainActor.h"
#include "DualContourMeshActor.h"
#include "DualContourPrimitiveShapeRainActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Set to false to keep the monitor plugin installed but hidden for this scene.
	constexpr bool bEnableRuntimePerformanceMonitor = true;

	void SetRuntimePerformanceMonitorVisibility(bool bVisible)
	{
		if (IConsoleVariable* ShowVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("RuntimePerformanceMonitor.Show")))
		{
			ShowVariable->Set(bVisible ? 1 : 0, ECVF_SetByCode);
		}
	}
}

ADualContourCleanViewController::ADualContourCleanViewController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
	bAutoManageActiveCameraTarget = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void ADualContourCleanViewController::BeginPlay()
{
	Super::BeginPlay();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (bEnableRuntimePerformanceMonitor)
	{
		SetRuntimePerformanceMonitorVisibility(false);
		if (IConsoleManager::Get().FindConsoleVariable(TEXT("RuntimePerformanceMonitor.Show")) == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("RuntimePerformanceMonitor is enabled in the project but its console variable was not found."));
		}
	}
#endif

	SetInputMode(FInputModeGameOnly());
	// No pawn is spawned by this GameMode, so place the controller camera at the
	// level's PlayerStart explicitly. APlayerController normally gets this when
	// possessing a pawn; the clean-view controller has no pawn to possess.
	SetViewTarget(this);
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It))
		{
			FRotator StartRotation = It->GetActorRotation();
			StartRotation.Roll = 0.0f;
			SetInitialLocationAndRotation(It->GetActorLocation(), StartRotation);
			break;
		}
	}
	TryStartSceneActors();
}

void ADualContourCleanViewController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TryStartSceneActors();
}

void ADualContourCleanViewController::TryStartSceneActors()
{
	if (bSceneActorsStarted || !GetWorld())
		return;

	bool bHasMeshActor = false;
	for (TActorIterator<ADualContourMeshActor> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It))
			continue;
		bHasMeshActor = true;
		if (!It->HasActorBegunPlay() || It->IsMeshInitializationPending())
			return;
	}

	if (!bHasMeshActor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Clean view scene contains no DualContour mesh actor."));
	}

	for (TActorIterator<ADualContourBombActor> It(GetWorld()); It; ++It)
		if (IsValid(*It))
			It->ActivateBomb();

	for (TActorIterator<ADualContourBombRainActor> It(GetWorld()); It; ++It)
		if (IsValid(*It) && It->bStartAutomatically)
			It->StartBombRain();

	for (TActorIterator<ADualContourPrimitiveShapeRainActor> It(GetWorld()); It; ++It)
		if (IsValid(*It) && It->bStartAutomatically)
			It->StartShapeRain();

	bSceneActorsStarted = true;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (bEnableRuntimePerformanceMonitor && bHasMeshActor)
	{
		SetRuntimePerformanceMonitorVisibility(true);
	}
#endif

	UE_LOG(LogTemp, Log, TEXT("DualContour finished loading; activated scene bombs and shape rain."));
}
