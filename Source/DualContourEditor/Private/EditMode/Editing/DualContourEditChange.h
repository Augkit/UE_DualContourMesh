#pragma once

#include "ToolContextInterfaces.h"
#include "DualContourTypes.h"

/** Sparse stroke-level undo. UDualContour::DensityChunks deliberately remains NonTransactional. */
class FDualContourEditChange final : public FToolCommandChange
{
public:
	TArray<FDualContourSampleDelta> Deltas;

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
