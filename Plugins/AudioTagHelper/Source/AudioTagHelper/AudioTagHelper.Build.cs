using UnrealBuildTool;

public class AudioTagHelper : ModuleRules
{
	public AudioTagHelper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"ToolMenus",
				"GameplayTags",
				"GameplayTagsEditor",
				"AssetTools",
				"AssetRegistry",
				"Outlier"
			}
		);
	}
}
