#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "DualContourTypes.h"
#include "DualContourMeshComponent.h"
#include "DualContourMeshActor.generated.h"

UCLASS()
class DUALCONTOURMESH_API ADualContourMeshActor : public AActor
{
	GENERATED_BODY()
public:
	ADualContourMeshActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	TArray<TObjectPtr<UDualContourMeshComponent>> MeshComponents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt CellCount = FVectorInt(16, 16, 16);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float CellSize = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt Divisions = FVectorInt(1, 1, 1);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	TArray<uint8> SamplePointGrid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid")
	TArray<FDualContourCell> DualContourGrid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	FVector SphereCenter = FVector(80.f, 80.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	float SphereRadius = 60.f;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	void FillSphereDensity();
	void BuildCells();
	uint8 GetSample(int32 X, int32 Y, int32 Z) const;
	float TrilinearSample(FVector GridPos) const;
	FVector ComputeGradient(FVector GridPos) const;

	FVector GetSampleWorldPos(int32 SX, int32 SY, int32 SZ) const { return FVector((float)SX, (float)SY, (float)SZ) * CellSize; }

private:
	FVectorInt GetSampleDims() const { return FVectorInt(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }

	int32 SampleIndex(int32 X, int32 Y, int32 Z) const;
	int32 CellIndex(int32 X, int32 Y, int32 Z) const;
};
