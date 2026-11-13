using UnrealBuildTool;

public class OpenTeeGolfTarget : TargetRules
{
	public OpenTeeGolfTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("OpenTeeGolf");
	}
}
