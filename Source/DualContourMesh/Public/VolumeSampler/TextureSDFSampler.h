#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "TextureSDFSampler.generated.h"

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

/** Samples a Texture3D/VolumeTexture exported by StaticMeshSDFExporter. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UTex3DSDFSampler : public UTextureSDFSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	TObjectPtr<UVolumeTexture> Texture;

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual bool PrepareTexture(FText& OutError) const override;
	virtual float SampleNormalized(const FVector& UVW) const override;
};

/** Samples a Z-slice Texture2D atlas exported by StaticMeshSDFExporter. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UTex2DSDFSampler : public UTextureSDFSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	TObjectPtr<UTexture2D> Texture;

	/** Original 3D export resolution. Z cannot be recovered from padded atlas dimensions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF", meta = (ClampMin = "1"))
	FVectorInt VolumeResolution = FVectorInt(64, 64, 64);

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual bool PrepareTexture(FText& OutError) const override;
	virtual float SampleNormalized(const FVector& UVW) const override;
};
