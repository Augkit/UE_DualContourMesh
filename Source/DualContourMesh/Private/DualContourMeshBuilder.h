#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"

class UDualContour;

/** Builds one mesh division from immutable dual-contour data without touching scene components. */
class FDualContourMeshBuilder
{
public:
	/** Builds with neutral 1x1 UV tiling for callers that do not own render settings. */
	static void Build(const UDualContour& DualContour, FIntVector CellRangeMin, FIntVector CellRangeMax, FDualContourMeshData& OutMeshData);
	static void Build(const UDualContour& DualContour, const FVector2D& UVTiling, FIntVector CellRangeMin, FIntVector CellRangeMax,
		FDualContourMeshData& OutMeshData);
};
