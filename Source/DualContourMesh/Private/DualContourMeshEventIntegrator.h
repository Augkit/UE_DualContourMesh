#pragma once

#include "CoreMinimal.h"

struct FPendingDualContourMeshEvent
{
	FIntVector CellMin = FIntVector::ZeroValue;
	FIntVector CellMax = FIntVector::ZeroValue;
	bool bDensityRebuilt = false;
	bool bUpdateCollision = false;
};

/** Collects contour notifications; mesh update policy remains in the owning actor. */
class FDualContourMeshEventIntegrator
{
public:
	void AddCellsRebuilt(FIntVector CellMin, FIntVector CellMax);
	void AddMaterialsChanged(FIntVector CellMin, FIntVector CellMax);
	bool HasPending() const { return bPending; }
	bool Consume(FPendingDualContourMeshEvent& OutEvent);

private:
	bool bPending = false;
	FPendingDualContourMeshEvent PendingEvent;

	void MergeRange(FIntVector CellMin, FIntVector CellMax);
};
