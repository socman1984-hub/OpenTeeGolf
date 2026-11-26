// The runtime course's trees and rocks (see OTGRuntimeCourse.h): drawn from trees.json and
// boulders.json as the tree design has them, and picked and marked for the tree designer.

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

// The trees (trees.json): Unreal's own models - each tree one of the three nearest its height
// (picked by its position, so the woods mix), scaled to the game's height within reason, turned
// at random; conifers the pine and spruce models (pines for the pines). As build_course.py does it.
// Then as the course's tree designer has them (UOTGCourseData::ForEachDesignedTree, as the ball's
// trees are): a species drawn with the model picked for it, larger or smaller, fewer or more.
void FOTGCourseTrees::BuildTrees()
{
	if (!bTreesRead)
	{
		bTreesRead = true;
		const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Course->Data->ExportDir, TEXT("trees.json")));
		if (J.IsValid())
			for (const TSharedPtr<FJsonValue>& V : J->AsArray())
			{
				const TSharedPtr<FJsonObject> T = V->AsObject();
				if (!T.IsValid()) continue;
				FOTGCourseTrees::FTreeRec R;
				R.X = T->GetNumberField(TEXT("x")); R.Y = T->GetNumberField(TEXT("y")); R.Z = T->GetNumberField(TEXT("z")); R.H = T->GetNumberField(TEXT("h"));
				T->TryGetStringField(TEXT("kind"), R.Kind);
				TreeRecs.Add(R);
			}
	}
	for (UHierarchicalInstancedStaticMeshComponent* C : TreeComps) if (C) { Course->Parts.Remove(C); C->DestroyComponent(); }
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
	const FOTGTreeDesign& Design = Course->Data->TreeDesign;
	// (KiteDemo's models the design uses, where the pack is in the game: else a tree drawn as one is
	// drawn as its species)
	TMap<FString, TArray<FTreeModel>> KiteModels;
	{
		TSet<FString> Used;
		for (const auto& KV : Design.Species) Used.Add(KV.Value.Model);
		for (const FOTGTreeEdit& E : Design.Trees) Used.Add(E.Model);
		for (const FString& Model : Used)
			for (const TCHAR* Path : otgkite::TreeMeshes(Model))
				if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path)) KiteModels.FindOrAdd(Model).Add({ M, M->GetBoundingBox().GetSize().Z });
	}
	struct FGroup { TArray<FTransform> Xfs; TArray<FLinearColor> Tints; bool bTinted = false; };
	TMap<UStaticMesh*, FGroup> ByMesh;
	DrawnTrees.Reset();
	// (E: the tree's own changes from the designer, if it has any - its species, model, leaf colour)
	auto Place = [&](const FString& InKind, double X, double Y, double Zc, double H, double Scale, const FOTGTreeEdit* E) {
		const FString Kind = E && !E->Kind.IsEmpty() ? E->Kind : InKind;
		const FString Model = E && !E->Model.IsEmpty() ? E->Model : Design.For(Kind).Model;
		const TArray<FTreeModel>* Kite = KiteModels.Find(Model);
		const TArray<FTreeModel>& Pool = Kite && Kite->Num() ? *Kite : Model == TEXT("beech") ? Beeches : Model == TEXT("hornbeam") ? Hornbeams : Model == TEXT("spruce") ? Spruces : Model == TEXT("pine") ? PineModels
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
		const FVector Foot = Course->Data->LevelOrigin + FVector(X, Y, Zc - 10);
		G.Xfs.Add(FTransform(FRotator(0, Yaw, 0), Foot, FVector(S)));
		const FLinearColor T = E ? E->LeafTint() : FLinearColor(0, 0, 0, 0);
		G.Tints.Add(T);
		G.bTinted |= E && E->HasColour();
		FOTGCourseTrees::FDrawn& D = DrawnTrees.AddDefaulted_GetRef();
		D.Base = Foot; D.H = M.Height * S; D.R = FMath::Max(150.0, D.H * 0.22); D.Kind = Kind; D.bAdded = E && E->bAdded; D.Mesh = M.Mesh;
	};
	if (TreeRecs.Num() == Course->Data->Trees.Num())
	{
		Course->Data->ForEachDesignedTree([&](int32 Src, double X, double Z, double Ground, float Scale, const FOTGTreeEdit* E) {
			const int32 n = DrawnTrees.Num();
			if (Src < 0)
			{
				// (added in the designer: standing on the ground where it was put, its own height)
				const double H = E->Height * E->Size * 100.0;
				Place(E->Kind, (X - Course->Data->HeightX0) * 100.0, (Course->Data->HeightZ1 - Z) * 100.0, (Ground - Course->Data->HeightBase) * 100.0, H, FMath::Max(0.3, H / 1500.0), E);
			}
			else
			{
				const FOTGCourseTrees::FTreeRec& R = TreeRecs[Src];
				const FOTGTreeShape& S = Course->Data->Trees[Src];
				const double Zc = R.Z + (Ground - Course->Data->GroundAt(S.X, S.Z)) * 100.0;
				Place(R.Kind, (X - Course->Data->HeightX0) * 100.0, (Course->Data->HeightZ1 - Z) * 100.0, Zc, R.H * Scale, Scale, E);
			}
			if (DrawnTrees.Num() > n) { DrawnTrees.Last().X = X; DrawnTrees.Last().Z = Z; }
		});
	}
	else
	{
		UE_LOG(LogOTGRuntimeCourse, Warning, TEXT("trees.json (%d) and play.json (%d) disagree: the trees drawn as exported"), TreeRecs.Num(), Course->Data->Trees.Num());
		for (const FOTGCourseTrees::FTreeRec& R : TreeRecs) Place(R.Kind, R.X, R.Y, R.Z, R.H, 1.0, nullptr);
		DrawnTrees.Reset();   // (no designing them one at a time: they aren't the ball's trees)
	}
	int32 Standins = 0;
	TSet<UStaticMesh*> Colourable;
	for (auto& P : ByMesh)
	{
		UHierarchicalInstancedStaticMeshComponent* C = Course->Instances(P.Key, P.Value.Xfs, TreeCull[0], TreeCull[1]);
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
	for (FOTGCourseTrees::FDrawn& D : DrawnTrees) D.bColourable = Colourable.Contains(D.Mesh);
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Trees: %d from %d models (%d stand-in materials)%s"), TreeCount, ByMesh.Num(), Standins, Design.IsDefault() ? TEXT("") : TEXT(", as the course's tree designer has them"));
}

