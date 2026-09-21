#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DualContourCleanViewGameMode.generated.h"

/** GameMode for clean DualContour presentation scenes. It intentionally spawns no pawn. */
UCLASS()
class DUALCONTOURGAMEPLAY_API ADualContourCleanViewGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADualContourCleanViewGameMode();
};
