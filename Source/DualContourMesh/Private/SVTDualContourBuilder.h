#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "DualContourTypes.h"

class USVTDualContour;

class FSVTDualContourBuilder
{
public:
	static bool Sample(const USVTDualContour& SVTDualContour, FDualContourSampledRegion& OutRegion, FText& OutError);
};

#endif