// Boulders (boulders.json: [x, y, z, size cm, yaw, flat, kind]): the game's six stones (as the
// tree designer has them: there or not, how many, how big)
void FOTGCourseTrees::BuildBoulders()
{
	if (!bRocksRead)
	{
		bRocksRead = true;
		const TSharedPtr<FJsonValue> J = ReadJson(FPaths::Combine(Course->Data->ExportDir, TEXT("boulders.json")));
		if (J.IsValid())
			for (const TSharedPtr<FJsonValue>& V : J->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& B = V->AsArray();
				if (B.Num() < 7) continue;
				FOTGCourseTrees::FRockRec R;
				R.P = FVector(B[0]->AsNumber(), B[1]->AsNumber(), B[2]->AsNumber());
				R.Size = B[3]->AsNumber(); R.Yaw = B[4]->AsNumber(); R.Flat = B[5]->AsNumber(); R.Kind = int32(B[6]->AsNumber()) % BoulderKinds;
				RockRecs.Add(R);
			}
	}
	for (UHierarchicalInstancedStaticMeshComponent* C : RockComps) if (C) { Course->Parts.Remove(C); C->DestroyComponent(); }
	RockComps.Reset();
	RockCount = 0;
	TArray<UStaticMesh*> Meshes;
	for (int32 k = 0; k < BoulderKinds; k++) Meshes.Add(Find<UStaticMesh>(FString::Printf(TEXT("SM_Boulder_%d"), k)));
	const FOTGTreeDesign& Design = Course->Data->TreeDesign;
	// (KiteDemo's rocks, where the pack is in the game: a look none of whose meshes are here is the game's stones)
	TMap<FString, TArray<UStaticMesh*>> KiteRocks;
	{
		TSet<FString> Used = { Design.RockModel };
		for (const FOTGRockEdit& E : Design.Rocks) Used.Add(E.Model);
		for (const FString& Model : Used)
			for (const TCHAR* Path : otgkite::RockMeshes(Model))
				if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path)) KiteRocks.FindOrAdd(Model).Add(M);
	}
	TMap<UStaticMesh*, TArray<FTransform>> ByMesh;
	DrawnRocks.Reset();
	auto Put = [&](const FOTGCourseTrees::FRockRec& R, double Size, bool bAdded, const FString& Model) {
		const double S = R.Size / 100.0 * Size;
		// (on the ground as it's drawn - the lowest under its middle and round it: boulders.json's height
		// is the export's, and in a stream bed dug out for its water a stone stood clear of the bed)
		FVector P = R.P;
		{
			const double Rr = R.Size * Size * 0.3;
			double G = Course->Ground.GroundAt(P.X, P.Y);
			for (int32 k = 0; k < 6; k++)
			{
				const double A = k * UE_DOUBLE_PI / 3.0;
				G = FMath::Min(G, Course->Ground.GroundAt(P.X + FMath::Cos(A) * Rr, P.Y + FMath::Sin(A) * Rr));
			}
			P.Z = G;
		}
		// (a bigger stone sits a little deeper)
		const FVector At = Course->Data->LevelOrigin + P - FVector(0, 0, (Size - 1) * R.Size * 0.15);
		const TArray<UStaticMesh*>* Kite = Model.IsEmpty() ? nullptr : KiteRocks.Find(Model);
		if (Kite && Kite->Num())
		{
			// (a KiteDemo rock: scaled to the size across, its own shape - sat on the ground a fifth of its height deep)
			UStaticMesh* M = (*Kite)[int32(FOTGTreeDesign::Hash01(R.P.X, R.P.Y, 17) * Kite->Num()) % Kite->Num()];
			const FBox B = M->GetBoundingBox();
			const double K = R.Size * Size / FMath::Max(1.0, FMath::Max(B.GetSize().X, B.GetSize().Y));
			ByMesh.FindOrAdd(M).Add(FTransform(FRotator(0, R.Yaw, 0), At - FVector(0, 0, (B.Min.Z + 0.2 * B.GetSize().Z) * K), FVector(K)));
		}
		else if (Meshes[R.Kind]) ByMesh.FindOrAdd(Meshes[R.Kind]).Add(FTransform(FRotator(0, R.Yaw, 0), At, FVector(S, S, S * R.Flat)));
		FOTGCourseTrees::FDrawn& D = DrawnRocks.AddDefaulted_GetRef();
		D.Base = At; D.R = FMath::Max(60.0, R.Size * Size * 0.5); D.H = R.Size * Size; D.X = R.P.X / 100.0; D.Z = R.P.Y / 100.0; D.bAdded = bAdded;
	};
	for (const FOTGCourseTrees::FRockRec& R : RockRecs)
	{
		if (!Design.KeepRock(R.P.X / 100.0, R.P.Y / 100.0)) continue;
		const FOTGRockEdit* E = Design.Rocks.Num() ? Design.RockEditAt(R.P.X / 100.0, R.P.Y / 100.0) : nullptr;
		Put(R, Design.RockSize * (E ? E->Size : 1.f), false, E && !E->Model.IsEmpty() ? E->Model : Design.RockModel);
	}
	// (and the rocks added in the designer, on the ground where they were put: a stone of the game's
	// six picked by where it is, its size across)
	for (const FOTGRockEdit& E : Design.Rocks)
	{
		if (!E.bAdded || E.bRemoved) continue;
		FOTGCourseTrees::FRockRec R;
		const double CX = Course->Data->HeightX0 + E.X, CZ = Course->Data->HeightZ1 - E.Z;
		R.P = FVector(E.X * 100.0, E.Z * 100.0, (Course->Data->GroundAt(CX, CZ) - Course->Data->HeightBase) * 100.0);
		R.Size = 100.0;
		R.Yaw = FOTGTreeDesign::Hash01(E.X, E.Z, 7) * 360;
		R.Flat = 0.6 + 0.3 * FOTGTreeDesign::Hash01(E.X, E.Z, 11);
		R.Kind = int32(FOTGTreeDesign::Hash01(E.X, E.Z, 13) * BoulderKinds) % BoulderKinds;
		Put(R, E.Size, true, E.Model.IsEmpty() ? Design.RockModel : E.Model);
	}
	for (auto& P : ByMesh) { RockComps.Add(Course->Instances(P.Key, P.Value, BoulderCull[0], BoulderCull[1])); RockCount += P.Value.Num(); }
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Boulders: %d"), RockCount);
}

