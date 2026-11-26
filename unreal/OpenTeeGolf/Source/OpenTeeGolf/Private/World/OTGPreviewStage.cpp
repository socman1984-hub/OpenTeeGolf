// See OTGPreviewStage.h.

#include "World/OTGPreviewStage.h"

#include "World/OTGRuntimeCourse.h"
#include "World/OTGGrass.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	constexpr double PlotM = 64;
}

AOTGPreviewStage::AOTGPreviewStage()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// its own sun, sky, sky light and haze: the menu's level has none
	UDirectionalLightComponent* Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(FRotator(-38, 55, 0));
	Sun->Intensity = 9.f;
	Sun->bAtmosphereSunLight = true;
	Sun->SetCastShadows(true);
	USkyAtmosphereComponent* Atm = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atm->SetupAttachment(RootComponent);
	USkyLightComponent* Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	Sky->SetupAttachment(RootComponent);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bRealTimeCapture = true;
	UExponentialHeightFogComponent* Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Haze"));
	Fog->SetupAttachment(RootComponent);
	// (thick enough past the plot that the ground beyond fades into it)
	Fog->FogDensity = 0.03f;
	Fog->StartDistance = 12000.f;
	Fog->FogHeightFalloff = 0.05f;
	UCameraComponent* Cam = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Cam->SetupAttachment(RootComponent);
	Cam->SetFieldOfView(55.f);
}

AOTGPreviewStage* AOTGPreviewStage::Open(UWorld* World)
{
	if (!World) return nullptr;
	// (well away from anything the menu's level has, on its own)
	const FVector At(0, 0, 0);
	AOTGPreviewStage* S = World->SpawnActor<AOTGPreviewStage>(At, FRotator::ZeroRotator);
	if (!S) return nullptr;
	S->Plot = World->SpawnActor<AOTGRuntimeCourse>(At, FRotator::ZeroRotator);
	if (S->Plot) S->Plot->BuildPreview(PlotM);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		S->ViewBefore = PC->GetViewTarget();
		PC->SetViewTarget(S);
	}
	S->Place();
	return S;
}

void AOTGPreviewStage::Close()
{
	if (UWorld* W = GetWorld())
		if (APlayerController* PC = W->GetFirstPlayerController())
			PC->SetViewTarget(ViewBefore ? ViewBefore.Get() : PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : static_cast<AActor*>(PC));
	if (Plot) Plot->Destroy();
	Plot = nullptr;
	Destroy();
}

void AOTGPreviewStage::ShowGrass(const TArray<otg::FGrassKind>& Kinds)
{
	if (!Plot) return;
	Plot->PreviewForest(nullptr);
	Plot->PreviewGrass(Kinds);
}

void AOTGPreviewStage::ShowForest(const FOTGForestPreset& Preset, const TArray<otg::FGrassKind>& Under)
{
	if (!Plot) return;
	Plot->PreviewGrass(Under);
	Plot->PreviewForest(&Preset);
}

void AOTGPreviewStage::Orbit(float DYaw, float DPitch)
{
	Yaw += DYaw;
	Pitch = FMath::Clamp(Pitch + DPitch, -70.f, -3.f);
	bTurning = false;
	bFree = false;
	Place();
}

// (the camera kept above the grass: at least this high over the plot's ground, however near it comes)
static constexpr float CameraAboveGroundCm = 120.f;

void AOTGPreviewStage::Zoom(float Times)
{
	Dist = FMath::Clamp(Dist * Times, 300.f, 16000.f);
	bTurning = false;
	bFree = false;
	Place();
}

void AOTGPreviewStage::Frame(bool bForest)
{
	bFree = false;
	if (bForest) { Pitch = -18; Dist = 9500; LookUp = 900; }
	else { Pitch = -15; Dist = 1700; LookUp = 0; }
	Place();
}

void AOTGPreviewStage::FrameClose()
{
	bFree = false;
	bTurning = false;
	Yaw = 35; Pitch = -32; Dist = 420; LookUp = 10;
	Place();
}

int32 AOTGPreviewStage::GrassCount() const { return Plot ? Plot->GrassInstances() : 0; }
TArray<FString> AOTGPreviewStage::Plants() const { return Plot ? Plot->GrassPlants() : TArray<FString>(); }
int32 AOTGPreviewStage::TreeCount() const { return Plot ? Plot->TreesDrawn() : 0; }

void AOTGPreviewStage::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bTurning) { Yaw += Dt * 3.f; Place(); }
}

void AOTGPreviewStage::StartFree()
{
	if (bFree) return;
	bFree = true;
	bTurning = false;
	if (const UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
	{
		FreeAt = Cam->GetComponentLocation();
		const FRotator R = Cam->GetComponentRotation();
		FreeYaw = R.Yaw;
		FreePitch = R.Pitch;
	}
}

void AOTGPreviewStage::Fly(const FVector& Move)
{
	StartFree();
	const FRotator R(FreePitch, FreeYaw, 0);
	const FRotator Flat(0, FreeYaw, 0);
	FreeAt += R.Vector() * Move.X + FRotationMatrix(Flat).GetUnitAxis(EAxis::Y) * Move.Y + FVector(0, 0, Move.Z);
	Place();
}

void AOTGPreviewStage::Look(float DYaw, float DPitch)
{
	StartFree();
	FreeYaw += DYaw;
	FreePitch = FMath::Clamp(FreePitch + DPitch, -85.f, 60.f);
	Place();
}

void AOTGPreviewStage::Place()
{
	if (UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
	{
		if (bFree)
		{
			// (above the grass, and not off beyond the plot's land: 200 m round it)
			const FVector C = GetActorLocation();
			FVector2D XY(FreeAt.X - C.X, FreeAt.Y - C.Y);
			if (XY.Size() > 20000.f) XY = XY.GetSafeNormal() * 20000.f;
			FreeAt = FVector(C.X + XY.X, C.Y + XY.Y, FMath::Clamp(FreeAt.Z, C.Z + CameraAboveGroundCm, C.Z + 30000.f));
			Cam->SetWorldLocationAndRotation(FreeAt, FRotator(FreePitch, FreeYaw, 0));
			return;
		}
		const FVector Target = GetActorLocation() + FVector(0, 0, LookUp);
		// (looking down enough that the camera stays above the grass: never under the plot)
		const float Need = FMath::Clamp((CameraAboveGroundCm - LookUp) / FMath::Max(Dist, 1.f), 0.f, 1.f);
		Pitch = FMath::Min(Pitch, FMath::Min(-3.f, -FMath::RadiansToDegrees(FMath::Asin(Need))));
		const FRotator R(Pitch, Yaw, 0);
		Cam->SetWorldLocationAndRotation(Target - R.Vector() * Dist, R);
	}
}
