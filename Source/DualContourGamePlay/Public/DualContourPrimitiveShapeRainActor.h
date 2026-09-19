#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DualContourPrimitiveShapeRainActor.generated.h"

class AStaticMeshActor;
class UBoxComponent;
class USceneComponent;
class UStaticMesh;

/** Spawns random physics-enabled Engine BasicShapes over a local XY area for collision demonstrations. */
UCLASS(Blueprintable)
class DUALCONTOURGAMEPLAY_API ADualContourPrimitiveShapeRainActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourPrimitiveShapeRainActor();

	/** Starts a new sequence and removes shapes spawned by the previous sequence. */
	UFUNCTION(BlueprintCallable, Category = "Primitive Shape Rain")
	void StartShapeRain();

	UFUNCTION(BlueprintCallable, Category = "Primitive Shape Rain")
	void StopShapeRain();

	UFUNCTION(BlueprintCallable, Category = "Primitive Shape Rain")
	void ClearSpawnedShapes();

	UFUNCTION(BlueprintPure, Category = "Primitive Shape Rain")
	bool IsShapeRainActive() const;

	UFUNCTION(BlueprintPure, Category = "Primitive Shape Rain")
	int32 GetSpawnedShapeCount() const { return SpawnedShapeCount; }

	/** Full local-space width and depth of the spawn area, centered on this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Area",
		meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D AreaSize = FVector2D(1000.0f, 1000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Timing",
		meta = (ClampMin = "0.0", Units = "s"))
	float SpawnDuration = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Timing",
		meta = (ClampMin = "0"))
	int32 ShapeCount = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Timing")
	bool bStartAutomatically = true;

	/** Optional initial speed along this actor's local down axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Physics",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float InitialDownwardSpeed = 150.0f;

	/** Random uniform scale range applied to each basic shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Visual",
		meta = (ClampMin = "0.01", Units = "x"))
	FVector2D ShapeScaleRange = FVector2D(0.6f, 1.4f);

	/** 0 keeps spawned shapes until manually cleared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Physics",
		meta = (ClampMin = "0.0", Units = "s"))
	float ShapeLifeSpan = 0.0f;

	/** Engine BasicShapes to choose from. Defaults to Cube, Sphere, Cylinder and Cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive Shape Rain|Shapes")
	TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;

	UFUNCTION(BlueprintImplementableEvent, Category = "Primitive Shape Rain",
		meta = (DisplayName = "On Shape Rain Finished"))
	void BP_OnShapeRainFinished();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Primitive Shape Rain|Components")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Primitive Shape Rain|Area")
	TObjectPtr<UBoxComponent> AreaVisualization;
#endif

private:
	void SpawnNextShape();
	void SpawnShape();
	void UpdateAreaVisualization();
	void LoadDefaultShapeMeshes();

	FTimerHandle SpawnTimerHandle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AStaticMeshActor>> SpawnedShapes;
	int32 SpawnedShapeCount = 0;
};
