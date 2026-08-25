#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

class USVTDensityField;

class FSVTDensityFieldSampler
{
public:
	static bool Sample(const USVTDensityField& DensityField, TArray<uint8>& OutSamples, FText& OutError);
};

#endif
