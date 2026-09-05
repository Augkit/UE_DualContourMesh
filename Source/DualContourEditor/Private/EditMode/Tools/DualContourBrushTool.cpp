#include "EditMode/Tools/DualContourBrushTool.h"

#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "EditMode/Editing/DualContourBrushOperations.h"
#include "VolumeSampledDualContour.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "InteractiveToolManager.h"
#include "Engine/World.h"
#include "DynamicMeshBuilder.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneView.h"
#include "ToolContextInterfaces.h"

#define LOCTEXT_NAMESPACE "DualContourBrushTool"

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
	bHasActiveRay = false;
	bFlattenHeightLocked = false;
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
		if (bFlattenHeightLocked && Settings && Settings->ActiveTool == EDualContourEditTool::Flatten)
		{
			HitPosition.Z = FlattenWorldHeight;
			HitNormal = FVector::UpVector;
		}
		bHasHit = true;
		if (OutDistance)
			*OutDistance = Hit.Distance;
		return true;
	}
	return false;
}

void UDualContourBrushTool::SetMaterialRegionTransformMode(bool bEnabled)
{
	if (bMaterialRegionTransformMode == bEnabled)
		return;
	if (bEnabled && bStrokeActive)
		FinishStroke(false);
	bMaterialRegionTransformMode = bEnabled;
	if (bEnabled)
		bHasHit = false;
}

bool UDualContourBrushTool::UpdateSculptHitAlongNormal(
	const FVector& WorldPosition,
	const FVector& WorldNormal)
{
	bHasHit = false;
	if (!TargetWorld || !TargetActor || !Settings)
		return false;

	const FVector TraceNormal = WorldNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float TraceHalfDepth = FMath::Max(50.0f, Settings->BrushSize);
	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectQuery(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DualContourSculptNormalTrace), true);
	TargetWorld->LineTraceMultiByObjectType(
		Hits,
		WorldPosition + TraceNormal * TraceHalfDepth,
		WorldPosition - TraceNormal * TraceHalfDepth,
		ObjectQuery,
		QueryParams);

	const FHitResult* ClosestHit = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() != TargetActor || !Hit.GetComponent()
		    || !Hit.GetComponent()->IsA<UDualContourMeshComponent>())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Hit.ImpactPoint, WorldPosition);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestHit = &Hit;
		}
	}

	if (!ClosestHit)
		return false;

	HitPosition = ClosestHit->ImpactPoint;
	HitNormal = ClosestHit->ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, TraceNormal);
	if (FVector::DotProduct(HitNormal, TraceNormal) < 0.0f)
		HitNormal *= -1.0f;
	bHasHit = true;
	return true;
}

FInputRayHit UDualContourBrushTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	if (bMaterialRegionTransformMode)
		return FInputRayHit();
	float Distance = 0.0f;
	return UpdateHit(PressPos.WorldRay, &Distance) ? FInputRayHit(Distance) : FInputRayHit();
}

void UDualContourBrushTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (!UpdateHit(PressPos.WorldRay) || !TargetActor || !TargetActor->DualContour)
		return;
	if (!BeginPendingBatch())
		return;
	bStrokeActive = true;
	if (Settings->ActiveTool != EDualContourEditTool::PaintMaterial)
	{
		// The cursor and subsequent stamps must follow the preview mesh, so keep its
		// collision in sync instead of tracing against the surface from stroke start.
		TargetActor->SetDensityEditInProgress(true, true);
	}
	ActiveRayOrigin = PressPos.WorldRay.Origin;
	ActiveRayDirection = PressPos.WorldRay.Direction;
	bHasActiveRay = true;
	StrokeOrigin = HitPosition;
	StrokeNormal = HitNormal;
	bStationarySculptStroke = Settings->ActiveTool == EDualContourEditTool::Sculpt
	                         && !Settings->bUseClayBrush;
	bStationarySculptSubtract = bStationarySculptStroke && bShiftDown;
	StrokeGrowthDirection = bStationarySculptSubtract ? -StrokeNormal : StrokeNormal;
	bStrokeMoved = false;
	StationarySculptDistance = 0.0f;
	StationarySculptEmbedDepth = 0.0f;
	bFlattenHeightLocked = Settings->ActiveTool == EDualContourEditTool::Flatten;
	if (bFlattenHeightLocked)
	{
		FlattenWorldHeight = HitPosition.Z;
		const FTransform ActorTransform = TargetActor->GetActorTransform();
		FlattenPlaneOrigin = ActorTransform.InverseTransformPosition(HitPosition);
		FlattenPlaneNormal = ActorTransform.InverseTransformVectorNoScale(FVector::UpVector)
			.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		HitNormal = FVector::UpVector;
	}
	ClayPlaneOrigin = TargetActor->GetActorTransform().InverseTransformPosition(HitPosition);
	ClayPlaneNormal = TargetActor->GetActorTransform().InverseTransformVectorNoScale(HitNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	StrokeDeltas.Reset();
	MaterialStrokeDeltas.Reset();
	LastStampPosition = HitPosition;
	LastStampNormal = HitNormal;
	LastPreviewFlushTime = FPlatformTime::Seconds();
	StationaryAccumulator = 0.0f;
	if (bStationarySculptStroke)
	{
		const float ActorScale = FMath::Max(
			FMath::Abs(TargetActor->GetActorTransform().GetScale3D().X), UE_SMALL_NUMBER);
		const float WorldCellSize = TargetActor->DualContour->CellSize * ActorScale;
		const float WorldRadius = Settings->BrushSize * 0.5f;
		StationarySculptEmbedDepth = FMath::Max(WorldCellSize * 2.0f, WorldRadius * 0.25f);
		const float EmbedStampSpacing = FMath::Max(WorldCellSize, Settings->BrushSize * 0.15f);
		for (float Distance = -StationarySculptEmbedDepth; Distance < 0.0f; Distance += EmbedStampSpacing)
			ApplyStationarySculptStamp(Distance, 1.0f);
		StationarySculptDistance = 0.0f;
		ApplyStationarySculptStamp(StationarySculptDistance, 1.0f);
	}
	else
	{
		ApplyStampAt(HitPosition, HitNormal, 1.0f);
	}
	if (Settings->ActiveTool == EDualContourEditTool::Brush)
		FinishStroke(false);
}

bool UDualContourBrushTool::BeginPendingBatch()
{
	ActiveBatch = FDualContourPendingBatch();
	ActiveMaterialBatch = FDualContourPendingMaterialBatch();
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (!IsValid(DualContour) || !DualContour->HasCurrentGeneratedData())
		return false;

	if (Settings && Settings->ActiveTool == EDualContourEditTool::PaintMaterial)
	{
		ActiveMaterialBatch.bOpen = true;
		ActiveMaterialBatch.Owner = DualContour;
	}
	else
	{
		ActiveBatch.bOpen = true;
		ActiveBatch.Owner = DualContour;
	}
	return true;
}

void UDualContourBrushTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bStrokeActive)
		return;
	if (bStationarySculptStroke && !bStrokeMoved)
	{
		constexpr float RayOriginMoveToleranceSquared = 0.01f;
		constexpr float RayDirectionDotTolerance = 0.999999f;
		const bool bRayMoved = FVector::DistSquared(ActiveRayOrigin, DragPos.WorldRay.Origin) > RayOriginMoveToleranceSquared
		                       || FVector::DotProduct(ActiveRayDirection, DragPos.WorldRay.Direction) < RayDirectionDotTolerance;
		if (!bRayMoved)
		{
			// Collision updates can produce drag callbacks even though the physical
			// mouse ray is unchanged. Do not trace the growing cylinder in that case.
			return;
		}
		bStrokeMoved = true;
	}
	ActiveRayOrigin = DragPos.WorldRay.Origin;
	ActiveRayDirection = DragPos.WorldRay.Direction;
	bHasActiveRay = true;
	if (!UpdateHit(DragPos.WorldRay))
		return;
	ApplyPathTo(HitPosition, HitNormal);
	if (FPlatformTime::Seconds() - LastPreviewFlushTime >= FMath::Max(0.08f, Settings->PreviewUpdateInterval))
		FlushStroke(false);
}

void UDualContourBrushTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if (!bStrokeActive)
		return;
	ActiveRayOrigin = ReleasePos.WorldRay.Origin;
	ActiveRayDirection = ReleasePos.WorldRay.Direction;
	bHasActiveRay = true;
	if (UpdateHit(ReleasePos.WorldRay) && (!bStationarySculptStroke || bStrokeMoved))
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
	if (bMaterialRegionTransformMode)
		return FInputRayHit();
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
	if (!bStrokeActive || !Settings || !TargetActor || !TargetActor->DualContour || !Settings->bApplyWithoutMoving
	    || Settings->ActiveTool == EDualContourEditTool::Brush)
		return;

	if (bStationarySculptStroke && !bStrokeMoved)
	{
		StationaryAccumulator += DeltaTime;
		constexpr float FixedStep = 1.0f / 30.0f;
		while (StationaryAccumulator >= FixedStep)
		{
			const float PreviousDistance = StationarySculptDistance;
			const float NextDistance = PreviousDistance + Settings->SculptGrowthSpeed * FixedStep;
			const float ActorScale = FMath::Max(
				FMath::Abs(TargetActor->GetActorTransform().GetScale3D().X), UE_SMALL_NUMBER);
			const float WorldCellSize = TargetActor->DualContour->CellSize * ActorScale;
			const float MaximumSpatialStep = FMath::Max(
				1.0f, FMath::Min(WorldCellSize, Settings->BrushSize * 0.1f));
			const float TravelDistance = NextDistance - PreviousDistance;
			if (TravelDistance > UE_SMALL_NUMBER)
			{
				const int32 StepCount = FMath::Max(1, FMath::CeilToInt(TravelDistance / MaximumSpatialStep));
				for (int32 StepIndex = 1; StepIndex <= StepCount; ++StepIndex)
				{
					const float Alpha = static_cast<float>(StepIndex) / static_cast<float>(StepCount);
					ApplyStationarySculptStamp(FMath::Lerp(PreviousDistance, NextDistance, Alpha), 1.0f);
				}
				StationarySculptDistance = NextDistance;
			}
			StationaryAccumulator -= FixedStep;
		}
		HitPosition = StrokeOrigin + StrokeGrowthDirection * StationarySculptDistance;
		HitNormal = StrokeNormal;
		if (FPlatformTime::Seconds() - LastPreviewFlushTime >= FMath::Max(0.033f, Settings->PreviewUpdateInterval))
			FlushStroke(false);
		return;
	}

	// A Sculpt stroke that has moved follows the changing surface along its
	// normal. Other tools retain screen-ray positioning semantics.
	const bool bSurfaceNormalSculpt = Settings->ActiveTool == EDualContourEditTool::Sculpt
	                                 && !Settings->bUseClayBrush;
	if (bSurfaceNormalSculpt)
	{
		if (!UpdateSculptHitAlongNormal(LastStampPosition, LastStampNormal))
			return;
	}
	else if (!bHasActiveRay || !UpdateHit(FRay(ActiveRayOrigin, ActiveRayDirection)))
	{
		return;
	}
	StationaryAccumulator += DeltaTime;
	constexpr float FixedStep = 1.0f / 30.0f;
	while (StationaryAccumulator >= FixedStep)
	{
		ApplyStampAt(HitPosition, HitNormal, FixedStep);
		StationaryAccumulator -= FixedStep;
	}
	LastStampPosition = HitPosition;
	LastStampNormal = HitNormal;
	if (FPlatformTime::Seconds() - LastPreviewFlushTime >= FMath::Max(0.033f, Settings->PreviewUpdateInterval))
		FlushStroke(false);
}

void UDualContourBrushTool::ApplyPathTo(const FVector& WorldPosition, const FVector& WorldNormal)
{
	const float Spacing = FMath::Max(1.0f, Settings->BrushSize * 0.15f);
	const FVector PathStartPosition = LastStampPosition;
	const FVector PathStartNormal = LastStampNormal;
	const FVector Delta = WorldPosition - LastStampPosition;
	const float Distance = Delta.Length();
	if (Distance < Spacing)
		return;
	const int32 Steps = FMath::FloorToInt(Distance / Spacing);
	float LastAlpha = 0.0f;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		LastAlpha = FMath::Min(1.0f, Step * Spacing / Distance);
		const FVector StampNormal = FMath::Lerp(PathStartNormal, WorldNormal, LastAlpha)
		                            .GetSafeNormal(UE_SMALL_NUMBER, WorldNormal);
		ApplyStampAt(FMath::Lerp(PathStartPosition, WorldPosition, LastAlpha), StampNormal, 1.0f);
	}
	LastStampPosition = FMath::Lerp(PathStartPosition, WorldPosition, LastAlpha);
	LastStampNormal = FMath::Lerp(PathStartNormal, WorldNormal, LastAlpha)
	                  .GetSafeNormal(UE_SMALL_NUMBER, WorldNormal);
}

