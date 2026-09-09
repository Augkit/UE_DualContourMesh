#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VolumeSampler.generated.h"

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE(FOnVolumeSamplerPropertyChanged);
#endif

class UDualContour;

/**
 * Immutable mapping from target-local positions into the sampler's internal input space,
 * combining the outer placement transform with SamplingTransform into a single affine.
 * Build once per sampling pass with UVolumeSampler::MakePlacement.
 */
struct FVolumeSamplerPlacement
{
	FMatrix TargetToSamplerLocalMatrix = FMatrix::Identity;
};

/** Samples a finite volume into a DualContour density grid. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, AutoExpandCategories = ("Volume"))
class DUALCONTOURMESH_API UVolumeSampler : public UObject
{
	GENERATED_BODY()

public:
	/** Bounds are expressed in sampler-input coordinates after SamplingTransform. */
	virtual FBox GetBounds() const;

	/**
	 * Combines SamplerToTargetTransform with SamplingTransform (both rotate/scale about Pivot * VolumeSize)
	 * into the single affine used by Sample. Pass nullptr when no outer placement transform is needed.
	 */
	FVolumeSamplerPlacement MakePlacement(const FTransform* SamplerToTargetTransform) const;

	/** Transforms a box about PivotPosition by Transform (forward direction). */
	static FBox TransformBoxAroundPivot(const FBox& Box, const FTransform& Transform, const FVector& PivotPosition);

	/**
	 * Samples a continuous position measured in the target contour's local units, through the placement
	 * produced by MakePlacement for this pass. The sampler must have been Prepared beforehand.
	 */
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const PURE_VIRTUAL(UVolumeSampler::Sample, return false;);
	/** Samples this volume and applies its density directly to the target contour. */
	bool ApplyToDualContour(UDualContour* Target, const FTransform& SamplerToTargetTransform, FText& OutError) const;

	/** Prepares resources and validates the sampler for one sampling pass. */
	virtual bool Prepare(FText& OutError) const;
	/** Releases resources held for the current sampling pass. */
	virtual void Finish() const;

	/** True when Sample may be called concurrently while the game thread is blocked. */
	virtual bool SupportsParallelSampling() const { return false; }

	/** Maps base-volume coordinates into sampler-input coordinates about Pivot * VolumeSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FTransform SamplingTransform = FTransform::Identity;

	/** Size of the base volume in target-local length units before placement transforms are applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0001"))
	FVector VolumeSize = FVector(640.0);

	/** Normalized base-volume point about which placement transforms rotate and scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Pivot = FVector(0.5);

#if WITH_EDITOR
	FOnVolumeSamplerPropertyChanged OnPropertyChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

protected:
	/**
	 * Maps a target-local position through the placement into base-volume coordinates (UE units,
	 * origin at the volume corner); false when the point lies outside [0, VolumeSize].
	 */
	bool TryGetBaseVolumePosition(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		FVector& OutBaseVolumePosition) const;

};
