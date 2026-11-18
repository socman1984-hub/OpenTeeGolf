using UnrealBuildTool;

// The game: rounds of golf on course packs built from OpenTeeGolf's web version (see unreal/README.md)
public class OpenTeeGolf : ModuleRules
{
	public OpenTeeGolf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings" });
		// (the launch monitors: OpenConnect and Mevo+ sockets, JSON, FRP WebSockets)
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "WebSockets", "ImageWrapper", "MeshDescription", "StaticMeshDescription" });
		// (the round: the tracer, the HUD)
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent", "Slate", "SlateCore", "JsonUtilities", "HTTP", "EngineSettings", "RenderCore", "RHI" });
		// (the weather's sound, made as it plays: OTGWeatherSound)
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioExtensions" });
		// (the sky, the time of day and the weather's look: the Meridian Sky plugin - OTGSky.cpp. The
		// Greensward plugin's grass mats need no code: OTGGrass loads them by path)
		PrivateDependencyModuleNames.AddRange(new string[] { "MeridianSky" });
	}
}
