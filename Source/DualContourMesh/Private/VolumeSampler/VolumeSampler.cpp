#include "VolumeSampler/VolumeSampler.h"
#include "Math/TranslationMatrix.h"

FBox UVolumeSampler::GetBounds() const
{
	if (SamplingTransform.ContainsNaN() || SamplingTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER ||
	    VolumeSize.ContainsNaN() || VolumeSize.GetMin() <= UE_SMALL_NUMBER || Pivot.ContainsNaN())
		return FBox(ForceInit);
	const FVector PivotPosition = Pivot * VolumeSize;
	FBox Bounds(ForceInit);
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector BaseVolumeCorner(
			(CornerIndex & 1) ? VolumeSize.X : 0,
			(CornerIndex & 2) ? VolumeSize.Y : 0,
			(CornerIndex & 4) ? VolumeSize.Z : 0);
		Bounds += PivotPosition + SamplingTransform.TransformPosition(BaseVolumeCorner - PivotPosition);
	}
	return Bounds;
}

FBox UVolumeSampler::TransformBoxAroundPivot(const FBox& Box, const FTransform& Transform, const FVector& PivotPosition)
{
	FBox Result(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector InputCorner(
			(Corner & 1) ? Box.Max.X : Box.Min.X,
			(Corner & 2) ? Box.Max.Y : Box.Min.Y,
			(Corner & 4) ? Box.Max.Z : Box.Min.Z);
		Result += PivotPosition + Transform.TransformPosition(InputCorner - PivotPosition);
	}
	return Result;
}

FVolumeSamplerPlacement UVolumeSampler::MakePlacement(const FTransform* SamplerToTargetTransform) const
{
	FVolumeSamplerPlacement Placement;
	const FVector PivotPosition = Pivot * VolumeSize;
	// SamplingTransform 与 SamplerToTargetTransform 都绕同一个 PivotPosition 映射（旋转/缩放），
	// 逐级逆变换（调用方 S2T⁻¹ 每样本一次 + 采样器内部 ST⁻¹ 每样本一次）可以合成单个仿射的逆：
	//   BaseVolumePosition = Pv + (ST * S2T)⁻¹(TargetLocalPosition - Pv)
	// 采样时每样本只需应用一次 Placement.TargetToSamplerLocalMatrix。
	const FMatrix SamplingMatrix = SamplingTransform.ToMatrixWithScale();
	const FTransform SamplerToTarget = SamplerToTargetTransform ? *SamplerToTargetTransform : FTransform::Identity;
	const FMatrix SamplerToTargetMatrix = SamplerToTarget.ToMatrixWithScale();
	const FMatrix ShiftFromPivot = FTranslationMatrix(-PivotPosition);
	const FMatrix ShiftToPivot = FTranslationMatrix(PivotPosition);
	// FMatrix/FVector 在 Unreal 中按行向量语义组合：v * A * B 表示先应用 A，再应用 B。
	// 正向：Base --ST--> SamplerInput --S2T--> TargetLocal，且两个变换都绕 PivotPosition 作用。
	// 因此正向矩阵为 T(-Pivot) * ST * S2T * T(+Pivot)；采样时使用其逆矩阵。
	Placement.TargetToSamplerLocalMatrix = (ShiftFromPivot * SamplingMatrix * SamplerToTargetMatrix * ShiftToPivot).Inverse();
	return Placement;
}

bool UVolumeSampler::TryGetBaseVolumePosition(
	const FVector& TargetLocalPosition, const FVolumeSamplerPlacement& Placement,
	FVector& OutBaseVolumePosition) const
{
	const FVector BaseVolumePosition = Placement.TargetToSamplerLocalMatrix.TransformPosition(TargetLocalPosition);
	OutBaseVolumePosition = BaseVolumePosition;
	return !BaseVolumePosition.ContainsNaN()
	       && BaseVolumePosition.X >= 0.0 && BaseVolumePosition.Y >= 0.0 && BaseVolumePosition.Z >= 0.0
	       && BaseVolumePosition.X <= VolumeSize.X && BaseVolumePosition.Y <= VolumeSize.Y && BaseVolumePosition.Z <= VolumeSize.Z;
}

bool UVolumeSampler::Prepare(FText& OutError) const
{
	if (VolumeSize.ContainsNaN() || Pivot.ContainsNaN() || VolumeSize.X <= UE_SMALL_NUMBER || VolumeSize.Y <= UE_SMALL_NUMBER ||
	    VolumeSize.Z <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidVolumeSize", "VolumeSize must be positive on every axis.");
		return false;
	}
	if (SamplingTransform.ContainsNaN() || SamplingTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidSamplingTransform",
			"SamplingTransform must not contain NaN and must have a non-zero scale on every axis.");
		return false;
	}
	return true;
}

void UVolumeSampler::Finish() const {}

#if WITH_EDITOR
void UVolumeSampler::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnPropertyChanged.Broadcast();
}

void UVolumeSampler::PostEditUndo()
{
	Super::PostEditUndo();
	OnPropertyChanged.Broadcast();
}
#endif
