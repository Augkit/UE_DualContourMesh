#pragma once

#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "HeightmapSampler.generated.h"

class UTexture2D;

/** Height field whose surface is displaced along sampler-local Z by a repeating Texture2D. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UHeightmapSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	/** Grayscale height texture. A value of 0.5 places the surface at the center of VolumeSize.Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Heightmap")
	TObjectPtr<UTexture2D> Heightmap;

	/** Total sampler-local Z displacement from a height value of 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Heightmap")
	float HeightOffset = 256.0f;

	/** Number of times the heightmap repeats across VolumeSize.XY. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Heightmap",
		meta = (ClampMin = "0.0001", UIMin = "0.1"))
	FVector2D Tiling = FVector2D(1.0, 1.0);

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;

private:
	float SampleHeight(const FVector2D& UV) const;

	mutable FIntPoint CachedHeightmapSize = FIntPoint::ZeroValue;
	mutable TArray<float> CachedHeightValues;
};
