#include "EditMode/DualContourEdModeToolkit.h"
#include "EditMode/DualContourEdMode.h"
#include "EditMode/Widgets/SDualContourEditPanel.h"
#include "EditMode/Widgets/DualContourToolPalette.h"

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
	DualContourToolPalette::Build(ToolbarBuilder, [WeakMode = EditMode] { return WeakMode; });
}

#undef LOCTEXT_NAMESPACE
