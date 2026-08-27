#pragma once

#include "CoreMinimal.h"
#include "VolumeSampler/VolumeSampler.h"
#include "ProceduralVolumeSampler.generated.h"

/**
 * Base class for analytic volume samplers.
 *
 * Positions passed to GetSignedDistance are measured in sampler-local units with
 * (0, 0, 0) at the center of VolumeSize. Negative distance is considered solid.
 * Blueprint subclasses can implement GetSignedDistance to define custom geometry.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UProceduralVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	/** Density units generated per sampler-local signed-distance unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Volume", meta = (ClampMin = "0.0001"))
	float DensityScale = 16.0f;

	/** Density offset applied after converting the signed distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Volume")
	float DensityBias = 0.0f;

	/**
	 * Returns the signed distance to the surface at a centered sampler-local position.
	 * Negative values are inside the solid, zero is on the surface, and positive values are outside.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Volume Sampling|Procedural")
	float GetSignedDistance(const FVector& LocalPosition) const;
	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const;

protected:
	virtual bool Prepare(FText& OutError) const override;
	virtual float SampleNormalized(const FVector& UVW) const override;
};

/** Analytic sphere centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API USphereVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Sphere", meta = (ClampMin = "0.0001"))
	float Radius = 256.0f;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};

/** Axis-aligned box centered in the sampled volume, with optional rounded corners. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UBoxVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Box", meta = (ClampMin = "0.0001"))
	FVector HalfExtents = FVector(256.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Box", meta = (ClampMin = "0.0"))
	float CornerRadius = 0.0f;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis capped cylinder centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UCylinderVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Cylinder", meta = (ClampMin = "0.0001"))
	float Radius = 192.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Cylinder", meta = (ClampMin = "0.0001"))
	float HalfHeight = 256.0f;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis capsule centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UCapsuleVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Capsule", meta = (ClampMin = "0.0001"))
	float Radius = 160.0f;

	/** Half length of the line segment between the centers of the two spherical caps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Capsule", meta = (ClampMin = "0.0"))
	float SegmentHalfLength = 128.0f;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis torus centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UTorusVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	/** Distance from the volume center to the center of the tube. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Torus", meta = (ClampMin = "0.0001"))
	float MajorRadius = 192.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry|Torus", meta = (ClampMin = "0.0001"))
	float MinorRadius = 64.0f;

	virtual float GetSignedDistance_Implementation(const FVector& LocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};
