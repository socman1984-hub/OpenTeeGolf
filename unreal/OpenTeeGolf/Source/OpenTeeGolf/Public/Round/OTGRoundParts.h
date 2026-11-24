// The round's parts (AOTGRound, OTGRound.h): each a class of its own that the round owns and calls,
// reaching the round (the ball, the hole, the weather, the HUD) through Round. The round itself keeps
// the golf - the hole, the aim and club, the shot and where it finished, holing out.
//
//  - FOTGRoundTurns: players taking turns and the scores (OTGRoundTurns.cpp)
//  - FOTGRoundCamera: the cameras - the hole from above the tee, behind the ball, the ball's flight,
//    and the free view (OTGRoundView.cpp, OTGRoundExtras.cpp)
//  - FOTGRoundHud: what the HUD shows of the round (OTGRoundHud.cpp)
//  - FOTGRoundScene: the course's look, the weather on the sky, sun, clouds and fog, the Meridian
//    sky, storms and the weather's sound (OTGRoundLook.cpp, OTGSky.cpp, OTGRoundExtras.cpp)
//  - FOTGRoundChanging: changing weather, from one state to the next (OTGRoundExtras.cpp)
//  - FOTGRoundRain: the rain near and far, its splashes, the ground getting wet (OTGRoundRain.cpp,
//    OTGRoundExtras.cpp)
//  - FOTGRoundWaterFx: rings on the water - a ball going in, the rain (OTGRoundExtras.cpp)
//  - FOTGRoundFlag: the flag's cloth in the wind (OTGRoundExtras.cpp)
//  - FOTGRoundSlopeGrid: the slope grid on the green (OTGRoundSlopeGrid.cpp)
//  - FOTGRoundRangeMode: the driving range (OTGRoundRange.cpp)
// The tracer is a plugin of its own (Plugins/OTGShotTracer). They're plain structs; the objects they
// hold are kept from the garbage collector by the round (AOTGRound::AddReferencedObjects).

#pragma once

#include "CoreMinimal.h"
#include "Round/OTGPlayers.h"
#include "Round/OTGWeather.h"
#include "Round/OTGRange.h"

class AOTGRound;
class FReferenceCollector;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;
class ACameraActor;
class APostProcessVolume;
class AMeridianSkyActor;
class UExponentialHeightFogComponent;
class UOTGWeatherSound;
class UAudioComponent;
class SOTGRangePanel;
class AOTGRangeProps;

// Players taking turns as in the web version (main.js loadSide, saveSide, roundAfterShot, beginTurn):
// whoever's away plays next, the others' balls lie where they are, the game's format scores it
struct FOTGRoundTurns
{
	AOTGRound* Round = nullptr;

	void LoadSide(int32 I);
	void SaveSide();
	bool RoundAfterShot();
	void BeginTurn(int32 I, bool bSameSpot);
	void RecordRound();
	void ShowOtherBalls();
	void AddReferencedObjects(FReferenceCollector& Collector);

	TUniquePtr<FOTGGame> Game;
	int32 SideIdx = 0, NextSide = 0;
	TArray<TObjectPtr<UStaticMeshComponent>> OtherBalls; // (one per side)
};

// The cameras: the hole from above the tee, behind the ball at address, following it in flight; and
// the free view (F), flown round the course
struct FOTGRoundCamera
{
	AOTGRound* Round = nullptr;

	void AddressView(bool bSnap);
	void OverviewView();
	void UpdateCamera(float Dt);
	void ToggleFreeView();
	void FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast);
	void FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg);
	void AddReferencedObjects(FReferenceCollector& Collector);

	TObjectPtr<ACameraActor> Cam;
	FVector CamGoal = FVector::ZeroVector, CamLook = FVector::ZeroVector, CamPos = FVector::ZeroVector, CamLookNow = FVector::ZeroVector;
	FVector ShotFrom = FVector::ZeroVector;
	double OverviewLeft = 0;
	float CamSpeed = 3.f;
	FVector OrbitCentre = FVector::ZeroVector, OrbitGoal = FVector(1e30);   // (the address view: AddressView, UpdateCamera)
	bool bFreeView = false;
	FVector FreePos = FVector::ZeroVector;
	FRotator FreeRot = FRotator::ZeroRotator;
};

