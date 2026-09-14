#include "VolumeSampledDualContour.h"

#if WITH_EDITOR
#include "VolumeSampler/VolumeSampler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/ObjectSaveContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogVolumeSampledDualContour, Log, All);

bool UVolumeSampledDualContour::SampleSource()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampledDualContour_SampleVolume);
	if (!VolumeSampler)
		return false;
	FText Error;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampledDualContour_Modify);
		Modify();
	}
	const FVector SamplingVolumeSize = FVector(CellCount) * CellSize;
	const FVector SamplerPivotPosition = VolumeSampler->Pivot * SamplingVolumeSize;
	FTransform SamplerPivotTransform = SampleTransform;
	SamplerPivotTransform.AddToTranslation(SamplerPivotPosition);
	if (!ApplySampler(*VolumeSampler, SamplingVolumeSize, SamplerPivotTransform, Error))
	{
		UE_LOG(LogVolumeSampledDualContour, Error, TEXT("Volume sampling failed for %s: %s"), *GetPathName(), *Error.ToString());
		return false;
	}

	++GenerationRevision;
	MarkPackageDirty();
	PostEditChange();
	return true;
}

void UVolumeSampledDualContour::PostLoad()
{
	Super::PostLoad();
	BindVolumeSampler();
}

void UVolumeSampledDualContour::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	BindVolumeSampler();
}

void UVolumeSampledDualContour::BindVolumeSampler()
{
	if (BoundVolumeSampler.Get() == VolumeSampler && VolumeSamplerChangedHandle.IsValid())
		return;

	if (UVolumeSampler* PreviousSampler = BoundVolumeSampler.Get())
		PreviousSampler->OnPropertyChanged.Remove(VolumeSamplerChangedHandle);

	BoundVolumeSampler = VolumeSampler;
	VolumeSamplerChangedHandle.Reset();
	if (VolumeSampler)
	{
		VolumeSamplerChangedHandle = VolumeSampler->OnPropertyChanged.AddUObject(this, &UVolumeSampledDualContour::HandleSamplerPropertyChanged);
	}
}

void UVolumeSampledDualContour::HandleSamplerPropertyChanged()
{
	if (IsTemplate())
		return;

	bRebuildRequired = true;
	MarkPackageDirty();
}

void UVolumeSampledDualContour::PreSave(FObjectPreSaveContext SaveContext)
{
	if (!IsTemplate() && VolumeSampler && bRebuildRequired)
	{
		SampleSource();
	}

	Super::PreSave(SaveContext);
}

void UVolumeSampledDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, VolumeSampler))
		BindVolumeSampler();

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, VolumeSampler)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, SampleTransform)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellCount)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellSize))
	{
		bRebuildRequired = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UVolumeSampledDualContour::PostEditUndo()
{
	Super::PostEditUndo();
	BindVolumeSampler();
}
#endif
