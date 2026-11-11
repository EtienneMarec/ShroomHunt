// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShroomHunt : ModuleRules
{
	public ShroomHunt(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
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

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ShroomHunt",
			"ShroomHunt/Variant_Horror",
			"ShroomHunt/Variant_Horror/UI",
			"ShroomHunt/Variant_Shooter",
			"ShroomHunt/Variant_Shooter/AI",
			"ShroomHunt/Variant_Shooter/UI",
			"ShroomHunt/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
