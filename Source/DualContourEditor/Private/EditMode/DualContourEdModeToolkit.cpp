#include "EditMode/DualContourEdModeToolkit.h"
#include "EditMode/DualContourEdMode.h"
#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Widgets/SDualContourEditPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DualContourEdModeToolkit"

void FDualContourEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	EditMode = Cast<UDualContourEdMode>(InOwningMode.Get());
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	Panel = SNew(SDualContourEditPanel).EditMode(EditMode);
}

FText FDualContourEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("Name", "Dual Contour Edit Mode");
}

void FDualContourEdModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(TEXT("Sculpt"));
}

FText FDualContourEdModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{
	return LOCTEXT("SculptPalette", "Sculpt");
}

void FDualContourEdModeToolkit::BuildToolPalette(FName Palette, FToolBarBuilder& ToolbarBuilder)
{
	const FName StyleSetName = FAppStyle::GetAppStyleSetName();
	auto AddTool = [this, &ToolbarBuilder, StyleSetName](EDualContourEditTool Tool, const FText& Label,
		const FText& Tooltip, const FName IconName)
	{
		const TWeakObjectPtr<UDualContourEdMode> WeakMode = EditMode;
		const FUIAction Action(
			FExecuteAction::CreateLambda([WeakMode, Tool]
			{
				if (WeakMode.IsValid())
					WeakMode->SetActiveTool(Tool);
			}),
			FCanExecuteAction::CreateLambda([WeakMode]
			{
				return WeakMode.IsValid() && WeakMode->HasValidTarget();
			}),
			FIsActionChecked::CreateLambda([WeakMode, Tool]
			{
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
		LOCTEXT("EraseTooltip", "Restore the initial volume."), TEXT("LandscapeEditor.EraseTool"));
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
