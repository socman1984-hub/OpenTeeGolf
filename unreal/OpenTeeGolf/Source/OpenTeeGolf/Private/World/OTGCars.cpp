// The parked cars: the web export's are plain boxes (props_car.glb), which the game no longer draws.
// Each car's collision box (play.json's solids: a 4-cornered footprint 4.6-4.8 m long, 1.45 or
// 1.75 m high - scene.js addCars) says where it stands and which way it points, and a car from a
// car pack stands there instead: any static mesh in the game's content under a folder with "car" or
// "vehicle" in its path, car sized (3-6.5 m long). No pack: no cars, and their collision boxes go too
// (a ball bounced off nothing). -boxcars: the export's boxes as before.
// The pack in the game's content: Kerbside Cars (Content/KerbsideCars, copied from the pack's own
// project). Its one car material takes each car's paint as per-instance custom data - linear RGB,
// metallic, rim darkness - so each car gets a colour from a car park's usual mix.

#include "World/OTGRuntimeCourse.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGGlb.h"

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
		if (AR.IsLoadingAssets()) AR.WaitForCompletion();   // (uncooked: still scanning at start-up)
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

	// a car park's colours (sRGB, metallic): mostly white, black, grey and silver, a few colours
	struct FPaint { uint8 R, G, B; float Metal; };
	const FPaint Paints[] = {
		{ 236, 236, 232, 0.0f }, { 236, 236, 232, 0.0f }, { 230, 230, 226, 0.6f }, { 240, 238, 230, 0.0f },
		{ 18, 18, 20, 0.0f }, { 22, 22, 26, 0.8f }, { 18, 18, 20, 0.0f },
		{ 150, 152, 156, 1.0f }, { 170, 172, 175, 1.0f }, { 150, 152, 156, 1.0f },
		{ 88, 90, 94, 0.9f }, { 70, 72, 76, 0.8f }, { 100, 102, 106, 0.0f },
		{ 22, 40, 82, 0.8f }, { 40, 70, 120, 0.7f }, { 140, 20, 22, 0.6f }, { 170, 28, 28, 0.0f },
		{ 30, 60, 40, 0.7f }, { 110, 90, 70, 0.7f }, { 180, 170, 150, 0.6f }, { 60, 30, 34, 0.8f },
	};
}

bool FOTGCourseProps::PlaceCars(const otg::FGlbMesh&)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("boxcars"))) return false;
	// the cars' boxes (the play frame: x, z metres) - each a car's spot in the glb's frame
	TArray<FCarSpot> Spots;
	TArray<int32> Boxes;
	for (int32 s = 0; s < Course->Data->Solids.Num(); s++)
	{
		const FOTGSolid& S = Course->Data->Solids[s];
		TArray<FVector2D> Pts = S.Footprint;
		if (Pts.Num() == 5 && Pts[0].Equals(Pts.Last(), 1e-3)) Pts.Pop();
		if (Pts.Num() != 4 || S.Height > 2.0) continue;
		const double L = FVector2D::Distance(Pts[0], Pts[3]), W = FVector2D::Distance(Pts[0], Pts[1]);
		if (L < 4.0 || L > 5.5 || W < 1.5 || W > 2.3) continue;
		FCarSpot C;
		const FVector2D Mid = (Pts[0] + Pts[1] + Pts[2] + Pts[3]) * 0.25;
		C.At = FVector2D(Mid.X - Course->Data->HeightX0, Course->Data->HeightZ1 - Mid.Y);   // (the play frame to the glb's)
		const FVector2D Ax = Pts[3] - Pts[0];
		C.Yaw = FMath::Atan2(-Ax.Y, Ax.X);   // (z flipped)
		C.Length = L;
		C.Bottom = Course->Ground.GroundAt(C.At.X * 100.0, C.At.Y * 100.0) / 100.0;
		Spots.Add(C);
		Boxes.Add(s);
	}
	const TArray<UStaticMesh*> Pack = PackCars();
	if (!Pack.Num())
	{
		// no cars, and none of their collision boxes
		const int32 Before = Course->Data->Solids.Num();
		for (int32 k = Boxes.Num() - 1; k >= 0; k--) Course->Data->Solids.RemoveAt(Boxes[k]);
		UE_LOG(LogOTGCars, Display, TEXT("Cars: %d spots, no car pack in the game's content - none drawn (%d collision boxes taken out)"), Spots.Num(), Before - Course->Data->Solids.Num());
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
			H = NewObject<UHierarchicalInstancedStaticMeshComponent>(Course);
			H->SetStaticMesh(M);
			H->NumCustomDataFloats = 5;   // (paint RGB, metallic, rim darkness)
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->SetMobility(EComponentMobility::Static);
			H->SetupAttachment(Course->GetRootComponent());
			H->RegisterComponent();
			Course->Parts.Add(H);
		}
		// (the glb's x, z: the world's X, Y; its long axis along the mesh's longer side; pointing either
		// way along the bay, chosen by the car)
		const FVector Ext = M->GetBoundingBox().GetSize();
		const double MeshYaw = Ext.X >= Ext.Y ? 0.0 : 90.0;
		const double Flip = ((uint32(i) * 40503u) >> 5) & 1 ? 180.0 : 0.0;
		const double Yaw = FMath::RadiansToDegrees(S.Yaw) - MeshYaw + Flip;
		const FVector Base = Course->Data->LevelOrigin + FVector(S.At.X * 100.0, S.At.Y * 100.0, S.Bottom * 100.0 - M->GetBoundingBox().Min.Z);
		const int32 Inst = H->AddInstance(FTransform(FRotator(0, Yaw, 0), Base), true);
		const uint32 Hash = uint32(i) * 2246822519u ^ 0x9E3779B9u;
		const FPaint& P = Paints[(Hash >> 9) % UE_ARRAY_COUNT(Paints)];
		const FLinearColor Lin = FLinearColor(FColor(P.R, P.G, P.B));
		const float Rim = ((Hash >> 3) % 5) == 0 ? 1.0f : 0.0f;   // (one in five with black wheels)
		H->SetCustomData(Inst, { Lin.R, Lin.G, Lin.B, P.Metal, Rim }, true);
	}
	UE_LOG(LogOTGCars, Display, TEXT("Cars: %d placed from %d pack meshes, the first at %s"), Spots.Num(), Pack.Num(),
		*(Spots.Num() ? (Course->Data->LevelOrigin + FVector(Spots[0].At.X * 100.0, Spots[0].At.Y * 100.0, Spots[0].Bottom * 100.0)).ToString() : FString()));
	return true;
}
