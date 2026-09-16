#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"
#include "DualContourUtils.h"

bool DualContourEditing::ApplyEditDeltas(UDualContour& DualContour, TConstArrayView<FDualContourDensitySampleDelta> DensityDeltas,
	TConstArrayView<FDualContourMaterialSampleDelta> MaterialDeltas, bool bUseAfterValues)
{
	if (!DualContour.HasCurrentGeneratedData() || (DensityDeltas.IsEmpty() && MaterialDeltas.IsEmpty()))
		return false;
	FDualContourPendingDensityBatch DensityBatch;
	DensityBatch.Owner = &DualContour;
	DensityBatch.bOpen = true;
	FDualContourPendingMaterialBatch MaterialBatch;
	MaterialBatch.Owner = &DualContour;
	MaterialBatch.bOpen = true;
	const FIntVector SampleDims = DualContour.GetSampleDimensions();
	for (const FDualContourDensitySampleDelta& Delta : DensityDeltas)
	{
		const FIntVector& Coord = Delta.SampleCoord;
		if (!DualContourUtils::IsValidCoordinate(SampleDims, Coord.X, Coord.Y, Coord.Z))
			continue;
		FDualContourPendingDensitySample& Pending = DensityBatch.ChunkSamples.FindOrAdd(
			DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)).FindOrAdd(
			DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z));
		Pending.Before = DualContour.GetDensity(Coord.X, Coord.Y, Coord.Z);
		Pending.WorkingValue = FDensityChunk::DecodeLinearDensity(bUseAfterValues ? Delta.After : Delta.Before);
	}
	for (const FDualContourMaterialSampleDelta& Delta : MaterialDeltas)
	{
		const FIntVector& Coord = Delta.SampleCoord;
		if (!DualContourUtils::IsValidCoordinate(SampleDims, Coord.X, Coord.Y, Coord.Z))
			continue;
		FDualContourPendingMaterialSample& Pending = MaterialBatch.ChunkSamples.FindOrAdd(
			DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)).FindOrAdd(
			DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z));
		Pending.Before = DualContour.GetMaterialId(Coord.X, Coord.Y, Coord.Z);
		Pending.WorkingId = bUseAfterValues ? Delta.After : Delta.Before;
	}
	return DualContour.ApplyPendingEdit(DensityBatch, MaterialBatch);
}

void FDualContourEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyEditDeltas(*DualContour, DensityDeltas, MaterialDeltas, true);
}

void FDualContourEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContourEditing::ApplyEditDeltas(*DualContour, DensityDeltas, MaterialDeltas, false);
}
