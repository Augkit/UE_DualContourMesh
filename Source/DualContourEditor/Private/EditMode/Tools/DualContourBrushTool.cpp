#include "EditMode/Tools/DualContourBrushTool.h"

#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "DualContourUtils.h"
#include "VolumeSampledDualContour.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Async/ParallelFor.h"
#include "InteractiveToolManager.h"
#include "Engine/World.h"
#include "DynamicMeshBuilder.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneView.h"
#include "ToolContextInterfaces.h"

#define LOCTEXT_NAMESPACE "DualContourBrushTool"

namespace
{
float EvaluateFalloff(float NormalizedDistance, float Falloff, EDualContourBrushFalloff FalloffType)
{
	if (NormalizedDistance >= 1.0f)
		return 0.0f;
	const float Inner = 1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f);
	if (NormalizedDistance <= Inner || Falloff <= UE_SMALL_NUMBER)
		return 1.0f;
	const float T = FMath::Clamp((NormalizedDistance - Inner) / FMath::Max(Falloff, UE_SMALL_NUMBER), 0.0f, 1.0f);
	switch (FalloffType)
	{
		case EDualContourBrushFalloff::Linear:
			return 1.0f - T;
		case EDualContourBrushFalloff::Spherical:
			return FMath::Sqrt(FMath::Max(0.0f, 1.0f - T * T));
		case EDualContourBrushFalloff::Tip:
			return FMath::Square(1.0f - T);
		default:
			return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
	}
}
}

void UDualContourBrushToolBuilder::Initialize(UDualContourEditModeSettings* InSettings, ADualContourMeshActor* InTargetActor)
{
	Settings = InSettings;
	TargetActor = InTargetActor;
}

bool UDualContourBrushToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Settings.IsValid() && TargetActor.IsValid() && SceneState.World != nullptr;
}

UInteractiveTool* UDualContourBrushToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDualContourBrushTool* Tool = NewObject<UDualContourBrushTool>(SceneState.ToolManager);
	Tool->Initialize(SceneState.World, Settings.Get(), TargetActor.Get());
	return Tool;
}

void UDualContourBrushTool::Initialize(UWorld* InWorld, UDualContourEditModeSettings* InSettings, ADualContourMeshActor* InTargetActor)
{
	TargetWorld = InWorld;
	Settings = InSettings;
	TargetActor = InTargetActor;
}

void UDualContourBrushTool::SetTargetActor(ADualContourMeshActor* InTargetActor)
{
	if (TargetActor != InTargetActor && bStrokeActive)
		FinishStroke(true);
	TargetActor = InTargetActor;
	bHasHit = false;
}

void UDualContourBrushTool::Setup()
{
	Super::Setup();
	BrushFalloffMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/DualContourMesh/Editor/Materials/M_DualContourBrushFalloff.M_DualContourBrushFalloff"));

	UClickDragInputBehavior* DragBehavior = NewObject<UClickDragInputBehavior>(this);
	DragBehavior->Modifiers.RegisterModifier(ShiftModifierId, FInputDeviceState::IsShiftKeyDown);
	DragBehavior->Initialize(this);
	AddInputBehavior(DragBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);
	AddToolPropertySource(Settings);
}

void UDualContourBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (bStrokeActive)
		FinishStroke(ShutdownType == EToolShutdownType::Cancel);
	if (Settings)
		Settings->SaveConfig();
	Super::Shutdown(ShutdownType);
}

void UDualContourBrushTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Settings)
		Settings->SaveConfig();
}

void UDualContourBrushTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == ShiftModifierId)
		bShiftDown = bIsOn;
}

bool UDualContourBrushTool::UpdateHit(const FRay& WorldRay, float* OutDistance)
{
	bHasHit = false;
	if (!TargetWorld || !TargetActor)
		return false;

	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectQuery(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DualContourEditTrace), true);
	TargetWorld->LineTraceMultiByObjectType(Hits, WorldRay.Origin, WorldRay.PointAt(HALF_WORLD_MAX), ObjectQuery, QueryParams);
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() != TargetActor || !Hit.GetComponent() || !Hit.GetComponent()->IsA<UDualContourMeshComponent>())
			continue;
		HitPosition = Hit.ImpactPoint;
		HitNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		bHasHit = true;
		if (OutDistance)
			*OutDistance = Hit.Distance;
		return true;
	}
	return false;
}

FInputRayHit UDualContourBrushTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	float Distance = 0.0f;
	return UpdateHit(PressPos.WorldRay, &Distance) ? FInputRayHit(Distance) : FInputRayHit();
}

void UDualContourBrushTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (!UpdateHit(PressPos.WorldRay) || !TargetActor || !TargetActor->DualContour)
		return;
	if (!BeginEditBatch())
		return;
	bStrokeActive = true;
	TargetActor->SetDensityEditInProgress(true);
	ClayPlaneOrigin = TargetActor->GetActorTransform().InverseTransformPosition(HitPosition);
	ClayPlaneNormal = TargetActor->GetActorTransform().InverseTransformVectorNoScale(HitNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	StrokeDeltas.Reset();
	LastStampPosition = HitPosition;
	LastPreviewFlushTime = FPlatformTime::Seconds();
	StationaryAccumulator = 0.0f;
	ApplyStampAt(HitPosition, HitNormal, 1.0f);
	if (Settings->ActiveTool == EDualContourEditTool::Brush)
		FinishStroke(false);
}

bool UDualContourBrushTool::BeginEditBatch()
{
	ActiveBatch = FDualContourEditBatch();
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (!IsValid(DualContour) || !DualContour->HasCurrentGeneratedData())
		return false;

	ActiveBatch.bOpen = true;
	ActiveBatch.Owner = DualContour;
	return true;
}

void UDualContourBrushTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bStrokeActive || !UpdateHit(DragPos.WorldRay))
		return;
	ApplyPathTo(HitPosition, HitNormal);
	if (FPlatformTime::Seconds() - LastPreviewFlushTime >= FMath::Max(0.033f, Settings->PreviewUpdateInterval))
		FlushStroke(false);
}

void UDualContourBrushTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if (!bStrokeActive)
		return;
	if (UpdateHit(ReleasePos.WorldRay))
		ApplyPathTo(HitPosition, HitNormal);
	FinishStroke(false);
}

void UDualContourBrushTool::OnTerminateDragSequence()
{
	if (bStrokeActive)
		FinishStroke(true);
}

FInputRayHit UDualContourBrushTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	float Distance = 0.0f;
	return UpdateHit(PressPos.WorldRay, &Distance) ? FInputRayHit(Distance) : FInputRayHit();
}

void UDualContourBrushTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	UpdateHit(DevicePos.WorldRay);
}

bool UDualContourBrushTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UpdateHit(DevicePos.WorldRay);
	return true;
}

void UDualContourBrushTool::OnTick(float DeltaTime)
{
	if (!bStrokeActive || !bHasHit || !Settings || !Settings->bApplyWithoutMoving
	    || Settings->ActiveTool == EDualContourEditTool::Brush)
		return;
	StationaryAccumulator += DeltaTime;
	constexpr float FixedStep = 1.0f / 30.0f;
	while (StationaryAccumulator >= FixedStep)
	{
		ApplyStampAt(HitPosition, HitNormal, FixedStep);
		StationaryAccumulator -= FixedStep;
	}
	if (FPlatformTime::Seconds() - LastPreviewFlushTime >= FMath::Max(0.033f, Settings->PreviewUpdateInterval))
		FlushStroke(false);
}

void UDualContourBrushTool::ApplyPathTo(const FVector& WorldPosition, const FVector& WorldNormal)
{
	const float Spacing = FMath::Max(1.0f, Settings->BrushSize * 0.15f);
	const FVector Delta = WorldPosition - LastStampPosition;
	const float Distance = Delta.Length();
	if (Distance < Spacing)
		return;
	const int32 Steps = FMath::FloorToInt(Distance / Spacing);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Alpha = FMath::Min(1.0f, Step * Spacing / Distance);
		ApplyStampAt(FMath::Lerp(LastStampPosition, WorldPosition, Alpha), WorldNormal, 1.0f);
	}
	LastStampPosition += Delta.GetSafeNormal() * (Steps * Spacing);
}

bool UDualContourBrushTool::ApplyStampAt(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale)
{
	return TargetActor && TargetActor->DualContour
	       && ApplyBrushStamp(ActiveBatch, MakeStamp(WorldPosition, WorldNormal, TimeScale));
}

