#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "DualContourTypes.h"
#include "DualContourMeshComponent.h"
#include "DualContourDebugComponent.generated.h"

/** Editor-only primitive component that renders a color-coded snapshot of generated mesh chunks. */
UCLASS(ClassGroup = "DualContour", NotBlueprintable, NotBlueprintType, HideCategories = (Collision, Physics))
class DUALCONTOURMESH_API UDualContourDebugComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	virtual bool IsEditorOnly() const override { return true; }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

#if WITH_EDITOR
	/** Returns true when the editor mesh-chunk visualization is actually requested. */
	static bool IsDrawEnabled();
	/** Render-thread-safe version of IsDrawEnabled. */
	static bool IsDrawEnabledOnAnyThread();

	/** Copies the non-overlapping cell partitions owned by the current mesh components. Call MarkRenderStateDirty() afterwards. */
	void UpdateFromMeshComponents(const TMap<int32, TObjectPtr<UDualContourMeshComponent>>& MeshComponents,
		FIntVector CellCount, float CellSize, FIntVector Divisions);

	struct FMeshEntry
	{
		FBox LocalBounds = FBox(ForceInit);
		FColor Color = FColor::White;
	};

	TArray<FMeshEntry> MeshEntries;
	FBoxSphereBounds CachedLocalBounds;
#endif
};
