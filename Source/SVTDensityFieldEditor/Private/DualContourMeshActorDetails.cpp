#include "DualContourMeshActorDetails.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DualContourMeshActor.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"

TSharedRef<IDetailCustomization> FDualContourMeshActorDetails::MakeInstance()
{
	return MakeShared<FDualContourMeshActorDetails>();
}

void FDualContourMeshActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
		if (ADualContourMeshActor* Actor = Cast<ADualContourMeshActor>(Object.Get()))
			CustomizedActors.Add(Actor);

	const TSharedRef<IPropertyHandle> DivisionsHandle =
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, Divisions));
	IDetailPropertyRow* DivisionsRow = DetailBuilder.EditDefaultProperty(DivisionsHandle);
	if (!DivisionsRow)
		return;

	TSharedPtr<SWidget> DefaultNameWidget;
	TSharedPtr<SWidget> DefaultValueWidget;
	DivisionsRow->GetDefaultWidgets(DefaultNameWidget, DefaultValueWidget);
	if (!DefaultNameWidget || !DefaultValueWidget)
		return;

	// Keep the struct's X/Y/Z child rows and place validation before the original value widget.
	DivisionsRow->CustomWidget(true)
	            .NameContent()
		[
			DefaultNameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage)
				.Image(this, &FDualContourMeshActorDetails::GetValidationIcon)
				.ColorAndOpacity(this, &FDualContourMeshActorDetails::GetValidationIconColor)
				.ToolTipText(this, &FDualContourMeshActorDetails::GetValidationTooltip)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				DefaultValueWidget.ToSharedRef()
			]
		];
}

bool FDualContourMeshActorDetails::AreDivisionsValid(FString* OutStatus) const
{
	bool bAllValid = !CustomizedActors.IsEmpty();
	TArray<FString> Statuses;
	for (const TWeakObjectPtr<ADualContourMeshActor>& WeakActor : CustomizedActors)
	{
		const ADualContourMeshActor* Actor = WeakActor.Get();
		if (!Actor)
		{
			bAllValid = false;
			continue;
		}

		FString Status;
		bAllValid &= Actor->ValidateDivisions(Status);
		Statuses.Add(MoveTemp(Status));
	}

	if (OutStatus)
		*OutStatus = FString::Join(Statuses, LINE_TERMINATOR);
	return bAllValid;
}

const FSlateBrush* FDualContourMeshActorDetails::GetValidationIcon() const
{
	return FAppStyle::GetBrush(AreDivisionsValid() ? TEXT("Icons.Check") : TEXT("Icons.XCircle"));
}

FSlateColor FDualContourMeshActorDetails::GetValidationIconColor() const
{
	return AreDivisionsValid() ? FStyleColors::Success : FStyleColors::Error;
}

FText FDualContourMeshActorDetails::GetValidationTooltip() const
{
	FString Status;
	AreDivisionsValid(&Status);
	return FText::FromString(Status);
}
