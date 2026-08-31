#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"

class UDualContour;

/** Builds one mesh division from immutable dual-contour data without touching scene components. */
class FDualContourMeshBuilder
{
public:
	static void Build(const UDualContour& DualContour, FIntVector CellRangeMin, FIntVector CellRangeMax, FDualContourMeshData& OutMeshData);
};
