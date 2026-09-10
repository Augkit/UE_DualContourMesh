#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DualContourModifierComponent.generated.h"

class ADualContourMeshActor;
class UVolumeSampler;

/** Performs screen-based ray hits and applies configured volume samplers to dual-contour meshes. */
UCLASS(ClassGroup = (DualContour), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class DUALCONTOURMESH_API UDualContourModifierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDualContourModifierComponent();

	/** Configured volume samplers. Individual edits select one entry by index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "DualContour")
	TArray<TObjectPtr<UVolumeSampler>> Samplers;

	/** Creates a sampler instance and appends it to Samplers. Returns its array index, or INDEX_NONE on failure. */
	UFUNCTION(BlueprintCallable, Category = "DualContour")
	int32 AddSampler(TSubclassOf<UVolumeSampler> SamplerClass);

	/** Uniform scale used when placing every sampler on the target actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "0.0001"))
	float SamplerScale = 0.2f;

	/** Moves the ray hit position back toward the ray origin before applying the sampler, in Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "0.0"))
	float HitPositionRetreatDistance = 0.0f;

	/** Fires a world-space ray and modifies the hit dual-contour mesh. */
	UFUNCTION(BlueprintCallable, Category = "DualContour")
	bool ModifyDualContourWithRay(const FVector& WorldRayOrigin, const FVector& WorldRayDirection, int32 SamplerIndex, uint8 MaterialId, bool bExcavate);

private:
	bool ModifyDualContourWithSamplerAndDirection(const FVector& WorldHitPos, const FVector& WorldHitNormal,
		const FVector& WorldRayDirection, ADualContourMeshActor* MeshActor, int32 SamplerIndex,
		uint8 MaterialId, bool bExcavate);
};
