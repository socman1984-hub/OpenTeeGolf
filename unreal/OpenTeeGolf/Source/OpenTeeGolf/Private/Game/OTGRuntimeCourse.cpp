// See OTGRuntimeCourse.h.

#include "Game/OTGRuntimeCourse.h"

#include "OTGCourseData.h"
#include "OTGGlb.h"
#include "OTGGrass.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "OTGTextures.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
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
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRuntimeCourse, Log, All);

namespace
{
	const TCHAR* Runtime = TEXT("/Game/OpenTeeGolf/Game/Runtime");
	const TCHAR* MaskKeys[] = { TEXT("fairway"), TEXT("green"), TEXT("tee"), TEXT("bunker"), TEXT("water"), TEXT("rough"), TEXT("native"), TEXT("stream"), TEXT("path"), TEXT("fringe") };
	// The pictures' sizes in the game (the masks are smooth distance fields: 4096 keeps their edges;
	// the mowing directions change only between holes)
	constexpr int32 PictureMax = 4096, MowMax = 2048;
	// The ground's levels of detail: metres between samples (x the heightmap's), and where each
	// starts (the tile's size on screen)
	constexpr int32 Lods = 4;
	const float LodScreen[Lods] = { 1.f, 0.22f, 0.08f, 0.03f };
	constexpr double SkirtCm = 300;   // (round each tile, down: no cracks between tiles at different detail)

	template <class T> T* Load(const FString& Name)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Runtime, *Name, *Name));
	}

	// Every asset of a class under a folder (the cooked game's asset registry knows them)
	TArray<FAssetData> AssetsUnder(const TCHAR* Folder, UClass* Class)
	{
		TArray<FAssetData> All, Out;
		IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
		AR.GetAssetsByPath(FName(Folder), All, true);
		for (const FAssetData& A : All)
		{
			// (its class or one made from it: a material instance is a material interface)
			UClass* C = A.GetClass();
			if (C && C->IsChildOf(Class)) Out.Add(A);
		}
		return Out;
	}

	// An asset of the game's run-time pieces by its name, wherever under Runtime it is
	template <class T> T* Find(const FString& Name)
	{
		for (const FAssetData& A : AssetsUnder(Runtime, T::StaticClass()))
			if (A.AssetName.ToString() == Name) return Cast<T>(A.GetAsset());
		return nullptr;
	}

	TSharedPtr<FJsonValue> ReadJson(const FString& File)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) return nullptr;
		TSharedPtr<FJsonValue> V;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), V) ? V : nullptr;
	}

	// (build_course.py hash01: a number 0..1 from a position, the same every time)
	double Hash01(double X, double Y, double K)
	{
		const double V = FMath::Sin(X * 12.9898 * K + Y * 78.233) * 43758.5453;
		return V - FMath::Floor(V);
	}

	// Unreal's own trees (build_course.py's UE_* lists): broadleaf by these names, conifers by these
	const TCHAR* BroadleafFolders[] = { TEXT("/Game/EuropeanBeech/Geometry/SimpleWind"), TEXT("/Game/EuropeanHornbeam/Geometry/SimpleWind") };
	const TCHAR* ConiferFolders[] = { TEXT("/Game/OTG_Conifers"), TEXT("/Game/Megaplant_Library/Tree_Norway_Spruce"), TEXT("/Game/Megaplant_Library/Tree_Baltic_Pine") };
	const TSet<FString> Pines = { TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"), TEXT("ponderosa") };
	constexpr int32 TreeCull[2] = { 90000, 130000 };
	constexpr int32 BoulderCull[2] = { 35000, 45000 };
	constexpr int32 BoulderKinds = 6;
	constexpr double PathSinkCm = -4;   // (build_course.py PATH_SINK_CM)
	constexpr double WaterLiftCm = 3;
	constexpr double PathOffGroundCm = 35;   // (a path's mesh drawn where it's this far above the ground: bridges)

	struct FTreeModel { UStaticMesh* Mesh; double Height; };
	TArray<FTreeModel> TreeModels(const TCHAR* const* Folders, int32 NumFolders, bool bBroadleaf)
	{
		TArray<FTreeModel> Out;
		for (int32 f = 0; f < NumFolders; f++)
			for (const FAssetData& A : AssetsUnder(Folders[f], UStaticMesh::StaticClass()))
			{
				const FString Name = A.AssetName.ToString();
				if (bBroadleaf ? !(Name.Contains(TEXT("_Forest_")) || Name.Contains(TEXT("_Field_"))) : !(Name.StartsWith(TEXT("Tree_")) || Name.StartsWith(TEXT("SM_")))) continue;
				UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
				if (!M) continue;
				const double H = M->GetBoundingBox().GetSize().Z;
				if (!bBroadleaf && H <= 300) continue;
				Out.Add({ M, H });
			}
		Out.Sort([](const FTreeModel& A, const FTreeModel& B) { return A.Height < B.Height; });
		return Out;
	}
}

AOTGRuntimeCourse::AOTGRuntimeCourse()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AOTGRuntimeCourse::Build(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress)
{
	Data = D;
	if (!D) return;
	const double T0 = FPlatformTime::Seconds();
	LandCm = (D->HeightSamples - 1) * D->MetresPerSample * 100.0;
	if (Progress) Progress(0.02f, TEXT("The course's pictures"));
	LoadPictures();
	BuildGround(Progress);
	if (Progress) Progress(0.6f, TEXT("Trees"));
	BuildTrees();
	if (Progress) Progress(0.75f, TEXT("Water, paths and buildings"));
	BuildBoulders();
	BuildWater();
	BuildSea();
	BuildFarLand();
	BuildProps();
	BuildGrass();
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("%s built in %.1f s"), *D->CourseName, FPlatformTime::Seconds() - T0);
}

void AOTGRuntimeCourse::BuildInSteps(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress, TFunction<void()> Done)
{
	Data = D;
	if (!D) { if (Done) Done(); return; }
	LandCm = (D->HeightSamples - 1) * D->MetresPerSample * 100.0;
	StepProgress = Progress;
	StepsDone = Done;
	Steps.Add({ TEXT("The course's pictures"), [this]() { LoadPictures(); PrepareGround(); } });
	// (the ground's tiles: added as steps once its size is known - PrepareGround - so the bar is right)
	const int32 N = D->HeightSamples;
	const int32 Quads = N > 2049 ? 128 : 64, Count = FMath::DivideAndRoundUp(N - 1, Quads), Total = Count * Count;
	const int32 Batch = Quads > 64 ? 6 : 24;
	for (int32 From = 0; From < Total; From += Batch)
		Steps.Add({ TEXT("The ground"), [this, From, Batch, Total]() { BuildGroundTiles(From, FMath::Min(From + Batch, Total)); } });
	Steps.Add({ TEXT("Trees"), [this]() { BuildTrees(); } });
	Steps.Add({ TEXT("Rocks and water"), [this]() { BuildBoulders(); BuildWater(); BuildSea(); } });
	Steps.Add({ TEXT("The land round it"), [this]() { BuildFarLand(); } });
	Steps.Add({ TEXT("Paths and buildings"), [this]() { BuildProps(); } });
	Steps.Add({ TEXT("Grass"), [this]() { BuildGrass(); } });
	StepsTotal = Steps.Num();
	if (Progress) Progress(0.f, Steps[0].Key);
}

// The wind's parameter collections, in this level before any grass or tree material that reads them
// is drawn. A level sets up the collections loaded when it opens; one loaded after (in a packaged
// game, the first time the round set the wind) has no place in it, and the first material reading
// it brings the game down ("Failed to find parameter collection buffer" - M_Turf_rough, the tall
// grass). In the editor they're loaded already, so it never showed there.
static void EnsureWindCollections(UWorld* W)
{
	if (!W) return;
	for (const TCHAR* Path : { TEXT("/Game/OpenTeeGolf/Game/MPC_Wind.MPC_Wind"), TEXT("/Game/Fab/MaterialParameterCollection/QMPC_GlobalFoliageActor.QMPC_GlobalFoliageActor") })
		if (UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, Path))
			W->GetParameterCollectionInstance(C);   // (made, and added to the scene, if it isn't there)
}

void AOTGRuntimeCourse::LoadPictures()
{
	EnsureWindCollections(GetWorld());
	const FString& Dir = Data->ExportDir;
	if (UTexture2D* T = otg::LoadTexture(FPaths::Combine(Dir, TEXT("photo.jpg")), otg::ETexKind::Photo, PictureMax, this)) Textures.Add(TEXT("Photo"), T);
	for (const TCHAR* K : MaskKeys)
	{
		otg::FPicture Pix;
		if (UTexture2D* T = otg::LoadTexture(FPaths::Combine(Dir, FString::Printf(TEXT("layer_%s.png"), K)), otg::ETexKind::Mask, PictureMax, this, &Pix))
		{
			Textures.Add(FName(FString::Printf(TEXT("Mask_%s"), K)), T);
			MaskPixels.Add(K, MoveTemp(Pix));   // (the grass reads them where it grows)
		}
	}
	if (UTexture2D* T = otg::LoadTexture(FPaths::Combine(Dir, TEXT("layer_mow.png")), otg::ETexKind::Data, MowMax, this)) Textures.Add(TEXT("Mow"), T);
}

