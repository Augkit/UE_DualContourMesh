#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"
#include "DualContourUtils.h"

bool DualContourEditing::ApplyDeltas(UDualContour& DualContour, TConstArrayView<FDualContourSampleDelta> Deltas, bool bUseAfterValues)
{
	if (!DualContour.HasCurrentGeneratedData() || Deltas.IsEmpty())
		return false;
	FDualContourPendingBatch Batch;
	Batch.Owner = &DualContour;
	Batch.bOpen = true;
	const FIntVector SampleDims = DualContour.GetSampleDimensions();
	for (const FDualContourSampleDelta& Delta : Deltas)
	{
		const FIntVector& Coord = Delta.SampleCoord;
		if (!DualContourUtils::IsValidCoordinate(SampleDims, Coord.X, Coord.Y, Coord.Z))
			continue;
		FDualContourPendingSample& Pending = Batch.ChunkSamples.FindOrAdd(
			DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)).FindOrAdd(
			DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z));
		Pending.Before = DualContour.GetDensity(Coord.X, Coord.Y, Coord.Z);
		Pending.WorkingValue = FDensityChunk::DecodeLinearDensity(bUseAfterValues ? Delta.After : Delta.Before);
	}
	return DualContour.ApplyPendingBatch(Batch);
}

void FDualContourEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyDeltas(*DualContour, Deltas, true);
}

void FDualContourEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyDeltas(*DualContour, Deltas, false);
}

void FDualContourMaterialEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContour->ApplyMaterialEditDeltas(Deltas, true);
}

void FDualContourMaterialEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContour->ApplyMaterialEditDeltas(Deltas, false);
}
