#include "DualContourEditorViewport.h"

#include "DualContourEditorToolkit.h"
#include "SVTDualContour.h"
#include "VolumeSampledDualContour.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "EditMode/DualContourEdMode.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "PrimitiveDrawingUtils.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "Widgets/Layout/SBox.h"

namespace
{
UActorComponent* FindObjectPropertyOwner(AActor* Actor, FName PropertyName)
{
	if (!Actor)
		return nullptr;
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
		if (Component && Component->GetClass()->FindPropertyByName(PropertyName))
			return Component;
	return nullptr;
}

void SetObjectProperty(UObject* Object, FName Name, UObject* Value)
{
	if (Object)
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name))
			Property->SetObjectPropertyValue_InContainer(Object, Value);
}

void SetFloatProperty(UObject* Object, FName Name, float Value)
{
	if (Object)
		if (FFloatProperty* Property = FindFProperty<FFloatProperty>(Object->GetClass(), Name))
			Property->SetPropertyValue_InContainer(Object, Value);
}

void SetBoolProperty(UObject* Object, FName Name, bool Value)
{
	if (Object)
		if (FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name))
			Property->SetPropertyValue_InContainer(Object, Value);
}

void SetPreviewActorVisible(AActor* Actor, bool bVisible)
{
	if (!Actor)
		return;

	Actor->SetActorHiddenInGame(!bVisible);
	Actor->SetIsTemporarilyHiddenInEditor(!bVisible);

	TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent)
			continue;

		SceneComponent->SetVisibility(bVisible, false);
		SceneComponent->SetHiddenInGame(!bVisible, false);
		SceneComponent->SetIsTemporarilyHiddenInEditor(!bVisible);
		SceneComponent->MarkRenderStateDirty();
	}
}

FVector ComputeSVTDisplayedSize(ESVTDualContourFit Fit, const FVector& TargetSize, const FIntVector& SourceResolution)
{
	const FVector SafeSourceSize(
		FMath::Max(SourceResolution.X, 1),
		FMath::Max(SourceResolution.Y, 1),
		FMath::Max(SourceResolution.Z, 1));
	if (Fit == ESVTDualContourFit::Fill)
		return TargetSize;

	const FVector AxisScale = TargetSize / SafeSourceSize;
	const float UniformScale = Fit == ESVTDualContourFit::Contain
		                           ? FMath::Min3(AxisScale.X, AxisScale.Y, AxisScale.Z)
		                           : FMath::Max3(AxisScale.X, AxisScale.Y, AxisScale.Z);
	return SafeSourceSize * FMath::Max(UniformScale, UE_SMALL_NUMBER);
}
}

void SDualContourEditorViewport::Construct(const FArguments& InArgs)
{
	EditorToolkit = InArgs._EditorToolkit;
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
	PreviewScene->SetFloorVisibility(true);

	DensityActor = PreviewScene->GetWorld()->SpawnActor<ADualContourMeshActor>();
	if (DensityActor)
	{
		const TWeakPtr<SDualContourEditorViewport> WeakThis = SharedThis(this);
		DensityActor->OnMeshComponentsUpdated.AddLambda([WeakThis]()
		{
			if (const TSharedPtr<SDualContourEditorViewport> PinnedThis = WeakThis.Pin())
				PinnedThis->OnMeshComponentsUpdated.Broadcast();
		});
	}
	if (UClass* ViewerClass = FindObject<UClass>(nullptr, TEXT("/Script/Renderer.SparseVolumeTextureViewer")))
		SVTViewerActor = PreviewScene->GetWorld()->SpawnActor<AActor>(ViewerClass);

	RefreshPreview();
	ViewportClient->SetViewRotation(FRotator(-15.0, -135.0, 0.0));
	ViewportClient->FocusPreview();
}

