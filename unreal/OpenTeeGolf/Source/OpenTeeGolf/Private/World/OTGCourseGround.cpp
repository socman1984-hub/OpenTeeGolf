// The runtime course's ground (see OTGRuntimeCourse.h): the heightmap made into tiles of mesh, the
// ponds' banks lifted and held, the water's levels settled, and the ground's height anywhere.

#include "World/OTGRuntimeCourse.h"

#include "Course/OTGCourseData.h"
#include "Course/OTGGlb.h"
#include "World/OTGGrass.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Course/OTGTextures.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "World/OTGKiteDemo.h"
#include "World/OTGPalette.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRuntimeCourse, Log, All);

#include "World/OTGCourseShared.h"

// The ground: the heightmap in tiles, each a mesh with four levels of detail (every sample, every
// 2nd, 4th, 8th), its edges hung with a skirt so tiles at different detail meet without a gap
void AOTGRuntimeCourse::BuildGround(TFunction<void(float, const FString&)> Progress)
{
	PrepareGround();
	const int32 Total = TileCount * TileCount;
	for (int32 From = 0; From < Total; From += 32)
	{
		BuildGroundTiles(From, FMath::Min(From + 32, Total));
		if (Progress) Progress(0.05f + 0.5f * FMath::Min(From + 32, Total) / float(Total), TEXT("The ground"));
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Ground: %d tiles of %d samples, %d levels of detail"), Tiles.Num(), TileQuads, Lods);
}

void AOTGRuntimeCourse::PrepareGround()
{
	UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_CourseGround_Runtime"));
	if (!Base) { UE_LOG(LogOTGRuntimeCourse, Error, TEXT("No M_CourseGround_Runtime in the game's content (unreal/make_assets.ps1)")); return; }
	GroundMat = UMaterialInstanceDynamic::Create(Base, this);
	SetPictures(GroundMat);
	// (the fairways' and greens' colour, as the grass designer saved it)
	for (const auto& P : SavedTurfLooks()) SetTurfLook(P.Key, P.Value);
	LiftPondBanks();
	SettleWater();
	PlanCreeks();   // (and HoldPonds, where a creek leaves a pond over it)
	const int32 N = Data->HeightSamples;
	TileQuads = N > 2049 ? 128 : 64;   // (a tile's samples a side: bigger tiles on a big course)
	TileCount = FMath::DivideAndRoundUp(N - 1, TileQuads);
	PlanGroundTiles();
}

// Which of the ground's tiles to draw. The ground is a square round the course, and past the course
// - fields, roads, houses - it drew the rough: the rough's colour from a blurred photo, a flat dark
// green band between the course and the land round it (the satellite photos, BuildRealFarLand).
// Where the export has that land, a tile is drawn only if it, or one next to it, has some of the
// course in it - its surfaces (not the rough: that's everywhere) or its trees; the land round it
// shows through the rest, the grass kept off it.
void AOTGRuntimeCourse::PlanGroundTiles()
{
	TileKeep.Reset();
	if (!FPaths::FileExists(FPaths::Combine(Data->ExportDir, TEXT("farland.json"))) || TileCount <= 0) return;
	const int32 Count = TileCount, N = Data->HeightSamples;
	const double TileCm = TileQuads * Data->MetresPerSample * 100.0;
	// the course's surfaces in each tile (the masks: 0.5 on each outline) - not the rough or the meadow:
	// the meadow's mask is speckled over the fields all round, and kept them all
	TArray<bool> Has;
	Has.SetNumZeroed(Count * Count);
	auto TileOf = [&](double XCm, double YCm) {
		const int32 tx = FMath::FloorToInt(XCm / TileCm), ty = FMath::FloorToInt(YCm / TileCm);
		return tx >= 0 && ty >= 0 && tx < Count && ty < Count ? ty * Count + tx : INDEX_NONE;
	};
	for (const TCHAR* K : { TEXT("fairway"), TEXT("green"), TEXT("tee"), TEXT("bunker"), TEXT("water"), TEXT("stream"), TEXT("path"), TEXT("fringe") })
	{
		const otg::FPicture* P = MaskPixels.Find(K);
		if (!P || !P->W || P->Pixels.Num() < P->W * P->H) continue;
		for (int32 j = 0; j < P->H; j++)
			for (int32 i = 0; i < P->W; i++)
				if (P->Pixels[j * P->W + i] > 128)
					if (const int32 t = TileOf((i + 0.5) / P->W * LandCm, (j + 0.5) / P->H * LandCm); t != INDEX_NONE) Has[t] = true;
	}
	// and its woods: a tile with a stand of trees in it (a few here and there don't count)
	TArray<int32> TreesIn;
	TreesIn.SetNumZeroed(Count * Count);
	if (const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("trees.json"))))
		if (J.IsValid() && J->Type == EJson::Array)
			for (const TSharedPtr<FJsonValue>& V : J->AsArray())
				if (const TSharedPtr<FJsonObject> T = V->AsObject())
					if (const int32 t = TileOf(T->GetNumberField(TEXT("x")), T->GetNumberField(TEXT("y"))); t != INDEX_NONE) TreesIn[t]++;
	const int32 Woods = FMath::Max(4, FMath::RoundToInt(15.0 * FMath::Square(TileCm / 6400.0)));   // (15 to a 64 m tile)
	// drawn: within about 130 m of the course's surfaces, or woods
	const int32 R = FMath::Max(1, FMath::CeilToInt(13000.0 / TileCm));
	TileKeep.SetNumZeroed(Count * Count);
	int32 Kept = 0;
	for (int32 ty = 0; ty < Count; ty++)
		for (int32 tx = 0; tx < Count; tx++)
		{
			bool bNear = TreesIn[ty * Count + tx] >= Woods;
			for (int32 dy = -R; dy <= R && !bNear; dy++)
				for (int32 dx = -R; dx <= R && !bNear; dx++)
				{
					const int32 x = tx + dx, y = ty + dy;
					bNear = x >= 0 && y >= 0 && x < Count && y < Count && Has[y * Count + x];
				}
			TileKeep[ty * Count + tx] = bNear;
			Kept += bNear;
		}
	// the grass kept off what's left out (it grows by the masks: the rough's and the meadow's)
	for (const TCHAR* K : { TEXT("rough"), TEXT("native") })
		if (otg::FPicture* P = MaskPixels.Find(K))
			for (int32 j = 0; j < P->H && P->Pixels.Num() >= P->W * P->H; j++)
				for (int32 i = 0; i < P->W; i++)
					if (!GroundDrawnAt((i + 0.5) / P->W * LandCm, (j + 0.5) / P->H * LandCm)) P->Pixels[j * P->W + i] = 0;
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Ground: %d of %d tiles near the course drawn, the land round it in the rest"), Kept, Count * Count);
	(void)N;
}