void AOTGRuntimeCourse::SetPictures(UMaterialInstanceDynamic* M) const
{
	for (const auto& P : Textures) M->SetTextureParameterValue(P.Key, P.Value);
	M->SetScalarParameterValue(TEXT("LandSizeM"), float(LandCm / 100.0));
	M->SetScalarParameterValue(TEXT("LandSizeCm"), float(LandCm));
}

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
	LiftPondBanks();
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
	GroundHeights = Data->Heights;
	const int32 N = Data->HeightSamples;
	if (GroundHeights.Num() != N * N) return;
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("water_bodies.json")));
	if (!J.IsValid() || J->Type != EJson::Array) return;
	const double Step = Data->MetresPerSample * 100.0;
	const double ZScale = Data->HeightRange * 100.0 / 65535.0;
	const double Clear = 8.0, Deepest = 25.0;   // cm over the water (it's drawn WaterLiftCm up); how far under it a bank may be
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
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X) - Reach - 1), Y0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y) - Reach - 1);
		const int32 X1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.X) + Reach + 1), Y1 = FMath::Min(N - 1, FMath::CeilToInt(Box.Max.Y) + Reach + 1);
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
		const double Want = Level + Clear;
		for (int32 y = 0; y < H; y++)
			for (int32 x = 0; x < W; x++)
			{
				if (Ring[y * W + x] < 2) continue;
				uint16& Hv = GroundHeights[(Y0 + y) * N + (X0 + x)];
				const double Z = Hv * ZScale;
				if (Z < Want && Z > Level - Deepest) { Hv = uint16(FMath::Clamp(FMath::CeilToDouble(Want / ZScale), 0.0, 65535.0)); Lifted++; }
			}
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Pond banks: %d samples lifted clear of the water"), Lifted);
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

UHierarchicalInstancedStaticMeshComponent* AOTGRuntimeCourse::Instances(UStaticMesh* Mesh, const TArray<FTransform>& Xfs, int32 CullFrom, int32 CullTo)
{
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	C->InstanceStartCullDistance = CullFrom;
	C->InstanceEndCullDistance = CullTo;
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	C->AddInstances(Xfs, false, true);
	Parts.Add(C);
	return C;
}

// The trees (trees.json): Unreal's own models - each tree one of the three nearest its height
// (picked by its position, so the woods mix), scaled to the game's height within reason, turned
// at random; conifers the pine and spruce models (pines for the pines). As build_course.py does it.
// Then as the course's tree designer has them (UOTGCourseData::ForEachDesignedTree, as the ball's
// trees are): a species drawn with the model picked for it, larger or smaller, fewer or more.
void AOTGRuntimeCourse::BuildTrees()
{
	if (!bTreesRead)
	{
		bTreesRead = true;
		const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("trees.json")));
		if (J.IsValid())
			for (const TSharedPtr<FJsonValue>& V : J->AsArray())
			{
				const TSharedPtr<FJsonObject> T = V->AsObject();
				if (!T.IsValid()) continue;
				FTreeRec R;
				R.X = T->GetNumberField(TEXT("x")); R.Y = T->GetNumberField(TEXT("y")); R.Z = T->GetNumberField(TEXT("z")); R.H = T->GetNumberField(TEXT("h"));
				T->TryGetStringField(TEXT("kind"), R.Kind);
				TreeRecs.Add(R);
			}
	}
	for (UHierarchicalInstancedStaticMeshComponent* C : TreeComps) if (C) { Parts.Remove(C); C->DestroyComponent(); }
	TreeComps.Reset();
	TreeCount = 0;
	if (!TreeRecs.Num()) return;
	const TArray<FTreeModel> Broad = TreeModels(BroadleafFolders, UE_ARRAY_COUNT(BroadleafFolders), true);
	const TArray<FTreeModel> Cones = TreeModels(ConiferFolders, UE_ARRAY_COUNT(ConiferFolders), false);
	TArray<FTreeModel> PineModels, Spruces, Beeches, Hornbeams;
	for (const FTreeModel& M : Cones) (M.Mesh->GetName().Contains(TEXT("pine"), ESearchCase::IgnoreCase) ? PineModels : Spruces).Add(M);
	for (const FTreeModel& M : Broad) (M.Mesh->GetName().Contains(TEXT("Hornbeam"), ESearchCase::IgnoreCase) ? Hornbeams : Beeches).Add(M);
	if (!PineModels.Num()) PineModels = Spruces;
	if (!Spruces.Num()) Spruces = PineModels;
	if (!Beeches.Num()) Beeches = Broad;
	if (!Hornbeams.Num()) Hornbeams = Broad;
	if (!Broad.Num() && !Cones.Num()) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("No tree models in the game")); return; }
	const FOTGTreeDesign& Design = Data->TreeDesign;
	struct FGroup { TArray<FTransform> Xfs; TArray<FLinearColor> Tints; bool bTinted = false; };
	TMap<UStaticMesh*, FGroup> ByMesh;
	DrawnTrees.Reset();
	// (E: the tree's own changes from the designer, if it has any - its species, model, leaf colour)
	auto Place = [&](const FString& InKind, double X, double Y, double Zc, double H, double Scale, const FOTGTreeEdit* E) {
		const FString Kind = E && !E->Kind.IsEmpty() ? E->Kind : InKind;
		const FString Model = E && !E->Model.IsEmpty() ? E->Model : Design.For(Kind).Model;
		const TArray<FTreeModel>& Pool = Model == TEXT("beech") ? Beeches : Model == TEXT("hornbeam") ? Hornbeams : Model == TEXT("spruce") ? Spruces : Model == TEXT("pine") ? PineModels
			: FOTGTreeDesign::IsConifer(Kind) ? (Pines.Contains(Kind) ? PineModels : Spruces) : Broad;
		if (!Pool.Num()) return;
		TArray<FTreeModel> Near = Pool;
		Near.Sort([H](const FTreeModel& A, const FTreeModel& B) { return FMath::Abs(A.Height - H) < FMath::Abs(B.Height - H); });
		Near.SetNum(FMath::Min(3, Near.Num()));
		const FTreeModel& M = Near[int32(Hash01(X, Y, 3.1) * Near.Num()) % Near.Num()];
		// (scaled to its height within reason - further when the designer made it bigger or smaller)
		const double S = FMath::Clamp(H / FMath::Max(1.0, M.Height), 0.7 * FMath::Min(1.0, Scale), 1.4 * FMath::Max(1.0, Scale)) * (0.92 + 0.16 * Hash01(X, Y, 5.3));
		const double Yaw = Hash01(X, Y, 2.9) * 360;
		FGroup& G = ByMesh.FindOrAdd(M.Mesh);
		const FVector Foot = Data->LevelOrigin + FVector(X, Y, Zc - 10);
		G.Xfs.Add(FTransform(FRotator(0, Yaw, 0), Foot, FVector(S)));
		const FLinearColor T = E ? E->LeafTint() : FLinearColor(0, 0, 0, 0);
		G.Tints.Add(T);
		G.bTinted |= E && E->HasColour();
		FDrawn& D = DrawnTrees.AddDefaulted_GetRef();
		D.Base = Foot; D.H = M.Height * S; D.R = FMath::Max(150.0, D.H * 0.22); D.Kind = Kind; D.bAdded = E && E->bAdded; D.Mesh = M.Mesh;
	};
	if (TreeRecs.Num() == Data->Trees.Num())
	{
		Data->ForEachDesignedTree([&](int32 Src, double X, double Z, double Ground, float Scale, const FOTGTreeEdit* E) {
			const int32 n = DrawnTrees.Num();
			if (Src < 0)
			{
				// (added in the designer: standing on the ground where it was put, its own height)
				const double H = E->Height * E->Size * 100.0;
				Place(E->Kind, (X - Data->HeightX0) * 100.0, (Data->HeightZ1 - Z) * 100.0, (Ground - Data->HeightBase) * 100.0, H, FMath::Max(0.3, H / 1500.0), E);
			}
			else
			{
				const FTreeRec& R = TreeRecs[Src];
				const FOTGTreeShape& S = Data->Trees[Src];
				const double Zc = R.Z + (Ground - Data->GroundAt(S.X, S.Z)) * 100.0;
				Place(R.Kind, (X - Data->HeightX0) * 100.0, (Data->HeightZ1 - Z) * 100.0, Zc, R.H * Scale, Scale, E);
			}
			if (DrawnTrees.Num() > n) { DrawnTrees.Last().X = X; DrawnTrees.Last().Z = Z; }
		});
	}
	else
	{
		UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("trees.json (%d) and play.json (%d) disagree: the trees drawn as exported"), TreeRecs.Num(), Data->Trees.Num());
		for (const FTreeRec& R : TreeRecs) Place(R.Kind, R.X, R.Y, R.Z, R.H, 1.0, nullptr);
		DrawnTrees.Reset();   // (no designing them one at a time: they aren't the ball's trees)
	}
	int32 Standins = 0;
	TSet<UStaticMesh*> Colourable;
	for (auto& P : ByMesh)
	{
		UHierarchicalInstancedStaticMeshComponent* C = Instances(P.Key, P.Value.Xfs, TreeCull[0], TreeCull[1]);
		// (trees given their own leaf colour: the leaves' material reads it per tree - M_TreeLeaves)
		if (P.Value.bTinted)
		{
			C->SetNumCustomDataFloats(3);
			for (int32 i = 0; i < P.Value.Tints.Num(); i++)
				if (!P.Value.Tints[i].Equals(FLinearColor(0, 0, 0, 0)))
					C->SetCustomData(i, TArray<float>({ P.Value.Tints[i].R, P.Value.Tints[i].G, P.Value.Tints[i].B }), false);
			C->MarkRenderStateDirty();
		}
		TreeComps.Add(C);
		// (the Fab trees' own materials need a master the game hasn't got: their stand-ins)
		const TArray<FStaticMaterial>& Mats = P.Key->GetStaticMaterials();
		for (int32 i = 0; i < Mats.Num(); i++)
			if (Mats[i].MaterialInterface)
				if (UMaterialInterface* Sub = Find<UMaterialInterface>(TEXT("MI_Standin_") + Mats[i].MaterialInterface->GetName()))
				{
					C->SetMaterial(i, Sub);
					Standins++;
					// (the game's leaves: they take a tree's own colour - M_TreeLeaves)
					if (Sub->GetBaseMaterial() && Sub->GetBaseMaterial()->GetName() == TEXT("M_TreeLeaves")) Colourable.Add(P.Key);
				}
		TreeCount += P.Value.Xfs.Num();
	}
	for (FDrawn& D : DrawnTrees) D.bColourable = Colourable.Contains(D.Mesh);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Trees: %d from %d models (%d stand-in materials)%s"), TreeCount, ByMesh.Num(), Standins, Design.IsDefault() ? TEXT("") : TEXT(", as the course's tree designer has them"));
}

