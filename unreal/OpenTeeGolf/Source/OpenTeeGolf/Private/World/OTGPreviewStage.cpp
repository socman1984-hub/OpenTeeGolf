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
	Pitch = FMath::Clamp(Pitch + DPitch, -70.f, 5.f);
	bTurning = false;
	Place();
}

void AOTGPreviewStage::Zoom(float Times)
{
	Dist = FMath::Clamp(Dist * Times, 300.f, 16000.f);
	bTurning = false;
	Place();
}

void AOTGPreviewStage::Frame(bool bForest)
{
	if (bForest) { Pitch = -18; Dist = 9500; LookUp = 900; }
	else { Pitch = -15; Dist = 1700; LookUp = 0; }
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

void AOTGPreviewStage::Place()
{
	if (UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
	{
		const FVector Target = GetActorLocation() + FVector(0, 0, LookUp);
		const FRotator R(Pitch, Yaw, 0);
		Cam->SetWorldLocationAndRotation(Target - R.Vector() * Dist, R);
	}
}
