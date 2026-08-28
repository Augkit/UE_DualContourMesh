#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "VolumeSampler.generated.h"

class UDualContour;
class USVTDualContour;
class UTexture2D;
class UVolumeTexture;

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
		FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax, FText& OutError);

#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual bool Prepare(FText& OutError) const;
	virtual void Finish() const;
	/** True when SampleNormalized may be called concurrently while the game thread is blocked. */
	virtual bool SupportsParallelSampling() const { return false; }
	virtual float SampleNormalized(const FVector& UVW) const PURE_VIRTUAL(UVolumeSampler::SampleNormalized, return 0.0f;);

private:
	/** Samples only the transformed volume's conservative grid-aligned subrange. */
	bool BuildDensitySamples(UDualContour* Target, const FTransform& SampleTransform,
		FVectorInt& OutSampleMin, FVectorInt& OutSampleDimensions,
		TArray<uint8>& OutSamples, FText& OutError) const;
};

/** Shared signed-distance conversion and interpolation for texture-backed samplers. */
UCLASS(Abstract, BlueprintType, EditInlineNew, AutoExpandCategories = ("SDF"))
class DUALCONTOURMESH_API UTextureSDFSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	/** Density units per signed-distance unit. Negative SDF values become solid density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF", meta = (ClampMin = "0.0"))
	float DensityScale = 16.0f;

	/** Added after signed-distance conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	float DensityBias = 0.0f;

protected:
	float SignedDistanceToDensity(float SignedDistance) const;
	float SampleCachedTexture(const FVector& UVW) const;
	virtual bool SupportsParallelSampling() const override { return true; }
	virtual bool PrepareTexture(FText& OutError) const PURE_VIRTUAL(UTextureSDFSampler::PrepareTexture, return false;);
	virtual void Finish() const override;

	mutable FVectorInt CachedResolution;
	mutable TArray<float> CachedSignedDistances;
};
