# One-shot edits

`FDualContourEditContext` is a runtime C++ object that owns pending density and material writes for one submission. Construct, stage and commit it on the game thread. Keep the target alive and do not modify or regenerate it through another path between construction and submission. Samplers are borrowed only for the duration of each `Apply` call.

```cpp
#include "DualContourEditContext.h"

FDualContourEditContext Edit(*Target);
FDualContourVolumeSampler Volume(*BrushSource, SourceToTarget);
Edit.ApplyDensity(Volume, EDualContourEditOperation::Union);

FDualContourShapeSampler Mask;
Mask.Center = LocalCenter;
Mask.Radius = 100.0f;
Edit.ApplyMaterial(Mask, 3, 0.5f, true);

FDualContourMaterialEditResult MaterialChanges;
Edit.Commit(MaterialChanges, [](const FIntVector& Coord, uint16 Before, uint16 After)
{
    // Collect actual encoded density changes for undo, if needed.
});
```

Material painting above sees density staged by the volume operation. `GetDensity` returns linear density, including any pending value; `GetMaterial` likewise checks pending material writes first. `SetDensity` and `SetMaterial` validate coordinates. Returning samples to their original values produces no change on submission.

Samplers implement target-local `GetBounds` and `Sample(Position, Value, Weight)`. Returning false or zero weight excludes a sample, including when the material threshold is zero. Density values use the project's centered fixed-point units. The operation selects how to use them:

- Add/Subtract apply a signed maximum-density increment scaled by strength and mask weight.
- Union/Difference combine the sampled density with the pending density.
- Replace interpolates toward the sampled density.
- Smooth uses the mask and a three-axis `[1, 2, 1] / 4` filter over a snapshot taken before that operation.

Built-in samplers cover sphere/box masks, directional falloff, plane fields, matching-grid restore, and transformed contour volumes. Plane and restore samplers borrow their mask. Volume sampling supports parallel computation while the game thread waits; custom samplers remain serial unless they explicitly opt in. `ApplySampledRegion` also accepts precomputed chunk samples from the existing `UVolumeSampler` pipeline, preserving its encoded union/difference rules.

`Commit` closes the context, writes both stores through `UDualContour::ApplyPendingEdit`, records save overlays, then notifies observers and starts density rebuilding once. Material-only edits do not rebuild cells. Density callbacks and material deltas report actual committed values, not merely staged intent. Callbacks and notification handlers must not mutate the target during submission. The bool result means at least one actual change; empty/no-op submissions return false and are still consumed. Density contour rebuilding remains asynchronous.

Copying is disabled. A second commit or writes after commit fail. Destruction discards unsubmitted writes; it never commits implicitly. Undo transactions and package dirtying remain the caller's responsibility. This is a synchronous staging/submission contract, not an asynchronous job with revision conflict resolution.

The editor retains `FDualContourBrushStamp` as an input description. `DualContourBrushOperations` adapts it to samplers and operations; the brush tool owns one context per preview submission. Material placement volumes implement the same sampler contract. Existing single-batch submission APIs and `ModifyDensityChunks` remain available as compatibility entry points.

Run `Automation RunTests DualContour.EditContext` for lifecycle, mixed density/material visibility, no-op edits, smoothing symmetry, transformed/parallel volume sampling, and sampled-region coverage.