const FOTGTreeDesign& FOTGCourseTrees::TreeDesign() const
{
	static const FOTGTreeDesign None;
	return Course->Data ? Course->Data->TreeDesign : None;
}

void FOTGCourseTrees::SetTreeDesign(const FOTGTreeDesign& Design)
{
	if (!Course->Data) return;
	Course->Data->TreeDesign = Design;
	BuildTrees();
	BuildBoulders();
}

bool FOTGCourseTrees::SaveTreeDesign(FString* Where) const
{
	if (!Course->Data) return false;
	if (Where) *Where = FPaths::Combine(Course->Data->ExportDir, TEXT("trees_design.json"));
	return Course->Data->TreeDesign.Save(Course->Data->ExportDir);
}

FOTGTreeDesign FOTGCourseTrees::SavedTreeDesign() const
{
	return Course->Data ? FOTGTreeDesign::Load(Course->Data->ExportDir) : FOTGTreeDesign();
}

TArray<TPair<FString, int32>> FOTGCourseTrees::TreeSpecies() const
{
	TMap<FString, int32> N;
	if (Course->Data) for (const FOTGTreeShape& S : Course->Data->Trees) N.FindOrAdd(S.Kind)++;
	TArray<TPair<FString, int32>> Out;
	for (const auto& KV : N) Out.Add(TPair<FString, int32>(KV.Key, KV.Value));
	Out.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	return Out;
}

