#pragma once

#include "SEditorViewport.h"
#include "EditorViewportClient.h"

class ADualContourMeshActor;
class FAdvancedPreviewScene;
class FDualContourEditorToolkit;

class SDualContourEditorViewport : public SEditorViewport, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDualContourEditorViewport) {}
		SLATE_ARGUMENT(TWeakPtr<FDualContourEditorToolkit>, EditorToolkit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void RefreshPreview();
	void InvalidatePreview();
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDualContourEditorViewport"); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override { return nullptr; }

private:
	TWeakPtr<FDualContourEditorToolkit> EditorToolkit;
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FEditorViewportClient> ViewportClient;
	TObjectPtr<ADualContourMeshActor> DensityActor = nullptr;
	TObjectPtr<AActor> SVTViewerActor = nullptr;
	int32 LastGenerationRevision = INDEX_NONE;
};

class FDualContourEditorViewportClient : public FEditorViewportClient
{
public:
	FDualContourEditorViewportClient(FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InViewport,
		const TWeakPtr<FDualContourEditorToolkit>& InEditorToolkit);
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

private:
	TWeakPtr<FDualContourEditorToolkit> EditorToolkit;
};
