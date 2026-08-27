#pragma once

#include "Factories/Factory.h"
#include "VolumeSampledDualContourFactory.generated.h"

UCLASS()
class UVolumeSampledDualContourFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVolumeSampledDualContourFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
};
