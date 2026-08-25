using UnrealBuildTool;

public class SVTDensityFieldEditor : ModuleRules
{
	public SVTDensityFieldEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Renderer", "DualContourMesh",
			"UnrealEd", "AssetDefinition", "AssetTools", "PropertyEditor",
			"Slate", "SlateCore", "EditorFramework", "AdvancedPreviewScene"
		});
	}
}
