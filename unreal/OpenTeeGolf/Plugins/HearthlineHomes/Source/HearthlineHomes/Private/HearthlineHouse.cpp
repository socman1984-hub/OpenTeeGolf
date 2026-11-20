// Copyright __PUBLISHER__. All Rights Reserved.
#include "HearthlineHouse.h"
#include "HearthlineHouseGen.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_EDITOR
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "ObjectTools.h"
#endif

UMaterialInterface* FHearthlineMaterialSet::Get(int32 Slot) const
{
	switch (Slot)
	{
	case HearthlineSlot::LapSiding: return LapSiding;
	case HearthlineSlot::BoardAndBatten: return BoardAndBatten;
	case HearthlineSlot::Brick: return Brick;
	case HearthlineSlot::Stucco: return Stucco;
	case HearthlineSlot::FieldStone: return FieldStone;
	case HearthlineSlot::Concrete: return Concrete;
	case HearthlineSlot::Shingles: return Shingles;
	case HearthlineSlot::Trim: return Trim;
	case HearthlineSlot::Wood: return Wood;
	case HearthlineSlot::Glass: return Glass;
	case HearthlineSlot::Metal: return Metal;
	case HearthlineSlot::Asphalt: return Asphalt;
	case HearthlineSlot::Foliage: return Foliage;
	default: return Grass;
	}
}

FName FHearthlineMaterialSet::SlotName(int32 Slot)
{
	static const TCHAR* Names[NumSlots] = { TEXT("LapSiding"), TEXT("BoardAndBatten"), TEXT("Brick"), TEXT("Stucco"), TEXT("FieldStone"), TEXT("Concrete"),
		TEXT("Shingles"), TEXT("Trim"), TEXT("Wood"), TEXT("Glass"), TEXT("Metal"), TEXT("Asphalt"), TEXT("Grass"), TEXT("Foliage") };
	return FName(Names[FMath::Clamp(Slot, 0, NumSlots - 1)]);
}

void FHearthlineMaterialSet::LoadDefaults()
{
	// Hard references, so that cooking always includes the default materials.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F0(TEXT("/HearthlineHomes/Materials/MI_HH_LapSiding"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F1(TEXT("/HearthlineHomes/Materials/MI_HH_BoardAndBatten"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F2(TEXT("/HearthlineHomes/Materials/MI_HH_Brick"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F3(TEXT("/HearthlineHomes/Materials/MI_HH_Stucco"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F4(TEXT("/HearthlineHomes/Materials/MI_HH_FieldStone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F5(TEXT("/HearthlineHomes/Materials/MI_HH_Concrete"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F6(TEXT("/HearthlineHomes/Materials/MI_HH_Shingles"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F7(TEXT("/HearthlineHomes/Materials/MI_HH_Trim"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F8(TEXT("/HearthlineHomes/Materials/MI_HH_Wood"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F9(TEXT("/HearthlineHomes/Materials/M_HH_Glass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F10(TEXT("/HearthlineHomes/Materials/MI_HH_Metal"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F11(TEXT("/HearthlineHomes/Materials/MI_HH_Asphalt"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F12(TEXT("/HearthlineHomes/Materials/MI_HH_Grass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> F13(TEXT("/HearthlineHomes/Materials/M_HH_Foliage"));
	LapSiding = F0.Object; BoardAndBatten = F1.Object; Brick = F2.Object; Stucco = F3.Object; FieldStone = F4.Object; Concrete = F5.Object;
	Shingles = F6.Object; Trim = F7.Object; Wood = F8.Object; Glass = F9.Object; Metal = F10.Object; Asphalt = F11.Object; Grass = F12.Object; Foliage = F13.Object;
}

AHearthlineHouse::AHearthlineHouse()
{
	PrimaryActorTick.bCanEverTick = false;

	ShellComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Shell"));
	RootComponent = ShellComponent;
	ShellComponent->SetMobility(EComponentMobility::Static);
	ShellComponent->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::LinearToSRGB);
	ShellComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
	ShellComponent->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	ShellComponent->bEnableComplexCollision = true;
	ShellComponent->SetCollisionProfileName(TEXT("BlockAll"));

	BakedComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Baked"));
	BakedComponent->SetupAttachment(RootComponent);
	BakedComponent->SetMobility(EComponentMobility::Static);
	BakedComponent->SetVisibility(false);

	Materials.LoadDefaults();
	Volumes.AddDefaulted();
}

void AHearthlineHouse::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AHearthlineHouse::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuiltOnce) { Rebuild(); }
}

