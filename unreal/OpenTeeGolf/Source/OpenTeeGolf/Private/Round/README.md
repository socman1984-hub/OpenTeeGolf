# Round

A round of golf on a course: the golf the web version's `main.js` plays, on the ball physics
(Plugins/OTGBallPhysics), with shots from the launch monitor (Plugins/OTGLaunchMonitor).

`AOTGRound` (`Public/Round/OTGRound.h`) keeps the golf: the hole, the aim and club, the shot and
where it finished, penalties, relief and holing out. `AOTGPlayerController` sends it the keys. The
rest is in parts, each a class of its own (`Public/Round/OTGRoundParts.h`):

| Part | Files | What it does |
|---|---|---|
| `FOTGRoundTurns` | `OTGRoundTurns.cpp` | Players taking turns and the game's scoring (`FOTGGame`, `OTGPlayers.h`). |
| `FOTGRoundCamera` | `OTGRoundView.cpp`, `OTGRoundExtras.cpp` | The cameras: the hole from above, behind the ball, the flight, and the free view. |
| `FOTGRoundHud` | `OTGRoundHud.cpp` | What the HUD shows: the hole, the shot's numbers, the minimap, the wind, banners, the scorecard. |
| `FOTGRoundScene` | `OTGRoundLook.cpp`, `OTGSky.cpp`, `OTGRoundExtras.cpp` | The course's look, the weather on the sky, sun and fog, the Meridian sky, storms and the weather's sound. |
| `FOTGRoundChanging` | `OTGRoundExtras.cpp` | Changing weather, blended from one state to the next. |
| `FOTGRoundRain` | `OTGRoundRain.cpp`, `OTGRoundExtras.cpp` | The rain near and far, splashes, and the ground getting wet. |
| `FOTGRoundWaterFx` | `OTGRoundExtras.cpp` | Rings on the water from a ball going in, and from the rain. |
| `FOTGRoundFlag` | `OTGRoundExtras.cpp` | The flag's cloth in the wind. |
| `FOTGRoundSlopeGrid` | `OTGRoundSlopeGrid.cpp` | The slope grid on the green (G). |
| `FOTGRoundRangeMode` | `OTGRoundRange.cpp` | The driving range. |

The round's own code is spread by purpose: `OTGRound.cpp` (setup, the hole, every frame),
`OTGRoundShot.cpp` (shots), `OTGRoundInput.cpp` (the keys), `OTGRoundCommands.cpp` (the `OTG.Round.*`
console commands), and the designer hooks in `OTGRoundExtras.cpp`. The tracer is its own plugin
(Plugins/OTGShotTracer). Each part reaches the round through its `Round` pointer.

## Also here

| File | What it is |
|---|---|
| `OTGPlayers.h` | Players' profiles, the round's setup, and `FOTGGame` (formats and scoring). |
| `OTGWeather.h` | `FOTGWeather`: presets, custom weather and live weather (Open-Meteo); the physics plays in it. |
| `OTGRange.h` | The driving range's level and props. |
| `OTGMiniGolf.h` and `OTGMiniGolf*.cpp` | The putt-putt: nine holes it makes itself (the layout in `OTGMiniGolfLayout.h`). |
