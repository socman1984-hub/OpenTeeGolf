// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardEditorLibrary.h"
#include "GreenswardEditorPrivate.h"

#include "GreenswardMaterialBuilder.h"

namespace GreenswardCorePrivate
{
	static UMaterialParameterCollection* BuildCollection()
	{
		UMaterialParameterCollection* MPC = GreenswardEditor::FindOrCreateAsset<UMaterialParameterCollection>(MaterialFolder, TEXT("MPC_Greensward"));
		auto Ensure = [MPC](const TCHAR* Name, const FLinearColor& Default)
		{
			for (const FCollectionVectorParameter& Existing : MPC->VectorParameters)
			{
				if (Existing.ParameterName == Name)
				{
					return;
				}
			}
			FCollectionVectorParameter P;
			P.ParameterName = Name;
			P.DefaultValue = Default;
			P.Id = FGuid::NewGuid();
			MPC->VectorParameters.Add(P);
		};
		MPC->PreEditChange(nullptr);
		// Every parameter needs its own id; materials look parameters up by id.
		TSet<FGuid> SeenIds;
		for (FCollectionVectorParameter& Existing : MPC->VectorParameters)
		{
			if (!Existing.Id.IsValid() || SeenIds.Contains(Existing.Id))
			{
				Existing.Id = FGuid::NewGuid();
			}
			SeenIds.Add(Existing.Id);
		}
		Ensure(TEXT("Wind"), FLinearColor(1.0f, 0.0f, 0.3f, 1.0f));
		Ensure(TEXT("Environment"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		MPC->PostEditChange();
		GreenswardEditor::SaveAsset(MPC);
		return MPC;
	}


	static const TCHAR* StripeCode = TEXT(R"(
// Values from the lawn actor (custom primitive data) win; otherwise use the material instance values.
bool prim = Mode > 0.5;
float mode = prim ? Mode : ModeI;
float a = radians(prim ? Angle : AngleI);
float con = prim ? Contrast : ContrastI;
float2 dir = float2(cos(a), sin(a));
float2 perp = float2(-dir.y, dir.x);
float w = max(prim ? Width : WidthI, 1.0);
float u = dot(WP.xy, perp) / w;
float v = dot(WP.xy, dir) / w;
float e = 0.03;
float su = abs(frac(u * 0.5) * 2.0 - 1.0);
float sv = abs(frac(v * 0.5) * 2.0 - 1.0);
float s1 = smoothstep(0.5 - e, 0.5 + e, su) * 2.0 - 1.0;
float s2 = smoothstep(0.5 - e, 0.5 + e, sv) * 2.0 - 1.0;
if (mode < 0.5) return 0.0;
if (mode < 1.5) return s1 * con;
return s1 * s2 * con;
)");

	static const TCHAR* WindCode = TEXT(R"(
float2 d = normalize(Wind.xy + float2(1e-4, 0.0));
float2 side = float2(-d.y, d.x);
float strength = Wind.z;
float t = T * Wind.w;
float ph = dot(WP.xy, d) * 0.0042 - t * 1.1;
float gust = sin(ph) * 0.5 + 0.5;
gust *= sin(ph * 0.37 + dot(WP.xy, side) * 0.0027 + 1.7) * 0.5 + 0.5;
float ripple = sin(dot(WP.xy, d) * 0.031 - t * 3.4 + Rnd * 2.0) * 0.5 + 0.5;
float flutter = sin(T * (5.0 + Rnd * 3.0) + Rnd * 40.0 + dot(WP.xy, float2(0.11, 0.07)));
float m = Mask * Mask;
float amp = H * 100.0 * m;
float bend = strength * (0.25 + 0.75 * gust + 0.25 * ripple * gust);
float2 xy = d * bend * amp * 0.62;
xy += (side * 0.07 + d * 0.05) * flutter * strength * amp;
float sa = radians(Mode > 0.5 ? Angle : AngleI);
xy += float2(cos(sa), sin(sa)) * Stripe * Lean * amp;
float drop = -dot(xy, xy) / max(amp * 2.2, 0.5);
return float3(xy, drop);
)");

	static const TCHAR* OverlayUVCode = TEXT(R"(
return (WP.xy - Origin.xy) / max(Size.xy, float2(1.0, 1.0));
)");

	static const TCHAR* ColourCode = TEXT(R"(
float season = Env.x;
float dryness = Env.y;
float snow = Env.z;
float wet = Env.w;

float3 c = lerp(RootCol, TipCol, pow(saturate(T), 0.75));
float avgLum = max(dot(lerp(RootCol, TipCol, 0.6), float3(0.3, 0.59, 0.11)), 1e-3);

// Per-blade variation.
float r = Rnd * 2.0 - 1.0;
c *= 1.0 + r * Micro * 0.45;
c = lerp(c, c * float3(1.12, 1.0, 0.7), saturate(r) * Micro);
c = lerp(c, c * float3(0.82, 1.0, 0.92), saturate(-r) * Micro);

// Blade anatomy: a darker midrib, and tips that bleach slightly where they were cut or weathered.
float rib = abs(UV.x - 0.5) * 2.0;
c *= lerp(0.8, 1.05, smoothstep(0.0, 0.4, rib));
c = lerp(c, c * float3(1.18, 1.06, 0.72), smoothstep(0.82, 1.0, T) * 0.4);

// Tuft-scale mottling, about a metre across.
c *= 1.0 + (MacroC.r - 0.5) * 0.34 * MacroAmt;
c = lerp(c, c * float3(0.9, 1.0, 0.86), saturate(MacroC.g * 2.0 - 1.0) * MacroAmt * 0.6);

// Patches a few metres across.
float m1 = MacroA.r * 2.0 - 1.0;
float m2 = MacroB.g * 2.0 - 1.0;
c *= 1.0 + (m1 * 0.3 + m2 * 0.12) * MacroAmt;
c = lerp(c, c * float3(1.14, 1.0, 0.7), saturate(MacroA.g * 2.0 - 0.9) * MacroAmt);

// Season.
c = lerp(c, c * float3(1.3, 0.96, 0.5), saturate(season) * 0.75);
c = lerp(c, c * float3(0.9, 1.14, 0.8), saturate(-season) * 0.6);

// Dry straw: blades baked dry, plus more as dryness and autumn rise, in patches.
float r2 = frac(Rnd * 17.31 + 0.137);
float dryChance = saturate((dryness + saturate(season) * 0.25) * (0.55 + MacroB.b * 0.9));
float dry = max(DryV, step(r2, dryChance));
float3 straw = DryCol * (0.7 + 0.5 * T) * (1.0 + r * 0.18);
c = lerp(c, straw, dry);

// Mowing stripes.
c *= 1.0 + Stripe;

// Overlay image (aerial photo): takes hue and brightness from the image, keeps blade detail.
float lum = dot(c, float3(0.3, 0.59, 0.11));
c = lerp(c, Overlay * (lum / avgLum), OverlayStrength);

c *= max(1.0 + Tint, 0.0);

// Wet.
c *= lerp(1.0, 0.62, wet);
float rough = lerp(lerp(0.7, 0.85, dry), 0.22, wet);

// Snow settles on the tips first, then buries the blade.
float snowMask = saturate(snow * 2.2 - (1.0 - T) * 1.2 + (MacroA.r - 0.5) * 0.5 * (1.0 - snow) * saturate(snow * 6.0));
snowMask = smoothstep(0.0, 1.0, snowMask);
c = lerp(c, float3(0.86, 0.88, 0.92), snowMask);
rough = lerp(rough, 0.6, snowMask);

return float4(c, rough);
)");

	static UMaterial* BuildGrassMaterial(UMaterialParameterCollection* MPC, UTexture2D* Noise)
	{
		UMaterial* M = GreenswardEditor::FindOrCreateAsset<UMaterial>(MaterialFolder, TEXT("M_Greensward_Grass"));
		M->PreEditChange(nullptr);
		ClearGraph(M);

		M->BlendMode = BLEND_Opaque;
		M->TwoSided = true;
		M->SetShadingModel(MSM_TwoSidedFoliage);
		M->bTangentSpaceNormal = false;
		M->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		M->SetMaterialUsage(MATUSAGE_Nanite);
		M->MaxWorldPositionOffsetDisplacement = 60.0f;

		UMaterialEditorOnlyData* Out = M->GetEditorOnlyData();

		// Inputs.
		UMaterialExpressionWorldPosition* WP = Node<UMaterialExpressionWorldPosition>(M, -2200, 0);
		WP->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
		UMaterialExpressionVertexColor* VC = Node<UMaterialExpressionVertexColor>(M, -2200, 300);
		UMaterialExpressionTime* Time = Node<UMaterialExpressionTime>(M, -2200, 500);
		UMaterialExpressionCollectionParameter* Wind = Collection(M, MPC, TEXT("Wind"), -2200, 600);
		UMaterialExpressionCollectionParameter* Env = Collection(M, MPC, TEXT("Environment"), -2200, 700);

		// Per-lawn values: set by the Greensward Lawn actor through custom primitive data, or here on an instance.
		UMaterialExpressionScalarParameter* StripeMode = Scalar(M, TEXT("StripeMode"), 0.0f, TEXT("Mowing"), -2200, 900, 0);
		UMaterialExpressionScalarParameter* StripeWidth = Scalar(M, TEXT("StripeWidth"), 200.0f, TEXT("Mowing"), -2200, 1000, 1);
		UMaterialExpressionScalarParameter* StripeAngle = Scalar(M, TEXT("StripeAngle"), 0.0f, TEXT("Mowing"), -2200, 1100, 2);
		UMaterialExpressionScalarParameter* StripeContrast = Scalar(M, TEXT("StripeContrast"), 0.22f, TEXT("Mowing"), -2200, 1200, 3);
		UMaterialExpressionScalarParameter* StripeLean = Scalar(M, TEXT("StripeLean"), 1.0f, TEXT("Mowing"), -2200, 1300);
		UMaterialExpressionVectorParameter* Tint = Vector(M, TEXT("TintOffset"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), TEXT("Colour"), -2200, 1400, 4);
		// The same mowing controls for mats placed without a lawn actor (PCG, foliage painting, by hand).
		UMaterialExpressionScalarParameter* StripeModeI = Scalar(M, TEXT("InstanceStripeMode"), 0.0f, TEXT("Mowing"), -2500, 900);
		UMaterialExpressionScalarParameter* StripeWidthI = Scalar(M, TEXT("InstanceStripeWidth"), 200.0f, TEXT("Mowing"), -2500, 1000);
		UMaterialExpressionScalarParameter* StripeAngleI = Scalar(M, TEXT("InstanceStripeAngle"), 0.0f, TEXT("Mowing"), -2500, 1100);
		UMaterialExpressionScalarParameter* StripeContrastI = Scalar(M, TEXT("InstanceStripeContrast"), 0.22f, TEXT("Mowing"), -2500, 1200);

		UMaterialExpressionVectorParameter* RootColour = Vector(M, TEXT("RootColour"), FLinearColor(0.026f, 0.058f, 0.013f), TEXT("Colour"), -2200, 1600);
		UMaterialExpressionVectorParameter* TipColour = Vector(M, TEXT("TipColour"), FLinearColor(0.072f, 0.165f, 0.030f), TEXT("Colour"), -2200, 1800);
		UMaterialExpressionVectorParameter* DryColour = Vector(M, TEXT("DryColour"), FLinearColor(0.36f, 0.29f, 0.13f), TEXT("Colour"), -2200, 2000);
		UMaterialExpressionVectorParameter* Subsurface = Vector(M, TEXT("SubsurfaceTint"), FLinearColor(0.65f, 0.9f, 0.28f), TEXT("Colour"), -2200, 2200);
		UMaterialExpressionScalarParameter* Micro = Scalar(M, TEXT("MicroVariation"), 0.5f, TEXT("Variation"), -2200, 2400);
		UMaterialExpressionScalarParameter* MacroAmt = Scalar(M, TEXT("MacroVariation"), 0.5f, TEXT("Variation"), -2200, 2500);
		UMaterialExpressionScalarParameter* MacroTiling = Scalar(M, TEXT("MacroTiling"), 0.0004f, TEXT("Variation"), -2200, 2600);
		UMaterialExpressionScalarParameter* RootOcclusion = Scalar(M, TEXT("RootOcclusion"), 0.5f, TEXT("Colour"), -2200, 2700);

		UMaterialExpressionVectorParameter* OverlayOrigin = Vector(M, TEXT("OverlayOrigin"), FLinearColor(-25000.0f, -25000.0f, 0.0f, 0.0f), TEXT("Overlay"), -2200, 2900);
		UMaterialExpressionVectorParameter* OverlaySize = Vector(M, TEXT("OverlaySize"), FLinearColor(50000.0f, 50000.0f, 0.0f, 0.0f), TEXT("Overlay"), -2200, 3100);
		UMaterialExpressionScalarParameter* OverlayStrength = Scalar(M, TEXT("OverlayStrength"), 0.0f, TEXT("Overlay"), -2200, 3300);

		// Macro noise.
		UMaterialExpressionComponentMask* WPxy = Node<UMaterialExpressionComponentMask>(M, -1900, 0);
		WPxy->R = true; WPxy->G = true; WPxy->B = false; WPxy->A = false;
		WPxy->Input.Connect(0, WP);

		UMaterialExpressionMultiply* UVA = Node<UMaterialExpressionMultiply>(M, -1700, 0);
		UVA->A.Connect(0, WPxy);
		UVA->B.Connect(0, MacroTiling);
		UMaterialExpressionMultiply* UVB = Node<UMaterialExpressionMultiply>(M, -1700, 150);
		UVB->A.Connect(0, UVA);
		UVB->ConstB = 5.3f;

		UMaterialExpressionTextureSample* MacroA = Node<UMaterialExpressionTextureSample>(M, -1500, 0);
		MacroA->Texture = Noise;
		MacroA->AutoSetSampleType();
		MacroA->Coordinates.Connect(0, UVA);
		UMaterialExpressionTextureSample* MacroB = Node<UMaterialExpressionTextureSample>(M, -1500, 250);
		MacroB->Texture = Noise;
		MacroB->AutoSetSampleType();
		MacroB->Coordinates.Connect(0, UVB);

		UMaterialExpressionMultiply* UVC = Node<UMaterialExpressionMultiply>(M, -1700, 300);
		UVC->A.Connect(0, UVA);
		UVC->ConstB = 23.7f;
		UMaterialExpressionTextureSample* MacroC = Node<UMaterialExpressionTextureSample>(M, -1500, 500);
		MacroC->Texture = Noise;
		MacroC->AutoSetSampleType();
		MacroC->Coordinates.Connect(0, UVC);
		UMaterialExpressionTextureCoordinate* BladeUV = Node<UMaterialExpressionTextureCoordinate>(M, -1500, 750);

		// Overlay image.
		UMaterialExpressionCustom* OverlayUV = Custom(M, TEXT("Overlay UV"), CMOT_Float2, OverlayUVCode, -1700, 2900);
		In(OverlayUV, TEXT("WP"), WP);
		In(OverlayUV, TEXT("Origin"), OverlayOrigin);
		In(OverlayUV, TEXT("Size"), OverlaySize);

		UMaterialExpressionTextureSampleParameter2D* Overlay = Node<UMaterialExpressionTextureSampleParameter2D>(M, -1500, 2900);
		Overlay->ParameterName = TEXT("OverlayTexture");
		Overlay->Group = TEXT("Overlay");
		Overlay->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		Overlay->AutoSetSampleType();
		Overlay->Coordinates.Connect(0, OverlayUV);

		// Mowing stripes.
		UMaterialExpressionCustom* Stripe = Custom(M, TEXT("Mowing Stripes"), CMOT_Float1, StripeCode, -1500, 900);
		In(Stripe, TEXT("WP"), WP);
		In(Stripe, TEXT("Angle"), StripeAngle);
		In(Stripe, TEXT("Width"), StripeWidth);
		In(Stripe, TEXT("Mode"), StripeMode);
		In(Stripe, TEXT("Contrast"), StripeContrast);
		In(Stripe, TEXT("ModeI"), StripeModeI);
		In(Stripe, TEXT("WidthI"), StripeWidthI);
		In(Stripe, TEXT("AngleI"), StripeAngleI);
		In(Stripe, TEXT("ContrastI"), StripeContrastI);

		// Wind.
		UMaterialExpressionCustom* WindNode = Custom(M, TEXT("Wind"), CMOT_Float3, WindCode, -1100, 500);
		In(WindNode, TEXT("WP"), WP);
		In(WindNode, TEXT("T"), Time);
		In(WindNode, TEXT("Wind"), Wind);
		In(WindNode, TEXT("Mask"), VC, 1);
		In(WindNode, TEXT("H"), VC, 4);
		In(WindNode, TEXT("Rnd"), VC, 2);
		In(WindNode, TEXT("Stripe"), Stripe);
		In(WindNode, TEXT("Angle"), StripeAngle);
		In(WindNode, TEXT("Lean"), StripeLean);
		In(WindNode, TEXT("Mode"), StripeMode);
		In(WindNode, TEXT("AngleI"), StripeAngleI);

		UMaterialExpressionConstant3Vector* NoOffset = Node<UMaterialExpressionConstant3Vector>(M, -1100, 800);
		NoOffset->Constant = FLinearColor(0, 0, 0, 0);

		UMaterialExpressionStaticSwitchParameter* EnableWind = Node<UMaterialExpressionStaticSwitchParameter>(M, -800, 600);
		EnableWind->ParameterName = TEXT("EnableWind");
		EnableWind->Group = TEXT("Wind");
		EnableWind->DefaultValue = true;
		EnableWind->A.Connect(0, WindNode);
		EnableWind->B.Connect(0, NoOffset);

		// Colour.
		UMaterialExpressionCustom* Colour = Custom(M, TEXT("Grass Colour"), CMOT_Float4, ColourCode, -1100, 1500);
		In(Colour, TEXT("T"), VC, 1);
		In(Colour, TEXT("Rnd"), VC, 2);
		In(Colour, TEXT("DryV"), VC, 3);
		In(Colour, TEXT("MacroA"), MacroA);
		In(Colour, TEXT("MacroB"), MacroB);
		In(Colour, TEXT("MacroC"), MacroC);
		In(Colour, TEXT("UV"), BladeUV);
		In(Colour, TEXT("RootCol"), RootColour);
		In(Colour, TEXT("TipCol"), TipColour);
		In(Colour, TEXT("DryCol"), DryColour);
		In(Colour, TEXT("Env"), Env);
		In(Colour, TEXT("Stripe"), Stripe);
		In(Colour, TEXT("StripeContrast"), StripeContrast);
		In(Colour, TEXT("Overlay"), Overlay);
		In(Colour, TEXT("OverlayStrength"), OverlayStrength);
		In(Colour, TEXT("Tint"), Tint);
		In(Colour, TEXT("Micro"), Micro);
		In(Colour, TEXT("MacroAmt"), MacroAmt);

		UMaterialExpressionComponentMask* RGB = Node<UMaterialExpressionComponentMask>(M, -800, 1500);
		RGB->R = true; RGB->G = true; RGB->B = true; RGB->A = false;
		RGB->Input.Connect(0, Colour);
		UMaterialExpressionComponentMask* Rough = Node<UMaterialExpressionComponentMask>(M, -800, 1650);
		Rough->R = false; Rough->G = false; Rough->B = false; Rough->A = true;
		Rough->Input.Connect(0, Colour);

		UMaterialExpressionMultiply* SSS = Node<UMaterialExpressionMultiply>(M, -600, 1800);
		SSS->A.Connect(0, RGB);
		SSS->B.Connect(0, Subsurface);

		UMaterialExpressionConstant* One = Node<UMaterialExpressionConstant>(M, -1000, 2700);
		One->R = 1.0f;
		UMaterialExpressionLinearInterpolate* AO = Node<UMaterialExpressionLinearInterpolate>(M, -800, 2700);
		AO->A.Connect(0, RootOcclusion);
		AO->B.Connect(0, One);
		AO->Alpha.Connect(1, VC);

		UMaterialExpressionConstant* Specular = Node<UMaterialExpressionConstant>(M, -600, 2000);
		Specular->R = 0.18f;

		UMaterialExpressionVertexNormalWS* Normal = Node<UMaterialExpressionVertexNormalWS>(M, -600, 2200);

		Out->BaseColor.Connect(0, RGB);
		Out->Roughness.Connect(0, Rough);
		Out->Specular.Connect(0, Specular);
		Out->SubsurfaceColor.Connect(0, SSS);
		Out->AmbientOcclusion.Connect(0, AO);
		Out->Normal.Connect(0, Normal);
		Out->WorldPositionOffset.Connect(0, EnableWind);

		M->PostEditChange();
		UMaterialEditingLibrary::RecompileMaterial(M);
		GreenswardEditor::SaveAsset(M);
		return M;
	}

	static UMaterial* BuildGroundMaterial(UTexture2D* Noise)
	{
		UMaterial* M = GreenswardEditor::FindOrCreateAsset<UMaterial>(MaterialFolder, TEXT("M_Greensward_Ground"));
		M->PreEditChange(nullptr);
		ClearGraph(M);
		UMaterialEditorOnlyData* Out = M->GetEditorOnlyData();

		UMaterialExpressionWorldPosition* WP = Node<UMaterialExpressionWorldPosition>(M, -1400, 0);
		UMaterialExpressionComponentMask* WPxy = Node<UMaterialExpressionComponentMask>(M, -1200, 0);
		WPxy->R = true; WPxy->G = true; WPxy->B = false; WPxy->A = false;
		WPxy->Input.Connect(0, WP);

		UMaterialExpressionMultiply* UVFine = Node<UMaterialExpressionMultiply>(M, -1000, 0);
		UVFine->A.Connect(0, WPxy);
		UVFine->ConstB = 0.004f;
		UMaterialExpressionMultiply* UVCoarse = Node<UMaterialExpressionMultiply>(M, -1000, 150);
		UVCoarse->A.Connect(0, WPxy);
		UVCoarse->ConstB = 0.0005f;

		UMaterialExpressionTextureSample* Fine = Node<UMaterialExpressionTextureSample>(M, -800, 0);
		Fine->Texture = Noise;
		Fine->AutoSetSampleType();
		Fine->Coordinates.Connect(0, UVFine);
		UMaterialExpressionTextureSample* Coarse = Node<UMaterialExpressionTextureSample>(M, -800, 250);
		Coarse->Texture = Noise;
		Coarse->AutoSetSampleType();
		Coarse->Coordinates.Connect(0, UVCoarse);

		UMaterialExpressionVectorParameter* Soil = Vector(M, TEXT("SoilColour"), FLinearColor(0.045f, 0.034f, 0.02f), TEXT("Ground"), -800, 500);
		UMaterialExpressionVectorParameter* Thatch = Vector(M, TEXT("ThatchColour"), FLinearColor(0.032f, 0.058f, 0.016f), TEXT("Ground"), -800, 700);

		UMaterialExpressionCustom* Colour = Custom(M, TEXT("Ground Colour"), CMOT_Float3, TEXT(R"(
float a = saturate(Fine.r * 1.4 - 0.2);
float3 c = lerp(Soil, Thatch, saturate(a * 0.6 + Coarse.g * 0.7));
c *= 0.8 + 0.4 * Fine.b;
c *= 0.85 + 0.3 * Coarse.r;
return c;
)"), -500, 300);
		In(Colour, TEXT("Fine"), Fine);
		In(Colour, TEXT("Coarse"), Coarse);
		In(Colour, TEXT("Soil"), Soil);
		In(Colour, TEXT("Thatch"), Thatch);

		UMaterialExpressionConstant* Roughness = Node<UMaterialExpressionConstant>(M, -500, 600);
		Roughness->R = 0.92f;

		Out->BaseColor.Connect(0, Colour);
		Out->Roughness.Connect(0, Roughness);

		M->PostEditChange();
		UMaterialEditingLibrary::RecompileMaterial(M);
		GreenswardEditor::SaveAsset(M);
		return M;
	}

	static UMaterialInstanceConstant* BuildInstance(const TCHAR* Name, UMaterial* Parent, bool bWind, int32 StripeMode = 0, float StripeWidth = 200.0f, float StripeAngle = 0.0f)
	{
		UMaterialInstanceConstant* MI = GreenswardEditor::FindOrCreateAsset<UMaterialInstanceConstant>(MaterialFolder, Name);
		MI->PreEditChange(nullptr);
		MI->SetParentEditorOnly(Parent);
		UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MI, TEXT("EnableWind"), bWind);
		if (StripeMode > 0)
		{
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("InstanceStripeMode"), static_cast<float>(StripeMode));
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("InstanceStripeWidth"), StripeWidth);
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("InstanceStripeAngle"), StripeAngle);
		}
		MI->PostEditChange();
		UMaterialEditingLibrary::UpdateMaterialInstance(MI);
		GreenswardEditor::SaveAsset(MI);
		return MI;
	}
}

