#include "EditMode/Widgets/SDualContourEditPanel.h"

#include "EditMode/DualContourEdMode.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DualContourEditPanel"

void SDualContourEditPanel::Construct(const FArguments& InArgs)
{
	EditMode = InArgs._EditMode;
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bLockable = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
	if (EditMode.IsValid())
	{
		DetailsView->SetObject(EditMode->GetSettings());
		EditMode->OnActiveToolChanged().AddSP(this, &SDualContourEditPanel::HandleActiveToolChanged);
	}

	const FName AppStyleName = FAppStyle::GetAppStyleSetName();
	TSharedRef<SSegmentedControl<EDualContourBrushShape>> BrushShapeSelector =
		SNew(SSegmentedControl<EDualContourBrushShape>)
		.Value(this, &SDualContourEditPanel::GetBrushShape)
		.OnValueChanged(this, &SDualContourEditPanel::SetBrushShape)
		+ SSegmentedControl<EDualContourBrushShape>::Slot(EDualContourBrushShape::Sphere)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.CircleBrush")).GetIcon())
		.ToolTip(LOCTEXT("SphereBrushTooltip", "Spherical volume brush"))
		+ SSegmentedControl<EDualContourBrushShape>::Slot(EDualContourBrushShape::Box)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.ComponentBrush")).GetIcon())
		.ToolTip(LOCTEXT("BoxBrushTooltip", "Box volume brush"));

	TSharedRef<SSegmentedControl<EDualContourBrushFalloff>> FalloffSelector =
		SNew(SSegmentedControl<EDualContourBrushFalloff>)
		.Value(this, &SDualContourEditPanel::GetBrushFalloffType)
		.OnValueChanged(this, &SDualContourEditPanel::SetBrushFalloffType)
		+ SSegmentedControl<EDualContourBrushFalloff>::Slot(EDualContourBrushFalloff::Smooth)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.CircleBrush_Smooth")).GetIcon())
		.ToolTip(LOCTEXT("SmoothFalloffTooltip", "Smooth falloff"))
		+ SSegmentedControl<EDualContourBrushFalloff>::Slot(EDualContourBrushFalloff::Linear)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.CircleBrush_Linear")).GetIcon())
		.ToolTip(LOCTEXT("LinearFalloffTooltip", "Linear falloff"))
		+ SSegmentedControl<EDualContourBrushFalloff>::Slot(EDualContourBrushFalloff::Spherical)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.CircleBrush_Spherical")).GetIcon())
		.ToolTip(LOCTEXT("SphericalFalloffTooltip", "Spherical falloff"))
		+ SSegmentedControl<EDualContourBrushFalloff>::Slot(EDualContourBrushFalloff::Tip)
		.Icon(FSlateIcon(AppStyleName, TEXT("LandscapeEditor.CircleBrush_Tip")).GetIcon())
		.ToolTip(LOCTEXT("TipFalloffTooltip", "Tip falloff"));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.0f, 4.0f, 2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Info"))).ColorAndOpacity(this, &SDualContourEditPanel::GetTargetColor)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SDualContourEditPanel::GetTargetText).AutoWrapText(true).ColorAndOpacity(this, &SDualContourEditPanel::GetTargetColor)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SDualContourEditPanel::GetBrushControlsVisibility)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.0f)
				+ SGridPanel::Slot(0, 0).VAlign(VAlign_Center).Padding(0.0f, 2.0f, 12.0f, 2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("BrushType", "Brush Type"))
				]
				+ SGridPanel::Slot(1, 0).HAlign(HAlign_Left).Padding(0.0f, 2.0f)
				[
					BrushShapeSelector
				]
				+ SGridPanel::Slot(0, 1).VAlign(VAlign_Center).Padding(0.0f, 2.0f, 12.0f, 2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("BrushFalloff", "Brush Falloff"))
				]
				+ SGridPanel::Slot(1, 1).HAlign(HAlign_Left).Padding(0.0f, 2.0f)
				[
					FalloffSelector
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 2.0f)
		[
			DetailsView.ToSharedRef()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([WeakMode = EditMode]()
			{
				return WeakMode.IsValid() && WeakMode->IsEditingPreviewActor()
					? LOCTEXT("AssetWarning", "Generating from the source again replaces direct preview edits.")
					: LOCTEXT("SaveWarning", "Reset Dual Contour replaces instance edits with InitialDualContour.");
			})
			.ToolTipText_Lambda([WeakMode = EditMode]()
			{
				return WeakMode.IsValid() && WeakMode->IsEditingPreviewActor()
					? LOCTEXT("AssetWarningTooltip", "Preview edits are written directly to this asset. Re-running source generation replaces its generated density data.")
					: LOCTEXT("SaveWarningTooltip", "Edits are stored on the selected actor instance. Reset Dual Contour copies InitialDualContour again and replaces them.");
			})
			.AutoWrapText(true)
			.TextStyle(FAppStyle::Get(), TEXT("SmallText"))
			.ColorAndOpacity(FStyleColors::Warning)
		]
	];
}

EDualContourBrushShape SDualContourEditPanel::GetBrushShape() const
{
	return EditMode.IsValid() && EditMode->GetSettings() ? EditMode->GetSettings()->BrushType : EDualContourBrushShape::Sphere;
}

void SDualContourEditPanel::SetBrushShape(EDualContourBrushShape Shape)
{
	if (EditMode.IsValid() && EditMode->GetSettings())
	{
		EditMode->GetSettings()->BrushType = Shape;
		EditMode->GetSettings()->SaveConfig();
	}
}

EDualContourBrushFalloff SDualContourEditPanel::GetBrushFalloffType() const
{
	return EditMode.IsValid() && EditMode->GetSettings() ? EditMode->GetSettings()->BrushFalloffType : EDualContourBrushFalloff::Smooth;
}

void SDualContourEditPanel::SetBrushFalloffType(EDualContourBrushFalloff FalloffType)
{
	if (EditMode.IsValid() && EditMode->GetSettings())
	{
		EditMode->GetSettings()->BrushFalloffType = FalloffType;
		EditMode->GetSettings()->SaveConfig();
	}
}

void SDualContourEditPanel::HandleActiveToolChanged()
{
	if (DetailsView.IsValid())
		DetailsView->ForceRefresh();
}

FText SDualContourEditPanel::GetTargetText() const
{
	return EditMode.IsValid() ? EditMode->GetTargetStatus() : LOCTEXT("Inactive", "Dual Contour edit mode is inactive.");
}

FSlateColor SDualContourEditPanel::GetTargetColor() const
{
	return EditMode.IsValid() && EditMode->HasValidTarget() ? FStyleColors::Foreground : FStyleColors::Warning;
}

EVisibility SDualContourEditPanel::GetBrushControlsVisibility() const
{
	return EditMode.IsValid() && EditMode->GetSettings()
		&& EditMode->GetSettings()->ActiveTool == EDualContourEditTool::Brush ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
