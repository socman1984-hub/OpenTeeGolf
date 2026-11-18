// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardEditorLibrary.h"
#include "GreenswardMaterialBuilder.h"
#include "GreenswardPitchMarking.h"

#include "Async/ParallelFor.h"

namespace GreenswardMarkingsPrivate
{
	using namespace GreenswardCorePrivate;

	static const TCHAR* MarkingFolder = TEXT("/Greensward/Markings");

	/** A coverage image of painted lines. Coordinates in metres, origin at the centre, X along the length. */
	struct FMarkingCanvas
	{
		int32 Width = 0;
		int32 Height = 0;
		FVector2f Extent = FVector2f(1, 1);
		float AA = 0.01f;
		TArray<float> Coverage;

		FMarkingCanvas(int32 InWidth, int32 InHeight, const FVector2D& InExtent)
			: Width(InWidth)
			, Height(InHeight)
			, Extent(static_cast<float>(InExtent.X), static_cast<float>(InExtent.Y))
		{
			Coverage.SetNumZeroed(Width * Height);
			AA = FMath::Max(Extent.X / Width, Extent.Y / Height);
		}

		FVector2f PixelToMetres(int32 X, int32 Y) const
		{
			return FVector2f(((X + 0.5f) / Width - 0.5f) * Extent.X, ((Y + 0.5f) / Height - 0.5f) * Extent.Y);
		}

