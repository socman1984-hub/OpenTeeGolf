// Editor helpers for the course pack builder (unreal/pack_course.py), callable from Python as
// unreal.OTGPackTools: what Unreal's Python can't do by itself.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OTGPackTools.generated.h"

class ALandscape;
class UOTGCourseData;

UCLASS()
class OPENTEEGOLFEDITOR_API UOTGPackTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// A landscape from an export's 16-bit heightmap.png in the open level, its north-west corner at
	// Corner, Scale as course.json's landscape settings give it (X = Y = cm between samples, Z)
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	static ALandscape* CreateLandscape(const FString& HeightmapPng, FVector Corner, FVector Scale);

	// The course's play data (an export's play.json, heightmap.png and surfaces.png) as a
	// UOTGCourseData asset at PackagePath/AssetName, for the level at LevelPath, whose export origin
	// is at LevelOrigin; saved
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	static UOTGCourseData* CreateCourseData(const FString& ExportDir, const FString& PackagePath, const FString& AssetName, const FString& LevelPath, FVector LevelOrigin);
};
