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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Misc/Paths.h"

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
	const TSet<FString> Conifers = { TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"), TEXT("ponderosa"), TEXT("spruce"), TEXT("cypress"), TEXT("baldcypress"), TEXT("hemlock"), TEXT("juniper"), TEXT("holly") };
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
	Steps.Add({ TEXT("Rocks and water"), [this]() { BuildBoulders(); BuildWater(); } });
	Steps.Add({ TEXT("The land round it"), [this]() { BuildFarLand(); } });
	Steps.Add({ TEXT("Paths and buildings"), [this]() { BuildProps(); } });
	Steps.Add({ TEXT("Grass"), [this]() { BuildGrass(); } });
	StepsTotal = Steps.Num();
	if (Progress) Progress(0.f, Steps[0].Key);
}

void AOTGRuntimeCourse::LoadPictures()
{
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
void AOTGRuntimeCourse::BuildTrees()
{
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("trees.json")));
	if (!J.IsValid()) return;
	const TArray<FTreeModel> Broad = TreeModels(BroadleafFolders, UE_ARRAY_COUNT(BroadleafFolders), true);
	const TArray<FTreeModel> Cones = TreeModels(ConiferFolders, UE_ARRAY_COUNT(ConiferFolders), false);
	TArray<FTreeModel> PineModels, Spruces;
	for (const FTreeModel& M : Cones) (M.Mesh->GetName().Contains(TEXT("pine"), ESearchCase::IgnoreCase) ? PineModels : Spruces).Add(M);
	if (!PineModels.Num()) PineModels = Spruces;
	if (!Spruces.Num()) Spruces = PineModels;
	if (!Broad.Num() && !Cones.Num()) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("No tree models in the game")); return; }
	TMap<UStaticMesh*, TArray<FTransform>> ByMesh;
	for (const TSharedPtr<FJsonValue>& V : J->AsArray())
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		if (!T.IsValid()) continue;
		const double X = T->GetNumberField(TEXT("x")), Y = T->GetNumberField(TEXT("y")), Zc = T->GetNumberField(TEXT("z")), H = T->GetNumberField(TEXT("h"));
		FString Kind;
		T->TryGetStringField(TEXT("kind"), Kind);
		const TArray<FTreeModel>& Pool = Conifers.Contains(Kind) ? (Pines.Contains(Kind) ? PineModels : Spruces) : Broad;
		if (!Pool.Num()) continue;
		TArray<FTreeModel> Near = Pool;
		Near.Sort([H](const FTreeModel& A, const FTreeModel& B) { return FMath::Abs(A.Height - H) < FMath::Abs(B.Height - H); });
		Near.SetNum(FMath::Min(3, Near.Num()));
		const FTreeModel& M = Near[int32(Hash01(X, Y, 3.1) * Near.Num()) % Near.Num()];
		const double S = FMath::Clamp(H / FMath::Max(1.0, M.Height), 0.7, 1.4) * (0.92 + 0.16 * Hash01(X, Y, 5.3));
		const double Yaw = Hash01(X, Y, 2.9) * 360;
		ByMesh.FindOrAdd(M.Mesh).Add(FTransform(FRotator(0, Yaw, 0), Data->LevelOrigin + FVector(X, Y, Zc - 10), FVector(S)));
	}
	int32 N = 0, Standins = 0;
	for (auto& P : ByMesh)
	{
		UHierarchicalInstancedStaticMeshComponent* C = Instances(P.Key, P.Value, TreeCull[0], TreeCull[1]);
		// (the Fab trees' own materials need a master the game hasn't got: their stand-ins)
		const TArray<FStaticMaterial>& Mats = P.Key->GetStaticMaterials();
		for (int32 i = 0; i < Mats.Num(); i++)
			if (Mats[i].MaterialInterface)
				if (UMaterialInterface* Sub = Find<UMaterialInterface>(TEXT("MI_Standin_") + Mats[i].MaterialInterface->GetName())) { C->SetMaterial(i, Sub); Standins++; }
		N += P.Value.Num();
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Trees: %d from %d models (%d stand-in materials)"), N, ByMesh.Num(), Standins);
}

// Boulders (boulders.json: [x, y, z, size cm, yaw, flat, kind]): the game's six stones
void AOTGRuntimeCourse::BuildBoulders()
{
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("boulders.json")));
	if (!J.IsValid()) return;
	TArray<UStaticMesh*> Meshes;
	for (int32 k = 0; k < BoulderKinds; k++) Meshes.Add(Find<UStaticMesh>(FString::Printf(TEXT("SM_Boulder_%d"), k)));
	TMap<int32, TArray<FTransform>> ByKind;
	for (const TSharedPtr<FJsonValue>& V : J->AsArray())
	{
		const TArray<TSharedPtr<FJsonValue>>& B = V->AsArray();
		if (B.Num() < 7) continue;
		const double S = B[3]->AsNumber() / 100.0;
		ByKind.FindOrAdd(int32(B[6]->AsNumber()) % BoulderKinds).Add(FTransform(FRotator(0, B[4]->AsNumber(), 0),
			Data->LevelOrigin + FVector(B[0]->AsNumber(), B[1]->AsNumber(), B[2]->AsNumber()), FVector(S, S, S * B[5]->AsNumber())));
	}
	int32 N = 0;
	for (auto& P : ByKind) if (Meshes[P.Key]) { Instances(Meshes[P.Key], P.Value, BoulderCull[0], BoulderCull[1]); N += P.Value.Num(); }
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Boulders: %d"), N);
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
void AOTGRuntimeCourse::BuildFarLand()
{
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
// the camera as it moves (otg::FGrass)
void AOTGRuntimeCourse::BuildGrass()
{
	TMap<FString, TPair<UStaticMesh*, UMaterialInterface*>> Have;
	for (const otg::FGrassKind& K : otg::FGrass::Kinds())
	{
		UStaticMesh* Mesh = Find<UStaticMesh>(TEXT("SM_Turf_") + K.Name);
		UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_Turf_") + K.Name);
		if (!Mesh || !Base) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Grass %s: not in the game's content"), *K.Name); continue; }
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		SetPictures(M);
		TurfMats.Add(M);
		TurfMeshes.Add(Mesh);
		Have.Add(K.Name, TPair<UStaticMesh*, UMaterialInterface*>(Mesh, M));
	}
	Grass = MakeShared<otg::FGrass>();
	Grass->Init(this, Data, MaskPixels, LandCm, Have);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Grass: %d kinds"), Have.Num());
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
