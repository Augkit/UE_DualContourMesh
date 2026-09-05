#include "EditMode/DualContourEdMode.h"

#include "EditMode/DualContourEdModeToolkit.h"
#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Tools/DualContourBrushTool.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "InteractiveToolManager.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "SceneManagement.h"
#include "HitProxies.h"

#define LOCTEXT_NAMESPACE "DualContourEdMode"

const FEditorModeID UDualContourEdMode::EM_DualContourEdModeId = TEXT("EM_DualContourEdMode");
const FString UDualContourEdMode::BrushToolName = TEXT("DualContourBrushTool");

namespace
{
const FName MaterialBrushGroupTag(TEXT("DualContourMaterialBrushGroup"));

AActor* GetOrCreateMaterialBrushGroup(UWorld* World, const FActorSpawnParameters& SpawnParameters)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(MaterialBrushGroupTag) && It->GetLevel() == SpawnParameters.OverrideLevel
			&& It->GetRootComponent())
			return *It;
	}
	AActor* Group = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!Group)
		return nullptr;
	Group->SetActorLabel(TEXT("Material Brush Volumes"));
	Group->Tags.Add(MaterialBrushGroupTag);
	Group->bIsEditorOnlyActor = true;
	USceneComponent* Root = NewObject<USceneComponent>(Group, TEXT("Root"), RF_Transactional);
	Root->SetMobility(EComponentMobility::Movable);
	Group->AddInstanceComponent(Root);
	Group->SetRootComponent(Root);
	Root->RegisterComponent();
	return Group;
}

void ShowMaterialRegionNotification(const FText& Text, SNotificationItem::ECompletionState State)
{
	FNotificationInfo Info(Text);
	Info.ExpireDuration = 4.0f;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		Item->SetCompletionState(State);
}
}

