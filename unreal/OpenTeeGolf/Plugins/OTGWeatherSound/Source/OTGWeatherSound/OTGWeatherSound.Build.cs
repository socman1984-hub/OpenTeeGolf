using UnrealBuildTool;

// The weather heard: a procedural sound wave. See ../../README.md
public class OTGWeatherSound : ModuleRules
{
	public OTGWeatherSound(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioExtensions" });
	}
}
