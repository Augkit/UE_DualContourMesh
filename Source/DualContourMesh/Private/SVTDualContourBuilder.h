#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

class USVTDualContour;

class FSVTDualContourBuilder
{
public:
	static bool Sample(const USVTDualContour& SVTDualContour, TArray<uint8>& OutSamples, FText& OutError);
};

#endif
