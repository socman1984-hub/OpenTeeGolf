// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardEditorLibrary.h"
#include "GreenswardEditorPrivate.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY(LogGreenswardEditor);

namespace GreenswardMatPrivate
{
	struct FMeshWriter
	{
		FMeshDescription& Mesh;
		FPolygonGroupID Group;
		TVertexAttributesRef<FVector3f> Positions;
		TVertexInstanceAttributesRef<FVector3f> Normals;
		TVertexInstanceAttributesRef<FVector2f> UVs;
		TVertexInstanceAttributesRef<FVector4f> Colors;

		FMeshWriter(FMeshDescription& InMesh, FStaticMeshAttributes& Attributes, FPolygonGroupID InGroup)
			: Mesh(InMesh)
			, Group(InGroup)
			, Positions(Attributes.GetVertexPositions())
			, Normals(Attributes.GetVertexInstanceNormals())
			, UVs(Attributes.GetVertexInstanceUVs())
			, Colors(Attributes.GetVertexInstanceColors())
		{
		}

		/**
		 * Colour channels as the material reads them:
		 * R = 0 at the root, 1 at the tip. G = per-blade random. B = dry amount. A = blade height / 100 cm.
		 */
		FVertexInstanceID AddVertex(const FVector3f& P, const FVector3f& N, const FVector2f& UV, float R, float G, float B, float A)
		{
			const FVertexID V = Mesh.CreateVertex();
			Positions[V] = P;
			const FVertexInstanceID VI = Mesh.CreateVertexInstance(V);
			Normals[VI] = N;
			UVs.Set(VI, 0, UV);
			// The static mesh build converts linear to sRGB bytes; pre-decode so the stored byte equals our value.
			const FColor Bytes(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(R * 255.0f), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(G * 255.0f), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(B * 255.0f), 0, 255)),
				255);
			const FLinearColor Linear = FLinearColor::FromSRGBColor(Bytes);
			Colors[VI] = FVector4f(Linear.R, Linear.G, Linear.B, FMath::Clamp(A, 0.0f, 1.0f));
			return VI;
		}

		void AddTriangle(FVertexInstanceID A, FVertexInstanceID B, FVertexInstanceID C)
		{
			const FVertexInstanceID Tri[3] = { A, B, C };
			Mesh.CreateTriangle(Group, MakeArrayView(Tri, 3));
		}
	};

	/** A blade's centre line: an arc that starts tilted by Tilt and turns by Curve radians over Length. */
	struct FBladeArc
	{
		float Length = 1.0f;
		float Tilt = 0.0f;
		float Curve = 0.0f;

		void Eval(float Along, float& OutForward, float& OutUp, float& OutAngle) const
		{
			OutAngle = Tilt + Curve * (Along / Length);
			if (FMath::Abs(Curve) < 1e-3f)
			{
				OutForward = Along * FMath::Sin(Tilt);
				OutUp = Along * FMath::Cos(Tilt);
			}
			else
			{
				const float K = Length / Curve;
				OutForward = K * (FMath::Cos(Tilt) - FMath::Cos(OutAngle));
				OutUp = K * (FMath::Sin(OutAngle) - FMath::Sin(Tilt));
			}
		}
	};

	static int32 AutoSegments(float Height)
	{
		if (Height <= 2.5f) return 1;
		if (Height <= 6.0f) return 2;
		if (Height <= 12.0f) return 3;
		if (Height <= 25.0f) return 4;
		return 5;
	}

	static float WrapToMat(float Value, float Half)
	{
		const float Size = Half * 2.0f;
		float W = FMath::Fmod(Value + Half, Size);
		if (W < 0.0f)
		{
			W += Size;
		}
		return W - Half;
	}

	static void AddBlade(FMeshWriter& Writer, FRandomStream& Rng, const FGreenswardMatSettings& S, const FVector2f& Root, float Facing, bool bDry, int32 Segments)
	{
		const FVector3f Up(0, 0, 1);
		const FVector3f Fwd(FMath::Cos(Facing), FMath::Sin(Facing), 0);
		const FVector3f Side(-Fwd.Y, Fwd.X, 0);

		FBladeArc Arc;
		const float HeightRand = Rng.FRand();
		if (S.bMown)
		{
			// Natural length straddles the cutting height: most blades get cut, some are young and short.
			Arc.Length = S.Height * FMath::Lerp(1.0f - S.HeightVariation, 1.9f, HeightRand);
		}
		else
		{
			Arc.Length = S.Height * (1.0f - S.HeightVariation * HeightRand * HeightRand);
		}
		if (bDry)
		{
			Arc.Length *= 0.85f;
		}

		const float MaxAngle = FMath::DegreesToRadians(80.0f);
		Arc.Tilt = FMath::DegreesToRadians(S.Lean) * Rng.FRand();
		Arc.Curve = FMath::DegreesToRadians(S.Bend) * Rng.FRandRange(0.35f, 1.0f) * (bDry ? 1.5f : 1.0f);
		Arc.Curve = FMath::Min(Arc.Curve, MaxAngle - Arc.Tilt);

		// Find where the mower cuts it.
		float End = Arc.Length;
		bool bCut = false;
		if (S.bMown)
		{
			float F, U, A;
			Arc.Eval(Arc.Length, F, U, A);
			if (U > S.Height)
			{
				float Lo = 0.0f;
				float Hi = Arc.Length;
				for (int32 I = 0; I < 14; ++I)
				{
					const float Mid = 0.5f * (Lo + Hi);
					Arc.Eval(Mid, F, U, A);
					(U > S.Height ? Hi : Lo) = Mid;
				}
				End = 0.5f * (Lo + Hi);
				bCut = true;
			}
		}

		const float BaseWidth = S.BladeWidth * Rng.FRandRange(0.7f, 1.25f);
		const float Random = Rng.FRand();
		const float Dry = bDry ? 1.0f : 0.0f;
		const float HeightAlpha = FMath::Clamp(End / 100.0f, 0.0f, 1.0f);
		const float UpBlend = 0.55f;

		auto WidthAt = [&](float Along)
		{
			const float T = Along / Arc.Length;
			// Slightly narrower at the sheath, widest a third of the way up, tapering to the tip.
			const float Sheath = FMath::Lerp(0.8f, 1.0f, FMath::Clamp(T * 4.0f, 0.0f, 1.0f));
			return BaseWidth * Sheath * (1.0f - FMath::Pow(T, 1.7f));
		};

		FVertexInstanceID PrevL, PrevR;
		for (int32 I = 0; I <= Segments; ++I)
		{
			const float T = static_cast<float>(I) / Segments;
			const float Along = End * T;
			float F, U, Angle;
			Arc.Eval(Along, F, U, Angle);

			const FVector3f Centre = FVector3f(Root.X, Root.Y, 0) + Fwd * F + Up * U;
			const FVector3f Face = -Fwd * FMath::Cos(Angle) + Up * FMath::Sin(Angle);
			const float Width = WidthAt(Along);
			const bool bPoint = (I == Segments) && (!bCut || Width < 0.03f);

			if (bPoint)
			{
				const FVector3f N = FMath::Lerp(Face, Up, UpBlend).GetSafeNormal();
				const FVertexInstanceID Tip = Writer.AddVertex(Centre, N, FVector2f(0.5f, T), T, Random, Dry, HeightAlpha);
				Writer.AddTriangle(PrevL, PrevR, Tip);
			}
			else
			{
				const FVector3f NL = FMath::Lerp((Face - Side * 0.35f).GetSafeNormal(), Up, UpBlend).GetSafeNormal();
				const FVector3f NR = FMath::Lerp((Face + Side * 0.35f).GetSafeNormal(), Up, UpBlend).GetSafeNormal();
				const FVertexInstanceID L = Writer.AddVertex(Centre - Side * (Width * 0.5f), NL, FVector2f(0.0f, T), T, Random, Dry, HeightAlpha);
				const FVertexInstanceID R = Writer.AddVertex(Centre + Side * (Width * 0.5f), NR, FVector2f(1.0f, T), T, Random, Dry, HeightAlpha);
				if (I > 0)
				{
					Writer.AddTriangle(PrevL, PrevR, R);
					Writer.AddTriangle(PrevL, R, L);
				}
				PrevL = L;
				PrevR = R;
			}
		}
	}

	/** Three-sided tube through a list of rings. A zero radius collapses the ring to a point. */
	static void AddTube(FMeshWriter& Writer, const TArray<FVector3f>& Centres, const TArray<float>& Radii, const TArray<float>& Along, float Random, const TArray<float>& Dry, float HeightAlpha)
	{
		const int32 Sides = 3;
		TArray<FVertexInstanceID> Prev;
		TArray<FVertexInstanceID> Ring;
		for (int32 I = 0; I < Centres.Num(); ++I)
		{
			Ring.Reset();
			FVector3f Axis = (I + 1 < Centres.Num() ? Centres[I + 1] - Centres[I] : Centres[I] - Centres[I - 1]).GetSafeNormal();
			FVector3f X = FVector3f::CrossProduct(Axis, FVector3f(0, 1, 0)).GetSafeNormal();
			if (X.IsNearlyZero())
			{
				X = FVector3f(1, 0, 0);
			}
			const FVector3f Y = FVector3f::CrossProduct(Axis, X);

			if (Radii[I] <= KINDA_SMALL_NUMBER)
			{
				Ring.Add(Writer.AddVertex(Centres[I], Axis * (I == 0 ? -1.0f : 1.0f), FVector2f(0.5f, Along[I]), Along[I], Random, Dry[I], HeightAlpha));
			}
			else
			{
				for (int32 S = 0; S < Sides; ++S)
				{
					const float Angle = 2.0f * PI * S / Sides;
					const FVector3f Dir = X * FMath::Cos(Angle) + Y * FMath::Sin(Angle);
					const FVector3f N = FMath::Lerp(Dir, FVector3f(0, 0, 1), 0.35f).GetSafeNormal();
					Ring.Add(Writer.AddVertex(Centres[I] + Dir * Radii[I], N, FVector2f(static_cast<float>(S) / Sides, Along[I]), Along[I], Random, Dry[I], HeightAlpha));
				}
			}

			if (I > 0)
			{
				if (Prev.Num() == Sides && Ring.Num() == Sides)
				{
					for (int32 S = 0; S < Sides; ++S)
					{
						const int32 N = (S + 1) % Sides;
						Writer.AddTriangle(Prev[S], Prev[N], Ring[N]);
						Writer.AddTriangle(Prev[S], Ring[N], Ring[S]);
					}
				}
				else if (Prev.Num() == Sides && Ring.Num() == 1)
				{
					for (int32 S = 0; S < Sides; ++S)
					{
						Writer.AddTriangle(Prev[S], Prev[(S + 1) % Sides], Ring[0]);
					}
				}
				else if (Prev.Num() == 1 && Ring.Num() == Sides)
				{
					for (int32 S = 0; S < Sides; ++S)
					{
						Writer.AddTriangle(Prev[0], Ring[(S + 1) % Sides], Ring[S]);
					}
				}
			}
			Prev = Ring;
		}
	}

	static void AddStalk(FMeshWriter& Writer, FRandomStream& Rng, const FGreenswardMatSettings& S, const FVector2f& Root)
	{
		const float Height = S.Height * S.StalkHeightScale * Rng.FRandRange(0.75f, 1.15f);
		const float Facing = Rng.FRandRange(0.0f, 2.0f * PI);
		const FVector3f Fwd(FMath::Cos(Facing), FMath::Sin(Facing), 0);
		const FVector3f Up(0, 0, 1);

		FBladeArc Arc;
		Arc.Length = Height;
		Arc.Tilt = FMath::DegreesToRadians(Rng.FRandRange(0.0f, 9.0f));
		Arc.Curve = FMath::DegreesToRadians(Rng.FRandRange(4.0f, 22.0f));

		const float HeadLength = Height * Rng.FRandRange(0.12f, 0.2f);
		const float StemRadius = FMath::Clamp(Height * 0.0022f, 0.05f, 0.14f);
		const float HeadRadius = StemRadius * Rng.FRandRange(2.2f, 3.2f);
		const float Random = Rng.FRand();
		const float HeightAlpha = FMath::Clamp(Height / 100.0f, 0.0f, 1.0f);

		TArray<FVector3f> Centres;
		TArray<float> Radii;
		TArray<float> Along;
		TArray<float> Dry;

		auto Push = [&](float Distance, float Radius, float DryAmount)
		{
			float F, U, A;
			Arc.Eval(Distance, F, U, A);
			Centres.Add(FVector3f(Root.X, Root.Y, 0) + Fwd * F + Up * U);
			Radii.Add(Radius);
			Along.Add(Distance / Height);
			Dry.Add(DryAmount);
		};

		const float StemEnd = Height - HeadLength;
		const int32 StemSegments = 4;
		for (int32 I = 0; I <= StemSegments; ++I)
		{
			const float T = static_cast<float>(I) / StemSegments;
			Push(StemEnd * T, FMath::Lerp(StemRadius, StemRadius * 0.6f, T), FMath::Lerp(0.15f, 0.55f, T));
		}
		// Seed head: a spindle.
		Push(StemEnd + HeadLength * 0.08f, HeadRadius * 0.7f, 1.0f);
		Push(StemEnd + HeadLength * 0.4f, HeadRadius, 1.0f);
		Push(StemEnd + HeadLength * 0.75f, HeadRadius * 0.6f, 1.0f);
		Push(Height, 0.0f, 1.0f);

		AddTube(Writer, Centres, Radii, Along, Random, Dry, HeightAlpha);
	}
}

