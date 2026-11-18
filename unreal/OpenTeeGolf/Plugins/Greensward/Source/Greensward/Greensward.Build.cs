// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

using UnrealBuildTool;

public class Greensward : ModuleRules
{
	public Greensward(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PCG" });
	}
}
