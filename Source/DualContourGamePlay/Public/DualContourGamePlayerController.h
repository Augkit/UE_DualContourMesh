#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourGamePlayerController.generated.h"

class UInputMappingContext;
class UDualContourInitProgressWidget;
class UDualContourMiningReticleWidget;
class UDualContourModifierComponent;
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
	float GetDigProgress() const { return bDigHeld ? DigProgress : 0.0f; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;

protected:
	virtual void SetupInputComponent() override;

private:
	void ActivatePossessedPistolPose();
	void ShowInitializationProgress();
	void HandleProgressFinished();
	void OnDigPressed();
	void OnDigReleased();
	void PerformDig();
	void InitializeSamplers();
	void EnsureReticle();

	UPROPERTY(Transient)
	TObjectPtr<UDualContourInitProgressWidget> ProgressWidget;

	UPROPERTY(Transient)
	TObjectPtr<UDualContourMiningReticleWidget> ReticleWidget;

	bool bDigHeld = false;
	float DigProgress = 0.0f;
};
