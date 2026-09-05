#include "EditMode/Widgets/DualContourToolPalette.h"
#include "EditMode/DualContourEdMode.h"
#include "EditMode/DualContourEditModeSettings.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DualContourToolPalette"

void DualContourToolPalette::Build(FToolBarBuilder& ToolbarBuilder, TFunction<TWeakObjectPtr<UDualContourEdMode>()> GetMode)
{
	const FName StyleSetName = FAppStyle::GetAppStyleSetName();
	auto AddTool = [GetMode, &ToolbarBuilder, StyleSetName](EDualContourEditTool Tool, const FText& Label,
		const FText& Tooltip, const FName IconName)
	{
		const FUIAction Action(
			FExecuteAction::CreateLambda([GetMode, Tool]
			{
				const TWeakObjectPtr<UDualContourEdMode> WeakMode = GetMode();
				if (WeakMode.IsValid())
					WeakMode->SetActiveTool(Tool);
			}),
			FCanExecuteAction::CreateLambda([GetMode]
			{
				const TWeakObjectPtr<UDualContourEdMode> WeakMode = GetMode();
				return WeakMode.IsValid() && WeakMode->HasValidTarget();
			}),
			FIsActionChecked::CreateLambda([GetMode, Tool]
			{
				const TWeakObjectPtr<UDualContourEdMode> WeakMode = GetMode();
				return WeakMode.IsValid() && WeakMode->GetSettings()
				       && WeakMode->GetSettings()->ActiveTool == Tool;
			}));
		ToolbarBuilder.AddToolBarButton(Action, NAME_None, Label, Tooltip,
			FSlateIcon(StyleSetName, IconName), EUserInterfaceActionType::RadioButton);
	};

	AddTool(EDualContourEditTool::Sculpt, LOCTEXT("Sculpt", "Sculpt"),
		LOCTEXT("SculptTooltip", "Add volume. Hold Shift to remove volume."), TEXT("LandscapeEditor.SculptTool"));
	AddTool(EDualContourEditTool::Flatten, LOCTEXT("Flatten", "Flatten"),
		LOCTEXT("FlattenTooltip", "Pick a height on press, then raise or lower the swept surface toward it."),
		TEXT("LandscapeEditor.FlattenTool"));
	AddTool(EDualContourEditTool::Erase, LOCTEXT("Erase", "Erase"),
		LOCTEXT("EraseTooltip", "Restore the initial volume (the entry snapshot in asset preview)."), TEXT("LandscapeEditor.EraseTool"));
	AddTool(EDualContourEditTool::Smooth, LOCTEXT("Smooth", "Smooth"),
		LOCTEXT("SmoothTooltip", "Smooth the sampled density field."), TEXT("LandscapeEditor.SmoothTool"));
	AddTool(EDualContourEditTool::Brush, LOCTEXT("BrushStamp", "Brush Stamp"),
		LOCTEXT("BrushStampTooltip", "Stamp a Volume Sampled Dual Contour. Hold Shift for difference."),
		TEXT("LandscapeEditor.BlueprintBrushTool"));
	AddTool(EDualContourEditTool::PaintMaterial, LOCTEXT("PaintMaterial", "Paint Material"),
		LOCTEXT("PaintMaterialTooltip", "Paint a discrete material ID. Hold Shift to restore material ID 0."),
		TEXT("LandscapeEditor.PaintTool"));
}

#undef LOCTEXT_NAMESPACE
