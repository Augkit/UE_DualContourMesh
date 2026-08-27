#include "VolumeSampledDualContourFactory.h"

#include "VolumeSampledDualContour.h"

UVolumeSampledDualContourFactory::UVolumeSampledDualContourFactory()
{
	SupportedClass = UVolumeSampledDualContour::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVolumeSampledDualContourFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVolumeSampledDualContour>(InParent, Class, Name, Flags | RF_Transactional);
}
