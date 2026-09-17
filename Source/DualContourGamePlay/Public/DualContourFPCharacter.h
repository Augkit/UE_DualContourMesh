#pragma once

#include "CoreMinimal.h"
#include "ShooterCharacter.h"
#include "DualContourFPCharacter.generated.h"

class UStaticMeshComponent;
class UAnimInstance;
struct FInputActionValue;

/**
 * Minimal Variant_Shooter-compatible character. Shooter inheritance is required by the
 * official ABP_FP_Pistol cast, while overrides below disable weapon pickup and damage gameplay.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourFPCharacter : public AShooterCharacter
{
	GENERATED_BODY()

	/** Cosmetic pistol parented to the right hand; no gameplay logic references it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PistolMesh;

public:
	ADualContourFPCharacter();
	virtual void Tick(float DeltaSeconds) override;

	/** Applies the official pistol stance after a controller possesses this pawn. */
	void ActivatePistolPose();
	/** Starts or stops the procedural hand motion driven by the controller's dig input. */
	void SetWeaponShakeHeld(bool bHeld) { bWeaponShakeHeld = bHeld; }

	/** Relative transform inside the official Variant_Shooter HandGrip_R socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Pistol")
	FTransform PistolRelativeTransform = FTransform::Identity;

	/** Cycles per second for the subtle held-fire hand motion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Pistol|Shake",
		meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float WeaponShakeFrequency = 5.0f;

	/** Maximum local hand displacement in centimeters. Kept small so aim stays centered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Pistol|Shake")
	FVector WeaponShakeLocationAmplitude = FVector(0.12f, 0.08f, 0.10f);

	/** Maximum local hand rotation in degrees. The attached pistol follows the hand; the camera stays fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Pistol|Shake")
	FRotator WeaponShakeRotationAmplitude = FRotator(0.18f, 0.12f, 0.08f);

	/** Speed used to blend the shake in and out without a visible snap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Pistol|Shake",
		meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float WeaponShakeBlendSpeed = 10.0f;

protected:
	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

public:
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

private:
	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);
	void ApplyWeaponShake(float DeltaSeconds);

	bool bWeaponShakeHeld = false;
	float WeaponShakeAlpha = 0.0f;
	float WeaponShakePhase = 0.0f;
	FTransform FirstPersonMeshRelativeTransform = FTransform::Identity;

	UPROPERTY()
	TSubclassOf<UAnimInstance> PistolFirstPersonAnimClass;

	UPROPERTY()
	TSubclassOf<UAnimInstance> PistolThirdPersonAnimClass;
};
