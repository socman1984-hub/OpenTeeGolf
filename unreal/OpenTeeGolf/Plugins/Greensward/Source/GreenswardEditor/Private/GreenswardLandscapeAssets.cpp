// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardEditorLibrary.h"
#include "GreenswardMaterialBuilder.h"

#include "Async/ParallelFor.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"

namespace GreenswardEditor
{
	const TArray<FName>& GetLandscapeLayerNames()
	{
		static const TArray<FName> Names = {
			TEXT("Grass"), TEXT("DryGrass"), TEXT("Soil"), TEXT("Mud"), TEXT("Sand"), TEXT("Gravel"), TEXT("Clay")
		};
		return Names;
	}

	void Cellular(float U, float V, int32 Period, int32 Seed, float& OutF1, float& OutF2, float& OutCellRandom)
	{
		const float X = U * Period;
		const float Y = V * Period;
		const int32 XI = FMath::FloorToInt32(X);
		const int32 YI = FMath::FloorToInt32(Y);
		OutF1 = 1e9f;
		OutF2 = 1e9f;
		OutCellRandom = 0.0f;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 CX = XI + DX;
				const int32 CY = YI + DY;
				const int32 WX = ((CX % Period) + Period) % Period;
				const int32 WY = ((CY % Period) + Period) % Period;
				const float PX = CX + Hash2D(WX, WY, Seed);
				const float PY = CY + Hash2D(WX, WY, Seed + 57);
				const float D = FMath::Sqrt(FMath::Square(PX - X) + FMath::Square(PY - Y));
				if (D < OutF1)
				{
					OutF2 = OutF1;
					OutF1 = D;
					OutCellRandom = Hash2D(WX, WY, Seed + 113);
				}
				else if (D < OutF2)
				{
					OutF2 = D;
				}
			}
		}
	}
}

namespace GreenswardLandscapeAssetsPrivate
{
	using namespace GreenswardCorePrivate;

	struct FLayerSample
	{
		float Height = 0.0f;
		FLinearColor Colour = FLinearColor::Black;
	};

	struct FLayerDef
	{
		const TCHAR* Name;
		float TileCm;
		float Roughness;
		float NormalStrength;
		TFunction<FLayerSample(float U, float V)> Shade;
	};

	static float Sat(float V)
	{
		return FMath::Clamp(V, 0.0f, 1.0f);
	}

	static float Smooth(float A, float B, float X)
	{
		return FMath::SmoothStep(A, B, X);
	}

