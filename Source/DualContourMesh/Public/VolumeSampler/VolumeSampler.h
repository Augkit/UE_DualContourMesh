#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VolumeSampler.generated.h"

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE(FOnVolumeSamplerPropertyChanged);
#endif

/** Samples a finite volume into a DualContour density grid. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, AutoExpandCategories = ("Volume"))
class DUALCONTOURMESH_API UVolumeSampler : public UObject
{
	GENERATED_BODY()

public:
	/** Target-local placement, rotating/scaling about Pivot * VolumeSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FTransform SamplingTransform = FTransform::Identity;

	/** Bounds and samples are in the target contour's local space. Call Prepare/Finish around a pass. */
	virtual FBox GetBounds() const;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const PURE_VIRTUAL(UVolumeSampler::Sample, return false;);

	/** Prepares resources and validates the sampler for one sampling pass. */
	virtual bool Prepare(FText& OutError) const;
	/** Releases resources held for the current sampling pass. */
	virtual void Finish() const;

	bool CanSampleInParallel() const
	{
		return SupportsParallelSampling();
	}

	/** Size of the sampled volume in DualContour local-space units before SampleTransform is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0001"))
	FVector VolumeSize = FVector(640.0);

	/** Normalized point about which SampleTransform rotates and scales. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Pivot = FVector(0.5);

#if WITH_EDITOR
	FOnVolumeSamplerPropertyChanged OnPropertyChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

protected:
	/** True when Sample may be called concurrently while the game thread is blocked. */
	virtual bool SupportsParallelSampling() const { return false; }
	/** Maps target-local Position through the volume placement; false outside [0, 1]. */
	bool TryGetNormalizedPosition(const FVector& Position, FVector& OutUVW) const;

};
