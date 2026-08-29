#pragma once

#include "CoreMinimal.h"

/** Plain CPU mesh data produced by the dual-contour mesh builder. */
struct DUALCONTOURMESH_API FDualContourMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2f> UVs;
	TArray<uint32> Indices;
	FBox LocalBounds = FBox(ForceInit);

	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
		UVs.Reset();
		Indices.Reset();
		LocalBounds = FBox(ForceInit);
	}
};
