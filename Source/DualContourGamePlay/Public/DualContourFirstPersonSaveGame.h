#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DualContourFirstPersonSaveGame.generated.h"

/** Player state stored by the first-person save/load menu. */
UCLASS()
class DUALCONTOURGAMEPLAY_API UDualContourFirstPersonSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 1;

	UPROPERTY(SaveGame)
	FString MapName;

	UPROPERTY(SaveGame)
	bool bHasPlayerTransform = false;

	UPROPERTY(SaveGame)
	FTransform PlayerTransform = FTransform::Identity;

	UPROPERTY(SaveGame)
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(SaveGame)
	int32 SelectedSamplerIndex = 0;

	UPROPERTY(SaveGame)
	int32 TerrainActorCount = 0;
};