UStaticMesh* UGreenswardEditorLibrary::GenerateGrassMat(const FGreenswardMatSettings& Settings, const FString& PackagePath, const FString& AssetName, UMaterialInterface* Material)
{
	using namespace GreenswardMatPrivate;

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogGreenswardEditor, Error, TEXT("GenerateGrassMat: a package path and asset name are required."));
		return nullptr;
	}

	FGreenswardMatSettings S = Settings;
	S.MatSize = FMath::Max(S.MatSize, 1.0f);
	S.Height = FMath::Max(S.Height, 0.2f);
	S.BladeCount = FMath::Clamp(S.BladeCount, 1, 60000);
	const int32 Segments = S.Segments > 0 ? S.Segments : AutoSegments(S.Height);

	if (!Material)
	{
		const TCHAR* Path = S.Height <= 5.0f
			? TEXT("/Greensward/Materials/MI_Greensward_Grass_Static.MI_Greensward_Grass_Static")
			: TEXT("/Greensward/Materials/MI_Greensward_Grass_Wind.MI_Greensward_Grass_Wind");
		Material = LoadObject<UMaterialInterface>(nullptr, Path);
	}

	UStaticMesh* StaticMesh = GreenswardEditor::FindOrCreateAsset<UStaticMesh>(PackagePath, AssetName);
	if (!StaticMesh)
	{
		return nullptr;
	}

	StaticMesh->PreEditChange(nullptr);
	StaticMesh->SetNumSourceModels(1);

	FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(0);
	SourceModel.BuildSettings.bRecomputeNormals = false;
	SourceModel.BuildSettings.bRecomputeTangents = true;
	SourceModel.BuildSettings.bUseMikkTSpace = false;
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
	SourceModel.BuildSettings.bUseFullPrecisionUVs = false;

	FMeshDescription* MeshDescription = StaticMesh->CreateMeshDescription(0);
	if (!MeshDescription)
	{
		return nullptr;
	}
	*MeshDescription = FMeshDescription();
	FStaticMeshAttributes Attributes(*MeshDescription);
	Attributes.Register();

	const int32 VertsPerBlade = (Segments + 1) * 2;
	MeshDescription->ReserveNewVertices(S.BladeCount * VertsPerBlade + S.StalkCount * 32);
	MeshDescription->ReserveNewVertexInstances(S.BladeCount * VertsPerBlade + S.StalkCount * 32);
	MeshDescription->ReserveNewTriangles(S.BladeCount * Segments * 2 + S.StalkCount * 48);
	MeshDescription->ReserveNewPolygonGroups(1);

	const FPolygonGroupID Group = MeshDescription->CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Grass");

	FMeshWriter Writer(*MeshDescription, Attributes, Group);
	FRandomStream Rng(S.Seed);
	const float Half = S.MatSize * 0.5f;

	// Tuft centres.
	const int32 NumTufts = FMath::Max(1, S.BladeCount / 14);
	TArray<FVector2f> Tufts;
	Tufts.Reserve(NumTufts);
	for (int32 I = 0; I < NumTufts; ++I)
	{
		Tufts.Add(FVector2f(Rng.FRandRange(-Half, Half), Rng.FRandRange(-Half, Half)));
	}
	const float TuftRadius = 0.9f + S.Height * 0.045f;

	for (int32 I = 0; I < S.BladeCount; ++I)
	{
		FVector2f Root;
		float Facing;
		if (Rng.FRand() < S.Clumping)
		{
			const FVector2f& Centre = Tufts[Rng.RandRange(0, NumTufts - 1)];
			const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
			const float Radius = TuftRadius * FMath::Sqrt(-2.0f * FMath::Loge(FMath::Max(Rng.FRand(), 1e-4f))) * 0.6f;
			Root = Centre + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			Facing = Angle + Rng.FRandRange(-0.7f, 0.7f);
		}
		else
		{
			Root = FVector2f(Rng.FRandRange(-Half, Half), Rng.FRandRange(-Half, Half));
			Facing = Rng.FRandRange(0.0f, 2.0f * PI);
		}
		Root.X = WrapToMat(Root.X, Half);
		Root.Y = WrapToMat(Root.Y, Half);

		const bool bDry = Rng.FRand() < S.DryFraction;
		AddBlade(Writer, Rng, S, Root, Facing, bDry, Segments);
	}

	for (int32 I = 0; I < S.StalkCount; ++I)
	{
		AddStalk(Writer, Rng, S, FVector2f(Rng.FRandRange(-Half, Half), Rng.FRandRange(-Half, Half)));
	}

	if (S.bGroundCover)
	{
		// A flat sheet of thatch just above the soil. Root-coloured, never moves in the wind.
		// U is 1 everywhere: the material shades a blade by U (dark midrib at 0.5) and the sheet must stay uniform.
		const float Z = 0.12f;
		const FVector3f Up(0, 0, 1);
		const FVertexInstanceID A = Writer.AddVertex(FVector3f(-Half, -Half, Z), Up, FVector2f(1, 0), 0.1f, 0.5f, 0.0f, 0.0f);
		const FVertexInstanceID B = Writer.AddVertex(FVector3f(Half, -Half, Z), Up, FVector2f(1, 0), 0.1f, 0.5f, 0.0f, 0.0f);
		const FVertexInstanceID C = Writer.AddVertex(FVector3f(Half, Half, Z), Up, FVector2f(1, 1), 0.1f, 0.5f, 0.0f, 0.0f);
		const FVertexInstanceID D = Writer.AddVertex(FVector3f(-Half, Half, Z), Up, FVector2f(1, 1), 0.1f, 0.5f, 0.0f, 0.0f);
		Writer.AddTriangle(A, C, B);
		Writer.AddTriangle(A, D, C);

		// A skirt that tucks the sheet into the soil, so a mat lifted clear of uneven ground never shows a gap.
		const float Depth = -4.0f;
		const FVector3f Corners[4] = { FVector3f(-Half, -Half, 0), FVector3f(Half, -Half, 0), FVector3f(Half, Half, 0), FVector3f(-Half, Half, 0) };
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			const FVector3f P0 = Corners[Edge];
			const FVector3f P1 = Corners[(Edge + 1) % 4];
			const FVector3f Outward = FVector3f((P0 + P1).X, (P0 + P1).Y, 0).GetSafeNormal();
			const FVector3f N = (Outward * 0.5f + Up * 0.5f).GetSafeNormal();
			const FVertexInstanceID T0 = Writer.AddVertex(P0 + FVector3f(0, 0, Z), N, FVector2f(1, 1), 0.1f, 0.5f, 0.0f, 0.0f);
			const FVertexInstanceID T1 = Writer.AddVertex(P1 + FVector3f(0, 0, Z), N, FVector2f(1, 1), 0.1f, 0.5f, 0.0f, 0.0f);
			const FVertexInstanceID B0 = Writer.AddVertex(P0 + FVector3f(0, 0, Depth), N, FVector2f(1, 0), 0.0f, 0.5f, 0.0f, 0.0f);
			const FVertexInstanceID B1 = Writer.AddVertex(P1 + FVector3f(0, 0, Depth), N, FVector2f(1, 0), 0.0f, 0.5f, 0.0f, 0.0f);
			Writer.AddTriangle(T0, T1, B1);
			Writer.AddTriangle(T0, B1, B0);
		}
	}

	StaticMesh->CommitMeshDescription(0);

	StaticMesh->GetStaticMaterials().Reset();
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Grass"), TEXT("Grass")));

	FMeshNaniteSettings Nanite = StaticMesh->GetNaniteSettings();
	Nanite.bEnabled = S.bNanite;
	Nanite.ShapePreservation = ENaniteShapePreservation::PreserveArea;
	StaticMesh->SetNaniteSettings(Nanite);

	// Room for wind sway so mats are not culled at the screen edge.
	const float Sway = S.Height * 0.8f;
	StaticMesh->SetPositiveBoundsExtension(FVector(Sway, Sway, 2.0f));
	StaticMesh->SetNegativeBoundsExtension(FVector(Sway, Sway, 0.0f));

	StaticMesh->SetImportVersion(EImportStaticMeshVersion::LastVersion);
	StaticMesh->Build(true);
	StaticMesh->PostEditChange();

	if (!GreenswardEditor::SaveAsset(StaticMesh))
	{
		UE_LOG(LogGreenswardEditor, Warning, TEXT("GenerateGrassMat: could not save %s/%s"), *PackagePath, *AssetName);
	}

	UE_LOG(LogGreenswardEditor, Log, TEXT("Generated %s: %d blades, %d triangles"), *AssetName, S.BladeCount, MeshDescription->Triangles().Num());
	return StaticMesh;
}

