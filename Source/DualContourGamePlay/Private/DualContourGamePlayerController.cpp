#include "DualContourGamePlayerController.h"
#include "DualContourFPCharacter.h"
#include "DualContourGamePlay.h"
#include "DualContourInitProgressWidget.h"
#include "DualContourMiningReticleWidget.h"
#include "DualContourSaveLoadWidget.h"
#include "DualContourFirstPersonSaveGame.h"
#include "DualContourMeshActor.h"
#include "DualContourModifierComponent.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"

ADualContourGamePlayerController::ADualContourGamePlayerController()
{
	// First person gameplay: the mouse steers the camera, so no cursor.
	bShowMouseCursor = false;
	ProgressWidgetClass = UDualContourInitProgressWidget::StaticClass();
	ReticleWidgetClass = UDualContourMiningReticleWidget::StaticClass();
	SaveLoadWidgetClass = UDualContourSaveLoadWidget::StaticClass();
	ModifierComponent = CreateDefaultSubobject<UDualContourModifierComponent>(TEXT("DualContourModifier"));

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultMappingContextFinder(
		TEXT("/DualContourMesh/Input/IMC_Default.IMC_Default"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookMappingContextFinder(
		TEXT("/DualContourMesh/Input/IMC_MouseLook.IMC_MouseLook"));
	if (DefaultMappingContextFinder.Succeeded())
		DefaultMappingContexts.Add(DefaultMappingContextFinder.Object);
	if (MouseLookMappingContextFinder.Succeeded())
		DefaultMappingContexts.Add(MouseLookMappingContextFinder.Object);

}

void ADualContourGamePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GetWorldTimerManager().SetTimerForNextTick(
		this, &ADualContourGamePlayerController::ActivatePossessedPistolPose);
}

void ADualContourGamePlayerController::ActivatePossessedPistolPose()
{
	if (ADualContourFPCharacter* FPCharacter = Cast<ADualContourFPCharacter>(GetPawn()))
		FPCharacter->ActivatePistolPose();
}

void ADualContourGamePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		{
			for (const TObjectPtr<UInputMappingContext>& Context : DefaultMappingContexts)
				if (Context)
					Subsystem->AddMappingContext(Context, /*Priority=*/ 0);
		}

		ShowInitializationProgress();
		EnsureReticle();
	}
}

void ADualContourGamePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDigHeld)
	{
		DigProgress += DeltaSeconds / FMath::Max(DigHoldDuration, KINDA_SMALL_NUMBER);
		while (DigProgress >= 1.0f)
		{
			DigProgress -= 1.0f;
			PerformDig();
		}
	}
	else
	{
		DigProgress = 0.0f;
	}

	if (ReticleWidget)
		ReticleWidget->SetProgress(GetDigProgress());
}

void ADualContourGamePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ProgressWidget)
	{
		ProgressWidget->OnFinished.RemoveAll(this);
		ProgressWidget->RemoveFromParent();
		ProgressWidget = nullptr;
	}
	if (ReticleWidget)
	{
		ReticleWidget->RemoveFromParent();
		ReticleWidget = nullptr;
	}
	if (SaveLoadWidget)
	{
		SaveLoadWidget->RemoveFromParent();
		SaveLoadWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ADualContourGamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(SaveLoadToggleKey, IE_Pressed,
		this, &ADualContourGamePlayerController::ToggleSaveLoadWidget);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed,
		this, &ADualContourGamePlayerController::OnDigPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released,
		this, &ADualContourGamePlayerController::OnDigReleased);
	InitializeSamplers();
}

void ADualContourGamePlayerController::OnDigPressed()
{
	if (IsSaveLoadWidgetOpen())
		return;

	bDigHeld = true;
	if (ADualContourFPCharacter* FPCharacter = Cast<ADualContourFPCharacter>(GetPawn()))
	{
		FPCharacter->SetWeaponShakeHeld(true);
		FPCharacter->SetBeamHeld(true);
	}
}