bool AOTGRuntimeCourse::GroundDrawnAt(double XCm, double YCm) const
{
	if (!TileKeep.Num()) return true;
	const double TileCm = TileQuads * Data->MetresPerSample * 100.0;
	const int32 tx = FMath::FloorToInt(XCm / TileCm), ty = FMath::FloorToInt(YCm / TileCm);
	if (tx < 0 || ty < 0 || tx >= TileCount || ty >= TileCount) return false;
	return TileKeep[ty * TileCount + tx];
}

// The heights the ground is drawn with. Round each pond (water_bodies.json's lakes: outline and
// level), the odd sample of the bank just outside the shoreline lies a little below the water: the
// water (drawn a few cm up, under the banks) shows through it as a dark pit, a row of them along the
// shore. Those within 3 m of the outline and up to 25 cm under the water are lifted just clear of
// it (the ball still plays on the course's own heights: a few cm)
void AOTGRuntimeCourse::LiftPondBanks()
{
	PondLevels.Reset();
	PondOutlines.Reset();
	GroundHeights = Data->Heights;
	const int32 N = Data->HeightSamples;
	if (GroundHeights.Num() != N * N) return;
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("water_bodies.json")));
	if (!J.IsValid() || J->Type != EJson::Array) return;
	const double Step = Data->MetresPerSample * 100.0;
	const double ZScale = Data->HeightRange * 100.0 / 65535.0;
	const double Clear = 8.0;   // cm over the water a metre and a half out (it's drawn WaterLiftCm up)
	constexpr int32 Reach = 3;                  // samples out from the outline
	int32 Lifted = 0;
	for (const TSharedPtr<FJsonValue>& B : J->AsArray())
	{
		const TSharedPtr<FJsonObject> O = B->AsObject();
		if (!O.IsValid() || O->GetStringField(TEXT("type")) != TEXT("lake")) continue;
		const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
		if (!O->TryGetArrayField(TEXT("points"), Pts) || Pts->Num() < 3) continue;
		TArray<FVector2D> Poly;
		double Level = 0;
		for (const TSharedPtr<FJsonValue>& P : *Pts)
		{
			const TSharedPtr<FJsonObject> Q = P->AsObject();
			Poly.Add(FVector2D(Q->GetNumberField(TEXT("x")), Q->GetNumberField(TEXT("y"))) / Step);
			Level = Q->GetNumberField(TEXT("z"));
		}
		FBox2D Box(Poly);
		const int32 Margin = Reach + 1;
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X) - Margin), Y0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y) - Margin);
		const int32 X1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.X) + Margin), Y1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.Y) + Margin);
		const int32 W = X1 - X0 + 1, H = Y1 - Y0 + 1;
		if (W <= 0 || H <= 0) continue;
		// inside the outline (crossings), then out from it a sample at a time
		TArray<uint8> Ring;
		Ring.SetNumZeroed(W * H);   // 1: water; 2..: that many - 1 samples out
		for (int32 y = 0; y < H; y++)
			for (int32 x = 0; x < W; x++)
			{
				const FVector2D S(X0 + x, Y0 + y);
				bool bIn = false;
				for (int32 a = 0, b = Poly.Num() - 1; a < Poly.Num(); b = a++)
					if ((Poly[a].Y > S.Y) != (Poly[b].Y > S.Y) && S.X < (Poly[b].X - Poly[a].X) * (S.Y - Poly[a].Y) / (Poly[b].Y - Poly[a].Y) + Poly[a].X) bIn = !bIn;
				if (bIn) Ring[y * W + x] = 1;
			}
		for (int32 r = 2; r <= Reach + 1; r++)
		{
			TArray<uint8> Next = Ring;
			for (int32 y = 0; y < H; y++)
				for (int32 x = 0; x < W; x++)
				{
					if (Ring[y * W + x]) continue;
					for (int32 dy = -1; dy <= 1 && !Next[y * W + x]; dy++)
						for (int32 dx = -1; dx <= 1; dx++)
						{
							const int32 u = x + dx, v = y + dy;
							if (u >= 0 && v >= 0 && u < W && v < H && Ring[v * W + u] == r - 1) { Next[y * W + x] = uint8(r); break; }
						}
				}
			Ring = MoveTemp(Next);
		}
		// (the water up to the bank's top: the low end of the ground 2-3 samples out round it - where
		// the grass starts - less a few cm. The outline's own level, its lowest point, often sat well
		// under it, and a strip of dug bank showed all round between the water and the grass)
		{
			TArray<double> Rim;
			for (int32 i = 0; i < W * H; i++)
				if (Ring[i] >= 3) Rim.Add(GroundHeights[(Y0 + i / W) * N + (X0 + i % W)] * ZScale);
			if (Rim.Num() > 8)
			{
				Rim.Sort();
				const double Top = Rim[Rim.Num() / 10] - 6.0;
				if (Top > Level) Level = FMath::Min(Top, Level + 200.0);
			}
		}
		PondLevels.Add(Level);
		// The bank shaped round the outline, rounded (its corners every 5 m or so were the water's edge:
		// straight runs and points): outside it the ground rises from just over the water to clear of it
		// over a metre and a half, inside it falls away under the water - one smooth slope through the
		// waterline, so the water's edge is the rounded outline. (Lifting the odd low sample on its own
		// made a sawtooth along the shore, each lifted sample a tooth.) Only the ground's drawing: the
		// ball plays on the course's own heights
		TArray<FVector2D> Round = Poly;
		for (int32 it = 0; it < 3 && Round.Num() >= 3; it++)
		{
			TArray<FVector2D> Next;
			for (int32 i = 0; i < Round.Num(); i++)
			{
				const FVector2D Pa = Round[i], Pb = Round[(i + 1) % Round.Num()];
				Next.Add(Pa * 0.75 + Pb * 0.25); Next.Add(Pa * 0.25 + Pb * 0.75);
			}
			Round = MoveTemp(Next);
		}
		TArray<FVector2D>& Kept = PondOutlines.AddDefaulted_GetRef();
		for (const FVector2D& P : Round) Kept.Add(P * Step);
		const double M = Data->MetresPerSample;
		for (int32 y = 0; y < H; y++)
			for (int32 x = 0; x < W; x++)
			{
				const FVector2D S(X0 + x, Y0 + y);
				bool bIn = false;
				double D2 = TNumericLimits<double>::Max();
				for (int32 ia = 0, ib = Round.Num() - 1; ia < Round.Num(); ib = ia++)
				{
					const FVector2D& Pa = Round[ia]; const FVector2D& Pb = Round[ib];
					if ((Pa.Y > S.Y) != (Pb.Y > S.Y) && S.X < (Pb.X - Pa.X) * (S.Y - Pa.Y) / (Pb.Y - Pa.Y) + Pa.X) bIn = !bIn;
					const FVector2D AB = Pb - Pa;
					const double T = FMath::Clamp(FVector2D::DotProduct(S - Pa, AB) / FMath::Max(AB.SizeSquared(), 1e-9), 0.0, 1.0);
					D2 = FMath::Min(D2, FVector2D::DistSquared(S, Pa + AB * T));
				}
				const double D = FMath::Sqrt(D2) * M;   // (metres from the rounded outline)
				uint16& Hv = GroundHeights[(Y0 + y) * N + (X0 + x)];
				const double Z = Hv * ZScale;
				double Z2 = Z;
				// (the export carves the bed with a hard step along its raw outline - corners every 5 m -
				// a little ledge along the shore: near the outline the ground is blurred over 2 m first,
				// the step a slope)
				if (D < 3.5)
				{
					double Sum = 0, Wt = 0;
					for (int32 dy = -2; dy <= 2; dy++)
						for (int32 dx = -2; dx <= 2; dx++)
						{
							const int32 u = FMath::Clamp(X0 + x + dx, 0, N - 1), v = FMath::Clamp(Y0 + y + dy, 0, N - 1);
							const double w = FMath::Exp(-(dx * dx + dy * dy) / 2.5);
							Sum += Data->Heights[v * N + u] * ZScale * w; Wt += w;
						}
					Z2 = FMath::Lerp(Sum / Wt, Z, FMath::SmoothStep(2.0, 3.5, D));
				}
				if (!bIn && D < 3.0)
				{
					// (from 2 cm over the water at the outline to Clear + 2 a metre out; ground more
					// than 60 cm under the water - a ditch beyond the bank - left as it is)
					// (the lift fading out from 1.5 m to 3 m: lifted to a fixed height right out to 3 m, a ridge
					// ran along the shore where it stopped, a straight step)
					const double Want = Level + 2.0 + Clear * FMath::SmoothStep(0.0, 1.0, D);
					if (Z2 < Want && Z2 > Level - 60.0) Z2 += (Want - Z2) * (1.0 - FMath::SmoothStep(1.5, 3.0, D));
				}
				else if (bIn && D < 2.0)
				{
					// (just under the water at the outline, 20 cm under it a metre and a half in)
					const double Cap = Level - 2.0 - 18.0 * FMath::SmoothStep(0.0, 1.5, D);
					if (Z2 > Cap && Z2 < Level + 60.0) Z2 = Cap;
				}
				if (FMath::Abs(Z2 - Z) > 0.5) { Hv = uint16(FMath::Clamp(FMath::RoundToDouble(Z2 / ZScale), 0.0, 65535.0)); Lifted++; }
			}
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Pond banks: %d samples shaped round the rounded outlines"), Lifted);
}

