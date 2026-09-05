#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Tools/LegacyEdModeInterfaces.h"
#include "DualContourEdMode.generated.h"

class ADualContourMeshActor;
class ADualContourMaterialBrushVolume;
class UDualContourBrushToolBuilder;
class UDualContourEditModeSettings;
enum class EDualContourEditTool : uint8;
enum class EDualContourMaterialBrushVolumeShape : uint8;

UCLASS()
class UDualContourEdMode final : public UEdMode,
	public ILegacyEdModeWidgetInterface,
	public ILegacyEdModeViewportInterface
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_DualContourEdModeId;
	static const FString BrushToolName;

	UDualContourEdMode();
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void ActorSelectionChangeNotify() override;
	virtual void CreateToolkit() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	// ILegacyEdModeWidgetInterface: the asset preview uses this widget to transform temporary regions.
	virtual bool AllowWidgetMove() override { return false; }
	virtual bool CanCycleWidgetMode() const override { return HasSelectedMaterialBrushVolumes(); }
	virtual bool ShowModeWidgets() const override { return true; }
	virtual EAxisList::Type GetWidgetAxisToDraw(UE::Widget::EWidgetMode InWidgetMode) const override
	{
		return EAxisList::All;
	}
	virtual FVector GetWidgetLocation() const override;
	virtual bool ShouldDrawWidget() const override { return HasSelectedMaterialBrushVolumes(); }
	virtual bool UsesTransformWidget() const override { return HasSelectedMaterialBrushVolumes(); }
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override
	{
		return HasSelectedMaterialBrushVolumes() && CheckMode != UE::Widget::WM_None;
	}
	virtual FVector GetWidgetNormalFromCurrentAxis(void* InData) override;
	virtual void SetCurrentWidgetAxis(EAxisList::Type InAxis) override { CurrentWidgetAxis = InAxis; }
	virtual EAxisList::Type GetCurrentWidgetAxis() const override { return CurrentWidgetAxis; }
	virtual bool UsesPropertyWidgets() const override { return false; }
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override { return false; }
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override { return false; }

	// Disable the unconfigured generic asset-editor ITF gizmo and use the widget above.
	virtual bool RequiresLegacyViewportInteractions() const override { return true; }

	UDualContourEditModeSettings* GetSettings() const { return Settings; }
	ADualContourMeshActor* GetTargetActor() const { return TargetActor; }
	FText GetTargetStatus() const { return TargetStatus; }
	bool HasValidTarget() const { return TargetActor != nullptr; }
	bool IsEditingPreviewActor() const { return bUseOverrideTarget; }
	void SetOverrideTargetActor(ADualContourMeshActor* InTargetActor);
	void SetActiveTool(EDualContourEditTool InTool);
	void CreateMaterialBrushVolume(EDualContourMaterialBrushVolumeShape Shape);
	void ApplySelectedMaterialBrushVolumes();
	bool HasSelectedMaterialBrushVolumes() const;
	void GetMaterialBrushVolumes(TArray<ADualContourMaterialBrushVolume*>& OutVolumes) const;
	ADualContourMaterialBrushVolume* GetSelectedMaterialBrushVolume() const;
	void SelectMaterialBrushVolume(ADualContourMaterialBrushVolume* Volume, bool bAddToSelection = false);
	void ClearMaterialBrushVolumeSelection();
	void DeleteMaterialBrushVolume(ADualContourMaterialBrushVolume* Volume);
	void BeginMaterialBrushTransform();
	bool ApplyMaterialBrushTransformDelta(const FVector& Drag, const FRotator& Rotation, const FVector& Scale);
	void EndMaterialBrushTransform();
	FSimpleMulticastDelegate& OnActiveToolChanged() { return ActiveToolChanged; }
	FSimpleMulticastDelegate& OnMaterialBrushVolumesChanged() { return MaterialBrushVolumesChanged; }
	FSimpleMulticastDelegate& OnMaterialBrushSelectionChanged() { return MaterialBrushSelectionChanged; }

private:
	void RefreshTarget();
	void EnsureBrushToolActive();
	void UpdateBrushToolInteractionMode();

	UPROPERTY()
	TObjectPtr<UDualContourEditModeSettings> Settings;

	UPROPERTY()
	TObjectPtr<UDualContourBrushToolBuilder> BrushToolBuilder;

	UPROPERTY()
	TObjectPtr<ADualContourMeshActor> TargetActor;

	UPROPERTY()
	TObjectPtr<ADualContourMeshActor> OverrideTargetActor;

	bool bUseOverrideTarget = false;
	EAxisList::Type CurrentWidgetAxis = EAxisList::None;

	FText TargetStatus;
	FSimpleMulticastDelegate ActiveToolChanged;
	FSimpleMulticastDelegate MaterialBrushVolumesChanged;
	FSimpleMulticastDelegate MaterialBrushSelectionChanged;
};