void ADualContourGamePlayerController::OnDigReleased()
{
	bDigHeld = false;
	if (ADualContourFPCharacter* FPCharacter = Cast<ADualContourFPCharacter>(GetPawn()))
	{
		FPCharacter->SetWeaponShakeHeld(false);
		FPCharacter->SetBeamHeld(false);
	}
}

void ADualContourGamePlayerController::PerformDig()
{
	UWorld* World = GetWorld();
	if (!World || !ModifierComponent || !ModifierComponent->Samplers.IsValidIndex(SelectedSamplerIndex))
		return;

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport)
		return;

	FVector2D ViewportSize;
	Viewport->GetViewportSize(ViewportSize);
	FVector WorldOrigin;
	FVector WorldDirection;
	if (!DeprojectScreenPositionToWorld(ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f,
		WorldOrigin, WorldDirection))
		return;

	ModifierComponent->ModifyDualContourWithRay(WorldOrigin, WorldDirection,
		SelectedSamplerIndex, MaterialId, /*bExcavate=*/ true);
}

void ADualContourGamePlayerController::InitializeSamplers()
{
	if (!ModifierComponent)
		return;

	if (ModifierComponent->Samplers.IsEmpty())
	{
		bool bHasAdditionalSampler = false;
		for (const TObjectPtr<UVolumeSampler>& Sampler : AdditionalSamplers)
		{
			if (IsValid(Sampler))
			{
				bHasAdditionalSampler = true;
				break;
			}
		}
		if (!bHasAdditionalSampler)
			ModifierComponent->AddSampler(USphereVolumeSampler::StaticClass());
	}

	for (const TObjectPtr<UVolumeSampler>& Sampler : AdditionalSamplers)
		if (IsValid(Sampler) && !ModifierComponent->Samplers.Contains(Sampler))
			ModifierComponent->Samplers.Add(Sampler);

	if (!ModifierComponent->Samplers.IsEmpty())
		SelectedSamplerIndex = FMath::Clamp(SelectedSamplerIndex, 0,
			ModifierComponent->Samplers.Num() - 1);
}

void ADualContourGamePlayerController::SetSelectedSamplerIndex(int32 SamplerIndex)
{
	if (ModifierComponent && ModifierComponent->Samplers.IsValidIndex(SamplerIndex))
		SelectedSamplerIndex = SamplerIndex;
}

void ADualContourGamePlayerController::EnsureReticle()
{
	if (ReticleWidget || !ReticleWidgetClass)
		return;

	ReticleWidget = CreateWidget<UDualContourMiningReticleWidget>(this, ReticleWidgetClass);
	if (ReticleWidget)
	{
		ReticleWidget->SetProgress(0.0f);
		ReticleWidget->AddToViewport(ReticleZOrder);
	}
}

void ADualContourGamePlayerController::ToggleSaveLoadWidget()
{
	if (IsSaveLoadWidgetOpen())
		CloseSaveLoadWidget();
	else
		OpenSaveLoadWidget();
}

void ADualContourGamePlayerController::OpenSaveLoadWidget()
{
	if (SaveLoadWidget || !SaveLoadWidgetClass)
		return;

	SaveLoadWidget = CreateWidget<UDualContourSaveLoadWidget>(this, SaveLoadWidgetClass);
	if (!SaveLoadWidget)
		return;

	bDigHeld = false;
	if (ADualContourFPCharacter* FPCharacter = Cast<ADualContourFPCharacter>(GetPawn()))
	{
		FPCharacter->SetWeaponShakeHeld(false);
		FPCharacter->SetBeamHeld(false);
	}
	if (ReticleWidget)
		ReticleWidget->SetVisibility(ESlateVisibility::Hidden);

	SaveLoadWidget->AddToViewport(SaveLoadWidgetZOrder);
	SetSaveLoadInputMode(true);
	SaveLoadWidget->SetStatusMessage(FText::FromString(TEXT("选择槽位后进行保存或加载")));
	UE_LOG(LogDualContourGamePlay, Log, TEXT("Save/load panel opened."));
}

