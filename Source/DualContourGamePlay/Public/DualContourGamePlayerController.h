#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourGamePlayerController.generated.h"

class UDualContourInitProgressWidget;
class UDualContourMiningReticleWidget;
class UDualContourModifierComponent;
class UInputMappingContext;
class UParticleSystemComponent;
class UStaticMeshComponent;
class UVolumeSampler;

/**
 * First-person PlayerController for DualContour gameplay.
 *
 * The pawn is the UE first person template character (BP_FirstPersonCharacter), which supplies the
 * animated arms, locomotion and head-socket camera; this controller activates the template's input
 * mapping contexts, attaches the grenade-launcher weapon and mining beam to the pawn's camera, and
 * drives the mining loop: holding the dig button accumulates progress on the reticle ring and carves
 * a slice out of the terrain with the selected volume sampler each time the hold duration completes.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourGamePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADualContourGamePlayerController();

	/** Overlay widget class shown while DualContour mesh actors initialize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Progress")
	TSubclassOf<UDualContourInitProgressWidget> ProgressWidgetClass;

	/** Draw order passed to AddToViewport so the overlay covers other startup UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Progress")
	int32 ProgressWidgetZOrder = 100;

	/** Seconds the dig button must be held before one terrain slice is carved away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0.05"))
	float DigHoldDuration = 1.0f;

	/** Reticle widget class rendering the center dot and ring progress bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	TSubclassOf<UDualContourMiningReticleWidget> ReticleWidgetClass;

	/** Draw order passed to AddToViewport for the reticle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	int32 ReticleZOrder = 90;

	/** Index into ModifierComponent->Samplers selecting the sampler used while digging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0"))
	int32 SelectedSamplerIndex = 0;

	/** Custom sampler instances appended to ModifierComponent->Samplers at input setup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "DualContour|Mining")
	TArray<TObjectPtr<UVolumeSampler>> AdditionalSamplers;

	/** Material ID assigned by the selected sampler when a dig is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 MaterialId = 0;

	/** Applies the selected samplers to the terrain via screen-center raycasts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	TObjectPtr<UDualContourModifierComponent> ModifierComponent;

	/** Selects a configured sampler, including custom sampler classes, by component-array index. */
	UFUNCTION(BlueprintCallable, Category = "DualContour|Mining")
	void SetSelectedSamplerIndex(int32 SamplerIndex);

	/** Current dig hold progress in [0, 1]; zero while the dig button is released. */
	UFUNCTION(BlueprintPure, Category = "DualContour|Mining")
	float GetDigProgress() const { return bDigHeld ? DigProgress : 0.0f; }

	/** Camera manager class providing the template first-person pitch limits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|FirstPerson")
	TSubclassOf<APlayerCameraManager> CameraManagerClass;

	/** Mapping contexts activated for the template character's Enhanced Input actions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|FirstPerson")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;

	/** Grenade-launcher weapon mesh spawned on the possessed pawn and attached to its camera. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "DualContour|Weapon")
	TObjectPtr<UStaticMeshComponent> Weapon;

	/** Weapon offset from the hand_r socket (negative X pulls the grip into the palm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon")
	FVector WeaponRelativeLocation = FVector(-8.0f, 0.0f, 0.0f);

	/** Weapon rotation relative to the hand_r socket; the launcher barrel lies on local +Y, so -90 yaw aims it along the fingers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon")
	FRotator WeaponRelativeRotation = FRotator(0.0f, -90.0f, 0.0f);

	/** Mining beam effect anchored at the weapon muzzle (barrel direction is weapon-local +Y). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "DualContour|Weapon")
	TObjectPtr<UParticleSystemComponent> Beam;

	/** Muzzle offset from the weapon origin, in weapon-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon")
	FVector BeamRelativeLocation = FVector(0.0f, 75.0f, 0.0f);

	/** Authored beam effect length in local units; the component X scale spans muzzle-to-hit divided by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon", meta = (ClampMin = "1.0"))
	float BeamUnitLength = 100.0f;

	/** Beam length in world units when the aim ray hits nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon", meta = (ClampMin = "100.0"))
	float BeamMaxRange = 100000.0f;

	/** Boolean variable on the first person arms anim BP that locks the upper body into the camera-aimed shooting stance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon")
	FName CameraLockAnimVariable = TEXT("CameraLock");

	/** Control name inside the arms' FP warp control rig enabling the camera-locked shooting stance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Weapon")
	FName CameraLockControlName = TEXT("Ctrl_CameraLock");

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	virtual void SetupInputComponent() override;

private:
	void ShowInitializationProgress();
	void HandleProgressFinished();

	void OnDigPressed();
	void OnDigReleased();
	void PerformDig();
	void InitializeSamplers();
	void EnsureReticle();

	/** Spawns the weapon and beam on the possessed pawn and attaches them to its camera. */
	void AttachFirstPersonEquipment();
	/** Activates the beam effect; it follows the screen-center aim while firing. */
	void StartBeamFire();
	void StopBeamFire();
	/** Toggles the camera-locked shooting stance on the first person arms animation. */
	void SetUpperBodyCameraLock(bool bLocked);
	/** Stretches the beam effect from the muzzle to the current screen-center aim hit point. */
	void UpdateBeam();
	/** If the pawn still sits at its spawn point and that point ended up inside the generated terrain, lifts it out. */
	void SnapPawnAboveTerrainIfEmbedded();

	UPROPERTY(Transient)
	TObjectPtr<UDualContourInitProgressWidget> ProgressWidget;

	UPROPERTY(Transient)
	TObjectPtr<UDualContourMiningReticleWidget> ReticleWidget;

	/** Loaded by the constructor; spawned onto each possessed pawn's camera. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> WeaponMeshAsset;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> BeamTemplateAsset;

	bool bDigHeld = false;
	bool bBeamFiring = false;
	bool bHasSpawnRecord = false;
	FVector SpawnRecordLocation = FVector::ZeroVector;
	float DigProgress = 0.0f;
};
