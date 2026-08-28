#include "VolumeSampledDualContour.h"

#if WITH_EDITOR
#include "VolumeSampler/VolumeSampler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogVolumeSampledDualContour, Log, All);

bool UVolumeSampledDualContour::SampleVolume()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VolumeSampledDualContour_SampleVolume);
	if (!VolumeSampler)
		return false;

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

void UVolumeSampledDualContour::NotifySamplerChanged()
{
	bRebuildRequired = true;
	MarkPackageDirty();
}

void UVolumeSampledDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, VolumeSampler)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVolumeSampledDualContour, SampleTransform))
	{
		bRebuildRequired = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