		template <typename DistanceFn>
		void Paint(const FVector2f& Min, const FVector2f& Max, float HalfWidth, DistanceFn&& Distance)
		{
			const float Pad = HalfWidth + AA * 2.0f;
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32(((Min.X - Pad) / Extent.X + 0.5f) * Width), 0, Width - 1);
			const int32 X1 = FMath::Clamp(FMath::CeilToInt32(((Max.X + Pad) / Extent.X + 0.5f) * Width), 0, Width - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(((Min.Y - Pad) / Extent.Y + 0.5f) * Height), 0, Height - 1);
			const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(((Max.Y + Pad) / Extent.Y + 0.5f) * Height), 0, Height - 1);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const float D = Distance(PixelToMetres(X, Y));
					const float C = FMath::Clamp(0.5f + (HalfWidth - D) / AA, 0.0f, 1.0f);
					float& Pixel = Coverage[Y * Width + X];
					Pixel = FMath::Max(Pixel, C);
				}
			}
		}

		void Line(const FVector2f& A, const FVector2f& B, float LineWidth)
		{
			const FVector2f AB = B - A;
			const float LengthSq = AB.SizeSquared();
			Paint(FVector2f(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2f(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)), LineWidth * 0.5f,
				[&](const FVector2f& P)
				{
					const float T = LengthSq > 0.0f ? FMath::Clamp(FVector2f::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f) : 0.0f;
					return (P - (A + AB * T)).Size();
				});
		}

		void Disc(const FVector2f& Centre, float Radius)
		{
			Line(Centre, Centre, Radius * 2.0f);
		}

		void Box(const FVector2f& Min, const FVector2f& Max, float LineWidth)
		{
			Line(FVector2f(Min.X, Min.Y), FVector2f(Max.X, Min.Y), LineWidth);
			Line(FVector2f(Max.X, Min.Y), FVector2f(Max.X, Max.Y), LineWidth);
			Line(FVector2f(Max.X, Max.Y), FVector2f(Min.X, Max.Y), LineWidth);
			Line(FVector2f(Min.X, Max.Y), FVector2f(Min.X, Min.Y), LineWidth);
		}

		/** Arc from StartDegrees counter-clockwise through SweepDegrees. */
		void Arc(const FVector2f& Centre, float Radius, float StartDegrees, float SweepDegrees, float LineWidth)
		{
			const float Start = FMath::DegreesToRadians(StartDegrees);
			const float Sweep = FMath::DegreesToRadians(FMath::Clamp(SweepDegrees, 0.0f, 360.0f));
			const FVector2f EndA = Centre + FVector2f(FMath::Cos(Start), FMath::Sin(Start)) * Radius;
			const FVector2f EndB = Centre + FVector2f(FMath::Cos(Start + Sweep), FMath::Sin(Start + Sweep)) * Radius;
			Paint(Centre - FVector2f(Radius, Radius), Centre + FVector2f(Radius, Radius), LineWidth * 0.5f,
				[&](const FVector2f& P)
				{
					const FVector2f D = P - Centre;
					float Angle = FMath::Atan2(D.Y, D.X) - Start;
					Angle = FMath::Fmod(Angle, 2.0f * PI);
					if (Angle < 0.0f)
					{
						Angle += 2.0f * PI;
					}
					if (Angle <= Sweep)
					{
						return FMath::Abs(D.Size() - Radius);
					}
					return FMath::Min((P - EndA).Size(), (P - EndB).Size());
				});
		}

		void Dashed(const FVector2f& A, const FVector2f& B, float LineWidth, float Dash, float Gap)
		{
			const float Length = (B - A).Size();
			const FVector2f Dir = (B - A) / FMath::Max(Length, 1e-4f);
			for (float T = 0.0f; T < Length; T += Dash + Gap)
			{
				Line(A + Dir * T, A + Dir * FMath::Min(T + Dash, Length), LineWidth);
			}
		}

		/** A seven-segment style digit, 1 unit wide and 2 units tall before scaling. Origin at its bottom-left. */
		void Digit(int32 Value, const FVector2f& Origin, const FVector2f& AxisX, const FVector2f& AxisY, float DigitWidth, float DigitHeight, float LineWidth)
		{
			static const TCHAR* Segments[10] = {
				TEXT("abcdef"), TEXT("bc"), TEXT("abged"), TEXT("abgcd"), TEXT("fgbc"),
				TEXT("afgcd"), TEXT("afgedc"), TEXT("abc"), TEXT("abcdefg"), TEXT("abcdfg")
			};
			auto At = [&](float X, float Y)
			{
				return Origin + AxisX * (X * DigitWidth) + AxisY * (Y * DigitHeight * 0.5f);
			};
			const FString Code = Segments[FMath::Clamp(Value, 0, 9)];
			for (TCHAR Segment : Code)
			{
				switch (Segment)
				{
				case TEXT('a'): Line(At(0, 2), At(1, 2), LineWidth); break;
				case TEXT('b'): Line(At(1, 2), At(1, 1), LineWidth); break;
				case TEXT('c'): Line(At(1, 1), At(1, 0), LineWidth); break;
				case TEXT('d'): Line(At(0, 0), At(1, 0), LineWidth); break;
				case TEXT('e'): Line(At(0, 0), At(0, 1), LineWidth); break;
				case TEXT('f'): Line(At(0, 1), At(0, 2), LineWidth); break;
				case TEXT('g'): Line(At(0, 1), At(1, 1), LineWidth); break;
				default: break;
				}
			}
		}
	};

	static void DrawSoccerFull(FMarkingCanvas& C)
	{
		const float W = 0.12f;
		const float L = 52.5f;
		const float H = 34.0f;
		C.Box(FVector2f(-L, -H), FVector2f(L, H), W);
		C.Line(FVector2f(0, -H), FVector2f(0, H), W);
		C.Arc(FVector2f(0, 0), 9.15f, 0.0f, 360.0f, W);
		C.Disc(FVector2f(0, 0), 0.15f);
		for (float Side : { -1.0f, 1.0f })
		{
			const float Goal = Side * L;
			// Penalty area, goal area, penalty spot.
			C.Line(FVector2f(Goal, -20.16f), FVector2f(Goal - Side * 16.5f, -20.16f), W);
			C.Line(FVector2f(Goal - Side * 16.5f, -20.16f), FVector2f(Goal - Side * 16.5f, 20.16f), W);
			C.Line(FVector2f(Goal - Side * 16.5f, 20.16f), FVector2f(Goal, 20.16f), W);
			C.Line(FVector2f(Goal, -9.16f), FVector2f(Goal - Side * 5.5f, -9.16f), W);
			C.Line(FVector2f(Goal - Side * 5.5f, -9.16f), FVector2f(Goal - Side * 5.5f, 9.16f), W);
			C.Line(FVector2f(Goal - Side * 5.5f, 9.16f), FVector2f(Goal, 9.16f), W);
			const FVector2f Spot(Goal - Side * 11.0f, 0.0f);
			C.Disc(Spot, 0.15f);
			// The part of the 9.15 m circle around the spot that lies outside the penalty area.
			const float Half = FMath::RadiansToDegrees(FMath::Acos(5.5f / 9.15f));
			C.Arc(Spot, 9.15f, (Side > 0.0f ? 180.0f : 0.0f) - Half, Half * 2.0f, W);
			// Corner arcs.
			C.Arc(FVector2f(Goal, -H), 1.0f, Side > 0.0f ? 90.0f : 0.0f, 90.0f, W);
			C.Arc(FVector2f(Goal, H), 1.0f, Side > 0.0f ? 180.0f : 270.0f, 90.0f, W);
		}
	}

	static void DrawSoccerSmall(FMarkingCanvas& C)
	{
		const float W = 0.08f;
		const float L = 20.0f;
		const float H = 10.0f;
		C.Box(FVector2f(-L, -H), FVector2f(L, H), W);
		C.Line(FVector2f(0, -H), FVector2f(0, H), W);
		C.Arc(FVector2f(0, 0), 3.0f, 0.0f, 360.0f, W);
		C.Disc(FVector2f(0, 0), 0.1f);
		for (float Side : { -1.0f, 1.0f })
		{
			const float Goal = Side * L;
			C.Arc(FVector2f(Goal, 0), 6.0f, Side > 0.0f ? 90.0f : 270.0f, 180.0f, W);
			C.Disc(FVector2f(Goal - Side * 4.5f, 0.0f), 0.1f);
		}
	}

	static void DrawAmericanFootball(FMarkingCanvas& C)
	{
		const float Yard = 0.9144f;
		const float W = 0.10f;
		const float L = 60.0f * Yard;
		const float H = 26.6667f * Yard;
		C.Box(FVector2f(-L, -H), FVector2f(L, H), W * 1.5f);
		// Goal lines and five-yard lines.
		for (int32 Y = -50; Y <= 50; Y += 5)
		{
			C.Line(FVector2f(Y * Yard, -H), FVector2f(Y * Yard, H), FMath::Abs(Y) == 50 ? W * 2.0f : W);
		}
		// One-yard marks at the sidelines and the inbounds (hash) lines.
		const float Hash = 3.0833f * Yard;
		const float Mark = 0.61f;
		for (int32 Y = -49; Y <= 49; ++Y)
		{
			if (Y % 5 == 0)
			{
				continue;
			}
			const float X = Y * Yard;
			C.Line(FVector2f(X, -H + 0.15f), FVector2f(X, -H + 0.15f + Mark), W);
			C.Line(FVector2f(X, H - 0.15f), FVector2f(X, H - 0.15f - Mark), W);
			C.Line(FVector2f(X, -Hash), FVector2f(X, -Hash - Mark), W);
			C.Line(FVector2f(X, Hash), FVector2f(X, Hash + Mark), W);
		}
		// Yard numbers, readable from each sideline.
		const float DigitW = 1.2f;
		const float DigitH = 1.83f;
		const float Gap = 0.5f;
		for (int32 Y = -40; Y <= 40; Y += 10)
		{
			const int32 Tens = (50 - FMath::Abs(Y)) / 10;
			const float X = Y * Yard;
			const float Inset = 11.0f;
			// Near sideline (-Y): upright with +Y up.
			C.Digit(Tens, FVector2f(X - Gap - DigitW, -H + Inset), FVector2f(1, 0), FVector2f(0, 1), DigitW, DigitH, 0.25f);
			C.Digit(0, FVector2f(X + Gap, -H + Inset), FVector2f(1, 0), FVector2f(0, 1), DigitW, DigitH, 0.25f);
			// Far sideline (+Y): turned half a turn.
			C.Digit(Tens, FVector2f(X + Gap + DigitW, H - Inset), FVector2f(-1, 0), FVector2f(0, -1), DigitW, DigitH, 0.25f);
			C.Digit(0, FVector2f(X - Gap, H - Inset), FVector2f(-1, 0), FVector2f(0, -1), DigitW, DigitH, 0.25f);
		}
	}

	static void DrawRugby(FMarkingCanvas& C, bool bLeague)
	{
		const float W = 0.10f;
		const float L = 50.0f;
		const float H = bLeague ? 34.0f : 35.0f;
		const float InGoal = bLeague ? 8.0f : 10.0f;
		C.Box(FVector2f(-L - InGoal, -H), FVector2f(L + InGoal, H), W);
		C.Line(FVector2f(-L, -H), FVector2f(-L, H), W * 1.5f);
		C.Line(FVector2f(L, -H), FVector2f(L, H), W * 1.5f);
		C.Line(FVector2f(0, -H), FVector2f(0, H), W);
		if (bLeague)
		{
			for (int32 X = -40; X <= 40; X += 10)
			{
				C.Line(FVector2f(static_cast<float>(X), -H), FVector2f(static_cast<float>(X), H), W);
			}
			for (float Side : { -1.0f, 1.0f })
			{
				C.Dashed(FVector2f(-L, Side * (H - 10.0f)), FVector2f(L, Side * (H - 10.0f)), W, 2.0f, 8.0f);
				C.Dashed(FVector2f(-L, Side * (H - 20.0f)), FVector2f(L, Side * (H - 20.0f)), W, 2.0f, 8.0f);
			}
		}
		else
		{
			for (float Side : { -1.0f, 1.0f })
			{
				C.Line(FVector2f(Side * 28.0f, -H), FVector2f(Side * 28.0f, H), W);
				C.Dashed(FVector2f(Side * 10.0f, -H), FVector2f(Side * 10.0f, H), W, 5.0f, 5.0f);
				C.Dashed(FVector2f(Side * 45.0f, -H + 2.5f), FVector2f(Side * 45.0f, H - 2.5f), W, 5.0f, 5.0f);
				C.Dashed(FVector2f(-L + 2.5f, Side * (H - 5.0f)), FVector2f(L - 2.5f, Side * (H - 5.0f)), W, 5.0f, 5.0f);
				C.Dashed(FVector2f(-L + 2.5f, Side * (H - 15.0f)), FVector2f(L - 2.5f, Side * (H - 15.0f)), W, 5.0f, 5.0f);
			}
		}
	}

	static void DrawTennisCourt(FMarkingCanvas& C, float OffsetY)
	{
		const float W = 0.05f;
		const float L = 11.885f;
		const float Doubles = 5.485f;
		const float Singles = 4.115f;
		const float Service = 6.40f;
		C.Box(FVector2f(-L, OffsetY - Doubles), FVector2f(L, OffsetY + Doubles), W);
		// Baselines are painted wider.
		C.Line(FVector2f(-L, OffsetY - Doubles), FVector2f(-L, OffsetY + Doubles), 0.10f);
		C.Line(FVector2f(L, OffsetY - Doubles), FVector2f(L, OffsetY + Doubles), 0.10f);
		C.Line(FVector2f(-L, OffsetY - Singles), FVector2f(L, OffsetY - Singles), W);
		C.Line(FVector2f(-L, OffsetY + Singles), FVector2f(L, OffsetY + Singles), W);
		C.Line(FVector2f(-Service, OffsetY - Singles), FVector2f(-Service, OffsetY + Singles), W);
		C.Line(FVector2f(Service, OffsetY - Singles), FVector2f(Service, OffsetY + Singles), W);
		C.Line(FVector2f(-Service, OffsetY), FVector2f(Service, OffsetY), W);
		C.Line(FVector2f(-L, OffsetY), FVector2f(-L + 0.10f, OffsetY), W);
		C.Line(FVector2f(L, OffsetY), FVector2f(L - 0.10f, OffsetY), W);
	}

	static void DrawHockey(FMarkingCanvas& C)
	{
		const float W = 0.075f;
		const float L = 45.7f;
		const float H = 27.5f;
		C.Box(FVector2f(-L, -H), FVector2f(L, H), W);
		C.Line(FVector2f(0, -H), FVector2f(0, H), W);
		for (float Side : { -1.0f, 1.0f })
		{
			const float Goal = Side * L;
			C.Line(FVector2f(Goal - Side * 22.9f, -H), FVector2f(Goal - Side * 22.9f, H), W);
			// Shooting circle: two quarter circles around the goalposts joined by a straight line.
			const float R = 14.63f;
			const float Post = 1.83f;
			C.Arc(FVector2f(Goal, Post), R, Side > 0.0f ? 90.0f : 0.0f, 90.0f, W);
			C.Arc(FVector2f(Goal, -Post), R, Side > 0.0f ? 180.0f : 270.0f, 90.0f, W);
			C.Line(FVector2f(Goal - Side * R, -Post), FVector2f(Goal - Side * R, Post), W);
			C.Disc(FVector2f(Goal - Side * 6.4f, 0.0f), 0.1f);
		}
	}

	static UTexture2D* SaveMask(const FString& Name, const FMarkingCanvas& Canvas)
	{
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(Canvas.Width * Canvas.Height);
		// A decal facing down maps its U axis against world X, so store the image mirrored to read correctly on the ground.
		for (int32 Y = 0; Y < Canvas.Height; ++Y)
		{
			for (int32 X = 0; X < Canvas.Width; ++X)
			{
				Pixels[Y * Canvas.Width + X] = ToByte(Canvas.Coverage[Y * Canvas.Width + (Canvas.Width - 1 - X)]);
			}
		}

		UTexture2D* Texture = GreenswardEditor::FindOrCreateAsset<UTexture2D>(MarkingFolder, Name);
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Canvas.Width, Canvas.Height, 1, 1, TSF_G8, Pixels.GetData());
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_Alpha;
		Texture->LODGroup = TEXTUREGROUP_World;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->PostEditChange();
		GreenswardEditor::SaveAsset(Texture);
		return Texture;
	}

	static UMaterial* BuildMarkingMaterial(UTexture2D* Noise)
	{
		UMaterial* M = GreenswardEditor::FindOrCreateAsset<UMaterial>(MarkingFolder, TEXT("M_Greensward_Marking"));
		M->PreEditChange(nullptr);
		ClearGraph(M);
		M->MaterialDomain = MD_DeferredDecal;
		M->BlendMode = BLEND_Translucent;
		UMaterialEditorOnlyData* Out = M->GetEditorOnlyData();

		UMaterialExpressionTextureCoordinate* UV = Node<UMaterialExpressionTextureCoordinate>(M, -1400, 0);

		UMaterialExpressionTextureSampleParameter2D* Mask = Node<UMaterialExpressionTextureSampleParameter2D>(M, -1100, 0);
		Mask->ParameterName = TEXT("Mask");
		Mask->Group = TEXT("Marking");
		Mask->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		Mask->AutoSetSampleType();
		Mask->Coordinates.Connect(0, UV);

		UMaterialExpressionScalarParameter* WearTiling = Scalar(M, TEXT("WearTiling"), 6.0f, TEXT("Marking"), -1400, 300);
		UMaterialExpressionMultiply* WearUV = Node<UMaterialExpressionMultiply>(M, -1250, 250);
		WearUV->A.Connect(0, UV);
		WearUV->B.Connect(0, WearTiling);
		UMaterialExpressionTextureSample* Wear = Node<UMaterialExpressionTextureSample>(M, -1100, 250);
		Wear->Texture = Noise;
		Wear->AutoSetSampleType();
		Wear->Coordinates.Connect(0, WearUV);

		UMaterialExpressionVectorParameter* Colour = Vector(M, TEXT("LineColour"), FLinearColor(0.82f, 0.82f, 0.8f), TEXT("Marking"), -800, -200);
		UMaterialExpressionScalarParameter* Opacity = Scalar(M, TEXT("Opacity"), 0.95f, TEXT("Marking"), -1100, 500);
		UMaterialExpressionScalarParameter* WearAmount = Scalar(M, TEXT("Wear"), 0.35f, TEXT("Marking"), -1100, 600);

		UMaterialExpressionCustom* Alpha = Custom(M, TEXT("Paint Coverage"), CMOT_Float1, TEXT(R"(
// Paint thins out in patches where the pitch has been played on.
float worn = smoothstep(0.3, 0.7, Noise.r * 0.6 + Noise.g * 0.4);
return saturate(Mask * Opacity * lerp(1.0, 0.25 + 0.75 * worn, Wear));
)"), -800, 200);
		In(Alpha, TEXT("Mask"), Mask, 1);
		In(Alpha, TEXT("Noise"), Wear);
		In(Alpha, TEXT("Opacity"), Opacity);
		In(Alpha, TEXT("Wear"), WearAmount);

		UMaterialExpressionConstant* Roughness = Node<UMaterialExpressionConstant>(M, -800, 450);
		Roughness->R = 0.8f;

		Out->BaseColor.Connect(0, Colour);
		Out->Opacity.Connect(0, Alpha);
		Out->Roughness.Connect(0, Roughness);

		M->PostEditChange();
		UMaterialEditingLibrary::RecompileMaterial(M);
		GreenswardEditor::SaveAsset(M);
		return M;
	}
}

