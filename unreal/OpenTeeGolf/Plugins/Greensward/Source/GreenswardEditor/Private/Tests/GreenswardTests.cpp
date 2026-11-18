// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "MeshDescription.h"
#include "Engine/World.h"
#include "GreenswardEditorLibrary.h"
#include "GreenswardLawn.h"
#include "GreenswardPitchMarking.h"
#include "GreenswardWindController.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

namespace GreenswardTests
{
	static const EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A throwaway editor world that cleans up after itself. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("GreenswardTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
			Context.SetCurrentWorld(World);
		}

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	static UStaticMesh* StockMat(const TCHAR* Name = TEXT("SM_GS_Lawn_04cm_Dense_Dry00"))
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Greensward/Mats/%s.%s"), Name, Name));
	}

	static AGreenswardLawn* SpawnLawn(UWorld* World, const FVector& Location, float SizeX, float SizeY, bool bWithMat)
	{
		AGreenswardLawn* Lawn = World->SpawnActor<AGreenswardLawn>(Location, FRotator::ZeroRotator);
		Lawn->bAutoRebuild = false;
		Lawn->bSnapToGround = false;
		Lawn->EdgeRaggedness = 0.0f;
		Lawn->SetRectangle(SizeX, SizeY);
		if (bWithMat)
		{
			FGreenswardMatEntry Entry;
			Entry.Mesh = StockMat();
			Lawn->Mats.Add(Entry);
		}
		return Lawn;
	}

	static void SetCircle(AGreenswardLawn* Lawn, float Radius)
	{
		USplineComponent* Spline = Lawn->Boundary;
		Spline->ClearSplinePoints(false);
		const int32 Points = 16;
		for (int32 I = 0; I < Points; ++I)
		{
			const float Angle = 2.0f * PI * I / Points;
			Spline->AddSplinePoint(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f), ESplineCoordinateSpace::Local, false);
		}
		Spline->SetClosedLoop(true, false);
		Spline->UpdateSpline();
	}

	static int32 CountAssets(const TCHAR* Path)
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByPath(FName(Path), Assets, false);
		return Assets.Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardStockAssetsTest, "Greensward.Assets.StockLibrary", GreenswardTests::Flags)
