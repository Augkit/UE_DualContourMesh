#include "DualContourGamePlayerController.h"
#include "DualContourFPCharacter.h"
#include "DualContourGamePlay.h"
#include "DualContourInitProgressWidget.h"
#include "DualContourMiningReticleWidget.h"
#include "DualContourMeshActor.h"
#include "DualContourModifierComponent.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"

ADualContourGamePlayerController::ADualContourGamePlayerController()
{
	// First person gameplay: the mouse steers the camera, so no cursor.
	bShowMouseCursor = false;
	ProgressWidgetClass = UDualContourInitProgressWidget::StaticClass();
	ReticleWidgetClass = UDualContourMiningReticleWidget::StaticClass();
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
	Super::EndPlay(EndPlayReason);
}

void ADualContourGamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed,
		this, &ADualContourGamePlayerController::OnDigPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released,
		this, &ADualContourGamePlayerController::OnDigReleased);
	InitializeSamplers();
}

void ADualContourGamePlayerController::OnDigPressed()
{
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
