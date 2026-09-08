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
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER || Center.ContainsNaN())
		return FBox(ForceInit);
	// A rotated directional box/cylinder can extend beyond Radius along a target axis.
	const FVector Extent(Radius * (bDirectional ? FMath::Sqrt(3.0f) : 1.0f));
	return FBox(Center - Extent, Center + Extent);
}

bool UDualContourShapeVolumeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
		return false;
	const FVector Offset = Position - Center;
	float Distance;
	if (bDirectional)
	{
		const FVector Axis = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const float Axial = FVector::DotProduct(Offset, Axis);
		const FVector Planar = Offset - Axis * Axial;
		FVector X, Y;
		Axis.FindBestAxisVectors(X, Y);
		Distance =
		    bBox ? FMath::Max(FMath::Abs(FVector::DotProduct(Planar, X)), FMath::Abs(FVector::DotProduct(Planar, Y))) : Planar.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
		if (FMath::Abs(Axial) > Radius * Weight)
			return false;
	}
	else
	{
		Distance = bBox ? FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z)) : Offset.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
	}
	Value = GDualContourMaxLinearDensity * (1.0f - Distance / Radius);
	return Weight > 0.0f;
}

bool UDualContourPlaneVolumeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!Mask || !Mask->Sample(Position, Value, Weight))
		return false;
	Value = -FVector::DotProduct(Position - Origin, Normal) * Scale;
	return true;
}

void UDualContourVolumeBrushSampler::Initialize(const UDualContour& InSource, const FTransform& InSourceToTarget)
{
	Source = &InSource;
	SourceToTarget = InSourceToTarget;
}

FBox UDualContourVolumeBrushSampler::GetBounds() const
{
	const UDualContour* Grid = Source.Get();
	if (!Grid || !Grid->HasCurrentGeneratedData() || SourceToTarget.ContainsNaN() ||
	    SourceToTarget.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER)
		return FBox(ForceInit);
	return FBox(FVector::ZeroVector, FVector(Grid->CellCount) * Grid->CellSize).TransformBy(SourceToTarget);
}

bool UDualContourVolumeBrushSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	const UDualContour* Grid = Source.Get();
	if (!Grid || !Grid->HasCurrentGeneratedData())
		return false;
	const FVector P = SourceToTarget.InverseTransformPosition(Position) / Grid->CellSize;
	if (P.X < 0 || P.Y < 0 || P.Z < 0 || P.X > Grid->CellCount.X || P.Y > Grid->CellCount.Y || P.Z > Grid->CellCount.Z)
		return false;
	Value = Grid->TrilinearDensity(P);
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

bool UDualContourRestoreVolumeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!Mask || !Source.IsValid() || !Source->HasCurrentGeneratedData() || !Mask->Sample(Position, Value, Weight))
		return false;
	const FVector Grid = Position / Source->CellSize;
	Value = Source->GetLinearDensity(FMath::RoundToInt(Grid.X), FMath::RoundToInt(Grid.Y), FMath::RoundToInt(Grid.Z));
	return true;
}
