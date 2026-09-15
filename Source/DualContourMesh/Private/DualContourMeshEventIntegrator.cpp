#include "DualContourMeshEventIntegrator.h"

void FDualContourMeshEventIntegrator::AddCellsRebuilt(FIntVector CellMin, FIntVector CellMax)
{
	MergeRange(CellMin, CellMax);
	PendingEvent.bDensityRebuilt = true;
	PendingEvent.bUpdateCollision = true;
}

void FDualContourMeshEventIntegrator::AddMaterialsChanged(FIntVector CellMin, FIntVector CellMax)
{
	MergeRange(CellMin, CellMax);
}

bool FDualContourMeshEventIntegrator::Consume(FPendingDualContourMeshEvent& OutEvent)
{
	if (!bPending)
		return false;

	OutEvent = PendingEvent;
	PendingEvent = {};
	bPending = false;
	return true;
}

void FDualContourMeshEventIntegrator::MergeRange(FIntVector CellMin, FIntVector CellMax)
{
	if (!bPending)
	{
		PendingEvent.CellMin = CellMin;
		PendingEvent.CellMax = CellMax;
		bPending = true;
		return;
	}

	PendingEvent.CellMin.X = FMath::Min(PendingEvent.CellMin.X, CellMin.X);
	PendingEvent.CellMin.Y = FMath::Min(PendingEvent.CellMin.Y, CellMin.Y);
	PendingEvent.CellMin.Z = FMath::Min(PendingEvent.CellMin.Z, CellMin.Z);
	PendingEvent.CellMax.X = FMath::Max(PendingEvent.CellMax.X, CellMax.X);
	PendingEvent.CellMax.Y = FMath::Max(PendingEvent.CellMax.Y, CellMax.Y);
	PendingEvent.CellMax.Z = FMath::Max(PendingEvent.CellMax.Z, CellMax.Z);
}
