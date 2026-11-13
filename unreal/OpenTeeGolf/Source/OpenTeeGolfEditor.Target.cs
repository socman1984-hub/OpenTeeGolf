using UnrealBuildTool;

public class OpenTeeGolfEditorTarget : TargetRules
{
	public OpenTeeGolfEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("OpenTeeGolf");
	}
}
