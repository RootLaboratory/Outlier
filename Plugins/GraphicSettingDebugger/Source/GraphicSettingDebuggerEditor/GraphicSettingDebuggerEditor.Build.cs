using UnrealBuildTool;

public class GraphicSettingDebuggerEditor : ModuleRules
{
	public GraphicSettingDebuggerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "ToolMenus", "UnrealEd",
			"GraphicSettingDebugger", "Json", "RHI", "RenderCore"
		});
	}
}
