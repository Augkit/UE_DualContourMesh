#pragma once
#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "DualContourTypes.h"
#include "DualContourMeshComponent.generated.h"

UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class DUALCONTOURMESH_API UDualContourMeshComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	// Cell range [Min, Max) this component owns; Max+1 ring is borrowed for quad building
	FVectorInt CellRangeMin;
	FVectorInt CellRangeMax;

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2f> UVs;
	TArray<uint32> Indices;
	FBox LocalBounds = FBox(ForceInit);

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	void BuildAndRefreshMesh();

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual int32 GetNumMaterials() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	void BuildMesh();
	void GenerateQuadsForCell(int32 CX, int32 CY, int32 CZ);
};
