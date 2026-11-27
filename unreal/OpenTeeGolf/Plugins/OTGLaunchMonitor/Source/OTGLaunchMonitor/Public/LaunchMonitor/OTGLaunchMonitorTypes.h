// What the launch monitors hand the game: a shot (ball and club data, the web version's toShot in
// server.js), the connection's status, and the Mevo+ setup. See OTGLaunchMonitorSubsystem.h.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "OTGLaunchMonitorTypes.generated.h"

// How shots arrive: FS Golf through OpenConnect (the OpenConnect listener always runs), the Mevo+
// directly over its Wi-Fi, a Garmin R10 or Square Golf through their bridge programs, any Flight
// Relay Protocol device, or rēlā (docs.rela.golf: a connector for many launch monitors, sending to
// the OpenConnect listener), or a Rapsodo MLM2PRO through the community MLM2PRO Connector, which
// also sends to the OpenConnect listener
UENUM(BlueprintType)
enum class EOTGConnection : uint8
{
	FSGolf UMETA(DisplayName = "FS Golf (OpenConnect)"),
	Direct UMETA(DisplayName = "Mevo+ directly"),
	R10 UMETA(DisplayName = "Garmin R10"),
	Square UMETA(DisplayName = "Square Golf"),
	Frp UMETA(DisplayName = "Flight Relay Protocol device"),
	Rela UMETA(DisplayName = "rela (OpenConnect)"),
	Rapsodo UMETA(DisplayName = "Rapsodo MLM2PRO (MLM2PRO Connector, OpenConnect)"),
};

USTRUCT(BlueprintType)
struct FOTGClubData
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") bool bValid = false;
	// (NaN: not measured)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double SpeedMph = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double AngleOfAttack = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double FaceToTarget = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double Path = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double Loft = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double SmashFactor = NAN;
};

// One shot, clamped to what's possible (a bad reading never reaches the physics as NaN or a runaway
// number). mph, degrees, rpm; HLA, side spin and spin axis positive = right
USTRUCT(BlueprintType)
struct FOTGShot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Source;  // launch-monitor (OpenConnect), mevo, r10, square, frp, test
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Device;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") int32 ShotNumber = -1;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double SpeedMph = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double VLA = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double HLA = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double BackSpin = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double SideSpin = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double TotalSpin = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double SpinAxis = 0;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double LmCarryYd = -1;  // the device's own carry (-1: none)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FOTGClubData Club;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FDateTime At;
	// Mevo+ fast shots: the radar's early launch result (speed and angles final, spin a first
	// estimate). The same shot (ShotNumber) comes again through OnShotUpdate with the full result.
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") bool bEarly = false;
	// Mevo+: the radar's first spin estimate (from the early launch result, as it gave it), shown above
	// the final spin; -1 when there's none (other launch monitors)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double EarlySpin = -1;
};

// The Mevo+ setup the player can change (what FS Golf asks for when you set it up)
USTRUCT(BlueprintType)
struct FOTGMevoSetup
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double DistanceM = 2.74;   // Mevo+ to ball
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double SurfaceIn = 1;      // mat height above the floor the Mevo+ stands on
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double TeeHeightIn = 1.5;  // driver tee height
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double TiltTarget = 12;    // the level the setup screen aims for (not sent)
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") int32 BallType = 0;        // 0 standard, 1 RCT
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double TrackPct = 1.0;     // minimum ball-tracking share (0.6-1)
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") FString Mode = TEXT("auto"); // auto (by club), or force full / outdoor
	// Fast shots: full swings start from the radar's early launch result (~0.2 s after impact, not ~2 s),
	// corrected in flight by the full result (not sent to the radar)
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") bool bFast = false;
	// Fast shots: wait up to this long (0.6-3 s) for the full result, with its measured spin, before
	// starting with the early one (a measured spin is in ~0.55 s after impact; if the full result comes
	// sooner, the ball takes off with it at once)
	UPROPERTY(BlueprintReadWrite, Category = "OpenTeeGolf") double FastWait = 0.7;
};

USTRUCT(BlueprintType)
struct FOTGLaunchMonitorStatus
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") EOTGConnection Connection = EOTGConnection::FSGolf;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") bool bConnected = false;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") bool bReady = false;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Device;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") int32 Battery = -1;        // percent (-1: unknown)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString State;             // the connection's own state (searching, handshake, ready, ...)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Text;              // for the player
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Error;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString Mode;              // Mevo+ detection mode
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString PortError;         // the OpenConnect port couldn't be opened
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double Tilt = NAN;         // Mevo+ level (degrees)
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double Roll = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") double TempC = NAN;
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FString DeviceText;        // the device's last status line
	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf") FDateTime LastShotAt;
};

// Ball data as the devices give it (OpenConnect's BallData names), before toShot's clamping
struct OTGLAUNCHMONITOR_API FOTGBallData
{
	TOptional<double> Speed, VLA, HLA, BackSpin, SideSpin, TotalSpin, SpinAxis, CarryYd;
	FOTGClubData Club;
};

// toShot (server.js): clamped into an FOTGShot
OTGLAUNCHMONITOR_API FOTGShot OTGToShot(const FOTGBallData& B, const FString& Source, const FString& Device, int32 ShotNumber = -1);
OTGLAUNCHMONITOR_API FString OTGShotLine(const FOTGShot& S); // "150.3 mph, VLA 12.0, HLA 1.3, spin 2737 rpm, axis 9.5"
