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

        bUseLoggingInShipping = true;

        if (Target.Configuration == UnrealTargetConfiguration.Shipping)
        {
            bUseConsoleInShipping = true;                 // 콘솔(~) + stat 명령 입력 허용
            GlobalDefinitions.Add("FORCE_USE_STATS=1");   // stat unit / stat gpu 활성화
            GlobalDefinitions.Add("HAS_GPU_STATS=1");
        }
    }
}