TSharedPtr<SWidget> SDualContourEditorViewport::BuildViewportToolbar()
{
	const FName ToolbarName(TEXT("DualContourEditor.ViewportToolbar"));
	if (!UToolMenus::Get()->IsMenuRegistered(ToolbarName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(ToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
		Menu->StyleName = TEXT("ViewportToolbar");
		FToolMenuSection& Section = Menu->AddSection(TEXT("Left"));
		Section.AddEntry(UE::UnrealEd::CreateTransformsSubmenu());
		Section.AddEntry(UE::UnrealEd::CreateSnappingSubmenu());
	}

	FToolMenuContext MenuContext;
	MenuContext.AppendCommandList(GetCommandList());
	UUnrealEdViewportToolbarContext* Context = UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this));
	Context->AssetEditorToolkit = EditorToolkit;
	// Region transforms support grid snapping, but do not implement surface tracing.
	Context->bShowSurfaceSnap = false;
	MenuContext.AddObject(Context);
	return SNew(SBox)
		.Visibility_Lambda([WeakToolkit = EditorToolkit]()
		{
			const TSharedPtr<FDualContourEditorToolkit> Toolkit = WeakToolkit.Pin();
			return Toolkit && Toolkit->GetActiveEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			UToolMenus::Get()->GenerateWidget(ToolbarName, MenuContext)
		];
}

void SDualContourEditorViewport::RefreshPreview()
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	UDualContour* Asset = Toolkit ? Toolkit->GetAsset() : nullptr;
	USVTDualContour* SVTDualContour = Toolkit ? Toolkit->GetSVTDualContour() : nullptr;
	if (!Toolkit || !Asset)
		return;
	const UVolumeSampledDualContour* VolumeSampledDualContour = Toolkit->GetVolumeSampledDualContour();
	LastGenerationRevision = SVTDualContour
		                         ? SVTDualContour->GenerationRevision
		                         : VolumeSampledDualContour
		                         ? VolumeSampledDualContour->GenerationRevision
		                         : INDEX_NONE;
	const bool bPreviewDualContour = Toolkit->GetPreviewType() == EDualContourEditorPreviewType::DualContour;
	RefreshPreviewMaterial();

	const FVector FieldSize(Asset->CellCount.X * Asset->CellSize,
		Asset->CellCount.Y * Asset->CellSize,
		Asset->CellCount.Z * Asset->CellSize);
	const FVector HalfExtent(FieldSize.X * 0.5f, FieldSize.Y * 0.5f, FieldSize.Z * 0.5f);
	const FBox PreviewBounds(
		FVector(-HalfExtent.X, -HalfExtent.Y, 0.f),
		FVector(HalfExtent.X, HalfExtent.Y, FieldSize.Z));
	if (ViewportClient)
		ViewportClient->SetPreviewBounds(PreviewBounds);
	if (DensityActor)
	{
		// Keep displaying the last generated mesh while settings are dirty. Moving its static
		// component tree to match ungenerated settings can enqueue thousands of render updates.
		if (bPreviewDualContour && Asset->HasCurrentGeneratedData())
		{
			DensityActor->SetActorLocation(FVector(-FieldSize.X * 0.5f, -FieldSize.Y * 0.5f, 0.f));
			DensityActor->SetGeneratedDualContour(Asset);
		}
		SetPreviewActorVisible(DensityActor, bPreviewDualContour);
	}

	if (SVTViewerActor)
	{
		UActorComponent* ViewerComponent = FindObjectPropertyOwner(SVTViewerActor, TEXT("SparseVolumeTexturePreview"));
		SetObjectProperty(ViewerComponent, TEXT("SparseVolumeTexturePreview"),
			SVTDualContour ? SVTDualContour->SourceSparseVolumeTexture.Get() : nullptr);
		SetFloatProperty(ViewerComponent, TEXT("VoxelSize"), Asset->CellSize);
		SetBoolProperty(ViewerComponent, TEXT("bPivotAtCentroid"), true);
		SetBoolProperty(ViewerComponent, TEXT("bApplyPerFrameTransforms"), false);
		const FIntVector VolumeResolution = SVTDualContour && SVTDualContour->SourceSparseVolumeTexture
			                                    ? SVTDualContour->SourceSparseVolumeTexture->GetVolumeResolution()
			                                    : FIntVector::ZeroValue;
		const FVector SafeSourceSize(
			FMath::Max(VolumeResolution.X, 1),
			FMath::Max(VolumeResolution.Y, 1),
			FMath::Max(VolumeResolution.Z, 1));
		const FVector DisplayedSize = ComputeSVTDisplayedSize(
			SVTDualContour ? SVTDualContour->Fit : ESVTDualContourFit::Contain, FieldSize, VolumeResolution);
		const float SafeVoxelSize = FMath::Max(FMath::Abs(Asset->CellSize), UE_SMALL_NUMBER);
		const FVector FitScale = DisplayedSize / (SafeSourceSize * SafeVoxelSize);
		const FVector PreviewScale = FitScale * (SVTDualContour ? SVTDualContour->SVTScale : FVector::OneVector);
		SVTViewerActor->SetActorScale3D(PreviewScale);
		const float SVTHalfHeight = DisplayedSize.Z
		                            * FMath::Abs(SVTDualContour ? SVTDualContour->SVTScale.Z : 1.0) * 0.5f;
		SVTViewerActor->SetActorLocation(FVector(0.f, 0.f, SVTHalfHeight));
		if (ViewerComponent)
			ViewerComponent->MarkRenderStateDirty();
		SetPreviewActorVisible(SVTViewerActor, SVTDualContour && !bPreviewDualContour);
	}

	if (ViewportClient)
		ViewportClient->Invalidate();
}

