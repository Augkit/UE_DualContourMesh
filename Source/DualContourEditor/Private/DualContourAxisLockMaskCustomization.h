#pragma once

#include "IPropertyTypeCustomization.h"
#include "Widgets/Input/SCheckBox.h"

class IPropertyHandle;

class FDualContourAxisLockMaskCustomization final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	ECheckBoxState GetState(const TSharedPtr<IPropertyHandle>& Handle) const;
	void SetState(ECheckBoxState NewState, const TSharedPtr<IPropertyHandle>& Handle) const;
	TSharedPtr<IPropertyHandle> XHandle;
	TSharedPtr<IPropertyHandle> YHandle;
	TSharedPtr<IPropertyHandle> ZHandle;
};
