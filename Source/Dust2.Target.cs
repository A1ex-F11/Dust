// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Dust2Target : TargetRules
{
	public Dust2Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;

		ExtraModuleNames.AddRange( new string[] { "Dust2" } );
	}
}
