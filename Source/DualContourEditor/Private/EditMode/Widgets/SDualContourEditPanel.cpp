#include "EditMode/Widgets/SDualContourEditPanel.h"

#include "EditMode/DualContourEdMode.h"
#include "DualContourMaterialBrushVolume.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "UObject/UnrealType.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "UnrealWidgetFwd.h"

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
	RegionDetailsView = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
	RegionDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Entry)
	{
		// Keep only brush inputs, including children of the BoxExtent vector.
		const FProperty& RootProperty = Entry.ParentProperties.IsEmpty() ? Entry.Property : *Entry.ParentProperties.Last();
		const FName Name = RootProperty.GetFName();
		return RootProperty.GetOwnerClass() == ADualContourMaterialBrushVolume::StaticClass()
			&& (Name == GET_MEMBER_NAME_CHECKED(ADualContourMaterialBrushVolume, Shape)
				|| Name == GET_MEMBER_NAME_CHECKED(ADualContourMaterialBrushVolume, BoxExtent)
				|| Name == GET_MEMBER_NAME_CHECKED(ADualContourMaterialBrushVolume, SphereRadius)
				|| Name == GET_MEMBER_NAME_CHECKED(ADualContourMaterialBrushVolume, SplineHeight));
	}));
	RegionDetailsView->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateSPLambda(this,
		[this](FName RowName, FName CategoryName)
		{
			// Component customizations add builders/groups that bypass the property filter
			// (Mobility, physics constraints, and spline point editing).
			if (CategoryName == TEXT("Selected Points"))
			{
				const ADualContourMaterialBrushVolume* Volume = EditMode.IsValid()
					? EditMode->GetSelectedMaterialBrushVolume() : nullptr;
				return Volume && Volume->Shape == EDualContourMaterialBrushVolumeShape::SplinePrism;
			}
			return CategoryName == TEXT("Material Brush") || CategoryName == TEXT("Shape");
		}));
	RegionDetailsView->OnFinishedChangingProperties().AddSPLambda(this,
		[this](const FPropertyChangedEvent& Event)
		{
			if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ADualContourMaterialBrushVolume, Shape))
			{
				// Rebuild custom rows after switching shape, outside the property callback.
				RegionDetailsView->RequestForceRefresh();
				RebuildMaterialRegionList();
			}
		});
	if (EditMode.IsValid())
	{
		DetailsView->SetObject(EditMode->GetSettings());
		EditMode->OnActiveToolChanged().AddSP(this, &SDualContourEditPanel::HandleActiveToolChanged);
		EditMode->OnMaterialBrushVolumesChanged().AddSP(this, &SDualContourEditPanel::RebuildMaterialRegionList);
		EditMode->OnMaterialBrushSelectionChanged().AddSP(this, &SDualContourEditPanel::HandleMaterialRegionSelectionChanged);
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
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SDualContourEditPanel::GetMaterialRegionControlsVisibility)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MaterialRegions", "Material Regions"))
					.TextStyle(FAppStyle::Get(), TEXT("DetailsView.CategoryTextStyle"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SButton).Text(LOCTEXT("PlaceBoxRegion", "Place Box"))
						.OnClicked(this, &SDualContourEditPanel::CreateBoxMaterialRegion)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f)
					[
						SNew(SButton).Text(LOCTEXT("PlaceSphereRegion", "Place Sphere"))
						.OnClicked(this, &SDualContourEditPanel::CreateSphereMaterialRegion)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton).Text(LOCTEXT("PlaceSplineRegion", "Place Spline"))
						.OnClicked(this, &SDualContourEditPanel::CreateSplineMaterialRegion)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SAssignNew(MaterialRegionList, SVerticalBox)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ResumeMaterialBrush", "Resume Brush"))
					.ToolTipText(LOCTEXT("ResumeMaterialBrushTooltip", "Clear the region selection and return mouse input to the material brush."))
					.IsEnabled(this, &SDualContourEditPanel::CanApplySelectedMaterialRegions)
					.OnClicked(this, &SDualContourEditPanel::ResumeMaterialBrush)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplySelectedRegions", "Apply Selected Regions"))
					.ToolTipText(LOCTEXT("ApplySelectedRegionsTooltip", "Paint the current Material Paint material into solid density samples inside the selected regions. The operation can be undone once."))
					.IsEnabled(this, &SDualContourEditPanel::CanApplySelectedMaterialRegions)
					.OnClicked(this, &SDualContourEditPanel::ApplySelectedMaterialRegions)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.MaxDesiredHeight(260.0f)
					.Visibility_Lambda([this]
					{
						return EditMode.IsValid() && EditMode->GetSelectedMaterialBrushVolume()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						RegionDetailsView.ToSharedRef()
					]
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

	RebuildMaterialRegionList();
	HandleMaterialRegionSelectionChanged();
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

EVisibility SDualContourEditPanel::GetMaterialRegionControlsVisibility() const
{
	return EditMode.IsValid() && EditMode->GetSettings()
		&& EditMode->GetSettings()->ActiveTool == EDualContourEditTool::PaintMaterial
		? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SDualContourEditPanel::CreateBoxMaterialRegion()
{
	if (EditMode.IsValid())
		EditMode->CreateMaterialBrushVolume(EDualContourMaterialBrushVolumeShape::Box);
	return FReply::Handled();
}

FReply SDualContourEditPanel::CreateSphereMaterialRegion()
{
	if (EditMode.IsValid())
		EditMode->CreateMaterialBrushVolume(EDualContourMaterialBrushVolumeShape::Sphere);
	return FReply::Handled();
}

FReply SDualContourEditPanel::CreateSplineMaterialRegion()
{
	if (EditMode.IsValid())
		EditMode->CreateMaterialBrushVolume(EDualContourMaterialBrushVolumeShape::SplinePrism);
	return FReply::Handled();
}

FReply SDualContourEditPanel::ApplySelectedMaterialRegions()
{
	if (EditMode.IsValid())
		EditMode->ApplySelectedMaterialBrushVolumes();
	return FReply::Handled();
}

FReply SDualContourEditPanel::ResumeMaterialBrush()
{
	if (EditMode.IsValid())
		EditMode->ClearMaterialBrushVolumeSelection();
	return FReply::Handled();
}

bool SDualContourEditPanel::CanApplySelectedMaterialRegions() const
{
	return EditMode.IsValid() && EditMode->HasSelectedMaterialBrushVolumes();
}

void SDualContourEditPanel::RebuildMaterialRegionList()
{
	if (!MaterialRegionList.IsValid())
		return;
	MaterialRegionList->ClearChildren();
	TArray<ADualContourMaterialBrushVolume*> Volumes;
	if (EditMode.IsValid())
		EditMode->GetMaterialBrushVolumes(Volumes);
	if (Volumes.IsEmpty())
	{
		MaterialRegionList->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoMaterialRegions", "No temporary material regions."))
			.TextStyle(FAppStyle::Get(), TEXT("SmallText"))
			.ColorAndOpacity(FStyleColors::ForegroundHeader)
		];
		return;
	}

	for (ADualContourMaterialBrushVolume* Volume : Volumes)
	{
		const TWeakObjectPtr<ADualContourMaterialBrushVolume> WeakVolume = Volume;
		MaterialRegionList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(this, &SDualContourEditPanel::GetMaterialRegionLabel, WeakVolume)
				.ToolTipText(LOCTEXT("SelectMaterialRegionTooltip", "Select this region in the viewport. Hold Ctrl to add it to the current selection."))
				.OnClicked(this, &SDualContourEditPanel::SelectMaterialRegion, WeakVolume)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DeleteMaterialRegion", "X"))
				.ToolTipText(LOCTEXT("DeleteMaterialRegionTooltip", "Delete this temporary material region."))
				.OnClicked(this, &SDualContourEditPanel::DeleteMaterialRegion, WeakVolume)
			]
		];
	}
}