bool FOTGCourseTrees::NearestTree(const FString& Kind, const FVector& From, FVector& OutBase, double& OutHeightCm) const
{
	double Best = TNumericLimits<double>::Max();
	if (Kind == TEXT("rock"))
	{
		// (the rocks: the nearest as drawn)
		for (const FOTGCourseTrees::FDrawn& D : DrawnRocks)
		{
			const double Dist = FVector::DistSquared2D(D.Base, From);
			if (Dist < Best && Dist > 1000.0 * 1000.0) { Best = Dist; OutBase = D.Base; OutHeightCm = D.H; }
		}
		return Best < TNumericLimits<double>::Max();
	}
	if (!Course->Data || TreeRecs.Num() != Course->Data->Trees.Num()) return false;
	Course->Data->ForEachDesignedTree([&](int32 Src, double X, double Z, double Ground, float Scale, const FOTGTreeEdit* E) {
		const FString& K = E && !E->Kind.IsEmpty() ? E->Kind : Course->Data->Trees[Src].Kind;
		if (K != Kind) return;
		const FVector P = Course->Data->CourseToWorld(FVector(X, Ground, Z));
		const double D = FVector::DistSquared2D(P, From);
		// (not right on top of you: one you can see)
		if (D < Best && D > 2500.0 * 2500.0) { Best = D; OutBase = P; OutHeightCm = Src >= 0 ? TreeRecs[Src].H * Scale : E->Height * E->Size * 100.0; }
	});
	return Best < TNumericLimits<double>::Max();
}

