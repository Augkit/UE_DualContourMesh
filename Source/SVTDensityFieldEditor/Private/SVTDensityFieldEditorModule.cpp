#include "DualContourMeshActor.h"
#include "DualContourMeshActorDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FSVTDensityFieldEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditor.RegisterCustomClassLayout(ADualContourMeshActor::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FDualContourMeshActorDetails::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditor.UnregisterCustomClassLayout(ADualContourMeshActor::StaticClass()->GetFName());
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}
};

IMPLEMENT_MODULE(FSVTDensityFieldEditorModule, SVTDensityFieldEditor)
