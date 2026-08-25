#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SSVTDensityFieldViewport;
class USVTDensityField;
class FToolBarBuilder;

enum class ESVTDensityFieldPreviewType : uint8
{
	SparseVolumeTexture,
	DensityField
};

class FSVTDensityFieldEditorToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, USVTDensityField* InAsset);
	USVTDensityField* GetAsset() const { return Asset; }
	ESVTDensityFieldPreviewType GetPreviewType() const { return PreviewType; }
	bool ShouldShowDualContourBounds() const { return bShowDualContourBounds; }

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FSVTDensityFieldEditorToolkit"); }

private:
	TSharedRef<SDockTab> SpawnViewportTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	void GenerateDensityField();
	bool CanGenerateDensityField() const;
	TSharedRef<SWidget> MakePreviewTypeMenu();
	void SetPreviewType(ESVTDensityFieldPreviewType InPreviewType);
	bool IsPreviewTypeSelected(ESVTDensityFieldPreviewType InPreviewType) const;
	FText GetPreviewTypeLabel() const;
	void ToggleDualContourBounds();
	void OnFinishedChangingProperties(const FPropertyChangedEvent& Event);

	static const FName ViewportTabId;
	static const FName DetailsTabId;
	TObjectPtr<USVTDensityField> Asset = nullptr;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SSVTDensityFieldViewport> Viewport;
	ESVTDensityFieldPreviewType PreviewType = ESVTDensityFieldPreviewType::SparseVolumeTexture;
	bool bShowDualContourBounds = true;
};
