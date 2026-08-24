#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "DualContourTypes.h"
#include "DualContourDebugComponent.generated.h"

/** Editor-only primitive component that renders dual-contour cell debug boxes via its SceneProxy. */
UCLASS(ClassGroup = "DualContour", NotBlueprintable, NotBlueprintType)
class DUALCONTOURMESH_API UDualContourDebugComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	virtual bool IsEditorOnly() const override { return true; }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

#if WITH_EDITOR
	/** Rebuild the cell snapshot from the actor's grid data. Call MarkRenderStateDirty() afterwards. */
	void UpdateFromGrid(const TArray<FDualContourCell>& Grid, FVectorInt InCellCount, float CellSize);

	struct FCellEntry
	{
		FIntVector GridCoordinate;
		FBox LocalBox;
		bool bActive;
	};

	TArray<FCellEntry> CellEntries;
	FBoxSphereBounds CachedLocalBounds;
#endif
};
