#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "DualContourTypes.h"
#include "DualContour.h"
#include "DualContourMeshComponent.h"
#include "DualContourDebugComponent.h"
#include "DualContourMeshActor.generated.h"

class UVolumeSampler;

DECLARE_MULTICAST_DELEGATE(FOnDualContourMeshComponentsUpdated);

UCLASS()
class DUALCONTOURMESH_API ADualContourMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourMeshActor();

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDualContourMeshComponent>> MeshComponents;

	/** Runtime contour state copied from InitialDualContour. Its settings are read-only on this actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "DualContour")
	TObjectPtr<UDualContour> DualContour;

	/** Optional persistent DualContour asset copied into this actor when its mesh is rebuilt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour")
	TObjectPtr<UDualContour> InitialDualContour;

	/** Automatically derives Divisions from CellCount so no generated component exceeds MaxCellsPerDivision on an axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering")
	bool bAutoCalculateDivisions = true;

	/** Maximum number of cells along any axis of an automatically generated mesh component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering",
		meta = (ClampMin = "1", EditCondition = "bAutoCalculateDivisions"))
	int32 MaxCellsPerDivision = 64;

	/** Number of independently generated mesh components along each axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering",
		meta = (ClampMin = "1", EditCondition = "!bAutoCalculateDivisions"))
	FIntVector Divisions = FIntVector(1, 1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	/** Maximum number of completed mesh chunks applied to components during one frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering", meta = (ClampMin = "1"))
	int32 MeshComponentsPerFrame = 4;

	/** Collision profile or custom channel settings applied to every generated mesh chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Collision")
	FBodyInstance CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Collision")
	bool bGenerateOverlapEvents = true;

	/** Broadcast after all queued mesh component creations, updates, and removals have completed. */
	FOnDualContourMeshComponentsUpdated OnMeshComponentsUpdated;

	/** Slot used by the test save/load buttons below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Runtime Save")
	FString RuntimeSaveSlotName = TEXT("DualContourRuntime");

	/** Platform user index used by the test save/load buttons below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Runtime Save", meta = (ClampMin = "0"))
	int32 RuntimeSaveUserIndex = 0;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	/** Saves the modified density and material chunk overlays accumulated since InitialDualContour was copied. */
	UFUNCTION(BlueprintCallable, Category = "DualContour|Runtime Save")
	bool SaveRuntimeDensityIncrement(const FString& SlotName, int32 UserIndex = 0) const;

	/** Rebuilds from InitialDualContour and applies previously saved density/material chunk overlays. */
	UFUNCTION(BlueprintCallable, Category = "DualContour|Runtime Save")
	bool LoadRuntimeDensityIncrement(const FString& SlotName, int32 UserIndex = 0);

	UFUNCTION(CallInEditor, Category = "DualContour|Runtime Save", meta = (DisplayName = "Test Save Density Increment"))
	void TestSaveRuntimeDensityIncrement();

	UFUNCTION(CallInEditor, Category = "DualContour|Runtime Save", meta = (DisplayName = "Test Load Density Increment"))
	void TestLoadRuntimeDensityIncrement();

#if WITH_EDITOR
	/** Rebuilds the editor-only mesh-chunk visualization when DualContour.Debug.DrawMeshComponents is enabled. */
	UFUNCTION(CallInEditor, Category = "DualContour|Debug")
	void RefreshDebugVisualization();
#endif

	/** Sets generated contour data, then queues its mesh chunks for application over subsequent frames. */
	bool SetGeneratedDualContour(UDualContour* InDualContour);

	/**
	 * Applies pending mesh data on the game thread without requiring the preview world's Tick.
	 * Editor preview owners can call this from an editor ticker when their viewport is inactive.
	 */
	void ProcessPendingMeshUpdates();

	/** Applies any volume sampler at a surface point, rotating its local +Z axis to the hit normal. */
	bool ModifyDensityWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal, UVolumeSampler* Sampler, float UniformScale,
		bool bExcavate);
	/** Defers expensive collision cooking while an interactive density stroke is producing preview meshes. */
	void SetDensityEditInProgress(bool bInProgress);

	/** Returns whether every Divisions value is valid for its corresponding CellCount value and describes the result. */
	bool ValidateDivisions(FString& OutStatus) const;

	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, Category = "DualContour")
	TObjectPtr<UDualContourDebugComponent> DebugComponent;
#endif

private:
	struct FPendingMeshApply
	{
		int32 DivisionIndex = INDEX_NONE;
		uint64 QueueRevision = 0;
		uint64 UpdateSerial = 0;
		double ViewDistanceSquared = MAX_dbl;
		FDualContourMeshData MeshData;
		bool bUpdateCollision = true;
	};

	FDelegateHandle DualContourCellsRebuiltHandle;
	FDelegateHandle DualContourMaterialsChangedHandle;
	TArray<FPendingMeshApply> PendingMeshApplies;
	TMap<int32, uint64> DivisionUpdateSerials;
	TSet<int32> DensityEditDirtyDivisions;
	int32 NextPendingMeshApplyIndex = 0;
	uint64 MeshQueueRevision = 0;
	uint64 NextMeshUpdateSerial = 0;
	bool bMeshUpdateCompletionPending = false;
	bool bRebuildingMesh = false;
	bool bDensityEditInProgress = false;
	FIntVector MeshCellCount = FIntVector(0, 0, 0);
	float MeshCellSize = 0.f;
	void BindToDualContour();
	void UnbindFromDualContour();
	void OnDualContourCellsRebuilt(FIntVector AffectedCellMin, FIntVector AffectedCellMax);
	void OnDualContourMaterialsChanged(FIntVector AffectedCellMin, FIntVector AffectedCellMax);
	void UpdateMeshDivisions(const TSet<int32>& AffectedDivisions, bool bUpdateCollision = true);

	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void RefreshCollisionSettings();
	void RefreshMeshMaterial();
	void UpdateAutoDivisions();
	void RecreateMeshComponents();
	UDualContourMeshComponent* CreateMeshComponent();
	void QueueMeshData(int32 DivisionIndex, FDualContourMeshData&& MeshData, bool bUpdateCollision = true);
	void SortQueuedMeshDataByViewDistance();
	void CancelQueuedMeshData(int32 DivisionIndex);
	void ResetQueuedMeshData();
	void ApplyQueuedMeshData();
	void NotifyMeshComponentsUpdatedIfReady();
	void UpdateActorTickEnabled();
	bool HasValidDivisions() const;
	int32 DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const;
	FIntVector DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const;
	FIntVector DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const;
	FIntVector DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const;
	void PartialUpdateComponents(FIntVector AffectedCellMin, FIntVector AffectedCellMax, bool bUpdateCollision = true);

#if WITH_EDITOR
	void RefreshDebugComponent();
	void RequestDebugComponentRefresh(bool bImmediate);
	void ProcessPendingDebugComponentRefresh();
	bool bRestoreMeshAfterLoad = false;
	bool bDebugRefreshPending = false;
	bool bDebugRefreshImmediatelyAfterMeshUpdate = false;
	double DebugRefreshDeadline = 0.0;
#endif

};
