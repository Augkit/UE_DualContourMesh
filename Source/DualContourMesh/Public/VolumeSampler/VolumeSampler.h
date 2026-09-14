#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VolumeSampler.generated.h"

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE(FOnVolumeSamplerPropertyChanged);
#endif

/**
 * Immutable mapping from target-local positions into the sampler's internal input space,
 * combining the outer placement transform with the sampler volume scale into a single affine.
 * Build once per sampling pass with UVolumeSampler::MakePlacement.
 */
struct FVolumeSamplerPlacement
{
	FMatrix TargetToSamplerNormalizedMatrix = FMatrix::Identity;
	FVector SamplingVolumeSize = FVector::OneVector;
	/** Optional encoded-density slope limit supplied by the destination grid. Zero keeps authored density units. */
	float MaxTargetDensitySlope = 0.0f;
};

/** Samples a finite volume into a DualContour density grid. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, AutoExpandCategories = ("Volume"))
class DUALCONTOURMESH_API UVolumeSampler : public UObject
{
	GENERATED_BODY()

public:
	/** Bounds are expressed in target-local units for the supplied sampling volume. */
	virtual FBox GetSamplingBounds(const FVector& SamplingVolumeSize) const;

	/**
	 * Converts target-local positions into normalized sampler coordinates. The placement transform
	 * maps the sampler volume in target-local units and rotates/scales it about Pivot * SamplingVolumeSize.
	 * Pass nullptr when no outer placement transform is needed.
	 * MaxTargetDensitySlope is an optional target-grid density-gradient limit; zero disables it.
	 */
	FVolumeSamplerPlacement MakePlacement(const FVector& SamplingVolumeSize, const FTransform* SamplerPivotTransform,
		float MaxTargetDensitySlope = 0.0f) const;

	/**
	 * Transforms a sampler-local box using a transform whose location is the sampler pivot target position.
	 * The sampler's normalized Pivot is converted to sampler-volume units using SamplingVolumeSize.
	 */
	FBox TransformBoxByPivotTransform(const FBox& Box, const FTransform& SamplerPivotTransform, const FVector& SamplingVolumeSize) const;

	/**
	 * Samples a continuous position measured in the target contour's local units, through the placement
	 * produced by MakePlacement for this pass. The sampler must have been Prepared beforehand.
	 */
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const PURE_VIRTUAL(UVolumeSampler::Sample, return false;);
	/** Prepares resources and validates the sampler for one sampling pass. */
	virtual bool Prepare(FText& OutError) const;
	/** Releases resources held for the current sampling pass. */
	virtual void Finish() const;

	/** True when Sample may be called concurrently while the game thread is blocked. */
	virtual bool SupportsParallelSampling() const { return false; }

	/** Normalized point about which the placement transform rotates and scales. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Pivot = FVector(0.5);

#if WITH_EDITOR
	FOnVolumeSamplerPropertyChanged OnPropertyChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

protected:
	/**
	 * Maps a target-local position through the placement into normalized base-volume coordinates;
	 * false when the point lies outside [0, 1].
	 */
	bool TryGetNormalizedPosition(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		FVector& OutNormalizedPosition) const;

};
