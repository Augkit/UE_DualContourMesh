#pragma once

#include "CoreMinimal.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "NoiseVolumeSampler.generated.h"

class FastNoiseLite;

/** Determines how noise is converted into a procedural signed-distance field. */
UENUM(BlueprintType)
enum class ENoiseSamplerDimension : uint8
{
	HeightField2D UMETA(DisplayName = "2D Height Field"),
	Volume3D UMETA(DisplayName = "3D Volume")
};

UENUM(BlueprintType)
enum class ENoiseSamplerType : uint8
{
	OpenSimplex2,
	OpenSimplex2S,
	Cellular,
	Perlin,
	ValueCubic UMETA(DisplayName = "Value Cubic"),
	Value
};

UENUM(BlueprintType)
enum class ENoiseSamplerFractalType : uint8
{
	None,
	FBm UMETA(DisplayName = "FBm"),
	Ridged,
	PingPong UMETA(DisplayName = "Ping Pong")
};

UENUM(BlueprintType)
enum class ENoiseSamplerRotation3D : uint8
{
	None,
	ImproveXYPlanes UMETA(DisplayName = "Improve XY Planes"),
	ImproveXZPlanes UMETA(DisplayName = "Improve XZ Planes")
};

UENUM(BlueprintType)
enum class ENoiseSamplerCellularDistance : uint8
{
	Euclidean,
	EuclideanSquared UMETA(DisplayName = "Euclidean Squared"),
	Manhattan,
	Hybrid
};

UENUM(BlueprintType)
enum class ENoiseSamplerCellularReturn : uint8
{
	CellValue UMETA(DisplayName = "Cell Value"),
	Distance,
	Distance2,
	Distance2Add UMETA(DisplayName = "Distance 2 Add"),
	Distance2Subtract UMETA(DisplayName = "Distance 2 Subtract"),
	Distance2Multiply UMETA(DisplayName = "Distance 2 Multiply"),
	Distance2Divide UMETA(DisplayName = "Distance 2 Divide")
};

/**
 * Continuous 2D/3D procedural noise sampler backed by FastNoiseLite.
 *
 * HeightField2D treats noise as a sampler-local Z height and is a precision-independent
 * alternative to UHeightmapSampler. Volume3D extracts an isosurface from volumetric noise.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Noise"))
class DUALCONTOURMESH_API UNoiseVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UNoiseVolumeSampler();
	virtual ~UNoiseVolumeSampler() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	ENoiseSamplerDimension Dimension = ENoiseSamplerDimension::HeightField2D;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	int32 Seed = 1337;

	/** Noise frequency in inverse sampler-local units. Smaller values produce larger features. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.000001", UIMin = "0.0001", UIMax = "0.1"))
	float Frequency = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	ENoiseSamplerType NoiseType = ENoiseSamplerType::OpenSimplex2;

	/** Rotation applied internally to reduce directional artifacts in 3D noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	ENoiseSamplerRotation3D Rotation3D = ENoiseSamplerRotation3D::None;

	/** Added to sampler-local coordinates before evaluating noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	FVector CoordinateOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal")
	ENoiseSamplerFractalType FractalType = ENoiseSamplerFractalType::FBm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal", meta = (ClampMin = "1", ClampMax = "30"))
	int32 Octaves = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal", meta = (ClampMin = "0.0001"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Gain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeightedStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Fractal", meta = (ClampMin = "0.0001"))
	float PingPongStrength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Cellular",
		meta = (EditCondition = "NoiseType == ENoiseSamplerType::Cellular", EditConditionHides))
	ENoiseSamplerCellularDistance CellularDistance = ENoiseSamplerCellularDistance::EuclideanSquared;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Cellular",
		meta = (EditCondition = "NoiseType == ENoiseSamplerType::Cellular", EditConditionHides))
	ENoiseSamplerCellularReturn CellularReturn = ENoiseSamplerCellularReturn::Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Cellular",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "NoiseType == ENoiseSamplerType::Cellular", EditConditionHides))
	float CellularJitter = 1.0f;

	/** Sampler-local Z position corresponding to a zero noise value in 2D mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|2D Height Field",
		meta = (EditCondition = "Dimension == ENoiseSamplerDimension::HeightField2D", EditConditionHides))
	float HeightOffset = 0.0f;

	/** Maximum sampler-local height displacement produced by noise in 2D mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|2D Height Field",
		meta = (ClampMin = "0.0", EditCondition = "Dimension == ENoiseSamplerDimension::HeightField2D", EditConditionHides))
	float HeightAmplitude = 128.0f;

	/** Noise value whose isosurface is extracted in 3D mode. Values above it are solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|3D Volume",
		meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "Dimension == ENoiseSamplerDimension::Volume3D", EditConditionHides))
	float IsoLevel = 0.0f;

	/** Converts the dimensionless 3D noise difference into sampler-local pseudo-distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|3D Volume",
		meta = (ClampMin = "0.0001", EditCondition = "Dimension == ENoiseSamplerDimension::Volume3D", EditConditionHides))
	float NoiseDistanceScale = 100.0f;

	/** Evaluates continuous 2D noise in sampler-local coordinate space. Output is nominally -1..1. */
	UFUNCTION(BlueprintPure, Category = "Volume Sampling|Noise")
	float SampleNoise2D(const FVector2D& LocalPosition) const;

	/** Evaluates continuous 3D noise in sampler-local coordinate space. Output is nominally -1..1. */
	UFUNCTION(BlueprintPure, Category = "Volume Sampling|Noise")
	float SampleNoise3D(const FVector& LocalPosition) const;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual void Finish() const override;

private:
	void ConfigureNoise(FastNoiseLite& Noise) const;

	mutable TUniquePtr<FastNoiseLite> CachedNoise;
};