void SDualContourEditPanel::HandleMaterialRegionSelectionChanged()
{
	if (RegionDetailsView.IsValid())
		RegionDetailsView->SetObject(EditMode.IsValid() ? EditMode->GetSelectedMaterialBrushVolume() : nullptr);
	RebuildMaterialRegionList();
}

FReply SDualContourEditPanel::SelectMaterialRegion(TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume)
{
	if (EditMode.IsValid() && Volume.IsValid())
		EditMode->SelectMaterialBrushVolume(
			Volume.Get(), FSlateApplication::Get().GetModifierKeys().IsControlDown());
	return FReply::Handled();
}

FReply SDualContourEditPanel::DeleteMaterialRegion(TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume)
{
	if (EditMode.IsValid() && Volume.IsValid())
		EditMode->DeleteMaterialBrushVolume(Volume.Get());
	return FReply::Handled();
}

FText SDualContourEditPanel::GetMaterialRegionLabel(
	TWeakObjectPtr<ADualContourMaterialBrushVolume> Volume) const
{
	if (!Volume.IsValid())
		return LOCTEXT("InvalidMaterialRegion", "Invalid region");
	FText ShapeText;
	switch (Volume->Shape)
	{
		case EDualContourMaterialBrushVolumeShape::Sphere:
			ShapeText = LOCTEXT("SphereRegionLabel", "Sphere");
			break;
		case EDualContourMaterialBrushVolumeShape::SplinePrism:
			ShapeText = LOCTEXT("SplineRegionLabel", "Spline");
			break;
		default:
			ShapeText = LOCTEXT("BoxRegionLabel", "Box");
			break;
	}
	return FText::Format(LOCTEXT("MaterialRegionListLabel", "{0}  |  {1}"),
		FText::FromString(Volume->GetActorLabel()), ShapeText);
}

#undef LOCTEXT_NAMESPACE
