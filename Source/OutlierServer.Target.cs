using UnrealBuildTool;
using System.Collections.Generic;

public class OutlierServerTarget : TargetRules
{
    public OutlierServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("Outlier");
    }
}