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

	/** Number of independently generated mesh components along each axis. Each value must divide the corresponding CellCount exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering", meta = (ClampMin = "1"))
	FVectorInt Divisions = FVectorInt(1, 1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Rendering")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	/** Collision profile or custom channel settings applied to every generated mesh chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour|Collision")
	FBodyInstance CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Collision")
	bool bGenerateOverlapEvents = true;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	/** Sets generated contour data and immediately recreates the mesh components from it. */
	bool SetGeneratedDualContour(UDualContour* InDualContour);

	/** Applies any volume sampler at a surface point, rotating its local +Z axis to the hit normal. */
	void ModifyDensityWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal, UVolumeSampler* Sampler, float UniformScale,
		bool bExcavate);

	/** Returns whether every Divisions value divides its corresponding CellCount value and describes the result. */
	bool ValidateDivisions(FString& OutStatus) const;

	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, Category = "DualContour")
	TObjectPtr<UDualContourDebugComponent> DebugComponent;
#endif

private:
	FDelegateHandle DualContourCellsRebuiltHandle;
	bool bRebuildingMesh = false;
	FVectorInt MeshCellCount;
	float MeshCellSize = 0.f;
	void BindToDualContour();
	void UnbindFromDualContour();
	void OnDualContourCellsRebuilt(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax);

	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void RefreshCollisionSettings();
	void RecreateMeshComponents();
	UDualContourMeshComponent* CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax);
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
