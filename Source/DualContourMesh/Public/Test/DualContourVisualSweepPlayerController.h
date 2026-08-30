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
	/** Builds the procedural sampler regression set and captures deterministic axis/isometric views. */
	UFUNCTION(Exec)
	void RunDualContourVisualSweep();

private:
	struct FVisualSweepView
	{
		FString Name;
		FVector Direction = FVector::ForwardVector;
	};

	void CaptureNextVisualSweepView();
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
	/** Existing level actors made invisible so screenshots have an uncluttered black background. */
	TArray<TWeakObjectPtr<AActor>> VisualSweepActorsHiddenByTest;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	FDelegateHandle ScreenshotProcessedHandle;
	FTimerHandle VisualSweepTimerHandle;
	FString VisualSweepOutputDirectory;
	FVector VisualSweepCenter = FVector::ZeroVector;
	int32 VisualSweepSubjectIndex = 0;
	int32 VisualSweepViewIndex = 0;
	bool bVisualSweepRunning = false;
};
