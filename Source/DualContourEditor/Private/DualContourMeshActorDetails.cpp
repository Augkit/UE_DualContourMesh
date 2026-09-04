#include "DualContourMeshActorDetails.h"

#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DualContour.h"
#include "DualContourFactory.h"
#include "DualContourMeshActor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "Misc/MessageDialog.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DualContourMeshActorDetails"

namespace
{
void ShowSaveNotification(const FText& Text, SNotificationItem::ECompletionState CompletionState)
{
	FNotificationInfo Info(Text);
	Info.ExpireDuration = 4.f;
	Info.bUseSuccessFailIcons = true;
	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		Notification->SetCompletionState(CompletionState);
}

void CopyDualContourToAsset(UDualContour* Source, UDualContour* Target)
{
	Target->CopyFrom(Source);
	Target->MarkPackageDirty();
	Target->PostEditChange();
}
}

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

	// Replace the default inline-object row with a read-only view. InitialDualContour is the
	// editable source; ResetDualContour copies its current data into the actor-owned DualContour.
	DetailBuilder.HideProperty(
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, DualContour)));

	// DualContour is reused in other editors, so keep its native categories unchanged and
	// relocate only the properties shown for ADualContourMeshActor.
	TArray<UObject*> DualContourObjects;
	DualContourObjects.Reserve(CustomizedActors.Num());
	for (const TWeakObjectPtr<ADualContourMeshActor>& WeakActor : CustomizedActors)
	{
		if (ADualContourMeshActor* Actor = WeakActor.Get(); Actor && Actor->DualContour)
			DualContourObjects.Add(Actor->DualContour.Get());
	}

	IDetailCategoryBuilder& DualContourCategory = DetailBuilder.EditCategory(TEXT("DualContour"));
	if (!DualContourObjects.IsEmpty())
	{
		IDetailGroup& DensityFieldGroup = DualContourCategory.AddGroup(
			TEXT("DensityField"), LOCTEXT("DensityFieldGroup", "Density Field"), false, true);
		DensityFieldGroup.SetDisplayMode(EDetailGroupDisplayMode::Category);
		DensityFieldGroup.AddExternalObjectProperty(
			DualContourObjects, GET_MEMBER_NAME_CHECKED(UDualContour, CellCount),
			EPropertyLocation::Default, FAddPropertyParams()).IsEnabled(false);
		DensityFieldGroup.AddExternalObjectProperty(
			DualContourObjects, GET_MEMBER_NAME_CHECKED(UDualContour, CellSize),
			EPropertyLocation::Default, FAddPropertyParams()).IsEnabled(false);
		DensityFieldGroup.AddExternalObjectProperty(
			DualContourObjects, GET_MEMBER_NAME_CHECKED(UDualContour, bRebuildRequired),
			EPropertyLocation::Default, FAddPropertyParams()).IsEnabled(false);
	}

	DualContourCategory.AddCustomRow(LOCTEXT("SaveDualContourFilter", "Save Dual Contour Asset"))
	                   .WholeRowContent()
	                   .HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SaveDualContourButton", "Save"))
			.ToolTipText(LOCTEXT("SaveDualContourTooltip",
				"Overwrite InitialDualContour with the current DualContour after confirmation."))
			.IsEnabled(this, &FDualContourMeshActorDetails::CanSaveDualContour)
			.OnClicked(this, &FDualContourMeshActorDetails::SaveDualContour)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("SaveDualContourAsButton", "Save As..."))
			.ToolTipText(LOCTEXT("SaveDualContourAsTooltip",
				"Create a new DualContour asset from the current DualContour and assign it as InitialDualContour."))
			.IsEnabled(this, &FDualContourMeshActorDetails::CanSaveDualContourAs)
			.OnClicked(this, &FDualContourMeshActorDetails::SaveDualContourAs)
		]
	];

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

bool FDualContourMeshActorDetails::CanSaveDualContour() const
{
	if (CustomizedActors.Num() != 1)
		return false;

	const ADualContourMeshActor* Actor = CustomizedActors[0].Get();
	return Actor && Actor->InitialDualContour && Actor->DualContour
		&& Actor->DualContour->HasCurrentGeneratedData();
}

FReply FDualContourMeshActorDetails::SaveDualContour()
{
	if (!CanSaveDualContour())
		return FReply::Handled();

	ADualContourMeshActor* Actor = CustomizedActors[0].Get();
	UDualContour* Source = Actor->DualContour;
	UDualContour* Target = Actor->InitialDualContour;

	const FText Confirmation = FText::Format(
		LOCTEXT("ConfirmOverwriteDualContourAsset",
			"Overwrite {0} with the current DualContour?\n\nThis will replace the asset's existing contour data."),
		FText::FromString(Target->GetPathName()));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation,
		LOCTEXT("ConfirmOverwriteDualContourTitle", "Save DualContour Asset")) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("SaveDualContourTransaction", "Save DualContour Asset"));
	CopyDualContourToAsset(Source, Target);
	ShowSaveNotification(
		FText::Format(LOCTEXT("UpdatedDualContourAsset", "Saved current DualContour to {0}."),
			FText::FromString(Target->GetPathName())),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

bool FDualContourMeshActorDetails::CanSaveDualContourAs() const
{
	if (CustomizedActors.Num() != 1)
		return false;

	const ADualContourMeshActor* Actor = CustomizedActors[0].Get();
	return Actor && Actor->DualContour && Actor->DualContour->HasCurrentGeneratedData();
}

FReply FDualContourMeshActorDetails::SaveDualContourAs()
{
	if (!CanSaveDualContourAs())
		return FReply::Handled();

	ADualContourMeshActor* Actor = CustomizedActors[0].Get();
	UDualContour* Source = Actor->DualContour;

	UDualContourFactory* Factory = NewObject<UDualContourFactory>();
	UDualContour* NewAsset = Cast<UDualContour>(
		FAssetToolsModule::GetModule().Get().CreateAssetWithDialog(UDualContour::StaticClass(), Factory,
			TEXT("SaveDualContourMeshActorAsset")));
	if (!NewAsset)
		return FReply::Handled();

	const FScopedTransaction Transaction(LOCTEXT("CreateDualContourTransaction", "Create DualContour Asset"));
	CopyDualContourToAsset(Source, NewAsset);
	Actor->Modify();
	Actor->InitialDualContour = NewAsset;
	Actor->MarkPackageDirty();
	Actor->ResetDualContour();
	ShowSaveNotification(
		FText::Format(LOCTEXT("CreatedDualContourAsset", "Created DualContour asset {0}."),
			FText::FromString(NewAsset->GetPathName())),
		SNotificationItem::CS_Success);
	return FReply::Handled();
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

#undef LOCTEXT_NAMESPACE
