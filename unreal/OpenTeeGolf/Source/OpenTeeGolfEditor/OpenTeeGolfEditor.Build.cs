using UnrealBuildTool;

// Editor-only helpers for the course pack builder (unreal/pack_course.py)
public class OpenTeeGolfEditor : ModuleRules
{
	public OpenTeeGolfEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "OpenTeeGolf", "UnrealEd", "Landscape", "ImageWrapper", "Json", "AssetRegistry" });
	}
}