bool UDualContourBrushTool::ApplyBrushStamp(FDualContourEditBatch& Batch, const FDualContourBrushStamp& Stamp) const
{
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (!IsValid(DualContour) || !Batch.bOpen || Batch.Owner != DualContour || !DualContour->HasCurrentGeneratedData()
	    || !FMath::IsFinite(Stamp.Radius) || Stamp.Radius <= UE_SMALL_NUMBER || !FMath::IsFinite(Stamp.Strength))
	{
		return false;
	}

	const FIntVector SampleDims = DualContour->GetSampleDimensions();
	const UDualContour* RestoreSource = Stamp.Operation == EDualContourDensityEditOperation::Erase && TargetActor
		                                    ? TargetActor->InitialDualContour.Get()
		                                    : nullptr;
	if (Stamp.Operation == EDualContourDensityEditOperation::Erase
	    && (!IsValid(RestoreSource) || !RestoreSource->HasCurrentGeneratedData()
	        || RestoreSource->GetSampleDimensions() != SampleDims
	        || !FMath::IsNearlyEqual(RestoreSource->CellSize, DualContour->CellSize)))
	{
		return false;
	}

	auto GetWorkingDensity = [DualContour, &Batch](int32 X, int32 Y, int32 Z) -> float
	{
		const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
		if (const TMap<uint16, FDualContourPendingSample>* Chunk = Batch.ChunkSamples.Find(ChunkCoord))
			if (const FDualContourPendingSample* Pending = Chunk->Find(DualContourUtils::ChunkLocalIndex(X, Y, Z)))
				return Pending->WorkingValue;
		return static_cast<float>(DualContour->GetDensity(X, Y, Z));
	};
	auto SetWorkingDensity = [DualContour, &Batch, &GetWorkingDensity](int32 X, int32 Y, int32 Z, float Value)
	{
		const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
		TMap<uint16, FDualContourPendingSample>& Chunk = Batch.ChunkSamples.FindOrAdd(ChunkCoord);
		const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(X, Y, Z);
		FDualContourPendingSample* Existing = Chunk.Find(LocalIndex);
		if (!Existing)
		{
			FDualContourPendingSample Pending;
			Pending.Before = DualContour->GetDensity(X, Y, Z);
			Pending.WorkingValue = GetWorkingDensity(X, Y, Z);
			Existing = &Chunk.Add(LocalIndex, Pending);
		}
		Existing->WorkingValue = FMath::Clamp(Value, 0.0f, 255.0f);
	};

	FVector BoundsMin = Stamp.LocalCenter - FVector(Stamp.Radius);
	FVector BoundsMax = Stamp.LocalCenter + FVector(Stamp.Radius);
	const bool bVolumeStamp = Stamp.Operation == EDualContourDensityEditOperation::StampUnion
	                          || Stamp.Operation == EDualContourDensityEditOperation::StampDifference;
	if (bVolumeStamp)
	{
		if (!IsValid(Stamp.VolumeBrush) || !Stamp.VolumeBrush->HasCurrentGeneratedData())
			return false;
		const FVector SourceMax(
			Stamp.VolumeBrush->CellCount.X * Stamp.VolumeBrush->CellSize,
			Stamp.VolumeBrush->CellCount.Y * Stamp.VolumeBrush->CellSize,
			Stamp.VolumeBrush->CellCount.Z * Stamp.VolumeBrush->CellSize);
		FBox TargetBounds(ForceInit);
		for (int32 Z = 0; Z <= 1; ++Z)
			for (int32 Y = 0; Y <= 1; ++Y)
				for (int32 X = 0; X <= 1; ++X)
					TargetBounds += Stamp.VolumeToTarget.TransformPosition(FVector(X * SourceMax.X, Y * SourceMax.Y, Z * SourceMax.Z));
		BoundsMin = TargetBounds.Min;
		BoundsMax = TargetBounds.Max;
	}

	const FIntVector SampleMin(
		FMath::Clamp(FMath::FloorToInt(BoundsMin.X / DualContour->CellSize), 0, SampleDims.X - 1),
		FMath::Clamp(FMath::FloorToInt(BoundsMin.Y / DualContour->CellSize), 0, SampleDims.Y - 1),
		FMath::Clamp(FMath::FloorToInt(BoundsMin.Z / DualContour->CellSize), 0, SampleDims.Z - 1));
	const FIntVector SampleMax(
		FMath::Clamp(FMath::CeilToInt(BoundsMax.X / DualContour->CellSize), 0, SampleDims.X - 1),
		FMath::Clamp(FMath::CeilToInt(BoundsMax.Y / DualContour->CellSize), 0, SampleDims.Y - 1),
		FMath::Clamp(FMath::CeilToInt(BoundsMax.Z / DualContour->CellSize), 0, SampleDims.Z - 1));
	const float OperationWeight = bVolumeStamp
		                              ? 1.0f
		                              : FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	if (OperationWeight <= 0.0f)
		return false;

	TArray<float> SmoothedValues;
	FIntVector SmoothDims = FIntVector::ZeroValue;
	if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
	{
		const FIntVector HaloMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1), FMath::Max(0, SampleMin.Z - 1));
		const FIntVector HaloMax(FMath::Min(SampleDims.X - 1, SampleMax.X + 1), FMath::Min(SampleDims.Y - 1, SampleMax.Y + 1),
			FMath::Min(SampleDims.Z - 1, SampleMax.Z + 1));
		SmoothDims = FIntVector(HaloMax.X - HaloMin.X + 1, HaloMax.Y - HaloMin.Y + 1, HaloMax.Z - HaloMin.Z + 1);
		TArray<float> Source;
		Source.SetNumUninitialized(DualContourUtils::Volume(SmoothDims));
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					Source[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] = GetWorkingDensity(HaloMin.X + X, HaloMin.Y + Y, HaloMin.Z + Z);

		TArray<float> PassX;
		TArray<float> PassY;
		PassX.SetNumUninitialized(Source.Num());
		PassY.SetNumUninitialized(Source.Num());
		SmoothedValues.SetNumUninitialized(Source.Num());
		auto AtClamped = [&SmoothDims](const TArray<float>& Values, int32 X, int32 Y, int32 Z)
		{
			return Values[DualContourUtils::LinearIndex(SmoothDims, FMath::Clamp(X, 0, SmoothDims.X - 1), FMath::Clamp(Y, 0, SmoothDims.Y - 1),
				FMath::Clamp(Z, 0, SmoothDims.Z - 1))];
		};
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					PassX[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(Source, X - 1, Y, Z) + 2.0f * AtClamped(Source, X, Y, Z) + AtClamped(Source, X + 1, Y, Z)) * 0.25f;
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					PassY[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(PassX, X, Y - 1, Z) + 2.0f * AtClamped(PassX, X, Y, Z) + AtClamped(PassX, X, Y + 1, Z)) * 0.25f;
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					SmoothedValues[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(PassY, X, Y, Z - 1) + 2.0f * AtClamped(PassY, X, Y, Z) + AtClamped(PassY, X, Y, Z + 1)) * 0.25f;

		// Store the snapshot origin in BoundsMin for indexing below; bounds are no longer needed.
		BoundsMin = FVector(HaloMin.X, HaloMin.Y, HaloMin.Z);
	}

	bool bChanged = false;
	if (bVolumeStamp)
	{
		check(IsInGameThread());
		const FIntVector VolumeSampleDims(
			SampleMax.X - SampleMin.X + 1,
			SampleMax.Y - SampleMin.Y + 1,
			SampleMax.Z - SampleMin.Z + 1);
		const int64 PlaneSampleCount64 = static_cast<int64>(VolumeSampleDims.X) * VolumeSampleDims.Y;
		const int64 VolumeSampleCount64 = PlaneSampleCount64 * VolumeSampleDims.Z;
		if (PlaneSampleCount64 <= 0 || PlaneSampleCount64 > MAX_int32
		    || VolumeSampleCount64 <= 0 || VolumeSampleCount64 > MAX_int32)
		{
			return false;
		}

		const int32 PlaneSampleCount = static_cast<int32>(PlaneSampleCount64);
		const int32 VolumeSampleCount = static_cast<int32>(VolumeSampleCount64);
		TArray<float> PendingDensities;
		TArray<uint8> bPendingDensityChanges;
		PendingDensities.SetNumUninitialized(VolumeSampleCount);
		bPendingDensityChanges.SetNumUninitialized(VolumeSampleCount);

		// Batch and both DualContours remain unchanged while the game thread blocks in
		// ParallelFor, so workers may read their TMaps concurrently. Each worker writes
		// only its own PendingDensities element; nested Batch TMaps are staged below.
		const TMap<FIntVector, TMap<uint16, FDualContourPendingSample>>& ReadOnlyChunkSamples = Batch.ChunkSamples;
		const auto GetSnapshotDensity = [DualContour, &ReadOnlyChunkSamples](int32 X, int32 Y, int32 Z) -> float
		{
			const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
			if (const TMap<uint16, FDualContourPendingSample>* Chunk = ReadOnlyChunkSamples.Find(ChunkCoord))
				if (const FDualContourPendingSample* Pending = Chunk->Find(DualContourUtils::ChunkLocalIndex(X, Y, Z)))
					return Pending->WorkingValue;
			return static_cast<float>(DualContour->GetDensity(X, Y, Z));
		};

		ParallelFor(TEXT("DualContourBrush.VolumeStampSamples"), VolumeSampleCount, 256,
			[DualContour, &Stamp, SampleMin, VolumeSampleDims, PlaneSampleCount, OperationWeight,
				&GetSnapshotDensity, &PendingDensities, &bPendingDensityChanges](int32 SampleIndex)
			{
				const int32 LocalZ = SampleIndex / PlaneSampleCount;
				const int32 PlaneIndex = SampleIndex - LocalZ * PlaneSampleCount;
				const int32 LocalY = PlaneIndex / VolumeSampleDims.X;
				const int32 LocalX = PlaneIndex - LocalY * VolumeSampleDims.X;
				const int32 X = SampleMin.X + LocalX;
				const int32 Y = SampleMin.Y + LocalY;
				const int32 Z = SampleMin.Z + LocalZ;

				const FVector LocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
				const FVector SourcePosition = Stamp.VolumeToTarget.InverseTransformPosition(LocalPosition);
				const FVector SourceGrid = SourcePosition / Stamp.VolumeBrush->CellSize;
				if (SourceGrid.X < 0.0 || SourceGrid.Y < 0.0 || SourceGrid.Z < 0.0
				    || SourceGrid.X > Stamp.VolumeBrush->CellCount.X
				    || SourceGrid.Y > Stamp.VolumeBrush->CellCount.Y
				    || SourceGrid.Z > Stamp.VolumeBrush->CellCount.Z)
				{
					bPendingDensityChanges[SampleIndex] = false;
					return;
				}

				const float TargetDensity = Stamp.VolumeBrush->TrilinearDensity(SourceGrid);
				const float OldDensity = GetSnapshotDensity(X, Y, Z);
				float CombinedDensity = TargetDensity;
				if (Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
				{
					const float DifferenceDensity = TargetDensity >= GDualContourIsoValue
						                                ? 2.0f * GDualContourIsoValue - TargetDensity
						                                : 255.0f;
					CombinedDensity = FMath::Min(OldDensity, DifferenceDensity);
				}
				else
				{
					CombinedDensity = FMath::Max(OldDensity, TargetDensity);
				}

				const float NewDensity = FMath::Lerp(OldDensity, CombinedDensity, OperationWeight);
				PendingDensities[SampleIndex] = NewDensity;
				bPendingDensityChanges[SampleIndex] = !FMath::IsNearlyEqual(NewDensity, OldDensity, KINDA_SMALL_NUMBER);
			}, EParallelForFlags::Unbalanced);

		for (int32 SampleIndex = 0; SampleIndex < VolumeSampleCount; ++SampleIndex)
		{
			if (!bPendingDensityChanges[SampleIndex])
				continue;

			const int32 LocalZ = SampleIndex / PlaneSampleCount;
			const int32 PlaneIndex = SampleIndex - LocalZ * PlaneSampleCount;
			const int32 LocalY = PlaneIndex / VolumeSampleDims.X;
			const int32 LocalX = PlaneIndex - LocalY * VolumeSampleDims.X;
			SetWorkingDensity(SampleMin.X + LocalX, SampleMin.Y + LocalY, SampleMin.Z + LocalZ,
				PendingDensities[SampleIndex]);
			bChanged = true;
		}
		return bChanged;
	}

	for (int32 Z = SampleMin.Z; Z <= SampleMax.Z; ++Z)
		for (int32 Y = SampleMin.Y; Y <= SampleMax.Y; ++Y)
			for (int32 X = SampleMin.X; X <= SampleMax.X; ++X)
			{
				const FVector LocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
				float TargetDensity = 0.0f;
				float Weight = 1.0f;
				const FVector Offset = LocalPosition - Stamp.LocalCenter;
				const float Distance = Stamp.Shape == EDualContourBrushShape::Box
					                       ? FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z))
					                       : Offset.Length();
				Weight = EvaluateFalloff(Distance / Stamp.Radius, Stamp.Falloff, Stamp.FalloffType);
				if (Weight <= 0.0f)
					continue;
				if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
				{
					const int32 SX = X - static_cast<int32>(BoundsMin.X);
					const int32 SY = Y - static_cast<int32>(BoundsMin.Y);
					const int32 SZ = Z - static_cast<int32>(BoundsMin.Z);
					TargetDensity = SmoothedValues[DualContourUtils::LinearIndex(SmoothDims, SX, SY, SZ)];
				}
				else if (Stamp.Operation == EDualContourDensityEditOperation::Erase)
				{
					TargetDensity = RestoreSource->GetDensity(X, Y, Z);
				}
				else if (Stamp.bUseClayBrush)
				{
					const float SignedDistance = FVector::DotProduct(LocalPosition - Stamp.ClayPlaneOrigin,
						Stamp.LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
					TargetDensity = GDualContourIsoValue - SignedDistance * (128.0f / FMath::Max(DualContour->CellSize, UE_SMALL_NUMBER));
				}
				else
				{
					TargetDensity = GDualContourIsoValue + (255.0f - GDualContourIsoValue) * (1.0f - Distance / Stamp.Radius);
				}

				const float OldDensity = GetWorkingDensity(X, Y, Z);
				float CombinedDensity = TargetDensity;
				if (Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract
				    || Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
				{
					const float DifferenceDensity = TargetDensity >= GDualContourIsoValue
						                                ? 2.0f * GDualContourIsoValue - TargetDensity
						                                : 255.0f;
					CombinedDensity = FMath::Min(OldDensity, DifferenceDensity);
				}
				else if (Stamp.Operation == EDualContourDensityEditOperation::Sculpt
				         || Stamp.Operation == EDualContourDensityEditOperation::StampUnion)
				{
					CombinedDensity = FMath::Max(OldDensity, TargetDensity);
				}
				const float NewDensity = FMath::Lerp(OldDensity, CombinedDensity, OperationWeight * Weight);
				if (FMath::IsNearlyEqual(NewDensity, OldDensity, KINDA_SMALL_NUMBER))
					continue;
				SetWorkingDensity(X, Y, Z, NewDensity);
				bChanged = true;
			}
	return bChanged;
}

