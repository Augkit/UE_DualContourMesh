#pragma once

#include "InteractiveToolChange.h"
#include "DualContourTypes.h"

/** Encoded density history owned exclusively by the editor. */
struct FDualContourDensitySampleDelta
{
	FIntVector SampleCoord = FIntVector::ZeroValue;
	uint16 Before = 0;
	uint16 After = 0;
};

namespace DualContourEditing
{
bool ApplyDensityDeltas(UDualContour& DualContour, TConstArrayView<FDualContourDensitySampleDelta> Deltas, bool bUseAfterValues);
bool ApplyMaterialDeltas(UDualContour& DualContour, TConstArrayView<FDualContourMaterialSampleDelta> Deltas, bool bUseAfterValues);
}

/** Sparse stroke-level undo. UDualContour::DensityChunks deliberately remains NonTransactional. */
class FDualContourEditChange final : public FToolCommandChange
{
public:
	TArray<FDualContourDensitySampleDelta> Deltas;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("Dual Contour Density Edit"); }
};

class FDualContourMaterialEditChange final : public FToolCommandChange
{
public:
	TArray<FDualContourMaterialSampleDelta> Deltas;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("Dual Contour Material Edit"); }
};
