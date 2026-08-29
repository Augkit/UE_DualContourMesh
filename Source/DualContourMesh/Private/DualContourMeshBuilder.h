#pragma once

#include "CoreMinimal.h"
#include "DualContourMeshData.h"

class UDualContour;

/** Builds one mesh division from immutable dual-contour data without touching scene components. */
class FDualContourMeshBuilder
{
public:
	static void Build(const UDualContour& DualContour, FVectorInt CellRangeMin, FVectorInt CellRangeMax,
		FDualContourMeshData& OutMeshData);
};