FDualContourBrushStamp UDualContourBrushTool::MakeStamp(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale) const
{
	FDualContourBrushStamp Stamp;
	const FTransform ActorTransform = TargetActor->GetActorTransform();
	const float ActorScale = FMath::Abs(ActorTransform.GetScale3D().X);
	Stamp.LocalCenter = ActorTransform.InverseTransformPosition(WorldPosition);
	Stamp.LocalNormal = ActorTransform.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Stamp.ClayPlaneOrigin = ClayPlaneOrigin;
	if (Settings->bUseClayBrush)
		Stamp.LocalNormal = ClayPlaneNormal;
	Stamp.Radius = Settings->BrushSize * 0.5f / FMath::Max(ActorScale, UE_SMALL_NUMBER);
	Stamp.Falloff = Settings->BrushFalloff;
	Stamp.FalloffType = Settings->BrushFalloffType;
	Stamp.Shape = Settings->BrushType;
	Stamp.Strength = Settings->ToolStrength;
	Stamp.TimeScale = TimeScale;
	Stamp.bUseClayBrush = Settings->bUseClayBrush;

	switch (Settings->ActiveTool)
	{
		case EDualContourEditTool::Erase:
			Stamp.Operation = EDualContourDensityEditOperation::Erase;
			break;
		case EDualContourEditTool::Smooth:
			Stamp.Operation = EDualContourDensityEditOperation::Smooth;
			break;
		case EDualContourEditTool::Brush:
			Stamp.Operation = bShiftDown ? EDualContourDensityEditOperation::StampDifference : EDualContourDensityEditOperation::StampUnion;
			Stamp.VolumeBrush = Settings->VolumeBrush.LoadSynchronous();
			if (Stamp.VolumeBrush)
			{
				const FVector SourceSize(Stamp.VolumeBrush->CellCount.X * Stamp.VolumeBrush->CellSize,
					Stamp.VolumeBrush->CellCount.Y * Stamp.VolumeBrush->CellSize, Stamp.VolumeBrush->CellCount.Z * Stamp.VolumeBrush->CellSize);
				const FVector SourcePivot = SourceSize * 0.5f;
				const FQuat Rotation = Settings->bAlignVolumeBrushToSurface
					                       ? FQuat::FindBetweenNormals(FVector::UpVector, Stamp.LocalNormal)
					                       : FQuat::Identity;
				const float LocalScale = Settings->VolumeBrushScale / FMath::Max(ActorScale, UE_SMALL_NUMBER);
				Stamp.VolumeToTarget = FTransform(Rotation, Stamp.LocalCenter - Rotation.RotateVector(SourcePivot * LocalScale), FVector(LocalScale));
			}
			break;
		default:
			Stamp.Operation = bShiftDown ? EDualContourDensityEditOperation::SculptSubtract : EDualContourDensityEditOperation::Sculpt;
			break;
	}
	return Stamp;
}

