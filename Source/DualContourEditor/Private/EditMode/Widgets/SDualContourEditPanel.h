#pragma once

#include "Widgets/SCompoundWidget.h"
#include "EditMode/DualContourEditModeSettings.h"

class IDetailsView;
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
	EDualContourBrushShape GetBrushShape() const;
	void SetBrushShape(EDualContourBrushShape Shape);
	EDualContourBrushFalloff GetBrushFalloffType() const;
	void SetBrushFalloffType(EDualContourBrushFalloff FalloffType);
	void HandleActiveToolChanged();

	TWeakObjectPtr<UDualContourEdMode> EditMode;
	TSharedPtr<IDetailsView> DetailsView;
};
