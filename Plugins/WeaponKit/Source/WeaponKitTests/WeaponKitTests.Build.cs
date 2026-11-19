using UnrealBuildTool;

public class WeaponKitTests : ModuleRules
{
	public WeaponKitTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WeaponKit"
		});
	}
}
