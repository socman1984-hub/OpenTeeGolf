using UnrealBuildTool;

// The ball physics: plain C++17 with no Unreal types (the parity tests in unreal/tests/physics build
// the same files outside Unreal). See ../../README.md
public class OTGBallPhysics : ModuleRules
{
	public OTGBallPhysics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
