#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SDualContourEditorViewport;
class UDualContour;
class USVTDualContour;
class UVolumeSampledDualContour;
class FToolBarBuilder;
class FReply;

enum class EDualContourEditorPreviewType : uint8
{
	SparseVolumeTexture,
	DualContour
};

class FDualContourEditorToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UDualContour* InAsset);
	UDualContour* GetAsset() const { return Asset; }
	USVTDualContour* GetSVTDualContour() const;
	UVolumeSampledDualContour* GetVolumeSampledDualContour() const;
	EDualContourEditorPreviewType GetPreviewType() const { return PreviewType; }
	bool ShouldShowDualContourBounds() const { return bShowDualContourBounds; }

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDualContourEditorToolkit"); }

private:
	TSharedRef<SDockTab> SpawnViewportTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	FReply OnGenerateDualContourClicked();
	void GenerateDualContour();
	bool CanGenerateDualContour() const;
	TSharedRef<SWidget> MakePreviewTypeMenu();
	void SetPreviewType(EDualContourEditorPreviewType InPreviewType);
	bool IsPreviewTypeSelected(EDualContourEditorPreviewType InPreviewType) const;
	FText GetPreviewTypeLabel() const;
	void ToggleDualContourBounds();

	static const FName ViewportTabId;
	static const FName DetailsTabId;
	TObjectPtr<UDualContour> Asset = nullptr;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SDualContourEditorViewport> Viewport;
	EDualContourEditorPreviewType PreviewType = EDualContourEditorPreviewType::SparseVolumeTexture;
	bool bShowDualContourBounds = true;
};
