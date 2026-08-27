#include "DualContourMeshActor.h"
#include "DualContourMeshActorDetails.h"
#include "NormalizedFloatCurveCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "PropertyEditorModule.h"

namespace
{
class FNormalizedCurvePropertyIdentifier final : public IPropertyTypeIdentifier
{
public:
	virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override
	{
		return PropertyHandle.HasMetaData(TEXT("NormalizedCurve"));
	}
};
}

class FDualContourEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		NormalizedCurveIdentifier = MakeShared<FNormalizedCurvePropertyIdentifier>();
		PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("RuntimeFloatCurve"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNormalizedFloatCurveCustomization::MakeInstance),
			NormalizedCurveIdentifier);
		PropertyEditor.RegisterCustomClassLayout(ADualContourMeshActor::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FDualContourMeshActorDetails::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			if (NormalizedCurveIdentifier.IsValid())
				PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("RuntimeFloatCurve"), NormalizedCurveIdentifier);
			PropertyEditor.UnregisterCustomClassLayout(ADualContourMeshActor::StaticClass()->GetFName());
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
		NormalizedCurveIdentifier.Reset();
	}

private:
	TSharedPtr<IPropertyTypeIdentifier> NormalizedCurveIdentifier;
};

IMPLEMENT_MODULE(FDualContourEditorModule, DualContourEditor)
