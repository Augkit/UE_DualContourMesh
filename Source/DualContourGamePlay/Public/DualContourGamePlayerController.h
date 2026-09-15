#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourGamePlayerController.generated.h"

class UDualContourInitProgressWidget;

/**
 * Minimal PlayerController that shows the fullscreen DualContour initialization
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

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ShowInitializationProgress();
	void HandleProgressFinished();

	UPROPERTY(Transient)
	TObjectPtr<UDualContourInitProgressWidget> ProgressWidget;
};
