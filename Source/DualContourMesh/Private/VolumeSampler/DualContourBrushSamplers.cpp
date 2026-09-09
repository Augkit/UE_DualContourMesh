#include "VolumeSampler/DualContourBrushSamplers.h"
#include "DualContour.h"

float UDualContourShapeVolumeSampler::EvaluateFalloff(float Distance, float Falloff, EDualContourEditFalloff Type)
{
	if (Distance >= 1.0f)
		return 0.0f;
	const float Inner = 1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f);
	if (Distance <= Inner || Falloff <= UE_SMALL_NUMBER)
		return 1.0f;
	const float T = FMath::Clamp((Distance - Inner) / FMath::Max(Falloff, UE_SMALL_NUMBER), 0.0f, 1.0f);
	switch (Type)
	{
		case EDualContourEditFalloff::Linear:
			return 1.0f - T;
		case EDualContourEditFalloff::Spherical:
			return FMath::Sqrt(FMath::Max(0.0f, 1.0f - T * T));
		case EDualContourEditFalloff::Tip:
			return FMath::Square(1.0f - T);
		default:
			return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
	}
}

FBox UDualContourShapeVolumeSampler::GetBounds() const
{
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER || TargetLocalCenter.ContainsNaN())
		return FBox(ForceInit);
	// A rotated directional box/cylinder can extend beyond Radius along a target axis.
	const FVector Extent(Radius * (bDirectional ? FMath::Sqrt(3.0f) : 1.0f));
	return FBox(TargetLocalCenter - Extent, TargetLocalCenter + Extent);
}

bool UDualContourShapeVolumeSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement&,
	float& Value, float& Weight) const
{
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
		return false;
	const FVector CenterToSample = TargetLocalPosition - TargetLocalCenter;
	float Distance;
	if (bDirectional)
	{
		const FVector BrushAxis = TargetLocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const float AxialDistance = FVector::DotProduct(CenterToSample, BrushAxis);
		const FVector PlanarOffset = CenterToSample - BrushAxis * AxialDistance;
		FVector PlanarAxisX, PlanarAxisY;
		BrushAxis.FindBestAxisVectors(PlanarAxisX, PlanarAxisY);
		Distance =
			bBox
				? FMath::Max(FMath::Abs(FVector::DotProduct(PlanarOffset, PlanarAxisX)),
					FMath::Abs(FVector::DotProduct(PlanarOffset, PlanarAxisY)))
				: PlanarOffset.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
		if (FMath::Abs(AxialDistance) > Radius * Weight)
			return false;
	}
	else
	{
		Distance = bBox
			           ? FMath::Max3(FMath::Abs(CenterToSample.X), FMath::Abs(CenterToSample.Y), FMath::Abs(CenterToSample.Z))
			           : CenterToSample.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
	}
	Value = GDualContourMaxLinearDensity * (1.0f - Distance / Radius);
	return Weight > 0.0f;
}

bool UDualContourPlaneVolumeSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
	float& Value, float& Weight) const
{
	if (!Mask || !Mask->Sample(TargetLocalPosition, Placement, Value, Weight))
		return false;
	Value = -FVector::DotProduct(TargetLocalPosition - TargetLocalOrigin, TargetLocalNormal) * DensityScale;
	return true;
}

void UDualContourVolumeBrushSampler::Initialize(const UDualContour& InSource, const FTransform& InSourceToTargetTransform)
{
	Source = &InSource;
	SourceToTargetTransform = InSourceToTargetTransform;
}

FBox UDualContourVolumeBrushSampler::GetBounds() const
{
	const UDualContour* SourceDualContour = Source.Get();
	if (!SourceDualContour || !SourceDualContour->HasCurrentGeneratedData() || SourceToTargetTransform.ContainsNaN() ||
	    SourceToTargetTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER)
		return FBox(ForceInit);
	const FVector SourceLocalExtent = FVector(SourceDualContour->CellCount) * SourceDualContour->CellSize;
	return FBox(FVector::ZeroVector, SourceLocalExtent).TransformBy(SourceToTargetTransform);
}

bool UDualContourVolumeBrushSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement&,
	float& Value, float& Weight) const
{
	const UDualContour* SourceDualContour = Source.Get();
	if (!SourceDualContour || !SourceDualContour->HasCurrentGeneratedData())
		return false;
	const FVector SourceLocalPosition = SourceToTargetTransform.InverseTransformPosition(TargetLocalPosition);
	const FVector SourceGridPosition = SourceLocalPosition / SourceDualContour->CellSize;
	if (SourceGridPosition.X < 0 || SourceGridPosition.Y < 0 || SourceGridPosition.Z < 0
	    || SourceGridPosition.X > SourceDualContour->CellCount.X
	    || SourceGridPosition.Y > SourceDualContour->CellCount.Y
	    || SourceGridPosition.Z > SourceDualContour->CellCount.Z)
		return false;
	Value = SourceDualContour->GetTrilinearDensity(SourceGridPosition);
	Weight = 1.0f;
	return true;
}

void UDualContourRestoreVolumeSampler::Initialize(UVolumeSampler& InMask, const UDualContour& InSource)
{
	Mask = &InMask;
	Source = &InSource;
}

FBox UDualContourRestoreVolumeSampler::GetBounds() const
{
	return Mask && Source.IsValid() && Source->HasCurrentGeneratedData() ? Mask->GetBounds() : FBox(ForceInit);
}

bool UDualContourRestoreVolumeSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
	float& Value, float& Weight) const
{
	if (!Mask || !Source.IsValid() || !Source->HasCurrentGeneratedData()
	    || !Mask->Sample(TargetLocalPosition, Placement, Value, Weight))
		return false;
	const FVector SourceGridPosition = TargetLocalPosition / Source->CellSize;
	Value = Source->GetLinearDensity(
		FMath::RoundToInt(SourceGridPosition.X),
		FMath::RoundToInt(SourceGridPosition.Y),
		FMath::RoundToInt(SourceGridPosition.Z));
	return true;
}
