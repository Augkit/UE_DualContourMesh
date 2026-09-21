#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourCleanViewController.generated.h"

/** Controller for a presentation scene with no pawn, HUD, weapon, or input UI. */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourCleanViewController : public APlayerController
{
	GENERATED_BODY()

public:
	ADualContourCleanViewController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TryStartSceneActors();
	bool bSceneActorsStarted = false;
};
