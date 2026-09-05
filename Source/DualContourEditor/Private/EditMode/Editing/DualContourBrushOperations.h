#pragma once

#include "DualContourEditorTypes.h"
#include "DualContourTypes.h"

class UDualContour;
class ADualContourMeshActor;
class ADualContourMaterialBrushVolume;

/** Stages sample changes only. The caller owns stroke lifetime, commit, undo and preview updates. */
namespace DualContourBrushOperations
{
	float EvaluateFalloff(float NormalizedDistance, float Falloff, EDualContourBrushFalloff FalloffType);
	bool ApplyDensityStamp(UDualContour* DualContour, const UDualContour* RestoreSource,
		FDualContourPendingBatch& Batch, const FDualContourBrushStamp& Stamp);
	bool ApplyMaterialStamp(UDualContour* DualContour, FDualContourPendingMaterialBatch& Batch,
		const FDualContourBrushStamp& Stamp, uint8 PaintId, float Threshold, bool bSolidSamplesOnly);
	void ApplyMaterialVolumes(ADualContourMeshActor* TargetActor, FDualContourPendingMaterialBatch& Batch,
		TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes, uint8 PaintId);
	bool SetMaterialSample(const UDualContour& DualContour, FDualContourPendingMaterialBatch& Batch,
		int32 X, int32 Y, int32 Z, uint8 PaintId);
}
