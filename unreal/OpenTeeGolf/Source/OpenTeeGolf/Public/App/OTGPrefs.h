// The player's own choices that last from one game to the next, kept in GameUserSettings.ini
// ([OpenTeeGolf]) beside the display settings: the main menu's Settings and the HUD both use them.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"

namespace otg::prefs
{
	// Straight on to the next hole after holing out, without stopping on the scorecard (Tab still shows it)
	inline bool SkipScorecard()
	{
		bool b = false;
		if (GConfig) GConfig->GetBool(TEXT("OpenTeeGolf"), TEXT("SkipScorecard"), b, GGameUserSettingsIni);
		return b;
	}
	inline void SetSkipScorecard(bool b)
	{
		if (!GConfig) return;
		GConfig->SetBool(TEXT("OpenTeeGolf"), TEXT("SkipScorecard"), b, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}
