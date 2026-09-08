#include "SVTDualContour.h"

#if WITH_EDITOR
#endif

#if WITH_EDITOR
bool USVTDualContour::SampleSource()
{
	if (!SourceSparseVolumeTexture)
		return false;

	Modify();
	FText Error;
	if (!Sample(Error))
	{
		UE_LOG(LogTemp, Error, TEXT("SVT dual-contour sampling failed for %s: %s"), *GetPathName(), *Error.ToString());
		return false;
	}

	++GenerationRevision;
	MarkPackageDirty();
	PostEditChange();
	return true;
}

void USVTDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(USVTDualContour, SourceSparseVolumeTexture)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(USVTDualContour, Fit)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(USVTDualContour, DensityAttribute)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(USVTDualContour, DensityScale)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(USVTDualContour, DensityBias))
	{
		bRebuildRequired = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
