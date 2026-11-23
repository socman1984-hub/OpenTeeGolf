// See OTGMiniGolf.h: the actor, its materials and shapes, and the moving windmill. The first six
// holes are the web version's (public/js/practice.js puttPutt), laid out the same; holes 7 to 9 are
// new. (The play data: OTGMiniGolfPlayData.cpp; the lanes and features: OTGMiniGolfBuild.cpp.)

#include "Round/OTGMiniGolf.h"

#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "Physics/OTGBallPhysics.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGMiniGolf, Log, All);

#include "Round/OTGMiniGolfLayout.h"

const TCHAR* AOTGMiniGolf::CourseId = TEXT("minigolf");

namespace
{
	FAutoConsoleCommandWithWorldAndArgs MiniGolfCmd(TEXT("OTG.MiniGolf"), TEXT("Play the putt-putt (nine holes of mini golf)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			if (UOTGCourseSubsystem* S = GI ? GI->GetSubsystem<UOTGCourseSubsystem>() : nullptr) S->PlayCourse(AOTGMiniGolf::CourseId);
		}));

}

// ------------------------------------------------------------------ drawing it

AOTGMiniGolf::AOTGMiniGolf()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Base(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// (the signs' lettering: the engine's own font and text material, referenced here so a packaged
	// game has them)
	static ConstructorHelpers::FObjectFinder<UFont> TextFont(TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TextMat(TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	ConeMesh = Cone.Object;
	BaseMat = Base.Object;
}

AOTGMiniGolf* AOTGMiniGolf::In(UWorld* World)
{
	if (World) for (TActorIterator<AOTGMiniGolf> It(World); It; ++It) return *It;
	return nullptr;
}

FVector AOTGMiniGolf::W(double X, double Y, double Z) const
{
	return Data->CourseToWorld(FVector(X, Y, Z));
}

UMaterialInstanceDynamic* AOTGMiniGolf::Mat(const FLinearColor& C)
{
	const uint32 Key = C.ToFColor(false).ToPackedARGB();
	if (const int32* I = MatIndex.Find(Key)) return Mats[*I];
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
	M->SetVectorParameterValue(TEXT("Color"), C);
	MatIndex.Add(Key, Mats.Add(M));
	return M;
}

UInstancedStaticMeshComponent* AOTGMiniGolf::Ism(UStaticMesh* Mesh, const FLinearColor& C, bool bShadow)
{
	const FString Key = FString::Printf(TEXT("%s %08x %d"), *Mesh->GetName(), C.ToFColor(false).ToPackedARGB(), bShadow ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* F = Isms.Find(Key)) return *F;
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
	I->SetStaticMesh(Mesh);
	I->SetMaterial(0, Mat(C));
	I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	I->SetCastShadow(bShadow);
	I->SetupAttachment(RootComponent);
	I->RegisterComponent();
	Isms.Add(Key, I);
	return I;
}

UStaticMeshComponent* AOTGMiniGolf::Shape(UStaticMesh* Mesh, const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent)
{
	UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(this);
	S->SetStaticMesh(Mesh);
	S->SetMaterial(0, Mat(C));
	S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	S->SetMobility(EComponentMobility::Movable);
	S->SetupAttachment(Parent ? Parent : RootComponent.Get());
	S->SetRelativeLocationAndRotation(CentreW, Rot);   // (the root's at the level's origin; a parent: relative to it)
	S->SetRelativeScale3D(SizeCm / 100.0);
	S->RegisterComponent();
	return S;
}

void AOTGMiniGolf::Box(const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent)
{
	Shape(CubeMesh, CentreW, SizeCm, Rot, C, Parent);
}

UProceduralMeshComponent* AOTGMiniGolf::NewProc()
{
	UProceduralMeshComponent* P = NewObject<UProceduralMeshComponent>(this);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetupAttachment(RootComponent);
	P->RegisterComponent();
	return P;
}

void AOTGMiniGolf::Build(UOTGCourseData* D)
{
	Data = D;
	const double T0 = FPlatformTime::Seconds();
	SetActorLocation(FVector::ZeroVector);
	// (the wind's material parameter collections, registered with the world before anything's drawn:
	// a packaged game draws materials that read one not yet registered wrongly - as AOTGRuntimeCourse)
	if (UWorld* World = GetWorld())
		for (const TCHAR* Path : { TEXT("/Game/OpenTeeGolf/Game/MPC_Wind.MPC_Wind"), TEXT("/Game/Fab/MaterialParameterCollection/QMPC_GlobalFoliageActor.QMPC_GlobalFoliageActor") })
			if (UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, Path)) World->GetParameterCollectionInstance(C);
	BuildLawn();
	BuildLanes();
	BuildBoards();
	BuildFeatures();
	BuildSurroundings();
	UE_LOG(LogOTGMiniGolf, Display, TEXT("Putt-putt drawn in %.0f ms (%d materials)"), (FPlatformTime::Seconds() - T0) * 1000, Mats.Num());
}

void AOTGMiniGolf::Tick(float Dt)
{
	Super::Tick(Dt);
	Clock += Dt;
	for (USceneComponent* S : Sails) if (S) S->SetRelativeRotation(FQuat(FVector::YAxisVector, Clock * SailSpeed));
}

// The sails sweeping across the windmill's tunnel (practice.js movers), from now on the clock
void AOTGMiniGolf::AddMovers(otg::ShotOptions& O) const
{
	const double T0 = Clock;
	for (const FMiniHole& H : Holes())
	{
		if (!H.bMill) continue;
		const double GX = H.At.X, GZ = H.At.Y + H.MillZ - 0.1, Half = H.MillHalf;
		O.movers.push_back([GX, GZ, Half, T0](const otg::V3& A, const otg::V3& B, double T, otg::Hit& Out) {
			const double DA = A.z - GZ, DB = B.z - GZ;
			if ((DA < 0) == (DB < 0) || DA == DB) return false;
			const double F = DA / (DA - DB), X = A.x + (B.x - A.x) * F, Y = A.y + (B.y - A.y) * F;
			if (FMath::Abs(X - GX) > Half + 0.03 || Y - HeightAt(X, GZ) > 0.35) return false;
			const double Q = HALF_PI, M = FMath::Fmod(FMath::Fmod((T0 + T) * SailSpeed, Q) + Q, Q);
			if (FMath::Min(M, Q - M) > SailBlock) return false;
			const double Dir = DB > DA ? 1 : -1;
			Out.kind = "blade";
			Out.normal = otg::V3{ 0, 0, -Dir };
			Out.point = otg::V3{ X, Y, GZ - Dir * 0.01 };
			return true;
		});
	}
}
