#include "DualContourFPCharacter.h"
#include "DualContourBombActor.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	const FName MuzzleSocketName = TEXT("Muzzle");
}

ADualContourFPCharacter::ADualContourFPCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	BombClass = ADualContourBombActor::StaticClass();

	// Match BP_ShooterCharacter's Blueprint override of CharacterMesh0.
	GetMesh()->SetRelativeLocationAndRotation(
		FVector(-10.0f, 0.0f, -96.0f),
		FRotator(0.0f, -90.0f, 0.0f));

	// Start with the same safe pre-weapon AnimBPs as BP_ShooterCharacter. The pistol
	// AnimBPs are activated after possession, because their update graph reads the
	// controller and camera every frame.
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
			TEXT("/DualContourMesh/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
		if (MeshFinder.Succeeded())
		{
			GetMesh()->SetSkeletalMesh(MeshFinder.Object);
			GetFirstPersonMesh()->SetSkeletalMesh(MeshFinder.Object);
		}

		static ConstructorHelpers::FClassFinder<UAnimInstance> InitialFirstPersonAnimFinder(
			TEXT("/DualContourMesh/FirstPerson/Anims/ABP_FP_Copy.ABP_FP_Copy_C"));
		if (InitialFirstPersonAnimFinder.Succeeded())
			GetFirstPersonMesh()->SetAnimInstanceClass(InitialFirstPersonAnimFinder.Class);

		static ConstructorHelpers::FClassFinder<UAnimInstance> InitialFullBodyAnimFinder(
			TEXT("/DualContourMesh/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
		if (InitialFullBodyAnimFinder.Succeeded())
			GetMesh()->SetAnimInstanceClass(InitialFullBodyAnimFinder.Class);

		static ConstructorHelpers::FClassFinder<UAnimInstance> PistolFirstPersonAnimFinder(
			TEXT("/DualContourMesh/Variant_Shooter/Anims/ABP_FP_Pistol.ABP_FP_Pistol_C"));
		if (PistolFirstPersonAnimFinder.Succeeded())
			PistolFirstPersonAnimClass = PistolFirstPersonAnimFinder.Class;

		static ConstructorHelpers::FClassFinder<UAnimInstance> PistolThirdPersonAnimFinder(
			TEXT("/DualContourMesh/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C"));
		if (PistolThirdPersonAnimFinder.Succeeded())
			PistolThirdPersonAnimClass = PistolThirdPersonAnimFinder.Class;

		static ConstructorHelpers::FObjectFinder<UInputAction> JumpActionFinder(
			TEXT("/DualContourMesh/Input/Actions/IA_Jump.IA_Jump"));
		if (JumpActionFinder.Succeeded())
			JumpAction = JumpActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(
			TEXT("/DualContourMesh/Input/Actions/IA_Move.IA_Move"));
		if (MoveActionFinder.Succeeded())
			MoveAction = MoveActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(
			TEXT("/DualContourMesh/Input/Actions/IA_Look.IA_Look"));
		if (LookActionFinder.Succeeded())
			LookAction = LookActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookActionFinder(
			TEXT("/DualContourMesh/Input/Actions/IA_MouseLook.IA_MouseLook"));
		if (MouseLookActionFinder.Succeeded())
			MouseLookAction = MouseLookActionFinder.Object;
	}

	// Cosmetic skeletal pistol (ships with the Muzzle socket) parented to the right hand.
	PistolMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Pistol Mesh"));
	PistolMesh->SetupAttachment(GetFirstPersonMesh(), FName("HandGrip_R"));
	PistolMesh->SetOnlyOwnerSee(true);
	PistolMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	PistolMesh->SetCastShadow(false);
	PistolMesh->SetGenerateOverlapEvents(false);
	PistolMesh->SetCanEverAffectNavigation(false);
	PistolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PistolMeshFinder(
			TEXT("/DualContourMesh/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol"));
	if (PistolMeshFinder.Succeeded())
		PistolMesh->SetSkeletalMesh(PistolMeshFinder.Object);

	// Built by the DualContourBeam commandlet in the plugin content.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeamMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (BeamMeshFinder.Succeeded())
		BeamMesh = BeamMeshFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMaterialFinder(
		TEXT("/DualContourMesh/FX/M_DualContourBeam.M_DualContourBeam"));
	if (BeamMaterialFinder.Succeeded())
		BeamMaterial = BeamMaterialFinder.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SplashSystemFinder(
		TEXT("/DualContourMesh/FX/N_Sparks.N_Sparks"));
	if (SplashSystemFinder.Succeeded())
		SplashSystem = SplashSystemFinder.Object;
}

ADualContourBombActor* ADualContourFPCharacter::FireBomb()
{
	UWorld* World = GetWorld();
	if (!World || !BombClass)
		return nullptr;

	FVector ViewLocation;
	FRotator ViewRotation;
	if (AController* OwningController = GetController())
		OwningController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	else
	{
		ViewLocation = GetActorLocation();
		ViewRotation = GetActorRotation();
	}

	const FVector LaunchDirection = ViewRotation.Vector();
	const FVector SpawnLocation = ViewLocation + LaunchDirection * BombSpawnDistance;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ADualContourBombActor* Bomb = World->SpawnActor<ADualContourBombActor>(
		BombClass, SpawnLocation, LaunchDirection.Rotation(), SpawnParameters);
	if (Bomb)
		Bomb->LaunchBomb(LaunchDirection * BombLaunchSpeed + GetVelocity());
	return Bomb;
}

void ADualContourFPCharacter::ActivatePistolPose()
{
	if (PistolFirstPersonAnimClass)
		GetFirstPersonMesh()->SetAnimInstanceClass(PistolFirstPersonAnimClass);
	if (PistolThirdPersonAnimClass)
		GetMesh()->SetAnimInstanceClass(PistolThirdPersonAnimClass);
}

void ADualContourFPCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	FirstPersonMeshRelativeTransform = GetFirstPersonMesh()->GetRelativeTransform();
	PistolMesh->SetRelativeTransform(PistolRelativeTransform);
}

void ADualContourFPCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyBeamVisual();
	DeactivateSplash();
	Super::EndPlay(EndPlayReason);
}

void ADualContourFPCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyWeaponShake(DeltaSeconds);
	if (bBeamHeld)
		UpdateBeam(DeltaSeconds);
}

void ADualContourFPCharacter::SetBeamHeld(bool bHeld)
{
	if (bBeamHeld == bHeld)
		return;

	bBeamHeld = bHeld;
	if (bHeld)
	{
		UpdateBeam(0.0f);
	}
	else
	{
		DestroyBeamVisual();
		DeactivateSplash();
	}
}

void ADualContourFPCharacter::EnsureBeamVisual()
{
	if (IsValid(BeamMeshComponent) || !BeamMesh)
		return;

	// The cylinder is positioned in world space every tick: midpoint between the muzzle and
	// the impact point, Z axis aligned to the beam direction, Z scale stretched to the length.
	BeamMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("BeamCylinder"));
	BeamMeshComponent->SetStaticMesh(BeamMesh);
	if (BeamMaterial)
		BeamMeshComponent->SetMaterial(0, BeamMaterial);
	BeamMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamMeshComponent->SetCastShadow(false);
	BeamMeshComponent->SetGenerateOverlapEvents(false);
	BeamMeshComponent->SetCanEverAffectNavigation(false);
	BeamMeshComponent->SetupAttachment(GetRootComponent());
	BeamMeshComponent->SetAbsolute(true, true, true);
	BeamMeshComponent->RegisterComponent();
	BeamMeshComponent->SetVisibility(true);
}

void ADualContourFPCharacter::DestroyBeamVisual()
{
	if (IsValid(BeamMeshComponent))
		BeamMeshComponent->DestroyComponent();
	BeamMeshComponent = nullptr;
}

