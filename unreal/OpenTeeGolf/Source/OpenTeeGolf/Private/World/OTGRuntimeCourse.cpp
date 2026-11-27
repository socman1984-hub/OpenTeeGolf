// See OTGRuntimeCourse.h: building the course in order (all at once or a step a frame), its pictures,
// and the frame's work. Each part of the course is in a file of its own: OTGCourseGround.cpp,
// OTGCourseTrees.cpp, OTGCourseWater.cpp (and OTGWater.cpp), OTGCourseFarLand.cpp, OTGCourseProps.cpp,
// OTGCourseGrass.cpp.

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

AOTGRuntimeCourse::AOTGRuntimeCourse()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Ground.Course = Water.Course = Trees.Course = FarLand.Course = Props.Course = Turf.Course = this;
}

void AOTGRuntimeCourse::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	AOTGRuntimeCourse* C = CastChecked<AOTGRuntimeCourse>(InThis);
	C->Ground.AddReferencedObjects(Collector);
	C->Water.AddReferencedObjects(Collector);
	C->Trees.AddReferencedObjects(Collector);
	C->Turf.AddReferencedObjects(Collector);
}

void AOTGRuntimeCourse::Build(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress)
{
	Data = D;
	if (!D) return;
	const double T0 = FPlatformTime::Seconds();
	LandCm = (D->HeightSamples - 1) * D->MetresPerSample * 100.0;
	if (Progress) Progress(0.02f, TEXT("The course's pictures"));
	LoadPictures();
	Ground.BuildGround(Progress);
	if (Progress) Progress(0.6f, TEXT("Trees"));
	Trees.BuildTrees();
	if (Progress) Progress(0.75f, TEXT("Water, paths and buildings"));
	Trees.BuildBoulders();
	Water.BuildWater();
	Water.BuildSea();
	FarLand.BuildFarLand();
	Props.BuildHouses();   // (before the props: the export's blocks under Hearthline's houses left out there)
	Props.BuildProps();
	BuildBridges();
	Turf.BuildGrass();
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("%s built in %.1f s"), *D->CourseName, FPlatformTime::Seconds() - T0);
}

void AOTGRuntimeCourse::BuildInSteps(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress, TFunction<void()> Done)
{
	Data = D;
	if (!D) { if (Done) Done(); return; }
	LandCm = (D->HeightSamples - 1) * D->MetresPerSample * 100.0;
	StepProgress = Progress;
	StepsDone = Done;
	Steps.Add({ TEXT("The course's pictures"), [this]() { LoadPictures(); Ground.PrepareGround(); } });
	// (the ground's tiles: added as steps once its size is known - PrepareGround - so the bar is right)
	const int32 N = D->HeightSamples;
	const int32 Quads = N > 2049 ? 128 : 64, Count = FMath::DivideAndRoundUp(N - 1, Quads), Total = Count * Count;
	const int32 Batch = Quads > 64 ? 6 : 24;
	for (int32 From = 0; From < Total; From += Batch)
		Steps.Add({ TEXT("The ground"), [this, From, Batch, Total]() { Ground.BuildGroundTiles(From, FMath::Min(From + Batch, Total)); } });
	Steps.Add({ TEXT("Trees"), [this]() { Trees.BuildTrees(); } });
	Steps.Add({ TEXT("Rocks and water"), [this]() { Trees.BuildBoulders(); Water.BuildWater(); Water.BuildSea(); } });
	Steps.Add({ TEXT("The land round it"), [this]() { FarLand.BuildFarLand(); } });
	Steps.Add({ TEXT("Houses"), [this]() { Props.BuildHouses(); } });   // (OTGHouses.cpp: before the props - the export's blocks under them left out there)
	Steps.Add({ TEXT("Paths and buildings"), [this]() { Props.BuildProps(); BuildBridges(); } });
	Steps.Add({ TEXT("Grass"), [this]() { Turf.BuildGrass(); } });
	StepsTotal = Steps.Num();
	if (Progress) Progress(0.f, Steps[0].Key);
}

// The wind's parameter collections, in this level before any grass or tree material that reads them
// is drawn. A level sets up the collections loaded when it opens; one loaded after (in a packaged
// game, the first time the round set the wind) has no place in it, and the first material reading
// it brings the game down ("Failed to find parameter collection buffer" - M_Turf_rough, the tall
// grass). In the editor they're loaded already, so it never showed there.
void AOTGRuntimeCourse::EnsureWindCollections(UWorld* W)
{
	if (!W) return;
	for (const TCHAR* Path : { TEXT("/Game/OpenTeeGolf/Game/MPC_Wind.MPC_Wind"), TEXT("/Game/Fab/MaterialParameterCollection/QMPC_GlobalFoliageActor.QMPC_GlobalFoliageActor") })
		if (UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, Path))
			W->GetParameterCollectionInstance(C);   // (made, and added to the scene, if it isn't there)
}

void AOTGRuntimeCourse::LoadPictures()
{
	EnsureWindCollections(GetWorld());
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

UHierarchicalInstancedStaticMeshComponent* AOTGRuntimeCourse::Instances(UStaticMesh* Mesh, const TArray<FTransform>& Xfs, int32 CullFrom, int32 CullTo, int32 CustomFloats)
{
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	// (each instance's own data, all 0, before there are any: set afterwards, a packaged game drew some
	// trees' leaves with others' numbers - red and blue)
	if (CustomFloats > 0) C->SetNumCustomDataFloats(CustomFloats);
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

void AOTGRuntimeCourse::Tick(float Dt)
{
	Super::Tick(Dt);
	if ((Water.Lakes.Num() || Water.Rivers.Num() || Water.Sea) && FPlatformTime::Seconds() > Water.TidewrightCheckAt) { Water.TidewrightCheckAt = FPlatformTime::Seconds() + 0.5; Water.UpdateTidewright(); }
	// (the picked one's marks: gone once the designer stops calling; the cone bobbing)
	Trees.UpdateMarks();
	UpdateMarkPoints();
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
	if (!Turf.Grass.IsValid()) return;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager) Turf.Grass->Update(PC->PlayerCameraManager->GetCameraLocation());
}

void AOTGRuntimeCourse::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Turf.Grass.IsValid()) Turf.Grass->Wait();
	Super::EndPlay(Reason);
}

FVector2D AOTGRuntimeCourse::CourseXZ(const FVector& World) const
{
	if (!Data) return FVector2D::ZeroVector;
	const FVector C = Data->WorldToCourse(World);
	return FVector2D(C.X, C.Z);
}

FVector2D AOTGRuntimeCourse::RockXY(const FVector& World) const
{
	if (!Data) return FVector2D::ZeroVector;
	const FVector L = World - Data->LevelOrigin;
	return FVector2D(L.X / 100.0, L.Y / 100.0);
}

TArray<otg::FGrassKind> AOTGRuntimeCourse::SavedGrassKinds() const { return Turf.SavedGrassKinds(); }
