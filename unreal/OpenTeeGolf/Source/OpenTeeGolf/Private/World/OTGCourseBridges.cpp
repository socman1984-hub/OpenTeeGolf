// The runtime course's bridges (see OTGRuntimeCourse.h, OTGBridges.h): a mesh each, made here as
// the props' are, in the props' bridge material (colours per vertex); the gold posts the bridge
// designer marks points with.

#include "World/OTGCourseShared.h"
#include "World/OTGBridges.h"

namespace
{
	UStaticMesh* MeshFrom(UObject* Owner, FMeshDescription& MD, UMaterialInterface* Mat)
	{
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Owner);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, TEXT("Main")));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		Mesh->BuildFromMeshDescriptions({ &MD }, Params);
		return Mesh;
	}
}

const FOTGBridgeDesign& AOTGRuntimeCourse::BridgeDesign() const
{
	static const FOTGBridgeDesign None;
	return Data ? Data->Bridges : None;
}

void AOTGRuntimeCourse::SetBridges(const FOTGBridgeDesign& Design)
{
	if (!Data) return;
	Data->Bridges = Design;
	BuildBridges();
}

bool AOTGRuntimeCourse::SaveBridges(FString* Where) const
{
	if (!Data) return false;
	if (Where) *Where = FPaths::Combine(Data->ExportDir, TEXT("bridges.json"));
	return Data->Bridges.Save(Data->ExportDir);
}

FOTGBridgeDesign AOTGRuntimeCourse::SavedBridges() const
{
	return Data ? FOTGBridgeDesign::Load(Data->ExportDir) : FOTGBridgeDesign();
}

double AOTGRuntimeCourse::GroundHeightAt(double X, double Z) const
{
	return Data ? Data->GroundAt(X, Z) : 0.0;
}

FVector AOTGRuntimeCourse::CourseToWorldAt(double X, double Y, double Z) const
{
	return Data ? Data->CourseToWorld(FVector(X, Y, Z)) : FVector::ZeroVector;
}

void AOTGRuntimeCourse::BuildBridges()
{
	for (UStaticMeshComponent* C : BridgeMeshes)
		if (C) { Parts.Remove(C); C->DestroyComponent(); }
	BridgeMeshes.Reset();
	if (!Data || !Data->Bridges.Bridges.Num()) return;
	UMaterialInterface* Mat = Load<UMaterialInterface>(TEXT("M_Prop_bridge"));
	if (!Mat) Mat = Load<UMaterialInterface>(TEXT("M_Prop_building"));
	for (const FOTGBridge& B : Data->Bridges.Bridges)
	{
		// (the mesh about its first end: the level's centimetres are large numbers)
		const FVector Origin = Data->CourseToWorld(FVector(B.AX, B.AY, B.AZ));
		FMeshDescription MD;
		B.BuildMesh(MD, [this](const FVector& P) { return Data->CourseToWorld(P); }, Origin, [this](double X, double Z) { return Data->GroundAt(X, Z); });
		if (!MD.Triangles().Num()) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(MeshFrom(this, MD, Mat));
		C->SetMaterial(0, Mat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetWorldLocation(Origin);
		BridgeMeshes.Add(C);
		Parts.Add(C);
	}
	UE_LOG(LogTemp, Display, TEXT("Bridges: %d drawn"), BridgeMeshes.Num());
}

int32 AOTGRuntimeCourse::PickBridge(const FVector& Origin, const FVector& Dir, double MaxT) const
{
	if (!Data || !Data->Bridges.Bridges.Num()) return -1;
	// (along the line, 20 cm at a time, as far as 1.5 km: the first point on or just under a deck)
	const double Far = FMath::Min(MaxT + 300.0, 150000.0);
	for (double T = 0; T < Far; T += 20)
	{
		const FVector C = Data->WorldToCourse(Origin + Dir * T);
		double Top = 0;
		const int32 I = Data->Bridges.At(C.X, C.Z, &Top);
		if (I >= 0 && C.Y <= Top + 1.2 && C.Y >= Top - 1.0) return I;
	}
	return -1;
}

void AOTGRuntimeCourse::MarkPoints(const TArray<FVector>& Points)
{
	if (!PointMarkMesh)
	{
		FMeshDescription MD;
		otg::BridgeMarkerMesh(MD, 260);
		UMaterialInterface* Mat = Load<UMaterialInterface>(TEXT("M_Prop_bridge"));
		if (!Mat) Mat = Load<UMaterialInterface>(TEXT("M_Prop_building"));
		PointMarkMesh = MeshFrom(this, MD, Mat);
	}
	while (PointMarks.Num() < Points.Num())
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(PointMarkMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCastShadow(false);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		PointMarks.Add(C);
	}
	for (int32 i = 0; i < PointMarks.Num(); i++)
	{
		PointMarks[i]->SetVisibility(i < Points.Num());
		if (i < Points.Num()) PointMarks[i]->SetWorldLocation(Points[i]);
	}
	PointMarksAt = FPlatformTime::Seconds();
}

void AOTGRuntimeCourse::UpdateMarkPoints()
{
	if (!PointMarks.Num() || FPlatformTime::Seconds() - PointMarksAt < 0.25) return;
	for (UStaticMeshComponent* C : PointMarks) if (C && C->IsVisible()) C->SetVisibility(false);
}