// Boulders (boulders.json: [x, y, z, size cm, yaw, flat, kind]): the game's six stones (as the
// tree designer has them: there or not, how many, how big)
void AOTGRuntimeCourse::BuildBoulders()
{
	if (!bRocksRead)
	{
		bRocksRead = true;
		const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("boulders.json")));
		if (J.IsValid())
			for (const TSharedPtr<FJsonValue>& V : J->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& B = V->AsArray();
				if (B.Num() < 7) continue;
				FRockRec R;
				R.P = FVector(B[0]->AsNumber(), B[1]->AsNumber(), B[2]->AsNumber());
				R.Size = B[3]->AsNumber(); R.Yaw = B[4]->AsNumber(); R.Flat = B[5]->AsNumber(); R.Kind = int32(B[6]->AsNumber()) % BoulderKinds;
				RockRecs.Add(R);
			}
	}
	for (UHierarchicalInstancedStaticMeshComponent* C : RockComps) if (C) { Parts.Remove(C); C->DestroyComponent(); }
	RockComps.Reset();
	RockCount = 0;
	TArray<UStaticMesh*> Meshes;
	for (int32 k = 0; k < BoulderKinds; k++) Meshes.Add(Find<UStaticMesh>(FString::Printf(TEXT("SM_Boulder_%d"), k)));
	const FOTGTreeDesign& Design = Data->TreeDesign;
	TMap<int32, TArray<FTransform>> ByKind;
	DrawnRocks.Reset();
	auto Put = [&](const FRockRec& R, double Size, bool bAdded) {
		const double S = R.Size / 100.0 * Size;
		// (a bigger stone sits a little deeper)
		const FVector At = Data->LevelOrigin + R.P - FVector(0, 0, (Size - 1) * R.Size * 0.15);
		ByKind.FindOrAdd(R.Kind).Add(FTransform(FRotator(0, R.Yaw, 0), At, FVector(S, S, S * R.Flat)));
		FDrawn& D = DrawnRocks.AddDefaulted_GetRef();
		D.Base = At; D.R = FMath::Max(60.0, R.Size * Size * 0.5); D.H = R.Size * Size; D.X = R.P.X / 100.0; D.Z = R.P.Y / 100.0; D.bAdded = bAdded;
	};
	for (const FRockRec& R : RockRecs)
	{
		if (!Design.KeepRock(R.P.X / 100.0, R.P.Y / 100.0)) continue;
		const FOTGRockEdit* E = Design.Rocks.Num() ? Design.RockEditAt(R.P.X / 100.0, R.P.Y / 100.0) : nullptr;
		Put(R, Design.RockSize * (E ? E->Size : 1.f), false);
	}
	// (and the rocks added in the designer, on the ground where they were put: a stone of the game's
	// six picked by where it is, its size across)
	for (const FOTGRockEdit& E : Design.Rocks)
	{
		if (!E.bAdded || E.bRemoved) continue;
		FRockRec R;
		const double CX = Data->HeightX0 + E.X, CZ = Data->HeightZ1 - E.Z;
		R.P = FVector(E.X * 100.0, E.Z * 100.0, (Data->GroundAt(CX, CZ) - Data->HeightBase) * 100.0);
		R.Size = 100.0;
		R.Yaw = FOTGTreeDesign::Hash01(E.X, E.Z, 7) * 360;
		R.Flat = 0.6 + 0.3 * FOTGTreeDesign::Hash01(E.X, E.Z, 11);
		R.Kind = int32(FOTGTreeDesign::Hash01(E.X, E.Z, 13) * BoulderKinds) % BoulderKinds;
		Put(R, E.Size, true);
	}
	for (auto& P : ByKind) if (Meshes[P.Key]) { RockComps.Add(Instances(Meshes[P.Key], P.Value, BoulderCull[0], BoulderCull[1])); RockCount += P.Value.Num(); }
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Boulders: %d"), RockCount);
}

const FOTGTreeDesign& AOTGRuntimeCourse::TreeDesign() const
{
	static const FOTGTreeDesign None;
	return Data ? Data->TreeDesign : None;
}

void AOTGRuntimeCourse::SetTreeDesign(const FOTGTreeDesign& Design)
{
	if (!Data) return;
	Data->TreeDesign = Design;
	BuildTrees();
	BuildBoulders();
}

bool AOTGRuntimeCourse::SaveTreeDesign(FString* Where) const
{
	if (!Data) return false;
	if (Where) *Where = FPaths::Combine(Data->ExportDir, TEXT("trees_design.json"));
	return Data->TreeDesign.Save(Data->ExportDir);
}

FOTGTreeDesign AOTGRuntimeCourse::SavedTreeDesign() const
{
	return Data ? FOTGTreeDesign::Load(Data->ExportDir) : FOTGTreeDesign();
}

TArray<TPair<FString, int32>> AOTGRuntimeCourse::TreeSpecies() const
{
	TMap<FString, int32> N;
	if (Data) for (const FOTGTreeShape& S : Data->Trees) N.FindOrAdd(S.Kind)++;
	TArray<TPair<FString, int32>> Out;
	for (const auto& KV : N) Out.Add(TPair<FString, int32>(KV.Key, KV.Value));
	Out.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	return Out;
}

bool AOTGRuntimeCourse::NearestTree(const FString& Kind, const FVector& From, FVector& OutBase, double& OutHeightCm) const
{
	if (!Data || TreeRecs.Num() != Data->Trees.Num()) return false;
	double Best = TNumericLimits<double>::Max();
	Data->ForEachDesignedTree([&](int32 Src, double X, double Z, double Ground, float Scale, const FOTGTreeEdit* E) {
		const FString& K = E && !E->Kind.IsEmpty() ? E->Kind : Data->Trees[Src].Kind;
		if (K != Kind) return;
		const FVector P = Data->CourseToWorld(FVector(X, Ground, Z));
		const double D = FVector::DistSquared2D(P, From);
		// (not right on top of you: one you can see)
		if (D < Best && D > 2500.0 * 2500.0) { Best = D; OutBase = P; OutHeightCm = Src >= 0 ? TreeRecs[Src].H * Scale : E->Height * E->Size * 100.0; }
	});
	return Best < TNumericLimits<double>::Max();
}

FVector2D AOTGRuntimeCourse::CourseXZ(const FVector& World) const
{
	if (!Data) return FVector2D::ZeroVector;
	const FVector C = Data->WorldToCourse(World);
	return FVector2D(C.X, C.Z);
}

FVector2D AOTGRuntimeCourse::RockXY(const FVector& World) const
{
	if (!Data) return FVector2D::ZeroVector;
	const FVector L = World - Data->LevelOrigin;
	return FVector2D(L.X / 100.0, L.Y / 100.0);
}

