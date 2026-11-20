// The turf's palette (bright, lime greens - DZ Grasses' showcase light): every cut's colour from one place, as a well-kept course (and DZ Grasses'
// showcase) has it - one green, its hue the same from the greens to the rough, each cut set apart by
// how light it is and how tall its grass stands, not by a colour of its own. The greens lightest,
// then the fairways and tees, the collars, the rough (and its first cut); the deep rough (the tall
// grass: "native") its own greyer green with straw. The numbers are set by what the game shows, not
// what they say: a green's short blades, lit from straight above, read about 40% lighter than its
// palette lightness, so it's set lower (unreal/tests/turf_colours.py measures it).
//
// What uses it (AOTGRuntimeCourse::TintGrass): the ground's material (TurfTint_<surface>: a
// multiplier on build_course.py's TURF colour, so Factor turns that into the palette's), our own
// blades on each cut (their Tint: the same multiplier - make_turf.py made them in their ground's
// colour) and Greensward's mats (their root and tip colours). The grass designer's colours turn
// these, as before. Tunable live: OTG.Palette.* (then OTG.Grass.Reset or a new hole to see the
// blades; the ground changes at once) - and checked by unreal/tests/turf_colours.ps1, which
// photographs each cut and compares.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

namespace otg
{
	namespace Palette
	{
		inline float Cv(const TCHAR* Name, float D)
		{
			const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
			return V ? V->GetFloat() : D;
		}

		// build_course.py TURF / FRINGE: the colours the ground's material and our blades were made in
		inline FLinearColor Made(const FString& S)
		{
			if (S == TEXT("green")) return FLinearColor(0.04f, 0.066f, 0.017f);
			if (S == TEXT("rough")) return FLinearColor(0.045f, 0.066f, 0.018f);
			if (S == TEXT("fringe")) return FLinearColor(0.036f, 0.057f, 0.015f);
			if (S == TEXT("native")) return FLinearColor(0.085f, 0.095f, 0.035f);
			return FLinearColor(0.04f, 0.061f, 0.016f);   // (fairway, tee)
		}

		// A cut's colour (linear): the one hue - red and blue as shares of the green, OTG.Palette.Red
		// and .Blue - at the cut's own lightness (its green, OTG.Palette.<cut>)
		inline FLinearColor Of(const FString& S)
		{
			// (the deep rough: the rough's green with some straw in it - OTG.Palette.NativeStraw of the
			// way to its own straw colour. Full straw, it stood out from the rough as a yellow patch)
			if (S == TEXT("native"))
			{
				const float N = Cv(TEXT("OTG.Palette.Native"), 0.9f);
				const FLinearColor Straw = Made(S) * FLinearColor(N, N * 1.03f, N * 1.1f);
				return FMath::Lerp(Of(TEXT("rough")), Straw, Cv(TEXT("OTG.Palette.NativeStraw"), 0.25f));
			}
			const float G = S == TEXT("green") ? Cv(TEXT("OTG.Palette.Green"), 0.09f)
				: S == TEXT("fringe") ? Cv(TEXT("OTG.Palette.Fringe"), 0.11f)
				: S == TEXT("rough") ? Cv(TEXT("OTG.Palette.Rough"), 0.1f)
				: Cv(TEXT("OTG.Palette.Fairway"), 0.12f);
			return FLinearColor(G * Cv(TEXT("OTG.Palette.Red"), 0.6f), G, G * Cv(TEXT("OTG.Palette.Blue"), 0.1f));
		}

		// What to multiply a surface's made colour by to draw it in the palette's
		inline FLinearColor Factor(const FString& S)
		{
			const FLinearColor M = Made(S), P = Of(S);
			return FLinearColor(P.R / M.R, P.G / M.G, P.B / M.B, 1.f);
		}

		// The surface a grass kind grows on, for its colour (none: a kind with its own, the tall grass's)
		inline FString SurfaceOf(const FString& Kind)
		{
			if (Kind == TEXT("green") || Kind == TEXT("fringe") || Kind == TEXT("fairway")) return Kind;
			if (Kind == TEXT("cut1") || Kind.StartsWith(TEXT("rough"))) return TEXT("rough");
			if (Kind.StartsWith(TEXT("deep_rough"))) return TEXT("native");
			return FString();
		}
	}
}
