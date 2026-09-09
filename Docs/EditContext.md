# One-shot edits and unified volume sources

`FDualContourEditContext` stages density and material changes for one submission. Reads include pending values. It is non-copyable; commit closes it, and destruction discards unsubmitted writes. Keep the target alive and avoid other writes until submission. All staging/submission occurs on the game thread.

```cpp
#include "DualContourEditContext.h"
#include "DualContourTypes.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "UObject/StrongObjectPtr.h"

TStrongObjectPtr<USphereVolumeSampler> Source(NewObject<USphereVolumeSampler>());
Source->VolumeSize = FVector(200);
Source->Radius = 80;
Source->SamplingTransform = FTransform(LocalOffset);

FDualContourEditContext Edit(*Target);
Edit.ApplyDensity(EDualContourDensityOperation::Union, *Source);
Edit.ApplyMaterial(*Source, 3, 0.5f, true);
Edit.Commit();
```

All sampling sources now inherit `UVolumeSampler`. There is no separate F field-sampler interface. Existing procedural, texture, noise and contour sources can feed editing directly.

## Coordinate terminology

- `TargetLocalPosition` is a continuous position in the target `UDualContour` local space, measured in the same length units as `CellSize`.
- `SamplerToTargetTransform` maps sampler-input coordinates into target-local coordinates around `Pivot * VolumeSize`.
- `SamplerInputPosition` is the position passed to a resource sampler after undoing `SamplerToTargetTransform`. It equals `TargetLocalPosition` when the outer transform is identity.
- `BaseVolumePosition` is the result of undoing the sampler's persistent `SamplingTransform`; its finite domain is `[0, VolumeSize]`.
- `NormalizedVolumePosition` is `BaseVolumePosition / VolumeSize` and therefore lies in `[0, 1]` on every axis.
- `CenteredLocalPosition` is `(NormalizedVolumePosition - 0.5) * VolumeSize`; analytic SDF implementations use this centered sampler-local space.
- `SourceLocalPosition` and `SourceGridPosition` belong to a referenced source `UDualContour`; divide the former by the source `CellSize` to obtain the latter.

## Source contract

- `GetBounds()` returns a finite sampler-input bounding box. With no outer placement transform, sampler-input coordinates are target-local coordinates.
- `Sample(SamplerInputPosition, Value, Weight)` returns linear density and influence weight. False or zero weight excludes the position.
- Procedural, texture and contour sources share `TryGetNormalizedVolumePosition`, which maps sampler-input coordinates through `SamplingTransform` into normalized base-volume coordinates. Brush sources work directly in target-local coordinates and provide their own influence weights.
- `Prepare` / `Finish` wrap resource preparation/cleanup. Context and generation paths balance these even on early exits. Direct callers must do the same and keep the source alive.
- `CanSampleInParallel` exposes the existing explicit thread-safety capability. Native procedural sources can run in parallel; Blueprint SDF dispatch remains on the game thread.

Density strength is multiplied by source weight. Material painting compares weight to the threshold and optionally checks pending solid density. Chunk generation uses weight as a validity mask (positive weights retain sampled density); it does not interpolate against a previous grid. The transform-aware edit overload accepts a `SamplerToTargetTransform` outside the source's own `SamplingTransform`, around the volume pivot.

`DualContourBrushSamplers.h` supplies UObject shape/falloff, masked plane, transformed volume-brush and matching-grid restore sources. They override target-local bounds/sample directly; their coordinates are already target-local. Composite masks are GC-tracked and their preparation is delegated. Editor placement volumes also use a UVolumeSampler subclass. These sources support both editing and chunk generation through the common bounds/sample interface.

## Editing and submission

`EDualContourDensityOperation` describes Add/Subtract, Union/Difference, Replace or Smooth. `FDualContourEditContext` performs the target read, operation evaluation and result staging; Smooth owns its three-pass neighborhood snapshot per invocation.

Commit writes both stores and save overlays through `UDualContour::ApplyPendingEdit` before notifying observers. Density rebuild is asynchronous; material-only edits do not rebuild cells. Callbacks report actual encoded changes; returning to original values creates no undo delta. Callbacks must not mutate the target. Undo transactions and package dirtying remain caller responsibilities.

The editor retains BrushStamp as input configuration, adapting it into sources and operation objects. Single-batch submission remains available through `UDualContour::ApplyPendingBatch` for callers that already own final density values.

Run `Automation RunTests DualContour.EditContext` for batch lifecycle, pending-batch submission, mixed visibility, smoothing/volume operations, and existing procedural sources shared by editing and generation.