bool AOTGRuntimeCourse::Pick(const FVector& Origin, const FVector& Dir, double MaxT, FOTGPicked& Out) const
{
	const FVector D = Dir.GetSafeNormal();
	double Best = TNumericLimits<double>::Max();
	const FDrawn* Hit = nullptr;
	bool bRock = false;
	// a tree: the line passing within its crown's reach of its trunk's line, foot to top
	for (const FDrawn& T : DrawnTrees)
	{
		const FVector W0 = Origin - T.Base;
		const double B = D.Z, Dd = FVector::DotProduct(D, W0), E = W0.Z, Den = FMath::Max(1e-6, 1 - B * B);
		const double S = FMath::Clamp((E - B * Dd) / Den, 0.0, T.H);
		const FVector Q = T.Base + FVector(0, 0, S);
		const double At = FVector::DotProduct(Q - Origin, D);
		if (At <= 0 || At > MaxT + T.R || At >= Best) continue;
		// (the trunk narrower low down, the crown wide)
		const double Reach = S < T.H * 0.3 ? FMath::Max(80.0, T.R * 0.4) : T.R;
		if (FVector::DistSquared(Origin + D * At, Q) > Reach * Reach) continue;
		Best = At; Hit = &T; bRock = false;
	}
	// a rock: within its size of its middle
	for (const FDrawn& R : DrawnRocks)
	{
		const FVector C = R.Base + FVector(0, 0, R.R * 0.4);
		const double At = FVector::DotProduct(C - Origin, D);
		if (At <= 0 || At > MaxT + R.R || At >= Best) continue;
		if (FVector::DistSquared(Origin + D * At, C) > R.R * R.R * 1.2) continue;
		Best = At; Hit = &R; bRock = true;
	}
	if (!Hit) return false;
	Out.bRock = bRock; Out.X = Hit->X; Out.Z = Hit->Z; Out.Kind = Hit->Kind; Out.HeightM = Hit->H / 100.0; Out.bAdded = Hit->bAdded; Out.Base = Hit->Base; Out.bColourable = Hit->bColourable;
	return true;
}

bool AOTGRuntimeCourse::Picked(bool bRock, double X, double Z, FOTGPicked& Out) const
{
	for (const FDrawn& T : bRock ? DrawnRocks : DrawnTrees)
		if (FMath::Abs(T.X - X) < 0.15 && FMath::Abs(T.Z - Z) < 0.15)
		{
			Out.bRock = bRock; Out.X = T.X; Out.Z = T.Z; Out.Kind = T.Kind; Out.HeightM = T.H / 100.0; Out.bAdded = T.bAdded; Out.Base = T.Base; Out.bColourable = T.bColourable;
			return true;
		}
	return false;
}

namespace
{
	// A glb in the export's frame (metres: x east, y up, z south) as a mesh in the level's (cm: X
	// east, Y south, Z up)
	UStaticMeshComponent* MeshFromGlb(AActor* Owner, const otg::FGlbMesh& G, const FVector& Origin, double LiftCm, bool bColoursInUVs, UMaterialInterface* Mat)
	{
		// (a mesh made here, as the ground's tiles are: one level of detail)
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		auto Pos = Attr.GetVertexPositions();
		auto Nrm = Attr.GetVertexInstanceNormals();
		auto Tan = Attr.GetVertexInstanceTangents();
		auto Sign = Attr.GetVertexInstanceBinormalSigns();
		auto UV = Attr.GetVertexInstanceUVs();
		UV.SetNumChannels(2);
		auto Slot = Attr.GetPolygonGroupMaterialSlotNames();
		const FPolygonGroupID Grp = MD.CreatePolygonGroup();
		Slot[Grp] = TEXT("Main");
		TArray<FVertexInstanceID> Inst;
		Inst.SetNum(G.Positions.Num());
		for (int32 i = 0; i < G.Positions.Num(); i++)
		{
			const FVector3f& P = G.Positions[i];
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Origin + FVector(P.X * 100.0, P.Z * 100.0, P.Y * 100.0 + LiftCm));
			const FVertexInstanceID VI = MD.CreateVertexInstance(V);
			// (a normal, and a tangent square to it along X: without one the water's ripples - a normal
			// map - shade as nonsense, which spreads over the screen)
			FVector3f N3 = G.Normals.Num() ? FVector3f(G.Normals[i].X, G.Normals[i].Z, G.Normals[i].Y).GetSafeNormal() : FVector3f(0, 0, 1);
			if (N3.IsNearlyZero()) N3 = FVector3f(0, 0, 1);
			FVector3f T3 = (FVector3f(1, 0, 0) - N3 * N3.X).GetSafeNormal();
			if (T3.IsNearlyZero()) T3 = (FVector3f(0, 1, 0) - N3 * N3.Y).GetSafeNormal();
			Nrm[VI] = N3;
			Tan[VI] = T3;
			Sign[VI] = 1.f;
			if (bColoursInUVs && G.Colors.Num())
			{
				// (the prop materials read a vertex's colour from its UVs: red and green, then blue)
				UV.Set(VI, 0, FVector2f(G.Colors[i].R, G.Colors[i].G));
				UV.Set(VI, 1, FVector2f(G.Colors[i].B, 0));
			}
			else if (G.UVs.IsValidIndex(i)) UV.Set(VI, 0, G.UVs[i]);
			Inst[i] = VI;
		}
		for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
		{
			const int32 A = G.Indices[t], B = G.Indices[t + 1], C = G.Indices[t + 2];
			if (A == B || B == C || A == C || !Inst.IsValidIndex(A) || !Inst.IsValidIndex(B) || !Inst.IsValidIndex(C)) continue;
			// (the file's own order: swapping y and z mirrors the mesh, and Unreal's frame mirrors it back)
			MD.CreateTriangle(Grp, { Inst[A], Inst[B], Inst[C] });
		}
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Owner);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, TEXT("Main")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		Mesh->BuildFromMeshDescriptions({ &MD }, Params);
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		Comp->SetStaticMesh(Mesh);
		Comp->SetMaterial(0, Mat);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(Owner->GetRootComponent());
		Comp->RegisterComponent();
		return Comp;
	}
}

