#include "EditMode/Tools/DualContourBrushTool.h"

#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "VolumeSampledDualContour.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "InteractiveToolManager.h"
#include "Engine/World.h"
#include "PrimitiveDrawingUtils.h"
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
		&& TargetActor->DualContour->ApplyBrushStamp(ActiveBatch, MakeStamp(WorldPosition, WorldNormal, TimeScale));
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
