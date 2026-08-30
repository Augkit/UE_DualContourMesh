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

	/** Takes ownership of CPU mesh data and refreshes bounds, collision, and rendering on the game thread. */
	void ApplyMeshData(FDualContourMeshData&& InMeshData, bool bUpdateCollision = true);
	/** Rebuilds collision from the current CPU mesh data, normally once at the end of an edit stroke. */
	void RefreshCollision();
	const FDualContourMeshData& GetMeshData() const { return MeshData; }

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
	void CreateMeshBodySetup();
	void UpdateCollision();
	FDualContourMeshData MeshData;

	UPROPERTY(Instanced, Transient)
	TObjectPtr<UBodySetup> MeshBodySetup;
};