// The land beyond the course, out to the horizon: the course is a square a couple of km across,
// and past its edge there was nothing - the sky's own blue showed below the horizon, a band of it
// over the trees. Rings round the square, out to 12 km, start at the course's edge heights (just
// under them: the ground's skirts hide the join), settle into low rolling hills and rise, 5 to
// 12 km out, into a line of hills 40 to 360 m high all round, peaks and saddles: the horizon. (Flat land out to
// 100 km left a strip of the sky's below-horizon blue; hills 20 km and more away were darkened to
// navy by that much air.) Fields and woods in patches of colour (the prop materials' vertex
// colours), the haze doing the rest.
// The real land round the course, when the export has it (farland.json, from the web version's
// distant land: lib/farland.js): its heights on a grid over a square 80 km across, the points
// closest together near the course, and its satellite photos - up to three, each sharper over less
// ground nearer the course - on it, each spot taking the sharpest that covers it (M_FarLand). The
// hills stand where they really are and look like the country round the course, not plain green.
// Under the course's own ground it sits 4 m down (the export lowered it).
bool AOTGRuntimeCourse::BuildRealFarLand()
{
	const FString Dir = Data->ExportDir;
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Dir, TEXT("farland.json")));
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!J.IsValid() || !J->TryGetObject(Obj)) return false;
	const TSharedPtr<FJsonObject>& F = *Obj;
	const TSharedPtr<FJsonObject>* CentreObj = nullptr;
	if (!F->TryGetObjectField(TEXT("centre"), CentreObj)) return false;
	const double Cx = (*CentreObj)->GetNumberField(TEXT("x")), Cz = (*CentreObj)->GetNumberField(TEXT("z"));
	const double Half = F->GetNumberField(TEXT("halfMetres"));
	const int32 Grid = int32(F->GetNumberField(TEXT("grid")));
	TArray<uint8> Raw;
	const int32 Side = Grid + 1;
	if (Half <= 0 || Grid < 8 || !FFileHelper::LoadFileToArray(Raw, *FPaths::Combine(Dir, F->GetStringField(TEXT("heights"))))
		|| Raw.Num() != Side * Side * int32(sizeof(float)))
	{
		UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("The land round it: farland.json without its heights - made-up hills instead"));
		return false;
	}
	const float* Hts = reinterpret_cast<const float*>(Raw.GetData());
	// the photos, sharpest first
	struct FLayer { UTexture2D* Tex; double Half; };
	TArray<FLayer> Photos;
	const TArray<TSharedPtr<FJsonValue>>* LayerList = nullptr;
	if (F->TryGetArrayField(TEXT("layers"), LayerList))
		for (int32 k = 0; k < LayerList->Num(); k++)
		{
			const TSharedPtr<FJsonObject> L = (*LayerList)[k]->AsObject();
			if (!L.IsValid()) continue;
			// (the nearest at full size, the wider ones smaller: they're only ever seen far off)
			UTexture2D* T = otg::LoadTexture(FPaths::Combine(Dir, L->GetStringField(TEXT("file"))), otg::ETexKind::Photo, Photos.Num() ? 2048 : 4096, this);
			if (!T) continue;
			Textures.Add(FName(*FString::Printf(TEXT("Far%d"), Photos.Num())), T);
			Photos.Add({ T, L->GetNumberField(TEXT("halfMetres")) });
		}
	UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_FarLand"));
	if (!Photos.Num() || !Base)
	{
		UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("The land round it: no photos or no M_FarLand - made-up hills instead"));
		return false;
	}
	// the mesh: the export's grid (row j, column i at x = Cx + s(i / Grid) * Half, z = Cz + s(j / Grid)
	// * Half, s(f) = sign(2f-1) (2f-1)^2, z south), the uvs over the widest photo
	const FLayer& Widest = Photos.Last();
	auto S = [](double f) { const double u = f * 2 - 1; return FMath::Sign(u) * u * u; };
	otg::FGlbMesh G;
	G.Positions.Reserve(Side * Side);
	G.UVs.Reserve(Side * Side);
	for (int32 j = 0; j < Side; j++)
		for (int32 i = 0; i < Side; i++)
		{
			const double X = Cx + S(double(i) / Grid) * Half, Z = Cz + S(double(j) / Grid) * Half;
			// (the last few km rise into a line of hills, 250 to 650 m: the land ends 40 km out, and
			// over its edge - the earth's curve taking it down - a strip of the sky's below-horizon
			// navy showed; the haze makes them distant hills)
			const double Out = FMath::Max(FMath::Abs(S(double(i) / Grid)), FMath::Abs(S(double(j) / Grid)));
			const double Rim = FMath::SmoothStep(0.82, 1.0, Out) * (250.0 + 400.0 * (0.5 + 0.5 * FMath::PerlinNoise2D(FVector2D(X, Z) / 6000.0)));
			// (the export put it 4 m under the course's whole square; where the ground's left out,
			// back up - a few cm under - so it's the ground there: PlanGroundTiles)
			const bool bInSquare = X > 0 && Z > 0 && X * 100.0 < LandCm && Z * 100.0 < LandCm;
			const double Up = bInSquare && !GroundDrawnAt(X * 100.0, Z * 100.0) ? 3.95 : 0.0;
			double H = Hts[j * Side + i] + Rim + Up;
			// (at or just over the sea's level: the open sea's - BuildSea - so a little under it)
			if (bSea && H < SeaLevelM + 0.5 && H > SeaLevelM - 3) H = SeaLevelM - 3;
			G.Positions.Add(FVector3f(X, H, Z));   // (the export's frame: x east, y up, z south)
			G.UVs.Add(FVector2f((X - (Cx - Widest.Half)) / (2 * Widest.Half), (Z - (Cz - Widest.Half)) / (2 * Widest.Half)));
		}
	G.Indices.Reserve(Grid * Grid * 6);
	for (int32 j = 0; j < Grid; j++)
		for (int32 i = 0; i < Grid; i++)
		{
			const int32 A = j * Side + i, B = A + 1, C = A + Side, D = C + 1;
			for (const FIntVector& T : { FIntVector(A, B, C), FIntVector(B, D, C) })
			{
				// (facing up, as the made-up land's are)
				const FVector3f& Pa = G.Positions[T.X];
				if (FVector3f::CrossProduct(G.Positions[T.Y] - Pa, G.Positions[T.Z] - Pa).Y >= 0) G.Indices.Append({ T.X, T.Y, T.Z });
				else G.Indices.Append({ T.X, T.Z, T.Y });
			}
		}
	G.Normals.SetNumZeroed(G.Positions.Num());
	for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
	{
		const FVector3f& Pa = G.Positions[G.Indices[t]];
		FVector3f Nn = FVector3f::CrossProduct(G.Positions[G.Indices[t + 1]] - Pa, G.Positions[G.Indices[t + 2]] - Pa);
		if (Nn.Y < 0) Nn = -Nn;
		for (int32 k = 0; k < 3; k++) G.Normals[G.Indices[t + k]] += Nn;
	}
	for (FVector3f& Nn : G.Normals) Nn = Nn.GetSafeNormal();
	// the material: the three photos (fewer: the widest stands in) and how much each is zoomed in
	// from the widest's uvs
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
	const int32 NL = Photos.Num();
	const FLayer& L0 = Photos[0];
	const FLayer& L1 = NL >= 3 ? Photos[1] : Widest;
	M->SetTextureParameterValue(TEXT("Photo0"), L0.Tex);
	M->SetTextureParameterValue(TEXT("Photo1"), L1.Tex);
	M->SetTextureParameterValue(TEXT("Photo2"), Widest.Tex);
	M->SetScalarParameterValue(TEXT("Zoom0"), float(Widest.Half / L0.Half));
	M->SetScalarParameterValue(TEXT("Zoom1"), float(Widest.Half / L1.Half));
	UStaticMeshComponent* C = MeshFromGlb(this, G, Data->LevelOrigin, 0, false, M);
	C->SetCastShadow(false);
	C->bUseAsOccluder = false;
	C->SetCullDistance(0);
	C->SetBoundsScale(1.f);
	Parts.Add(C);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("The land round it: real heights and %d photos, %d km across"), NL, FMath::RoundToInt(Half * 2 / 1000));
	return true;
}

void AOTGRuntimeCourse::BuildFarLand()
{
	if (BuildRealFarLand()) return;
	const int32 N = Data->HeightSamples;
	if (Data->Heights.Num() != N * N) return;
	const double SizeM = (N - 1) * Data->MetresPerSample;
	const double StepM = 32.0;
	const int32 Side = FMath::Max(8, FMath::RoundToInt(SizeM / StepM));
	const int32 Around = Side * 4;
	const double Rings[] = { 0, 40, 150, 450, 1200, 2500, 4500, 7000, 9500, 12000 };
	constexpr int32 NumRings = UE_ARRAY_COUNT(Rings);
	// (value noise, smooth, 0..1)
	auto Noise = [](double X, double Y, double Cell, int32 Seed) {
		const double U = X / Cell, V = Y / Cell;
		const double i = FMath::Floor(U), j = FMath::Floor(V);
		auto R = [&](double a, double b) { return Hash01(a + Seed * 17.0, b - Seed * 31.0, 1.0); };
		auto S = [](double t) { return t * t * (3 - 2 * t); };
		const double fu = S(U - i), fv = S(V - j);
		return FMath::Lerp(FMath::Lerp(R(i, j), R(i + 1, j), fu), FMath::Lerp(R(i, j + 1), R(i + 1, j + 1), fu), fv);
	};
	// the course's edge, all the way round (metres, its height from the ground as it's drawn)
	TArray<FVector2D> Edge;
	TArray<double> EdgeH;
	double MeanH = 0;
	for (int32 k = 0; k < Around; k++)
	{
		const int32 s = k / Side;
		const double t = double(k % Side) / Side * SizeM;
		const FVector2D P = s == 0 ? FVector2D(t, 0) : s == 1 ? FVector2D(SizeM, t) : s == 2 ? FVector2D(SizeM - t, SizeM) : FVector2D(0, SizeM - t);
		Edge.Add(P);
		EdgeH.Add(GroundAt(P.X * 100.0, P.Y * 100.0) / 100.0);
		MeanH += EdgeH.Last() / Around;
	}
	const FVector2D Mid(SizeM / 2, SizeM / 2);
	otg::FGlbMesh G;
	for (int32 r = 0; r < NumRings; r++)
		for (int32 k = 0; k < Around; k++)
		{
			const double D = Rings[r];
			// (the square grown by D on every side, its corners kept)
			const FVector2D Off = Edge[k] - Mid;
			const double Half = SizeM / 2;
			const FVector2D P = Mid + Off * ((Half + D) / Half);
			const double Settle = FMath::SmoothStep(0.0, 1500.0, D);
			const double Hills = (Noise(P.X, P.Y, 2600, 3) - 0.5) * 70 + (Noise(P.X, P.Y, 700, 5) - 0.5) * 18;
			const double Fade = FMath::SmoothStep(30.0, 900.0, D);
			// (far off, a line of hills all round: the horizon, in front of the sky's own)
			const double Ridge = FMath::SmoothStep(4500.0, 12000.0, D) * (40.0 + 230.0 * FMath::Square(Noise(P.X, P.Y, 3500, 13)) + 90.0 * Noise(P.X, P.Y, 1200, 17));
			const double H = FMath::Lerp(EdgeH[k], MeanH, Settle) + Hills * Fade + Ridge - (r == 0 ? 0.15 : 0.0);
			G.Positions.Add(FVector3f(P.X, H, P.Y));   // (the export's frame: x east, y up, z south)
			// woods and fields: dark greens, olive and straw, by patches
			const double Wood = Noise(P.X, P.Y, 380, 7), Dry = Noise(P.X, P.Y, 650, 11);
			const FLinearColor Forest(0.075f, 0.1f, 0.055f), Field(0.15f, 0.18f, 0.085f), Straw(0.24f, 0.22f, 0.13f);
			FLinearColor Col = FMath::Lerp(Field, Straw, float(FMath::SmoothStep(0.5, 0.85, Dry) * 0.7));
			Col = FMath::Lerp(Col, Forest, float(FMath::SmoothStep(0.35, 0.65, Wood)));
			// (next to the course: its own edge - trees and rough, mostly - so darker green)
			Col = FMath::Lerp(Forest * 1.3f, Col, float(FMath::SmoothStep(0.0, 400.0, D)));
			G.Colors.Add(Col);
		}
	for (int32 r = 0; r + 1 < NumRings; r++)
		for (int32 k = 0; k < Around; k++)
		{
			const int32 k2 = (k + 1) % Around;
			const int32 A = r * Around + k, B = r * Around + k2, C = (r + 1) * Around + k, D = (r + 1) * Around + k2;
			for (const FIntVector& T : { FIntVector(A, B, C), FIntVector(B, D, C) })
			{
				// (facing up: the export frame is glTF's, front faces counter-clockwise from above; the
				// prop material is two-sided, and a back face is lit as if facing down - black)
				const FVector3f& Pa = G.Positions[T.X];
				const FVector3f E1 = G.Positions[T.Y] - Pa, E2 = G.Positions[T.Z] - Pa;
				if (FVector3f::CrossProduct(E1, E2).Y >= 0) G.Indices.Append({ T.X, T.Y, T.Z });
				else G.Indices.Append({ T.X, T.Z, T.Y });
			}
		}
	// smooth normals
	G.Normals.SetNumZeroed(G.Positions.Num());
	for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
	{
		const FVector3f& Pa = G.Positions[G.Indices[t]];
		FVector3f Nn = FVector3f::CrossProduct(G.Positions[G.Indices[t + 1]] - Pa, G.Positions[G.Indices[t + 2]] - Pa);
		if (Nn.Y < 0) Nn = -Nn;   // (up)
		for (int32 k = 0; k < 3; k++) G.Normals[G.Indices[t + k]] += Nn;
	}
	for (FVector3f& Nn : G.Normals) Nn = Nn.GetSafeNormal();
	UMaterialInterface* M = Load<UMaterialInterface>(TEXT("M_Prop_building"));
	UStaticMeshComponent* C = MeshFromGlb(this, G, Data->LevelOrigin, 0, true, M);
	C->SetCastShadow(false);
	C->bUseAsOccluder = false;
	C->SetCullDistance(0);
	C->SetBoundsScale(1.f);
	Parts.Add(C);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("The land round it: %d km out"), FMath::RoundToInt(Rings[NumRings - 1] / 1000));
}

