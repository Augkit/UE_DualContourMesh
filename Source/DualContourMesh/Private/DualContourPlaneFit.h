#pragma once

#include "CoreMinimal.h"

class UDualContour;

namespace DualContourPlaneFit
{
// Replaces constraints only when local planes explain the quantized field.
bool Fit(const UDualContour& Grid, FIntVector Cell, double Matrix[3][3], double RHS[3],
		int32& ConstraintCount, FVector& OutNormal);
}
