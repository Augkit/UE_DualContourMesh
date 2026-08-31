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
#include "InteractiveToolManager.h"
#include "Engine/World.h"
#include "PrimitiveDrawingUtils.h"
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
	ActiveBatch = TargetActor->DualContour->BeginEditBatch();
	if (!ActiveBatch.bOpen)
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
	const float BaseStrength = FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	if (BaseStrength <= 0.0f)
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
	for (int32 Z = SampleMin.Z; Z <= SampleMax.Z; ++Z)
		for (int32 Y = SampleMin.Y; Y <= SampleMax.Y; ++Y)
			for (int32 X = SampleMin.X; X <= SampleMax.X; ++X)
			{
				const FVector LocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
				float TargetDensity = 0.0f;
				float Weight = 1.0f;
				if (bVolumeStamp)
				{
					const FVector SourcePosition = Stamp.VolumeToTarget.InverseTransformPosition(LocalPosition);
					const FVector SourceGrid = SourcePosition / Stamp.VolumeBrush->CellSize;
					if (SourceGrid.X < 0.0 || SourceGrid.Y < 0.0 || SourceGrid.Z < 0.0
					    || SourceGrid.X > Stamp.VolumeBrush->CellCount.X || SourceGrid.Y > Stamp.VolumeBrush->CellCount.Y
					    || SourceGrid.Z > Stamp.VolumeBrush->CellCount.Z)
						continue;
					TargetDensity = Stamp.VolumeBrush->TrilinearDensity(SourceGrid);
				}
				else
				{
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
				}

				const float OldDensity = GetWorkingDensity(X, Y, Z);
				float CombinedDensity = TargetDensity;
				const bool bDifference = Stamp.Operation == EDualContourDensityEditOperation::Erase
					                         || Stamp.Operation == EDualContourDensityEditOperation::StampDifference;
				if (bDifference)
				{
					const float DifferenceDensity = TargetDensity >= GDualContourIsoValue
						                                ? 2.0f * GDualContourIsoValue - TargetDensity
						                                : 255.0f;
					CombinedDensity = FMath::Min(OldDensity, DifferenceDensity);
				}
				else if (Stamp.Operation != EDualContourDensityEditOperation::Smooth)
				{
					CombinedDensity = FMath::Max(OldDensity, TargetDensity);
				}
				const float NewDensity = FMath::Lerp(OldDensity, CombinedDensity, BaseStrength * Weight);
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
		Stamp.Operation = bShiftDown ? EDualContourDensityEditOperation::Sculpt : EDualContourDensityEditOperation::Erase;
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
				? FQuat::FindBetweenNormals(FVector::UpVector, Stamp.LocalNormal) : FQuat::Identity;
			const float LocalScale = Settings->VolumeBrushScale / FMath::Max(ActorScale, UE_SMALL_NUMBER);
			Stamp.VolumeToTarget = FTransform(Rotation, Stamp.LocalCenter - Rotation.RotateVector(SourcePivot * LocalScale), FVector(LocalScale));
		}
		break;
	default:
		Stamp.Operation = bShiftDown ? EDualContourDensityEditOperation::Erase : EDualContourDensityEditOperation::Sculpt;
		break;
	}
	return Stamp;
}

void UDualContourBrushTool::FlushStroke(bool bFinalFlush)
{
	if (!bStrokeActive || !TargetActor || !TargetActor->DualContour)
		return;
	FDualContourEditResult Result;
	if (TargetActor->DualContour->EndEditBatch(ActiveBatch, Result))
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
		ActiveBatch = TargetActor->DualContour->BeginEditBatch();
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

void UDualContourBrushTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!bHasHit || !Settings)
		return;
	FLinearColor Color = FLinearColor::Green;
	if (Settings->ActiveTool == EDualContourEditTool::Erase)
		Color = FLinearColor::Red;
	else if (Settings->ActiveTool == EDualContourEditTool::Smooth)
		Color = FLinearColor(0.1f, 0.4f, 1.0f);
	else if (Settings->ActiveTool == EDualContourEditTool::Brush)
		Color = FLinearColor::Yellow;
	FVector AxisX;
	FVector AxisY;
	HitNormal.FindBestAxisVectors(AxisX, AxisY);
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const float Radius = Settings->BrushSize * 0.5f;
	DrawCircle(PDI, HitPosition, AxisX, AxisY, Color, Radius, 64, SDPG_Foreground, 1.5f, 0.0f, true);
	DrawCircle(PDI, HitPosition, AxisX, AxisY, Color * 0.65f, Radius * (1.0f - Settings->BrushFalloff), 64, SDPG_Foreground, 1.0f, 0.0f, true);
	PDI->DrawLine(HitPosition, HitPosition + HitNormal * FMath::Max(20.0f, Radius * 0.25f), Color, SDPG_Foreground, 1.5f);
}

#undef LOCTEXT_NAMESPACE
