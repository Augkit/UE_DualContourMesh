#pragma once

#include "CoreMinimal.h"
#include "ShooterCharacter.h"
#include "DualContourFPCharacter.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UAnimInstance;
class UNiagaraComponent;
class UNiagaraSystem;
class ADualContourBombActor;
struct FInputActionValue;

/**
 * Minimal Variant_Shooter-compatible character. Shooter inheritance is required by the
 * official ABP_FP_Pistol cast, while overrides below disable weapon pickup and damage gameplay.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourFPCharacter : public AShooterCharacter
{
	GENERATED_BODY()

	/** Cosmetic skeletal pistol (SKM_Pistol; its SK_Pistol skeleton ships the Muzzle socket) parented to the right hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> PistolMesh;

public:
	ADualContourFPCharacter();
	virtual void Tick(float DeltaSeconds) override;

	/** Applies the official pistol stance after a controller possesses this pawn. */
	void ActivatePistolPose();
	/** Starts or stops the procedural hand motion driven by the controller's dig input. */
	void SetWeaponShakeHeld(bool bHeld) { bWeaponShakeHeld = bHeld; }
	/** Starts or stops the muzzle beam fired from the pistol toward the screen center. */
	void SetBeamHeld(bool bHeld);

	/** Spawns and launches a physics bomb along the current view direction. */
	UFUNCTION(BlueprintCallable, Category = "DualContour|Bomb")
	ADualContourBombActor* FireBomb();

	/** Bomb implementation spawned by FireBomb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Bomb")
	TSubclassOf<ADualContourBombActor> BombClass;

	/** Initial speed applied to the bomb rigid body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Bomb", meta = (ClampMin = "0.0", Units = "cm/s"))
	float BombLaunchSpeed = 1800.0f;

	/** Distance in front of the camera used to keep the spawned bomb clear of the character capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Bomb", meta = (ClampMin = "0.0", Units = "cm"))
	float BombSpawnDistance = 100.0f;

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

	/** Cylinder mesh stretched from the muzzle to the screen-center impact point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam")
	TObjectPtr<UStaticMesh> BeamMesh;

	/** Flowing emissive translucent material applied to the beam cylinder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam")
	TObjectPtr<UMaterialInterface> BeamMaterial;

	/** Beam cylinder radius in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam",
		meta = (ClampMin = "0.1"))
	float BeamRadius = 2.0f;

	/** Distance the cylinder extends backward from the Muzzle socket along the beam, so its start hides inside the gun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam",
		meta = (ClampMin = "0.0"))
	float BeamStartPullBack = 15.0f;

	/** Fallback start offset (pistol-local) used only if the pistol mesh is missing its Muzzle socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam")
	FVector BeamMuzzleOffset = FVector(0.0f, 0.0f, 0.0f);

	/** Maximum distance of the screen-center trace used as the beam end point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam",
		meta = (ClampMin = "100.0"))
	float BeamMaxDistance = 5000.0f;

	/** Niagara system used for the persistent impact effect while the held beam has a blocking hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="DualContour|Beam|Splash")
	TObjectPtr<UNiagaraSystem> SplashSystem;

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

public:
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

private:
	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);
	void ApplyWeaponShake(float DeltaSeconds);
	void EnsureBeamVisual();
	void DestroyBeamVisual();
	void UpdateBeam(float DeltaSeconds);
	void UpdateSplash(bool bHitting);
	void DeactivateSplash();
	bool GetScreenCenterRay(FVector& OutOrigin, FVector& OutDirection) const;
	FVector GetMuzzleWorldLocation() const;

	bool bWeaponShakeHeld = false;
	float WeaponShakeAlpha = 0.0f;
	float WeaponShakePhase = 0.0f;
	FTransform FirstPersonMeshRelativeTransform = FTransform::Identity;

	bool bBeamHeld = false;
	FVector BeamImpactPoint = FVector::ZeroVector;
	FVector BeamImpactNormal = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BeamMeshComponent;

	/** Persistent impact Niagara instance; it naturally finishes after Deactivate(). */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SplashComponent;

	UPROPERTY()
	TSubclassOf<UAnimInstance> PistolFirstPersonAnimClass;

	UPROPERTY()
	TSubclassOf<UAnimInstance> PistolThirdPersonAnimClass;
};
