#include "DualContourGamePlayerController.h"
#include "DualContourGamePlay.h"
#include "DualContourInitProgressWidget.h"
#include "DualContourMiningReticleWidget.h"
#include "DualContourMeshActor.h"
#include "DualContourModifierComponent.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "AnimNode_ControlRig.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UnrealType.h"

ADualContourGamePlayerController::ADualContourGamePlayerController()
{
	// First-person gameplay: the mouse steers the camera and digs, so no cursor.
	bShowMouseCursor = false;
	ProgressWidgetClass = UDualContourInitProgressWidget::StaticClass();
	ReticleWidgetClass = UDualContourMiningReticleWidget::StaticClass();
	ModifierComponent = CreateDefaultSubobject<UDualContourModifierComponent>(TEXT("DualContourModifier"));

	{
		static ConstructorHelpers::FClassFinder<APlayerCameraManager> CameraManagerFinder(
			TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCameraManager.BP_FirstPersonCameraManager_C"));
		if (CameraManagerFinder.Succeeded())
			CameraManagerClass = CameraManagerFinder.Class;
	}

	{
		static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultMappingContextFinder(
			TEXT("/Game/Input/IMC_Default.IMC_Default"));
		static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookMappingContextFinder(
			TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
		if (DefaultMappingContextFinder.Succeeded())
			DefaultMappingContexts.Add(DefaultMappingContextFinder.Object);
		if (MouseLookMappingContextFinder.Succeeded())
			DefaultMappingContexts.Add(MouseLookMappingContextFinder.Object);
	}

	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> WeaponMeshFinder(
			TEXT("/Game/Weapons/GrenadeLauncher/Meshes/SM_GrenadeLauncher.SM_GrenadeLauncher"));
		if (WeaponMeshFinder.Succeeded())
			WeaponMeshAsset = WeaponMeshFinder.Object;
	}

	{
		static ConstructorHelpers::FObjectFinder<UParticleSystem> BeamFinder(
			TEXT("/Game/RPGEffects/Particles/P_Mage_EnergyBeam.P_Mage_EnergyBeam"));
		if (BeamFinder.Succeeded())
			BeamTemplateAsset = BeamFinder.Object;
	}
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
	else if (!FMath::IsNearlyZero(DigProgress))
	{
		DigProgress = 0.0f;
	}

	if (ReticleWidget)
		ReticleWidget->SetProgress(GetDigProgress());

	if (bBeamFiring)
		UpdateBeam();

	// Re-apply every frame while held: the anim graph may reset the rig control during evaluation.
	if (bDigHeld)
		SetUpperBodyCameraLock(true);
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

void ADualContourGamePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	SpawnRecordLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	bHasSpawnRecord = InPawn != nullptr;

	AttachFirstPersonEquipment();
}

void ADualContourGamePlayerController::OnUnPossess()
{
	StopBeamFire();
	// The weapon lives on the pawn; it goes away with the pawn.
	Weapon = nullptr;
	Beam = nullptr;
	Super::OnUnPossess();
}

void ADualContourGamePlayerController::AttachFirstPersonEquipment()
{
	APawn* PossessedPawn = GetPawn();
	if (!PossessedPawn || !WeaponMeshAsset || Weapon)
		return;

	// Prefer the first-person arms mesh (only-owner-see) offering a hand socket; the control rig
	// aims that hand at the camera, so a weapon on the socket is carried by the animated hand.
	USkeletalMeshComponent* HandMesh = nullptr;
	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	PossessedPawn->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
	for (USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
	{
		if (SkeletalMeshComponent->DoesSocketExist(FName("hand_r")))
		{
			HandMesh = SkeletalMeshComponent;
			if (HandMesh->bOnlyOwnerSee)
				break;
		}
	}
	UCameraComponent* PawnCamera = PossessedPawn->FindComponentByClass<UCameraComponent>();

	// Spawned on the pawn so bOnlyOwnerSee resolves against the view target.
	Weapon = NewObject<UStaticMeshComponent>(PossessedPawn, TEXT("DualContourWeapon"));
	Weapon->SetStaticMesh(WeaponMeshAsset);
	Weapon->bOnlyOwnerSee = true;
	Weapon->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	Weapon->SetCastShadow(false);
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Weapon->RegisterComponent();

	if (HandMesh)
	{
		Weapon->AttachToComponent(HandMesh, FAttachmentTransformRules::KeepRelativeTransform, FName("hand_r"));
		UE_LOG(LogDualContourGamePlay, Log,
			TEXT("Attached the first person weapon to the hand_r socket of '%s'."), *HandMesh->GetName());
	}
	else if (PawnCamera)
	{
		Weapon->AttachToComponent(PawnCamera, FAttachmentTransformRules::KeepRelativeTransform);
		UE_LOG(LogDualContourGamePlay, Warning,
			TEXT("No skeletal mesh with a hand_r socket on pawn '%s'; attached the weapon to its camera instead."),
			*GetNameSafe(PossessedPawn));
	}
	else
	{
		UE_LOG(LogDualContourGamePlay, Warning,
			TEXT("Could not attach the first person weapon: pawn '%s' has neither a hand socket nor a camera."),
			*GetNameSafe(PossessedPawn));
		return;
	}
	Weapon->SetRelativeLocation(WeaponRelativeLocation);
	Weapon->SetRelativeRotation(WeaponRelativeRotation);

	if (BeamTemplateAsset)
	{
		Beam = NewObject<UParticleSystemComponent>(PossessedPawn, TEXT("DualContourBeam"));
		Beam->SetTemplate(BeamTemplateAsset);
		Beam->bAutoActivate = false;
		Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Beam->RegisterComponent();
		Beam->AttachToComponent(Weapon, FAttachmentTransformRules::KeepRelativeTransform);
		Beam->SetRelativeLocation(BeamRelativeLocation);
	}
}

void ADualContourGamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADualContourGamePlayerController::OnDigPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ADualContourGamePlayerController::OnDigReleased);

	InitializeSamplers();
}

