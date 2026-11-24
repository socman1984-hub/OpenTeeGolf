using UnrealBuildTool;

// The game: rounds of golf on course packs built from OpenTeeGolf's web version (see unreal/README.md)
public class OpenTeeGolf : ModuleRules
{
	public OpenTeeGolf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// (every header by its folder - "Round/OTGRound.h", "UI/SOTGHud.h" - public or private; see README.md)
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings" });
		// (the project's own plugins: the ball physics and the launch monitors - Plugins/OTGBallPhysics,
		// Plugins/OTGLaunchMonitor. Public: the round's header uses their types)
		PublicDependencyModuleNames.AddRange(new string[] { "OTGBallPhysics", "OTGLaunchMonitor" });
		// (and the small pieces anyone can take: Plugins/OTGUiKit, OTGMinimap, OTGWindIndicator, OTGWeatherSound, OTGGlb, OTGShotTracer)
		PrivateDependencyModuleNames.AddRange(new string[] { "OTGUiKit", "OTGMinimap", "OTGWindIndicator", "OTGWeatherSound", "OTGGlb", "OTGShotTracer" });
		// (the course helper's sockets, JSON, the course pictures and meshes)
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "Json", "WebSockets", "ImageWrapper", "MeshDescription", "StaticMeshDescription" });
		// (the round: the tracer, the HUD)
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent", "Slate", "SlateCore", "JsonUtilities", "HTTP", "EngineSettings", "RenderCore", "RHI" });
		// (the weather's sound, made as it plays: OTGWeatherSound)
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioExtensions" });
		// (the sky, the time of day and the weather's look: the Meridian Sky plugin - OTGSky.cpp. The
		// Greensward plugin's grass mats need no code: OTGGrass loads them by path)
		PrivateDependencyModuleNames.AddRange(new string[] { "MeridianSky" });
		// (the ponds, lakes, rivers and sea: the Tidewright Water plugin - OTGWater.cpp)
		PrivateDependencyModuleNames.AddRange(new string[] { "Tidewright", "ProceduralMeshComponent" });
		// (the houses round the course: the Hearthline Homes plugin - OTGHouses.cpp - its houses made static meshes here)
		PrivateDependencyModuleNames.AddRange(new string[] { "HearthlineHomes", "GeometryCore", "GeometryFramework" });
	}
}
