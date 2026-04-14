// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OutlierEditorTarget : TargetRules
{
    public OutlierEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("Outlier");
    }
}
