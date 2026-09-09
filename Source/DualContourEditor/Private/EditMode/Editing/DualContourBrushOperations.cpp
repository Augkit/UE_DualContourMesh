#include "EditMode/Editing/DualContourBrushOperations.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "EditMode/Editing/DualContourMaterialRegionSampler.h"
#include "VolumeSampledDualContour.h"
#include "DualContourEditContext.h"
#include "VolumeSampler/DualContourBrushSamplers.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TStrongObjectPtr<UDualContourShapeVolumeSampler> MakeShapeSampler(const FDualContourBrushStamp& Stamp)
{
	TStrongObjectPtr<UDualContourShapeVolumeSampler> SamplerOwner(NewObject<UDualContourShapeVolumeSampler>());
	auto& Sampler = *SamplerOwner;
	Sampler.TargetLocalCenter = Stamp.TargetLocalCenter;
	Sampler.TargetLocalNormal = Stamp.TargetLocalNormal;
	Sampler.Radius = Stamp.Radius;
	Sampler.Falloff = Stamp.Falloff;
	Sampler.FalloffType = static_cast<EDualContourEditFalloff>(Stamp.FalloffType);
	Sampler.bBox = Stamp.Shape == EDualContourBrushShape::Box;
	Sampler.bDirectional = Stamp.bUseDirectionalFalloff;
	return SamplerOwner;
}

} // namespace

float DualContourBrushOperations::EvaluateFalloff(float Distance, float Falloff, EDualContourBrushFalloff Type)
{
	return UDualContourShapeVolumeSampler::EvaluateFalloff(Distance, Falloff, static_cast<EDualContourEditFalloff>(Type));
}

bool DualContourBrushOperations::ApplyMaterialStamp(FDualContourEditContext& Edit, const FDualContourBrushStamp& Stamp, uint8 PaintId,
	float Threshold, bool bSolidSamplesOnly)
{
	auto SamplerOwner = MakeShapeSampler(Stamp);
	auto& Sampler = *SamplerOwner;
	Sampler.bDirectional = false;
	return Edit.ApplyMaterial(Sampler, PaintId, Threshold, bSolidSamplesOnly);
}

bool DualContourBrushOperations::ApplyDensityStamp(FDualContourEditContext& Edit, const UDualContour* RestoreSource,
	const FDualContourBrushStamp& Stamp)
{
	if (!Edit.IsOpen() || !FMath::IsFinite(Stamp.Strength) || !FMath::IsFinite(Stamp.TimeScale))
		return false;
	auto MaskOwner = MakeShapeSampler(Stamp);
	auto& Mask = *MaskOwner;
	const float Strength = FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	const bool bSculpt =
		Stamp.Operation == EDualContourDensityEditOperation::Sculpt || Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract;
	Mask.bDirectional &= bSculpt;
	if (Stamp.Operation == EDualContourDensityEditOperation::StampUnion ||
	    Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
	{
		if (!IsValid(Stamp.VolumeBrush))
			return false;
		TStrongObjectPtr<UDualContourVolumeBrushSampler> SamplerOwner(NewObject<UDualContourVolumeBrushSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(*Stamp.VolumeBrush, Stamp.SourceToTargetTransform);
		return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::StampUnion
			                         ? EDualContourDensityOperation::Union
			                         : EDualContourDensityOperation::Difference, Sampler);
	}
	if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
		return Edit.ApplyDensity(EDualContourDensityOperation::Smooth, Mask, Strength);
	if (Stamp.Operation == EDualContourDensityEditOperation::Erase)
	{
		const UDualContour* Target = Edit.GetTarget();
		if (!IsValid(RestoreSource) || !RestoreSource->HasCurrentGeneratedData() ||
		    RestoreSource->GetSampleDimensions() != Target->GetSampleDimensions() ||
		    !FMath::IsNearlyEqual(RestoreSource->CellSize, Target->CellSize))
			return false;
		TStrongObjectPtr<UDualContourRestoreVolumeSampler> SamplerOwner(NewObject<UDualContourRestoreVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, *RestoreSource);
		return Edit.ApplyDensity(EDualContourDensityOperation::Replace, Sampler, Strength);
	}
	if (Stamp.Operation == EDualContourDensityEditOperation::Flatten)
	{
		TStrongObjectPtr<UDualContourPlaneVolumeSampler> SamplerOwner(NewObject<UDualContourPlaneVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, Stamp.TargetLocalFlattenPlaneOrigin, Stamp.TargetLocalFlattenPlaneNormal,
			GDualContourLinearDensityFixedPointScale);
		return Edit.ApplyDensity(EDualContourDensityOperation::Replace, Sampler, Strength);
	}
	if (bSculpt && Stamp.bUseClayBrush)
	{
		TStrongObjectPtr<UDualContourPlaneVolumeSampler> SamplerOwner(NewObject<UDualContourPlaneVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, Stamp.TargetLocalClayPlaneOrigin, Stamp.TargetLocalNormal,
			GDualContourMaxLinearDensity / Edit.GetTarget()->CellSize);
		return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::Sculpt
			                         ? EDualContourDensityOperation::Union
			                         : EDualContourDensityOperation::Difference, Sampler, Strength);
	}
	return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::Sculpt
		                         ? EDualContourDensityOperation::Add
		                         : EDualContourDensityOperation::Subtract, Mask, Strength);
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
		TStrongObjectPtr<UDualContourMaterialRegionSampler> SamplerOwner(NewObject<UDualContourMaterialRegionSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Volume = Volume;
		Sampler.TargetLocalToWorldTransform = TargetActor->GetActorTransform();
		Edit.ApplyMaterial(Sampler, PaintId, 0.5f, true);
	}
}

FBox UDualContourMaterialRegionSampler::GetBounds() const
{
	return Volume.IsValid()
		       ? Volume->GetBrushWorldBounds().TransformBy(TargetLocalToWorldTransform.Inverse())
		       : FBox(ForceInit);
}

bool UDualContourMaterialRegionSampler::Sample(const FVector& TargetLocalPosition, const FVolumeSamplerPlacement&,
	float& Value, float& Weight) const
{
	Value = 0;
	Weight = 1;
	const FVector WorldPosition = TargetLocalToWorldTransform.TransformPosition(TargetLocalPosition);
	return Volume.IsValid() && Volume->EncompassesWorldPosition(WorldPosition);
}
