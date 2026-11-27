# OTGLaunchMonitor

Shots from a launch monitor, inside the game (what the web version's `server.js` does):

- **OpenConnect:** FS Golf or any OpenConnect connector sends shots to TCP port 921. It's always listening.
  rēlā (docs.rela.golf) uses it too: its GSPro output pointed at this PC, port 921 (the `Rela` connection:
  the same listener, its own name and help). Split packets (ball data, then club data alone) are fine: a
  message without ball data is answered as a heartbeat.
  The Rapsodo MLM2PRO comes the same way, through the community MLM2PRO Connector
  (github.com/springbok/MLM2PRO-GSPro-Connector) set to this PC, port 921 (the `Rapsodo` connection). The
  Connector talks Bluetooth to the MLM2PRO and needs GSPro authorised in the Rapsodo app; putts don't come from it.
- **Mevo+ directly:** the PC joins the Mevo+'s Wi-Fi and talks to the radar itself (`OTGMevoClient`, `OTGMevoWire`), plus the camera's live view for lining up the ball (`OTGMevoVideo`).
- **Garmin R10 / Square Golf:** through their bridge programs (`bridges/`), over FRP (Flight Relay Protocol).
- **Any FRP device** at an address (`OTGFrp`).

## Main classes

| Class | What it does |
|---|---|
| `UOTGLaunchMonitorSubsystem` | A game instance subsystem that runs the chosen connection. `OnShot` fires for each shot (`FOTGShot`: mph, degrees, rpm) and `OnStatus` for the connection's state. `SetPlayer` passes the club, the distance and the hand (the Mevo+'s detection mode follows them). `SetTurn` passes whose turn it is (the Mevo+ is armed only then). `InjectShot` plays a test shot. |
| `FOTGShot`, `FOTGLaunchMonitorStatus`, `FOTGMevoSetup` | The types it hands out (`LaunchMonitor/OTGLaunchMonitorTypes.h`). |
| `FOTGOpenConnectServer`, `FOTGMevoClient`, `FOTGFrpController` | One per kind of connection (private). |

Settings are saved in `GameUserSettings.ini` under `[OpenTeeGolf.LaunchMonitor]`. The command
line and the `OTG.LM.*` console commands are listed at the top of `OTGLaunchMonitorSubsystem.h`.

## How the game uses it

`AOTGRound` binds `OnShot` and plays each shot. The menu and the pause menu show
`SOTGLaunchMonitorPanel`, which reads `OnStatus` and changes the connection.

## Checking it

`node unreal/tests/launchmonitor/lm_check.mjs` sends OpenConnect shots to a running game and checks
what it logs.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder (it needs the engine's Sockets, Networking, Json and WebSockets modules). It's enabled by default. Add
`"OTGLaunchMonitor"` to your module's dependencies in its `.Build.cs`.
