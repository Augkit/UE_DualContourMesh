#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "TextureSDFSampler.generated.h"

/** Shared signed-distance conversion and interpolation for texture-backed samplers. */
UCLASS(Abstract, BlueprintType, EditInlineNew, AutoExpandCategories = ("SDF"))
class DUALCONTOURMESH_API UTextureSDFSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	virtual bool Sample(const FVector& SamplerInputPosition, float& Value, float& Weight) const override;

	/** Density units per signed-distance unit. Negative SDF values become solid density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF", meta = (ClampMin = "0.0"))
	float DensityScale = 16.0f;

	/** Added after signed-distance conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	float DensityBias = 0.0f;

	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;

	virtual bool SupportsParallelSampling() const override { return true; }

protected:
	float SignedDistanceToDensity(float SignedDistance) const;
	float SampleCachedTexture(const FVector& NormalizedVolumePosition) const;
	virtual bool PrepareTexture(FText& OutError) const PURE_VIRTUAL(UTextureSDFSampler::PrepareTexture, return false;);

	mutable FIntVector CachedResolution = FIntVector::ZeroValue;
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
	virtual bool PrepareTexture(FText& OutError) const override;
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
	FIntVector VolumeResolution = FIntVector(64, 64, 64);

protected:
	virtual bool PrepareTexture(FText& OutError) const override;
};
