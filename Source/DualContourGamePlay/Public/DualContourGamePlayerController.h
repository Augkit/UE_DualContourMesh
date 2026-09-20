#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "DualContourGamePlayerController.generated.h"

class UInputMappingContext;
class UDualContourInitProgressWidget;
class UDualContourMiningReticleWidget;
class UDualContourSaveLoadWidget;
class UDualContourModifierComponent;
class USoundBase;
class USoundAttenuation;
class UVolumeSampler;

/**
 * Minimal first person PlayerController: registers the Enhanced Input mapping contexts
 * used by ADualContourFPCharacter and shows the fullscreen DualContour initialization
 * progress overlay when the game starts.
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

	/** Mapping contexts added on BeginPlay so the first person character receives input. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Input")
	TArray<TObjectPtr<UInputMappingContext>> DefaultMappingContexts;

	/** Seconds the left mouse button must be held before one terrain piece is removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0.05"))
	float DigHoldDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	TSubclassOf<UDualContourMiningReticleWidget> ReticleWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	int32 ReticleZOrder = 90;

	/** Widget class shown by SaveLoadToggleKey. The default is the native save/load panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|SaveLoad")
	TSubclassOf<UDualContourSaveLoadWidget> SaveLoadWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|SaveLoad")
	int32 SaveLoadWidgetZOrder = 110;

	/** Key used to open and close the save/load panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|SaveLoad")
	FKey SaveLoadToggleKey = EKeys::Tab;

	/** Key used by the first-person template to launch a physics bomb. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Input")
	FKey BombFireKey = EKeys::RightMouseButton;

	/** Sound played when the left mouse button is released after mining. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Audio")
	TObjectPtr<USoundBase> DigReleaseSound;

	/** Runtime attenuation override so the distant laser impact is not globally audible. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> DigReleaseSoundAttenuation;

	/** Index of the sampler used when the progress ring completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0"))
	int32 SelectedSamplerIndex = 0;

	/** Custom sampler instances. When supplied, they replace the default sphere sampler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "DualContour|Mining")
	TArray<TObjectPtr<UVolumeSampler>> AdditionalSamplers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 MaterialId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour|Mining")
	TObjectPtr<UDualContourModifierComponent> ModifierComponent;

	UFUNCTION(BlueprintCallable, Category = "DualContour|Mining")
	void SetSelectedSamplerIndex(int32 SamplerIndex);

	UFUNCTION(BlueprintPure, Category = "DualContour|Mining")
	float GetDigProgress() const
	{
		return bBombHeld ? BombChargeProgress : (bDigHeld ? DigProgress : 0.0f);
	}

	UFUNCTION(BlueprintCallable, Category = "DualContour|SaveLoad")
	void SaveToSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "DualContour|SaveLoad")
	void LoadFromSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "DualContour|SaveLoad")
	void ClearSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "DualContour|SaveLoad")
	void CloseSaveLoadWidget();

	UFUNCTION(BlueprintPure, Category = "DualContour|SaveLoad")
	bool IsSaveLoadWidgetOpen() const { return SaveLoadWidget != nullptr; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;

protected:
	virtual void SetupInputComponent() override;

private:
	void ActivatePossessedPistolPose();
	void SetPawnGravityEnabled(bool bEnabled);
	void ShowInitializationProgress();
	void HandleProgressFinished();
	void OnDigPressed();
	void OnDigReleased();
	void OnBombFirePressed();
	void OnBombFireReleased();
	void PerformDig();
	void InitializeSamplers();
	void EnsureReticle();
	void ToggleSaveLoadWidget();
	void OpenSaveLoadWidget();
	void SetSaveLoadInputMode(bool bMenuOpen);
	FString GetSaveSlotName(int32 SlotIndex) const;
	FString GetTerrainSlotName(const FString& SaveSlotName, int32 ActorIndex) const;
	void GetSortedMeshActors(TArray<class ADualContourMeshActor*>& OutActors) const;

	UPROPERTY(Transient)
	TObjectPtr<UDualContourInitProgressWidget> ProgressWidget;

	UPROPERTY(Transient)
	TObjectPtr<UDualContourMiningReticleWidget> ReticleWidget;

	UPROPERTY(Transient)
	TObjectPtr<UDualContourSaveLoadWidget> SaveLoadWidget;

	bool bDigHeld = false;
	float DigProgress = 0.0f;
	bool bBombHeld = false;
	float BombChargeProgress = 0.0f;
	float PawnGravityScaleBeforeInitialization = 1.0f;
	bool bPawnGravitySuspended = false;
};
