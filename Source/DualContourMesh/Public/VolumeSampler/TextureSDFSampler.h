#pragma once

#include "VolumeSampler/VolumeSampler.h"
#include "TextureSDFSampler.generated.h"

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
