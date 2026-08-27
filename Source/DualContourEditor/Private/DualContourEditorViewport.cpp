#include "DualContourEditorViewport.h"

#include "DualContourEditorToolkit.h"
#include "SVTDualContour.h"
#include "VolumeSampledDualContour.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "AdvancedPreviewScene.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "PrimitiveDrawingUtils.h"
#include "UObject/UnrealType.h"

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
	if (UClass* ViewerClass = FindObject<UClass>(nullptr, TEXT("/Script/Renderer.SparseVolumeTextureViewer")))
		SVTViewerActor = PreviewScene->GetWorld()->SpawnActor<AActor>(ViewerClass);

	RefreshPreview();
	ViewportClient->SetViewRotation(FRotator(-15.0, -135.0, 0.0));
	ViewportClient->FocusPreview();
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
		if (bPreviewDualContour)
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

void SDualContourEditorViewport::InvalidatePreview()
{
	if (ViewportClient)
		ViewportClient->Invalidate();
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
	ViewportClient = MakeShared<FDualContourEditorViewportClient>(PreviewScene.Get(), SharedThis(this), EditorToolkit);
	return ViewportClient.ToSharedRef();
}

FDualContourEditorViewportClient::FDualContourEditorViewportClient(
	FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InViewport,
	const TWeakPtr<FDualContourEditorToolkit>& InEditorToolkit)
	: FEditorViewportClient(nullptr, InPreviewScene, InViewport)
	  , EditorToolkit(InEditorToolkit)
{
	bSetListenerPosition = false;
	SetRealtime(true);
	SetViewMode(VMI_Lit);
	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;
	EngineShowFlags.SetGrid(true);
	ShowWidget(false);
	SetIsSimulateInEditorViewport(true);
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
	const UDualContour* Asset = Toolkit ? Toolkit->GetAsset() : nullptr;
	if (!Toolkit || !Toolkit->ShouldShowDualContourBounds() || !Asset)
		return;

	const FVector FieldSize(
		Asset->CellCount.X * Asset->CellSize,
		Asset->CellCount.Y * Asset->CellSize,
		Asset->CellCount.Z * Asset->CellSize);
	const FVector HalfExtent(FieldSize.X * 0.5f, FieldSize.Y * 0.5f, FieldSize.Z * 0.5f);
	const FBox Bounds(FVector(-HalfExtent.X, -HalfExtent.Y, 0.f), FVector(HalfExtent.X, HalfExtent.Y, FieldSize.Z));
	DrawWireBox(PDI, Bounds, FLinearColor(0.05f, 0.65f, 1.f), SDPG_World, 1.5f);
}
