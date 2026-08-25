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

class USVTDensityField;

UCLASS()
class DUALCONTOURMESH_API ADualContourMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourMeshActor();

	UPROPERTY(VisibleAnywhere, Category = "DualContour")
	TMap<int32, TObjectPtr<UDualContourMeshComponent>> MeshComponents;

	/** Data model and generator managed by this actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "DualContour")
	TObjectPtr<UDualContour> DualContour;

	/** Number of independently generated mesh components along each axis. Each value must divide the corresponding CellCount exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "1"))
	FVectorInt Divisions = FVectorInt(1, 1, 1);

	/** Optional asset copied into DualContour whenever the mesh is rebuilt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DualContour")
	TObjectPtr<USVTDensityField> InitialDensityField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	/** Collision profile or custom channel settings applied to every generated mesh chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ShowOnlyInnerProperties))
	FBodyInstance CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bGenerateOverlapEvents = true;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	/** Sets generated contour data and immediately recreates the mesh components from it. */
	bool SetGeneratedDualContour(UDualContour* InDualContour);

	UFUNCTION(BlueprintCallable, Category = "Collision")
	void RefreshCollisionSettings();

	void ModifyDensityWithHemisphere(const FVector& WorldHitPos, const FVector& WorldHitNormal, float Radius, bool bExcavate);
	/** Returns whether every Divisions value divides its corresponding CellCount value and describes the result. */
	bool ValidateDivisions(FString& OutStatus) const;

	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, Category = "DualContour")
	TObjectPtr<UDualContourDebugComponent> DebugComponent;
#endif

private:
	void RecreateMeshComponents();
	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void PartialUpdateComponents(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax);
	UDualContourMeshComponent* CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax);
	bool HasValidDivisions() const;
	int32 DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const;
	FVectorInt DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const;
#if WITH_EDITOR
	void RefreshDebugComponent();
#endif

};