// What the HUD (SOTGHud) shows of the round: the hole, the shot's numbers, the pin's yardage, the
// minimap, the wind box, banners and toasts, the scorecard
struct FOTGRoundHud
{
	AOTGRound* Round = nullptr;

	void UpdateHudInfo();
	void UpdatePinTag();               // (the pin's yardage and height over the flag, each frame)
	void UpdateMap();
	void UpdateWindBox();
	void ShowShotData(bool bWithResult);
	void Banner(const FString& Title, const FString& Small, float Seconds = 3.f);
	void Toast(const FString& Text, float Seconds = 2.5f);
	void ShowScorecard(bool bShow);
};

// The course's look and the weather on the scene: a clear summer day as golf games show one (the
// OTG.Look.* console variables), the weather on the sky, the sun, the clouds and the fog; one Meridian
// Sky actor (the plugin) in place of the level's sun, sky, sky light, fog and clouds, driven from the
// round's weather and time of day (-oldsky on the command line: the level's own sky, as before); a
// storm's lightning; and the weather's sound (UOTGWeatherSound: rain, wind, thunder)
struct FOTGRoundScene
{
	AOTGRound* Round = nullptr;

	void ApplyLook();                   // (the course's colour and light: OTG.Look.* console variables)
	uint32 LookStampNow() const;        // (the look's settings, the hole and the hour, hashed: ApplyLook again when it changes)
	void ApplyWeatherToScene();
	bool EnsureSky();                   // (true: the Meridian sky's in charge - spawned the first time)
	void ApplySky();                    // (the look and the weather on it: from ApplyLook and ApplyWeatherToScene)
	void UpdateStorm(float Dt);
	void AddReferencedObjects(FReferenceCollector& Collector);

	float BaseFogDensity = -1, BaseSun = -1, BaseCoverage = -1, BaseDensity = -1, BaseRayleigh = -1, BaseMie = -1, BaseSkyLight = -1;
	FLinearColor BaseSkyLum = FLinearColor(-1, -1, -1);   // (the sky's own brightness, greyed under cloud)
	TObjectPtr<UMaterialInstanceDynamic> CloudMat;
	TObjectPtr<UMaterialInstanceDynamic> OwnCloudMat;   // (M_Clouds)
	TObjectPtr<APostProcessVolume> LookVolume;
	float OrigSun = -1, OrigMie = -1, OrigRayleigh = -1, OrigFog = -1, OrigSky = -1;
	FRotator OrigSunRot = FRotator::ZeroRotator;
	FLinearColor OrigSunColour = FLinearColor::White;
	uint32 LookStamp = 0;
	TObjectPtr<AMeridianSkyActor> Sky;
	bool bSkyTried = false;
	TWeakObjectPtr<UExponentialHeightFogComponent> LevelFog;   // (kept: the level's haze, not Meridian's)
	TObjectPtr<UOTGWeatherSound> WxSound;
	TObjectPtr<UAudioComponent> WxAudio;
	float NextBolt = 5.f, BoltAge = 99.f, BoltNear = 0.f;
	float SkyNow = -1.f;   // (the sky light as the weather set it: a flash lifts it for a moment)
};

// Changing weather ("changing": showers come and go; live: fetched again every 15 minutes) - from one
// state to the next over a minute and a half, each held a few minutes
struct FOTGRoundChanging
{
	AOTGRound* Round = nullptr;

	void UpdateChanging(float Dt);
	void BlendTo(const FOTGWeather& Next, const FString& Say);

	FOTGWeather WxFrom, WxTo;
	float WxBlend = 1.f, WxHold = 0.f, WxRefresh = 0.f, WxApplyIn = 0.f;
	int32 WxStage = 0;
};

// The rain: its streaks round the camera, the far rain and the splashes on hard ground near the
// camera (far rain is the height fog's), and the ground getting wet and drying
struct FOTGRoundRain
{
	AOTGRound* Round = nullptr;

