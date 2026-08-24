#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "DualContourTypes.h"
#include "DualContourMeshComponent.h"
#include "DualContourDebugComponent.h"
#include "DualContourMeshActor.generated.h"

UCLASS()
class DUALCONTOURMESH_API ADualContourMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourMeshActor();

	UPROPERTY(VisibleAnywhere, Category = "DualContour")
	TMap<int32, TObjectPtr<UDualContourMeshComponent>> MeshComponents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	/** Collision profile or custom channel settings applied to every generated mesh chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ShowOnlyInnerProperties))
	FBodyInstance CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bGenerateOverlapEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt CellCount = FVectorInt(16, 16, 16);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float CellSize = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt Divisions = FVectorInt(1, 1, 1);

	UPROPERTY(BlueprintReadOnly, Category = "Grid", meta = (HideInDetailsPanel))
	TArray<uint8> SamplePointGrid;

	UPROPERTY(BlueprintReadOnly, Category = "Grid", meta = (HideInDetailsPanel))
	TArray<FDualContourCell> DualContourGrid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	FVector SphereCenter = FVector(80.f, 80.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	float SphereRadius = 60.f;

	/** True when mesh-generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bMeshRebuildRequired = true;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	UFUNCTION(BlueprintCallable, Category = "Collision")
	void RefreshCollisionSettings();

	void ModifyDensityWithHemisphere(const FVector& WorldHitPos, const FVector& WorldHitNormal, float Radius, bool bExcavate);

	void FillSphereDensity();
	void BuildCells();
	uint8 GetSample(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	float TrilinearSample(FVector GridPos) const;
	FVector ComputeGradient(FVector GridPos) const;
	bool HasCurrentGeneratedData() const;

	FVector GetSampleWorldPos(int32 SampleX, int32 SampleY, int32 SampleZ) const
	{
		return FVector((float)SampleX, (float)SampleY, (float)SampleZ) * CellSize;
	}

	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	TObjectPtr<UDualContourDebugComponent> DebugComponent;
#endif

private:
	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax);
	void PartialUpdateComponents(const TSet<int32>& AffectedDivisions);
	UDualContourMeshComponent* CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax);
#if WITH_EDITOR
	void RefreshDebugComponent();
#endif

	int32 DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const;
	FVectorInt DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const;

	FVectorInt GetSampleDims() const { return FVectorInt(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }

	int32 SampleIndex(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	int32 CellIndex(int32 CellX, int32 CellY, int32 CellZ) const;
	bool ValidateMeshGenerationSettings() const;
};
