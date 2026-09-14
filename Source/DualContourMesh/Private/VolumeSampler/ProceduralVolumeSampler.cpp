#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "DualContourTypes.h"

namespace
{
bool IsPositiveVector(const FVector& Value)
{
	return Value.X > UE_SMALL_NUMBER && Value.Y > UE_SMALL_NUMBER && Value.Z > UE_SMALL_NUMBER;
}

float CappedCylinderSignedDistance(const FVector& Position, float Radius, float HalfHeight)
{
	const FVector2D Distance(FVector2D(Position.X, Position.Y).Length() - Radius,
		FMath::Abs(Position.Z) - HalfHeight);
	const FVector2D Outside(FMath::Max(Distance.X, 0.0), FMath::Max(Distance.Y, 0.0));
	return Outside.Length() + FMath::Min(FMath::Max(Distance.X, Distance.Y), 0.0);
}

}

bool UProceduralVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!FMath::IsFinite(DensityScale) || DensityScale <= UE_SMALL_NUMBER || !FMath::IsFinite(DensityBias))
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidDensityConversion",
			"LinearDensityScale must be positive and LinearDensityScale/LinearDensityBias must be finite.");
		return false;
	}
	return true;
}

bool UProceduralVolumeSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement, float& Value, float& Weight) const
{
	FVector NormalizedPosition;
	if (!TryGetNormalizedPosition(TargetLocalPosition, Placement, NormalizedPosition))
		return false;
	Weight = 1.0f;
	const FVector CenteredLocalPosition = NormalizedPosition - 0.5;
	const float SignedDistance = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
		                             ? GetSignedDistance(CenteredLocalPosition)
		                             : GetSignedDistance_Implementation(CenteredLocalPosition);
	// Shape parameters are normalized, but the density field must retain a distance
	// slope in target-local units. Without this conversion a 400-unit brush weakens
	// the SDF gradient by 400x relative to pre-normalization terrain assets.
	const double DistanceUnit = Placement.SamplingVolumeSize.GetMin();
	Value = FMath::IsFinite(SignedDistance)
		        ? (DensityBias - SignedDistance * DistanceUnit * DensityScale) * GDualContourLinearDensityFixedPointScale
		        : 0.0f;
	if (Placement.MaxTargetDensitySlope > 0.0f)
	{
		// Bound the gradient under the full affine placement, including nonuniform
		// scales. Rescale the whole field (including bias), preserving its zero set.
		// The Frobenius norm is a conservative bound on the inverse transform norm.
		const double TransformBound = FMath::Sqrt(
			Placement.TargetToSamplerNormalizedMatrix.TransformVector(FVector::ForwardVector).SizeSquared()
			+ Placement.TargetToSamplerNormalizedMatrix.TransformVector(FVector::RightVector).SizeSquared()
			+ Placement.TargetToSamplerNormalizedMatrix.TransformVector(FVector::UpVector).SizeSquared());
		const double SlopeBound = DistanceUnit * DensityScale * GDualContourLinearDensityFixedPointScale * TransformBound;
		if (SlopeBound > Placement.MaxTargetDensitySlope)
			Value *= Placement.MaxTargetDensitySlope / SlopeBound;
	}
	return FMath::IsFinite(Value);
}

float UProceduralVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	return 1.0e20f;
}

bool UProceduralVolumeSampler::SupportsParallelSampling() const
{
	// Blueprint event dispatch uses ProcessEvent and must stay on the game thread. Native
	// implementations are called directly by Sample and only read prepared state.
	return !GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
}

bool USphereVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidSphereRadius", "Sphere Radius must be positive and finite.");
		return false;
	}
	return true;
}

float USphereVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	return CenteredLocalPosition.Length() - Radius;
}

bool UBoxVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	const double MinimumHalfExtent = FMath::Min3(HalfExtents.X, HalfExtents.Y, HalfExtents.Z);
	if (HalfExtents.ContainsNaN() || !IsPositiveVector(HalfExtents)
	    || !FMath::IsFinite(CornerRadius) || CornerRadius < 0.0f || CornerRadius > MinimumHalfExtent)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidBoxSettings",
			"Box HalfExtents must be positive and finite; CornerRadius must be between zero and the smallest half extent.");
		return false;
	}
	return true;
}

float UBoxVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	const FVector RoundedCore = HalfExtents - FVector(CornerRadius);
	const FVector Distance = CenteredLocalPosition.GetAbs() - RoundedCore;
	const FVector Outside(FMath::Max(Distance.X, 0.0), FMath::Max(Distance.Y, 0.0), FMath::Max(Distance.Z, 0.0));
	return Outside.Length() + FMath::Min(FMath::Max3(Distance.X, Distance.Y, Distance.Z), 0.0) - CornerRadius;
}

bool UCylinderVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER
	    || !FMath::IsFinite(HalfHeight) || HalfHeight <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidCylinderSettings",
			"Cylinder Radius and HalfHeight must be positive and finite.");
		return false;
	}
	return true;
}

float UCylinderVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	return CappedCylinderSignedDistance(CenteredLocalPosition, Radius, HalfHeight);
}

bool UCapsuleVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER
	    || !FMath::IsFinite(SegmentHalfLength) || SegmentHalfLength < 0.0f)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidCapsuleSettings",
			"Capsule Radius must be positive; SegmentHalfLength must be non-negative; both must be finite.");
		return false;
	}
	return true;
}

float UCapsuleVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	FVector PositionToSegment = CenteredLocalPosition;
	PositionToSegment.Z -= FMath::Clamp(CenteredLocalPosition.Z, -static_cast<double>(SegmentHalfLength),
		static_cast<double>(SegmentHalfLength));
	return PositionToSegment.Length() - Radius;
}

bool UTorusVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!FMath::IsFinite(MajorRadius) || MajorRadius <= UE_SMALL_NUMBER
	    || !FMath::IsFinite(MinorRadius) || MinorRadius <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidTorusSettings",
			"Torus MajorRadius and MinorRadius must be positive and finite.");
		return false;
	}
	return true;
}

float UTorusVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	const FVector2D TubePosition(
		FVector2D(CenteredLocalPosition.X, CenteredLocalPosition.Y).Length() - MajorRadius,
		CenteredLocalPosition.Z);
	return TubePosition.Length() - MinorRadius;
}
