using UnrealBuildTool;

public class LightmapUVHelper : ModuleRules
{
	public LightmapUVHelper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore",
			"UnrealEd", "ToolMenus", "AssetRegistry", "ContentBrowser", "PropertyEditor",
			"MeshDescription", "StaticMeshDescription", "AdvancedPreviewScene", "RenderCore", "RHI",
			"LevelEditor", "Projects", "Json"
		});
	}
}
