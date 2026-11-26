using UnrealBuildTool;

// The launch monitors: OpenConnect and Mevo+ sockets, JSON, FRP WebSockets; the Mevo+ camera's JPEG
// pictures (ImageWrapper). See ../../README.md
public class OTGLaunchMonitor : ModuleRules
{
	public OTGLaunchMonitor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "WebSockets", "ImageWrapper" });
	}
}
