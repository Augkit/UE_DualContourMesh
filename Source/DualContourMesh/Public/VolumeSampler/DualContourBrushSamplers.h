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
	FVector Center = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	float Radius = 100.0f;
	float Falloff = 0.5f;
	EDualContourEditFalloff FalloffType = EDualContourEditFalloff::Smooth;
	bool bBox = false;
	bool bDirectional = false;
	static float EvaluateFalloff(float Distance, float Falloff, EDualContourEditFalloff Type);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

protected:
	virtual float SampleNormalized(const FVector& UVW) const override
	{
		return 0.0f;
	}
};

/** A plane density field restricted by another sampler's mask. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourPlaneVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(UVolumeSampler& InMask, FVector InOrigin, FVector InNormal, float InScale)
	{
		Mask = &InMask;
		Origin = InOrigin;
		Normal = InNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		Scale = InScale;
	}

	virtual FBox GetBounds() const override
	{
		return Mask ? Mask->GetBounds() : FBox(ForceInit);
	}

	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

protected:
	virtual float SampleNormalized(const FVector& UVW) const override
	{
		return 0.0f;
	}

protected:
	virtual bool Prepare(FText& OutError) const override
	{
		return Mask && Mask->BeginSampling(OutError);
	}

	virtual void Finish() const override
	{
		if (Mask)
			Mask->EndSampling();
	}

private:
	UPROPERTY()
	TObjectPtr<UVolumeSampler> Mask = nullptr;
	FVector Origin = FVector::ZeroVector, Normal = FVector::UpVector;
	float Scale = 1;
};

/** Independent volume brush; SourceToTarget maps contour-local coordinates into target-local space. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourVolumeBrushSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(const UDualContour& InSource, const FTransform& InSourceToTarget);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

protected:
	virtual float SampleNormalized(const FVector& UVW) const override
	{
		return 0.0f;
	}

public:
	virtual bool SupportsParallelSampling() const override
	{
		return true;
	}

private:
	TWeakObjectPtr<const UDualContour> Source;
	FTransform SourceToTarget;
};

/** Restores matching grid samples through another sampler's mask. The caller validates source/target grid compatibility. */
UCLASS(NotBlueprintable, EditInlineNew)
class DUALCONTOURMESH_API UDualContourRestoreVolumeSampler : public UVolumeSampler
{
	GENERATED_BODY()

public:
	void Initialize(UVolumeSampler& InMask, const UDualContour& InSource);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

protected:
	virtual float SampleNormalized(const FVector& UVW) const override
	{
		return 0.0f;
	}

protected:
	virtual bool Prepare(FText& OutError) const override
	{
		return Mask && Mask->BeginSampling(OutError);
	}

	virtual void Finish() const override
	{
		if (Mask)
			Mask->EndSampling();
	}

private:
	UPROPERTY()
	TObjectPtr<UVolumeSampler> Mask = nullptr;
	TWeakObjectPtr<const UDualContour> Source;
};
