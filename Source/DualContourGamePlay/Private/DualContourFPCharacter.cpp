#include "DualContourFPCharacter.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"

ADualContourFPCharacter::ADualContourFPCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Match BP_ShooterCharacter's Blueprint override of CharacterMesh0.
	GetMesh()->SetRelativeLocationAndRotation(
		FVector(-10.0f, 0.0f, -96.0f),
		FRotator(0.0f, -90.0f, 0.0f));

	// Start with the same safe pre-weapon AnimBPs as BP_ShooterCharacter. The pistol
	// AnimBPs are activated after possession, because their update graph reads the
	// controller and camera every frame.
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
		if (MeshFinder.Succeeded())
		{
			GetMesh()->SetSkeletalMesh(MeshFinder.Object);
			GetFirstPersonMesh()->SetSkeletalMesh(MeshFinder.Object);
		}

		static ConstructorHelpers::FClassFinder<UAnimInstance> InitialFirstPersonAnimFinder(
			TEXT("/Game/FirstPerson/Anims/ABP_FP_Copy.ABP_FP_Copy_C"));
		if (InitialFirstPersonAnimFinder.Succeeded())
			GetFirstPersonMesh()->SetAnimInstanceClass(InitialFirstPersonAnimFinder.Class);

		static ConstructorHelpers::FClassFinder<UAnimInstance> InitialFullBodyAnimFinder(
			TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
		if (InitialFullBodyAnimFinder.Succeeded())
			GetMesh()->SetAnimInstanceClass(InitialFullBodyAnimFinder.Class);

		static ConstructorHelpers::FClassFinder<UAnimInstance> PistolFirstPersonAnimFinder(
			TEXT("/Game/Variant_Shooter/Anims/ABP_FP_Pistol.ABP_FP_Pistol_C"));
		if (PistolFirstPersonAnimFinder.Succeeded())
			PistolFirstPersonAnimClass = PistolFirstPersonAnimFinder.Class;

		static ConstructorHelpers::FClassFinder<UAnimInstance> PistolThirdPersonAnimFinder(
			TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C"));
		if (PistolThirdPersonAnimFinder.Succeeded())
			PistolThirdPersonAnimClass = PistolThirdPersonAnimFinder.Class;

		static ConstructorHelpers::FObjectFinder<UInputAction> JumpActionFinder(
			TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
		if (JumpActionFinder.Succeeded())
			JumpAction = JumpActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(
			TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
		if (MoveActionFinder.Succeeded())
			MoveAction = MoveActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> LookActionFinder(
			TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
		if (LookActionFinder.Succeeded())
			LookAction = LookActionFinder.Object;

		static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookActionFinder(
			TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
		if (MouseLookActionFinder.Succeeded())
			MouseLookAction = MouseLookActionFinder.Object;
	}

	// Cosmetic static pistol only. It has no weapon actor, collision or gameplay.
	PistolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pistol Mesh"));
	PistolMesh->SetupAttachment(GetFirstPersonMesh(), FName("HandGrip_R"));
	PistolMesh->SetOnlyOwnerSee(true);
	PistolMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	PistolMesh->SetCastShadow(false);
	PistolMesh->SetGenerateOverlapEvents(false);
	PistolMesh->SetCanEverAffectNavigation(false);
	PistolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PistolMeshFinder(
		TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol"));
	if (PistolMeshFinder.Succeeded())
		PistolMesh->SetStaticMesh(PistolMeshFinder.Object);
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

void ADualContourFPCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyWeaponShake(DeltaSeconds);
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
	// Intentionally ignore shooter pickups. The only weapon is the cosmetic SM_Pistol.
}
