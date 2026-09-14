#pragma once

#include "CoreMinimal.h"
#include "VolumeSampler/VolumeSampler.h"
#include "ProceduralVolumeSampler.generated.h"

/**
 * Base class for analytic volume samplers.
 *
 * Positions passed to GetSignedDistance are normalized sampler-local coordinates with
 * (0, 0, 0) at the volume center and half extents of 0.5. Negative distance is solid.
 * Blueprint subclasses can implement GetSignedDistance to define custom geometry.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UProceduralVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement, float& Value, float& Weight) const override;

	/** Density units generated per normalized signed-distance unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0001"))
	float DensityScale = 16.0f;

	/** Density offset applied after converting the signed distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	float DensityBias = 0.0f;

	/**
	 * Returns the normalized signed distance to the surface at a centered sampler-local position.
	 * Negative values are inside the solid, zero is on the surface, and positive values are outside.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Volume Sampling|Procedural")
	float GetSignedDistance(const FVector& CenteredLocalPosition) const;
	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const;

	virtual bool Prepare(FText& OutError) const override;

	virtual bool SupportsParallelSampling() const override;
};

/** Analytic sphere centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Sphere"))
class DUALCONTOURMESH_API USphereVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sphere", meta = (ClampMin = "0.0001"))
	float Radius = 0.4f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};

/** Axis-aligned box centered in the sampled volume, with optional rounded corners. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Box"))
class DUALCONTOURMESH_API UBoxVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (ClampMin = "0.0001"))
	FVector HalfExtents = FVector(0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box", meta = (ClampMin = "0.0"))
	float CornerRadius = 0.0f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis capped cylinder centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Cylinder"))
class DUALCONTOURMESH_API UCylinderVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (ClampMin = "0.0001"))
	float Radius = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cylinder", meta = (ClampMin = "0.0001"))
	float HalfHeight = 0.4f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis capsule centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Capsule"))
class DUALCONTOURMESH_API UCapsuleVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "0.0001"))
	float Radius = 0.25f;

	/** Half length of the line segment between the centers of the two spherical caps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "0.0"))
	float SegmentHalfLength = 0.2f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

	virtual bool Prepare(FText& OutError) const override;
};

/** Z-axis torus centered in the sampled volume. */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Torus"))
class DUALCONTOURMESH_API UTorusVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	/** Distance from the volume center to the center of the tube. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (ClampMin = "0.0001"))
	float MajorRadius = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torus", meta = (ClampMin = "0.0001"))
	float MinorRadius = 0.1f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

	virtual bool Prepare(FText& OutError) const override;
};
