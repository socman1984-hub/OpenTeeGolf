using UnrealBuildTool;

// The minimap widget (Slate). See ../../README.md
public class OTGMinimap : ModuleRules
{
	public OTGMinimap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "SlateCore", "Slate" });
		PrivateDependencyModuleNames.AddRange(new string[] { "OTGUiKit" });
	}
}
