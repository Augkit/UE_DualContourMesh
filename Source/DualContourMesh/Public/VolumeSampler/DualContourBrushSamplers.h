#pragma once
#include "VolumeSampler/VolumeSampler.h"
#include "DualContourBrushSamplers.generated.h"

enum class EDualContourEditFalloff : uint8
{
	Smooth,
	Linear,
	Spherical,
	Tip
};

UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourShapeVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	FVector TargetLocalCenter = FVector::ZeroVector;
	FVector TargetLocalNormal = FVector::UpVector;
	float Radius = 100.0f;
	float Falloff = 0.5f;
	EDualContourEditFalloff FalloffType = EDualContourEditFalloff::Smooth;
	bool bBox = false;
	bool bDirectional = false;
	static float EvaluateFalloff(float Distance, float Falloff, EDualContourEditFalloff Type);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const override;
};

/** A plane density field restricted by another sampler's mask. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourPlaneVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(UVolumeSampler& InMask, FVector InTargetLocalOrigin, FVector InTargetLocalNormal, float InDensityScale)
	{
		Mask = &InMask;
		TargetLocalOrigin = InTargetLocalOrigin;
		TargetLocalNormal = InTargetLocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		DensityScale = InDensityScale;
	}

	virtual FBox GetBounds() const override
	{
		return Mask ? Mask->GetBounds() : FBox(ForceInit);
	}

	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const override;

	virtual bool Prepare(FText& OutError) const override
	{
		return Mask && Mask->Prepare(OutError);
	}

	virtual void Finish() const override
	{
		if (Mask)
			Mask->Finish();
	}

private:
	UPROPERTY()
	TObjectPtr<UVolumeSampler> Mask = nullptr;
	FVector TargetLocalOrigin = FVector::ZeroVector;
	FVector TargetLocalNormal = FVector::UpVector;
	float DensityScale = 1;
};

/** Independent volume brush; SourceToTargetTransform maps source-local coordinates into target-local space. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourVolumeBrushSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(const UDualContour& InSource, const FTransform& InSourceToTargetTransform);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const override;

	virtual bool SupportsParallelSampling() const override
	{
		return true;
	}

private:
	TWeakObjectPtr<const UDualContour> Source;
	FTransform SourceToTargetTransform;
};

/** Restores matching grid samples through another sampler's mask. The caller validates source/target grid compatibility. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourRestoreVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(UVolumeSampler& InMask, const UDualContour& InSource);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
		float& Value, float& Weight) const override;

	virtual bool Prepare(FText& OutError) const override
	{
		return Mask && Mask->Prepare(OutError);
	}

	virtual void Finish() const override
	{
		if (Mask)
			Mask->Finish();
	}

private:
	UPROPERTY()
	TObjectPtr<UVolumeSampler> Mask = nullptr;
	TWeakObjectPtr<const UDualContour> Source;
};