// A pond held by its bank where a creek leaves it from below (PondHolds: PlanCreeks). There the ground
// beside the pond - the creek's channel, dug by SettleWater under the export's own water - lay well
// under the pond's water, which stood over it in mid-air, the sheet's square edge showing as stairs.
// Within 15 m of the creek's end the ground's raised to a berm: its top 3 m wide just over the water,
// its outside falling about 30 degrees to the ground, fading out from 10 to 15 m (raised all round
// every pond, it left a sawtooth along their shores). Only the ground's drawing
void AOTGRuntimeCourse::HoldPonds()
{
	BermCells.Reset();
	if (FParse::Param(FCommandLine::Get(), TEXT("oldbanks"))) return;   // (-oldbanks: as LiftPondBanks left them)
	const int32 N = Data->HeightSamples;
	if (GroundHeights.Num() != N * N) return;
	const double Step = Data->MetresPerSample * 100.0;
	const double ZScale = Data->HeightRange * 100.0 / 65535.0;
	constexpr double BermCm = 1200.0, Fall = 0.55, Clear = 8.0;   // (its reach; its fall, cm a cm: about 30 degrees)
	constexpr double Near = 1000.0, Far = 1500.0;   // (round the creek's end: full, fading out)
	int32 Raised = 0;
	BermCells.Reset();
	for (const FPondHold& Hold : PondHolds)
	{
		const int32 k = Hold.Pond;
		if (!PondOutlines.IsValidIndex(k) || !PondLevels.IsValidIndex(k)) continue;
		const TArray<FVector2D>& R = PondOutlines[k];
		if (R.Num() < 3) continue;
		const double Level = PondLevels[k];
		const int32 Before = Raised;
		FVector2D Sum(0, 0); double MaxUp = 0;
		const FBox2D Box(Hold.At - FVector2D(Far + Step), Hold.At + FVector2D(Far + Step));
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X / Step)), Y0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y / Step));
		const int32 X1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.X / Step)), Y1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.Y / Step));
		for (int32 y = Y0; y <= Y1; y++)
			for (int32 x = X0; x <= X1; x++)
			{
				uint16& Hv = GroundHeights[y * N + x];
				const double Z = Hv * ZScale;
				if (Z >= Level + 2.0 + Clear) continue;   // (high enough whatever: the usual bank)
				const FVector2D S(x * Step, y * Step);
				const double K = 1.0 - FMath::SmoothStep(Near, Far, FVector2D::Distance(S, Hold.At));
				if (K <= 0) continue;
				bool bIn = false;
				double D2 = TNumericLimits<double>::Max();
				for (int32 ia = 0, ib = R.Num() - 1; ia < R.Num(); ib = ia++)
				{
					const FVector2D& Pa = R[ia]; const FVector2D& Pb = R[ib];
					if ((Pa.Y > S.Y) != (Pb.Y > S.Y) && S.X < (Pb.X - Pa.X) * (S.Y - Pa.Y) / (Pb.Y - Pa.Y) + Pa.X) bIn = !bIn;
					const FVector2D AB = Pb - Pa;
					const double T = FMath::Clamp(FVector2D::DotProduct(S - Pa, AB) / FMath::Max(AB.SizeSquared(), 1e-9), 0.0, 1.0);
					D2 = FMath::Min(D2, FVector2D::DistSquared(S, Pa + AB * T));
				}
				if (bIn) continue;
				const double D = FMath::Sqrt(D2);   // (cm from the rounded outline)
				if (D > BermCm) continue;
				// (its top 3 m wide, just over the water: the water's drawn that far past the outline - a lake's
				// rectangle, a still pond's sheet - and over a berm falling from 1.5 m it hung in the air, black)
				const double Berm = FMath::Lerp(Z, Level + 2.0 + Clear * FMath::SmoothStep(0.0, 100.0, D) - FMath::Max(0.0, D - 320.0) * Fall, K);
				if (Berm > Z + 0.5) { Hv = uint16(FMath::Clamp(FMath::CeilToDouble(Berm / ZScale), 0.0, 65535.0)); Raised++; Sum += S; MaxUp = FMath::Max(MaxUp, Berm - Z); }
				if (Berm > Z + 15.0) BermCells.Add(FIntPoint(x, y));
			}
		if (Raised > Before) UE_LOG(LogOTGRuntimeCourse, Display, TEXT("  pond %d: berm at %d samples round %.0f %.0f, up to %.0f cm"), k, Raised - Before, Sum.X / (Raised - Before), Sum.Y / (Raised - Before), MaxUp);
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Ponds held: the ground raised to a berm at %d samples"), Raised);
}

