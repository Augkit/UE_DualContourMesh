#include "EditMode/Editing/DualContourBrushOperations.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "EditMode/Editing/DualContourMaterialRegionSampler.h"
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
	Sampler.Radius = Stamp.BrushSize * 0.5f;
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
	return FMath::IsFinite(Stamp.BrushSize) && Stamp.BrushSize > UE_SMALL_NUMBER
	       && Edit.ApplyMaterial(Sampler, FVector(Stamp.BrushSize), PaintId, Threshold, bSolidSamplesOnly);
}

bool DualContourBrushOperations::ApplyDensityStamp(FDualContourEditContext& Edit, const UDualContour* RestoreSource,
	const FDualContourBrushStamp& Stamp)
{
	if (!Edit.IsOpen() || !FMath::IsFinite(Stamp.Strength) || !FMath::IsFinite(Stamp.TimeScale)
	    || !FMath::IsFinite(Stamp.BrushSize) || Stamp.BrushSize <= UE_SMALL_NUMBER)
		return false;
	auto MaskOwner = MakeShapeSampler(Stamp);
	auto& Mask = *MaskOwner;
	const UDualContour* Target = Edit.GetTarget();
	if (!Target)
		return false;
	const float Strength = FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	const bool bSculpt =
		Stamp.Operation == EDualContourDensityEditOperation::Sculpt || Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract;
	Mask.bDirectional &= bSculpt;
	if (Stamp.Operation == EDualContourDensityEditOperation::StampUnion ||
	    Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
	{
		if (!IsValid(Stamp.VolumeSampler))
			return false;
		return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::StampUnion
			                         ? EDualContourDensityOperation::Union
			                         : EDualContourDensityOperation::Difference,
			*Stamp.VolumeSampler, FVector(Stamp.BrushSize), Stamp.SourceToTargetTransform);
	}
	const FVector SamplingVolumeSize(Stamp.BrushSize);
	if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
		return Edit.ApplyDensity(EDualContourDensityOperation::Smooth, Mask, SamplingVolumeSize, Strength);
	if (Stamp.Operation == EDualContourDensityEditOperation::Erase)
	{
		if (!IsValid(RestoreSource) || !RestoreSource->HasCurrentGeneratedData() ||
		    RestoreSource->GetSampleDimensions() != Target->GetSampleDimensions() ||
		    !FMath::IsNearlyEqual(RestoreSource->CellSize, Target->CellSize))
			return false;
		TStrongObjectPtr<UDualContourRestoreVolumeSampler> SamplerOwner(NewObject<UDualContourRestoreVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, *RestoreSource);
		return Edit.ApplyDensity(EDualContourDensityOperation::Replace, Sampler, SamplingVolumeSize, Strength);
	}
	if (Stamp.Operation == EDualContourDensityEditOperation::Flatten)
	{
		TStrongObjectPtr<UDualContourPlaneVolumeSampler> SamplerOwner(NewObject<UDualContourPlaneVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, Stamp.TargetLocalFlattenPlaneOrigin, Stamp.TargetLocalFlattenPlaneNormal,
			GDualContourLinearDensityFixedPointScale);
		return Edit.ApplyDensity(EDualContourDensityOperation::Replace, Sampler, SamplingVolumeSize, Strength);
	}
	if (bSculpt && Stamp.bUseClayBrush)
	{
		TStrongObjectPtr<UDualContourPlaneVolumeSampler> SamplerOwner(NewObject<UDualContourPlaneVolumeSampler>());
		auto& Sampler = *SamplerOwner;
		Sampler.Initialize(Mask, Stamp.TargetLocalClayPlaneOrigin, Stamp.TargetLocalNormal,
			GDualContourMaxLinearDensity / Edit.GetTarget()->CellSize);
		return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::Sculpt
			                         ? EDualContourDensityOperation::Union
			                         : EDualContourDensityOperation::Difference, Sampler, SamplingVolumeSize, Strength);
	}
	return Edit.ApplyDensity(Stamp.Operation == EDualContourDensityEditOperation::Sculpt
		                         ? EDualContourDensityOperation::Add
		                         : EDualContourDensityOperation::Subtract, Mask, SamplingVolumeSize, Strength);
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
		const FVector SamplingVolumeSize = Sampler.GetSamplingBounds(FVector::OneVector).GetSize().ComponentMax(FVector(UE_SMALL_NUMBER));
		Edit.ApplyMaterial(Sampler, SamplingVolumeSize, PaintId, 0.5f, true);
	}
}

FBox UDualContourMaterialRegionSampler::GetSamplingBounds(const FVector& SamplingVolumeSize) const
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