UDualContourEdMode::UDualContourEdMode()
{
	Settings = CreateDefaultSubobject<UDualContourEditModeSettings>(TEXT("DualContourEditModeSettings"));
	Settings->LoadConfig();
	Info = FEditorModeInfo(EM_DualContourEdModeId, LOCTEXT("ModeName", "Dual Contour"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.LandscapeMode"), TEXT("LevelEditor.LandscapeMode")), true);
}

void UDualContourEdMode::Enter()
{
	Super::Enter();
	BrushToolBuilder = NewObject<UDualContourBrushToolBuilder>(this);
	RefreshTarget();
	BrushToolBuilder->Initialize(Settings, TargetActor);
	GetToolManager()->RegisterToolType(BrushToolName, BrushToolBuilder);
	EnsureBrushToolActive();
}

void UDualContourEdMode::Exit()
{
	if (GetToolManager() && GetToolManager()->HasActiveTool(EToolSide::Left))
		GetToolManager()->DeactivateTool(EToolSide::Left, EToolShutdownType::Accept);
	if (bUseOverrideTarget && TargetActor)
	{
		TArray<ADualContourMaterialBrushVolume*> PreviewVolumes;
		GetMaterialBrushVolumes(PreviewVolumes);
		if (Owner && Owner->GetSelectedActors())
			Owner->GetSelectedActors()->DeselectAll();
		for (ADualContourMaterialBrushVolume* Volume : PreviewVolumes)
			if (IsValid(Volume))
				Volume->Destroy();
		// Preview regions and their empty organizational actor share the preview lifetime.
		for (TActorIterator<AActor> It(TargetActor->GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(MaterialBrushGroupTag))
				continue;
			TArray<AActor*> Children;
			It->GetAttachedActors(Children);
			if (Children.IsEmpty())
				It->Destroy();
		}
	}
	if (Settings)
		Settings->SaveConfig();
	TargetActor = nullptr;
	OverrideTargetActor = nullptr;
	bUseOverrideTarget = false;
	BrushToolBuilder = nullptr;
	Super::Exit();
}

void UDualContourEdMode::SetActiveTool(EDualContourEditTool InTool)
{
	if (!Settings || Settings->ActiveTool == InTool)
		return;
	Settings->ActiveTool = InTool;
	Settings->SaveConfig();
	ActiveToolChanged.Broadcast();
}

void UDualContourEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	TArray<ADualContourMaterialBrushVolume*> Volumes;
	GetMaterialBrushVolumes(Volumes);
	for (ADualContourMaterialBrushVolume* Volume : Volumes)
	{
		const bool bSelected = Owner && Owner->GetSelectedActors() && Owner->GetSelectedActors()->IsSelected(Volume);
		const FLinearColor Color = bSelected ? FLinearColor(1.0f, 0.65f, 0.1f) : FLinearColor(0.05f, 0.55f, 1.0f);
		const FMatrix Transform = Volume->GetActorTransform().ToMatrixWithScale();
		PDI->SetHitProxy(new HActor(Volume, nullptr));
		switch (Volume->Shape)
		{
		case EDualContourMaterialBrushVolumeShape::Box:
			DrawWireBox(PDI, Transform, FBox(-Volume->BoxExtent, Volume->BoxExtent), Color, SDPG_Foreground);
			break;
		case EDualContourMaterialBrushVolumeShape::Sphere:
			DrawCircle(PDI, Transform.GetOrigin(), Transform.GetScaledAxis(EAxis::X), Transform.GetScaledAxis(EAxis::Y), Color, Volume->SphereRadius, 64, SDPG_Foreground);
			DrawCircle(PDI, Transform.GetOrigin(), Transform.GetScaledAxis(EAxis::X), Transform.GetScaledAxis(EAxis::Z), Color, Volume->SphereRadius, 64, SDPG_Foreground);
			DrawCircle(PDI, Transform.GetOrigin(), Transform.GetScaledAxis(EAxis::Y), Transform.GetScaledAxis(EAxis::Z), Color, Volume->SphereRadius, 64, SDPG_Foreground);
			break;
		case EDualContourMaterialBrushVolumeShape::SplinePrism:
			if (Volume->Spline)
			{
				TArray<FVector2D> Polygon;
				Volume->GetSplinePolygon(Polygon);
				const FTransform& SplineTransform = Volume->Spline->GetComponentTransform();
				const double HalfHeight = Volume->SplineHeight * 0.5;
				for (int32 Index = 0; Index < Polygon.Num(); ++Index)
				{
					const FVector2D& A = Polygon[Index];
					const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
					const FVector Bottom = SplineTransform.TransformPosition(FVector(A.X, A.Y, -HalfHeight));
					const FVector Top = SplineTransform.TransformPosition(FVector(A.X, A.Y, HalfHeight));
					PDI->DrawLine(Bottom, SplineTransform.TransformPosition(FVector(B.X, B.Y, -HalfHeight)), Color, SDPG_Foreground);
					PDI->DrawLine(Top, SplineTransform.TransformPosition(FVector(B.X, B.Y, HalfHeight)), Color, SDPG_Foreground);
					// One upright per spline segment; the footprint has eight samples per segment.
					if (Index % 8 == 0)
						PDI->DrawLine(Bottom, Top, Color, SDPG_Foreground);
				}
			}
			break;
		}
		PDI->SetHitProxy(nullptr);
	}
}

void UDualContourEdMode::CreateMaterialBrushVolume(EDualContourMaterialBrushVolumeShape Shape)
{
	if (!TargetActor || !TargetActor->DualContour)
	{
		ShowMaterialRegionNotification(
			LOCTEXT("CannotCreateRegion", "Material regions require an editable Dual Contour target."),
			SNotificationItem::CS_Fail);
		return;
	}

	UWorld* World = TargetActor->GetWorld();
	if (!World)
		return;
	const FVector FieldSize(
		TargetActor->DualContour->CellCount.X * TargetActor->DualContour->CellSize,
		TargetActor->DualContour->CellCount.Y * TargetActor->DualContour->CellSize,
		TargetActor->DualContour->CellCount.Z * TargetActor->DualContour->CellSize);
	const FVector HalfSize = (FieldSize * 0.2).ComponentMax(FVector(TargetActor->DualContour->CellSize * 2.0));
	const FVector WorldCenter = TargetActor->GetActorTransform().TransformPosition(FieldSize * 0.5);

	TUniquePtr<FScopedTransaction> Transaction;
	if (!bUseOverrideTarget)
		Transaction = MakeUnique<FScopedTransaction>(
			LOCTEXT("CreateMaterialRegionTransaction", "Create Dual Contour Material Region"));
	World->Modify();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = TargetActor->GetLevel();
	SpawnParameters.ObjectFlags |= bUseOverrideTarget
		? (RF_Transient | RF_Transactional) : RF_Transactional;
	AActor* Group = GetOrCreateMaterialBrushGroup(World, SpawnParameters);
	if (!Group)
		return;
	ADualContourMaterialBrushVolume* BrushVolume = World->SpawnActor<ADualContourMaterialBrushVolume>(
		WorldCenter, TargetActor->GetActorRotation(), SpawnParameters);
	if (!BrushVolume)
		return;
	BrushVolume->SetActorScale3D(TargetActor->GetActorScale3D().GetAbs());
	BrushVolume->Initialize(TargetActor, Shape, HalfSize);
	TArray<ADualContourMaterialBrushVolume*> ExistingVolumes;
	GetMaterialBrushVolumes(ExistingVolumes);
	Group->Modify();
	for (ADualContourMaterialBrushVolume* Volume : ExistingVolumes)
	{
		if (Volume->GetLevel() == Group->GetLevel() && Volume->GetAttachParentActor() != Group)
		{
			Volume->Modify();
			Volume->AttachToActor(Group, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	TSet<FString> ExistingLabels;
	for (const ADualContourMaterialBrushVolume* ExistingVolume : ExistingVolumes)
		if (ExistingVolume != BrushVolume)
			ExistingLabels.Add(ExistingVolume->GetActorLabel());
	int32 RegionNumber = 1;
	FString RegionLabel;
	do
	{
		RegionLabel = FString::Printf(TEXT("Material Region %d"), RegionNumber++);
	}
	while (ExistingLabels.Contains(RegionLabel));
	BrushVolume->SetActorLabel(RegionLabel);
	BrushVolume->Modify();

	MaterialBrushVolumesChanged.Broadcast();
	SelectMaterialBrushVolume(BrushVolume);
}

bool UDualContourEdMode::HasSelectedMaterialBrushVolumes() const
{
	if (!Owner || !Owner->GetSelectedActors() || !TargetActor)
		return false;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
		if (const ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			if (Volume->TargetActor == TargetActor)
				return true;
	return false;
}

void UDualContourEdMode::GetMaterialBrushVolumes(
	TArray<ADualContourMaterialBrushVolume*>& OutVolumes) const
{
	OutVolumes.Reset();
	UWorld* World = TargetActor ? TargetActor->GetWorld() : nullptr;
	if (!World)
		return;
	for (TActorIterator<ADualContourMaterialBrushVolume> It(World); It; ++It)
		if (It->TargetActor == TargetActor)
			OutVolumes.Add(*It);
	OutVolumes.Sort([](const ADualContourMaterialBrushVolume& A, const ADualContourMaterialBrushVolume& B)
	{
		return A.GetActorLabel() < B.GetActorLabel();
	});
}

ADualContourMaterialBrushVolume* UDualContourEdMode::GetSelectedMaterialBrushVolume() const
{
	if (!Owner || !Owner->GetSelectedActors())
		return nullptr;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			if (Volume->TargetActor == TargetActor)
				return Volume;
	return nullptr;
}

void UDualContourEdMode::SelectMaterialBrushVolume(
	ADualContourMaterialBrushVolume* Volume,
	bool bAddToSelection)
{
	if (!Owner || !Owner->GetSelectedActors() || !IsValid(Volume) || Volume->TargetActor != TargetActor)
		return;
	USelection* Selection = Owner->GetSelectedActors();
	Selection->BeginBatchSelectOperation();
	if (!bAddToSelection)
		Selection->DeselectAll();
	Selection->Select(Volume, true);
	Selection->EndBatchSelectOperation();
	Owner->ActorSelectionChangeNotify();
	Owner->PivotLocation = Volume->GetActorLocation();
	Owner->SnappedLocation = Volume->GetActorLocation();
	Owner->SetShowWidget(true);
	if (Owner->GetWidgetMode() == UE::Widget::WM_None)
		Owner->SetWidgetMode(UE::Widget::WM_Translate);
	UpdateBrushToolInteractionMode();
	MaterialBrushSelectionChanged.Broadcast();
}

void UDualContourEdMode::ClearMaterialBrushVolumeSelection()
{
	if (!Owner || !Owner->GetSelectedActors())
		return;
	USelection* Selection = Owner->GetSelectedActors();
	TArray<ADualContourMaterialBrushVolume*> SelectedVolumes;
	for (FSelectionIterator It(*Selection); It; ++It)
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			SelectedVolumes.Add(Volume);
	if (SelectedVolumes.IsEmpty())
		return;
	Selection->BeginBatchSelectOperation();
	for (ADualContourMaterialBrushVolume* Volume : SelectedVolumes)
		Selection->Deselect(Volume);
	Selection->EndBatchSelectOperation();
	Owner->ActorSelectionChangeNotify();
	UpdateBrushToolInteractionMode();
	MaterialBrushSelectionChanged.Broadcast();
}

void UDualContourEdMode::DeleteMaterialBrushVolume(ADualContourMaterialBrushVolume* Volume)
{
	if (!IsValid(Volume) || Volume->TargetActor != TargetActor)
		return;
	TUniquePtr<FScopedTransaction> Transaction;
	if (!bUseOverrideTarget)
		Transaction = MakeUnique<FScopedTransaction>(
			LOCTEXT("DeleteMaterialRegionTransaction", "Delete Dual Contour Material Region"));
	if (Owner && Owner->GetSelectedActors())
		Owner->GetSelectedActors()->Deselect(Volume);
	Volume->Modify();
	Volume->Destroy();
	if (Owner)
		Owner->ActorSelectionChangeNotify();
	MaterialBrushVolumesChanged.Broadcast();
	MaterialBrushSelectionChanged.Broadcast();
}

void UDualContourEdMode::BeginMaterialBrushTransform()
{
	if (!Owner || !Owner->GetSelectedActors())
		return;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			if (Volume->TargetActor == TargetActor)
				Volume->Modify();
}

bool UDualContourEdMode::ApplyMaterialBrushTransformDelta(
	const FVector& Drag,
	const FRotator& Rotation,
	const FVector& Scale)
{
	if (!Owner || !Owner->GetSelectedActors())
		return false;
	bool bChanged = false;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
	{
		ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It);
		if (!Volume || Volume->TargetActor != TargetActor)
			continue;
		if (!Drag.IsNearlyZero())
			Volume->AddActorWorldOffset(Drag);
		if (!Rotation.IsNearlyZero())
			Volume->AddActorWorldRotation(Rotation);
		if (!Scale.IsNearlyZero())
			Volume->SetActorScale3D((Volume->GetActorScale3D() + Scale).ComponentMax(FVector(0.001)));
		Volume->PostEditMove(false);
		bChanged = true;
	}
	if (bChanged)
	{
		if (ADualContourMaterialBrushVolume* Selected = GetSelectedMaterialBrushVolume())
		{
			Owner->PivotLocation = Selected->GetActorLocation();
			Owner->SnappedLocation = Selected->GetActorLocation();
		}
		MaterialBrushSelectionChanged.Broadcast();
	}
	return bChanged;
}

void UDualContourEdMode::EndMaterialBrushTransform()
{
	if (!Owner || !Owner->GetSelectedActors())
		return;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			if (Volume->TargetActor == TargetActor)
				Volume->PostEditMove(true);
}

FVector UDualContourEdMode::GetWidgetLocation() const
{
	if (const ADualContourMaterialBrushVolume* Volume = GetSelectedMaterialBrushVolume())
		return Volume->GetActorLocation();
	return FVector::ZeroVector;
}

FVector UDualContourEdMode::GetWidgetNormalFromCurrentAxis(void* InData)
{
	switch (CurrentWidgetAxis)
	{
		case EAxisList::X:
			return FVector::ForwardVector;
		case EAxisList::Y:
			return FVector::RightVector;
		case EAxisList::Z:
			return FVector::UpVector;
		default:
			return FVector::ZeroVector;
	}
}

void UDualContourEdMode::ApplySelectedMaterialBrushVolumes()
{
	if (!Owner || !Owner->GetSelectedActors() || !TargetActor)
		return;

	TArray<ADualContourMaterialBrushVolume*> BrushVolumes;
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			if (Volume->TargetActor == TargetActor)
				BrushVolumes.Add(Volume);

	UDualContourBrushTool* BrushTool = GetToolManager()
		? Cast<UDualContourBrushTool>(GetToolManager()->GetActiveTool(EToolSide::Left))
		: nullptr;
	if (!BrushTool || BrushVolumes.IsEmpty())
	{
		ShowMaterialRegionNotification(
			LOCTEXT("NoSelectedRegions", "Select one or more material regions assigned to the current target."),
			SNotificationItem::CS_Fail);
		return;
	}

	const int32 ChangedSamples = BrushTool->ApplyMaterialBrushVolumes(BrushVolumes);
	ShowMaterialRegionNotification(
		ChangedSamples > 0
			? FText::Format(LOCTEXT("RegionApplied", "Applied material to {0} solid samples."), FText::AsNumber(ChangedSamples))
			: LOCTEXT("RegionNoChanges", "No solid samples inside the selected regions required a material change."),
		ChangedSamples > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_None);
}

void UDualContourEdMode::SetOverrideTargetActor(ADualContourMeshActor* InTargetActor)
{
	bUseOverrideTarget = InTargetActor != nullptr;
	OverrideTargetActor = InTargetActor;
	RefreshTarget();
	if (BrushToolBuilder)
		BrushToolBuilder->SetTargetActor(TargetActor);
	if (UDualContourBrushTool* Tool = GetToolManager()
		? Cast<UDualContourBrushTool>(GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr)
	{
		Tool->SetTargetActor(TargetActor);
	}
	EnsureBrushToolActive();
}

void UDualContourEdMode::ActorSelectionChangeNotify()
{
	RefreshTarget();
	if (BrushToolBuilder)
		BrushToolBuilder->SetTargetActor(TargetActor);
	if (UDualContourBrushTool* Tool = GetToolManager() ? Cast<UDualContourBrushTool>(GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr)
		Tool->SetTargetActor(TargetActor);
	EnsureBrushToolActive();
	UpdateBrushToolInteractionMode();
	MaterialBrushSelectionChanged.Broadcast();
}

void UDualContourEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FDualContourEdModeToolkit>();
}

void UDualContourEdMode::EnsureBrushToolActive()
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	if (!ToolManager || ToolManager->HasActiveTool(EToolSide::Left) || !TargetActor)
		return;
	if (ToolManager->SelectActiveToolType(EToolSide::Left, BrushToolName))
		ToolManager->ActivateTool(EToolSide::Left);
}

void UDualContourEdMode::UpdateBrushToolInteractionMode()
{
	if (UDualContourBrushTool* Tool = GetToolManager()
		? Cast<UDualContourBrushTool>(GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr)
	{
		Tool->SetMaterialRegionTransformMode(HasSelectedMaterialBrushVolumes());
	}
}

void UDualContourEdMode::RefreshTarget()
{
	TargetActor = nullptr;
	if (!GEditor)
	{
		TargetStatus = LOCTEXT("NoEditor", "Editor context is unavailable.");
		return;
	}
	if (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)
	{
		TargetStatus = LOCTEXT("PIE", "Editing is disabled during PIE or SIE.");
		return;
	}
	if (bUseOverrideTarget)
	{
		ADualContourMeshActor* Candidate = OverrideTargetActor;
		if (!Candidate || !Candidate->DualContour)
		{
			TargetStatus = LOCTEXT("NoPreviewData", "The preview has no editable Dual Contour data.");
			return;
		}
		if (!Candidate->DualContour->HasCurrentGeneratedData())
		{
			TargetStatus = LOCTEXT("PreviewRebuild", "Generate the Dual Contour before editing it in the preview.");
			return;
		}
		FString DivisionStatus;
		if (!Candidate->ValidateDivisions(DivisionStatus))
		{
			TargetStatus = FText::FromString(DivisionStatus);
			return;
		}
		TargetActor = Candidate;
		TargetStatus = LOCTEXT("PreviewReady", "Editing the asset directly in the preview. Hold Shift to invert the active brush.");
		return;
	}

	TArray<ADualContourMeshActor*> SelectedActors;
	TArray<ADualContourMaterialBrushVolume*> SelectedBrushVolumes;
	USelection* ActorSelection = Owner ? Owner->GetSelectedActors() : GEditor->GetSelectedActors();
	for (FSelectionIterator It(*ActorSelection); It; ++It)
	{
		if (ADualContourMeshActor* Actor = Cast<ADualContourMeshActor>(*It))
			SelectedActors.Add(Actor);
		else if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(*It))
			SelectedBrushVolumes.Add(Volume);
	}
	if (SelectedActors.IsEmpty() && !SelectedBrushVolumes.IsEmpty())
	{
		ADualContourMeshActor* RegionTarget = SelectedBrushVolumes[0]->TargetActor;
		for (const ADualContourMaterialBrushVolume* Volume : SelectedBrushVolumes)
			if (Volume->TargetActor != RegionTarget)
			{
				TargetStatus = LOCTEXT("MixedRegionTargets", "Selected material regions belong to different Dual Contour actors.");
				return;
			}
		if (RegionTarget)
			SelectedActors.Add(RegionTarget);
	}
	if (SelectedActors.IsEmpty())
	{
		TargetStatus = LOCTEXT("NoTarget", "Select one DualContourMeshActor.");
		return;
	}
	if (SelectedActors.Num() != 1)
	{
		TargetStatus = LOCTEXT("MultipleTargets", "Select only one DualContourMeshActor.");
		return;
	}

	ADualContourMeshActor* Candidate = SelectedActors[0];
	if (!Candidate->DualContour)
	{
		TargetStatus = LOCTEXT("NoData", "The selected actor has no runtime DualContour instance.");
		return;
	}
	if (!Candidate->DualContour->HasCurrentGeneratedData())
	{
		TargetStatus = LOCTEXT("Rebuild", "Generation settings changed. Run Rebuild Mesh before editing.");
		return;
	}
	FString DivisionStatus;
	if (!Candidate->ValidateDivisions(DivisionStatus))
	{
		TargetStatus = FText::FromString(DivisionStatus);
		return;
	}
	const FVector Scale = Candidate->GetActorScale3D().GetAbs();
	if (!FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z))
	{
		TargetStatus = LOCTEXT("NonUniformScale", "Non-uniform actor scale is not supported. Use a uniform scale before editing.");
		return;
	}
	TargetActor = Candidate;
	TargetStatus = FText::Format(LOCTEXT("Ready", "Ready: {0}  |  Reset Dual Contour replaces unsaved instance edits."),
		FText::FromString(Candidate->GetActorLabel()));
}

#undef LOCTEXT_NAMESPACE