void UDualContourBrushTool::FlushStroke(bool bFinalFlush)
{
	if (!bStrokeActive || !TargetActor || !TargetActor->DualContour)
		return;
	FDualContourEditResult Result;
	if (TargetActor->DualContour->ApplyEditBatch(ActiveBatch, Result))
	{
		for (const FDualContourSampleDelta& Delta : Result.Deltas)
		{
			FDualContourSampleDelta* Existing = StrokeDeltas.Find(Delta.SampleCoord);
			if (Existing)
				Existing->After = Delta.After;
			else
				StrokeDeltas.Add(Delta.SampleCoord, Delta);
		}
	}
	if (!bFinalFlush)
		BeginEditBatch();
	LastPreviewFlushTime = FPlatformTime::Seconds();
}

void UDualContourBrushTool::FinishStroke(bool bCancel)
{
	if (!bStrokeActive)
		return;
	FlushStroke(true);
	bStrokeActive = false;
	if (TargetActor)
		TargetActor->SetDensityEditInProgress(false);
	if (!TargetActor || !TargetActor->DualContour || StrokeDeltas.IsEmpty())
		return;

	TArray<FDualContourSampleDelta> Deltas;
	StrokeDeltas.GenerateValueArray(Deltas);
	Deltas.RemoveAllSwap([](const FDualContourSampleDelta& Delta) { return Delta.Before == Delta.After; });
	if (Deltas.IsEmpty())
		return;
	if (bCancel)
	{
		TargetActor->DualContour->ApplyEditDeltas(Deltas, false);
	}
	else
	{
		TUniquePtr<FDualContourEditChange> Change = MakeUnique<FDualContourEditChange>();
		Change->Deltas = MoveTemp(Deltas);
		GetToolManager()->EmitObjectChange(TargetActor->DualContour, MoveTemp(Change), LOCTEXT("DensityEdit", "Edit Dual Contour"));
		TargetActor->DualContour->MarkPackageDirty();
		TargetActor->MarkPackageDirty();
	}
	StrokeDeltas.Reset();
}

