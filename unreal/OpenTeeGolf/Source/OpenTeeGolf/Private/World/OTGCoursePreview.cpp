// The runtime course as the studio's preview plot (see OTGPreviewStage.h): no export folder - a flat
// square of ground made here, its masks a round of tall grass in short rough, and the grass and trees
// the studio shows grown on it by the course's own parts.

#include "World/OTGRuntimeCourse.h"

#include "Course/OTGCourseData.h"
#include "Course/OTGTextures.h"
#include "World/OTGGrass.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRuntimeCourse, Log, All);

#include "World/OTGCourseShared.h"

namespace
{
	constexpr double PlotRound = 0.42;   // (the tall grass's round: this much of the plot across, from the middle)
}

void AOTGRuntimeCourse::BuildPreview(double SizeM)
{
	EnsureWindCollections(GetWorld());
	UOTGCourseData* D = NewObject<UOTGCourseData>(this);
	const int32 N = FMath::Max(9, FMath::RoundToInt32(SizeM / 2) + 1);
	D->CourseName = TEXT("Preview");
	D->HeightSamples = N;
	D->MetresPerSample = 2;
	D->HeightRange = 1;
	D->HeightBase = 0;
	D->HeightX0 = 0;
	D->HeightZ1 = (N - 1) * 2.0;
	D->Heights.SetNumZeroed(N * N);
	D->LevelOrigin = GetActorLocation() - FVector((N - 1) * 100.0, (N - 1) * 100.0, 0);
	Data = D;
	LandCm = (N - 1) * 200.0;
	Trees.bTreesRead = Trees.bRocksRead = true;

	// the masks: a round of tall grass ("native", a distance field - 0.5 on its edge, a sixth more a
	// metre in), the plain ground round it (as the land round a course reads: lighter than the rough's)
	constexpr int32 P = 128;
	const double Cm = LandCm / P, R = LandCm * PlotRound;
	for (const TCHAR* K : MaskKeys)
	{
		otg::FPicture Pic;
		Pic.W = Pic.H = P;
		Pic.Channels = 1;
		Pic.Pixels.SetNumZeroed(P * P);
		const FString Key(K);
		for (int32 j = 0; j < P; j++)
			for (int32 i = 0; i < P; i++)
			{
				const double Dm = (FVector2D((i + 0.5) * Cm, (j + 0.5) * Cm) - FVector2D(LandCm / 2, LandCm / 2)).Size() / 100.0;
				const double In = 0.5 + (R / 100.0 - Dm) / 6.0;
				double V = 0;
				if (Key == TEXT("native")) V = In;

				Pic.Pixels[j * P + i] = uint8(FMath::Clamp(V, 0.0, 1.0) * 255);
			}
		if (UTexture2D* T = otg::MakeTexture(Pic, otg::ETexKind::Mask, this)) Textures.Add(FName(FString::Printf(TEXT("Mask_%s"), K)), T);
		MaskPixels.Add(Key, MoveTemp(Pic));
	}
	{
		// (the "photo": a dull green, where the ground material mixes one in)
		otg::FPicture Pic;
		Pic.W = Pic.H = 4;
		Pic.Channels = 4;
		for (int32 i = 0; i < 16; i++) Pic.Pixels.Append({ 40, 78, 64, 255 });
		if (UTexture2D* T = otg::MakeTexture(Pic, otg::ETexKind::Photo, this)) Textures.Add(TEXT("Photo"), T);
	}

	// the ground: flat, the course's ground material on it - the plot itself, and land round it out
	// well past (a copy of the material with none of its masks: the material places them by
	// where they are, wrapping, so the round of tall grass came again and again across the land round it)
	UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("M_CourseGround_Runtime"));
	UMaterialInstanceDynamic* Round = nullptr;
	if (Base)
	{
		GroundMat = UMaterialInstanceDynamic::Create(Base, this);
		SetPictures(GroundMat);
		Turf.TintGround();
		Round = UMaterialInstanceDynamic::Create(Base, this);
		SetPictures(Round);
		Round->CopyParameterOverrides(GroundMat);
		for (const TCHAR* K : MaskKeys)
		{
			otg::FPicture Pic;
			Pic.W = Pic.H = 4;
			Pic.Channels = 1;
			Pic.Pixels.Init(0, 16);
			if (UTexture2D* T = otg::MakeTexture(Pic, otg::ETexKind::Mask, this))
			{
				Round->SetTextureParameterValue(FName(FString::Printf(TEXT("Mask_%s"), K)), T);
				Textures.Add(FName(FString::Printf(TEXT("Plain_%s"), K)), T);   // (kept)
			}
		}
	}
	auto Sheet = [&](double From, double To, int32 Q, double Z, UMaterialInterface* M) {
		UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
		const double Step = (To - From) / Q;
		TArray<FVector> V; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tan;
		for (int32 j = 0; j <= Q; j++)
			for (int32 i = 0; i <= Q; i++)
			{
				const double X = From + i * Step, Y = From + j * Step;   // (the level's frame, from the plot's corner)
				V.Add(D->LevelOrigin + FVector(X, Y, Z) - GetActorLocation());
				UV.Add(FVector2D(X / 100.0, Y / 100.0));
				Nrm.Add(FVector::UpVector);
				Tan.Add(FProcMeshTangent(1, 0, 0));
			}
		for (int32 j = 0; j < Q; j++)
			for (int32 i = 0; i < Q; i++)
			{
				const int32 A = j * (Q + 1) + i, B = A + 1, C = A + Q + 1, E = C + 1;
				Tri.Append({ A, C, B, B, C, E });
			}
		G->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, false);
		if (M) G->SetMaterial(0, M);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetupAttachment(RootComponent);
		G->RegisterComponent();
		Parts.Add(G);
	};
	Sheet(-LandCm * 3, LandCm * 4, 28, -3, Round);
	{
		// the round itself, a little over the land round it, a few metres wider than its tall grass
		// (its edge under the grass: the two materials' grounds never quite match)
		UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
		constexpr int32 Segs = 64, Rings = 8;
		const double Rad = R + 200.0;
		const FVector2D Mid(LandCm / 2, LandCm / 2);
		TArray<FVector> V; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tan;
		auto Vert = [&](const FVector2D& P) { V.Add(D->LevelOrigin + FVector(P.X, P.Y, 0) - GetActorLocation()); UV.Add(P / 100.0); Nrm.Add(FVector::UpVector); Tan.Add(FProcMeshTangent(1, 0, 0)); };
		Vert(Mid);
		for (int32 r = 1; r <= Rings; r++)
			for (int32 k = 0; k < Segs; k++)
			{
				const double A = 2 * PI * k / Segs;
				Vert(Mid + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad * r / Rings);
			}
		for (int32 k = 0; k < Segs; k++) Tri.Append({ 0, 1 + (k + 1) % Segs, 1 + k });
		for (int32 r = 1; r < Rings; r++)
			for (int32 k = 0; k < Segs; k++)
			{
				const int32 A = 1 + (r - 1) * Segs + k, B = 1 + (r - 1) * Segs + (k + 1) % Segs, C = A + Segs, E = B + Segs;
				Tri.Append({ A, B, C, B, E, C });
			}
		G->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, false);
		if (GroundMat) G->SetMaterial(0, GroundMat);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetupAttachment(RootComponent);
		G->RegisterComponent();
		Parts.Add(G);
	}
	UE_LOG(LogOTGRuntimeCourse, Display, TEXT("Preview plot: %.0f m"), LandCm / 100.0);
}

