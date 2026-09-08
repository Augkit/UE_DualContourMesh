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

	/** Bounds and samples are in the target contour's local space. Call BeginSampling/EndSampling around a pass. */
	virtual FBox GetBounds() const;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const;

	bool BeginSampling(FText& OutError) const
	{
		return Prepare(OutError);
	}

	void EndSampling() const
	{
		Finish();
	}

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
	virtual bool Prepare(FText& OutError) const;
	virtual void Finish() const;
	/** True when SampleNormalized may be called concurrently while the game thread is blocked. */
	virtual bool SupportsParallelSampling() const { return false; }
	virtual float SampleNormalized(const FVector& UVW) const PURE_VIRTUAL(UVolumeSampler::SampleNormalized, return 0.0f;);

};
