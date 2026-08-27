#include "VolumeSampler/ProceduralVolumeSampler.h"

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
			"DensityScale must be positive and DensityScale/DensityBias must be finite.");
		return false;
	}
	return true;
}

float UProceduralVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	return 1.0e20f;
}

float UProceduralVolumeSampler::SampleNormalized(const FVector& UVW) const
{
	const FVector LocalPosition = (UVW - FVector(0.5)) * VolumeSize;
	const float SignedDistance = GetSignedDistance(LocalPosition);
	if (!FMath::IsFinite(SignedDistance))
		return 0.0f;
	return static_cast<float>(GDualContourIsoValue) + DensityBias - SignedDistance * DensityScale;
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

float USphereVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	return LocalPosition.Length() - Radius;
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

float UBoxVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	const FVector RoundedCore = HalfExtents - FVector(CornerRadius);
	const FVector Distance = LocalPosition.GetAbs() - RoundedCore;
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

float UCylinderVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	return CappedCylinderSignedDistance(LocalPosition, Radius, HalfHeight);
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

float UCapsuleVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	FVector PositionToSegment = LocalPosition;
	PositionToSegment.Z -= FMath::Clamp(LocalPosition.Z, -static_cast<double>(SegmentHalfLength),
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

float UTorusVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	const FVector2D TubePosition(FVector2D(LocalPosition.X, LocalPosition.Y).Length() - MajorRadius, LocalPosition.Z);
	return TubePosition.Length() - MinorRadius;
}
