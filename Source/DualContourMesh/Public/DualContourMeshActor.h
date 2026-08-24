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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<UMaterialInterface> MeshMaterial = nullptr;

	/** Collision profile or custom channel settings applied to every generated mesh chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ShowOnlyInnerProperties))
	FBodyInstance CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bGenerateOverlapEvents = true;

	UFUNCTION(CallInEditor, Category = "DualContour")
	void RebuildMesh();

	UFUNCTION(BlueprintCallable, Category = "Collision")
	void RefreshCollisionSettings();

	void ModifyDensityWithHemisphere(const FVector& WorldHitPos, const FVector& WorldHitNormal, float Radius, bool bExcavate);

	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Transient, Category = "DualContour")
	TObjectPtr<UDualContourDebugComponent> DebugComponent;
#endif

private:
	void ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const;
	void PartialUpdateComponents(const TSet<int32>& AffectedDivisions);
	UDualContourMeshComponent* CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax);
#if WITH_EDITOR
	void RefreshDebugComponent();
#endif

};
