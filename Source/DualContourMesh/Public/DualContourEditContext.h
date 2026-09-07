#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"
#include "Templates/Function.h"

class UDualContour;

/** Target-local sampling. Returning false excludes the position from the operation. */
class DUALCONTOURMESH_API FDualContourEditSampler
{
public:
	virtual ~FDualContourEditSampler() = default;
	virtual FBox GetBounds() const = 0;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const = 0;
	/** Workers only sample while the game thread is blocked; custom samplers opt in explicitly. */
	virtual bool SupportsParallelSampling() const
	{
		return false;
	}
};

enum class EDualContourEditFalloff : uint8
{
	Smooth,
	Linear,
	Spherical,
	Tip
};

class DUALCONTOURMESH_API FDualContourShapeSampler : public FDualContourEditSampler
{
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
};

/** A plane density field restricted by another sampler's mask. */
class DUALCONTOURMESH_API FDualContourPlaneSampler : public FDualContourEditSampler
{
public:
	FDualContourPlaneSampler(const FDualContourEditSampler& InMask, FVector InOrigin, FVector InNormal, float InScale)
	    : Mask(InMask), Origin(InOrigin), Normal(InNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)), Scale(InScale)
	{
	}
	virtual FBox GetBounds() const override
	{
		return Mask.GetBounds();
	}
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

private:
	const FDualContourEditSampler& Mask;
	FVector Origin, Normal;
	float Scale;
};

/** Independent volume brush; SourceToTarget maps contour-local coordinates into target-local space. */
class DUALCONTOURMESH_API FDualContourVolumeSampler : public FDualContourEditSampler
{
public:
	FDualContourVolumeSampler(const UDualContour& InSource, const FTransform& InSourceToTarget);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;
	virtual bool SupportsParallelSampling() const override
	{
		return true;
	}

private:
	TWeakObjectPtr<const UDualContour> Source;
	FTransform SourceToTarget;
};

/** Restores matching grid samples through another sampler's mask. The caller validates source/target grid compatibility. */
class DUALCONTOURMESH_API FDualContourRestoreSampler : public FDualContourEditSampler
{
public:
	FDualContourRestoreSampler(const FDualContourEditSampler& InMask, const UDualContour& InSource);
	virtual FBox GetBounds() const override;
	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override;

private:
	const FDualContourEditSampler& Mask;
	TWeakObjectPtr<const UDualContour> Source;
};

enum class EDualContourEditOperation : uint8
{
	Add,
	Subtract,
	Union,
	Difference,
	Replace,
	Smooth
};

/** One submission, on the game thread. The caller keeps the target alive and avoids other writes until Commit.
 * Reads see staged values. Destruction discards unsubmitted edits; copying and repeated submission are forbidden.
 */
class DUALCONTOURMESH_API FDualContourEditContext
{
public:
	explicit FDualContourEditContext(UDualContour& InTarget);
	FDualContourEditContext(const FDualContourEditContext&) = delete;
	FDualContourEditContext& operator=(const FDualContourEditContext&) = delete;
	bool IsOpen() const;
	UDualContour* GetTarget() const;
	float GetDensity(FIntVector Coord) const;
	uint8 GetMaterial(FIntVector Coord) const;
	bool SetDensity(FIntVector Coord, float Value);
	bool SetMaterial(FIntVector Coord, uint8 Value);
	bool ApplyDensity(const FDualContourEditSampler& Sampler, EDualContourEditOperation Operation, float Strength = 1.0f);
	bool ApplyMaterial(const FDualContourEditSampler& Sampler, uint8 MaterialId, float Threshold = 0.5f, bool bSolidOnly = true);
	bool ApplySampledRegion(const FDualContourSampledRegion& Region, bool bExcavate);
	bool Commit(
	    FDualContourMaterialEditResult& MaterialResult,
	    TFunctionRef<void(const FIntVector&, uint16, uint16)> OnDensityChanged = [](const FIntVector&, uint16, uint16) {});
	bool Commit();

private:
	bool GetSampleBounds(const FDualContourEditSampler& Sampler, FIntVector& Min, FIntVector& Max) const;
	TWeakObjectPtr<UDualContour> Target;
	FDualContourPendingBatch DensityBatch;
	FDualContourPendingMaterialBatch MaterialBatch;
	bool bOpen = true;
};