void ADualContourFPCharacter::UpdateBeam(float DeltaSeconds)
{
	FVector RayOrigin;
	FVector RayDirection;
	if (!GetScreenCenterRay(RayOrigin, RayDirection))
		return;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DualContourBeam), /*bTraceComplex=*/ false);
	QueryParams.AddIgnoredActor(this);
	const FVector TraceEnd = RayOrigin + RayDirection * BeamMaxDistance;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(
		Hit, RayOrigin, TraceEnd, ECC_Visibility, QueryParams);
	const FVector EndPoint = bBlockingHit ? Hit.ImpactPoint : TraceEnd;
	BeamImpactPoint = bBlockingHit ? Hit.ImpactPoint : FVector::ZeroVector;
	BeamImpactNormal = bBlockingHit ? Hit.ImpactNormal : FVector::ZeroVector;

	EnsureBeamVisual();
	if (BeamMeshComponent)
	{
		const FVector BeamStart = GetMuzzleWorldLocation();
		const FVector BeamDelta = EndPoint - BeamStart;
		const float BeamLength = BeamDelta.Size();
		if (BeamLength > KINDA_SMALL_NUMBER)
		{
			const FVector BeamDirection = BeamDelta / BeamLength;
			// Extend the cylinder backward past the Muzzle socket so the start hides inside the gun.
			const FVector VisualStart = BeamStart - BeamDirection * BeamStartPullBack;
			const float VisualLength = BeamLength + BeamStartPullBack;

			BeamMeshComponent->SetWorldLocation((VisualStart + EndPoint) * 0.5f);
			BeamMeshComponent->SetWorldRotation(FRotationMatrix::MakeFromZ(BeamDirection).Rotator());
			// BasicShapes/Cylinder: radius 50, height 100 along Z.
			BeamMeshComponent->SetWorldScale3D(FVector(
				BeamRadius / 50.0f, BeamRadius / 50.0f, VisualLength / 100.0f));
		}
	}

	UpdateSplash(bBlockingHit);
}

void ADualContourFPCharacter::UpdateSplash(bool bHitting)
{
	if (!bHitting || !SplashSystem)
	{
		DeactivateSplash();
		return;
	}

	// Add Velocity in Cone uses Local space with Cone Axis +Z. Align that local
	// axis to the surface normal so sparks leave the impact surface instead of
	// always using world-up. The Niagara asset owns the continuous Spawn Rate.
	const FRotator SplashRotation = BeamImpactNormal.IsNearlyZero()
		? FRotator::ZeroRotator
		: FRotationMatrix::MakeFromZ(BeamImpactNormal.GetSafeNormal()).Rotator();

	if (!IsValid(SplashComponent))
	{
		SplashComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, SplashSystem, BeamImpactPoint, SplashRotation, FVector::OneVector,
			/*bAutoDestroy=*/ true, /*bAutoActivate=*/ true, ENCPoolMethod::None);
	}
	else
	{
		// Keep the persistent effect on the current impact point while held.
		SplashComponent->SetWorldLocationAndRotation(BeamImpactPoint, SplashRotation);
	}
}

void ADualContourFPCharacter::DeactivateSplash()
{
	if (IsValid(SplashComponent))
	{
		// Natural deactivation stops new particles but allows existing particles
		// to finish. bAutoDestroy removes the component after the system completes.
		SplashComponent->Deactivate();
		SplashComponent = nullptr;
	}
}

bool ADualContourFPCharacter::GetScreenCenterRay(FVector& OutOrigin, FVector& OutDirection) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!PlayerController || !Viewport)
		return false;

	FVector2D ViewportSize;
	Viewport->GetViewportSize(ViewportSize);
	if (ViewportSize.IsNearlyZero())
		return false;

	return PlayerController->DeprojectScreenPositionToWorld(
		ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f, OutOrigin, OutDirection);
}

FVector ADualContourFPCharacter::GetMuzzleWorldLocation() const
{
	if (PistolMesh)
	{
		// Prefer the Muzzle socket bound on SM_Pistol; fall back to the manual offset.
		if (PistolMesh->DoesSocketExist(MuzzleSocketName))
			return PistolMesh->GetSocketLocation(MuzzleSocketName);
		return PistolMesh->GetComponentTransform().TransformPosition(BeamMuzzleOffset);
	}
	return GetActorLocation();
}

void ADualContourFPCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Copy only ATP_FirstPersonCharacter's official base bindings. Deliberately do
	// not call AShooterCharacter, which would add gameplay fire/switch actions.
	if (UEnhancedInputComponent* EnhancedInputComponent =
		Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(
			JumpAction, ETriggerEvent::Started, this, &ADualContourFPCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(
			JumpAction, ETriggerEvent::Completed, this, &ADualContourFPCharacter::DoJumpEnd);
		EnhancedInputComponent->BindAction(
			MoveAction, ETriggerEvent::Triggered, this, &ADualContourFPCharacter::MoveInput);
		EnhancedInputComponent->BindAction(
			LookAction, ETriggerEvent::Triggered, this, &ADualContourFPCharacter::LookInput);
		EnhancedInputComponent->BindAction(
			MouseLookAction, ETriggerEvent::Triggered, this, &ADualContourFPCharacter::LookInput);
	}

}

void ADualContourFPCharacter::MoveInput(const FInputActionValue& Value)
{
	const FVector2D Movement = Value.Get<FVector2D>();
	DoMove(Movement.X, Movement.Y);
}

void ADualContourFPCharacter::LookInput(const FInputActionValue& Value)
{
	const FVector2D Look = Value.Get<FVector2D>();
	DoAim(Look.X, Look.Y);
}

void ADualContourFPCharacter::ApplyWeaponShake(float DeltaSeconds)
{
	const float TargetAlpha = bWeaponShakeHeld ? 1.0f : 0.0f;
	WeaponShakeAlpha = FMath::FInterpTo(
		WeaponShakeAlpha, TargetAlpha, DeltaSeconds, WeaponShakeBlendSpeed);

	if (!bWeaponShakeHeld && WeaponShakeAlpha < KINDA_SMALL_NUMBER)
	{
		WeaponShakeAlpha = 0.0f;
		WeaponShakePhase = 0.0f;
		GetFirstPersonMesh()->SetRelativeTransform(FirstPersonMeshRelativeTransform);
		return;
	}

	WeaponShakePhase = FMath::Fmod(
		WeaponShakePhase + DeltaSeconds * WeaponShakeFrequency * UE_TWO_PI,
		2.0f * UE_TWO_PI);

	const float PrimaryWave = FMath::Sin(WeaponShakePhase);
	const float SecondaryWave = FMath::Sin(WeaponShakePhase * 2.0f + UE_PI / 3.0f);
	const float TertiaryWave = FMath::Sin(WeaponShakePhase * 1.5f + UE_PI / 2.0f);

	const FVector LocationOffset(
		WeaponShakeLocationAmplitude.X * PrimaryWave * WeaponShakeAlpha,
		WeaponShakeLocationAmplitude.Y * SecondaryWave * WeaponShakeAlpha,
		WeaponShakeLocationAmplitude.Z * TertiaryWave * WeaponShakeAlpha);
	const FRotator RotationOffset(
		WeaponShakeRotationAmplitude.Pitch * PrimaryWave * WeaponShakeAlpha,
		WeaponShakeRotationAmplitude.Yaw * SecondaryWave * WeaponShakeAlpha,
		WeaponShakeRotationAmplitude.Roll * TertiaryWave * WeaponShakeAlpha);

	// Move the first-person arms, not the pistol. Because the pistol remains attached
	// to HandGrip_R, it can only move as a consequence of the hand/arm motion.
	GetFirstPersonMesh()->SetRelativeLocation(
		FirstPersonMeshRelativeTransform.GetLocation() + LocationOffset);
	GetFirstPersonMesh()->SetRelativeRotation(
		FirstPersonMeshRelativeTransform.Rotator() + RotationOffset);
	GetFirstPersonMesh()->SetRelativeScale3D(
		FirstPersonMeshRelativeTransform.GetScale3D());
}

float ADualContourFPCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	return 0.0f;
}

void ADualContourFPCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// Intentionally ignore shooter pickups. The only weapon is the cosmetic SK_Pistol.
}
