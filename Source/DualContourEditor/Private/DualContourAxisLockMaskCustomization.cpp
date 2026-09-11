#include "DualContourAxisLockMaskCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FDualContourAxisLockMaskCustomization::MakeInstance()
{
	return MakeShared<FDualContourAxisLockMaskCustomization>();
}

void FDualContourAxisLockMaskCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils&)
{
	XHandle = StructPropertyHandle->GetChildHandle(TEXT("bX"));
	YHandle = StructPropertyHandle->GetChildHandle(TEXT("bY"));
	ZHandle = StructPropertyHandle->GetChildHandle(TEXT("bZ"));

	HeaderRow.NameContent()[StructPropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()
		.HAlign(HAlign_Left)
		[ SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[ SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return GetState(XHandle); })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetState(State, XHandle); })
				.Content()[SNew(STextBlock).Text(NSLOCTEXT("DualContourAxisLock", "XAxis", "X"))] ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[ SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return GetState(YHandle); })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetState(State, YHandle); })
				.Content()[SNew(STextBlock).Text(NSLOCTEXT("DualContourAxisLock", "YAxis", "Y"))] ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return GetState(ZHandle); })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetState(State, ZHandle); })
				.Content()[SNew(STextBlock).Text(NSLOCTEXT("DualContourAxisLock", "ZAxis", "Z"))] ] ];
}

void FDualContourAxisLockMaskCustomization::CustomizeChildren(TSharedRef<IPropertyHandle>, IDetailChildrenBuilder&,
	IPropertyTypeCustomizationUtils&)
{
}

ECheckBoxState FDualContourAxisLockMaskCustomization::GetState(const TSharedPtr<IPropertyHandle>& Handle) const
{
	bool bValue = false;
	return Handle.IsValid() && Handle->GetValue(bValue) == FPropertyAccess::Success && bValue
		       ? ECheckBoxState::Checked
		       : ECheckBoxState::Unchecked;
}

void FDualContourAxisLockMaskCustomization::SetState(ECheckBoxState NewState,
	const TSharedPtr<IPropertyHandle>& Handle) const
{
	if (Handle.IsValid())
		Handle->SetValue(NewState == ECheckBoxState::Checked);
}
