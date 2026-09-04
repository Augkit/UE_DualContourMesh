#pragma once

#include "Toolkits/AssetEditorToolkit.h"
#include "Containers/Ticker.h"

class IDetailsView;
class SDualContourEditorViewport;
class UDualContour;
class UMaterialInterface;
class USVTDualContour;
class UVolumeSampledDualContour;
class FToolBarBuilder;
class FReply;
class SBox;
class UDualContourEdMode;
struct FPropertyChangedEvent;
struct FAssetData;

enum class ECheckBoxState : uint8;

enum class EDualContourEditorPreviewType : uint8
{
	SparseVolumeTexture,
	DualContour
};

enum class EDualContourEditorPreviewMaterialMode : uint8
{
	Default,
	MaterialId,
	Custom
};

class FDualContourEditorToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
	virtual ~FDualContourEditorToolkit() override;
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UDualContour* InAsset);
	UDualContour* GetAsset() const { return Asset; }
	USVTDualContour* GetSVTDualContour() const;
	UVolumeSampledDualContour* GetVolumeSampledDualContour() const;
	EDualContourEditorPreviewType GetPreviewType() const { return PreviewType; }
	UMaterialInterface* GetPreviewMaterial() const;
	bool ShouldShowDualContourBounds() const { return bShowDualContourBounds; }
	bool IsEditModeEnabled() const { return bEditModeEnabled; }

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDualContourEditorToolkit"); }

private:
	virtual void CreateEditorModeManager() override;
	TSharedRef<SDockTab> SpawnViewportTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	FReply OnGenerateDualContourClicked();
	void GenerateDualContour();
	bool CanGenerateDualContour() const;
	void HandleCellsRebuilt(FIntVector CellMin, FIntVector CellMax);
	void HandlePreviewMeshComponentsUpdated();
	bool StartGeneration(float DeltaTime);
	bool TickGenerationProgress(float DeltaTime);
	bool TickPreviewMeshUpdates(float DeltaTime);
	EVisibility GetGenerationProgressVisibility() const;
	TOptional<float> GetGenerationProgress() const;
	void HandleFinishedChangingProperties(const FPropertyChangedEvent&);
	ECheckBoxState GetAutoGenerateCheckState() const;
	void HandleAutoGenerateCheckStateChanged(ECheckBoxState NewState);
	TSharedRef<SWidget> MakePreviewTypeMenu();
	void SetPreviewType(EDualContourEditorPreviewType InPreviewType);
	bool IsPreviewTypeSelected(EDualContourEditorPreviewType InPreviewType) const;
	FText GetPreviewTypeLabel() const;
	TSharedRef<SWidget> MakePreviewMaterialMenu();
	void SetPreviewMaterialMode(EDualContourEditorPreviewMaterialMode InMode);
	bool IsPreviewMaterialModeSelected(EDualContourEditorPreviewMaterialMode InMode) const;
	FText GetPreviewMaterialModeLabel() const;
	EVisibility GetCustomPreviewMaterialVisibility() const;
	FString GetPreviewMaterialPath() const;
	void HandlePreviewMaterialChanged(const FAssetData& AssetData);
	void ToggleDualContourBounds();
	void SetInteractionMode(bool bEnableEditing);
	bool CanEnableEditMode() const;
	bool IsInteractionModeSelected(bool bEditing) const;
	void SetActiveEditTool(uint8 ToolValue);
	bool IsEditToolSelected(uint8 ToolValue) const;
	bool CanUseEditTools() const;
	UDualContourEdMode* GetActiveEditMode() const;
	void RefreshEditPanel();

	static const FName ViewportTabId;
	static const FName DetailsTabId;
	TObjectPtr<UDualContour> Asset = nullptr;
	TObjectPtr<UMaterialInterface> MaterialIdPreviewMaterial = nullptr;
	TObjectPtr<UMaterialInterface> CustomPreviewMaterial = nullptr;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SDualContourEditorViewport> Viewport;
	TSharedPtr<SBox> EditPanelContainer;
	bool bGenerationInProgress = false;
	bool bGenerationCompletionRequested = false;
	bool bContourCellsReadyForGeneration = false;
	float GenerationProgress = 0.0f;
	float GenerationProgressTarget = 0.0f;
	FTSTicker::FDelegateHandle GenerationTickerHandle;
	FTSTicker::FDelegateHandle PreviewMeshTickerHandle;
	EDualContourEditorPreviewType PreviewType = EDualContourEditorPreviewType::SparseVolumeTexture;
	EDualContourEditorPreviewMaterialMode PreviewMaterialMode = EDualContourEditorPreviewMaterialMode::Default;
	bool bShowDualContourBounds = true;
	bool bEditModeEnabled = false;
};
