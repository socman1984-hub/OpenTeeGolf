// The land beyond the runtime course (see OTGRuntimeCourse.h): the real land round it (farland.json)
// or rings of hills made up, out to the horizon.

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
