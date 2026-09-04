#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"

void FDualContourEditChange::Apply(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContour->ApplyEditDeltas(Deltas, true);
}

void FDualContourEditChange::Revert(UObject* Object)
{
	if (UDualContour* DualContour = Cast<UDualContour>(Object))
		DualContour->ApplyEditDeltas(Deltas, false);
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
