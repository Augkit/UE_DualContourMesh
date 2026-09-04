#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"
#include "GameFramework/SaveGame.h"
#include "DualContourRuntimeSaveGame.generated.h"

/** Modified density and material chunks that are overlaid on an InitialDualContour. */
UCLASS()
class DUALCONTOURMESH_API UDualContourRuntimeSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 8;

	UPROPERTY(SaveGame)
	FSoftObjectPath BaseDualContourPath;

	UPROPERTY(SaveGame)
	FIntVector BaseCellCount = FIntVector::ZeroValue;

	UPROPERTY(SaveGame)
	TMap<FIntVector, FDensityChunk> DensityChunks;

	UPROPERTY(SaveGame)
	TMap<FIntVector, FMaterialIdChunk> MaterialChunks;
};