void ADualContourGamePlayerController::OnDigPressed()
{
	bDigHeld = true;
	SetUpperBodyCameraLock(true);
	StartBeamFire();
}

void ADualContourGamePlayerController::OnDigReleased()
{
	bDigHeld = false;
	SetUpperBodyCameraLock(false);
	StopBeamFire();
}

void ADualContourGamePlayerController::SetUpperBodyCameraLock(bool bLocked)
{
	APawn* PossessedPawn = GetPawn();
	if (!PossessedPawn)
		return;

	// The first person arms are the only-owner-see skeletal mesh carrying the weapon.
	TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	PossessedPawn->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
	for (USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
	{
		if (!SkeletalMeshComponent->bOnlyOwnerSee)
			continue;

		UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
		if (!AnimInstance)
			return;

		bool bApplied = false;

		// Path 1: an exposed anim BP variable the graph feeds into the rig control.
		if (FBoolProperty* LockProperty = FindFProperty<FBoolProperty>(AnimInstance->GetClass(), CameraLockAnimVariable))
		{
			LockProperty->SetPropertyValue_InContainer(AnimInstance, bLocked);
			bApplied = true;
		}

		// Path 2: set the control rig control directly on the anim graph's ControlRig nodes.
		for (TFieldIterator<FStructProperty> PropertyIt(AnimInstance->GetClass()); PropertyIt; ++PropertyIt)
		{
			bool bIsControlRigNode = false;
			for (const UStruct* NodeStruct = PropertyIt->Struct; NodeStruct; NodeStruct = NodeStruct->GetSuperStruct())
			{
				if (NodeStruct == FAnimNode_ControlRig::StaticStruct())
				{
					bIsControlRigNode = true;
					break;
				}
			}
			if (!bIsControlRigNode)
				continue;

			FAnimNode_ControlRig* RigNode = PropertyIt->ContainerPtrToValuePtr<FAnimNode_ControlRig>(AnimInstance);
			UControlRig* Rig = RigNode ? RigNode->GetControlRig() : nullptr;
			if (!Rig)
				continue;

			if (FRigControlElement* Control = Rig->FindControl(CameraLockControlName))
			{
				switch (Control->Settings.ControlType)
				{
				case ERigControlType::Bool:
					Rig->SetControlValue<bool>(CameraLockControlName, bLocked);
					bApplied = true;
					break;
				case ERigControlType::Float:
					Rig->SetControlValue<float>(CameraLockControlName, bLocked ? 1.0f : 0.0f);
					bApplied = true;
					break;
				default:
					UE_LOG(LogDualContourGamePlay, Warning,
						TEXT("Control '%s' on rig '%s' is of unsupported type %d; cannot toggle the shooting stance."),
						*CameraLockControlName.ToString(), *Rig->GetName(),
						static_cast<int32>(Control->Settings.ControlType));
					break;
				}
			}
		}

		if (!bApplied)
			UE_LOG(LogDualContourGamePlay, Warning,
				TEXT("Found neither a boolean '%s' variable nor a '%s' control on the first person arms; cannot toggle the shooting stance."),
				*CameraLockAnimVariable.ToString(), *CameraLockControlName.ToString());
		return;
	}
}

void ADualContourGamePlayerController::StartBeamFire()
{
	bBeamFiring = true;
	if (Beam && Beam->Template)
	{
		Beam->ActivateSystem(/*bReset=*/ true);
		UpdateBeam();
	}
}

void ADualContourGamePlayerController::StopBeamFire()
{
	bBeamFiring = false;
	if (Beam)
		Beam->DeactivateSystem();
}

void ADualContourGamePlayerController::UpdateBeam()
{
	UWorld* World = GetWorld();
	if (!World || !Beam || !Beam->Template)
		return;

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * BeamMaxRange;
	const FCollisionQueryParams QueryParams(TEXT("DualContourWeaponBeam"), /*bTraceComplex=*/ true, /*IgnoreActor=*/ GetPawn());
	FHitResult Hit;
	World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, QueryParams);

	const FVector BeamStart = Beam->GetComponentLocation();
	const FVector BeamEnd = Hit.bBlockingHit ? Hit.ImpactPoint : TraceEnd;
	const FVector BeamVector = BeamEnd - BeamStart;
	const float BeamLength = BeamVector.Size();
	if (BeamLength < 1.0f)
		return;

	Beam->SetWorldRotation(FRotationMatrix::MakeFromX(BeamVector).Rotator());
	Beam->SetWorldScale3D(FVector(BeamLength / FMath::Max(BeamUnitLength, 1.0f), 1.0f, 1.0f));
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

	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f, WorldOrigin, WorldDir))
		return;

	if (ModifierComponent->ModifyDualContourWithRay(WorldOrigin, WorldDir, SelectedSamplerIndex, MaterialId, /*bExcavate=*/ true))
	{
		UE_LOG(LogDualContourGamePlay, Verbose,
			TEXT("Dug a terrain slice with sampler %d after a %.2fs hold."), SelectedSamplerIndex, DigHoldDuration);
	}
}

