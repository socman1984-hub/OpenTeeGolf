using UnrealBuildTool;

public class MeridianSky : ModuleRules
{
	public MeridianSky(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Niagara" });
		PrivateDependencyModuleNames.AddRange(new[] { "NetCore" });
	}
}
