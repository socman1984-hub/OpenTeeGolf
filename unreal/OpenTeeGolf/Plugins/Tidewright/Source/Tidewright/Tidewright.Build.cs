// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

using UnrealBuildTool;

public class Tidewright : ModuleRules
{
	public Tidewright(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ProceduralMeshComponent",
			"Niagara",
			"NiagaraCore",
			"VectorVM"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI"
		});
	}
}