int32 UGreenswardEditorLibrary::GenerateMatLibrary(int32 MaxMats)
{
	struct FRecipe
	{
		FString Name;
		FGreenswardMatSettings Settings;
	};
	TArray<FRecipe> Recipes;

	struct FHeightTier
	{
		float Height;
		bool bMown;
		int32 Blades;
		const TCHAR* Family;
	};
	const FHeightTier Tiers[] = {
		{ 2.0f, true, 3600, TEXT("Lawn") },
		{ 3.0f, true, 3400, TEXT("Lawn") },
		{ 4.0f, true, 3200, TEXT("Lawn") },
		{ 5.0f, true, 3000, TEXT("Lawn") },
		{ 7.0f, true, 2800, TEXT("Lawn") },
		{ 10.0f, true, 2600, TEXT("Lawn") },
		{ 15.0f, false, 2300, TEXT("Rough") },
		{ 20.0f, false, 2100, TEXT("Rough") },
		{ 30.0f, false, 1900, TEXT("Rough") },
		{ 45.0f, false, 1700, TEXT("Rough") },
	};
	struct FDensity { const TCHAR* Name; float Scale; };
	const FDensity Densities[] = { { TEXT("Dense"), 1.6f }, { TEXT("Medium"), 1.0f }, { TEXT("Sparse"), 0.5f } };
	const int32 DryPercents[] = { 0, 10, 25, 50 };

	int32 SeedCounter = 100;
	for (const FDensity& Density : Densities)
	{
		for (int32 DryPercent : DryPercents)
		{
			for (const FHeightTier& Tier : Tiers)
			{
				FRecipe& R = Recipes.AddDefaulted_GetRef();
				R.Name = FString::Printf(TEXT("SM_GS_%s_%02dcm_%s_Dry%02d"), Tier.Family, FMath::RoundToInt32(Tier.Height), Density.Name, DryPercent);
				FGreenswardMatSettings& S = R.Settings;
				S.Height = Tier.Height;
				S.bMown = Tier.bMown;
				S.BladeCount = FMath::RoundToInt32(Tier.Blades * Density.Scale);
				S.BladeWidth = 0.32f + Tier.Height * 0.012f;
				S.HeightVariation = Tier.bMown ? 0.3f : 0.45f;
				S.Lean = Tier.bMown ? 24.0f : 16.0f;
				S.Bend = Tier.bMown ? 28.0f : 30.0f + Tier.Height * 0.7f;
				S.DryFraction = DryPercent / 100.0f;
				S.Clumping = Tier.bMown ? 0.3f : 0.55f;
				S.Seed = SeedCounter++;
			}
		}
	}

	// Meadow mats with seed stalks.
	const float MeadowHeights[] = { 25.0f, 35.0f, 50.0f };
	for (float Height : MeadowHeights)
	{
		for (int32 Variant = 0; Variant < 2; ++Variant)
		{
			FRecipe& R = Recipes.AddDefaulted_GetRef();
			R.Name = FString::Printf(TEXT("SM_GS_Meadow_%02dcm_%s"), FMath::RoundToInt32(Height), Variant == 0 ? TEXT("A") : TEXT("B"));
			FGreenswardMatSettings& S = R.Settings;
			S.Height = Height;
			S.bMown = false;
			S.BladeCount = Variant == 0 ? 2000 : 1500;
			S.BladeWidth = 0.34f + Height * 0.011f;
			S.HeightVariation = 0.55f;
			S.Lean = 18.0f;
			S.Bend = 38.0f + Height * 0.6f;
			S.DryFraction = Variant == 0 ? 0.12f : 0.3f;
			S.Clumping = 0.65f;
			S.StalkCount = Variant == 0 ? 55 : 90;
			S.StalkHeightScale = 1.45f;
			S.Seed = SeedCounter++;
		}
	}

	// Putting-green mats: 25 cm, cut to millimetres.
	const float GreenHeights[] = { 0.6f, 0.8f, 1.0f, 1.2f };
	for (float Height : GreenHeights)
	{
		FRecipe& R = Recipes.AddDefaulted_GetRef();
		R.Name = FString::Printf(TEXT("SM_GS_Green_%02dmm"), FMath::RoundToInt32(Height * 10.0f));
		FGreenswardMatSettings& S = R.Settings;
		S.MatSize = 25.0f;
		S.Height = Height;
		S.bMown = true;
		S.BladeCount = 4200;
		S.BladeWidth = 0.28f;
		S.HeightVariation = 0.15f;
		S.Lean = 30.0f;
		S.Bend = 15.0f;
		S.Clumping = 0.1f;
		S.Seed = SeedCounter++;
	}

	const int32 Count = MaxMats > 0 ? FMath::Min(MaxMats, Recipes.Num()) : Recipes.Num();
	FScopedSlowTask Task(Count, NSLOCTEXT("Greensward", "GenerateLibrary", "Generating Greensward grass mats..."));
	Task.MakeDialog();

	int32 Written = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		Task.EnterProgressFrame();
		if (GenerateGrassMat(Recipes[I].Settings, TEXT("/Greensward/Mats"), Recipes[I].Name, nullptr))
		{
			++Written;
		}
	}
	return Written;
}
