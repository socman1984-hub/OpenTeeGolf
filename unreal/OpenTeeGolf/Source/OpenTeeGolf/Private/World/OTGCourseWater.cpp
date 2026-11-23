// The runtime course's water (see OTGRuntimeCourse.h): the ponds and streams, the sea, and the
// ponds' masks drawn again round the water as built. (The Tidewright water itself: OTGWater.cpp.)

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

// The ponds and streams (water.glb): one surface, the game's water on it
void AOTGRuntimeCourse::BuildWater()
{
	if (!bWaterSettled) SettleWater();
	if (!WaterPos.Num()) return;
	otg::FGlbMesh G;
	G.Positions = WaterPos;   // (flat enough: straight up everywhere - no normals)
	G.Indices = WaterIdx;
	// (the ponds and rivers Tidewright draws - OTGWater.cpp: the game's own water under them gone)
	// (every pond, lake and river is Tidewright water now - OTGWater.cpp - and none of the game's own
	// is drawn; -oldwater draws it as before, for comparison)
	TArray<bool> Gone;
	if (BuildTidewright(Gone)) return;
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
	FVector2D Swell = FVector2D::ZeroVector;
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Data->ExportDir, TEXT("sea.json")));
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (J.IsValid() && J->TryGetObject(Obj))
	{
		double L = 0;
		if ((*Obj)->TryGetNumberField(TEXT("levelMetres"), L)) SeaLevelM = L;
		const TSharedPtr<FJsonObject>* Sw = nullptr;
		if ((*Obj)->TryGetObjectField(TEXT("swell"), Sw))
		{
			Swell = FVector2D((*Sw)->GetNumberField(TEXT("x")), (*Sw)->GetNumberField(TEXT("y")));
			M->SetVectorParameterValue(TEXT("SwellDir"), FLinearColor(Swell.X, Swell.Y, 0, 0));
		}
	}
	// (Tidewright's ocean: OTGWater.cpp. -oldwater: the game's own sea, as before)
	if (BuildTidewrightSea(G, Box, Swell, SeaLevelM)) return;
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

// The ponds' part of the water mask (the ground's paint - the pond's bed - and no grass on it) drawn
// again from their rounded outlines (LiftPondBanks, which shaped the banks to them): the export's is
// the raw outline, a corner every 5 m - a pale strip of bare bank with straight edges along the
// shore, between where the grass stopped and the water began. Within 3 m of a rounded outline, and
// inside it; the rest (the streams) as it was. A distance field as the export's masks are.
void AOTGRuntimeCourse::RedrawPondMasks()
{
	otg::FPicture* P = MaskPixels.Find(TEXT("water"));
	if (!P || !P->W || !GroundMat || !PondOutlines.Num() || FParse::Param(FCommandLine::Get(), TEXT("oldpondmask"))) return;
	const int32 W = P->W, H = P->H;
	const double Px = LandCm / W, Py = LandCm / H, Reach = 300.0;
	int32 Changed = 0;
	for (int32 k = 0; k < PondOutlines.Num(); k++)
	{
		const TArray<FVector2D>& R = PondOutlines[k];
		if (R.Num() < 3) continue;
		const double Level = PondLevels.IsValidIndex(k) ? PondLevels[k] : -1e9;
		const FBox2D Box = FBox2D(R).ExpandBy(Reach);
		const int32 I0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X / Px)), I1 = FMath::Min(W - 1, FMath::CeilToInt(Box.Max.X / Px));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y / Py)), J1 = FMath::Min(H - 1, FMath::CeilToInt(Box.Max.Y / Py));
		for (int32 j = J0; j <= J1; j++)
			for (int32 i = I0; i <= I1; i++)
			{
				const FVector2D Q((i + 0.5) * Px, (j + 0.5) * Py);
				bool bIn = false;
				double D2 = TNumericLimits<double>::Max();
				for (int32 a = 0, b = R.Num() - 1; a < R.Num(); b = a++)
				{
					const FVector2D& Pa = R[a]; const FVector2D& Pb = R[b];
					if ((Pa.Y > Q.Y) != (Pb.Y > Q.Y) && Q.X < (Pb.X - Pa.X) * (Q.Y - Pa.Y) / (Pb.Y - Pa.Y) + Pa.X) bIn = !bIn;
					const FVector2D AB = Pb - Pa;
					const double T = FMath::Clamp(FVector2D::DotProduct(Q - Pa, AB) / FMath::Max(AB.SizeSquared(), 1e-6), 0.0, 1.0);
					D2 = FMath::Min(D2, FVector2D::DistSquared(Q, Pa + AB * T));
				}
				const double D = FMath::Sqrt(D2);
				if (!bIn && D > Reach) continue;
				// (the shore just under the water - drawn dark, the water over it - only where the ground
				// is: on a bank standing well over the water, a berm holding the pond, it showed black)
				if (!bIn && GroundAt(Q.X, Q.Y) > Level + 4.0) { P->Pixels[j * W + i] = 0; Changed++; continue; }
				const double S = (bIn ? 1.0 : -1.0) * D / 600.0;
				P->Pixels[j * W + i] = uint8(FMath::Clamp(0.5 + S, 0.0, 1.0) * 255.0 + 0.5);
				Changed++;
			}
	}
	// (a berm holding a pond over a creek's channel or a ditch - HoldPonds: rough, not the export's water
	// and stream bed there. With those only cleared, no surface was left and the ground fell back to
	// the aerial photo, the dark water of the channel: a black patch)
	otg::FPicture* St = MaskPixels.Find(TEXT("stream"));
	otg::FPicture* Ro = MaskPixels.Find(TEXT("rough"));
	const double Step = Data->MetresPerSample * 100.0;
	for (const FIntPoint& C : BermCells)
		for (otg::FPicture* M : { P, St, Ro })
		{
			if (!M || !M->W) continue;
			const double Mx = LandCm / M->W, My = LandCm / M->H;
			const int32 I0 = FMath::Max(0, FMath::FloorToInt((C.X - 0.5) * Step / Mx)), I1 = FMath::Min(M->W - 1, FMath::CeilToInt((C.X + 0.5) * Step / Mx));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt((C.Y - 0.5) * Step / My)), J1 = FMath::Min(M->H - 1, FMath::CeilToInt((C.Y + 0.5) * Step / My));
			for (int32 j = J0; j <= J1; j++) for (int32 i = I0; i <= I1; i++) M->Pixels[j * M->W + i] = M == Ro ? 255 : 0;
		}
	if (UTexture2D* T = otg::MakeTexture(*P, otg::ETexKind::Mask, this))
	{
		Textures.Add(TEXT("Mask_water"), T);
		GroundMat->SetTextureParameterValue(TEXT("Mask_water"), T);
	}
	if (BermCells.Num())
		for (const TPair<const TCHAR*, otg::FPicture*> KV : { TPair<const TCHAR*, otg::FPicture*>(TEXT("Mask_stream"), St), TPair<const TCHAR*, otg::FPicture*>(TEXT("Mask_rough"), Ro) })
			if (KV.Value && KV.Value->W)
				if (UTexture2D* T = otg::MakeTexture(*KV.Value, otg::ETexKind::Mask, this))
				{
					Textures.Add(KV.Key, T);
					GroundMat->SetTextureParameterValue(KV.Key, T);
				}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Ponds: the water mask drawn from their rounded outlines (%d pixels)"), Changed);
}
