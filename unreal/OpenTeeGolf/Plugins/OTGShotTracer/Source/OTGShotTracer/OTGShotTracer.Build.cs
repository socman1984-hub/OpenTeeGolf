using UnrealBuildTool;

// The shot tracer component (a procedural mesh). See ../../README.md
public class OTGShotTracer : ModuleRules
{
	public OTGShotTracer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent" });
	}
}
