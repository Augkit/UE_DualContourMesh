#include "NormalizedFloatCurveCustomization.h"

#include "Curves/CurveFloat.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "SCurveEditor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FNormalizedFloatCurveCustomization::MakeInstance()
{
	return MakeShareable(new FNormalizedFloatCurveCustomization());
}

FNormalizedFloatCurveCustomization::FNormalizedFloatCurveCustomization()
{
	if (GEditor)
		GEditor->RegisterForUndo(this);
}

FNormalizedFloatCurveCustomization::~FNormalizedFloatCurveCustomization()
{
	if (CurveWidget.IsValid() && CurveWidget->GetCurveOwner() == this)
		CurveWidget->SetCurveOwner(nullptr, false);
	if (GEditor)
		GEditor->UnregisterForUndo(this);
}

void FNormalizedFloatCurveCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = InStructHandle;

	TArray<void*> StructPointers;
	StructHandle->AccessRawData(StructPointers);
	TArray<UObject*> OuterObjects;
	StructHandle->GetOuterObjects(OuterObjects);

	if (StructPointers.Num() != 1)
	{
		HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()]
			.ValueContent()
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("NormalizedFloatCurve", "MultipleValues", "Multiple curves cannot be edited together"))
			];
		return;
	}

	RuntimeCurve = static_cast<FRuntimeFloatCurve*>(StructPointers[0]);
	Owner = OuterObjects.Num() == 1 ? OuterObjects[0] : nullptr;

	TOptional<FString> XAxisName;
	TOptional<FString> YAxisName;
	if (StructHandle->HasMetaData(TEXT("XAxisName")))
		XAxisName = StructHandle->GetMetaData(TEXT("XAxisName"));
	if (StructHandle->HasMetaData(TEXT("YAxisName")))
		YAxisName = StructHandle->GetMetaData(TEXT("YAxisName"));

	HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()]
		.ValueContent()
		.HAlign(HAlign_Fill)
		.MinDesiredWidth(200.0f)
		[
			SNew(SBorder)
			[
				SAssignNew(CurveWidget, SCurveEditor)
				.ViewMinInput_Static(&FNormalizedFloatCurveCustomization::GetViewMinimum)
				.ViewMaxInput_Static(&FNormalizedFloatCurveCustomization::GetViewMaximum)
				.DataMinInput(0.0f)
				.DataMaxInput(1.0f)
				.ViewMinOutput_Static(&FNormalizedFloatCurveCustomization::GetViewMinimum)
				.ViewMaxOutput_Static(&FNormalizedFloatCurveCustomization::GetViewMaximum)
				.TimelineLength(1.0f)
				.XAxisName(XAxisName)
				.YAxisName(YAxisName)
				.HideUI(false)
				.AllowZoomOutput(false)
				.ZoomToFitVertical(false)
				.ZoomToFitHorizontal(false)
				.ShowZoomButtons(false)
				.ShowCurveSelector(false)
				.DesiredSize(FVector2D(300.0, 150.0))
			]
		];

	RefreshCurveOwner();
	CurveWidget->SetPropertyUtils(CustomizationUtils.GetPropertyUtilities());

	TSharedPtr<IPropertyHandle> RootHandle = StructHandle;
	while (RootHandle->GetParentHandle().IsValid())
		RootHandle = RootHandle->GetParentHandle();
	CurveWidget->RegisterToPropertyChangedEvent(RootHandle);
}

void FNormalizedFloatCurveCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructHandle,
	IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ExternalCurveHandle = InStructHandle->GetChildHandle(TEXT("ExternalCurve"));
	if (ExternalCurveHandle.IsValid())
	{
		ExternalCurveHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateSP(this, &FNormalizedFloatCurveCustomization::OnExternalCurveChanged));
		StructBuilder.AddProperty(ExternalCurveHandle.ToSharedRef());
	}
}

TArray<FRichCurveEditInfoConst> FNormalizedFloatCurveCustomization::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	if (RuntimeCurve)
		Curves.Emplace(&RuntimeCurve->EditorCurveData, GetCurveName());
	return Curves;
}

void FNormalizedFloatCurveCustomization::GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> Curves) const
{
	if (RuntimeCurve)
		Curves.Add(FRichCurveEditInfoConst(&RuntimeCurve->EditorCurveData, GetCurveName()));
}

TArray<FRichCurveEditInfo> FNormalizedFloatCurveCustomization::GetCurves()
{
	TArray<FRichCurveEditInfo> Curves;
	if (RuntimeCurve)
		Curves.Emplace(&RuntimeCurve->EditorCurveData, GetCurveName());
	return Curves;
}

void FNormalizedFloatCurveCustomization::ModifyOwner()
{
	if (Owner)
		Owner->Modify(true);
}

TArray<const UObject*> FNormalizedFloatCurveCustomization::GetOwners() const
{
	TArray<const UObject*> Owners;
	if (Owner)
		Owners.Add(Owner);
	return Owners;
}

void FNormalizedFloatCurveCustomization::MakeTransactional()
{
	if (Owner)
		Owner->SetFlags(RF_Transactional);
}

void FNormalizedFloatCurveCustomization::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
	if (StructHandle.IsValid())
		StructHandle->NotifyPostChange(EPropertyChangeType::Unspecified);
}

bool FNormalizedFloatCurveCustomization::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	return RuntimeCurve && CurveInfo.CurveToEdit == &RuntimeCurve->EditorCurveData;
}

void FNormalizedFloatCurveCustomization::PostUndo(bool bSuccess)
{
	if (!StructHandle.IsValid())
		return;

	TArray<void*> StructPointers;
	StructHandle->AccessRawData(StructPointers);
	RuntimeCurve = StructPointers.Num() == 1 ? static_cast<FRuntimeFloatCurve*>(StructPointers[0]) : nullptr;
	RefreshCurveOwner();
}

void FNormalizedFloatCurveCustomization::OnExternalCurveChanged()
{
	RefreshCurveOwner();
}

void FNormalizedFloatCurveCustomization::RefreshCurveOwner()
{
	if (!CurveWidget.IsValid())
		return;

	if (RuntimeCurve && RuntimeCurve->ExternalCurve)
		CurveWidget->SetCurveOwner(RuntimeCurve->ExternalCurve, false);
	else
		CurveWidget->SetCurveOwner(RuntimeCurve ? this : nullptr, StructHandle.IsValid() && StructHandle->IsEditable());
}

FName FNormalizedFloatCurveCustomization::GetCurveName() const
{
	return StructHandle.IsValid() ? FName(StructHandle->GetPropertyDisplayName().ToString()) : NAME_None;
}
