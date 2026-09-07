using UnrealBuildTool;

public class OutlierEditor : ModuleRules
{
	public OutlierEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Outlier",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"AssetRegistry",
			"DeveloperSettings",
			"GameplayTags",
			"GameplayAbilities"
		});
	}
}
