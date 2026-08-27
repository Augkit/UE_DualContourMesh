#include "SVTDualContourFactory.h"
#include "SVTDualContour.h"

USVTDualContourFactory::USVTDualContourFactory()
{
	SupportedClass = USVTDualContour::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USVTDualContourFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USVTDualContour>(InParent, Class, Name, Flags | RF_Transactional);
}