	static FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FMath::Lerp(A, B, Sat(T));
	}

	static FLayerSample ShadeTurf(float U, float V, int32 Seed, bool bDry)
	{
		float F1, F2, R;
		GreenswardEditor::Cellular(U, V, 150, Seed, F1, F2, R);
		const float Tuft = Sat(1.0f - F1 * 1.3f);
		const float Fine = GreenswardEditor::Fbm(U, V, 96, 3, Seed + 6, true);
		const float Patch = GreenswardEditor::Fbm(U, V, 5, 4, Seed + 10, true);

		FLayerSample S;
		S.Height = Sat(Tuft * 0.35f + Fine * 0.65f);
		const float Lit = Smooth(0.25f, 0.7f, S.Height);
		if (bDry)
		{
			S.Colour = Mix(FLinearColor(0.10f, 0.085f, 0.045f), FLinearColor(0.33f, 0.27f, 0.13f), Lit);
			S.Colour = Mix(S.Colour, S.Colour * FLinearColor(0.8f, 0.85f, 0.7f), R * 0.6f);
			S.Colour = Mix(S.Colour, FLinearColor(0.09f, 0.13f, 0.04f), Smooth(0.6f, 0.8f, Patch) * 0.15f);
		}
		else
		{
			S.Colour = Mix(FLinearColor(0.050f, 0.045f, 0.022f), FLinearColor(0.055f, 0.115f, 0.028f), Lit);
			S.Colour = Mix(S.Colour, FLinearColor(0.075f, 0.14f, 0.035f), R * 0.5f * S.Height);
			S.Colour = Mix(S.Colour, FLinearColor(0.12f, 0.12f, 0.055f), Smooth(0.62f, 0.8f, Patch) * 0.12f);
		}
		return S;
	}

	static TArray<FLayerDef> MakeLayers()
	{
		TArray<FLayerDef> Layers;

		Layers.Add({ TEXT("Grass"), 150.0f, 0.82f, 0.8f, [](float U, float V) { return ShadeTurf(U, V, 301, false); } });
		Layers.Add({ TEXT("DryGrass"), 150.0f, 0.86f, 0.8f, [](float U, float V) { return ShadeTurf(U, V, 351, true); } });

		Layers.Add({ TEXT("Soil"), 200.0f, 0.9f, 2.2f, [](float U, float V)
		{
			const float Base = GreenswardEditor::Fbm(U, V, 6, 7, 401, true);
			float F1, F2, R;
			GreenswardEditor::Cellular(U, V, 26, 409, F1, F2, R);
			const float Clod = Sat(1.0f - F1 * 1.2f);
			float P1, P2, PR;
			GreenswardEditor::Cellular(U, V, 70, 419, P1, P2, PR);
			const float Pebble = PR > 0.8f ? Sat(1.0f - P1 * 3.2f) : 0.0f;

			FLayerSample S;
			S.Height = Sat(Base * 0.6f + Clod * 0.3f + Pebble * 0.5f);
			S.Colour = Mix(FLinearColor(0.030f, 0.021f, 0.013f), FLinearColor(0.095f, 0.066f, 0.040f), Smooth(0.2f, 0.8f, Base * 0.7f + Clod * 0.3f));
			S.Colour = Mix(S.Colour, FLinearColor(0.22f, 0.20f, 0.18f) * (0.7f + 0.6f * FMath::Frac(PR * 7.3f)), Pebble * 3.0f);
			return S;
		} });

		Layers.Add({ TEXT("Mud"), 250.0f, 0.5f, 2.6f, [](float U, float V)
		{
			const float Base = GreenswardEditor::Fbm(U, V, 4, 5, 501, true);
			float F1, F2, R;
			GreenswardEditor::Cellular(U, V, 9, 509, F1, F2, R);
			const float Plate = Smooth(0.0f, 0.09f, F2 - F1);

			FLayerSample S;
			S.Height = Sat(0.2f + Base * 0.35f + Plate * 0.45f * (0.6f + 0.4f * R));
			S.Colour = Mix(FLinearColor(0.030f, 0.021f, 0.014f), FLinearColor(0.090f, 0.066f, 0.042f), S.Height);
			return S;
		} });

		Layers.Add({ TEXT("Sand"), 180.0f, 0.88f, 1.2f, [](float U, float V)
		{
			const float Grain = GreenswardEditor::Fbm(U, V, 128, 2, 601, true);
			const float Low = GreenswardEditor::Fbm(U, V, 4, 4, 607, true);
			const float Ripple = 0.5f + 0.5f * FMath::Sin((V * 18.0f + Low * 1.6f) * 2.0f * PI);

			FLayerSample S;
			S.Height = Sat(Grain * 0.35f + Ripple * 0.4f + Low * 0.25f);
			S.Colour = Mix(FLinearColor(0.33f, 0.27f, 0.17f), FLinearColor(0.50f, 0.43f, 0.30f), Grain * 0.5f + Ripple * 0.25f + Low * 0.3f);
			return S;
		} });

		Layers.Add({ TEXT("Gravel"), 120.0f, 0.8f, 3.0f, [](float U, float V)
		{
			float F1, F2, R;
			GreenswardEditor::Cellular(U, V, 30, 701, F1, F2, R);
			const float Stone = FMath::Sqrt(Sat(1.0f - F1 * F1 * 2.6f)) * Smooth(0.0f, 0.12f, F2 - F1);
			float G1, G2, GR;
			GreenswardEditor::Cellular(U, V, 75, 709, G1, G2, GR);
			const float Small = FMath::Sqrt(Sat(1.0f - G1 * G1 * 3.2f)) * 0.45f * Smooth(0.0f, 0.1f, G2 - G1);
			const float Tone = Stone >= Small ? R : GR;
			const float Speckle = GreenswardEditor::Fbm(U, V, 128, 2, 719, true);

			FLayerSample S;
			S.Height = FMath::Max(Stone, Small);
			FLinearColor StoneColour = Mix(FLinearColor(0.08f, 0.075f, 0.07f), FLinearColor(0.25f, 0.24f, 0.22f), Tone);
			if (FMath::Frac(Tone * 7.0f) > 0.6f)
			{
				StoneColour = StoneColour * FLinearColor(1.15f, 1.0f, 0.82f);
			}
			S.Colour = Mix(FLinearColor(0.05f, 0.04f, 0.03f), StoneColour * (0.75f + 0.35f * S.Height) * (0.9f + 0.2f * Speckle), Smooth(0.05f, 0.3f, S.Height));
			return S;
		} });

		Layers.Add({ TEXT("Clay"), 200.0f, 0.9f, 0.9f, [](float U, float V)
		{
			const float Grain = GreenswardEditor::Fbm(U, V, 128, 3, 801, true);
			const float Low = GreenswardEditor::Fbm(U, V, 5, 5, 809, true);
			const float Scuff = Smooth(0.6f, 0.8f, GreenswardEditor::Fbm(U, V, 10, 3, 811, true));

			FLayerSample S;
			S.Height = Sat(Grain * 0.5f + Low * 0.5f);
			S.Colour = Mix(FLinearColor(0.26f, 0.09f, 0.04f), FLinearColor(0.40f, 0.155f, 0.07f), Low * 0.7f + Grain * 0.3f);
			S.Colour = Mix(S.Colour, S.Colour * 1.25f + FLinearColor(0.03f, 0.03f, 0.03f), Scuff * 0.4f);
			return S;
		} });

		return Layers;
	}

	static UTexture2D* SaveTexture(const FString& Name, int32 Size, const TArray<uint8>& Pixels, bool bSRGB, TextureCompressionSettings Compression, TextureGroup Group)
	{
		UTexture2D* Texture = GreenswardEditor::FindOrCreateAsset<UTexture2D>(TextureFolder, Name);
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, Pixels.GetData());
		Texture->SRGB = bSRGB;
		Texture->CompressionSettings = Compression;
		Texture->LODGroup = Group;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->PostEditChange();
		GreenswardEditor::SaveAsset(Texture);
		return Texture;
	}

	/** Bakes one layer to a colour+height texture and a normal map. */
	static void BakeLayer(const FLayerDef& Layer, int32 Size, UTexture2D*& OutColour, UTexture2D*& OutNormal)
	{
		TArray<float> Heights;
		Heights.SetNumUninitialized(Size * Size);
		TArray<uint8> Colour;
		Colour.SetNumUninitialized(Size * Size * 4);

		ParallelFor(Size, [&](int32 Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FLayerSample S = Layer.Shade((X + 0.5f) / Size, (Y + 0.5f) / Size);
				Heights[Y * Size + X] = S.Height;
				const FColor C = S.Colour.GetClamped().ToFColor(true);
				uint8* P = &Colour[(Y * Size + X) * 4];
				P[0] = C.B;
				P[1] = C.G;
				P[2] = C.R;
				P[3] = ToByte(S.Height);
			}
		});

		TArray<uint8> Normal;
		Normal.SetNumUninitialized(Size * Size * 4);
		const float Scale = Layer.NormalStrength * Size / 128.0f;
		ParallelFor(Size, [&](int32 Y)
		{
			const int32 Y0 = (Y + Size - 1) % Size;
			const int32 Y1 = (Y + 1) % Size;
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 X0 = (X + Size - 1) % Size;
				const int32 X1 = (X + 1) % Size;
				const float DX = (Heights[Y * Size + X1] - Heights[Y * Size + X0]) * 0.5f * Scale;
				const float DY = (Heights[Y1 * Size + X] - Heights[Y0 * Size + X]) * 0.5f * Scale;
				const FVector3f N = FVector3f(-DX, -DY, 1.0f).GetSafeNormal();
				uint8* P = &Normal[(Y * Size + X) * 4];
				P[0] = ToByte(N.Z * 0.5f + 0.5f);
				P[1] = ToByte(N.Y * 0.5f + 0.5f);
				P[2] = ToByte(N.X * 0.5f + 0.5f);
				P[3] = 255;
			}
		});

		OutColour = SaveTexture(FString::Printf(TEXT("T_GS_%s_D"), Layer.Name), Size, Colour, true, TC_Default, TEXTUREGROUP_World);
		OutNormal = SaveTexture(FString::Printf(TEXT("T_GS_%s_N"), Layer.Name), Size, Normal, false, TC_Normalmap, TEXTUREGROUP_WorldNormalMap);
	}

	static const TCHAR* FarCode = TEXT(R"(
return saturate((distance(WP, Cam) - Start) / max(Range, 1.0));
)");

	static const TCHAR* LayerColourCode = TEXT(R"(
// Near: full detail. Far: mostly the large-scale sample, which hides the tiling grid.
return lerp(Dn, lerp(Dn, Df, 0.8), Far) * Tint;
)");

	static const TCHAR* FinishCode = TEXT(R"(
float snow = Env.z;
float wet = Env.w;
float3 c = Col;

c *= 1.0 + ((MacroA.r - 0.5) * 0.3 + (MacroB.g - 0.5) * 0.16) * MacroAmt;

float lum = dot(c, float3(0.3, 0.59, 0.11));
float detail = clamp(lum / 0.09, 0.5, 1.6);
c = lerp(c, Overlay * detail, OverlayStrength);

c *= lerp(1.0, 0.6, wet);
float rough = lerp(Rough, 0.25, wet * 0.8);

float snowMask = saturate(snow * 1.6 - 0.2 + (MacroB.r - 0.5) * 0.6) * saturate(snow * 8.0);
snowMask = smoothstep(0.0, 1.0, snowMask);
c = lerp(c, float3(0.86, 0.88, 0.92), snowMask);
rough = lerp(rough, 0.6, snowMask);

return float4(c, rough);
)");

	static void BuildMaterial(UMaterialParameterCollection* MPC, UTexture2D* Noise, const TArray<FLayerDef>& Layers, const TArray<UTexture2D*>& Colours, const TArray<UTexture2D*>& Normals)
	{
		UMaterial* M = GreenswardEditor::FindOrCreateAsset<UMaterial>(MaterialFolder, TEXT("M_Greensward_Landscape"));
		M->PreEditChange(nullptr);
		ClearGraph(M);
		UMaterialEditorOnlyData* Out = M->GetEditorOnlyData();

		UMaterialExpressionWorldPosition* WP = Node<UMaterialExpressionWorldPosition>(M, -3200, 0);
		UMaterialExpressionComponentMask* WPxy = Node<UMaterialExpressionComponentMask>(M, -3000, 0);
		WPxy->R = true; WPxy->G = true; WPxy->B = false; WPxy->A = false;
		WPxy->Input.Connect(0, WP);

		UMaterialExpressionCameraPositionWS* Camera = Node<UMaterialExpressionCameraPositionWS>(M, -3200, 200);
		UMaterialExpressionScalarParameter* FarStart = Scalar(M, TEXT("FarBlendStart"), 1200.0f, TEXT("Distance"), -3200, 300);
		UMaterialExpressionScalarParameter* FarRange = Scalar(M, TEXT("FarBlendRange"), 4000.0f, TEXT("Distance"), -3200, 400);
		UMaterialExpressionCustom* Far = Custom(M, TEXT("Far Blend"), CMOT_Float1, FarCode, -2900, 250);
		In(Far, TEXT("WP"), WP);
		In(Far, TEXT("Cam"), Camera);
		In(Far, TEXT("Start"), FarStart);
		In(Far, TEXT("Range"), FarRange);

		UMaterialExpressionLandscapeLayerBlend* ColourBlend = Node<UMaterialExpressionLandscapeLayerBlend>(M, -1200, 0);
		UMaterialExpressionLandscapeLayerBlend* NormalBlend = Node<UMaterialExpressionLandscapeLayerBlend>(M, -1200, 900);
		UMaterialExpressionLandscapeLayerBlend* RoughBlend = Node<UMaterialExpressionLandscapeLayerBlend>(M, -1200, 1800);
		ColourBlend->Layers.Reset();
		NormalBlend->Layers.Reset();
		RoughBlend->Layers.Reset();
		ColourBlend->Layers.SetNum(Layers.Num());
		NormalBlend->Layers.SetNum(Layers.Num());
		RoughBlend->Layers.SetNum(Layers.Num());

		for (int32 I = 0; I < Layers.Num(); ++I)
		{
			const FLayerDef& Layer = Layers[I];
			const int32 Y = I * 700;
			const FString Group = Layer.Name;

			UMaterialExpressionScalarParameter* Tiling = Scalar(M, *FString::Printf(TEXT("%sTiling"), Layer.Name), 1.0f / Layer.TileCm, *Group, -2700, Y);
			UMaterialExpressionMultiply* UVNear = Node<UMaterialExpressionMultiply>(M, -2500, Y);
			UVNear->A.Connect(0, WPxy);
			UVNear->B.Connect(0, Tiling);
			UMaterialExpressionMultiply* UVFar = Node<UMaterialExpressionMultiply>(M, -2500, Y + 150);
			UVFar->A.Connect(0, UVNear);
			UVFar->ConstB = 0.137f;

			auto Sample = [&](UTexture2D* Texture, UMaterialExpression* UV, int32 OffsetY)
			{
				UMaterialExpressionTextureSample* S = Node<UMaterialExpressionTextureSample>(M, -2250, Y + OffsetY);
				S->Texture = Texture;
				S->AutoSetSampleType();
				S->SamplerSource = SSM_Wrap_WorldGroupSettings;
				S->Coordinates.Connect(0, UV);
				return S;
			};
			UMaterialExpressionTextureSample* ColourNear = Sample(Colours[I], UVNear, 0);
			UMaterialExpressionTextureSample* ColourFar = Sample(Colours[I], UVFar, 220);
			UMaterialExpressionTextureSample* NormalNear = Sample(Normals[I], UVNear, 440);

			UMaterialExpressionVectorParameter* Tint = Vector(M, *FString::Printf(TEXT("%sTint"), Layer.Name), FLinearColor::White, *Group, -2250, Y + 600);
			UMaterialExpressionScalarParameter* Roughness = Scalar(M, *FString::Printf(TEXT("%sRoughness"), Layer.Name), Layer.Roughness, *Group, -1900, Y + 600);

			UMaterialExpressionCustom* LayerColour = Custom(M, *FString::Printf(TEXT("%s Colour"), Layer.Name), CMOT_Float3, LayerColourCode, -1900, Y);
			In(LayerColour, TEXT("Dn"), ColourNear);
			In(LayerColour, TEXT("Df"), ColourFar);
			In(LayerColour, TEXT("Far"), Far);
			In(LayerColour, TEXT("Tint"), Tint);

			// The first layer is the base everywhere; the rest are blended in by their height maps.
			const ELandscapeLayerBlendType BlendType = I == 0 ? LB_WeightBlend : LB_HeightBlend;
			auto Fill = [&](UMaterialExpressionLandscapeLayerBlend* Blend, UMaterialExpression* Input)
			{
				FLayerBlendInput& L = Blend->Layers[I];
				L.LayerName = Layer.Name;
				L.BlendType = BlendType;
				L.PreviewWeight = I == 0 ? 1.0f : 0.0f;
				L.LayerInput.Connect(0, Input);
				if (BlendType == LB_HeightBlend)
				{
					L.HeightInput.Connect(4, ColourNear);
				}
			};
			Fill(ColourBlend, LayerColour);
			Fill(NormalBlend, NormalNear);
			Fill(RoughBlend, Roughness);
		}

		// Macro variation, overlay image, weather.
		UMaterialExpressionScalarParameter* MacroTiling = Scalar(M, TEXT("MacroTiling"), 0.0004f, TEXT("Variation"), -1200, 2700);
		UMaterialExpressionScalarParameter* MacroAmt = Scalar(M, TEXT("MacroVariation"), 0.6f, TEXT("Variation"), -1200, 2800);
		UMaterialExpressionMultiply* UVA = Node<UMaterialExpressionMultiply>(M, -1000, 2700);
		UVA->A.Connect(0, WPxy);
		UVA->B.Connect(0, MacroTiling);
		UMaterialExpressionMultiply* UVB = Node<UMaterialExpressionMultiply>(M, -1000, 2850);
		UVB->A.Connect(0, UVA);
		UVB->ConstB = 5.3f;
		UMaterialExpressionTextureSample* MacroA = Node<UMaterialExpressionTextureSample>(M, -800, 2700);
		MacroA->Texture = Noise;
		MacroA->AutoSetSampleType();
		MacroA->SamplerSource = SSM_Wrap_WorldGroupSettings;
		MacroA->Coordinates.Connect(0, UVA);
		UMaterialExpressionTextureSample* MacroB = Node<UMaterialExpressionTextureSample>(M, -800, 2950);
		MacroB->Texture = Noise;
		MacroB->AutoSetSampleType();
		MacroB->SamplerSource = SSM_Wrap_WorldGroupSettings;
		MacroB->Coordinates.Connect(0, UVB);

		UMaterialExpressionVectorParameter* OverlayOrigin = Vector(M, TEXT("OverlayOrigin"), FLinearColor(-25000.0f, -25000.0f, 0.0f, 0.0f), TEXT("Overlay"), -1200, 3200);
		UMaterialExpressionVectorParameter* OverlaySize = Vector(M, TEXT("OverlaySize"), FLinearColor(50000.0f, 50000.0f, 0.0f, 0.0f), TEXT("Overlay"), -1200, 3400);
		UMaterialExpressionScalarParameter* OverlayStrength = Scalar(M, TEXT("OverlayStrength"), 0.0f, TEXT("Overlay"), -1200, 3600);
		UMaterialExpressionCustom* OverlayUV = Custom(M, TEXT("Overlay UV"), CMOT_Float2, TEXT("return (WP.xy - Origin.xy) / max(Size.xy, float2(1.0, 1.0));"), -1000, 3200);
		In(OverlayUV, TEXT("WP"), WP);
		In(OverlayUV, TEXT("Origin"), OverlayOrigin);
		In(OverlayUV, TEXT("Size"), OverlaySize);
		UMaterialExpressionTextureSampleParameter2D* Overlay = Node<UMaterialExpressionTextureSampleParameter2D>(M, -800, 3200);
		Overlay->ParameterName = TEXT("OverlayTexture");
		Overlay->Group = TEXT("Overlay");
		Overlay->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		Overlay->AutoSetSampleType();
		Overlay->SamplerSource = SSM_Wrap_WorldGroupSettings;
		Overlay->Coordinates.Connect(0, OverlayUV);

		UMaterialExpressionCollectionParameter* Env = Collection(M, MPC, TEXT("Environment"), -800, 3500);

		UMaterialExpressionCustom* Finish = Custom(M, TEXT("Ground Finish"), CMOT_Float4, FinishCode, -500, 600);
		In(Finish, TEXT("Col"), ColourBlend);
		In(Finish, TEXT("Rough"), RoughBlend);
		In(Finish, TEXT("Env"), Env);
		In(Finish, TEXT("MacroA"), MacroA);
		In(Finish, TEXT("MacroB"), MacroB);
		In(Finish, TEXT("MacroAmt"), MacroAmt);
		In(Finish, TEXT("Overlay"), Overlay);
		In(Finish, TEXT("OverlayStrength"), OverlayStrength);

		UMaterialExpressionComponentMask* RGB = Node<UMaterialExpressionComponentMask>(M, -250, 500);
		RGB->R = true; RGB->G = true; RGB->B = true; RGB->A = false;
		RGB->Input.Connect(0, Finish);
		UMaterialExpressionComponentMask* Rough = Node<UMaterialExpressionComponentMask>(M, -250, 650);
		Rough->R = false; Rough->G = false; Rough->B = false; Rough->A = true;
		Rough->Input.Connect(0, Finish);
		UMaterialExpressionConstant* Specular = Node<UMaterialExpressionConstant>(M, -250, 800);
		Specular->R = 0.3f;

		Out->BaseColor.Connect(0, RGB);
		Out->Roughness.Connect(0, Rough);
		Out->Specular.Connect(0, Specular);
		Out->Normal.Connect(0, NormalBlend);

		M->PostEditChange();
		UMaterialEditingLibrary::RecompileMaterial(M);
		GreenswardEditor::SaveAsset(M);

		UMaterialInstanceConstant* MI = GreenswardEditor::FindOrCreateAsset<UMaterialInstanceConstant>(MaterialFolder, TEXT("MI_Greensward_Landscape"));
		MI->PreEditChange(nullptr);
		MI->SetParentEditorOnly(M);
		MI->PostEditChange();
		GreenswardEditor::SaveAsset(MI);
	}
}

