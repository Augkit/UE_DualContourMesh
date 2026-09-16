#pragma once

#include "InteractiveToolChange.h"
#include "DualContourTypes.h"

namespace DualContourEditing
{
/** Submits both delta channels in one pending edit so chunk compaction, rebuilds and
 * notifications run a single pass. */
bool ApplyEditDeltas(UDualContour& DualContour, TConstArrayView<FDualContourDensitySampleDelta> DensityDeltas,
	TConstArrayView<FDualContourMaterialSampleDelta> MaterialDeltas, bool bUseAfterValues);
}

/** Sparse stroke-level undo covering every sample channel one stroke touched; empty arrays are no-ops.
 * UDualContour::DensityChunks deliberately remains NonTransactional. */
class FDualContourEditChange final : public FToolCommandChange
{
public:
	TArray<FDualContourDensitySampleDelta> DensityDeltas;
	TArray<FDualContourMaterialSampleDelta> MaterialDeltas;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("Dual Contour Edit"); }
};