void SDualContourEditorViewport::RefreshPreviewMaterial()
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	if (!DensityActor || !Toolkit)
		return;

	UMaterialInterface* PreviewMaterial = Toolkit->GetPreviewMaterial();
	DensityActor->MeshMaterial = PreviewMaterial;
	for (const TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : DensityActor->MeshComponents)
	{
		if (Pair.Value)
			Pair.Value->SetMaterial(0, PreviewMaterial);
	}
	if (ViewportClient)
		ViewportClient->Invalidate();
}

void SDualContourEditorViewport::InvalidatePreview()
{
	if (ViewportClient)
		ViewportClient->Invalidate();
}

void SDualContourEditorViewport::ProcessPendingMeshUpdates()
{
	if (DensityActor)
		DensityActor->ProcessPendingMeshUpdates();
}

bool SDualContourEditorViewport::SetEditingEnabled(bool bEnabled)
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	if (!Toolkit || !DensityActor)
		return false;

	FEditorModeTools& ModeManager = Toolkit->GetEditorModeManager();
	if (!bEnabled)
	{
		ModeManager.DeactivateMode(UDualContourEdMode::EM_DualContourEdModeId);
		return true;
	}

	UDualContour* Asset = Toolkit->GetAsset();
	if (!Asset || !Asset->HasCurrentGeneratedData())
		return false;

	// Erase restores the state that existed when this edit session began.
	DensityActor->InitialDualContour = DuplicateObject<UDualContour>(Asset, DensityActor);
	ModeManager.ActivateMode(UDualContourEdMode::EM_DualContourEdModeId);
	UDualContourEdMode* Mode = Cast<UDualContourEdMode>(
		ModeManager.GetActiveScriptableMode(UDualContourEdMode::EM_DualContourEdModeId));
	if (!Mode)
		return false;
	Mode->SetOverrideTargetActor(DensityActor);
	return Mode->HasValidTarget();
}

void SDualContourEditorViewport::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	const USVTDualContour* SVTAsset = Toolkit ? Toolkit->GetSVTDualContour() : nullptr;
	const UVolumeSampledDualContour* VolumeAsset = Toolkit ? Toolkit->GetVolumeSampledDualContour() : nullptr;
	const int32 GenerationRevision = SVTAsset
		                                 ? SVTAsset->GenerationRevision
		                                 : VolumeAsset
		                                 ? VolumeAsset->GenerationRevision
		                                 : INDEX_NONE;
	if (GenerationRevision != INDEX_NONE && LastGenerationRevision != GenerationRevision)
		RefreshPreview();
}

void SDualContourEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DensityActor);
	Collector.AddReferencedObject(SVTViewerActor);
}

TSharedRef<FEditorViewportClient> SDualContourEditorViewport::MakeEditorViewportClient()
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	FEditorModeTools* ModeTools = Toolkit ? &Toolkit->GetEditorModeManager() : nullptr;
	ViewportClient = MakeShared<FDualContourEditorViewportClient>(PreviewScene.Get(), SharedThis(this), EditorToolkit, ModeTools);
	return ViewportClient.ToSharedRef();
}

FDualContourEditorViewportClient::FDualContourEditorViewportClient(
	FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InViewport,
	const TWeakPtr<FDualContourEditorToolkit>& InEditorToolkit, FEditorModeTools* InModeTools)
	: FEditorViewportClient(InModeTools, InPreviewScene, InViewport)
	  , EditorToolkit(InEditorToolkit)
{
	if (FAssetEditorModeManager* AssetModeManager = static_cast<FAssetEditorModeManager*>(InModeTools))
		AssetModeManager->SetPreviewScene(InPreviewScene);
	bSetListenerPosition = false;
	SetRealtime(true);
	SetViewMode(VMI_Lit);
	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;
	EngineShowFlags.SetGrid(true);
	ShowWidget(true);
	SetIsSimulateInEditorViewport(true);
}