void ADualContourGamePlayerController::CloseSaveLoadWidget()
{
	if (!SaveLoadWidget)
		return;

	SaveLoadWidget->RemoveFromParent();
	SaveLoadWidget = nullptr;
	if (ReticleWidget)
		ReticleWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	SetSaveLoadInputMode(false);
	UE_LOG(LogDualContourGamePlay, Log, TEXT("Save/load panel closed."));
}

void ADualContourGamePlayerController::SetSaveLoadInputMode(bool bMenuOpen)
{
	SetIgnoreMoveInput(bMenuOpen);
	SetIgnoreLookInput(bMenuOpen);
	if (bMenuOpen)
	{
		FInputModeGameAndUI InputMode;
		if (SaveLoadWidget)
			InputMode.SetWidgetToFocus(SaveLoadWidget->TakeWidget());
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

FString ADualContourGamePlayerController::GetSaveSlotName(int32 SlotIndex) const
{
	return FString::Printf(TEXT("DualContourFirstPerson_%d"), FMath::Clamp(SlotIndex, 0, 2));
}

FString ADualContourGamePlayerController::GetTerrainSlotName(const FString& SaveSlotName, int32 ActorIndex) const
{
	return FString::Printf(TEXT("%s_Terrain_%d"), *SaveSlotName, ActorIndex);
}

void ADualContourGamePlayerController::GetSortedMeshActors(TArray<ADualContourMeshActor*>& OutActors) const
{
	OutActors.Reset();
	UWorld* World = GetWorld();
	if (!World)
		return;

	for (TActorIterator<ADualContourMeshActor> ActorIt(World); ActorIt; ++ActorIt)
		if (IsValid(*ActorIt))
			OutActors.Add(*ActorIt);

	OutActors.Sort([](const ADualContourMeshActor& Left, const ADualContourMeshActor& Right)
	{
		return Left.GetName() < Right.GetName();
	});
}

void ADualContourGamePlayerController::SaveToSlot(int32 SlotIndex)
{
	if (!IsLocalController())
		return;

	const FString SaveSlotName = GetSaveSlotName(SlotIndex);
	UDualContourFirstPersonSaveGame* SaveGame = Cast<UDualContourFirstPersonSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UDualContourFirstPersonSaveGame::StaticClass()));
	if (!SaveGame)
	{
		if (SaveLoadWidget)
			SaveLoadWidget->SetStatusMessage(FText::FromString(TEXT("创建存档失败")));
		return;
	}

	SaveGame->MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (APawn* ControlledPawn = GetPawn())
	{
		SaveGame->bHasPlayerTransform = true;
		SaveGame->PlayerTransform = ControlledPawn->GetActorTransform();
		SaveGame->ControlRotation = GetControlRotation();
	}
	SaveGame->SelectedSamplerIndex = SelectedSamplerIndex;

	TArray<ADualContourMeshActor*> MeshActors;
	GetSortedMeshActors(MeshActors);
	SaveGame->TerrainActorCount = MeshActors.Num();

	const bool bPlayerSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SaveSlotName, 0);
	bool bTerrainSaved = true;
	for (int32 ActorIndex = 0; ActorIndex < MeshActors.Num(); ++ActorIndex)
	{
		bTerrainSaved &= MeshActors[ActorIndex]->SaveRuntimeDensityIncrement(
			GetTerrainSlotName(SaveSlotName, ActorIndex), 0);
	}

	if (SaveLoadWidget)
	{
		SaveLoadWidget->SetStatusMessage((bPlayerSaved && bTerrainSaved)
			? FText::Format(FText::FromString(TEXT("槽位 {0} 保存成功")), FText::AsNumber(SlotIndex + 1))
			: FText::FromString(TEXT("保存失败：请查看日志")));
	}
	UE_LOG(LogDualContourGamePlay, Log, TEXT("Save slot %d completed: player=%s terrain=%s actors=%d."),
		SlotIndex + 1, bPlayerSaved ? TEXT("ok") : TEXT("failed"),
		bTerrainSaved ? TEXT("ok") : TEXT("failed"), MeshActors.Num());
}

