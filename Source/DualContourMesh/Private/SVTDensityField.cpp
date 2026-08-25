#include "SVTDensityField.h"

#include "DualContour.h"

#if WITH_EDITOR
#include "SVTDensityFieldSampler.h"
#endif

USVTDensityField::USVTDensityField()
{
	DualContour = CreateDefaultSubobject<UDualContour>(TEXT("DualContour"));
}

#if WITH_EDITOR
bool USVTDensityField::SampleSparseVolumeTexture()
{
	if (!DualContour || !SourceSparseVolumeTexture)
		return false;

	TArray<uint8> Samples;
	FText Error;
	if (!FSVTDensityFieldSampler::Sample(*this, Samples, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("SVT density sampling failed for %s: %s"), *GetPathName(), *Error.ToString());
		return false;
	}

	Modify();
	DualContour->Modify();
	if (!DualContour->SetDensitySamples(Samples))
		return false;

	++GenerationRevision;
	MarkPackageDirty();
	PostEditChange();
	return true;
}
#endif