// The ponds and streams (water.glb): one surface, the game's water on it
void AOTGRuntimeCourse::BuildWater()
{
	otg::FGlbMesh G;
	if (!otg::ReadGlb(FPaths::Combine(Data->ExportDir, TEXT("water.glb")), G)) return;
	UStaticMeshComponent* C = MeshFromGlb(this, G, Data->LevelOrigin, WaterLiftCm, false, Load<UMaterialInterface>(TEXT("M_Water")));
	C->SetCastShadow(false);
	// (see-through: it mustn't hide what's under it from the renderer - the ground tiles under a
	// pond were culled as hidden, and the water showed the sky)
	C->bUseAsOccluder = false;
	Parts.Add(C);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Water: %d triangles"), G.Indices.Num() / 3);
}

// The sea beside a seaside course (sea.glb, sea.json: none inland), drawn as open sea - M_Sea:
// swells, lines of surf rolling in, foam along the beach, deep blue-green that hides the bed - not
// as a pond. Each point's metres to the shore and depth of the sea bed come in its colours (read
// by the material from its uvs). Past the sea's own surface, out to the horizon, a flat sea at the
// same level (the land round it, where it's at or under the sea, put under it: BuildRealFarLand).
void AOTGRuntimeCourse::BuildSea()
{
	bSea = false;
	otg::FGlbMesh G;
	if (!otg::ReadGlb(FPaths::Combine(Data->ExportDir, TEXT("sea.glb")), G) || !G.Colors.Num()) return;
	UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_Sea"));
	if (!Base)
	{
		UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Sea: no M_Sea - drawn as the ponds are"));
		Base = Load<UMaterialInterface>(TEXT("M_Water"));
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
	FBox Box(ForceInit);
	for (const FVector3f& P : G.Positions) Box += FVector(P);
	bSea = true;
	SeaLevelM = G.Positions[0].Y;
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("sea.json")));
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (J.IsValid() && J->TryGetObject(Obj))
	{
		double L = 0;
		if ((*Obj)->TryGetNumberField(TEXT("levelMetres"), L)) SeaLevelM = L;
		const TSharedPtr<FJsonObject>* Sw = nullptr;
		if ((*Obj)->TryGetObjectField(TEXT("swell"), Sw))
			M->SetVectorParameterValue(TEXT("SwellDir"), FLinearColor((*Sw)->GetNumberField(TEXT("x")), (*Sw)->GetNumberField(TEXT("y")), 0, 0));
	}
	auto Add = [&](const otg::FGlbMesh& Mesh)
	{
		UStaticMeshComponent* C = MeshFromGlb(this, Mesh, Data->LevelOrigin, WaterLiftCm, true, M);
		C->SetCastShadow(false);
		C->bUseAsOccluder = false;
		Parts.Add(C);
		return C;
	};
	Add(G);
	// the open sea round it: a flat square 90 km across with the sea's own surface cut out (its
	// bounds: where they're land, the land stands above it), as far from the shore as the material
	// knows (full swell, deep)
	otg::FGlbMesh O;
	const double Far = 45000, Cx = LandCm / 200.0, Cz = LandCm / 200.0, Y = SeaLevelM;
	const double X0 = Box.Min.X, X1 = Box.Max.X, Z0 = Box.Min.Z, Z1 = Box.Max.Z;
	const double Xs[4] = { Cx - Far, X0, X1, Cx + Far }, Zs[4] = { Cz - Far, Z0, Z1, Cz + Far };
	for (int32 j = 0; j < 4; j++)
		for (int32 i = 0; i < 4; i++)
		{
			O.Positions.Add(FVector3f(Xs[i], Y, Zs[j]));
			O.Normals.Add(FVector3f(0, 1, 0));
			O.Colors.Add(FLinearColor(400, -50, 0, 1));
		}
	for (int32 j = 0; j < 3; j++)
		for (int32 i = 0; i < 3; i++)
		{
			if (i == 1 && j == 1) continue;   // (the sea's own surface)
			const int32 A = j * 4 + i, B = A + 1, C = A + 4, D = C + 1;
			for (const FIntVector& T : { FIntVector(A, B, C), FIntVector(B, D, C) })
			{
				// (facing up)
				const FVector3f& Pa = O.Positions[T.X];
				if (FVector3f::CrossProduct(O.Positions[T.Y] - Pa, O.Positions[T.Z] - Pa).Y >= 0) O.Indices.Append({ T.X, T.Y, T.Z });
				else O.Indices.Append({ T.X, T.Z, T.Y });
			}
		}
	UStaticMeshComponent* Open = Add(O);
	Open->SetCullDistance(0);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Sea: %d triangles, at %.2f m"), G.Indices.Num() / 3, SeaLevelM);
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

void AOTGRuntimeCourse::BuildProps()
{
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("props.json")));
	if (!J.IsValid()) return;
	for (const TSharedPtr<FJsonValue>& V : J->AsArray())
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Kind = P->GetStringField(TEXT("kind"));
		otg::FGlbMesh G;
		if (!otg::ReadGlb(FPaths::Combine(Data->ExportDir, P->GetStringField(TEXT("file"))), G)) continue;
		const bool bPath = Kind.StartsWith(TEXT("path"));
		// The paths are painted in the ground (its path mask): their mesh is only wanted where it
		// leaves the ground - bridges, boardwalks. Lying on the ground, sunk a little, it pokes up
		// through it in patches (the ground's own triangles, and coarser ones further off, aren't
		// the path's): those triangles go
		if (bPath)
		{
			const int32 Before = G.Indices.Num() / 3;
			TArray<int32> Keep;
			for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
			{
				bool bOff = false;
				for (int32 k = 0; k < 3 && !bOff; k++)
				{
					const FVector3f& Pv = G.Positions[G.Indices[t + k]];
					bOff = Pv.Y * 100.0 - GroundAt(Pv.X * 100.0, Pv.Z * 100.0) > PathOffGroundCm;
				}
				if (bOff) Keep.Append(&G.Indices[t], 3);
			}
			G.Indices = MoveTemp(Keep);
			UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Props: %s, %d of %d triangles off the ground (the rest painted in it)"), *Kind, G.Indices.Num() / 3, Before);
			if (!G.Indices.Num()) continue;
		}
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("M_Prop_") + Kind);
		if (!M) M = Load<UMaterialInterface>(TEXT("M_Prop_building"));
		UStaticMeshComponent* C = MeshFromGlb(this, G, Data->LevelOrigin, bPath ? PathSinkCm : 0, !bPath, M);
		Parts.Add(C);
		UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Props: %s, %d triangles"), *Kind, G.Indices.Num() / 3);
	}
}

