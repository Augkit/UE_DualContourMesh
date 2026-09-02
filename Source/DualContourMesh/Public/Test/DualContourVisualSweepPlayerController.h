#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourVisualSweepPlayerController.generated.h"

class ACameraActor;
class ADirectionalLight;
class ASkyLight;
class ADualContourMeshActor;

UCLASS()
class DUALCONTOURMESH_API ADualContourVisualSweepPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Builds the procedural sampler regression set and captures deterministic axis/isometric views, subset-selectable via dc.VisualTest.Views. */
	UFUNCTION(Exec)
	void RunDualContourVisualSweep();

private:
	struct FVisualSweepView
	{
		FString Name;
		FVector Direction = FVector::ForwardVector;
	};

	void CaptureNextVisualSweepView();
	void CapturePreparedVisualSweepView();
	void HandleVisualSweepScreenshotProcessed();
	void WriteVisualSweepMetrics() const;
	void FinishVisualSweep(bool bSucceeded);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADualContourMeshActor>> VisualSweepSubjects;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> VisualSweepCamera;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> VisualSweepDirectionalLight;

	/** Cubemap-lit ambient fill; it contributes light without drawing a sky background. */
	UPROPERTY(Transient)
	TObjectPtr<ASkyLight> VisualSweepSkyLight;

	TArray<FString> VisualSweepSubjectNames;
	TArray<FVisualSweepView> VisualSweepViews;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TWeakObjectPtr<AActor> HiddenVisualSweepPawn;
	FDelegateHandle ScreenshotProcessedHandle;
	FTimerHandle VisualSweepTimerHandle;
	FString VisualSweepOutputDirectory;
	FVector VisualSweepCenter = FVector::ZeroVector;
	int32 VisualSweepSubjectIndex = 0;
	int32 VisualSweepViewIndex = 0;
	bool bVisualSweepPawnWasHidden = false;
	bool bVisualSweepRunning = false;
};
