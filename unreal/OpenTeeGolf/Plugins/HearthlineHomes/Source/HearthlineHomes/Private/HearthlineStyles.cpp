// Copyright __PUBLISHER__. All Rights Reserved.
// Style presets: each one fills an AHearthlineHouse from a random stream. Every value they set is an ordinary
// property, so a generated house can be edited afterwards.
#include "HearthlineHouse.h"

namespace
{
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }

	template <typename T, int32 N>
	const T& Pick(FRandomStream& R, const T (&Items)[N]) { return Items[R.RandRange(0, N - 1)]; }

	float Range(FRandomStream& R, float A, float B, float Step = 10.f) { return FMath::RoundToFloat(R.FRandRange(A, B) / Step) * Step; }
	bool Chance(FRandomStream& R, float P) { return R.FRand() < P; }

	const FLinearColor SidingColors[] = { Srgb(236, 236, 230), Srgb(226, 214, 186), Srgb(190, 192, 190), Srgb(150, 160, 135), Srgb(110, 130, 150), Srgb(52, 66, 88),
		Srgb(132, 50, 44), Srgb(178, 150, 115), Srgb(72, 74, 78), Srgb(230, 214, 150), Srgb(165, 190, 205), Srgb(205, 198, 180) };
	const FLinearColor EarthColors[] = { Srgb(120, 128, 100), Srgb(140, 110, 80), Srgb(96, 84, 70), Srgb(160, 140, 105), Srgb(84, 100, 104), Srgb(150, 80, 55) };
	const FLinearColor BrickTints[] = { Srgb(255, 255, 255), Srgb(215, 205, 205), Srgb(255, 235, 215), Srgb(190, 185, 190) };
	const FLinearColor StuccoColors[] = { Srgb(238, 236, 228), Srgb(226, 216, 196), Srgb(205, 200, 192), Srgb(214, 190, 160), Srgb(180, 184, 186) };
	const FLinearColor RoofColors[] = { Srgb(60, 60, 62), Srgb(92, 72, 58), Srgb(110, 112, 115), Srgb(96, 56, 48), Srgb(58, 74, 62), Srgb(38, 38, 40) };
	const FLinearColor TrimColors[] = { Srgb(240, 240, 236), Srgb(240, 240, 236), Srgb(232, 224, 204), Srgb(226, 226, 222) };
	const FLinearColor DoorColors[] = { Srgb(150, 36, 30), Srgb(36, 56, 92), Srgb(30, 30, 32), Srgb(30, 96, 100), Srgb(212, 170, 50), Srgb(110, 70, 40), Srgb(50, 90, 60), Srgb(240, 240, 236) };
	const FLinearColor ShutterColors[] = { Srgb(30, 34, 38), Srgb(40, 58, 50), Srgb(36, 50, 80), Srgb(110, 36, 32), Srgb(70, 54, 44) };
	const FLinearColor DarkTrim = Srgb(38, 40, 42);

	void PickColors(AHearthlineHouse& H, FRandomStream& R)
	{
		switch (H.Cladding)
		{
		case EHearthlineCladding::Brick: H.WallColor = Pick(R, BrickTints); break;
		case EHearthlineCladding::FieldStone: H.WallColor = FLinearColor::White; break;
		case EHearthlineCladding::Stucco: H.WallColor = Pick(R, StuccoColors); break;
		default: H.WallColor = (H.Style == EHearthlineStyle::Craftsman || H.Style == EHearthlineStyle::Cottage) && Chance(R, 0.6f) ? Pick(R, EarthColors) : Pick(R, SidingColors); break;
		}
		if (H.Style == EHearthlineStyle::Farmhouse && Chance(R, 0.7f)) { H.WallColor = Srgb(240, 240, 236); }
		H.RoofColor = Pick(R, RoofColors);
		H.TrimColor = Pick(R, TrimColors);
		H.SashColor = H.TrimColor;
		H.GutterColor = H.TrimColor;
		H.GarageDoorColor = Chance(R, 0.75f) ? H.TrimColor : H.WallColor;
		H.DoorColor = Pick(R, DoorColors);
		H.ShutterColor = Pick(R, ShutterColors);
		H.FoundationColor = H.FoundationCladding == EHearthlineCladding::Concrete ? Srgb(190, 190, 186) : FLinearColor::White;
		H.DeckColor = Chance(R, 0.5f) ? Srgb(160, 156, 150) : Srgb(150, 120, 90);
		switch (H.AccentCladding)
		{
		case EHearthlineCladding::Brick: case EHearthlineCladding::FieldStone: H.AccentColor = FLinearColor::White; break;
		default: H.AccentColor = Chance(R, 0.5f) ? H.TrimColor : FLinearColor::LerpUsingHSV(H.WallColor, Srgb(240, 240, 236), 0.45f); break;
		}
		H.Chimney.Color = FLinearColor::White;
		if (H.Style == EHearthlineStyle::Farmhouse || H.Style == EHearthlineStyle::Modern)
		{
			if (Chance(R, 0.75f)) { H.SashColor = DarkTrim; H.GutterColor = DarkTrim; }
			if (H.Style == EHearthlineStyle::Modern) { H.TrimColor = Chance(R, 0.5f) ? DarkTrim : Srgb(236, 236, 232); H.RoofColor = Srgb(70, 70, 72); H.GarageDoorColor = Srgb(92, 94, 96); }
			else { H.RoofColor = Chance(R, 0.6f) ? Srgb(44, 44, 46) : H.RoofColor; }
		}
	}

	FHearthlineVolume Vol(float X, float Y, float Width, float Depth, int32 Storeys, EHearthlineRoof Roof, bool bRidgeAlongWidth, float Pitch, float Overhang)
	{
		FHearthlineVolume V; V.Offset = FVector2D(X, Y); V.Width = Width; V.Depth = Depth; V.Storeys = Storeys; V.Roof = Roof; V.bRidgeAlongWidth = bRidgeAlongWidth; V.Pitch = Pitch; V.Overhang = Overhang;
		return V;
	}

	void BuildShape(AHearthlineHouse& H, FRandomStream& R)
	{
		H.Volumes.Reset(); H.Porches.Reset(); H.Openings.Reset();
		H.Chimney = FHearthlineChimney();
		H.bGarage = false; H.bFrontDoor = true; H.DoorVolume = 0; H.DoorSide = EHearthlineSide::Front; H.DoorPosition = 0.5f; H.bSidelights = false;
		H.Accent = EHearthlineAccent::None; H.bShutters = false; H.bDoubleHung = true; H.MuntinColumns = 1; H.MuntinRows = 1;
		H.CasingWidth = 10; H.bCornerBoards = true; H.bBandBoards = false; H.bGableVents = true; H.bGutters = true;
		H.FoundationHeight = Range(R, 30, 60, 5); H.FoundationCladding = EHearthlineCladding::Concrete;
		H.WindowWidth = 95; H.WindowHeight = 145; H.SillHeight = 85; H.WindowSpacing = 300; H.DoorStyle = EHearthlineDoorStyle::SixPanel; H.DoorWidth = 95;
		H.GarageDoors = 1; H.GarageDoorWidth = 490; H.GaragePosition = 0.5f; H.GarageSide = EHearthlineSide::Front; H.bGarageWindows = Chance(R, 0.5f);
		H.FasciaDepth = 16; H.ParapetHeight = 45;
		const float Side = Chance(R, 0.5f) ? 1.f : -1.f;   // which side secondary volumes go on (+1 = +Y)
		static const EHearthlineCladding SidingKinds[] = { EHearthlineCladding::LapSiding, EHearthlineCladding::LapSiding, EHearthlineCladding::Brick, EHearthlineCladding::BoardAndBatten, EHearthlineCladding::Stucco };

		switch (H.Style)
		{
		case EHearthlineStyle::Ranch:
		{
			const float W = Range(R, 1400, 1900, 50), D = Range(R, 780, 920, 20), Gw = Chance(R, 0.7f) ? 640.f : 400.f, Gd = Range(R, 660, 720, 20), Fwd = Range(R, 0, 220, 20);
			const EHearthlineRoof Roof = Chance(R, 0.5f) ? EHearthlineRoof::Hip : EHearthlineRoof::Gable;
			const float Pitch = Range(R, 20, 26, 1), Oh = Range(R, 45, 65, 5);
			H.Volumes.Add(Vol(0, -Side * Gw * 0.5f, W, D, 1, Roof, true, Pitch, Oh));
			H.Volumes.Add(Vol(D * 0.5f + Fwd - Gd * 0.5f, Side * W * 0.5f, Gw, Gd, 1, Roof, Fwd < 60, Pitch, Oh));
			H.Volumes[1].WindowSides = int32(EHearthlineSideMask::Right) | int32(EHearthlineSideMask::Left);
			H.bGarage = true; H.GarageVolume = 1; H.GarageDoorWidth = Gw > 500 ? 490.f : 270.f;
			H.Cladding = Pick(R, SidingKinds);
			H.WindowWidth = Range(R, 120, 180, 10); H.WindowHeight = 125; H.SillHeight = 95; H.WindowSpacing = Range(R, 340, 420, 20); H.MuntinColumns = Chance(R, 0.5f) ? 1 : 0; H.MuntinRows = 0;
			H.bShutters = Chance(R, 0.45f);
			H.DoorPosition = Side > 0 ? 0.28f : 0.72f;
			if (Chance(R, 0.55f))
			{
				FHearthlinePorch P; P.Start = H.DoorPosition - 0.13f; P.End = H.DoorPosition + 0.13f; P.Depth = 160; P.bRailing = false; P.StepsWidth = 150;
				H.Porches.Add(P);
			}
			H.Chimney.bEnabled = Chance(R, 0.5f); H.Chimney.AcrossWidth = Side > 0 ? 0.3f : 0.7f; H.Chimney.AlongDepth = 0.35f;
			if (H.Cladding == EHearthlineCladding::Brick && Chance(R, 0.5f)) { H.Accent = EHearthlineAccent::Gables; H.AccentCladding = EHearthlineCladding::LapSiding; }
			break;
		}
		case EHearthlineStyle::Colonial:
		{
			const float W = Range(R, 1100, 1300, 50), D = Range(R, 780, 900, 20), Pitch = Range(R, 30, 36, 1);
			H.Volumes.Add(Vol(0, 0, W, D, 2, EHearthlineRoof::Gable, true, Pitch, 30));
			H.Cladding = Chance(R, 0.5f) ? EHearthlineCladding::Brick : EHearthlineCladding::LapSiding;
			H.WindowSpacing = W / 5.f; H.WindowWidth = 90; H.WindowHeight = 150; H.MuntinColumns = 2; H.MuntinRows = 1; H.bShutters = true;
			H.bSidelights = Chance(R, 0.5f);
			H.Chimney.bEnabled = true; H.Chimney.AcrossWidth = Side > 0 ? 1.03f : -0.03f; H.Chimney.AlongDepth = 0.5f;
			if (Chance(R, 0.6f))
			{
				FHearthlinePorch P; P.Start = 0.5f - 170.f / W; P.End = 0.5f + 170.f / W; P.Depth = 170; P.bRailing = false; P.PostSize = 20; P.StepsWidth = 200;
				H.Porches.Add(P);
			}
			if (Chance(R, 0.55f))
			{
				const float Gw = 660, Gd = 680;
				H.Volumes.Add(Vol(-(D - Gd) * 0.5f, -Side * (W + Gw) * 0.5f, Gw, Gd, 1, EHearthlineRoof::Gable, true, Pitch, 30));
				H.Volumes[1].WindowSides = int32(EHearthlineSideMask::Back);
				H.bGarage = true; H.GarageVolume = 1;
			}
			break;
		}
		case EHearthlineStyle::CapeCod:
		{
			const float W = Range(R, 1000, 1200, 50), D = Range(R, 760, 860, 20);
			H.Volumes.Add(Vol(0, 0, W, D, 1, EHearthlineRoof::Gable, true, Range(R, 42, 47, 1), 22));
			H.Volumes[0].DormersFront = W > 1100 ? 3 : 2;
			H.Cladding = EHearthlineCladding::LapSiding;
			H.WindowSpacing = W / 4.2f; H.WindowWidth = 90; H.WindowHeight = 135; H.MuntinColumns = 2; H.MuntinRows = 1; H.bShutters = Chance(R, 0.8f);
			H.Chimney.bEnabled = true; H.Chimney.AcrossWidth = 0.5f; H.Chimney.AlongDepth = 0.45f; H.Chimney.Size = FVector2D(90, 90);
			break;
		}
		case EHearthlineStyle::Craftsman:
		{
			const float W = Range(R, 900, 1050, 50), D = Range(R, 1100, 1300, 50);
			H.Volumes.Add(Vol(0, 0, W, D, 1, EHearthlineRoof::Gable, false, Range(R, 27, 32, 1), 60));
			H.Volumes[0].StoreyHeight = 295;
			H.Cladding = EHearthlineCladding::LapSiding; H.Accent = EHearthlineAccent::Gables; H.AccentCladding = Chance(R, 0.6f) ? EHearthlineCladding::BoardAndBatten : EHearthlineCladding::Stucco;
			H.FoundationCladding = Chance(R, 0.6f) ? EHearthlineCladding::FieldStone : EHearthlineCladding::Brick; H.FoundationHeight = Range(R, 55, 80, 5);
			FHearthlinePorch P; P.Depth = Range(R, 220, 280, 20); P.bPostPiers = true; P.PostSize = 18; P.RoofPitch = 12; P.StepsPosition = 0.35f;
			H.Porches.Add(P);
			H.DoorPosition = 0.35f; H.DoorStyle = EHearthlineDoorStyle::Craftsman;
			H.WindowWidth = 100; H.WindowHeight = 150; H.MuntinColumns = 2; H.MuntinRows = 0; H.WindowSpacing = 330; H.FasciaDepth = 20;
			H.Chimney.bEnabled = Chance(R, 0.6f); H.Chimney.AcrossWidth = Side > 0 ? 1.03f : -0.03f; H.Chimney.AlongDepth = 0.45f; H.Chimney.Cladding = H.FoundationCladding;
			break;
		}
		case EHearthlineStyle::Farmhouse:
		{
			const float W = Range(R, 850, 950, 50), D = Range(R, 950, 1100, 50), Ww = Range(R, 650, 800, 50), Wd = Range(R, 650, 750, 50), Pitch = Range(R, 36, 42, 1);
			H.Volumes.Add(Vol(0, -Side * Ww * 0.5f, W, D, 2, EHearthlineRoof::Gable, false, Pitch, 35));
			H.Volumes.Add(Vol(-(D - Wd) * 0.5f + Range(R, 0, 150, 50), Side * W * 0.5f, Ww, Wd, Chance(R, 0.4f) ? 2 : 1, EHearthlineRoof::Gable, true, Pitch, 35));
			H.Cladding = EHearthlineCladding::BoardAndBatten; H.bCornerBoards = true;
			FHearthlinePorch P; P.Volume = 1; P.Depth = 230; P.PostSize = 14; P.StepsPosition = 0.5f;
			H.Porches.Add(P);
			H.DoorVolume = 1; H.DoorPosition = 0.5f; H.DoorStyle = EHearthlineDoorStyle::HalfGlass;
			H.WindowWidth = 85; H.WindowHeight = 165; H.SillHeight = 70; H.MuntinColumns = 1; H.MuntinRows = 1; H.WindowSpacing = 300;
			H.Chimney.bEnabled = Chance(R, 0.4f); H.Chimney.Volume = 1; H.Chimney.AcrossWidth = Side > 0 ? 1.03f : -0.03f; H.Chimney.Cladding = EHearthlineCladding::Brick;
			break;
		}
		case EHearthlineStyle::Modern:
		{
			const float W = Range(R, 1000, 1200, 50), D = Range(R, 850, 1000, 50), W2 = Range(R, 650, 800, 50), D2 = Range(R, 900, 1100, 50), Fwd = Range(R, 100, 300, 50);
			H.Volumes.Add(Vol(0, -Side * W2 * 0.5f, W, D, 2, EHearthlineRoof::Flat, true, 5, 0));
			H.Volumes.Add(Vol(D * 0.5f + Fwd - D2 * 0.5f, Side * W * 0.5f, W2, D2, 1, EHearthlineRoof::Flat, true, 5, 0));
			H.Volumes[1].StoreyHeight = 310; H.Volumes[1].bOverrideCladding = true; H.Volumes[1].Cladding = Chance(R, 0.5f) ? EHearthlineCladding::LapSiding : EHearthlineCladding::FieldStone;
			H.Volumes[1].bOverrideColor = true; H.Volumes[1].WallColor = H.Volumes[1].Cladding == EHearthlineCladding::FieldStone ? FLinearColor::White : Srgb(150, 108, 74);
			H.Volumes[1].WindowSides = int32(EHearthlineSideMask::Right) | int32(EHearthlineSideMask::Left) | int32(EHearthlineSideMask::Back);
			H.Cladding = EHearthlineCladding::Stucco; H.bCornerBoards = false; H.CasingWidth = 0; H.bGutters = false;
			H.bGarage = true; H.GarageVolume = 1; H.GarageDoorWidth = W2 > 700 ? 490.f : 270.f; H.bGarageWindows = false;
			H.WindowWidth = Range(R, 180, 240, 20); H.WindowHeight = 160; H.SillHeight = 60; H.WindowSpacing = 420; H.bDoubleHung = false; H.MuntinColumns = 1; H.MuntinRows = 0;
			H.DoorStyle = EHearthlineDoorStyle::Modern; H.DoorPosition = Side > 0 ? 0.25f : 0.75f; H.DoorWidth = 105;
			H.FoundationHeight = 15;
			break;
		}
		case EHearthlineStyle::Cottage:
		{
			const float W = Range(R, 800, 950, 50), D = Range(R, 700, 800, 50), Pw = Range(R, 320, 400, 20), Pitch = Range(R, 38, 46, 1);
			H.Volumes.Add(Vol(0, 0, W, D, 1, EHearthlineRoof::Gable, true, Pitch, 25));
			H.Volumes.Add(Vol(D * 0.5f + 60, Side * (W * 0.5f - Pw * 0.5f - 60), Pw, 280, 1, EHearthlineRoof::Gable, false, Pitch + 4, 22));
			H.Volumes[1].WindowSides = int32(EHearthlineSideMask::Front);
			H.Cladding = Chance(R, 0.5f) ? EHearthlineCladding::Stucco : EHearthlineCladding::FieldStone;
			H.Accent = EHearthlineAccent::Gables; H.AccentCladding = Chance(R, 0.5f) ? EHearthlineCladding::LapSiding : EHearthlineCladding::Stucco;
			H.DoorPosition = Side > 0 ? 0.68f : 0.32f; H.DoorStyle = EHearthlineDoorStyle::HalfGlass;
			H.WindowWidth = 85; H.WindowHeight = 125; H.SillHeight = 90; H.MuntinColumns = 2; H.MuntinRows = 2; H.WindowSpacing = 280; H.bShutters = Chance(R, 0.5f);
			H.Chimney.bEnabled = true; H.Chimney.AcrossWidth = Side > 0 ? -0.03f : 1.03f; H.Chimney.Cladding = EHearthlineCladding::FieldStone; H.bCornerBoards = false;
			break;
		}
		default: // SplitLevel
		{
			const float W1 = Range(R, 800, 950, 50), W2 = Range(R, 700, 800, 50), D = Range(R, 800, 900, 50), Pitch = Range(R, 20, 25, 1);
			H.FoundationHeight = 20;
			H.Volumes.Add(Vol(0, -Side * W2 * 0.5f, W1, D, 1, EHearthlineRoof::Gable, true, Pitch, 50));
			H.Volumes[0].FloorOffset = 110;
			H.Volumes.Add(Vol(40, Side * W1 * 0.5f, W2, D + 80, 2, EHearthlineRoof::Gable, true, Pitch, 50));
			H.Volumes[1].StoreyHeight = 260;
			H.bGarage = true; H.GarageVolume = 1; H.GarageDoorWidth = 490;
			H.Cladding = EHearthlineCladding::Brick; H.Accent = EHearthlineAccent::UpperStoreys; H.AccentCladding = EHearthlineCladding::LapSiding;
			H.DoorPosition = Side > 0 ? 0.2f : 0.8f;
			H.WindowWidth = 150; H.WindowHeight = 120; H.SillHeight = 95; H.WindowSpacing = 380; H.MuntinColumns = 1; H.MuntinRows = 0; H.bShutters = Chance(R, 0.4f);
			break;
		}
		}
	}
}