bool UDualContourBrushTool::ProjectBrushPointToSurface(const FVector& PlanePoint, float ProjectionHalfDepth, FVector& OutSurfacePoint) const
{
	if (!TargetWorld || !TargetActor)
		return false;

	const FVector TraceOffset = HitNormal * ProjectionHalfDepth;
	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectQuery(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DualContourBrushProjection), true);
	TargetWorld->LineTraceMultiByObjectType(Hits, PlanePoint + TraceOffset, PlanePoint - TraceOffset, ObjectQuery, QueryParams);

	const FHitResult* ClosestHit = nullptr;
	float ClosestDistanceToPlane = TNumericLimits<float>::Max();
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() != TargetActor || !Hit.GetComponent() || !Hit.GetComponent()->IsA<UDualContourMeshComponent>())
			continue;

		const float DistanceToPlane = FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - PlanePoint, HitNormal));
		if (DistanceToPlane < ClosestDistanceToPlane)
		{
			ClosestDistanceToPlane = DistanceToPlane;
			ClosestHit = &Hit;
		}
	}

	if (!ClosestHit)
		return false;

	// Lift the line slightly off the mesh to avoid depth fighting while keeping it
	// depth-tested like the Landscape editor brush.
	OutSurfacePoint = ClosestHit->ImpactPoint
	                  + ClosestHit->ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, HitNormal) * 0.75f;
	return true;
}

