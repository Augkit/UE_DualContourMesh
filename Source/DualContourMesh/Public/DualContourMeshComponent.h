#pragma once
#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "DualContourTypes.h"
#include "DualContourMeshComponent.generated.h"

class UBodySetup;

UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class DUALCONTOURMESH_API UDualContourMeshComponent : public UMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()
public:
	UDualContourMeshComponent(const FObjectInitializer& ObjectInitializer);

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
	virtual UBodySetup* GetBodySetup() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;

	// IInterface_CollisionDataProvider
	virtual bool GetTriMeshSizeEstimates(FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const override;
	virtual bool GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool bInUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool bInUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override { return false; }

private:
	void BuildMesh();
	void GenerateQuadsForCell(int32 CX, int32 CY, int32 CZ);
	void CreateMeshBodySetup();
	void UpdateCollision();

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UBodySetup> MeshBodySetup;
};
