#include "SVTDensityFieldFactory.h"
#include "SVTDensityField.h"

USVTDensityFieldFactory::USVTDensityFieldFactory()
{
	SupportedClass = USVTDensityField::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USVTDensityFieldFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USVTDensityField>(InParent, Class, Name, Flags | RF_Transactional);
}