bool FOTGCourseTrees::Pick(const FVector& Origin, const FVector& Dir, double MaxT, FOTGPicked& Out) const
{
	const FVector D = Dir.GetSafeNormal();
	double Best = TNumericLimits<double>::Max();
	const FOTGCourseTrees::FDrawn* Hit = nullptr;
	bool bRock = false;
	// a tree: the line passing within its crown's reach of its trunk's line, foot to top
	for (const FOTGCourseTrees::FDrawn& T : DrawnTrees)
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
	for (const FOTGCourseTrees::FDrawn& R : DrawnRocks)
	{
		const FVector C = R.Base + FVector(0, 0, R.R * 0.4);
		const double At = FVector::DotProduct(C - Origin, D);
		if (At <= 0 || At > MaxT + R.R || At >= Best) continue;
		if (FVector::DistSquared(Origin + D * At, C) > R.R * R.R * 1.2) continue;
		Best = At; Hit = &R; bRock = true;
	}
	if (!Hit) return false;
	Out.bRock = bRock; Out.X = Hit->X; Out.Z = Hit->Z; Out.Kind = Hit->Kind; Out.HeightM = Hit->H / 100.0; Out.bAdded = Hit->bAdded; Out.Base = Hit->Base; Out.bColourable = Hit->bColourable; Out.RadiusM = Hit->R / 100.0;
	return true;
}

void FOTGCourseTrees::MarkPicked(const FOTGPicked& P)
{
	// (made the first time: a ring of gold beads, and a cone pointing down at it from above)
	if (!PickRing)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Sphere || !Cone || !Base) return;
		UMaterialInstanceDynamic* Gold = UMaterialInstanceDynamic::Create(Base, Course);
		Gold->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.72f, 0.08f));
		PickRing = NewObject<UInstancedStaticMeshComponent>(Course);
		PickRing->SetStaticMesh(Sphere);
		PickCone = NewObject<UStaticMeshComponent>(Course);
		PickCone->SetStaticMesh(Cone);
		for (UStaticMeshComponent* C : { static_cast<UStaticMeshComponent*>(PickRing), PickCone.Get() })
		{
			C->SetMaterial(0, Gold);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->SetMobility(EComponentMobility::Movable);
			C->SetupAttachment(Course->RootComponent);
			C->RegisterComponent();
		}
	}
	// (the cone: over its top)
	const FVector Top = P.Base + FVector(0, 0, (P.bRock ? P.RadiusM * 100.0 : P.HeightM * 100.0) + 250.0);
	const bool bMoved = PickMarkedAt < 0 || !PickRing->IsVisible() || !PickConeAt.Equals(Top, 1.0);
	PickMarkedAt = FPlatformTime::Seconds();
	if (!bMoved) return;
	PickConeAt = Top;
	// the ring: round the crown's edge (a rock's), on the ground
	const double R = FMath::Max(P.bRock ? 120.0 : 220.0, P.RadiusM * 100.0 * (P.bRock ? 1.3 : 0.9));
	const int32 N = 32;
	PickRing->ClearInstances();
	for (int32 i = 0; i < N; i++)
	{
		const double A = 2 * PI * i / N;
		PickRing->AddInstance(FTransform(FRotator::ZeroRotator, P.Base + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 25), FVector(0.32)), true);
	}
	PickRing->SetVisibility(true);
	PickCone->SetVisibility(true);
}