bool UDualContourBrushTool::ApplyStampAt(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale)
{
	if (!TargetActor || !TargetActor->DualContour)
		return false;
	const FDualContourBrushStamp Stamp = MakeStamp(WorldPosition, WorldNormal, TimeScale);
	return Settings && Settings->ActiveTool == EDualContourEditTool::PaintMaterial
		? DualContourBrushOperations::ApplyMaterialStamp(TargetActor->DualContour, ActiveMaterialBatch, Stamp,
			bShiftDown ? 0 : static_cast<uint8>(FMath::Clamp(Settings->PaintMaterialId, 0, 255)),
			Settings->MaterialPaintThreshold, Settings->bPaintSolidSamplesOnly)
		: DualContourBrushOperations::ApplyDensityStamp(TargetActor->DualContour, TargetActor->InitialDualContour, ActiveBatch, Stamp);
}

bool UDualContourBrushTool::ApplyStationarySculptStamp(float WorldDistance, float TimeScale)
{
	if (!TargetActor || !TargetActor->DualContour || !Settings)
		return false;

	const FVector StampPosition = StrokeOrigin + StrokeGrowthDirection * WorldDistance;
	FDualContourBrushStamp Stamp = MakeStamp(StampPosition, StrokeNormal, TimeScale);
	Stamp.Operation = bStationarySculptSubtract
		                  ? EDualContourDensityEditOperation::SculptSubtract
		                  : EDualContourDensityEditOperation::Sculpt;
	Stamp.bUseClayBrush = false;
	Stamp.bUseDirectionalFalloff = true;
	return DualContourBrushOperations::ApplyDensityStamp(TargetActor->DualContour, TargetActor->InitialDualContour, ActiveBatch, Stamp);
}

int32 UDualContourBrushTool::ApplyMaterialBrushVolumes(
	TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes)
{
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (bStrokeActive || !Settings || !IsValid(DualContour) || !DualContour->HasCurrentGeneratedData()
		|| BrushVolumes.IsEmpty())
	{
		return 0;
	}

	FDualContourPendingMaterialBatch Batch;
	Batch.bOpen = true;
	Batch.Owner = DualContour;
	DualContourBrushOperations::ApplyMaterialVolumes(TargetActor, Batch, BrushVolumes,
		static_cast<uint8>(FMath::Clamp(Settings->PaintMaterialId, 0, 255)));

	FDualContourMaterialEditResult Result;
	if (!DualContour->ApplyPendingMaterialBatch(Batch, Result))
		return 0;

	const int32 ChangedSampleCount = Result.Deltas.Num();
	TUniquePtr<FDualContourMaterialEditChange> Change = MakeUnique<FDualContourMaterialEditChange>();
	Change->Deltas = MoveTemp(Result.Deltas);
	GetToolManager()->EmitObjectChange(
		DualContour, MoveTemp(Change), LOCTEXT("MaterialRegionEdit", "Paint Dual Contour Material Region"));
	DualContour->MarkPackageDirty();
	TargetActor->MarkPackageDirty();
	return ChangedSampleCount;
}

