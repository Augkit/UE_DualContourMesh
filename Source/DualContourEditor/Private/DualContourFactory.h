#pragma once

#include "Factories/Factory.h"
#include "DualContourFactory.generated.h"

UCLASS()
class UDualContourFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDualContourFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
