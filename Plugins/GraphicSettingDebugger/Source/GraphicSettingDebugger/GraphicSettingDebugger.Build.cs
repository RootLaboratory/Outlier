using UnrealBuildTool;

public class GraphicSettingDebugger : ModuleRules
{
	public GraphicSettingDebugger(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "RHI", "RenderCore" });
	}
}
