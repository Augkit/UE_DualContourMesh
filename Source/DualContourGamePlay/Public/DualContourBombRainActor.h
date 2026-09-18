#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DualContourBombRainActor.generated.h"

class ADualContourBombActor;
class UBoxComponent;
class USceneComponent;

/** Drops a configured number of physics bombs from random points in a local XY area. */
UCLASS(Blueprintable)
class DUALCONTOURGAMEPLAY_API ADualContourBombRainActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourBombRainActor();

	/** Starts a new sequence, resetting the number spawned by any sequence already in progress. */
	UFUNCTION(BlueprintCallable, Category = "Bomb Rain")
	void StartBombRain();

	UFUNCTION(BlueprintCallable, Category = "Bomb Rain")
	void StopBombRain();

	UFUNCTION(BlueprintPure, Category = "Bomb Rain")
	bool IsBombRainActive() const;

	UFUNCTION(BlueprintPure, Category = "Bomb Rain")
	int32 GetSpawnedBombCount() const { return SpawnedBombCount; }

	/** Full local-space width and depth of the spawn area, centered on this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Area",
		meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D AreaSize = FVector2D(1000.0f, 1000.0f);

	/** Total time over which BombCount bombs are spawned. The final bomb is spawned at this time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Timing",
		meta = (ClampMin = "0.0", Units = "s"))
	float SpawnDuration = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Timing", meta = (ClampMin = "0"))
	int32 BombCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Timing")
	bool bStartAutomatically = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Bomb")
	TSubclassOf<ADualContourBombActor> BombClass;

	/** Optional initial speed along this actor's local down axis. Gravity acts after spawning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb Rain|Bomb",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float InitialDownwardSpeed = 0.0f;

	/** Called once after the requested number of bombs has been spawned. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bomb Rain", meta = (DisplayName = "On Bomb Rain Finished"))
	void BP_OnBombRainFinished();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bomb Rain|Components")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	/** Editor-only wireframe showing the random local XY spawn area. */
	UPROPERTY(VisibleAnywhere, Category = "Bomb Rain|Area")
	TObjectPtr<UBoxComponent> AreaVisualization;
#endif

private:
	void SpawnNextBomb();
	void SpawnBomb();
	void UpdateAreaVisualization();

	FTimerHandle SpawnTimerHandle;
	int32 SpawnedBombCount = 0;
};