// The water's surface (water.glb: a point on each of the ground's samples, at its water's level),
// before the ground is drawn. A stream's level comes in steps along it - a line across it at each,
// the water a few cm higher on one side - so the levels are smoothed into an even slope (the edges
// never raised: they're tucked under the banks). And the ground under the water is kept below it:
// where the ground rose through it, brown patches showed in the middle of a pond or stream.
void AOTGRuntimeCourse::SettleWater()
{
	bWaterSettled = true;
	WaterPos.Reset();
	WaterIdx.Reset();
	otg::FGlbMesh G;
	if (!otg::ReadGlb(FPaths::Combine(Data->ExportDir, TEXT("water.glb")), G) || !G.Positions.Num()) return;
	WaterPos = MoveTemp(G.Positions);
	WaterIdx = MoveTemp(G.Indices);
	const int32 N = Data->HeightSamples;
	const double Mps = Data->MetresPerSample;
	if (GroundHeights.Num() != N * N || Mps <= 0) return;
	// each point's sample (the surface is on the ground's grid; one off it is left alone)
	TArray<int32> At;
	At.Init(INDEX_NONE, N * N);
	TArray<int32> Cell;
	Cell.Init(INDEX_NONE, WaterPos.Num());
	for (int32 v = 0; v < WaterPos.Num(); v++)
	{
		const double fi = WaterPos[v].X / Mps, fj = WaterPos[v].Z / Mps;
		const int32 i = FMath::RoundToInt(fi), j = FMath::RoundToInt(fj);
		if (i < 0 || j < 0 || i >= N || j >= N || FMath::Abs(fi - i) > 0.05 || FMath::Abs(fj - j) > 0.05 || At[j * N + i] != INDEX_NONE) continue;
		At[j * N + i] = v;
		Cell[v] = j * N + i;
	}
	// an edge point: one without water on all four sides
	auto Nb = [&](int32 k, int32 d) -> int32 {
		const int32 i = k % N, j = k / N;
		const int32 u = i + (d == 0 ? 1 : d == 1 ? -1 : 0), w = j + (d == 2 ? 1 : d == 3 ? -1 : 0);
		return u < 0 || w < 0 || u >= N || w >= N ? INDEX_NONE : At[w * N + u];
	};
	TArray<bool> Edge;
	Edge.Init(true, WaterPos.Num());
	for (int32 v = 0; v < WaterPos.Num(); v++)
		if (Cell[v] != INDEX_NONE) Edge[v] = Nb(Cell[v], 0) == INDEX_NONE || Nb(Cell[v], 1) == INDEX_NONE || Nb(Cell[v], 2) == INDEX_NONE || Nb(Cell[v], 3) == INDEX_NONE;
	// the levels smoothed: a few passes of each point to its neighbours' average (a step of 20 cm
	// spreads over 10 m or so; a still pond stays flat)
	TArray<float> L, Next;
	L.SetNum(WaterPos.Num());
	for (int32 v = 0; v < WaterPos.Num(); v++) L[v] = WaterPos[v].Y;
	const TArray<float> L0 = L;
	for (int32 Pass = 0; Pass < 24; Pass++)
	{
		Next = L;
		for (int32 v = 0; v < WaterPos.Num(); v++)
		{
			if (Cell[v] == INDEX_NONE) continue;
			double Sum = L[v] * 2.0, W = 2.0;
			for (int32 d = 0; d < 4; d++)
				if (const int32 u = Nb(Cell[v], d); u != INDEX_NONE) { Sum += L[u]; W += 1.0; }
			Next[v] = float(Sum / W);
		}
		Swap(L, Next);
	}
	// how far each point is from the water's edge (samples): the bed shelves down from the banks - the
	// water's see-through where it's shallow, and a ridge of bed left between two pieces of a stream,
	// or a high spot, showed through it as a light band or a brown patch
	TArray<int32> FromEdge;
	FromEdge.Init(MAX_int32, WaterPos.Num());
	{
		TArray<int32> Front;
		for (int32 v = 0; v < WaterPos.Num(); v++) if (Cell[v] != INDEX_NONE && Edge[v]) { FromEdge[v] = 0; Front.Add(v); }
		for (int32 r = 1; Front.Num() && r < 64; r++)
		{
			TArray<int32> Out;
			for (const int32 v : Front)
				for (int32 d = 0; d < 4; d++)
					if (const int32 u = Nb(Cell[v], d); u != INDEX_NONE && FromEdge[u] == MAX_int32) { FromEdge[u] = r; Out.Add(u); }
			Front = MoveTemp(Out);
		}
	}
	int32 Moved = 0, Lowered = 0;
	const double ZScale = Data->HeightRange * 100.0 / 65535.0;   // (cm per height step)
	for (int32 v = 0; v < WaterPos.Num(); v++)
	{
		if (Cell[v] == INDEX_NONE) continue;
		const float Lv = Edge[v] ? FMath::Min(L[v], L0[v]) : L[v];
		if (FMath::Abs(Lv - L0[v]) > 0.01f) Moved++;
		WaterPos[v].Y = Lv;
		if (Edge[v]) continue;
		// (the water is drawn WaterLiftCm up; the heights are cm over the export's base, as its levels)
		uint16& Hv = GroundHeights[Cell[v]];
		// (30 cm down a sample in from the edge, then 35 cm more a metre, to 1.6 m)
		const double DepthCm = FMath::Min(160.0, 30.0 + 35.0 * (FMath::Min(FromEdge[v], 64) - 1) * Mps);
		const double Want = Lv * 100.0 - DepthCm;
		if (Hv * ZScale > Want) { Hv = uint16(FMath::Clamp(FMath::FloorToDouble(Want / ZScale), 0.0, 65535.0)); Lowered++; }
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Water: %d points' levels smoothed, the ground lowered under it at %d"), Moved, Lowered);
}

// Tiles From..To-1 of the ground (row by row)
void AOTGRuntimeCourse::BuildGroundTiles(int32 From, int32 To)
{
	if (!GroundMat) return;

	const int32 N = Data->HeightSamples;
	const double Step = Data->MetresPerSample * 100.0;
	const double ZScale = Data->HeightRange * 100.0 / 65535.0;
	const TArray<uint16>& Hs = GroundHeights.Num() == N * N ? GroundHeights : Data->Heights;
	auto Z = [&](int32 i, int32 j) { return double(Hs[FMath::Clamp(j, 0, N - 1) * N + FMath::Clamp(i, 0, N - 1)]) * ZScale; };
	auto Normal = [&](int32 i, int32 j) {
		return FVector((Z(i - 1, j) - Z(i + 1, j)) / (2 * Step), (Z(i, j - 1) - Z(i, j + 1)) / (2 * Step), 1.0).GetSafeNormal();
	};
	const int32 Quads = TileQuads, Count = TileCount;
	for (int32 Idx = From; Idx < To; Idx++)
		{
			const int32 tx = Idx % Count, ty = Idx / Count;
			if (TileKeep.IsValidIndex(Idx) && !TileKeep[Idx]) continue;   // (the land round it shows here: PlanGroundTiles)
			const int32 I0 = tx * Quads, J0 = ty * Quads;
			const int32 I1 = FMath::Min(I0 + Quads, N - 1), J1 = FMath::Min(J0 + Quads, N - 1);
			TArray<FMeshDescription> Descs;
			Descs.SetNum(Lods);
			for (int32 L = 0; L < Lods; L++)
			{
				const int32 S = 1 << L;
				// the samples along each side at this detail (always the tile's last one)
				auto Line = [S](int32 A, int32 B) { TArray<int32> Out; for (int32 k = A; k < B; k += S) Out.Add(k); Out.Add(B); return Out; };
				const TArray<int32> Is = Line(I0, I1), Js = Line(J0, J1);
				FMeshDescription& MD = Descs[L];
				FStaticMeshAttributes Attr(MD);
				Attr.Register();
				auto Pos = Attr.GetVertexPositions();
				auto Nrm = Attr.GetVertexInstanceNormals();
				auto Tan = Attr.GetVertexInstanceTangents();
				auto Sign = Attr.GetVertexInstanceBinormalSigns();
				auto UV = Attr.GetVertexInstanceUVs();
				auto Slot = Attr.GetPolygonGroupMaterialSlotNames();
				const FPolygonGroupID G = MD.CreatePolygonGroup();
				Slot[G] = TEXT("Ground");
				const int32 NI = Is.Num(), NJ = Js.Num();
				TArray<FVertexInstanceID> Inst;
				Inst.SetNum(NI * NJ);
				auto AddVertex = [&](const FVector& P, const FVector& N3) {
					const FVertexID V = MD.CreateVertex();
					Pos[V] = FVector3f(P);
					const FVertexInstanceID VI = MD.CreateVertexInstance(V);
					Nrm[VI] = FVector3f(N3);
					Tan[VI] = FVector3f((FVector(1, 0, 0) - N3 * N3.X).GetSafeNormal());
					Sign[VI] = 1.f;
					UV.Set(VI, 0, FVector2f(P.X / 100.f, P.Y / 100.f));
					return VI;
				};
				for (int32 b = 0; b < NJ; b++)
					for (int32 a = 0; a < NI; a++)
						Inst[b * NI + a] = AddVertex(FVector(Is[a] * Step, Js[b] * Step, Z(Is[a], Js[b])), Normal(Is[a], Js[b]));
				for (int32 b = 0; b + 1 < NJ; b++)
					for (int32 a = 0; a + 1 < NI; a++)
					{
						const FVertexInstanceID A = Inst[b * NI + a], B = Inst[b * NI + a + 1], C = Inst[(b + 1) * NI + a], D = Inst[(b + 1) * NI + a + 1];
						// (facing up: X east, Y south)
						MD.CreateTriangle(G, { A, C, B });
						MD.CreateTriangle(G, { B, C, D });
					}
				// the skirt: each edge's vertices again, SkirtCm lower, joined to the edge
				auto Skirt = [&](TArray<int32> Edge, bool bFlip) {
					TArray<FVertexInstanceID> Lo;
					for (int32 k : Edge)
					{
						const int32 a = k % NI, b = k / NI;
						Lo.Add(AddVertex(FVector(Is[a] * Step, Js[b] * Step, Z(Is[a], Js[b]) - SkirtCm), Normal(Is[a], Js[b])));
					}
					for (int32 k = 0; k + 1 < Edge.Num(); k++)
					{
						const FVertexInstanceID U0 = Inst[Edge[k]], U1 = Inst[Edge[k + 1]], D0 = Lo[k], D1 = Lo[k + 1];
						if (bFlip) { MD.CreateTriangle(G, { U0, U1, D0 }); MD.CreateTriangle(G, { U1, D1, D0 }); }
						else { MD.CreateTriangle(G, { U0, D0, U1 }); MD.CreateTriangle(G, { U1, D0, D1 }); }
					}
				};
				TArray<int32> North, South, West, East;
				for (int32 a = 0; a < NI; a++) { North.Add(a); South.Add((NJ - 1) * NI + a); }
				for (int32 b = 0; b < NJ; b++) { West.Add(b * NI); East.Add(b * NI + NI - 1); }
				Skirt(North, false); Skirt(South, true); Skirt(West, true); Skirt(East, false);
			}
			UStaticMesh* Mesh = NewObject<UStaticMesh>(this);
			Mesh->GetStaticMaterials().Add(FStaticMaterial(GroundMat, TEXT("Ground")));
			TArray<const FMeshDescription*> Ptrs;
			for (const FMeshDescription& MD : Descs) Ptrs.Add(&MD);
			UStaticMesh::FBuildMeshDescriptionsParams P;
			P.bBuildSimpleCollision = false;
			P.bFastBuild = true;
			P.bAllowCpuAccess = false;
			Mesh->BuildFromMeshDescriptions(Ptrs, P);
			if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
				for (int32 L = 0; L < Lods && L < MAX_STATIC_MESH_LODS; L++) RD->ScreenSize[L].Default = LodScreen[L];
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(Mesh);
			C->SetMaterial(0, GroundMat);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetMobility(EComponentMobility::Movable);   // (no baked lighting in this game)
			C->SetupAttachment(RootComponent);
			C->RegisterComponent();
			Tiles.Add(C);
		}
}

// Cart paths, bridges, buildings and cars (props.json, props_<kind>.glb): the game's materials,
// a path a few cm into the ground (the ground has the path painted in: it never floats)
// The ground's height (cm, the level's frame less its origin) at X, Y: as it's drawn
double AOTGRuntimeCourse::GroundAt(double X, double Y) const
{
	const int32 N = Data->HeightSamples;
	const TArray<uint16>& Hs = GroundHeights.Num() == N * N ? GroundHeights : Data->Heights;
	if (Hs.Num() != N * N) return 0;
	const double Step = Data->MetresPerSample * 100.0, ZScale = Data->HeightRange * 100.0 / 65535.0;
	const double U = FMath::Clamp(X / Step, 0.0, N - 1.001), V = FMath::Clamp(Y / Step, 0.0, N - 1.001);
	const int32 i = FMath::FloorToInt(U), j = FMath::FloorToInt(V);
	const double fu = U - i, fv = V - j;
	auto H = [&](int32 a, int32 b) { return double(Hs[b * N + a]); };
	return FMath::Lerp(FMath::Lerp(H(i, j), H(i + 1, j), fu), FMath::Lerp(H(i, j + 1), H(i + 1, j + 1), fu), fv) * ZScale;
}