FDualContourBrushStamp UDualContourBrushTool::MakeStamp(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale) const
{
	FDualContourBrushStamp Stamp;
	const FTransform ActorTransform = TargetActor->GetActorTransform();
	const float ActorScale = FMath::Abs(ActorTransform.GetScale3D().X);
	Stamp.LocalCenter = ActorTransform.InverseTransformPosition(WorldPosition);
	Stamp.LocalNormal = ActorTransform.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Stamp.ClayPlaneOrigin = ClayPlaneOrigin;
	Stamp.FlattenPlaneOrigin = FlattenPlaneOrigin;
	Stamp.FlattenPlaneNormal = FlattenPlaneNormal;
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
		case EDualContourEditTool::Flatten:
			Stamp.Operation = EDualContourDensityEditOperation::Flatten;
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
	if (Settings && Settings->ActiveTool == EDualContourEditTool::PaintMaterial)
	{
		FDualContourMaterialEditResult Result;
		if (TargetActor->DualContour->ApplyPendingMaterialBatch(ActiveMaterialBatch, Result))
		{
			for (const FDualContourMaterialSampleDelta& Delta : Result.Deltas)
			{
				FDualContourMaterialSampleDelta* Existing = MaterialStrokeDeltas.Find(Delta.SampleCoord);
				if (Existing) Existing->After = Delta.After;
				else MaterialStrokeDeltas.Add(Delta.SampleCoord, Delta);
			}
		}
	}
	else
	{
		TargetActor->DualContour->ApplyPendingBatch(ActiveBatch,
			[this](const FIntVector& SampleCoord, uint16 Before, uint16 After)
			{
				if (FDualContourSampleDelta* Existing = StrokeDeltas.Find(SampleCoord))
					Existing->After = After;
				else
					StrokeDeltas.Add(SampleCoord, FDualContourSampleDelta{SampleCoord, Before, After});
			});
	}
	if (!bFinalFlush)
		BeginPendingBatch();
	LastPreviewFlushTime = FPlatformTime::Seconds();
}

void UDualContourBrushTool::FinishStroke(bool bCancel)
{
	if (!bStrokeActive)
		return;
	FlushStroke(true);
	bStrokeActive = false;
	bHasActiveRay = false;
	bStationarySculptStroke = false;
	bStationarySculptSubtract = false;
	bStrokeMoved = false;
	StationarySculptEmbedDepth = 0.0f;
	bFlattenHeightLocked = false;
	if (TargetActor && (!Settings || Settings->ActiveTool != EDualContourEditTool::PaintMaterial))
		TargetActor->SetDensityEditInProgress(false);
	if (!TargetActor || !TargetActor->DualContour)
		return;
	if (Settings && Settings->ActiveTool == EDualContourEditTool::PaintMaterial)
	{
		TArray<FDualContourMaterialSampleDelta> Deltas;
		MaterialStrokeDeltas.GenerateValueArray(Deltas);
		Deltas.RemoveAllSwap([](const FDualContourMaterialSampleDelta& Delta) { return Delta.Before == Delta.After; });
		if (Deltas.IsEmpty()) return;
		if (bCancel) TargetActor->DualContour->ApplyMaterialEditDeltas(Deltas, false);
		else
		{
			TUniquePtr<FDualContourMaterialEditChange> Change = MakeUnique<FDualContourMaterialEditChange>();
			Change->Deltas = MoveTemp(Deltas);
			GetToolManager()->EmitObjectChange(TargetActor->DualContour, MoveTemp(Change), LOCTEXT("MaterialEdit", "Paint Dual Contour Material"));
			TargetActor->DualContour->MarkPackageDirty();
			TargetActor->MarkPackageDirty();
		}
		MaterialStrokeDeltas.Reset();
		return;
	}
	if (StrokeDeltas.IsEmpty()) return;

	TArray<FDualContourSampleDelta> Deltas;
	StrokeDeltas.GenerateValueArray(Deltas);
	Deltas.RemoveAllSwap([](const FDualContourSampleDelta& Delta) { return Delta.Before == Delta.After; });
	if (Deltas.IsEmpty())
		return;
	if (bCancel)
	{
		DualContourEditing::ApplyDeltas(*TargetActor->DualContour, Deltas, false);
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
	if (bFlattenHeightLocked && Settings && Settings->ActiveTool == EDualContourEditTool::Flatten)
	{
		OutSurfacePoint = FVector(PlanePoint.X, PlanePoint.Y, FlattenWorldHeight + 0.75f);
		return true;
	}

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
		const float FalloffWeight = DualContourBrushOperations::EvaluateFalloff(NormalizedRadius, Settings->BrushFalloff, Settings->BrushFalloffType);
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
		SDPG_Foreground,
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
			PDI->DrawLine(PreviousPoint, SurfacePoint, Color, SDPG_Foreground, Thickness, 0.0f, true);
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