void GreenswardEditor::BuildMarkingAssets(UTexture2D* Noise)
{
	using namespace GreenswardMarkingsPrivate;

	UMaterial* Material = BuildMarkingMaterial(Noise);

	const EGreenswardMarking All[] = {
		EGreenswardMarking::SoccerFull, EGreenswardMarking::SoccerSmall, EGreenswardMarking::AmericanFootball,
		EGreenswardMarking::RugbyUnion, EGreenswardMarking::RugbyLeague, EGreenswardMarking::TennisSingle,
		EGreenswardMarking::TennisTriple, EGreenswardMarking::Hockey
	};

	for (EGreenswardMarking Marking : All)
	{
		const FVector2D Extent = GreenswardMarkings::GetExtentMetres(Marking);
		const bool bSmall = Extent.X < 50.0;
		// Twice as many pixels along the length as across: about 2.7 cm per pixel on a full-size pitch.
		FMarkingCanvas Canvas(bSmall ? 2048 : 4096, bSmall ? 1024 : 2048, Extent);
		switch (Marking)
		{
		case EGreenswardMarking::SoccerFull: DrawSoccerFull(Canvas); break;
		case EGreenswardMarking::SoccerSmall: DrawSoccerSmall(Canvas); break;
		case EGreenswardMarking::AmericanFootball: DrawAmericanFootball(Canvas); break;
		case EGreenswardMarking::RugbyUnion: DrawRugby(Canvas, false); break;
		case EGreenswardMarking::RugbyLeague: DrawRugby(Canvas, true); break;
		case EGreenswardMarking::TennisSingle: DrawTennisCourt(Canvas, 0.0f); break;
		case EGreenswardMarking::TennisTriple:
			DrawTennisCourt(Canvas, -17.0f);
			DrawTennisCourt(Canvas, 0.0f);
			DrawTennisCourt(Canvas, 17.0f);
			break;
		case EGreenswardMarking::Hockey: DrawHockey(Canvas); break;
		}

		const FString Suffix = GreenswardMarkings::GetAssetSuffix(Marking);
		UTexture2D* Mask = SaveMask(FString::Printf(TEXT("T_GS_Marking_%s"), *Suffix), Canvas);

		UMaterialInstanceConstant* MI = FindOrCreateAsset<UMaterialInstanceConstant>(MarkingFolder, FString::Printf(TEXT("MI_GS_Marking_%s"), *Suffix));
		MI->PreEditChange(nullptr);
		MI->SetParentEditorOnly(Material);
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("Mask"), Mask);
		MI->PostEditChange();
		UMaterialEditingLibrary::UpdateMaterialInstance(MI);
		SaveAsset(MI);
	}
	UE_LOG(LogGreenswardEditor, Log, TEXT("Greensward marking assets built."));
}