void GreenswardEditor::BuildLandscapeAssets(UMaterialParameterCollection* Collection, UTexture2D* Noise)
{
	using namespace GreenswardLandscapeAssetsPrivate;

	const TArray<FLayerDef> Layers = MakeLayers();
	check(Layers.Num() == GetLandscapeLayerNames().Num());

	TArray<UTexture2D*> Colours;
	TArray<UTexture2D*> Normals;
	for (const FLayerDef& Layer : Layers)
	{
		UTexture2D* Colour = nullptr;
		UTexture2D* Normal = nullptr;
		BakeLayer(Layer, 1024, Colour, Normal);
		Colours.Add(Colour);
		Normals.Add(Normal);

		ULandscapeLayerInfoObject* Info = FindOrCreateAsset<ULandscapeLayerInfoObject>(TEXT("/Greensward/Landscape"), FString::Printf(TEXT("LI_GS_%s"), Layer.Name));
		Info->SetLayerName(FName(Layer.Name), false);
		Info->SetBlendMethod(ELandscapeTargetLayerBlendMethod::FinalWeightBlending, false);
		SaveAsset(Info);
	}

	BuildMaterial(Collection, Noise, Layers, Colours, Normals);
	UE_LOG(LogGreenswardEditor, Log, TEXT("Greensward landscape assets built."));
}
