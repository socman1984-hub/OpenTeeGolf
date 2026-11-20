// Copyright __PUBLISHER__. All Rights Reserved.
using UnrealBuildTool;

public class HearthlineHomes : ModuleRules
{
	public HearthlineHomes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GeometryCore", "GeometryFramework" });
		PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription", "MeshConversion" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry" });
		}
	}
}
