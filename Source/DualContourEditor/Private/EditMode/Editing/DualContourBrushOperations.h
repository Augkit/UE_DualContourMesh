#pragma once

#include "DualContourEditorTypes.h"
#include "DualContourTypes.h"

class UDualContour;
class FDualContourEditContext;
class ADualContourMeshActor;
class ADualContourMaterialBrushVolume;

/** Stages sample changes only. The caller owns stroke lifetime, commit, undo and preview updates. */
namespace DualContourBrushOperations
{
float EvaluateFalloff(float NormalizedDistance, float Falloff, EDualContourBrushFalloff FalloffType);
bool ApplyDensityStamp(FDualContourEditContext& Edit, const UDualContour* RestoreSource, const FDualContourBrushStamp& Stamp);
bool ApplyMaterialStamp(FDualContourEditContext& Edit, const FDualContourBrushStamp& Stamp, uint8 PaintId, float Threshold,
                        bool bSolidSamplesOnly);
void ApplyMaterialVolumes(ADualContourMeshActor* TargetActor, FDualContourEditContext& Edit,
                          TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes, uint8 PaintId);
} // namespace DualContourBrushOperations