bool UGreenswardEditorLibrary::BuildCoreAssets()
{
	using namespace GreenswardCorePrivate;

	UMaterialParameterCollection* MPC = BuildCollection();

	// Tiling noise: three independent channels.
	UTexture2D* Noise = BuildTexture(TEXT("T_Greensward_Noise"), 512, false, [](float U, float V)
	{
		const float R = GreenswardEditor::Fbm(U, V, 4, 6, 11, true);
		const float G = GreenswardEditor::Fbm(U, V, 6, 5, 29, true);
		const float B = GreenswardEditor::Fbm(U, V, 10, 5, 53, true);
		auto Stretch = [](float X) { return FMath::Clamp((X - 0.5f) * 2.2f + 0.5f, 0.0f, 1.0f); };
		return FColor(ToByte(Stretch(R)), ToByte(Stretch(G)), ToByte(Stretch(B)), 255);
	});

	// A stand-in aerial photo for demonstrating the overlay feature.
	BuildTexture(TEXT("T_Greensward_DemoOverlay"), 512, true, [](float U, float V)
	{
		const float A = GreenswardEditor::Fbm(U, V, 3, 5, 71, false);
		const float B = GreenswardEditor::Fbm(U, V, 5, 4, 97, false);
		const FLinearColor Lush(0.07f, 0.17f, 0.03f);
		const FLinearColor Pale(0.19f, 0.24f, 0.07f);
		const FLinearColor Worn(0.26f, 0.21f, 0.10f);
		FLinearColor C = FMath::Lerp(Lush, Pale, FMath::SmoothStep(0.35f, 0.65f, A));
		C = FMath::Lerp(C, Worn, FMath::SmoothStep(0.6f, 0.75f, B));
		return C.ToFColor(true);
	});

	UMaterial* Grass = BuildGrassMaterial(MPC, Noise);
	BuildGroundMaterial(Noise);
	GreenswardEditor::BuildLandscapeAssets(MPC, Noise);
	BuildInstance(TEXT("MI_Greensward_Grass_Wind"), Grass, true);
	BuildInstance(TEXT("MI_Greensward_Grass_Static"), Grass, false);
	BuildInstance(TEXT("MI_Greensward_Grass_Striped"), Grass, false, 1, 525.0f, 90.0f);
	BuildInstance(TEXT("MI_Greensward_Grass_Checker"), Grass, false, 2, 300.0f);
	GreenswardEditor::BuildMarkingAssets(Noise);

	UE_LOG(LogGreenswardEditor, Log, TEXT("Greensward core assets built."));
	return true;
}