void UDualContourBrushTool::DrawSurfaceProjectedFalloff(IToolsContextRenderAPI* RenderAPI, float Radius) const
{
	if (!RenderAPI || !BrushFalloffMaterial || Radius <= UE_SMALL_NUMBER)
		return;

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const FSceneView* SceneView = RenderAPI->GetSceneView();
	if (!PDI || !SceneView)
		return;

	FVector AxisX;
	FVector AxisY;
	HitNormal.FindBestAxisVectors(AxisX, AxisY);
	const FVector3f TangentX(AxisX);
	const FVector3f TangentY(AxisY);
	const FVector3f TangentZ(HitNormal);

	constexpr int32 RadialBandCount = 12;
	constexpr int32 AngularSegmentCount = 64;
	const float ProjectionHalfDepth = FMath::Max(50.0f, Settings->BrushSize * 0.75f);
	constexpr float MaximumOpacity = 0.16f;

	FDynamicMeshBuilder MeshBuilder(SceneView->GetFeatureLevel());
	MeshBuilder.ReserveVertices(1 + RadialBandCount * AngularSegmentCount);
	MeshBuilder.ReserveTriangles(AngularSegmentCount * (2 * RadialBandCount - 1));

	FVector SurfaceCenter;
	if (!ProjectBrushPointToSurface(HitPosition, ProjectionHalfDepth, SurfaceCenter))
		return;

	const FColor CenterColor = FLinearColor(1.0f, 1.0f, 1.0f, MaximumOpacity).ToFColor(false);
	const int32 CenterVertexIndex = MeshBuilder.AddVertex(
		FVector3f(SurfaceCenter), FVector2f(0.5f, 0.5f), TangentX, TangentY, TangentZ, CenterColor);

	TArray<int32> RingVertexIndices;
	RingVertexIndices.Init(INDEX_NONE, RadialBandCount * AngularSegmentCount);
	for (int32 RadialIndex = 1; RadialIndex <= RadialBandCount; ++RadialIndex)
	{
		const float NormalizedRadius = static_cast<float>(RadialIndex) / static_cast<float>(RadialBandCount);
		const float FalloffWeight = EvaluateFalloff(NormalizedRadius, Settings->BrushFalloff, Settings->BrushFalloffType);
		const FColor VertexColor = FLinearColor(1.0f, 1.0f, 1.0f, MaximumOpacity * FalloffWeight).ToFColor(false);

		for (int32 AngularIndex = 0; AngularIndex < AngularSegmentCount; ++AngularIndex)
		{
			const float Angle = 2.0f * UE_PI * static_cast<float>(AngularIndex) / static_cast<float>(AngularSegmentCount);
			const FVector RadialDirection = AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle);
			const FVector PlanePoint = HitPosition + Radius * NormalizedRadius * RadialDirection;
			FVector SurfacePoint;
			if (!ProjectBrushPointToSurface(PlanePoint, ProjectionHalfDepth, SurfacePoint))
				continue;

			const FVector2f UV(
				0.5f + 0.5f * NormalizedRadius * FMath::Cos(Angle),
				0.5f + 0.5f * NormalizedRadius * FMath::Sin(Angle));
			const int32 VertexIndex = MeshBuilder.AddVertex(
				FVector3f(SurfacePoint), UV, TangentX, TangentY, TangentZ, VertexColor);
			RingVertexIndices[(RadialIndex - 1) * AngularSegmentCount + AngularIndex] = VertexIndex;
		}
	}

	auto GetRingVertex = [&RingVertexIndices](int32 RadialIndex, int32 AngularIndex)
	{
		return RingVertexIndices[(RadialIndex - 1) * AngularSegmentCount + AngularIndex % AngularSegmentCount];
	};

	for (int32 AngularIndex = 0; AngularIndex < AngularSegmentCount; ++AngularIndex)
	{
		const int32 Outer0 = GetRingVertex(1, AngularIndex);
		const int32 Outer1 = GetRingVertex(1, AngularIndex + 1);
		if (Outer0 != INDEX_NONE && Outer1 != INDEX_NONE)
			MeshBuilder.AddTriangle(CenterVertexIndex, Outer1, Outer0);
	}

	for (int32 RadialIndex = 2; RadialIndex <= RadialBandCount; ++RadialIndex)
	{
		for (int32 AngularIndex = 0; AngularIndex < AngularSegmentCount; ++AngularIndex)
		{
			const int32 Inner0 = GetRingVertex(RadialIndex - 1, AngularIndex);
			const int32 Inner1 = GetRingVertex(RadialIndex - 1, AngularIndex + 1);
			const int32 Outer0 = GetRingVertex(RadialIndex, AngularIndex);
			const int32 Outer1 = GetRingVertex(RadialIndex, AngularIndex + 1);
			if (Inner0 == INDEX_NONE || Inner1 == INDEX_NONE || Outer0 == INDEX_NONE || Outer1 == INDEX_NONE)
				continue;

			MeshBuilder.AddTriangle(Inner0, Outer1, Outer0);
			MeshBuilder.AddTriangle(Inner0, Inner1, Outer1);
		}
	}

	MeshBuilder.Draw(
		PDI,
		FMatrix::Identity,
		BrushFalloffMaterial->GetRenderProxy(),
		SDPG_World,
		true,
		false);
}

