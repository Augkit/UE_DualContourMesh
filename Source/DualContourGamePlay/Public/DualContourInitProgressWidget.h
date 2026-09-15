#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DualContourInitProgressWidget.generated.h"

class ADualContourMeshActor;
class UImage;
class UProgressBar;
class UTextBlock;

/** Broadcast after every tracked mesh actor finished initializing and the overlay removed itself. */
DECLARE_MULTICAST_DELEGATE(FOnDualContourInitProgressFinished);

/**
 * Fullscreen overlay tracking ADualContourMeshActor initialization at game start.
 *
 * Progress mirrors the DualContour editor toolkit's generation progress: an animated
 * fake value with diminishing returns until the contour cells are ready (capped at 0.6),
 * then while the actor applies its queued mesh components (capped at 0.95), and a snap
 * to 100% when OnMeshComponentsUpdated reports the actor is done.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API UDualContourInitProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDualContourInitProgressWidget(const FObjectInitializer& ObjectInitializer);

	/** Seconds the overlay stays visible even when every actor initialized immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Progress", meta = (ClampMin = "0.0"))
	float MinimumDisplayTime = 0.5f;

	/** Seconds the completed 100% state is held before the overlay removes itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Progress", meta = (ClampMin = "0.0"))
	float CompleteHoldTime = 0.25f;

	/** Opacity of the fullscreen backdrop drawn behind the progress panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Progress",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackdropOpacity = 0.92f;

	/** Notified once after the overlay finished; the widget has already removed itself from the viewport. */
	FOnDualContourInitProgressFinished OnFinished;

	/** Starts tracking mesh initialization of the supplied actors. Call before adding the widget to the viewport. */
	void TrackActors(const TArray<TObjectPtr<ADualContourMeshActor>>& InActors);

	/** Overall progress across every tracked actor in [0, 1]. */
	UFUNCTION(BlueprintPure, Category = "DualContour|Progress")
	float GetOverallProgress() const;

	/** Number of tracked actors whose mesh components finished initializing. */
	UFUNCTION(BlueprintPure, Category = "DualContour|Progress")
	int32 GetCompletedActorCount() const;

	/** Phase description shown below the progress bar. */
	UFUNCTION(BlueprintPure, Category = "DualContour|Progress")
	FText GetStatusText() const;

	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

private:
	struct FTrackedActor
	{
		TWeakObjectPtr<ADualContourMeshActor> Actor;
		FDelegateHandle CellsRebuiltHandle;
		FDelegateHandle MeshComponentsUpdatedHandle;
		float Progress = 0.0f;
		float ProgressTarget = 0.0f;
		bool bCellsReady = false;
		bool bComplete = false;
	};

	void BindActor(FTrackedActor& Tracked, int32 TrackedIndex);
	void UnbindActor(FTrackedActor& Tracked);
	void HandleCellsRebuilt(int32 TrackedIndex);
	void HandleMeshComponentsUpdated(int32 TrackedIndex);
	/** True when the actor can no longer queue mesh updates, so there is nothing left to observe. */
	bool IsActorInitializationComplete(const ADualContourMeshActor& Actor) const;
	bool AreAllActorsComplete() const;

	void BuildWidgetTree();
	void UpdateProgressDisplay();
	void RequestRemoval();

	UPROPERTY(Transient)
	TObjectPtr<UImage> BackdropImage;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> MainProgressBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleTextBlock;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusTextBlock;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PercentTextBlock;

	TArray<FTrackedActor> TrackedActors;
	float ElapsedDisplayTime = 0.0f;
	float ElapsedCompleteTime = 0.0f;
	bool bRemovalRequested = false;
};
