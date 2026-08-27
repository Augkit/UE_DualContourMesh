#pragma once

#include "Curves/CurveOwnerInterface.h"
#include "EditorUndoClient.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class SCurveEditor;
class UObject;
struct FRuntimeFloatCurve;

/** Displays a metadata-selected RuntimeFloatCurve in a view permanently fixed to 0..1 on both axes. */
class FNormalizedFloatCurveCustomization final : public IPropertyTypeCustomization, public FCurveOwnerInterface, public FEditorUndoClient
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual ~FNormalizedFloatCurveCustomization() override;

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	UE_DEPRECATED(5.6, "Use version taking a TAdderReserverRef")
	virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
	virtual void GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> Curves) const override;
	virtual TArray<FRichCurveEditInfo> GetCurves() override;
	virtual void ModifyOwner() override;
	virtual TArray<const UObject*> GetOwners() const override;
	virtual void MakeTransactional() override;
	virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
	virtual bool IsValidCurve(FRichCurveEditInfo CurveInfo) override;

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

private:
	FNormalizedFloatCurveCustomization();
	void OnExternalCurveChanged();
	void RefreshCurveOwner();
	FName GetCurveName() const;

	/** Visual padding keeps keys at data-space 0 and 1 away from the widget's clipped edges. */
	static constexpr float ViewPadding = 0.05f;
	static float GetViewMinimum() { return -ViewPadding; }
	static float GetViewMaximum() { return 1.0f + ViewPadding; }

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> ExternalCurveHandle;
	TSharedPtr<SCurveEditor> CurveWidget;
	FRuntimeFloatCurve* RuntimeCurve = nullptr;
	UObject* Owner = nullptr;
};