void FOTGCourseTrees::MarkForests(const TArray<int32>& Which)
{
	const FOTGTreeDesign& D = TreeDesign();
	FString Key;
	for (int32 i : Which)
		if (D.Forests.IsValidIndex(i))
		{
			Key += FString::Printf(TEXT("%d:%d;"), i, D.Forests[i].Pts.Num());
			for (const FVector2D& P : D.Forests[i].Pts) Key += FString::Printf(TEXT("%.1f,%.1f "), P.X, P.Y);
		}
	if (Key.IsEmpty()) return;
	// (made the first time: beads, pale green, unlit by shadows)
	if (!ForestBeads)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Sphere || !Base) return;
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Course);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 1.f, 0.35f));
		ForestBeads = NewObject<UInstancedStaticMeshComponent>(Course);
		ForestBeads->SetStaticMesh(Sphere);
		ForestBeads->SetMaterial(0, M);
		ForestBeads->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ForestBeads->SetCastShadow(false);
		ForestBeads->SetMobility(EComponentMobility::Movable);
		ForestBeads->SetupAttachment(Course->RootComponent);
		ForestBeads->RegisterComponent();
	}
	ForestMarkedAt = FPlatformTime::Seconds();
	if (Key == ForestMarkKey && ForestBeads->IsVisible()) return;
	ForestMarkKey = Key;
	// (a bead every 4 m round each one's edge, 1 m over the ground: course metres to the level's cm as the trees are placed)
	ForestBeads->ClearInstances();
	const UOTGCourseData* Data = Course->Data;
	TArray<FTransform> Xfs;
	for (int32 i : Which)
	{
		if (!D.Forests.IsValidIndex(i)) continue;
		const TArray<FVector2D>& Pts = D.Forests[i].Pts;
		for (int32 k = 0; k < Pts.Num(); k++)
		{
			const FVector2D A = Pts[k], B = Pts[(k + 1) % Pts.Num()];
			const int32 N = FMath::Max(1, FMath::CeilToInt32(FVector2D::Distance(A, B) / 4.0));
			for (int32 j = 0; j < N; j++)
			{
				const FVector2D P = FMath::Lerp(A, B, double(j) / N);
				const double G = Data->GroundAt(P.X, P.Y);
				Xfs.Add(FTransform(FRotator::ZeroRotator, FVector((P.X - Data->HeightX0) * 100.0, (Data->HeightZ1 - P.Y) * 100.0, (G - Data->HeightBase) * 100.0 + 100.0), FVector(0.6)));
			}
		}
	}
	ForestBeads->AddInstances(Xfs, false, true);
	UE_LOG(LogTemp, Display, TEXT("Forests outlined: %d, %d beads"), Which.Num(), Xfs.Num());
	ForestBeads->SetVisibility(true);
}

bool FOTGCourseTrees::Picked(bool bRock, double X, double Z, FOTGPicked& Out) const
{
	for (const FOTGCourseTrees::FDrawn& T : bRock ? DrawnRocks : DrawnTrees)
		if (FMath::Abs(T.X - X) < 0.15 && FMath::Abs(T.Z - Z) < 0.15)
		{
			Out.bRock = bRock; Out.X = T.X; Out.Z = T.Z; Out.Kind = T.Kind; Out.HeightM = T.H / 100.0; Out.bAdded = T.bAdded; Out.Base = T.Base; Out.bColourable = T.bColourable; Out.RadiusM = T.R / 100.0;
			return true;
		}
	return false;
}

// (each frame: the picked one's marks gone once the designer stops calling; the cone bobbing)
void FOTGCourseTrees::UpdateMarks()
{
	if (ForestBeads && ForestBeads->IsVisible() && FPlatformTime::Seconds() - ForestMarkedAt > 0.3) ForestBeads->SetVisibility(false);
	if (PickRing && PickRing->IsVisible())
	{
		if (FPlatformTime::Seconds() - PickMarkedAt > 0.3) { PickRing->SetVisibility(false); PickCone->SetVisibility(false); }
		else
		{
			const double T = FPlatformTime::Seconds();
			// (bigger the farther the camera: findable from the tee)
			double S = 1.3;
			if (const APlayerController* PC = Course->GetWorld() ? Course->GetWorld()->GetFirstPlayerController() : nullptr)
				if (PC->PlayerCameraManager) S = FMath::Clamp(FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), PickConeAt) / 2500.0, 1.3, 14.0);
			PickCone->SetWorldLocationAndRotation(PickConeAt + FVector(0, 0, (60 * FMath::Sin(T * 3.0) + 60 + 40) * S), FRotator(180, T * 90.0, 0));
			PickCone->SetWorldScale3D(FVector(S, S, S * 1.3));
		}
	}
}

void FOTGCourseTrees::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PickRing);
	Collector.AddReferencedObject(PickCone);
	Collector.AddReferencedObject(ForestBeads);
	Collector.AddReferencedObjects(TreeComps);
	Collector.AddReferencedObjects(RockComps);
}
