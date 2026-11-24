using UnrealBuildTool;

// Reading .glb files at run time. See ../../README.md
public class OTGGlb : ModuleRules
{
	public OTGGlb(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
