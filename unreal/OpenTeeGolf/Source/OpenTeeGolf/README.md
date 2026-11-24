# OpenTeeGolf: the game module

The game: rounds of golf on courses built at run time from the web version's exports. The code is
in folders by area. Each area has a README, under `Private/<area>/README.md`:

| Folder | What's in it |
|---|---|
| `App/` | The game mode, and the game instance's services: the course helper (the web version's server, run hidden) and updates. |
| `Course/` | Courses as data: finding them, loading play data and pictures, the course editor's source files, quick updates. |
| `World/` | The course built in the level at run time: ground, water, trees, far land, props, grass. |
| `Round/` | A round of golf: the hole, shots, turns, the camera, the HUD's wiring, the weather on the scene; the range and the putt-putt. |
| `UI/` | The Slate widgets: menus, the course builder, the designers, the course editor, the HUD. |

`Public/<area>/` holds the headers other areas (or modules) use; `Private/<area>/` holds the rest.
Every header is included by its folder (`#include "Round/OTGRound.h"`), public or private.

## Plugins

Self-contained pieces are project plugins in `unreal/OpenTeeGolf/Plugins/`, each with a README. The
game depends on them; they never depend on the game.

| Plugin | What it is |
|---|---|
| `OTGBallPhysics` | The ball's flight, bounce and roll (plain C++, the same as the web version's). |
| `OTGLaunchMonitor` | Shots from OpenConnect, the Mevo+ directly, and FRP devices. |
| `OTGUiKit` | The game's Slate look: fonts, colours, panels, buttons. |
| `OTGMinimap` | The hole's minimap, with yardages and click to aim. |
| `OTGWindIndicator` | The wind box: an arrow and two lines. |
| `OTGShotTracer` | The ball's flight as a ribbon. |
| `OTGWeatherSound` | Rain, wind and thunder made as they play. |
| `OTGGlb` | Reading `.glb` files at run time. |

The other plugins there (Greensward, MeridianSky, Tidewright, HearthlineHomes, CarPackFab) are
third-party content the game uses.

## Building

Close the editor first, then:

```
Engine\Build\BatchFiles\Build.bat OpenTeeGolfEditor Win64 Development -Project=<path>\OpenTeeGolf.uproject
```

`unreal/make_release.ps1` packages a release; enabled plugins are built into it.
