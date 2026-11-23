// A course's edits put in the game in seconds, without the web game's export (public/js/
// export-unreal.js, 5-15 minutes): the export folder changed where the course editor changed it.
//
//  - the holes (play.json, holes.json): tees, aim points, the green's centre, pins, par, handicap,
//    numbers - and each hole's path and green radius worked out as course.js does
//  - greens, fairways, tee boxes and bunkers added, moved, reshaped or taken away: the play
//    surfaces (surfaces.png, course.js surfaceAt's rules: a fringe 1.5 m round each green, a first
//    cut 2 m round each fairway, paths kept) and the surface masks (layer_green/fairway/tee/bunker/
//    rough.png: 0.5 on the outline, 3 m either way) made again round what changed; a new or moved
//    bunker's hollow pressed into the ground (heightmap.png)
//  - tall grass painted or rubbed out (surfaces.png: native / rough)
//  - your own trees added, moved or taken away, found trees removed, new tree areas planted
//    (trees.json and play.json's trees: the same trees in the same order)
//  - then the masks derived from those (derive-layers.js): the native grass and the ground under
//    the trees (layer_native.png), the fringes (layer_fringe.png), the mowing directions (layer_mow.png)
//
// What it can't do - water, the boundary, penalty areas, trees taken out of a tree area or a
// stroke taken back, building types, tree species turned off, the course's looks - is listed in
// NeedsFull: those show after the full export (Save and put in the game).

#pragma once

#include "CoreMinimal.h"
#include "Course/OTGCourseSource.h"

namespace otg
{
	struct FQuickResult
	{
		bool bOk = false;
		FString Message;
		TArray<FString> NeedsFull;     // (what changed that only the full export puts in the game)
		double Seconds = 0;
	};

	// Before: the course as the export folder has it (as the editor opened it, or last updated it);
	// After: as it is now. Heavy (a few seconds): call it off the game thread.
	FQuickResult QuickUpdate(const FString& ExportDir, const FOTGCourseSource& Before, const FOTGCourseSource& After);
}
