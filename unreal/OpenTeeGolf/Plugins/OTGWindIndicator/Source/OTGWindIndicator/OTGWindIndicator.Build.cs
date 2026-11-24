using UnrealBuildTool;

// The wind indicator widget (Slate). See ../../README.md
public class OTGWindIndicator : ModuleRules
{
	public OTGWindIndicator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "SlateCore", "Slate" });
		PrivateDependencyModuleNames.AddRange(new string[] { "OTGUiKit" });
	}
}
