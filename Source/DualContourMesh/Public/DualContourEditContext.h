#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"
#include "Templates/Function.h"

class UDualContour;
class UVolumeSampler;

/** One submission, on the game thread. The caller keeps the target alive and avoids other writes until Commit.
 * Reads see staged values. Destruction discards unsubmitted edits; copying and repeated submission are forbidden.
 */
class DUALCONTOURMESH_API FDualContourEditContext
{
public:
	explicit FDualContourEditContext(UDualContour& InTarget);
	FDualContourEditContext(const FDualContourEditContext&) = delete;
	FDualContourEditContext& operator=(const FDualContourEditContext&) = delete;
	bool IsOpen() const;
	UDualContour* GetTarget() const;
	float GetDensity(FIntVector Coord) const;
	uint8 GetMaterial(FIntVector Coord) const;
	bool SetDensity(FIntVector Coord, float Value);
	bool SetMaterial(FIntVector Coord, uint8 Value);
	bool ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler, float Strength = 1.0f);
	/** Applies a source after an additional transform around the sampler pivot. */
	bool ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
		const FTransform& SampleTransform, float Strength = 1.0f);
	bool ApplyMaterial(const UVolumeSampler& Sampler, uint8 MaterialId, float Threshold = 0.5f, bool bSolidOnly = true);
	bool Commit(FDualContourDensityChangedCallback OnDensityChanged = {},
		FDualContourMaterialChangedCallback OnMaterialChanged = {});

private:
	bool GetSampleBounds(const UVolumeSampler& Sampler, const FTransform* SampleTransform, FIntVector& Min, FIntVector& Max) const;
	bool ApplyDensityInternal(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
		const FTransform* SampleTransform, float Strength);
	TWeakObjectPtr<UDualContour> Target;
	FDualContourPendingDensityBatch DensityBatch;
	FDualContourPendingMaterialBatch MaterialBatch;
	bool bOpen = true;
};
