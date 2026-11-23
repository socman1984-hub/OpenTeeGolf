// Helpers shared by the runtime course's files (OTGRuntimeCourse.h): the game's runtime content and
// its loaders, the tree models, culling distances, and a mesh made from a glb. Private to World.

#pragma once

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

namespace
{
	const TCHAR* Runtime = TEXT("/Game/OpenTeeGolf/Game/Runtime");
	const TCHAR* MaskKeys[] = { TEXT("fairway"), TEXT("green"), TEXT("tee"), TEXT("bunker"), TEXT("water"), TEXT("rough"), TEXT("native"), TEXT("stream"), TEXT("path"), TEXT("fringe"), TEXT("road") };
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