// The grass: each kind's turf patch and its material (reading this course's pictures), grown round
// the camera as it moves (otg::FGrass): the game's kinds as the course's grass designer left them
// (grass.json in its folder)
TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> AOTGRuntimeCourse::GrassMeshes(const TArray<otg::FGrassKind>& Kinds)
{
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> Have;
	for (const otg::FGrassKind& K : Kinds)
	{
		// (each kind's meshes found once: the designer asks again whenever it changes something)
		if (const TArray<TPair<UStaticMesh*, UMaterialInterface*>>* Was = GrassMeshCache.Find(K.Name)) { Have.Add(K.Name, *Was); continue; }
		TArray<TPair<UStaticMesh*, UMaterialInterface*>>& V = Have.Add(K.Name);
		if (K.Fab.Num())
		{
			// Megascans plants: every variant of each, with its own material (Megascans' foliage
			// material: it sways with QMPC_GlobalFoliageActor's wind, which the round sets from the
			// game's - AOTGRound::UpdateFlag)
			for (const FString& Plant : K.Fab)
			{
				const FString Folder = TEXT("/Game/Fab/Megascans/Plants/") + Plant;
				for (const FAssetData& A : AssetsUnder(*Folder, UStaticMesh::StaticClass()))
				{
					UStaticMesh* Mesh = Cast<UStaticMesh>(A.GetAsset());
					if (!Mesh || !Mesh->GetStaticMaterials().Num()) continue;
					TurfMeshes.Add(Mesh);
					V.Add(TPair<UStaticMesh*, UMaterialInterface*>(Mesh, nullptr));   // (its own)
					// (each of its materials copied, to be tinted: make_game_assets.py's fab_tints set its
					// "Base Color Tint" and "Translucency Tint"; the designer's colour multiplies them)
					for (const FStaticMaterial& SM : Mesh->GetStaticMaterials())
					{
						UMaterialInterface* Own = SM.MaterialInterface;
						FLinearColor Base, Through;
						if (!Own || GrassSwaps.Contains(Own)) continue;
						if (!Own->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Base Color Tint")), Base))
						{
							UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Grass %s: %s has no Base Color Tint: its colour stays as it is"), *K.Name, *Own->GetName());
							continue;
						}
						if (!Own->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Translucency Tint")), Through)) Through = Base;
						FLinearColor Controls(1, 1, 1, 0);
						Own->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Base Color Controls")), Controls);
						UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Own, this);
						TurfMats.Add(M);
						GrassSwaps.Add(Own, M);
						GrassTints.FindOrAdd(K.Name).Add(FGrassTint{ M, true, Base, Through, Controls });
					}
				}
			}
			if (!V.Num()) UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Grass %s: none of its Megascans plants are in the game (Content/Fab)"), *K.Name);
		}
		else
		{
			UStaticMesh* Mesh = Find<UStaticMesh>(TEXT("SM_Turf_") + K.Name);
			UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_Turf_") + K.Name);
			if (!Mesh || !Base) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Grass %s: not in the game's content"), *K.Name); }
			else
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
				SetPictures(M);
				TurfMats.Add(M);
				GrassTints.FindOrAdd(K.Name).Add(FGrassTint{ M, false, FLinearColor::White, FLinearColor::White, FLinearColor::White });
				TurfMeshes.Add(Mesh);
				V.Add(TPair<UStaticMesh*, UMaterialInterface*>(Mesh, M));
			}
		}
		GrassMeshCache.Add(K.Name, V);
	}
	return Have;
}

void AOTGRuntimeCourse::BuildGrass()
{
	const TArray<otg::FGrassKind> Kinds = otg::FGrass::LoadKinds(Data->ExportDir);
	const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> Have = GrassMeshes(Kinds);
	Grass = MakeShared<otg::FGrass>();
	Grass->Init(this, Data, MaskPixels, LandCm, Kinds, Have);
	Grass->SetSwaps(GrassSwaps);
	TintGrass();
	int32 On = 0;
	for (const otg::FGrassKind& K : Kinds) On += K.bOn ? 1 : 0;
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Grass: %d kinds (%d growing)%s"), Kinds.Num(), On,
		FPaths::FileExists(FPaths::Combine(Data->ExportDir, TEXT("grass.json"))) ? TEXT(", as the course's grass designer left them") : TEXT(""));
}

