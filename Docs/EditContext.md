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
- `Sample` takes a `TargetLocalPosition` and an `FVolumeSamplerPlacement`; `SamplerToTargetTransform` (when non-identity) is folded with the sampler's `SamplingTransform` into that single affine by `MakePlacement`, since both rotate/scale about the same pivot `Pivot * VolumeSize`.
- `BaseVolumePosition` is the result of undoing that combined transform; its finite domain is `[0, VolumeSize]`.
- `CenteredLocalPosition` is `BaseVolumePosition - 0.5 * VolumeSize`; analytic SDF implementations use this centered sampler-local space.
- `SourceLocalPosition` and `SourceGridPosition` belong to a referenced source `UDualContour`; divide the former by the source `CellSize` to obtain the latter.

## Source contract

- `GetBounds()` returns a finite sampler-input bounding box. With no outer placement transform, sampler-input coordinates are target-local coordinates.
- `Sample(TargetLocalPosition, Value, Weight)` returns linear density and influence weight. False or zero weight excludes the position.
- Procedural, texture and contour sources share `TryGetBaseVolumePosition`, which maps target-local coordinates through the combined placement/sampling affine into base-volume coordinates (UE units, corner origin). Brush sources work directly in target-local coordinates and provide their own influence weights.
- `Prepare` / `Finish` wrap resource preparation/cleanup and are balanced by context and generation paths even on early exits. A pass builds one immutable `FVolumeSamplerPlacement` value and shares it across workers, which keeps `Sample` parallel-safe. Direct callers must do the same and keep the source alive.
- `CanSampleInParallel` exposes the existing explicit thread-safety capability. Native procedural sources can run in parallel; Blueprint SDF dispatch remains on the game thread.

Density strength is multiplied by source weight. Material painting compares weight to the threshold and optionally checks pending solid density. Chunk generation uses weight as a validity mask (positive weights retain sampled density); it does not interpolate against a previous grid. The transform-aware edit overload accepts a `SamplerToTargetTransform` outside the source's own `SamplingTransform`, around the volume pivot.

`DualContourBrushSamplers.h` supplies UObject shape/falloff, masked plane, transformed volume-brush and matching-grid restore sources. They override target-local bounds/sample directly; their coordinates are already target-local. Composite masks are GC-tracked and their preparation is delegated. Editor placement volumes also use a UVolumeSampler subclass. These sources support both editing and chunk generation through the common bounds/sample interface.

## Editing and submission

`EDualContourDensityOperation` describes Add/Subtract, Union/Difference, Replace or Smooth. `FDualContourEditContext` performs the target read, operation evaluation and result staging; Smooth owns its three-pass neighborhood snapshot per invocation.

Commit writes both stores and save overlays through `UDualContour::ApplyPendingEdit` before notifying observers. Density rebuild is asynchronous; material-only edits do not rebuild cells. Callbacks report actual encoded changes; returning to original values creates no undo delta. Callbacks must not mutate the target. Undo transactions and package dirtying remain caller responsibilities.

The editor retains BrushStamp as input configuration, adapting it into sources and operation objects. Single-batch submission remains available through `UDualContour::ApplyPendingBatch` for callers that already own final density values.

Run `Automation RunTests DualContour.EditContext` for batch lifecycle, pending-batch submission, mixed visibility, smoothing/volume operations, and existing procedural sources shared by editing and generation.
