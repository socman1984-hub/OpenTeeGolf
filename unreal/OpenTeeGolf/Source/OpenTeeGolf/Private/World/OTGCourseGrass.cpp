// The runtime course's grass and turf colours (see OTGRuntimeCourse.h): the grass's meshes and
// streaming, the grass designer's kinds and looks, the turf's palette on the ground and the blades,
// and the OTG.Grass console commands.

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
				// (a KiteDemo plant: its meshes with their own materials - no tint to turn: its colour stays)
				if (const otgkite::FPlant* KP = otgkite::Plant(Plant))
				{
					for (const TCHAR* Path : KP->Meshes)
						if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path))
						{
							TurfMeshes.Add(Mesh);
							V.Add(TPair<UStaticMesh*, UMaterialInterface*>(Mesh, nullptr));
						}
					continue;
				}
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
			if (!V.Num()) UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Grass %s: none of its plants are in the game (Content/Fab, Content/KiteDemo)"), *K.Name);
		}
		else if (K.IsGreensward())
		{
			// Greensward's mats (the plugin): its static grass material (no wind). The rough one even
			// colour - make_turf.py TURF_CUTS' root and tip - with no broad variation across the ground;
			// the deep rough a greyer green with paler tips, varying (TintGrass puts them in the palette)
			const bool bDeep = K.Name.StartsWith(TEXT("deep_rough"));
			const FLinearColor Root = bDeep ? FLinearColor(0.03f, 0.042f, 0.015f) : FLinearColor(0.034f, 0.055f, 0.014f);
			const FLinearColor Tip = bDeep ? FLinearColor(0.105f, 0.115f, 0.05f) : FLinearColor(0.092f, 0.136f, 0.032f);
			UMaterialInterface* GsBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Greensward/Materials/MI_Greensward_Grass_Static.MI_Greensward_Grass_Static"));
			for (const FString& Mat : K.GsMats)
			{
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Greensward/Mats/SM_GS_%s.SM_GS_%s"), *Mat, *Mat));
				if (!Mesh) { UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("Grass %s: Greensward's %s not in the game"), *K.Name, *Mat); continue; }
				UMaterialInstanceDynamic* M = GsBase ? UMaterialInstanceDynamic::Create(GsBase, this) : nullptr;
				if (M)
				{
					M->SetScalarParameterValue(TEXT("MacroVariation"), bDeep ? 0.35f : 0.f);
					M->SetScalarParameterValue(TEXT("MicroVariation"), bDeep ? 0.5f : 0.3f);
					TurfMats.Add(M);
					GrassTints.FindOrAdd(K.Name).Add(FGrassTint{ M, false, Root, Tip, FLinearColor::White, true });
				}
				TurfMeshes.Add(Mesh);
				V.Add(TPair<UStaticMesh*, UMaterialInterface*>(Mesh, M));
			}
			UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Grass %s: %d of Greensward's mats (%d material slots)"), *K.Name, V.Num(), V.Num() ? V[0].Key->GetStaticMaterials().Num() : 0);
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
	FAutoConsoleCommandWithWorldAndArgs GrassAddCmd(TEXT("OTG.Grass.Add"), TEXT("OTG.Grass.Add <Megascans plant folder, or kite_<name>: OTGKiteDemo.h>: a plant added to the tall grass"), FGrassCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
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

// The turf's palette (OTGPalette.h), tunable live
static TAutoConsoleVariable<float> CVarPalRed(TEXT("OTG.Palette.Red"), 0.55f, TEXT("The turf's hue: red as a share of its green (lower: fresher, bluer; 0.66 was the old olive)"));
static TAutoConsoleVariable<float> CVarPalBlue(TEXT("OTG.Palette.Blue"), 0.35f, TEXT("The turf's hue: blue as a share of its green"));
static TAutoConsoleVariable<float> CVarPalGreen(TEXT("OTG.Palette.Green"), 0.08f, TEXT("The greens' lightness (their linear green; their short blades, lit from above, read about 40% lighter than this)"));
static TAutoConsoleVariable<float> CVarPalFairway(TEXT("OTG.Palette.Fairway"), 0.1f, TEXT("The fairways' and tees' lightness"));
static TAutoConsoleVariable<float> CVarPalFringe(TEXT("OTG.Palette.Fringe"), 0.095f, TEXT("The collars' lightness"));
static TAutoConsoleVariable<float> CVarPalRough(TEXT("OTG.Palette.Rough"), 0.085f, TEXT("The rough's (and first cut's) lightness"));
static TAutoConsoleVariable<float> CVarPalRoughRed(TEXT("OTG.Palette.RoughRed"), 0.88f, TEXT("The rough's blades' red against the palette's (their tips are yellower than the ground they were made for)"));
static TAutoConsoleVariable<float> CVarPalNative(TEXT("OTG.Palette.Native"), 0.9f, TEXT("The deep rough's ground: x its own straw-green"));
static TAutoConsoleVariable<float> CVarPalNativeStraw(TEXT("OTG.Palette.NativeStraw"), 0.25f, TEXT("The deep rough: how far from the rough's green to its own straw colour (0..1)"));
static TAutoConsoleVariable<float> CVarPalMat(TEXT("OTG.Palette.MatLight"), 1.15f, TEXT("Greensward's mats (the deep rough) against our blades in the same colour (their dense blades shade each other: darker)"));
static TAutoConsoleVariable<float> CVarPalCut1(TEXT("OTG.Palette.FirstCut"), 1.04f, TEXT("The first cut's blades against the rough's colour (a touch lighter: shorter)"));

// The ground's colour on every surface: the palette's, turned by the grass designer's colours - the
// fairways' and greens' looks, and the rough's kind (the ground under its blades the same as they are)
void AOTGRuntimeCourse::TintGround()
{
	if (!GroundMat) return;
	const FLinearColor TurfGreen(0.06f, 0.09f, 0.022f);
	for (const TCHAR* S : { TEXT("fairway"), TEXT("green") })
	{
		otg::FGrassKind K;
		if (const FOTGTurfLook* L = TurfLooks.Find(S)) { K.Hue = L->Hue; K.Sat = L->Sat; K.Bright = L->Bright; }
		GroundMat->SetVectorParameterValue(*(FString(TEXT("TurfTint_")) + S), otg::Palette::Factor(S) * K.TintFor(otg::Palette::Of(S)));
	}
	FLinearColor RoughLook = FLinearColor::White;
	if (Grass.IsValid())
		if (const otg::FGrassKind* R = Grass->Kinds().FindByPredicate([](const otg::FGrassKind& X) { return X.Name == TEXT("rough"); })) RoughLook = R->TintFor(TurfGreen);
	GroundMat->SetVectorParameterValue(TEXT("TurfTint_rough"), otg::Palette::Factor(TEXT("rough")) * RoughLook);
	GroundMat->SetVectorParameterValue(TEXT("TurfTint_fringe"), otg::Palette::Factor(TEXT("fringe")));
	GroundMat->SetVectorParameterValue(TEXT("TurfTint_native"), otg::Palette::Factor(TEXT("native")));
}

// Each kind's materials tinted as its colour is: our turf by its Tint (a multiplier on the green it
// draws: the palette's colour for its cut, then the designer's hue, strength, brightness); a
// Megascans plant by its Base Color Controls (strength and brightness only)
void AOTGRuntimeCourse::TintGrass()
{
	TintGround();
	if (!Grass.IsValid()) return;
	const FLinearColor TurfGreen(0.06f, 0.09f, 0.022f);
	// (the first cut's width, which the rough leaves to it - 0 when it's off: the rough to the fairway)
	const float FirstCut = otg::FGrass::FirstCutM(Grass->Kinds());
	for (const otg::FGrassKind& K : Grass->Kinds())
		if (const TArray<FGrassTint>* Ts = GrassTints.Find(K.Name))
			for (const FGrassTint& T : *Ts)
			{
				UMaterialInstanceDynamic* M = T.M.Get();
				if (!M) continue;
				if (T.bGs)
				{
					// (Greensward's: its root and tip in the palette's colour for its cut, lifted to read as
					// light as our blades would - its dense blades shade each other - turned as the designer
					// turned the kind)
					const FString S = otg::Palette::SurfaceOf(K.Name);
					const FLinearColor Tn = K.TintFor(TurfGreen) * (S.IsEmpty() ? FLinearColor::White : otg::Palette::Factor(S) * CVarPalMat.GetValueOnGameThread());
					M->SetVectorParameterValue(TEXT("RootColour"), T.Base * Tn);
					M->SetVectorParameterValue(TEXT("TipColour"), T.BaseThrough * Tn);
					continue;
				}
				if (!T.bFab)
				{
					const FString S = otg::Palette::SurfaceOf(K.Name);
					const float Cut1 = K.Name == TEXT("cut1") ? CVarPalCut1.GetValueOnGameThread() : 1.f;
					const FLinearColor Trim = S == TEXT("rough") ? FLinearColor(CVarPalRoughRed.GetValueOnGameThread(), 1.f, 1.f) : FLinearColor::White;
					M->SetVectorParameterValue(TEXT("Tint"), K.TintFor(TurfGreen) * (S.IsEmpty() ? FLinearColor::White : otg::Palette::Factor(S) * Cut1 * Trim));
					M->SetScalarParameterValue(TEXT("FirstCutM"), FirstCut);
					// (the fairways' and greens' blades take the look their ground does: SetTurfLook)
					M->SetVectorParameterValue(TEXT("TurfTint"), TurfLookTint(K.Name));
					continue;
				}
				// (Base Color Controls: the first how strong the colour is - under 1 paler, washing out;
				// the second how bright. Their tints hardly turn the hue, so a plant keeps its own: the
				// designer says so. Kept within what reads as grass)
				const float X = K.Sat < 1 ? 1.f - (1.f - K.Sat) * 0.5f : 1.f + (K.Sat - 1.f) * 0.4f;   // (0.7 .. 1.24: past it, neon)
				const float Y = K.Bright < 1 ? 1.f - (1.f - K.Bright) * 0.7f : 1.f + (K.Bright - 1.f) * 0.33f;   // (0.65 .. 1.2)
				M->SetVectorParameterValue(TEXT("Base Color Controls"), FLinearColor(T.Controls.R * X, T.Controls.G * Y, T.Controls.B, T.Controls.A));
			}
}

bool AOTGRuntimeCourse::FindSurface(const FString& Mask, const FVector& From, FVector& Out, double InsideM) const
{
	return Grass.IsValid() && Grass->FindSurface(Mask, From, Out, InsideM);
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
	// (and KiteDemo's, where the pack is in the game)
	IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
	for (const otgkite::FPlant& P : otgkite::Plants())
		if (AR.GetAssetByObjectPath(FSoftObjectPath(P.Meshes[0])).IsValid()) Out.Add(P.Key);
	return Out;
}

float AOTGRuntimeCourse::GrassHeightCm(const FString& Kind) const
{
	if (!Grass.IsValid()) return 0;
	const int32 I = Grass->Kinds().IndexOfByPredicate([&](const otg::FGrassKind& K) { return K.Name == Kind; });
	return I == INDEX_NONE ? 0 : Grass->MeshHeightCm(I);
}

void AOTGRuntimeCourse::SetTurfLook(const FString& Surface, const FOTGTurfLook& Look)
{
	TurfLooks.Add(Surface, Look);
	if (!GroundMat) return;
	TintGrass();   // (the ground, and the blades mown on it)
}

FLinearColor AOTGRuntimeCourse::TurfLookTint(const FString& Kind) const
{
	// (a grass kind named for a mown surface - make_turf.py's fairway and green - takes its look)
	const FOTGTurfLook* L = TurfLooks.Find(Kind);
	if (!L || (Kind != TEXT("fairway") && Kind != TEXT("green"))) return FLinearColor::White;
	otg::FGrassKind K;
	K.Hue = L->Hue; K.Sat = L->Sat; K.Bright = L->Bright;
	return K.TintFor(otg::Palette::Of(Kind));
}

TMap<FString, FOTGTurfLook> AOTGRuntimeCourse::SavedTurfLooks() const
{
	TMap<FString, FOTGTurfLook> Out;
	if (!Data) return Out;
	const TSharedPtr<FJsonValue> V = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("turf.json")));
	const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
	if (!O.IsValid()) return Out;
	for (const TCHAR* S : { TEXT("fairway"), TEXT("green") })
	{
		const TSharedPtr<FJsonObject>* Lp = nullptr;
		if (!O->TryGetObjectField(S, Lp) || !Lp || !Lp->IsValid()) continue;
		const TSharedPtr<FJsonObject> L = *Lp;
		FOTGTurfLook T;
		double D;
		if (L->TryGetNumberField(TEXT("hue"), D)) T.Hue = FMath::Clamp(float(D), -otg::FGrassKind::HueMax, otg::FGrassKind::HueMax);
		if (L->TryGetNumberField(TEXT("sat"), D)) T.Sat = FMath::Clamp(float(D), otg::FGrassKind::SatLo, otg::FGrassKind::SatHi);
		if (L->TryGetNumberField(TEXT("bright"), D)) T.Bright = FMath::Clamp(float(D), otg::FGrassKind::BrightLo, otg::FGrassKind::BrightHi);
		Out.Add(S, T);
	}
	return Out;
}

bool AOTGRuntimeCourse::SaveTurfLooks(FString* Where) const
{
	if (!Data) return false;
	const FString File = FPaths::Combine(Data->ExportDir, TEXT("turf.json"));
	if (Where) *Where = File;
	const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	for (const auto& P : TurfLooks)
	{
		if (P.Value.IsOwn()) continue;
		const TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetNumberField(TEXT("hue"), P.Value.Hue);
		L->SetNumberField(TEXT("sat"), P.Value.Sat);
		L->SetNumberField(TEXT("bright"), P.Value.Bright);
		O->SetObjectField(P.Key, L);
	}
	FString Text;
	FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TArray<otg::FGrassKind> AOTGRuntimeCourse::SavedGrassKinds() const
{
	return Data ? otg::FGrass::LoadKinds(Data->ExportDir) : otg::FGrass::DefaultKinds();
}

int32 AOTGRuntimeCourse::GrassInstances() const
{
	return Grass.IsValid() ? Grass->Instances() : 0;
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
