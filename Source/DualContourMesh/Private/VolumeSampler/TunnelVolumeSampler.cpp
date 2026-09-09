#include "VolumeSampler/TunnelVolumeSampler.h"

namespace
{
bool IsFinitePositive(const float Value)
{
	return FMath::IsFinite(Value) && Value > UE_SMALL_NUMBER;
}

bool IsFiniteNonNegative(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f;
}

/**
 * Signed-distance-like field for a flat-bottomed arch in the YZ plane.
 * The zero contour is exact; distances around an elliptical roof are scaled
 * approximations, which is sufficient for density sampling and contour normals.
 */
float ArchCrossSectionField(const float Y, const float Z, const float FloorZ, const float HalfWidth,
	const float WallHeight, const float RoofHeight)
{
	const float SpringZ = FloorZ + WallHeight;
	const float RoofZ = FMath::Max(Z - SpringZ, 0.0f);
	const float EllipseSpaceZ = RoofZ * HalfWidth / RoofHeight;
	const float ArchField = FVector2D(Y, EllipseSpaceZ).Length() - HalfWidth;
	const float FloorField = FloorZ - Z;
	return FMath::Max(ArchField, FloorField);
}

float SmoothStep(const float Value)
{
	const float T = FMath::Clamp(Value, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}
}

UTunnelVolumeSampler::UTunnelVolumeSampler()
{
	// Leave enough sampling margin around the default shape, including its enlarged entrance.
	VolumeSize = FVector(1120.0, 600.0, 600.0);
}

bool UTunnelVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;

	if (!IsFinitePositive(Length) || !IsFinitePositive(HeadLength) || HeadLength >= Length
		|| !FMath::IsFinite(FloorZ)
		|| !IsFinitePositive(RegularHalfWidth) || !IsFiniteNonNegative(RegularWallHeight)
		|| !IsFinitePositive(RegularRoofHeight)
		|| !IsFinitePositive(EntranceHalfWidth) || !IsFiniteNonNegative(EntranceWallHeight)
		|| !IsFinitePositive(EntranceRoofHeight)
		|| !IsFiniteNonNegative(EntranceStraightLength) || !IsFinitePositive(EntranceTransitionLength))
	{
		OutError = NSLOCTEXT("TunnelVolumeSampler", "InvalidTunnelSettings",
			"Tunnel dimensions must be finite; widths, roof heights, Length, HeadLength and transition length "
			"must be positive; wall and entrance straight lengths must be non-negative; HeadLength must be "
			"shorter than Length.");
		return false;
	}

	const float StraightBodyLength = Length - HeadLength;
	if (EntranceStraightLength + EntranceTransitionLength > StraightBodyLength)
	{
		OutError = NSLOCTEXT("TunnelVolumeSampler", "EntranceLongerThanBody",
			"EntranceStraightLength plus EntranceTransitionLength must not exceed Length minus HeadLength.");
		return false;
	}

	return true;
}

float UTunnelVolumeSampler::GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const
{
	const float EntranceX = -0.5f * Length;
	const float TipX = 0.5f * Length;
	const float HeadStartX = TipX - HeadLength;

	// A cubic smoothstep gives the entrance roof a horizontal tangent at both ends of the transition.
	const float TransitionStartX = EntranceX + EntranceStraightLength;
	const float TransitionAlpha = SmoothStep(
		(CenteredLocalPosition.X - TransitionStartX) / EntranceTransitionLength);
	const float HalfWidth = FMath::Lerp(EntranceHalfWidth, RegularHalfWidth, TransitionAlpha);
	const float WallHeight = FMath::Lerp(EntranceWallHeight, RegularWallHeight, TransitionAlpha);
	const float RoofHeight = FMath::Lerp(EntranceRoofHeight, RegularRoofHeight, TransitionAlpha);

	if (CenteredLocalPosition.X <= HeadStartX)
	{
		const float CrossSection = ArchCrossSectionField(CenteredLocalPosition.Y, CenteredLocalPosition.Z,
			FloorZ, HalfWidth, WallHeight, RoofHeight);
		// The entrance is deliberately a flat cut while the other five sides remain analytic.
		return FMath::Max(CrossSection, EntranceX - CenteredLocalPosition.X);
	}

	const float HeadCenterZ = FloorZ + 0.5f * (RegularWallHeight + RegularRoofHeight);
	if (CenteredLocalPosition.X >= TipX)
	{
		return FVector(CenteredLocalPosition.X - TipX, CenteredLocalPosition.Y,
			CenteredLocalPosition.Z - HeadCenterZ).Length();
	}

	// Ellipsoidal scaling closes the entire regular arch into a rounded point. At HeadStartX
	// the scale derivative is zero, so the side view joins the straight body without a corner.
	const float HeadAlpha = (CenteredLocalPosition.X - HeadStartX) / HeadLength;
	const float CrossSectionScale = FMath::Sqrt(FMath::Max(1.0f - HeadAlpha * HeadAlpha, 0.0f));
	if (CrossSectionScale <= UE_SMALL_NUMBER)
	{
		return FVector(CenteredLocalPosition.X - TipX, CenteredLocalPosition.Y,
			CenteredLocalPosition.Z - HeadCenterZ).Length();
	}

	const float ScaledY = CenteredLocalPosition.Y / CrossSectionScale;
	const float ScaledZ = HeadCenterZ + (CenteredLocalPosition.Z - HeadCenterZ) / CrossSectionScale;
	return ArchCrossSectionField(ScaledY, ScaledZ, FloorZ, RegularHalfWidth, RegularWallHeight,
		RegularRoofHeight) * CrossSectionScale;
}
