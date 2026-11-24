// The runtime course's props (see OTGRuntimeCourse.h): cart paths, bridges, buildings and cars, and
// the path mask drawn again under the paths as built. (Houses: OTGHouses.cpp; cars: OTGCars.cpp.)

#include "World/OTGRuntimeCourse.h"

#include "Course/OTGCourseData.h"
#include "Glb/OTGGlb.h"
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

// The cart paths' mask (the ground's paint, and no grass on them) drawn again from their own mesh:
// the export's is the course's surface map, a pixel a metre or so, blurred - its edges wandered in
// and out along a path. The mesh is the path's line at its width: straight, even edges.
// A distance field as the export's masks are: 0.5 on the edge, 3 m = 0.5.
void FOTGCourseProps::RedrawPathMask(const otg::FGlbMesh& G)
{
	otg::FPicture* P = Course->MaskPixels.Find(TEXT("path"));
	if (!P || !P->W || !Course->GroundMat || G.Indices.Num() < 3 || FParse::Param(FCommandLine::Get(), TEXT("oldpaths"))) return;
	const int32 W = P->W, H = P->H;
	const double Px = Course->LandCm / W, Py = Course->LandCm / H, Reach = 300.0;
	auto At = [&](int32 v) { const FVector3f& V = G.Positions[v]; return FVector2D(V.X * 100.0, V.Z * 100.0); };
	// the outline: the edges of one triangle only (vertices matched by place: a strip's are shared or not)
	auto Key = [&](int32 v) { const FVector2D A = At(v); return FIntPoint(FMath::RoundToInt(A.X), FMath::RoundToInt(A.Y)); };
	TMap<TPair<FIntPoint, FIntPoint>, int32> Edges;
	auto EdgeKey = [](FIntPoint A, FIntPoint B) { if (A.X > B.X || (A.X == B.X && A.Y > B.Y)) Swap(A, B); return TPair<FIntPoint, FIntPoint>(A, B); };
	for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
		for (int32 k = 0; k < 3; k++) Edges.FindOrAdd(EdgeKey(Key(G.Indices[t + k]), Key(G.Indices[t + (k + 1) % 3])))++;
	TArray<float> Dist;
	Dist.Init(float(Reach), W * H);
	TArray<uint8> In;
	In.Init(0, W * H);
	// inside: each triangle's pixels
	for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
	{
		const FVector2D A = At(G.Indices[t]), B = At(G.Indices[t + 1]), C = At(G.Indices[t + 2]);
		const double D = (B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y);
		if (FMath::Abs(D) < 1e-6) continue;
		const int32 I0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) / Px)), I1 = FMath::Min(W - 1, FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X) / Px));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) / Py)), J1 = FMath::Min(H - 1, FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y) / Py));
		for (int32 j = J0; j <= J1; j++)
			for (int32 i = I0; i <= I1; i++)
			{
				const double X = (i + 0.5) * Px, Y = (j + 0.5) * Py;
				const double U = ((B.X - X) * (C.Y - Y) - (C.X - X) * (B.Y - Y)) / D, V = ((C.X - X) * (A.Y - Y) - (A.X - X) * (C.Y - Y)) / D;
				if (U >= 0 && V >= 0 && U + V <= 1) In[j * W + i] = 1;
			}
	}
	// the distance to the outline, within 3 m of it
	int32 Outline = 0;
	for (const auto& E : Edges)
	{
		if (E.Value != 1) continue;
		Outline++;
		const FVector2D A(E.Key.Key), B(E.Key.Value);
		const int32 I0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.X, B.X) - Reach) / Px)), I1 = FMath::Min(W - 1, FMath::CeilToInt((FMath::Max(A.X, B.X) + Reach) / Px));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.Y, B.Y) - Reach) / Py)), J1 = FMath::Min(H - 1, FMath::CeilToInt((FMath::Max(A.Y, B.Y) + Reach) / Py));
		const FVector2D AB = B - A;
		const double L2 = FMath::Max(AB.SizeSquared(), 1e-6);
		for (int32 j = J0; j <= J1; j++)
			for (int32 i = I0; i <= I1; i++)
			{
				const FVector2D Q((i + 0.5) * Px, (j + 0.5) * Py);
				const double T = FMath::Clamp(FVector2D::DotProduct(Q - A, AB) / L2, 0.0, 1.0);
				float& Dv = Dist[j * W + i];
				Dv = FMath::Min(Dv, float(FVector2D::Distance(Q, A + AB * T)));
			}
	}
	for (int32 q = 0; q < W * H; q++)
	{
		const double S = (In[q] ? 1.0 : -1.0) * Dist[q] / 600.0;
		P->Pixels[q] = uint8(FMath::Clamp(0.5 + S, 0.0, 1.0) * 255.0 + 0.5);
	}
	if (UTexture2D* T = otg::MakeTexture(*P, otg::ETexKind::Mask, Course))
	{
		Course->Textures.Add(TEXT("Mask_path"), T);
		Course->GroundMat->SetTextureParameterValue(TEXT("Mask_path"), T);
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Paths: the mask drawn from their mesh (%d outline edges)"), Outline);
}