void AHearthlineHouse::ApplyStyle()
{
	FRandomStream R(Seed);
	BuildShape(*this, R);
	PickColors(*this, R);

	// planting draws from its own stream so that it never changes the shape a seed produces
	FRandomStream P(Seed * 7 + 1013);
	static const FLinearColor Greens[] = { Srgb(236, 240, 222), Srgb(224, 232, 196), Srgb(206, 226, 204), Srgb(240, 232, 200) };
	bFoundationShrubs = Chance(P, 0.85f);
	ShrubDensity = Range(P, 0.5f, 0.95f, 0.05f);
	LotTrees = P.RandRange(1, 3);
	PlantSeed = P.RandRange(1, 9999);
	FoliageColor = Pick(P, Greens);
	bFence = Chance(P, 0.65f);
	FenceStyle = (Style == EHearthlineStyle::Modern || Chance(P, 0.45f)) ? EHearthlineFence::Privacy : EHearthlineFence::Picket;
	FenceHeight = 0.f;
	FenceColor = FenceStyle == EHearthlineFence::Picket ? Srgb(240, 240, 236) : (Chance(P, 0.5f) ? Srgb(206, 180, 146) : Srgb(168, 150, 130));

	Modify();
	BakedMesh = nullptr;
	Rebuild();
}

void AHearthlineHouse::Randomize()
{
	Seed = FMath::RandRange(1, 999999);
	ApplyStyle();
}

void AHearthlineHouse::RandomizeColors()
{
	FRandomStream R(FMath::RandRange(1, 999999));
	PickColors(*this, R);
	Modify();
	Rebuild();
}
