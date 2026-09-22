using UnrealBuildTool;

public class OutlierEditor : ModuleRules
{
	public OutlierEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
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
			"GameplayAbilities",
			"GraphicSettingDebugger",
			"GraphicSettingDebuggerEditor"
		});

		PrivateDependencyModuleNames.Add("LevelEditor");
	}
}
