// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DualContourGamePlay : ModuleRules
{
	public DualContourGamePlay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"InputCore",
				"EnhancedInput",
				"TP_FirstPerson",
				"DualContourMesh",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
			}
		);
	}
}
