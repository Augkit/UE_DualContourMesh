#pragma once

#include "Toolkits/BaseToolkit.h"

class UDualContourEdMode;

class FDualContourEdModeToolkit final : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual FName GetToolkitFName() const override { return TEXT("DualContourEdMode"); }
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return Panel; }
	virtual void GetToolPaletteNames(TArray<FName>& PaletteNames) const override;
	virtual FText GetToolPaletteDisplayName(FName Palette) const override;
	virtual void BuildToolPalette(FName Palette, FToolBarBuilder& ToolbarBuilder) override;
	virtual bool HasIntegratedToolPalettes() const override { return true; }

private:
	TSharedPtr<SWidget> Panel;
	TWeakObjectPtr<UDualContourEdMode> EditMode;
};
