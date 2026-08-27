#pragma once

#include "Curves/CurveFloat.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "HeightmapSampler.generated.h"

class UTexture2D;

/** Height field whose surface is displaced along sampler-local Z by a repeating Texture2D. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Heightmap"))
class DUALCONTOURMESH_API UHeightmapSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UHeightmapSampler();

	/** Grayscale height texture whose sampled value is passed through HeightCurve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heightmap")
	TObjectPtr<UTexture2D> Heightmap;

	/** Maps the sampled grayscale value to a normalized height. Input and effective output are limited to 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heightmap")
	FRuntimeFloatCurve HeightCurve;

	/** Vertical baseline in normalized volume space. Increasing this value moves the surface upward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heightmap", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Bias = 0.5f;

	/** Number of times the heightmap repeats across VolumeSize.XY. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heightmap", meta = (ClampMin = "0.0001", UIMin = "0.1"))
	FVector2D Tiling = FVector2D(1.0, 1.0);

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;

private:
	float SampleHeight(const FVector2D& UV) const;

#if WITH_EDITOR
	void ClampHeightCurve();
#endif

	mutable FIntPoint CachedHeightmapSize = FIntPoint::ZeroValue;
	mutable TArray<float> CachedHeightValues;
};
