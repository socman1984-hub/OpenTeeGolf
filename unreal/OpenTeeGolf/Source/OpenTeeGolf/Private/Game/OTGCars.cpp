// The parked cars: the web export's are plain boxes (props_car.glb), which the game no longer draws.
// Each car's collision box (play.json's solids: a 4-cornered footprint 4.6-4.8 m long, 1.45 or
// 1.75 m high - scene.js addCars) says where it stands and which way it points, and a car from a
// car pack stands there instead: any static mesh in the game's content under a folder with "car" or
// "vehicle" in its path, car sized (3-6.5 m long). No pack: no cars, and their collision boxes go too
// (a ball bounced off nothing). -boxcars: the export's boxes as before.

#include "Game/OTGRuntimeCourse.h"
#include "OTGCourseData.h"
#include "OTGGlb.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGCars, Log, All);

namespace
{
	struct FCarSpot { FVector2D At; double Yaw = 0; double Bottom = 0; double Length = 0; };   // (glb frame, metres; yaw: its long axis from +X towards +Z)

	// The car pack's cars: static meshes under a car or vehicle folder, car sized
	TArray<UStaticMesh*> PackCars()
	{
		TArray<FAssetData> All;
		IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();
		FARFilter F;
		F.PackagePaths.Add(TEXT("/Game"));
		F.bRecursivePaths = true;
		F.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		AR.GetAssets(F, All);
		TArray<UStaticMesh*> Out;
		for (const FAssetData& A : All)
		{
			const FString Path = A.PackagePath.ToString();
			if (!Path.Contains(TEXT("car"), ESearchCase::IgnoreCase) && !Path.Contains(TEXT("vehicle"), ESearchCase::IgnoreCase)) continue;
			if (Path.Contains(TEXT("/Courses/"))) continue;
			const FString Name = A.AssetName.ToString();
			// (not a wheel, door or interior on its own)
			if (Name.Contains(TEXT("wheel"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("tire"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("door"), ESearchCase::IgnoreCase)) continue;
			UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
			if (!M) continue;
			const FVector Ext = M->GetBoundingBox().GetSize();
			const double L = FMath::Max(Ext.X, Ext.Y), W = FMath::Min(Ext.X, Ext.Y);
			if (L < 300 || L > 650 || W < 140 || W > 260 || Ext.Z < 100 || Ext.Z > 260) continue;
			Out.Add(M);
		}
		return Out;
	}
}

bool AOTGRuntimeCourse::PlaceCars(const otg::FGlbMesh&)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("boxcars"))) return false;
	// the cars' boxes (the play frame: x, z metres) - each a car's spot in the glb's frame
	TArray<FCarSpot> Spots;
	TArray<int32> Boxes;
	for (int32 s = 0; s < Data->Solids.Num(); s++)
	{
		const FOTGSolid& S = Data->Solids[s];
		TArray<FVector2D> Pts = S.Footprint;
		if (Pts.Num() == 5 && Pts[0].Equals(Pts.Last(), 1e-3)) Pts.Pop();
		if (Pts.Num() != 4 || S.Height > 2.0) continue;
		const double L = FVector2D::Distance(Pts[0], Pts[3]), W = FVector2D::Distance(Pts[0], Pts[1]);
		if (L < 4.0 || L > 5.5 || W < 1.5 || W > 2.3) continue;
		FCarSpot C;
		const FVector2D Mid = (Pts[0] + Pts[1] + Pts[2] + Pts[3]) * 0.25;
		C.At = FVector2D(Mid.X - Data->HeightX0, Data->HeightZ1 - Mid.Y);   // (the play frame to the glb's)
		const FVector2D Ax = Pts[3] - Pts[0];
		C.Yaw = FMath::Atan2(-Ax.Y, Ax.X);   // (z flipped)
		C.Length = L;
		C.Bottom = GroundAt(C.At.X * 100.0, C.At.Y * 100.0) / 100.0;
		Spots.Add(C);
		Boxes.Add(s);
	}
	const TArray<UStaticMesh*> Pack = PackCars();
	if (!Pack.Num())
	{
		// no cars, and none of their collision boxes
		const int32 Before = Data->Solids.Num();
		for (int32 k = Boxes.Num() - 1; k >= 0; k--) Data->Solids.RemoveAt(Boxes[k]);
		UE_LOG(LogOTGCars, Display, TEXT("Cars: %d spots, no car pack in the game's content - none drawn (%d collision boxes taken out)"), Spots.Num(), Before - Data->Solids.Num());
		return true;
	}
	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> Comps;
	for (int32 i = 0; i < Spots.Num(); i++)
	{
		const FCarSpot& S = Spots[i];
		UStaticMesh* M = Pack[(uint32(i) * 2654435761u >> 7) % uint32(Pack.Num())];
		UHierarchicalInstancedStaticMeshComponent*& H = Comps.FindOrAdd(M);
		if (!H)
		{
			H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			H->SetStaticMesh(M);
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->SetMobility(EComponentMobility::Static);
			H->SetupAttachment(GetRootComponent());
			H->RegisterComponent();
			Parts.Add(H);
		}
		// (the glb's x, z: the world's X, Y; its long axis along the mesh's longer side; pointing either
		// way along the bay, chosen by the car)
		const FVector Ext = M->GetBoundingBox().GetSize();
		const double MeshYaw = Ext.X >= Ext.Y ? 0.0 : 90.0;
		const double Flip = ((uint32(i) * 40503u) >> 5) & 1 ? 180.0 : 0.0;
		const double Yaw = FMath::RadiansToDegrees(S.Yaw) - MeshYaw + Flip;
		const FVector Base = Data->LevelOrigin + FVector(S.At.X * 100.0, S.At.Y * 100.0, S.Bottom * 100.0 - M->GetBoundingBox().Min.Z);
		H->AddInstance(FTransform(FRotator(0, Yaw, 0), Base), true);
	}
	UE_LOG(LogOTGCars, Display, TEXT("Cars: %d placed from %d pack meshes"), Spots.Num(), Pack.Num());
	return true;
}
