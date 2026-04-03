// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OutlierTarget : TargetRules
{
    public OutlierTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("Outlier");
    }
}
