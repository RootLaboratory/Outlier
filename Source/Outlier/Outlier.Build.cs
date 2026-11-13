// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Outlier : ModuleRules
{
    public Outlier(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // FirstPerson template classes were merged into the Outlier module but still use
        // the original template export macro in several headers.

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "LevelSequence",
            "MovieScene",
            "AIModule",
            "StateTreeModule",
            "GameplayStateTreeModule",
            "GameplayTags",
            "GameplayAbilities",
            "GameplayTasks",
            "UMG",
            "MediaAssets",
            "Slate",
            "AnimGraphRuntime",
            "TagDrivenUI",
            "DeveloperSettings",
            "VisualEvent",
            "Niagara",
            "PhysicsCore",
            "GeometryCollectionEngine",
            "Sockets",
            "Networking",
            "Json",
            "GraphicSettingDebugger"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "SlateCore",
            "RDG"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("AssetRegistry");
            PrivateDependencyModuleNames.Add("UnrealEd");
            // 자동화에서 transient StateTree를 컴파일해 실제 해킹/공격 Task 경계를 검증한다.
            PrivateDependencyModuleNames.Add("StateTreeEditorModule");
            PrivateDependencyModuleNames.Add("PropertyBindingUtils");
        }

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory
        });
        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
