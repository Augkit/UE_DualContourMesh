#pragma once

#include "Factories/Factory.h"
#include "SVTDensityFieldFactory.generated.h"

UCLASS()
class USVTDensityFieldFactory : public UFactory
{
	GENERATED_BODY()

public:
	USVTDensityFieldFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
