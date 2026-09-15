#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DualContourGameGameModeBase.generated.h"

/**
 * Minimal GameMode whose only job is making ADualContourGamePlayerController the default
 * player controller, so the fullscreen initialization overlay appears when the game starts.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourGameGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADualContourGameGameModeBase();
};
