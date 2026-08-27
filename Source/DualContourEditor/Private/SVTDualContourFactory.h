#pragma once

#include "Factories/Factory.h"
#include "SVTDualContourFactory.generated.h"

UCLASS()
class USVTDualContourFactory : public UFactory
{
	GENERATED_BODY()

public:
	USVTDualContourFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