void AHearthlineHouse::Rebuild()
{
	bBuiltOnce = true;
	if (!ShellComponent || !BakedComponent) { return; }

	if (BakedMesh)
	{
		ShellComponent->SetMesh(UE::Geometry::FDynamicMesh3());
		ShellComponent->SetVisibility(false);
		ShellComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BakedComponent->SetStaticMesh(BakedMesh);
		BakedComponent->SetVisibility(true);
		BakedComponent->SetCollisionEnabled(bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		return;
	}
	BakedComponent->SetStaticMesh(nullptr);
	BakedComponent->SetVisibility(false);
	BakedComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FHearthlineHouseGen Gen(*this);
	Gen.Build();
	UE::Geometry::FDynamicMesh3& Mesh = Gen.B.Mesh;

	// keep only the material slots that are in use
	TArray<int32> Remap; Remap.Init(-1, FHearthlineMaterialSet::NumSlots);
	TArray<UMaterialInterface*> Mats;
	UE::Geometry::FDynamicMeshMaterialAttribute* MatID = Mesh.Attributes()->GetMaterialID();
	UsedSlots.Reset();
	for (int32 Slot = 0; Slot < FHearthlineMaterialSet::NumSlots; ++Slot)
	{
		bool bUsed = false;
		for (int32 Tid : Mesh.TriangleIndicesItr()) { if (MatID->GetValue(Tid) == Slot) { bUsed = true; break; } }
		if (bUsed) { Remap[Slot] = Mats.Num(); Mats.Add(Materials.Get(Slot)); UsedSlots.Add(Slot); }
	}
	for (int32 Tid : Mesh.TriangleIndicesItr()) { MatID->SetValue(Tid, Remap[FMath::Clamp(MatID->GetValue(Tid), 0, FHearthlineMaterialSet::NumSlots - 1)]); }
	TriangleCount = Mesh.TriangleCount();

	ShellComponent->SetMesh(MoveTemp(Mesh));
	ShellComponent->ConfigureMaterialSet(Mats);
	ShellComponent->SetVisibility(true);
	ShellComponent->bEnableComplexCollision = bGenerateCollision;
	ShellComponent->SetCollisionEnabled(bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	ShellComponent->UpdateCollision(false);
}

void AHearthlineHouse::ClearBake()
{
	BakedMesh = nullptr;
	Rebuild();
}

void AHearthlineHouse::BakeToStaticMesh()
{
#if WITH_EDITOR
	BakedMesh = nullptr;
	Rebuild();
	const UE::Geometry::FDynamicMesh3* Mesh = ShellComponent->GetMesh();
	if (!Mesh || Mesh->TriangleCount() == 0) { return; }

	const FString Name = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("SM_HH_%s"), *GetActorNameOrLabel()));
	const FString PackageName = BakeFolder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UStaticMesh* SM = FindObject<UStaticMesh>(Package, *Name);
	if (!SM) { SM = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone); }
	SM->Modify();

	FMeshDescription MD;
	FStaticMeshAttributes Attributes(MD);
	Attributes.Register();
	// The shell stores linear colours and the static mesh build encodes them to sRGB itself, so no transform here.
	FConversionToMeshDescriptionOptions ConversionOptions;
	ConversionOptions.bTransformVtxColorsSRGBToLinear = false;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	Converter.Convert(Mesh, MD);

	// name the polygon groups after the surface they carry
	TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	int32 GroupIndex = 0;
	for (const FPolygonGroupID GroupID : MD.PolygonGroups().GetElementIDs())
	{
		if (UsedSlots.IsValidIndex(GroupIndex)) { SlotNames[GroupID] = FHearthlineMaterialSet::SlotName(UsedSlots[GroupIndex]); }
		++GroupIndex;
	}

	SM->SetNumSourceModels(1);
	FMeshBuildSettings& Build = SM->GetSourceModel(0).BuildSettings;
	Build.bRecomputeNormals = false;
	Build.bRecomputeTangents = true;
	Build.bRemoveDegenerates = true;
	Build.bGenerateLightmapUVs = true;
	Build.SrcLightmapIndex = 0;
	Build.DstLightmapIndex = 1;
	Build.MinLightmapResolution = 128;
	SM->SetLightMapCoordinateIndex(1);
	SM->SetLightMapResolution(256);
	SM->CreateMeshDescription(0, MoveTemp(MD));
	SM->CommitMeshDescription(0);

	TArray<FStaticMaterial>& StaticMaterials = SM->GetStaticMaterials();
	StaticMaterials.Reset();
	for (int32 Slot : UsedSlots)
	{
		const FName SlotName = FHearthlineMaterialSet::SlotName(Slot);
		StaticMaterials.Add(FStaticMaterial(Materials.Get(Slot), SlotName, SlotName));
	}

	FMeshNaniteSettings Nanite = SM->GetNaniteSettings();
	Nanite.bEnabled = bBakeWithNanite;
	SM->SetNaniteSettings(Nanite);

	if (!SM->GetBodySetup()) { SM->CreateBodySetup(); }
	SM->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;

	SM->Build(false);
	SM->PostEditChange();
	SM->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(SM);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, SM, *FileName, SaveArgs);

	Modify();
	BakedMesh = SM;
	Rebuild();
#endif
}