// The grass designer's changes from the console (testing, screenshots): OTG.Grass.List,
// OTG.Grass.Set <kind> on|surface|density|height|vary|out|patch|cover|hue|saturation|brightness <value>, OTG.Grass.Add
// <Megascans folder>, OTG.Grass.Save, OTG.Grass.Reset (the game's own)
namespace
{
	AOTGRuntimeCourse* CourseIn(UWorld* W)
	{
		if (W) for (TActorIterator<AOTGRuntimeCourse> It(W); It; ++It) return *It;
		return nullptr;
	}
	using FGrassCmd = FConsoleCommandWithWorldAndArgsDelegate;
	FAutoConsoleCommandWithWorldAndArgs GrassListCmd(TEXT("OTG.Grass.List"), TEXT("The course's grass kinds as they grow now"), FGrassCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		if (AOTGRuntimeCourse* C = CourseIn(W))
			for (const otg::FGrassKind& K : C->GrassKinds())
				UE_LOG(LogOTGRuntimeCourse, Display, TEXT("%-22s %-3s %-6s density %6.0f scale %.2f-%.2f out to %3.0f m patch %4.1f m %.2f-%.2f%s"), *K.Name, K.bOn ? TEXT("on") : TEXT("off"),
					K.Masks.Num() ? *K.Masks[0] : TEXT("-"), K.Density, K.ScaleLo, K.ScaleHi, K.DrawTo, K.PatchM, K.PatchLo, K.PatchHi, K.FarOf.IsEmpty() ? TEXT("") : TEXT(" (follows)"));
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassSetCmd(TEXT("OTG.Grass.Set"), TEXT("OTG.Grass.Set <kind> on|surface|density|height|vary|out|patch|cover|hue|saturation|brightness <value>"), FGrassCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseIn(W);
		if (!C || A.Num() < 3) return;
		TArray<otg::FGrassKind> K = C->GrassKinds();
		otg::FGrassKind* G = K.FindByPredicate([&](const otg::FGrassKind& X) { return X.Name == A[0]; });
		if (!G) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("No grass kind %s"), *A[0]); return; }
		const FString F = A[1].ToLower();
		const float V = FCString::Atof(*A[2]);
		const float Mid = (G->ScaleLo + G->ScaleHi) / 2, Spr = (G->ScaleHi - G->ScaleLo) / FMath::Max(0.01f, G->ScaleHi + G->ScaleLo);
		if (F == TEXT("on")) G->bOn = A[2] == TEXT("1") || A[2].ToLower() == TEXT("true") || A[2].ToLower() == TEXT("on");
		else if (F == TEXT("surface") && (A[2] == TEXT("native") || (A[2] == TEXT("rough") && G->bEven))) G->Masks = { A[2] };
		else if (F == TEXT("density")) G->Density = FMath::Max(0.f, V);
		else if (F == TEXT("height")) { G->ScaleLo = V * (1 - Spr); G->ScaleHi = V * (1 + Spr); }
		else if (F == TEXT("vary")) { G->ScaleLo = Mid * (1 - V); G->ScaleHi = Mid * (1 + V); }
		else if (F == TEXT("out")) { const float R = G->DrawTo > 0 ? G->DrawFrom / G->DrawTo : 0.7f; G->DrawTo = V; G->DrawFrom = V * R; }
		else if (F == TEXT("patch")) G->PatchM = V;
		else if (F == TEXT("cover")) { const float Wd = G->PatchHi - G->PatchLo, Cn = 0.27f + 0.19f * (G->bInvert ? V : 1 - V); G->PatchLo = Cn - Wd / 2; G->PatchHi = Cn + Wd / 2; }
		else if (F == TEXT("hue") || F == TEXT("saturation") || F == TEXT("brightness"))
		{
			// (the colour: tinted at once, nothing grown again)
			(F == TEXT("hue") ? G->Hue : F == TEXT("saturation") ? G->Sat : G->Bright) = V;
			C->SetGrassColours(K);
			return;
		}
		else { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("OTG.Grass.Set: no field %s"), *F); return; }
		C->SetGrassKinds(K);
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassAddCmd(TEXT("OTG.Grass.Add"), TEXT("OTG.Grass.Add <Megascans plant folder>: a plant added to the tall grass"), FGrassCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseIn(W);
		if (!C || !A.Num()) return;
		TArray<otg::FGrassKind> K = C->GrassKinds();
		K.Add(otg::FGrass::PlantKind(A[0]));
		C->SetGrassKinds(K);
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassSaveCmd(TEXT("OTG.Grass.Save"), TEXT("Save the course's grass as it grows now (grass.json)"), FGrassCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		FString Where;
		// (saved first: a Shipping build leaves out the log, its arguments and all)
		if (AOTGRuntimeCourse* C = CourseIn(W)) { const bool bOk = C->SaveGrassKinds(&Where); UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Grass %s %s"), bOk ? TEXT("saved to") : TEXT("NOT saved to"), *Where); }
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassTintsCmd(TEXT("OTG.Grass.Tints"), TEXT("(testing) the grass's tinted materials and what the grass is drawn with"), FGrassCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		if (AOTGRuntimeCourse* C = CourseIn(W)) C->DumpGrassTints();
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassParamCmd(TEXT("OTG.Grass.Param"), TEXT("(testing) OTG.Grass.Param <kind> <vector parameter, _ for a space> <r> <g> <b>: on its Megascans materials"), FGrassCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRuntimeCourse* C = CourseIn(W); C && A.Num() >= 5)
			C->SetGrassParam(A[0], A[1].Replace(TEXT("_"), TEXT(" ")), FLinearColor(FCString::Atof(*A[2]), FCString::Atof(*A[3]), FCString::Atof(*A[4]), 0.f));
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassResetCmd(TEXT("OTG.Grass.Reset"), TEXT("The game's own grass (not saved)"), FGrassCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		if (AOTGRuntimeCourse* C = CourseIn(W)) C->SetGrassKinds(otg::FGrass::DefaultKinds());
	}));
}

const TArray<otg::FGrassKind>& AOTGRuntimeCourse::GrassKinds() const
{
	static const TArray<otg::FGrassKind> None;
	return Grass.IsValid() ? Grass->Kinds() : None;
}

void AOTGRuntimeCourse::SetGrassKinds(const TArray<otg::FGrassKind>& Kinds)
{
	if (!Grass.IsValid()) return;
	TArray<otg::FGrassKind> K = Kinds;
	otg::FGrass::Derive(K);
	Grass->SetKinds(K, GrassMeshes(K));
	Grass->SetSwaps(GrassSwaps);
	TintGrass();
}

void AOTGRuntimeCourse::SetGrassColours(const TArray<otg::FGrassKind>& Kinds)
{
	if (!Grass.IsValid()) return;
	TArray<otg::FGrassKind> K = Kinds;
	for (otg::FGrassKind& G : K) G.ClampColour();
	otg::FGrass::Derive(K);
	Grass->SetColours(K);
	TintGrass();
}

// Each kind's materials tinted as its colour is: our turf by its Tint (a multiplier on the green it
// draws: hue, strength, brightness); a Megascans plant by its Base Color Controls (strength and
// brightness only)
void AOTGRuntimeCourse::TintGrass()
{
	if (!Grass.IsValid()) return;
	const FLinearColor TurfGreen(0.06f, 0.09f, 0.022f);
	for (const otg::FGrassKind& K : Grass->Kinds())
		if (const TArray<FGrassTint>* Ts = GrassTints.Find(K.Name))
			for (const FGrassTint& T : *Ts)
			{
				UMaterialInstanceDynamic* M = T.M.Get();
				if (!M) continue;
				if (!T.bFab) { M->SetVectorParameterValue(TEXT("Tint"), K.TintFor(TurfGreen)); continue; }
				// (Base Color Controls: the first how strong the colour is - under 1 paler, washing out;
				// the second how bright. Their tints hardly turn the hue, so a plant keeps its own: the
				// designer says so. Kept within what reads as grass)
				const float X = K.Sat < 1 ? 1.f - (1.f - K.Sat) * 0.5f : 1.f + (K.Sat - 1.f) * 0.4f;   // (0.7 .. 1.24: past it, neon)
				const float Y = K.Bright < 1 ? 1.f - (1.f - K.Bright) * 0.7f : 1.f + (K.Bright - 1.f) * 0.33f;   // (0.65 .. 1.2)
				M->SetVectorParameterValue(TEXT("Base Color Controls"), FLinearColor(T.Controls.R * X, T.Controls.G * Y, T.Controls.B, T.Controls.A));
			}
}

bool AOTGRuntimeCourse::FindGrass(const FString& Kind, const FVector& From, FVector& Out) const
{
	return Grass.IsValid() && Grass->FindNear(Kind, From, Out);
}

bool AOTGRuntimeCourse::SaveGrassKinds(FString* Where) const
{
	if (!Grass.IsValid() || !Data) return false;
	if (Where) *Where = FPaths::Combine(Data->ExportDir, TEXT("grass.json"));
	return otg::FGrass::SaveKinds(Data->ExportDir, Grass->Kinds());
}

TArray<FString> AOTGRuntimeCourse::GrassPlants() const
{
	TSet<FString> Folders;
	for (const FAssetData& A : AssetsUnder(TEXT("/Game/Fab/Megascans/Plants"), UStaticMesh::StaticClass()))
	{
		// (/Game/Fab/Megascans/Plants/<folder>/...)
		TArray<FString> Bits;
		A.PackagePath.ToString().ParseIntoArray(Bits, TEXT("/"));
		if (Bits.Num() >= 5) Folders.Add(Bits[4]);
	}
	TArray<FString> Out = Folders.Array();
	Out.Sort();
	return Out;
}

float AOTGRuntimeCourse::GrassHeightCm(const FString& Kind) const
{
	if (!Grass.IsValid()) return 0;
	const int32 I = Grass->Kinds().IndexOfByPredicate([&](const otg::FGrassKind& K) { return K.Name == Kind; });
	return I == INDEX_NONE ? 0 : Grass->MeshHeightCm(I);
}

TArray<otg::FGrassKind> AOTGRuntimeCourse::SavedGrassKinds() const
{
	return Data ? otg::FGrass::LoadKinds(Data->ExportDir) : otg::FGrass::DefaultKinds();
}

int32 AOTGRuntimeCourse::GrassInstances() const
{
	return Grass.IsValid() ? Grass->Instances() : 0;
}

void AOTGRuntimeCourse::Tick(float Dt)
{
	Super::Tick(Dt);
	// (BuildInSteps: one step a frame)
	if (Steps.Num())
	{
		const TPair<FString, TFunction<void()>> S = Steps[0];
		Steps.RemoveAt(0);
		S.Value();
		if (StepProgress) StepProgress(1.f - float(Steps.Num()) / FMath::Max(1, StepsTotal), Steps.Num() ? Steps[0].Key : FString(TEXT("Ready")));
		if (!Steps.Num())
		{
			UE_LOG(LogOTGRuntimeCourse, Display, TEXT("%s built"), Data ? *Data->CourseName : TEXT("?"));
			if (StepsDone) StepsDone();
		}
		return;
	}
	if (!Grass.IsValid()) return;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager) Grass->Update(PC->PlayerCameraManager->GetCameraLocation());
}

void AOTGRuntimeCourse::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Grass.IsValid()) Grass->Wait();
	Super::EndPlay(Reason);
}

void AOTGRuntimeCourse::DumpGrassTints() const
{
	for (const auto& KV : GrassTints)
		for (const FGrassTint& T : KV.Value)
			if (UMaterialInstanceDynamic* M = T.M.Get())
			{
				FLinearColor V(0, 0, 0, 0);
				M->GetVectorParameterValue(FHashedMaterialParameterInfo(T.bFab ? TEXT("Base Color Tint") : TEXT("Tint")), V);
				UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Tint %s %s (of %s): %s"), *KV.Key, *M->GetName(), M->Parent ? *M->Parent->GetName() : TEXT("-"), *V.ToString());
			}
	TSet<FString> Seen;
	for (const UActorComponent* AC : GetComponents())
		if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(AC))
			if (I->GetStaticMesh() && I->GetStaticMesh()->GetPathName().Contains(TEXT("/Fab/")) && !Seen.Contains(I->GetStaticMesh()->GetName()))
			{
				Seen.Add(I->GetStaticMesh()->GetName());
				for (int32 m = 0; m < I->GetNumMaterials(); m++)
					UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Drawn %s slot %d: %s"), *I->GetStaticMesh()->GetName(), m, I->GetMaterial(m) ? *I->GetMaterial(m)->GetName() : TEXT("-"));
			}
}

void AOTGRuntimeCourse::SetGrassParam(const FString& Kind, const FString& Param, const FLinearColor& V)
{
	if (const TArray<FGrassTint>* Ts = GrassTints.Find(Kind))
		for (const FGrassTint& T : *Ts) if (UMaterialInstanceDynamic* M = T.M.Get()) M->SetVectorParameterValue(*Param, V);
}
