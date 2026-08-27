using UnrealBuildTool;

public class DualContourEditor : ModuleRules
{
	public DualContourEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Renderer",
			"DualContourMesh",
			"UnrealEd",
			"AssetDefinition",
			"AssetTools",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"EditorFramework",
			"AdvancedPreviewScene"
		});
	}
}
