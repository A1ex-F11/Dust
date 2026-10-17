// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Dust2 : ModuleRules
{
	public Dust2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "NiagaraEditor", "BlueprintGraph", "RHI" });
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Niagara", "UMG", "SlateCore", "AIModule", "GameplayTasks", "NavigationSystem", "StateTreeModule", "GameplayStateTreeModule" });


		// Uncomment if you are using Slate UI
		PrivateDependencyModuleNames.Add("Slate");
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