bool FDualContourEditorViewportClient::ShouldOrbitCamera() const
{
	// Never use the asset-preview convention where an unmodified left drag orbits.
	// It conflicts with both material painting and legacy transform-widget dragging.
	// The base implementation keeps the standard editor Alt+drag orbit gesture.
	return FEditorViewportClient::ShouldOrbitCamera();
}

bool FDualContourEditorViewportClient::CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	const UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr;
	return Mode && Mode->GetSelectedMaterialBrushVolume();
}

bool FDualContourEditorViewportClient::CanCycleWidgetMode() const
{
	return CanSetWidgetMode(GetWidgetMode());
}

FVector FDualContourEditorViewportClient::GetWidgetLocation() const
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	const UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr;
	if (const ADualContourMaterialBrushVolume* Volume = Mode ? Mode->GetSelectedMaterialBrushVolume() : nullptr)
		return Volume->GetActorLocation();
	return FEditorViewportClient::GetWidgetLocation();
}

bool FDualContourEditorViewportClient::InputWidgetDelta(
	FViewport* InViewport,
	EAxisList::Type CurrentAxis,
	FVector& Drag,
	FRotator& Rot,
	FVector& Scale)
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr;
	if (CurrentAxis != EAxisList::None && Mode && Mode->ApplyMaterialBrushTransformDelta(Drag, Rot, Scale))
	{
		Invalidate();
		return true;
	}
	return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
}

void FDualContourEditorViewportClient::ProcessClick(
	FSceneView& View,
	HHitProxy* HitProxy,
	FKey Key,
	EInputEvent Event,
	uint32 HitX,
	uint32 HitY)
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr;
	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorHit = static_cast<HActor*>(HitProxy);
		if (ADualContourMaterialBrushVolume* Volume = Cast<ADualContourMaterialBrushVolume>(ActorHit->Actor))
		{
			if (Mode)
			{
				const FViewportClick Click(&View, this, Key, Event, HitX, HitY);
				Mode->SelectMaterialBrushVolume(Volume, Click.IsControlDown());
				Invalidate();
				return;
			}
		}
	}
	if (Key == EKeys::LeftMouseButton && Mode && Mode->HasSelectedMaterialBrushVolumes())
	{
		Mode->ClearMaterialBrushVolumeSelection();
		Invalidate();
		return;
	}
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FDualContourEditorViewportClient::TrackingStarted(
	const FInputEventState& InInputState,
	bool bIsDraggingWidget,
	bool bNudge)
{
	FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
	if (!bIsDraggingWidget)
		return;
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	if (UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr)
	{
		TransformTransaction = MakeUnique<FScopedTransaction>(
			NSLOCTEXT("DualContourEditorViewport", "TransformMaterialRegion", "Transform Dual Contour Material Region"));
		Mode->BeginMaterialBrushTransform();
	}
}

void FDualContourEditorViewportClient::TrackingStopped()
{
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	if (UDualContourEdMode* Mode = Toolkit ? Toolkit->GetActiveEditMode() : nullptr)
		Mode->EndMaterialBrushTransform();
	TransformTransaction.Reset();
	FEditorViewportClient::TrackingStopped();
}

bool FDualContourEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Key == EKeys::F && EventArgs.Event == IE_Pressed && PreviewBounds.IsValid)
	{
		FocusPreview();
		return true;
	}
	return FEditorViewportClient::InputKey(EventArgs);
}

void FDualContourEditorViewportClient::FocusPreview()
{
	if (PreviewBounds.IsValid)
		FocusViewportOnBox(PreviewBounds, true);
}

void FDualContourEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	if (PreviewScene && PreviewScene->GetWorld())
		PreviewScene->GetWorld()->Tick(IsRealtime() ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaSeconds);
}

void FDualContourEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	const TSharedPtr<FDualContourEditorToolkit> Toolkit = EditorToolkit.Pin();
	if (!Toolkit || !Toolkit->ShouldShowDualContourBounds() || !PreviewBounds.IsValid)
		return;

	DrawWireBox(PDI, PreviewBounds, FLinearColor(0.05f, 0.65f, 1.f), SDPG_World);
}
