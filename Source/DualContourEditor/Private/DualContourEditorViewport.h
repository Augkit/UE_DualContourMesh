#pragma once

#include "SEditorViewport.h"
#include "EditorViewportClient.h"

class ADualContourMeshActor;
class FAdvancedPreviewScene;
class FDualContourEditorToolkit;
class FDualContourEditorViewportClient;
class FEditorModeTools;
class FScopedTransaction;

DECLARE_MULTICAST_DELEGATE(FOnPreviewMeshComponentsUpdated);

class SDualContourEditorViewport : public SEditorViewport, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDualContourEditorViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDualContourEditorToolkit>, EditorToolkit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void RefreshPreview();
	void RefreshPreviewMaterial();
	void InvalidatePreview();
	/** Consumes generated mesh data even when this Slate viewport is not ticking. */
	void ProcessPendingMeshUpdates();
	bool SetEditingEnabled(bool bEnabled);
	ADualContourMeshActor* GetDensityActor() const { return DensityActor; }
	FOnPreviewMeshComponentsUpdated OnMeshComponentsUpdated;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDualContourEditorViewport"); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;

private:
	TWeakPtr<FDualContourEditorToolkit> EditorToolkit;
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FDualContourEditorViewportClient> ViewportClient;
	TObjectPtr<ADualContourMeshActor> DensityActor = nullptr;
	TObjectPtr<AActor> SVTViewerActor = nullptr;
	int32 LastGenerationRevision = INDEX_NONE;
};

class FDualContourEditorViewportClient : public FEditorViewportClient
{
public:
	FDualContourEditorViewportClient(FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InViewport,
		const TWeakPtr<FDualContourEditorToolkit>& InEditorToolkit, FEditorModeTools* InModeTools);
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool ShouldOrbitCamera() const override;
	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override;
	virtual bool CanCycleWidgetMode() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis,
		FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key,
		EInputEvent Event, uint32 HitX, uint32 HitY) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	void SetPreviewBounds(const FBox& InBounds) { PreviewBounds = InBounds; }
	void FocusPreview();

private:
	TWeakPtr<FDualContourEditorToolkit> EditorToolkit;
	FBox PreviewBounds = FBox(ForceInit);
	TUniquePtr<FScopedTransaction> TransformTransaction;
};
