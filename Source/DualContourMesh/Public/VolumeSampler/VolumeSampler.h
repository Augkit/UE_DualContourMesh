#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "VolumeSampler.generated.h"

class UDualContour;
class USVTDualContour;
class UTexture2D;
class UVolumeTexture;

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE(FOnVolumeSamplerPropertyChanged);
#endif

/** Samples a finite volume into a DualContour density grid. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, AutoExpandCategories = ("Volume"))
class DUALCONTOURMESH_API UVolumeSampler : public UObject
{
	GENERATED_BODY()

public:
	/** Size of the sampled volume in DualContour local-space units before SampleTransform is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0001"))
	FVector VolumeSize = FVector(640.0);

	/** Normalized point about which SampleTransform rotates and scales. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Pivot = FVector(0.5);

	/** Fills Target with this sampler after applying translation, rotation and scale about Pivot. */
	UFUNCTION(BlueprintCallable, Category = "Volume Sampling")
	bool ReplaceDualContour(UDualContour* Target, const FTransform& SampleTransform, FText& OutError);

	/** Combines this sampler with existing target density and returns the rebuilt half-open cell range. */
	bool ModifyDualContour(UDualContour* Target, const FTransform& SampleTransform, bool bExcavate,
		FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax, FText& OutError);

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

private:
	/** Samples the transformed volume directly into independently owned density chunks. */
	bool BuildDensityChunks(UDualContour* Target, const FTransform& SampleTransform, FDualContourSampledRegion& OutRegion, FText& OutError) const;
};
