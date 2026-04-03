// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Outlier : ModuleRules
{
    public Outlier(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // FirstPerson template classes were merged into the Outlier module but still use
        // the original template export macro in several headers.
        PublicDefinitions.Add("TP_FIRSTPERSON_API=OUTLIER_API");

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "StateTreeModule",
            "GameplayStateTreeModule",
            "UMG",
            "Slate"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
        });

        PublicIncludePaths.AddRange(new string[] {
            "Outlier",
            "Outlier/TP_FirstPerson",
            "Outlier/TP_FirstPerson/Variant_Horror",
            "Outlier/TP_FirstPerson/Variant_Horror/UI",
            "Outlier/TP_FirstPerson/Variant_Shooter",
            "Outlier/TP_FirstPerson/Variant_Shooter/AI",
            "Outlier/TP_FirstPerson/Variant_Shooter/UI",
            "Outlier/TP_FirstPerson/Variant_Shooter/Weapons"
        });
        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
