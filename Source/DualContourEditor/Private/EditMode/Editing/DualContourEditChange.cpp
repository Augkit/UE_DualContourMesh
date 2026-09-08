#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"
#include "DualContourUtils.h"

bool DualContourEditing::ApplyDensityDeltas(UDualContour& DualContour, TConstArrayView<FDualContourDensitySampleDelta> Deltas, bool bUseAfterValues)
{
	if (!DualContour.HasCurrentGeneratedData() || Deltas.IsEmpty())
		return false;
	FDualContourPendingDensityBatch Batch;
	Batch.Owner = &DualContour;
	Batch.bOpen = true;
	const FIntVector SampleDims = DualContour.GetSampleDimensions();
	for (const FDualContourDensitySampleDelta& Delta : Deltas)
	{
		const FIntVector& Coord = Delta.SampleCoord;
		if (!DualContourUtils::IsValidCoordinate(SampleDims, Coord.X, Coord.Y, Coord.Z))
			continue;
		FDualContourPendingDensitySample& Pending = Batch.ChunkSamples.FindOrAdd(
			DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)).FindOrAdd(
			DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z));
		Pending.Before = DualContour.GetDensity(Coord.X, Coord.Y, Coord.Z);
		Pending.WorkingValue = FDensityChunk::DecodeLinearDensity(bUseAfterValues ? Delta.After : Delta.Before);
	}
	return DualContour.ApplyPendingDensityBatch(Batch);
}

bool DualContourEditing::ApplyMaterialDeltas(UDualContour& DualContour, TConstArrayView<FDualContourMaterialSampleDelta> Deltas, bool bUseAfterValues)
{
	if (!DualContour.HasCurrentGeneratedData() || Deltas.IsEmpty())
		return false;
	FDualContourPendingMaterialBatch Batch;
	Batch.Owner = &DualContour;
	Batch.bOpen = true;
	const FIntVector SampleDims = DualContour.GetSampleDimensions();
	for (const FDualContourMaterialSampleDelta& Delta : Deltas)
	{
		const FIntVector& Coord = Delta.SampleCoord;
		if (!DualContourUtils::IsValidCoordinate(SampleDims, Coord.X, Coord.Y, Coord.Z))
			continue;
		FDualContourPendingMaterialSample& Pending = Batch.ChunkSamples.FindOrAdd(
			DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)).FindOrAdd(
			DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z));
		Pending.Before = DualContour.GetMaterialId(Coord.X, Coord.Y, Coord.Z);
		Pending.WorkingId = bUseAfterValues ? Delta.After : Delta.Before;
	}
	return DualContour.ApplyPendingMaterialBatch(Batch);
}

void FDualContourDensityEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyDensityDeltas(*DualContour, Deltas, true);
}

void FDualContourDensityEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyDensityDeltas(*DualContour, Deltas, false);
}

void FDualContourMaterialEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyMaterialDeltas(*DualContour, Deltas, true);
}

void FDualContourMaterialEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyMaterialDeltas(*DualContour, Deltas, false);
}
