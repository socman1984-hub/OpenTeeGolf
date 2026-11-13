using UnrealBuildTool;

// The game: rounds of golf on course packs built from OpenTeeGolf's web version (see unreal/README.md)
public class OpenTeeGolf : ModuleRules
{
	public OpenTeeGolf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings" });
	}
}