	void UpdateRain(float Dt);
	void UpdateRainFar(float Dt, double Mm);
	void UpdateWet(float Dt);
	void AddReferencedObjects(FReferenceCollector& Collector);

	TObjectPtr<UProceduralMeshComponent> RainMesh;
	TObjectPtr<UMaterialInstanceDynamic> RainMat;
	TArray<FVector> RainDrops;
	int32 RainVerts = 0;
	struct FSplash { FVector P; float Age = 0, Size = 1; };
	TArray<FSplash> Splashes;
	float SplashCarry = 0;
	int32 SplashVerts = 0;
	float Wet = -1.f;   // (0 dry .. 1 soaked: rises in rain, dries slowly; -1 until the round's weather is set)
};

// Rings on the water: a ball into water - a splash, then rings spreading over the surface; and rain
// on the water - small rings where the drops land, more the harder it rains
struct FOTGRoundWaterFx
{
	AOTGRound* Round = nullptr;

	void AddRipple(const FVector& WorldAt);
	void UpdateRipples(float Dt);
	void SpawnRainRings(float Dt);
	void AddReferencedObjects(FReferenceCollector& Collector);

	struct FRipple { FVector P; float Age = 0; };
	TArray<FRipple> Ripples;
	struct FRainRing { FVector P; float Age = 0; float Size = 1; float Width = 1; };
	TArray<FRainRing> RainRings;
	float RainRingCarry = 0;
	TObjectPtr<UMaterialInstanceDynamic> RainRingMat;
	TObjectPtr<UProceduralMeshComponent> RippleMesh;
	TObjectPtr<UMaterialInstanceDynamic> RippleMat;
};

// The flag's cloth, rippling in the wind
struct FOTGRoundFlag
{
	AOTGRound* Round = nullptr;

	void UpdateFlag(float Dt);
	void AddReferencedObjects(FReferenceCollector& Collector);

	TObjectPtr<UProceduralMeshComponent> FlagCloth;
	double FlagTime = 0;
	double FlagPhase = 0;   // (the flag's ripples, radians: added up a frame at a time)
	bool bFlagMade = false;
};

// The slope grid on the green (the G key; the web version's putting grid): lines draped over the
// green coloured by its slope, dots sliding downhill; on unless turned off
struct FOTGRoundSlopeGrid
{
	AOTGRound* Round = nullptr;

	void UpdateSlopeGrid(float Dt);
	void BuildSlopeGrid();
	void HideSlopeGrid();
	void ToggleSlopeGrid() { bGridOn = !bGridOn; GridFor = FVector::ZeroVector; }
	void AddReferencedObjects(FReferenceCollector& Collector);

	TObjectPtr<UProceduralMeshComponent> GridMesh;
	TObjectPtr<UInstancedStaticMeshComponent> GridDots;
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GridMats;
	struct FGridDot { double X, Z, Dx, Dz, Pct, Phase; };
	TArray<FGridDot> Dots;
	double GridSpacing = 1.0;
	bool bGridOn = true;
	FVector GridFor = FVector::ZeroVector;   // (the ball and pin it was drawn for: drawn again if either moves)
};

// The driving range (otg::range, OTGRange.h): every shot's numbers in the range panel (SOTGRange.h)
// with the club's recent shots and averages, where they stopped, and a new ball on the mat
struct FOTGRoundRangeMode
{
	AOTGRound* Round = nullptr;

	double ClubCarryM() const;
	void RangeShotDone();
	void UpdateRange();
	void NewBall();
	void AddReferencedObjects(FReferenceCollector& Collector);

	TArray<FOTGRangeShot> RangeShots;
	TSharedPtr<SOTGRangePanel> RangePanel;
	TObjectPtr<AOTGRangeProps> RangeProps;
	TObjectPtr<UInstancedStaticMeshComponent> RangeDots;   // (where the club's last shots stopped)
	TObjectPtr<UMaterialInstanceDynamic> RangeDotMat;
	int32 RangeBalls = 0;
};
