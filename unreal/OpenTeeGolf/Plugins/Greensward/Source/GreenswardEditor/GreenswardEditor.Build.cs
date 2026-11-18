// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

using UnrealBuildTool;

public class GreenswardEditor : ModuleRules
{
	public GreenswardEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Greensward" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "MeshDescription", "StaticMeshDescription", "MaterialEditor",
			"AssetRegistry", "Landscape"
		});
	}
}
