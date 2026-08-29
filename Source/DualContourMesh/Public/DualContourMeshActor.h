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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDualContourMeshComponentsUpdated);

UCLASS()
class DUALCONTOURMESH_API ADualContourMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourMeshActor();

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDualContourMeshComponent>> MeshComponents;

	/** Data model and generator managed by this actor. */
	UPROPERTY(BlueprintReadOnly, Instanced)
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
	FVectorInt Divisions = FVectorInt(1, 1, 1);

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
	UPROPERTY(BlueprintAssignable, Category = "DualContour|Rendering")
	FOnDualContourMeshComponentsUpdated OnMeshComponentsUpdated;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	/** Sets generated contour data, then queues its mesh chunks for application over subsequent frames. */
	bool SetGeneratedDualContour(UDualContour* InDualContour);

	/** Applies any volume sampler at a surface point, rotating its local +Z axis to the hit normal. */
	bool ModifyDensityWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal, UVolumeSampler* Sampler, float UniformScale,
		bool bExcavate);

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
	};

	FDelegateHandle DualContourCellsRebuiltHandle;
	TArray<FPendingMeshApply> PendingMeshApplies;
	TMap<int32, uint64> DivisionUpdateSerials;
	int32 NextPendingMeshApplyIndex = 0;
	uint64 MeshQueueRevision = 0;
	uint64 NextMeshUpdateSerial = 0;
	bool bMeshUpdateCompletionPending = false;
	bool bRebuildingMesh = false;
	FVectorInt MeshCellCount;
	float MeshCellSize = 0.f;
	void BindToDualContour();
	void UnbindFromDualContour();
	void OnDualContourCellsRebuilt(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax);

	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void RefreshCollisionSettings();
	void UpdateAutoDivisions();
	void RecreateMeshComponents();
	UDualContourMeshComponent* CreateMeshComponent();
	void QueueMeshData(int32 DivisionIndex, FDualContourMeshData&& MeshData);
	void SortQueuedMeshDataByViewDistance();
	void CancelQueuedMeshData(int32 DivisionIndex);
	void ResetQueuedMeshData();
	void ApplyQueuedMeshData();
	void NotifyMeshComponentsUpdatedIfReady();
	bool HasValidDivisions() const;
	int32 DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const;
	FVectorInt DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const;
	void PartialUpdateComponents(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax);

#if WITH_EDITOR
	void RefreshDebugComponent();
	bool bRebuildInitialDualContourAfterLoad = false;
#endif

};