void UDualContourBrushTool::DrawSurfaceProjectedRing(
	FPrimitiveDrawInterface* PDI,
	float Radius,
	const FLinearColor& Color,
	float Thickness) const
{
	if (!PDI || Radius <= UE_SMALL_NUMBER)
		return;

	FVector AxisX;
	FVector AxisY;
	HitNormal.FindBestAxisVectors(AxisX, AxisY);

	constexpr int32 SegmentCount = 64;
	const float ProjectionHalfDepth = FMath::Max(50.0f, Settings->BrushSize * 0.75f);
	FVector PreviousPoint = FVector::ZeroVector;
	bool bPreviousPointValid = false;

	for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
	{
		const float Angle = 2.0f * UE_PI * static_cast<float>(SegmentIndex % SegmentCount) / static_cast<float>(SegmentCount);
		const FVector PlanePoint = HitPosition + Radius * (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle));
		FVector SurfacePoint;
		const bool bSurfacePointValid = ProjectBrushPointToSurface(PlanePoint, ProjectionHalfDepth, SurfacePoint);

		if (bSurfacePointValid && bPreviousPointValid)
		{
			PDI->DrawLine(PreviousPoint, SurfacePoint, Color, SDPG_World, Thickness, 0.0f, true);
		}

		PreviousPoint = SurfacePoint;
		bPreviousPointValid = bSurfacePointValid;
	}
}

void UDualContourBrushTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!bHasHit || !Settings)
		return;

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const float Radius = Settings->BrushSize * 0.5f;
	const float FalloffRadius = Radius * (1.0f - Settings->BrushFalloff);
	const FLinearColor BrushRingColor(1.0f, 1.0f, 1.0f, 0.65f);

	DrawSurfaceProjectedFalloff(RenderAPI, Radius);

	// Landscape-style radius and falloff boundaries remain crisp above the
	// vertex-alpha gradient.
	DrawSurfaceProjectedRing(PDI, Radius, BrushRingColor, 1.0f);
	if (FalloffRadius > UE_SMALL_NUMBER)
	{
		DrawSurfaceProjectedRing(PDI, FalloffRadius, BrushRingColor, 1.0f);
	}
}

#undef LOCTEXT_NAMESPACE
