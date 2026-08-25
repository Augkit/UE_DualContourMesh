// Copyright Epic Games, Inc. All Rights Reserved.

#include "DualContourMesh.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FDualContourMeshModule"

void FDualContourMeshModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DualContourMesh"));
	if (Plugin.IsValid())
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/DualContourMesh"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
}

void FDualContourMeshModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDualContourMeshModule, DualContourMesh)
