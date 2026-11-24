using UnrealBuildTool;

// The game's Slate look (header only). See ../../README.md
public class OTGUiKit : ModuleRules
{
	public OTGUiKit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "SlateCore", "Slate" });
	}
}
