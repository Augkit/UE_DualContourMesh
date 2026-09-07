#include "EditMode/Editing/DualContourBrushOperations.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "VolumeSampledDualContour.h"
#include "DualContourEditContext.h"

namespace
{
class FMaterialVolumeSampler final : public FDualContourEditSampler
{
public:
	FMaterialVolumeSampler(ADualContourMaterialBrushVolume& InVolume, const FTransform& InTargetTransform)
		: Volume(InVolume), TargetTransform(InTargetTransform) {}

	virtual FBox GetBounds() const override
	{
		return Volume.GetBrushWorldBounds().TransformBy(TargetTransform.Inverse());
	}

	virtual bool Sample(const FVector& Position, float& Value, float& Weight) const override
	{
		Value = 0;
		Weight = 1;
		return Volume.EncompassesWorldPosition(TargetTransform.TransformPosition(Position));
	}

private:
	ADualContourMaterialBrushVolume& Volume;
	FTransform TargetTransform;
};

FDualContourShapeSampler MakeShapeSampler(const FDualContourBrushStamp& Stamp)
{
	FDualContourShapeSampler Sampler;
	Sampler.Center = Stamp.LocalCenter;
	Sampler.Normal = Stamp.LocalNormal;
	Sampler.Radius = Stamp.Radius;
	Sampler.Falloff = Stamp.Falloff;
	Sampler.FalloffType = static_cast<EDualContourEditFalloff>(Stamp.FalloffType);
	Sampler.bBox = Stamp.Shape == EDualContourBrushShape::Box;
	Sampler.bDirectional = Stamp.bUseDirectionalFalloff;
	return Sampler;
}

} // namespace

float DualContourBrushOperations::EvaluateFalloff(float Distance, float Falloff, EDualContourBrushFalloff Type)
{
	return FDualContourShapeSampler::EvaluateFalloff(Distance, Falloff, static_cast<EDualContourEditFalloff>(Type));
}

bool DualContourBrushOperations::ApplyMaterialStamp(FDualContourEditContext& Edit, const FDualContourBrushStamp& Stamp, uint8 PaintId,
	float Threshold, bool bSolidSamplesOnly)
{
	FDualContourShapeSampler Sampler = MakeShapeSampler(Stamp);
	Sampler.bDirectional = false;
	return Edit.ApplyMaterial(Sampler, PaintId, Threshold, bSolidSamplesOnly);
}

bool DualContourBrushOperations::ApplyDensityStamp(FDualContourEditContext& Edit, const UDualContour* RestoreSource,
	const FDualContourBrushStamp& Stamp)
{
	if (!Edit.IsOpen() || !FMath::IsFinite(Stamp.Strength) || !FMath::IsFinite(Stamp.TimeScale))
		return false;
	FDualContourShapeSampler Mask = MakeShapeSampler(Stamp);
	const float Strength = FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	const bool bSculpt =
		Stamp.Operation == EDualContourDensityEditOperation::Sculpt || Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract;
	Mask.bDirectional &= bSculpt;
	if (Stamp.Operation == EDualContourDensityEditOperation::StampUnion ||
	    Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
	{
		if (!IsValid(Stamp.VolumeBrush))
			return false;
		FDualContourVolumeSampler Sampler(*Stamp.VolumeBrush, Stamp.VolumeToTarget);
		return Edit.ApplyDensity(Sampler, Stamp.Operation == EDualContourDensityEditOperation::StampUnion
			                                  ? EDualContourEditOperation::Union
			                                  : EDualContourEditOperation::Difference);
	}
	if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
		return Edit.ApplyDensity(Mask, EDualContourEditOperation::Smooth, Strength);
	if (Stamp.Operation == EDualContourDensityEditOperation::Erase)
	{
		const UDualContour* Target = Edit.GetTarget();
		if (!IsValid(RestoreSource) || !RestoreSource->HasCurrentGeneratedData() ||
		    RestoreSource->GetSampleDimensions() != Target->GetSampleDimensions() ||
		    !FMath::IsNearlyEqual(RestoreSource->CellSize, Target->CellSize))
			return false;
		FDualContourRestoreSampler Sampler(Mask, *RestoreSource);
		return Edit.ApplyDensity(Sampler, EDualContourEditOperation::Replace, Strength);
	}
	if (Stamp.Operation == EDualContourDensityEditOperation::Flatten)
	{
		FDualContourPlaneSampler Sampler(Mask, Stamp.FlattenPlaneOrigin, Stamp.FlattenPlaneNormal,
			GDualContourLinearDensityFixedPointScale);
		return Edit.ApplyDensity(Sampler, EDualContourEditOperation::Replace, Strength);
	}
	if (bSculpt && Stamp.bUseClayBrush)
	{
		FDualContourPlaneSampler Sampler(Mask, Stamp.ClayPlaneOrigin, Stamp.LocalNormal,
			GDualContourMaxLinearDensity / Edit.GetTarget()->CellSize);
		return Edit.ApplyDensity(Sampler,
			Stamp.Operation == EDualContourDensityEditOperation::Sculpt
				? EDualContourEditOperation::Union
				: EDualContourEditOperation::Difference,
			Strength);
	}
	return Edit.ApplyDensity(Mask,
		Stamp.Operation == EDualContourDensityEditOperation::Sculpt
			? EDualContourEditOperation::Add
			: EDualContourEditOperation::Subtract,
		Strength);
}

void DualContourBrushOperations::ApplyMaterialVolumes(ADualContourMeshActor* TargetActor, FDualContourEditContext& Edit,
	TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes, uint8 PaintId)
{
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (!IsValid(DualContour) || !Edit.IsOpen() || Edit.GetTarget() != DualContour || !DualContour->HasCurrentGeneratedData())
		return;

	for (ADualContourMaterialBrushVolume* Volume : BrushVolumes)
	{
		if (!IsValid(Volume) || Volume->TargetActor != TargetActor)
			continue;
		Volume->CacheBrushGeometry();
		if (!Volume->GetBrushWorldBounds().IsValid)
			continue;
		FMaterialVolumeSampler Sampler(*Volume, TargetActor->GetActorTransform());
		Edit.ApplyMaterial(Sampler, PaintId, 0.5f, true);
	}
}
