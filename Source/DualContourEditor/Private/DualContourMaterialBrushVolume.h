#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DualContourMaterialBrushVolume.generated.h"

class ADualContourMeshActor;
class UBoxComponent;
class USceneComponent;
class USphereComponent;
class USplineComponent;

UENUM(BlueprintType)
enum class EDualContourMaterialBrushVolumeShape : uint8
{
	Box UMETA(DisplayName = "Box"),
	Sphere UMETA(DisplayName = "Sphere"),
	SplinePrism UMETA(DisplayName = "Spline + Height"),
};

/**
 * Editor placement volume used by the Dual Contour material brush.
 *
 * Box and sphere regions use the actor's normal transform gizmo. SplinePrism
 * uses the engine spline visualizer for its footprint and extrudes it along
 * the component's local Z axis by SplineHeight.
 */
UCLASS(BlueprintType, NotPlaceable)
class ADualContourMaterialBrushVolume final : public AActor
{
	GENERATED_BODY()

public:
	ADualContourMaterialBrushVolume();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Brush")
	EDualContourMaterialBrushVolumeShape Shape = EDualContourMaterialBrushVolumeShape::Box;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(200.0, 200.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::Sphere", EditConditionHides))
	float SphereRadius = 200.0f;

	/** Extrusion height centered around the spline's local XY plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::SplinePrism", EditConditionHides))
	float SplineHeight = 400.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Material Brush")
	TObjectPtr<ADualContourMeshActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material Brush", meta = (EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::SplinePrism", EditConditionHides))
	TObjectPtr<USplineComponent> Spline;

	void Initialize(ADualContourMeshActor* InTargetActor, EDualContourMaterialBrushVolumeShape InShape, const FVector& InHalfSize);
	/** Captures spline geometry once before a potentially large sample query. */
	void CacheBrushGeometry() const;
	FBox GetBrushWorldBounds() const;
	/** The same local XY footprint used by material application and editor bounds. */
	void GetSplinePolygon(TArray<FVector2D>& OutPolygon) const;
	bool EncompassesWorldPosition(const FVector& WorldPosition) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool IsEditorOnly() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category = "Material Brush")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Material Brush", meta = (EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::Box", EditConditionHides))
	TObjectPtr<UBoxComponent> BoxPreview;

	UPROPERTY(VisibleAnywhere, Category = "Material Brush", meta = (EditCondition = "Shape == EDualContourMaterialBrushVolumeShape::Sphere", EditConditionHides))
	TObjectPtr<USphereComponent> SpherePreview;

	void UpdatePreviewComponents();
	static bool IsInsidePolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon);

	mutable TArray<FVector2D> CachedSplinePolygon;
};
