// What the launch monitors hand the game: a shot (ball and club data, the web version's toShot in
// server.js), the connection's status, and the Mevo+ setup. See OTGLaunchMonitorSubsystem.h.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "OTGLaunchMonitorTypes.generated.h"

// How shots arrive: FS Golf through OpenConnect (the OpenConnect listener always runs), the Mevo+
// directly over its Wi-Fi, a Garmin R10 or Square Golf through their bridge programs, or any Flight
// Relay Protocol device
UENUM(BlueprintType)
enum class EOTGConnection : uint8
{
	FSGolf UMETA(DisplayName = "FS Golf (OpenConnect)"),
	Direct UMETA(DisplayName = "Mevo+ directly"),
	R10 UMETA(DisplayName = "Garmin R10"),
	Square UMETA(DisplayName = "Square Golf"),
	Frp UMETA(DisplayName = "Flight Relay Protocol device"),
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
struct OPENTEEGOLF_API FOTGBallData
{
	TOptional<double> Speed, VLA, HLA, BackSpin, SideSpin, TotalSpin, SpinAxis, CarryYd;
	FOTGClubData Club;
};

// toShot (server.js): clamped into an FOTGShot
OPENTEEGOLF_API FOTGShot OTGToShot(const FOTGBallData& B, const FString& Source, const FString& Device, int32 ShotNumber = -1);
OPENTEEGOLF_API FString OTGShotLine(const FOTGShot& S); // "150.3 mph, VLA 12.0, HLA 1.3, spin 2737 rpm, axis 9.5"
