#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "VolumeSampler.generated.h"

class UDualContour;
class USVTDensityField;
class UTexture2D;
class UVolumeTexture;

/** Samples a finite volume into a DualContour density grid. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
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
	bool SampleToDualContour(UDualContour* Target, const FTransform& SampleTransform, FText& OutError) const;

	/** Combines this sampler with existing target density and returns the rebuilt half-open cell range. */
	bool ModifyDualContour(UDualContour* Target, const FTransform& SampleTransform, bool bExcavate,
		FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax, FText& OutError) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual bool Prepare(FText& OutError) const;
	virtual void Finish() const;
	virtual float SampleNormalized(const FVector& UVW) const PURE_VIRTUAL(UVolumeSampler::SampleNormalized, return 0.0f;);

private:
	/** Shared sampling path used by both initialization and density modification. */
	bool BuildDensitySamples(UDualContour* Target, const FTransform& SampleTransform,
		TArray<uint8>& OutSamples, FText& OutError) const;
};

/** Shared signed-distance conversion and interpolation for texture-backed samplers. */
UCLASS(Abstract, BlueprintType, EditInlineNew)
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

/** Base class for normalized sampling of an existing DualContour. */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContourSampler : public UVolumeSampler
{
	GENERATED_BODY()

protected:
	virtual UDualContour* ResolveDualContour() const PURE_VIRTUAL(UDualContourSampler::ResolveDualContour, return nullptr;);
	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;
	virtual float SampleNormalized(const FVector& UVW) const override;

	mutable TWeakObjectPtr<UDualContour> CachedDualContour;
};

/** Samples the baked DualContour stored in a USVTDensityField. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API USVTDualContourSampler : public UDualContourSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	TObjectPtr<USVTDensityField> DensityField;

protected:
	virtual UDualContour* ResolveDualContour() const override;
};
