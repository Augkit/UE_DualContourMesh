#include "SVTDensityFieldEditorViewport.h"

#include "SVTDensityFieldEditorToolkit.h"
#include "SVTDensityField.h"
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

FVector ComputeSVTDisplayedSize(ESVTDensityFieldFit Fit, const FVector& TargetSize, const FIntVector& SourceResolution)
{
	const FVector SafeSourceSize(
		FMath::Max(SourceResolution.X, 1),
		FMath::Max(SourceResolution.Y, 1),
		FMath::Max(SourceResolution.Z, 1));
	if (Fit == ESVTDensityFieldFit::Fill)
		return TargetSize;

	const FVector AxisScale = TargetSize / SafeSourceSize;
	const float UniformScale = Fit == ESVTDensityFieldFit::Contain
		                           ? FMath::Min3(AxisScale.X, AxisScale.Y, AxisScale.Z)
		                           : FMath::Max3(AxisScale.X, AxisScale.Y, AxisScale.Z);
	return SafeSourceSize * FMath::Max(UniformScale, UE_SMALL_NUMBER);
}
}

void SSVTDensityFieldViewport::Construct(const FArguments& InArgs)
{
	EditorToolkit = InArgs._EditorToolkit;
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
	PreviewScene->SetFloorVisibility(true);

	DensityActor = PreviewScene->GetWorld()->SpawnActor<ADualContourMeshActor>();
	if (UClass* ViewerClass = FindObject<UClass>(nullptr, TEXT("/Script/Renderer.SparseVolumeTextureViewer")))
		SVTViewerActor = PreviewScene->GetWorld()->SpawnActor<AActor>(ViewerClass);

	RefreshPreview();
	ViewportClient->SetViewLocation(FVector(-800.f, -800.f, 550.f));
	ViewportClient->SetViewRotation(FRotator(-25.f, 45.f, 0.f));
}

void SSVTDensityFieldViewport::RefreshPreview()
{
	const TSharedPtr<FSVTDensityFieldEditorToolkit> Toolkit = EditorToolkit.Pin();
	USVTDensityField* Asset = Toolkit ? Toolkit->GetAsset() : nullptr;
	if (!Toolkit || !Asset || !Asset->DualContour)
		return;
	LastGenerationRevision = Asset->GenerationRevision;
	const bool bPreviewDensityField = Toolkit->GetPreviewType() == ESVTDensityFieldPreviewType::DensityField;

	const FVector FieldSize(Asset->DualContour->CellCount.X * Asset->DualContour->CellSize,
		Asset->DualContour->CellCount.Y * Asset->DualContour->CellSize,
		Asset->DualContour->CellCount.Z * Asset->DualContour->CellSize);
	if (DensityActor)
	{
		if (bPreviewDensityField)
		{
			DensityActor->SetDualContour(Asset->DualContour);
			DensityActor->SetActorLocation(FVector(-FieldSize.X * 0.5f, -FieldSize.Y * 0.5f, 0.f));
			DensityActor->RefreshMeshFromCurrentData();
		}
		SetPreviewActorVisible(DensityActor, bPreviewDensityField);
	}

	if (SVTViewerActor)
	{
		UActorComponent* ViewerComponent = FindObjectPropertyOwner(SVTViewerActor, TEXT("SparseVolumeTexturePreview"));
		SetObjectProperty(ViewerComponent, TEXT("SparseVolumeTexturePreview"), Asset->SourceSparseVolumeTexture.Get());
		SetFloatProperty(ViewerComponent, TEXT("VoxelSize"), Asset->DualContour->CellSize);
		SetBoolProperty(ViewerComponent, TEXT("bPivotAtCentroid"), true);
		SetBoolProperty(ViewerComponent, TEXT("bApplyPerFrameTransforms"), false);
		const FIntVector VolumeResolution = Asset->SourceSparseVolumeTexture
			                                    ? Asset->SourceSparseVolumeTexture->GetVolumeResolution()
			                                    : FIntVector::ZeroValue;
		const FVector SafeSourceSize(
			FMath::Max(VolumeResolution.X, 1),
			FMath::Max(VolumeResolution.Y, 1),
			FMath::Max(VolumeResolution.Z, 1));
		const FVector DisplayedSize = ComputeSVTDisplayedSize(Asset->Fit, FieldSize, VolumeResolution);
		const float SafeVoxelSize = FMath::Max(FMath::Abs(Asset->DualContour->CellSize), UE_SMALL_NUMBER);
		const FVector FitScale = DisplayedSize / (SafeSourceSize * SafeVoxelSize);
		const FVector PreviewScale = FitScale * Asset->SVTScale;
		SVTViewerActor->SetActorScale3D(PreviewScale);
		const float SVTHalfHeight = DisplayedSize.Z * FMath::Abs(Asset->SVTScale.Z) * 0.5f;
		SVTViewerActor->SetActorLocation(FVector(0.f, 0.f, SVTHalfHeight));
		if (ViewerComponent)
			ViewerComponent->MarkRenderStateDirty();
		SetPreviewActorVisible(SVTViewerActor, !bPreviewDensityField);
	}

	if (ViewportClient)
		ViewportClient->Invalidate();
}