void AOTGRuntimeCourse::PreviewGrass(const TArray<otg::FGrassKind>& Kinds)
{
	if (!Data) return;
	Turf.BuildGrassFrom(Kinds);
}

void AOTGRuntimeCourse::PreviewForest(const FOTGForestPreset* Preset)
{
	if (!Data) return;
	Data->Trees.Reset();
	Trees.TreeRecs.Reset();
	Data->TreeDesign = FOTGTreeDesign();
	if (Preset && Preset->IsSet())
	{
		// its species each drawn with its model
		for (const FOTGForestSpecies& S : Preset->Species)
			if (!S.Kind.IsEmpty() && !S.Model.IsEmpty() && S.Model != TEXT("auto")) Data->TreeDesign.Species.FindOrAdd(S.Kind).Model = S.Model;
		// planted on the round at the preset's spacing (jittered), each its species and height by where it
		// stands - as a forest on a course is (FOTGForestPreset::PickAt, HeightAt)
		const double Spacing = 100.0 / FMath::Sqrt(FMath::Max(1.0, double(Preset->PerHa)));
		const double SizeM = LandCm / 100.0, R = SizeM * PlotRound - 2;
		for (double Z = Spacing / 2; Z < SizeM; Z += Spacing)
			for (double X = Spacing / 2; X < SizeM; X += Spacing)
			{
				const double PX = X + (FOTGTreeDesign::Hash01(X, Z, 41) - 0.5) * Spacing * 0.8, PZ = Z + (FOTGTreeDesign::Hash01(X, Z, 43) - 0.5) * Spacing * 0.8;
				if (FVector2D(PX - SizeM / 2, PZ - SizeM / 2).Size() > R || Data->Trees.Num() >= 400) continue;
				const FOTGForestSpecies* Sp = Preset->PickAt(PX, PZ);
				if (!Sp) continue;
				FOTGTreeEdit E;
				E.X = PX; E.Z = PZ; E.Kind = Sp->Kind; E.Height = Preset->HeightAt(PX, PZ);
				Data->Trees.Add(UOTGCourseData::AddedShape(E));
				FOTGCourseTrees::FTreeRec Rec;
				Rec.X = (PX - Data->HeightX0) * 100.0; Rec.Y = (Data->HeightZ1 - PZ) * 100.0; Rec.Z = 0; Rec.H = E.Height * 100.0; Rec.Kind = Sp->Kind;
				Trees.TreeRecs.Add(Rec);
			}
	}
	Trees.BuildTrees();
}