void ADualContourGamePlayerController::InitializeSamplers()
{
	if (!ModifierComponent)
		return;

	// Preserve samplers configured on the component, including Blueprint subclasses.
	// When AdditionalSamplers are supplied, they replace the default dig sampler.
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
		if (IsValid(Sampler))
			ModifierComponent->Samplers.Add(Sampler);

	if (!ModifierComponent->Samplers.IsEmpty())
		SelectedSamplerIndex = FMath::Clamp(SelectedSamplerIndex, 0, ModifierComponent->Samplers.Num() - 1);
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
		UE_LOG(LogDualContourGamePlay, Log, TEXT("Showing the mining reticle."));
	}
}

void ADualContourGamePlayerController::SnapPawnAboveTerrainIfEmbedded()
{
	UWorld* World = GetWorld();
	APawn* PossessedPawn = GetPawn();
	if (!World || !PossessedPawn || !bHasSpawnRecord)
		return;

	// Only rescue pawns the player has not already steered away from their spawn point.
	if (FVector::DistSquared2D(PossessedPawn->GetActorLocation(), SpawnRecordLocation) > FMath::Square(300.0f))
		return;

	const FVector Current = PossessedPawn->GetActorLocation();
	const FCollisionQueryParams QueryParams(TEXT("DualContourSpawnSnap"), /*bTraceComplex=*/ true, PossessedPawn);
	FHitResult Hit;
	// A surface above the current height means the pawn spawned inside the generated terrain.
	if (World->LineTraceSingleByChannel(Hit, FVector(Current.X, Current.Y, Current.Z + 10000.0f),
		FVector(Current.X, Current.Y, Current.Z - 1000.0f), ECC_Visibility, QueryParams)
		&& Hit.ImpactPoint.Z > Current.Z + 10.0f)
	{
		PossessedPawn->SetActorLocation(FVector(Current.X, Current.Y, Hit.ImpactPoint.Z + 120.0f));
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

	// Terrain collision only exists now, after contouring finished; rescue a spawn point inside it.
	SnapPawnAboveTerrainIfEmbedded();
}