bool FGreenswardStockAssetsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Stock mats"), GreenswardTests::CountAssets(TEXT("/Greensward/Mats")), 130);
	TestEqual(TEXT("PCG graphs"), GreenswardTests::CountAssets(TEXT("/Greensward/PCG")), 9);
	TestEqual(TEXT("Landscape layer infos"), GreenswardTests::CountAssets(TEXT("/Greensward/Landscape")), 7);

	const TCHAR* Required[] = {
		TEXT("/Greensward/Materials/M_Greensward_Grass.M_Greensward_Grass"),
		TEXT("/Greensward/Materials/MI_Greensward_Grass_Wind.MI_Greensward_Grass_Wind"),
		TEXT("/Greensward/Materials/MI_Greensward_Grass_Static.MI_Greensward_Grass_Static"),
		TEXT("/Greensward/Materials/MI_Greensward_Grass_Striped.MI_Greensward_Grass_Striped"),
		TEXT("/Greensward/Materials/MI_Greensward_Grass_Checker.MI_Greensward_Grass_Checker"),
		TEXT("/Greensward/Materials/MI_Greensward_Landscape.MI_Greensward_Landscape"),
		TEXT("/Greensward/Materials/MPC_Greensward.MPC_Greensward"),
		TEXT("/Greensward/Markings/M_Greensward_Marking.M_Greensward_Marking"),
	};
	for (const TCHAR* Path : Required)
	{
		TestNotNull(Path, LoadObject<UObject>(nullptr, Path));
	}

	// Every stock mat is a Nanite mesh with a material and sensible bounds.
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Mats;
	Registry.Get().GetAssetsByPath(FName(TEXT("/Greensward/Mats")), Mats, false);
	for (const FAssetData& Data : Mats)
	{
		const UStaticMesh* Mesh = Cast<UStaticMesh>(Data.GetAsset());
		if (!TestNotNull(*Data.AssetName.ToString(), Mesh))
		{
			continue;
		}
		const FString Name = Mesh->GetName();
		TestTrue(*(Name + TEXT(" has Nanite enabled")), Mesh->GetNaniteSettings().bEnabled);
		TestTrue(*(Name + TEXT(" has a material")), Mesh->GetStaticMaterials().Num() == 1 && Mesh->GetStaticMaterials()[0].MaterialInterface != nullptr);
		const FVector Extent = Mesh->GetBounds().BoxExtent;
		const bool bGreen = Name.Contains(TEXT("_Green_"));
		const float Half = bGreen ? 12.5f : 25.0f;
		TestTrue(*(Name + TEXT(" footprint")), Extent.X > Half * 0.95f && Extent.X < Half * 4.0f && Extent.Y > Half * 0.95f && Extent.Y < Half * 4.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardMatGeneratorTest, "Greensward.Mats.Generate", GreenswardTests::Flags)
bool FGreenswardMatGeneratorTest::RunTest(const FString& Parameters)
{
	const FString Folder = TEXT("/Game/GreenswardTests");
	FGreenswardMatSettings Settings;
	Settings.BladeCount = 300;
	Settings.Height = 6.0f;
	Settings.bMown = false;
	Settings.Seed = 42;

	UStaticMesh* A = UGreenswardEditorLibrary::GenerateGrassMat(Settings, Folder, TEXT("SM_Test_A"), nullptr);
	UStaticMesh* B = UGreenswardEditorLibrary::GenerateGrassMat(Settings, Folder, TEXT("SM_Test_B"), nullptr);
	Settings.StalkCount = 20;
	UStaticMesh* C = UGreenswardEditorLibrary::GenerateGrassMat(Settings, Folder, TEXT("SM_Test_C"), nullptr);

	if (TestNotNull(TEXT("Mat A"), A) && TestNotNull(TEXT("Mat B"), B) && TestNotNull(TEXT("Mat C"), C))
	{
		const int32 TrianglesA = A->GetMeshDescription(0)->Triangles().Num();
		// 300 pointed blades of 2 segments: 3 triangles each, plus the turf sheet (2) and its skirt (8).
		TestEqual(TEXT("Triangle count"), TrianglesA, 300 * 3 + 10);
		TestEqual(TEXT("Same seed, same mesh"), B->GetMeshDescription(0)->Triangles().Num(), TrianglesA);
		TestTrue(TEXT("Stalks add geometry"), C->GetMeshDescription(0)->Triangles().Num() > TrianglesA);
		TestTrue(TEXT("Render data was built"), A->GetNumTriangles(0) > 0);
		TestTrue(TEXT("Nanite enabled"), A->GetNaniteSettings().bEnabled);
		TestTrue(TEXT("Has a material"), A->GetStaticMaterials().Num() == 1 && A->GetStaticMaterials()[0].MaterialInterface != nullptr);
		const FBox Box = A->GetBoundingBox();
		// Bounds include 2 cm of headroom for wind sway on top of the 6 cm blades.
		TestTrue(TEXT("Stands about as tall as requested"), Box.Max.Z > 5.0f && Box.Max.Z <= 8.1f);
		TestTrue(TEXT("Skirt reaches below ground"), Box.Min.Z < -3.0f);
	}

	// Remove the files the generator saved.
	for (const TCHAR* Name : { TEXT("SM_Test_A"), TEXT("SM_Test_B"), TEXT("SM_Test_C") })
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Folder / Name, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().Delete(*Filename, false, true, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLawnFillTest, "Greensward.Lawn.Fill", GreenswardTests::Flags)
bool FGreenswardLawnFillTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	if (!TestNotNull(TEXT("Stock mat"), GreenswardTests::StockMat()))
	{
		return false;
	}

	AGreenswardLawn* Lawn = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 1000.0f, 1000.0f, true);
	Lawn->EdgeSubdivisions = 0;
	Lawn->Rebuild();
	TestEqual(TEXT("10 m square at 50 cm"), Lawn->GetInstanceCount(), 400);

	Lawn->EdgeSubdivisions = 2;
	Lawn->Rebuild();
	TestEqual(TEXT("A grid-aligned square needs no edge subdivision"), Lawn->GetInstanceCount(), 400);

	AddExpectedMessage(TEXT("reached MaxInstances"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Lawn->MaxInstances = 50;
	Lawn->Rebuild();
	TestEqual(TEXT("MaxInstances is respected"), Lawn->GetInstanceCount(), 50);

	Lawn->Clear();
	TestEqual(TEXT("Clear empties the lawn"), Lawn->GetInstanceCount(), 0);

	TestTrue(TEXT("Centre is inside"), Lawn->IsPointInside(FVector(0, 0, 500)));
	TestFalse(TEXT("Far point is outside"), Lawn->IsPointInside(FVector(900, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLawnCutOutTest, "Greensward.Lawn.CutOut", GreenswardTests::Flags)
bool FGreenswardLawnCutOutTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	AGreenswardLawn* Hole = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 500.0f, 500.0f, false);
	AGreenswardLawn* Lawn = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 1000.0f, 1000.0f, true);
	Lawn->EdgeSubdivisions = 0;
	Lawn->CutOutLawns.Add(Hole);
	Lawn->Rebuild();
	TestEqual(TEXT("A 5 m hole removes 100 of 400 mats"), Lawn->GetInstanceCount(), 300);
	TestEqual(TEXT("A lawn with no mats places nothing"), Hole->GetInstanceCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLawnEdgeTest, "Greensward.Lawn.Edges", GreenswardTests::Flags)
bool FGreenswardLawnEdgeTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	AGreenswardLawn* Lawn = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 1000.0f, 1000.0f, true);
	GreenswardTests::SetCircle(Lawn, 500.0f);

	Lawn->EdgeSubdivisions = 0;
	Lawn->Rebuild();
	const int32 Coarse = Lawn->GetInstanceCount();

	Lawn->EdgeSubdivisions = 2;
	Lawn->Rebuild();
	const int32 Fine = Lawn->GetInstanceCount();

	// Area of a 5 m circle is 78.5 m2, or about 314 mats.
	TestTrue(TEXT("Coarse fill is close to the circle's area"), Coarse > 280 && Coarse < 350);
	TestTrue(TEXT("Subdividing the edge adds small mats"), Fine > Coarse);

	// With a crisp edge, no mat centre may lie outside the outline.
	TArray<UInstancedStaticMeshComponent*> Components;
	Lawn->GetComponents(Components);
	int32 Outside = 0;
	int32 Small = 0;
	for (const UInstancedStaticMeshComponent* Component : Components)
	{
		for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
		{
			FTransform Transform;
			Component->GetInstanceTransform(I, Transform, true);
			Outside += Lawn->IsPointInside(Transform.GetLocation()) ? 0 : 1;
			Small += Transform.GetScale3D().X < 0.9f ? 1 : 0;
		}
	}
	TestEqual(TEXT("Mats outside a crisp outline"), Outside, 0);
	TestTrue(TEXT("Edge mats are scaled down"), Small > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLawnAppearanceTest, "Greensward.Lawn.Appearance", GreenswardTests::Flags)
bool FGreenswardLawnAppearanceTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	AGreenswardLawn* Lawn = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 500.0f, 500.0f, true);
	Lawn->StripeMode = EGreenswardStripeMode::Double;
	Lawn->StripeWidth = 150.0f;
	Lawn->StripeAngle = 30.0f;
	Lawn->StripeContrast = 0.4f;
	Lawn->Tint = FLinearColor(0.5f, 1.0f, 0.75f);
	Lawn->Rebuild();

	TArray<UInstancedStaticMeshComponent*> Components;
	Lawn->GetComponents(Components);
	if (!TestTrue(TEXT("Lawn has a mat component"), Components.Num() > 0))
	{
		return false;
	}
	// Saved (default) custom primitive data: this is what survives into a saved level and a play session.
	const TArray<float>& Data = Components[0]->GetDefaultCustomPrimitiveData().Data;
	if (TestTrue(TEXT("Eight values are stored"), Data.Num() >= 8))
	{
		TestEqual(TEXT("Stripe mode"), Data[0], 2.0f);
		TestEqual(TEXT("Stripe width"), Data[1], 150.0f);
		TestEqual(TEXT("Stripe angle"), Data[2], 30.0f);
		TestEqual(TEXT("Stripe contrast"), Data[3], 0.4f);
		TestEqual(TEXT("Tint is stored as an offset from white"), Data[4], -0.5f);
		TestEqual(TEXT("Tint green"), Data[5], 0.0f);
		TestEqual(TEXT("Tint blue"), Data[6], -0.25f);
	}
	TestFalse(TEXT("Mats have no collision"), Components[0]->IsCollisionEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLawnGroundTest, "Greensward.Lawn.SnapToGround", GreenswardTests::Flags)
bool FGreenswardLawnGroundTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;

	// A slab whose top face is at Z = -200.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* Ground = Scope.World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -250), FRotator::ZeroRotator);
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->SetActorScale3D(FVector(30.0, 30.0, 1.0));

	AGreenswardLawn* Lawn = GreenswardTests::SpawnLawn(Scope.World, FVector::ZeroVector, 500.0f, 500.0f, true);
	Lawn->bSnapToGround = true;
	Lawn->SurfaceOffset = 1.0f;
	Lawn->HeightJitter = 0.0f;
	Lawn->Rebuild();
	TestEqual(TEXT("All cells found ground"), Lawn->GetInstanceCount(), 100);

	TArray<UInstancedStaticMeshComponent*> Components;
	Lawn->GetComponents(Components);
	bool bAllOnGround = Components.Num() > 0;
	for (const UInstancedStaticMeshComponent* Component : Components)
	{
		for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
		{
			FTransform Transform;
			Component->GetInstanceTransform(I, Transform, true);
			bAllOnGround &= FMath::IsNearlyEqual(Transform.GetLocation().Z, -199.0, 0.5);
		}
	}
	TestTrue(TEXT("Mats sit on the ground, lifted by the surface offset"), bAllOnGround);

	// Nothing under a lawn far to the side: with Require Ground it stays empty.
	AGreenswardLawn* Floating = GreenswardTests::SpawnLawn(Scope.World, FVector(50000, 0, 0), 500.0f, 500.0f, true);
	Floating->bSnapToGround = true;
	Floating->Rebuild();
	TestEqual(TEXT("No ground, no mats"), Floating->GetInstanceCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardMarkingTest, "Greensward.Markings.Actor", GreenswardTests::Flags)
bool FGreenswardMarkingTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	AGreenswardPitchMarking* Actor = Scope.World->SpawnActor<AGreenswardPitchMarking>(FVector::ZeroVector, FRotator(-90.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("Marking actor"), Actor))
	{
		return false;
	}

	for (uint8 Index = 0; Index <= static_cast<uint8>(EGreenswardMarking::Hockey); ++Index)
	{
		const EGreenswardMarking Marking = static_cast<EGreenswardMarking>(Index);
		const FString Suffix = GreenswardMarkings::GetAssetSuffix(Marking);
		const FVector2D Extent = GreenswardMarkings::GetExtentMetres(Marking);

		Actor->Marking = Marking;
		Actor->Scale = 0.5f;
		Actor->ProjectionDepth = 200.0f;
		Actor->Refresh();

		const UDecalComponent* Decal = Actor->GetDecal();
		TestTrue(*(Suffix + TEXT(" extent")), Extent.X > 20.0 && Extent.Y > 10.0);
		TestNotNull(*(Suffix + TEXT(" mask texture")), LoadObject<UObject>(nullptr, *FString::Printf(TEXT("/Greensward/Markings/T_GS_Marking_%s.T_GS_Marking_%s"), *Suffix, *Suffix)));
		if (TestNotNull(*(Suffix + TEXT(" material")), Decal->GetDecalMaterial()))
		{
			TestTrue(*(Suffix + TEXT(" uses its own instance")), Decal->GetDecalMaterial()->GetName().EndsWith(Suffix));
		}
		TestTrue(*(Suffix + TEXT(" size")), Decal->DecalSize.Equals(FVector(200.0, Extent.Y * 25.0, Extent.X * 25.0), 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardWindTest, "Greensward.Weather.Controller", GreenswardTests::Flags)
bool FGreenswardWindTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Greensward/Materials/MPC_Greensward.MPC_Greensward"));
	if (!TestNotNull(TEXT("Parameter collection"), Collection))
	{
		return false;
	}

	AGreenswardWindController* Controller = Scope.World->SpawnActor<AGreenswardWindController>(FVector::ZeroVector, FRotator(0.0, 90.0, 0.0));
	Controller->WindStrength = 0.7f;
	Controller->WindSpeed = 2.0f;
	Controller->Season = -0.5f;
	Controller->Dryness = 0.25f;
	Controller->Snow = 0.6f;
	Controller->Wetness = 0.9f;
	Controller->Apply();

	UMaterialParameterCollectionInstance* Instance = Scope.World->GetParameterCollectionInstance(Collection);
	FLinearColor Wind, Environment;
	TestTrue(TEXT("Wind parameter exists"), Instance->GetVectorParameterValue(TEXT("Wind"), Wind));
	TestTrue(TEXT("Environment parameter exists"), Instance->GetVectorParameterValue(TEXT("Environment"), Environment));
	// Yaw 90: the wind blows along +Y.
	TestTrue(TEXT("Wind direction follows the actor"), FMath::IsNearlyEqual(Wind.R, 0.0f, 0.001f) && FMath::IsNearlyEqual(Wind.G, 1.0f, 0.001f));
	TestEqual(TEXT("Wind strength"), Wind.B, 0.7f);
	TestEqual(TEXT("Wind speed"), Wind.A, 2.0f);
	TestEqual(TEXT("Season"), Environment.R, -0.5f);
	TestEqual(TEXT("Dryness"), Environment.G, 0.25f);
	TestEqual(TEXT("Snow"), Environment.B, 0.6f);
	TestEqual(TEXT("Wetness"), Environment.A, 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreenswardLandscapeTest, "Greensward.Landscape.Create", GreenswardTests::Flags)
bool FGreenswardLandscapeTest::RunTest(const FString& Parameters)
{
	GreenswardTests::FScopedWorld Scope;

	TArray<FGreenswardLandscapePatch> Patches;
	FGreenswardLandscapePatch& Bunker = Patches.AddDefaulted_GetRef();
	Bunker.Layer = TEXT("Sand");
	Bunker.Centre = FVector2D(0.0, 0.0);
	Bunker.HalfSize = FVector2D(400.0, 400.0);
	Bunker.bLevel = true;
	Bunker.HeightOffset = -100.0f;

	ALandscape* Landscape = UGreenswardEditorLibrary::CreateLandscape(Scope.World, nullptr, Patches, 2, 50.0f, 0.0f, 0.0f, 3);
	if (!TestNotNull(TEXT("Landscape"), Landscape))
	{
		return false;
	}
	TestEqual(TEXT("2 x 2 components"), Landscape->LandscapeComponents.Num(), 4);
	TestNotNull(TEXT("Landscape material"), Landscape->GetLandscapeMaterial());

	// Flat terrain (hill height 0) with a 1 m deep sunken patch in the middle.
	TOptional<float> Centre = Landscape->GetHeightAtLocation(FVector(0, 0, 0));
	TOptional<float> Corner = Landscape->GetHeightAtLocation(FVector(2500, 2500, 0));
	if (TestTrue(TEXT("Heights can be read"), Centre.IsSet() && Corner.IsSet()))
	{
		TestTrue(TEXT("Flat away from the patch"), FMath::IsNearlyEqual(Corner.GetValue(), 0.0f, 2.0f));
		TestTrue(TEXT("Patch is sunk by its height offset"), FMath::IsNearlyEqual(Centre.GetValue(), -100.0f, 3.0f));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