void FOTGCourseProps::BuildProps()
{
	Course->Water.RedrawPondMasks();
	const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Course->Data->ExportDir, TEXT("props.json")));
	if (!J.IsValid()) return;
	for (const TSharedPtr<FJsonValue>& V : J->AsArray())
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Kind = P->GetStringField(TEXT("kind"));
		otg::FGlbMesh G;
		if (!otg::ReadGlb(FPaths::Combine(Course->Data->ExportDir, P->GetStringField(TEXT("file"))), G)) continue;
		const bool bPath = Kind.StartsWith(TEXT("path"));
		// The paths are painted in the ground (its path mask): their mesh is only wanted where it
		// leaves the ground - bridges, boardwalks. Lying on the ground, sunk a little, it pokes up
		// through it in patches (the ground's own triangles, and coarser ones further off, aren't
		// the path's): those triangles go
		if (bPath)
		{
			RedrawPathMask(G);
			const int32 Before = G.Indices.Num() / 3;
			TArray<int32> Keep;
			for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
			{
				bool bOff = false;
				for (int32 k = 0; k < 3 && !bOff; k++)
				{
					const FVector3f& Pv = G.Positions[G.Indices[t + k]];
					bOff = Pv.Y * 100.0 - Course->Ground.GroundAt(Pv.X * 100.0, Pv.Z * 100.0) > PathOffGroundCm;
				}
				if (bOff) Keep.Append(&G.Indices[t], 3);
			}
			G.Indices = MoveTemp(Keep);
			UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Props: %s, %d of %d triangles off the ground (the rest painted in it)"), *Kind, G.Indices.Num() / 3, Before);
			if (!G.Indices.Num()) continue;
		}
		if (Kind == TEXT("car") && PlaceCars(G)) continue;
		if (Kind == TEXT("building") && HouseFeet.Num())
		{
			// (a Hearthline house stands there: OTGHouses.cpp)
			const int32 Before = G.Indices.Num() / 3;
			TArray<int32> Keep;
			for (int32 t = 0; t + 2 < G.Indices.Num(); t += 3)
			{
				// (its middle or any corner on it: the blocks' roofs reach well past their footprints, their eaves' triangles
				// long - by their middles, some stayed, spikes across the sky)
				const FVector3f Mid = (G.Positions[G.Indices[t]] + G.Positions[G.Indices[t + 1]] + G.Positions[G.Indices[t + 2]]) / 3.f;
				bool bUnder = UnderHouse(FVector2D(Mid.X, Mid.Z));
				for (int32 k = 0; k < 3 && !bUnder; k++) { const FVector3f& Pv = G.Positions[G.Indices[t + k]]; bUnder = UnderHouse(FVector2D(Pv.X, Pv.Z)); }
				if (!bUnder) Keep.Append(&G.Indices[t], 3);
			}
			G.Indices = MoveTemp(Keep);
			UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Props: building, %d of %d triangles left (the rest Hearthline houses)"), G.Indices.Num() / 3, Before);
			if (!G.Indices.Num()) continue;
		}
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("M_Prop_") + Kind);
		if (!M) M = Load<UMaterialInterface>(TEXT("M_Prop_building"));
		UStaticMeshComponent* C = MeshFromGlb(Course, G, Course->Data->LevelOrigin, bPath ? PathSinkCm : 0, !bPath, M);
		Course->Parts.Add(C);
		UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Props: %s, %d triangles"), *Kind, G.Indices.Num() / 3);
	}
}
