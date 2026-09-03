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
	UpdateAutomaticVolumeSize();

	FText Error;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampledDualContour_Modify);
		Modify();
	}
	if (!VolumeSampler->ReplaceDualContour(this, SampleTransform, Error))
	{
		UE_LOG(LogVolumeSampledDualContour, Error, TEXT("Volume sampling failed for %s: %s"),
			*GetPathName(), *Error.ToString());
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

	UpdateAutomaticVolumeSize();
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

void UVolumeSampledDualContour::UpdateAutomaticVolumeSize()
{
	if (!bAutoCalculateVolumeSize || !VolumeSampler)
		return;

	const FVector AutomaticVolumeSize(
		static_cast<double>(CellCount.X) * CellSize,
		static_cast<double>(CellCount.Y) * CellSize,
		static_cast<double>(CellCount.Z) * CellSize);
	if (VolumeSampler->VolumeSize != AutomaticVolumeSize)
	{
		VolumeSampler->Modify();
		VolumeSampler->VolumeSize = AutomaticVolumeSize;
	}
}

void UVolumeSampledDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, VolumeSampler))
		BindVolumeSampler();

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, VolumeSampler)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, bAutoCalculateVolumeSize)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, SampleTransform)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellCount)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellSize))
	{
		UpdateAutomaticVolumeSize();
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
