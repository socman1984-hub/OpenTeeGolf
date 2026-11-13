// Shots from the launch monitor, inside the game (what server.js does for the web version):
//
//  - OpenConnect: FS Golf (or any OpenConnect connector) sends shots to TCP 921; always listening
//  - Mevo+ directly: the PC on the Mevo+ Wi-Fi, talking to the radar itself (no FS Golf)
//  - Garmin R10 / Square Golf: through their bridge programs (bridges/), over FRP
//  - any Flight Relay Protocol device at an address
//
// OnShot gets each shot (clamped; mph, degrees, rpm), OnStatus the connection's state. The game
// says what the player is hitting (SetPlayer: the Mevo+ detection mode follows the club and the
// distance) and when it's their turn (SetTurn: with "arm on turn", the Mevo+ is armed only then and
// stays disarmed after each shot until the next turn).
//
// The choice of connection and the Mevo+ setup are saved in GameUserSettings.ini
// [OpenTeeGolf.LaunchMonitor]. Command line (not saved): -lm=fsgolf|direct|r10|square|frp,
// -mevo=host[:port], -frp=ws://..., -ocport=<n>, -bridgecmd="<program> <args>", -lmturn (start on
// the player's turn), -quitafter=<s>, -execat="<s>:<console command>|<s>:<command>...".
// Console: OTG.LM.Status, OTG.LM.Connect <kind> [address], OTG.LM.Turn 0|1, OTG.LM.Player <club>
// [yards] [RH|LH], OTG.LM.Disconnect, OTG.LM.TestShot [mph vla hla spin axis].

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "OTGLaunchMonitorSubsystem.generated.h"

struct FOTGLMOutbox;
class FOTGOpenConnectServer;
class FOTGMevoClient;
class FOTGFrpController;
class FOTGBridgeProcess;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOTGShotEvent, const FOTGShot&, Shot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOTGLMStatusEvent, const FOTGLaunchMonitorStatus&, Status);

UCLASS()
class OPENTEEGOLF_API UOTGLaunchMonitorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "OpenTeeGolf") FOTGShotEvent OnShot;
	UPROPERTY(BlueprintAssignable, Category = "OpenTeeGolf") FOTGLMStatusEvent OnStatus;
	TMulticastDelegate<void(const FOTGShot&)> OnShotNative;

	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") FOTGLaunchMonitorStatus GetStatus() const { return Status; }

	// How shots arrive (saved). Address: the Mevo+ host for Direct, the ws:// URL for Frp (empty: keep)
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void SetConnection(EOTGConnection Kind, const FString& Address = TEXT(""));
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") EOTGConnection GetConnection() const { return Connection; }

	// What the player is hitting: club (DR, W3, W5, H4, I4..I9, PW, GW, SW, LW, PT), yards to the target
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void SetPlayer(const FString& Club, double DistanceYd, bool bLeftHanded);
	// Whether it's the player's turn to hit
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void SetTurn(bool bReady);
	// Arm the Mevo+ only on the player's turn (saved; on by default)
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void SetArmOnTurn(bool bOn);
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void SetMevoSetup(const FOTGMevoSetup& Setup);
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") FOTGMevoSetup GetMevoSetup() const { return MevoSetup; }
	// The player disconnected the device (until ConnectDevice or a restart); and connecting again
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void DisconnectDevice();
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void ConnectDevice();
	// A shot as if from the launch monitor (no device needed)
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf") void InjectShot(double SpeedMph, double VLA, double HLA, double TotalSpin, double SpinAxis);

	// The Mevo+ detection mode for the player's club and distance (server.js mevoMode)
	FString MevoMode() const;

private:
	bool Tick(float Dt);
	void LoadSettings();
	void SaveSettings();
	void StartDevice();  // the chosen connection (OpenConnect runs regardless)
	void StopDevice();
	void ApplyTurn();
	void PlayerChanged();
	void Deliver(const FOTGShot& Shot);
	void Compose();
	FString FrpModeName() const;
	TArray<FString> SquareArgs() const;

	EOTGConnection Connection = EOTGConnection::FSGolf;
	FString MevoHost = TEXT("192.168.2.1"), FrpUrl = TEXT("ws://127.0.0.1:5880/frp"), BridgeCommand;
	int32 MevoPort = 5100, OpenConnectPort = 921, BridgePort = 5881;
	bool bArmOnTurn = true, bHasSetup = false, bTurn = false, bDeviceOff = false;
	FOTGMevoSetup MevoSetup;
	FString Club = TEXT("DR"), Handed = TEXT("RH");
	double DistanceYd = 0;

	TSharedPtr<FOTGLMOutbox> OcOut, DevOut, BridgeOut;
	// (shared pointers: they're only declared here)
	TSharedPtr<FOTGOpenConnectServer> OpenConnect;
	TSharedPtr<FOTGMevoClient> Mevo;
	TSharedPtr<FOTGFrpController> Frp;
	TSharedPtr<FOTGBridgeProcess> Bridge;

	FOTGLaunchMonitorStatus Status;
	FTSTicker::FDelegateHandle TickHandle;
	double StartedAt = 0;
	TArray<TPair<double, FString>> ExecAt;
	double QuitAt = 0;
};
