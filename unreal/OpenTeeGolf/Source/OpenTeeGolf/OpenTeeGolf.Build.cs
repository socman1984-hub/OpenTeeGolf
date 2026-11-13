using UnrealBuildTool;

// The game: rounds of golf on course packs built from OpenTeeGolf's web version (see unreal/README.md)
public class OpenTeeGolf : ModuleRules
{
	public OpenTeeGolf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings" });
		// (the launch monitors: OpenConnect and Mevo+ sockets, JSON, FRP WebSockets)
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "WebSockets" });
		// (the round: the tracer, the HUD)
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent", "Slate", "SlateCore", "JsonUtilities" });
	}
}
