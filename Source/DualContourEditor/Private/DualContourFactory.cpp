#include "DualContourFactory.h"

#include "DualContour.h"

UDualContourFactory::UDualContourFactory()
{
	SupportedClass = UDualContour::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UDualContourFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UDualContour>(InParent, Class, Name, Flags | RF_Transactional);
}
