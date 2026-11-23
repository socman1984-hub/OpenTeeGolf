// The ground's turf textures, made from the game's own grass (OTG.Look.Bake): a square of a surface -
// well inside a fairway, a green, the rough ... - photographed from straight above with only its grass
// in the picture, orthographic, three times: the blades' base colour, their normals and their height.
// Written to Saved/TurfBake as <name>_color.png (sRGB, alpha: blade or not), <name>_normal.png (world
// normal, 0.5 + 0.5 n) and <name>_height.png (0 at the ground, 255 at the tallest blade).
// unreal/bake_turf.py makes them tile and puts them in unreal/textures, where build_course.py's ground
// material takes them: the ground under the blades - and on past them, where they're no longer drawn -
// is then the same grass, in the same colours, as a real lawn seen from a little way off.

#include "World/OTGRuntimeCourse.h"
#include "World/OTGGrass.h"
#include "Course/OTGCourseData.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGTurfBake, Log, All);

namespace
{
	constexpr int32 BakePx = 2048;
	constexpr double BakeUpCm = 300;    // (the camera over the patch: near - the blades fade out with the distance from it)

	bool SavePng(const FString& File, const TArray<uint8>& Px, int32 W, int32 H, ERGBFormat Format)
	{
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Img = M.CreateImageWrapper(EImageFormat::PNG);
		if (!Img.IsValid() || !Img->SetRaw(Px.GetData(), Px.Num(), W, H, Format, 8)) return false;
		return FFileHelper::SaveArrayToFile(Img->GetCompressed(), *File);
	}
}

bool AOTGRuntimeCourse::BakeTurf(const FString& Surface, double SizeCm, const FString& Name, const FVector& From)
{
	if (!Grass.IsValid() || !Data) return false;
	// the patch: well inside the surface, its whole square on it (From and the patch: world cm; the
	// grass's own search is in the level's frame)
	FVector At;
	bool bFound = false;
	for (const double In : { SizeCm / 200.0 + 1.0, SizeCm / 200.0 + 0.3 })
		if ((bFound = Grass->FindSurface(Surface, From, At, In))) break;
	if (!bFound) { UE_LOG(LogOTGTurfBake, Warning, TEXT("Bake %s: no patch %.0f cm square inside it near here"), *Surface, SizeCm); return false; }
	const TArray<UPrimitiveComponent*> Comps = Grass->ComponentsNear(At, SizeCm);
	if (!Comps.Num()) { UE_LOG(LogOTGTurfBake, Warning, TEXT("Bake %s: no grass drawn there yet (bring the camera to it first)"), *Surface); return false; }

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this);
	RT->RenderTargetFormat = RTF_RGBA32f;
	RT->ClearColor = FLinearColor(0, 0, 0, 0);
	RT->InitAutoFormat(BakePx, BakePx);
	RT->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Cap = NewObject<USceneCaptureComponent2D>(this);
	Cap->SetupAttachment(GetRootComponent());
	Cap->RegisterComponent();
	Cap->SetWorldLocationAndRotation(At + FVector(0, 0, BakeUpCm), FRotator(-90, 0, 0));
	Cap->ProjectionType = ECameraProjectionMode::Orthographic;
	Cap->OrthoWidth = float(SizeCm);
	Cap->bCaptureEveryFrame = false;
	Cap->bCaptureOnMovement = false;
	Cap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	for (UPrimitiveComponent* P : Comps) Cap->ShowOnlyComponents.Add(P);
	Cap->TextureTarget = RT;
	Cap->ShowFlags.SetFog(false);
	Cap->ShowFlags.SetAtmosphere(false);

	auto Grab = [&](ESceneCaptureSource Src, TArray<FLinearColor>& Out) {
		Cap->CaptureSource = Src;
		Cap->CaptureScene();
		FlushRenderingCommands();
		Out.Reset();
		return RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Out) && Out.Num() == BakePx * BakePx;
	};
	TArray<FLinearColor> Col, Nrm, Dep;
	const bool bOk = Grab(SCS_BaseColor, Col) && Grab(SCS_Normal, Nrm) && Grab(SCS_SceneDepth, Dep);
	Cap->DestroyComponent();
	if (!bOk) { UE_LOG(LogOTGTurfBake, Warning, TEXT("Bake %s: the capture didn't read back"), *Surface); return false; }

	// each pixel's blade height over the ground (the depth from the camera; nothing there: far off)
	double Tallest = 1;
	TArray<double> H;
	H.SetNumUninitialized(Dep.Num());
	for (int32 i = 0; i < Dep.Num(); i++)
	{
		const double D = Dep[i].R;
		H[i] = (D <= 1 || D > BakeUpCm * 1.5) ? -1 : FMath::Max(0.0, BakeUpCm - D);
		if (H[i] > Tallest) Tallest = H[i];
	}
	TArray<uint8> C8, N8, H8;
	C8.SetNumUninitialized(Col.Num() * 4); N8.SetNumUninitialized(Col.Num() * 4); H8.SetNumUninitialized(Col.Num());
	for (int32 i = 0; i < Col.Num(); i++)
	{
		const bool bBlade = H[i] >= 0;
		const FColor C = Col[i].ToFColor(true);   // (sRGB)
		C8[i * 4 + 0] = C.R; C8[i * 4 + 1] = C.G; C8[i * 4 + 2] = C.B; C8[i * 4 + 3] = bBlade ? 255 : 0;
		const FVector3f N = bBlade ? FVector3f(Nrm[i].R, Nrm[i].G, Nrm[i].B).GetSafeNormal() : FVector3f(0, 0, 1);
		N8[i * 4 + 0] = uint8(FMath::Clamp(N.X * 0.5f + 0.5f, 0.f, 1.f) * 255.f);
		N8[i * 4 + 1] = uint8(FMath::Clamp(N.Y * 0.5f + 0.5f, 0.f, 1.f) * 255.f);
		N8[i * 4 + 2] = uint8(FMath::Clamp(N.Z * 0.5f + 0.5f, 0.f, 1.f) * 255.f);
		N8[i * 4 + 3] = 255;
		H8[i] = bBlade ? uint8(FMath::Clamp(H[i] / Tallest, 0.0, 1.0) * 255.0) : 0;
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("TurfBake");
	const bool bSaved = SavePng(Dir / (Name + TEXT("_color.png")), C8, BakePx, BakePx, ERGBFormat::RGBA)
		&& SavePng(Dir / (Name + TEXT("_normal.png")), N8, BakePx, BakePx, ERGBFormat::RGBA)
		&& SavePng(Dir / (Name + TEXT("_height.png")), H8, BakePx, BakePx, ERGBFormat::Gray);
	UE_LOG(LogOTGTurfBake, Display, TEXT("Bake %s: %.0f cm square at %s, %d grass components, tallest %.1f cm -> %s (%s)"),
		*Surface, SizeCm, *At.ToString(), Comps.Num(), Tallest, *Dir, bSaved ? TEXT("saved") : TEXT("NOT saved"));
	return bSaved;
}
