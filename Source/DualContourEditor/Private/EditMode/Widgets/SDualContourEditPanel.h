#pragma once

#include "Widgets/SCompoundWidget.h"
#include "DualContourEditorTypes.h"
#include "EditMode/DualContourEditModeSettings.h"

class IDetailsView;
class ADualContourMaterialBrushVolume;
class SVerticalBox;
class UDualContourEdMode;

class SDualContourEditPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDualContourEditPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UDualContourEdMode>, EditMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText GetTargetText() const;
	FSlateColor GetTargetColor() const;
	EVisibility GetBrushControlsVisibility() const;
	EVisibility GetMaterialRegionControlsVisibility() const;
	FReply CreateBoxMaterialRegion();
	FReply CreateSphereMaterialRegion();
	FReply CreateSplineMaterialRegion();
	FReply ApplySelectedMaterialRegions();
	FReply ResumeMaterialBrush();
	bool CanApplySelectedMaterialRegions() const;
	void RebuildMaterialRegionList();
	void HandleMaterialRegionSelectionChanged();
	FReply SelectMaterialRegion(TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume);
	FReply DeleteMaterialRegion(TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume);
	FText GetMaterialRegionLabel(TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume) const;
	EDualContourBrushShape GetBrushShape() const;
	void SetBrushShape(EDualContourBrushShape Shape);
	EDualContourBrushFalloff GetBrushFalloffType() const;
	void SetBrushFalloffType(EDualContourBrushFalloff FalloffType);
	void HandleActiveToolChanged();

	TWeakObjectPtr<UDualContourEdMode> EditMode;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<IDetailsView> RegionDetailsView;
	TSharedPtr<SVerticalBox> MaterialRegionList;
};