void SSVTDensityFieldViewport::InvalidatePreview()
{
	if (ViewportClient)
		ViewportClient->Invalidate();
}

void SSVTDensityFieldViewport::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const TSharedPtr<FSVTDensityFieldEditorToolkit> Toolkit = EditorToolkit.Pin();
	const USVTDensityField* Asset = Toolkit ? Toolkit->GetAsset() : nullptr;
	if (Asset && LastGenerationRevision != Asset->GenerationRevision)
		RefreshPreview();
}

void SSVTDensityFieldViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DensityActor);
	Collector.AddReferencedObject(SVTViewerActor);
}

TSharedRef<FEditorViewportClient> SSVTDensityFieldViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FSVTDensityFieldViewportClient>(PreviewScene.Get(), SharedThis(this), EditorToolkit);
	return ViewportClient.ToSharedRef();
}

FSVTDensityFieldViewportClient::FSVTDensityFieldViewportClient(
	FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InViewport,
	const TWeakPtr<FSVTDensityFieldEditorToolkit>& InEditorToolkit)
	: FEditorViewportClient(nullptr, InPreviewScene, InViewport)
	  , EditorToolkit(InEditorToolkit)
{
	bSetListenerPosition = false;
	SetRealtime(true);
	EngineShowFlags.SetGrid(true);
}

void FSVTDensityFieldViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	if (PreviewScene && PreviewScene->GetWorld())
		PreviewScene->GetWorld()->Tick(IsRealtime() ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaSeconds);
}

void FSVTDensityFieldViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	const TSharedPtr<FSVTDensityFieldEditorToolkit> Toolkit = EditorToolkit.Pin();
	const USVTDensityField* Asset = Toolkit ? Toolkit->GetAsset() : nullptr;
	if (!Toolkit || !Toolkit->ShouldShowDualContourBounds() || !Asset || !Asset->DualContour)
		return;

	const FVector FieldSize(
		Asset->DualContour->CellCount.X * Asset->DualContour->CellSize,
		Asset->DualContour->CellCount.Y * Asset->DualContour->CellSize,
		Asset->DualContour->CellCount.Z * Asset->DualContour->CellSize);
	const FVector HalfExtent(FieldSize.X * 0.5f, FieldSize.Y * 0.5f, FieldSize.Z * 0.5f);
	const FBox Bounds(FVector(-HalfExtent.X, -HalfExtent.Y, 0.f), FVector(HalfExtent.X, HalfExtent.Y, FieldSize.Z));
	DrawWireBox(PDI, Bounds, FLinearColor(0.05f, 0.65f, 1.f), SDPG_World, 1.5f);
}