void ADualContourGamePlayerController::LoadFromSlot(int32 SlotIndex)
{
	if (!IsLocalController())
		return;

	const FString SaveSlotName = GetSaveSlotName(SlotIndex);
	UDualContourFirstPersonSaveGame* SaveGame = Cast<UDualContourFirstPersonSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!SaveGame)
	{
		if (SaveLoadWidget)
			SaveLoadWidget->SetStatusMessage(FText::Format(
				FText::FromString(TEXT("槽位 {0} 没有可用存档")), FText::AsNumber(SlotIndex + 1)));
		return;
	}

	const FString CurrentMapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	if (!SaveGame->MapName.IsEmpty() && SaveGame->MapName != CurrentMapName)
	{
		if (SaveLoadWidget)
			SaveLoadWidget->SetStatusMessage(FText::FromString(TEXT("该存档属于其他地图")));
		return;
	}

	if (SaveGame->bHasPlayerTransform)
	{
		if (APawn* ControlledPawn = GetPawn())
			ControlledPawn->SetActorTransform(SaveGame->PlayerTransform);
		SetControlRotation(SaveGame->ControlRotation);
	}
	SetSelectedSamplerIndex(SaveGame->SelectedSamplerIndex);

	TArray<ADualContourMeshActor*> MeshActors;
	GetSortedMeshActors(MeshActors);
	bool bTerrainLoaded = true;
	for (int32 ActorIndex = 0; ActorIndex < MeshActors.Num(); ++ActorIndex)
	{
		bTerrainLoaded &= MeshActors[ActorIndex]->LoadRuntimeDensityIncrement(
			GetTerrainSlotName(SaveSlotName, ActorIndex), 0);
	}

	if (SaveLoadWidget)
	{
		SaveLoadWidget->SetStatusMessage(bTerrainLoaded
			? FText::Format(FText::FromString(TEXT("槽位 {0} 加载成功")), FText::AsNumber(SlotIndex + 1))
			: FText::FromString(TEXT("玩家状态已加载，但部分地形加载失败")));
	}
	UE_LOG(LogDualContourGamePlay, Log, TEXT("Load slot %d completed: terrain=%s actors=%d."),
		SlotIndex + 1, bTerrainLoaded ? TEXT("ok") : TEXT("failed"), MeshActors.Num());
}

void ADualContourGamePlayerController::ClearSlot(int32 SlotIndex)
{
	if (!IsLocalController())
		return;

	const FString SaveSlotName = GetSaveSlotName(SlotIndex);
	int32 TerrainSlotCount = 0;
	if (UDualContourFirstPersonSaveGame* SaveGame = Cast<UDualContourFirstPersonSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)))
	{
		TerrainSlotCount = FMath::Max(0, SaveGame->TerrainActorCount);
	}

	TArray<ADualContourMeshActor*> MeshActors;
	GetSortedMeshActors(MeshActors);
	TerrainSlotCount = FMath::Max(TerrainSlotCount, MeshActors.Num());

	bool bDeletedAny = false;
	for (int32 ActorIndex = 0; ActorIndex < TerrainSlotCount; ++ActorIndex)
	{
		const FString TerrainSlotName = GetTerrainSlotName(SaveSlotName, ActorIndex);
		if (UGameplayStatics::DoesSaveGameExist(TerrainSlotName, 0))
		{
			bDeletedAny |= UGameplayStatics::DeleteGameInSlot(TerrainSlotName, 0);
		}
	}

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		bDeletedAny |= UGameplayStatics::DeleteGameInSlot(SaveSlotName, 0);
	}

	if (SaveLoadWidget)
	{
		SaveLoadWidget->SetStatusMessage(bDeletedAny
			? FText::Format(FText::FromString(TEXT("槽位 {0} 的存档修改已失效")), FText::AsNumber(SlotIndex + 1))
			: FText::Format(FText::FromString(TEXT("槽位 {0} 没有有效存档")), FText::AsNumber(SlotIndex + 1)));
	}
	UE_LOG(LogDualContourGamePlay, Log, TEXT("Invalidated save slot %d: deleted=%s terrain_slots=%d; current world was not rebuilt."),
		SlotIndex + 1, bDeletedAny ? TEXT("yes") : TEXT("no"), TerrainSlotCount);
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
