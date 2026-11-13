# OpenTeeGolf in Unreal Engine 5: prototype

A test of how an OpenTeeGolf course looks in Unreal Engine 5. The game builds the course as usual and
exports it; `build_course.py` turns the export into an Unreal level: the real terrain, the course's
surfaces, Unreal's own trees and grass, the water, paths and buildings, and every tee and pin.

Status: **prototype, on hold.** Built and looked at for Majestic Springs. Water and the mown turf
still need work; see "Where it stands" below.

## 1. Export a course from the game

1. Start OpenTeeGolf and play a course built from OpenStreetMap (not a practice area). Wait until
   you're on the first tee.
2. Press **Ctrl+Shift+U**. Messages show progress (a couple of minutes: it writes every tree model).
3. The files are in `exports/<course id>/` in the OpenTeeGolf folder (not in git):

| File | What it is |
|---|---|
| `heightmap.png` | 16-bit terrain sized for Unreal's Landscape (1 m between samples, 2 m on big courses); pond beds carved deeper than the game's, every shoreline re-cut on an even slope along the water's smoothed outline (a low bank where the land beside the water is lower than it), the ground round bunkers and banks smoothed |
| `photo.jpg` | The aerial photo (cleaned of shadows when the game has that), with the wider photo round it, over exactly the landscape's area; a power of two a side |
| `layer_*.png` | Fairway (with green fringes, and the fairways the game finds in the photo), green, tee, bunker, water and rough as distance fields from smoothed outlines, a power of two pixels a side (about twice the heightmap's samples) |
| `trees.json` | Every tree: position, height, crown, species, and the game's model, turn and size |
| `tree_N.glb`, `tree_N_*.png/jpg`, `tree_models.json` | The game's own tree models and pictures (used for conifers when there are no Unreal ones) |
| `water.glb`, `water.json` | Every pond and stream surface at its level, as one surface on the landscape's grid that reaches under the banks |
| `water_bodies.json` | The same as lake outlines and river centre lines, for Unreal's Water plugin (not used by default) |
| `props_*.glb`, `props.json` | Cart paths, bridges, buildings and cars |
| `holes.json` | Tees, pins and aim points per hole |
| `course.json` | Sizes, the Landscape import settings, how the coordinates map |
| `play.json`, `surfaces.png` | What the Unreal game needs to play the course: holes, drop zones, trees' and buildings' shapes, and the surfaces every half metre |

## 2. Unreal project setup (once)

1. Unreal Engine 5.4 or later; a **Games > Blank** project, Starter Content off.
2. **Edit > Plugins**: enable **Python Editor Script Plugin** (and **Procedural Vegetation Editor**
   for Megaplants). Restart.
3. From **Fab** (free), add to the project: **European Beech** and **European Hornbeam** (whole
   broadleaf trees) and the **Megaplants Norway Spruce** and **Baltic Pine**. (The grass is made
   by the script: no grass pack needed.)
4. Conifers: open each Megaplant's `PVE_...` graph and **Export** two to four variants as
   **Static Mesh** (Create Nanite Foliage off) named `SM_...`, next to the graph.
   (`unreal/list_assets.py` lists what's in the project, into `exports/ue_assets.txt`.)

## 3. Build a course

1. **File > New Level > Basic**; delete its Floor.
2. Set `EXPORT_DIR` at the top of `build_course.py` to the course's export folder.
3. **Tools > Execute Python Script > `build_course.py`**: with no landscape yet it prints the import
   settings (location and scale) in the Output Log.
4. **Landscape mode** (Shift+2) **> Manage > New > Import from File**: `heightmap.png`, that location
   and scale. **Import**.
   After a new export of the same course, load the new `heightmap.png` into the landscape you have:
   **Landscape mode > Manage > Import**, that file, **Import** (Location Z and Scale Z must match
   `course.json`; if they don't, set them or make the landscape again as above).
5. Run `build_course.py` again. It builds, each part reported in the Output Log:
   - the ground: turf colours lit by the photo's light and shade (blurred, so its pixels don't
     show), the game's lawn, rough and sand textures for detail, crisp smooth edges from the
     distance fields, wet banks darker
   - the turf: real blades of grass made by the script (`TURF_GRASS`), thick 5-10 cm rough and
     short fairway grass, each cut off exactly at its surface's edge (Nanite; Unreal grows it near
     the camera, out to 90 m for the rough)
   - the trees: Unreal's beech and hornbeam for broadleaf trees and the exported spruces and pines
     for conifers, each where the game plants it, at its height
   - the water: every pond and stream as one surface meeting its banks, Single Layer Water shading,
     ripples at three sizes drifting in the wind with calmer and rougher patches
   - cart paths, bridges, buildings and cars
   - a marker on every tee and pin
6. At the top of `build_course.py`: `BUILD` picks which parts to rebuild, `REIMPORT = True` brings
   in files from a new export, and the turf (`TURF_GRASS`), ground colours (`TURF`), water and
   texture-memory settings are there too. Unreal only grows the grass while its window is on
   screen.

## 4. Play a round (first step)

OpenTeeGolf does the golf, Unreal shows it: the server takes the shots from the launch monitor,
works out each one's flight, bounce and roll with the game's own physics and keeps the score
(`lib/unrealplay.js`); `play_course.py`, running in the editor, flies the ball along each shot's
path with a tracer and the camera following, then waits behind the ball, aimed at the pin.

1. Start OpenTeeGolf as usual (a version with this; restart it after updating). Connect the launch
   monitor on its setup page as for the game.
2. Build the course in the level (above), from an export of the same course.
3. In `play_course.py`: `SERVER` is OpenTeeGolf's address (`http://127.0.0.1:8080` on the same PC),
   `COURSE` the course's id, `START_HOLE` where to start.
4. **Tools > Execute Python Script > `play_course.py`**. The round starts on the first tee; hit.
   Each shot plays out, the ball is played from where it finishes (out of bounds: stroke and
   distance; water: a drop and a stroke), and when it's holed the next hole starts after a few
   seconds. Where the ball is, the distance and the score show in the Output Log (and at the top
   left of the viewport).
5. Run `play_course.py` again to stop.

No launch monitor? `POST /api/test-shot` with `{"Speed": 150, "VLA": 12, "HLA": 0, "TotalSpin": 2700,
"SpinAxis": 0}` (mph and degrees), or `node test/send-shot.js 150 12 0 2700 0`, plays a shot.

Not yet: trees (the ball flies through them), aiming (every shot is aimed at the pin), wind,
several players, a scorecard on screen, and playing as a game (Play in Editor / a packaged game).

## 5. The standalone game (in progress)

`unreal/OpenTeeGolf/` is the start of OpenTeeGolf as a game of its own: courses made in the web
version, built into course packs, played without a browser or the editor. Everything the golf needs
runs inside the game, in C++.

- Done: the project (Unreal 5.8, C++; needs Visual Studio with "Game development with C++"), and
  the ball physics, `Source/OpenTeeGolf/.../Physics/OTGBallPhysics.*`: `public/js/openfairway.js`
  and `public/js/physics.js` line for line (flight, wind, bounce, roll, surfaces, the cup, trees,
  solid things).
- `node unreal/tests/physics/parity.mjs` plays the same shots on the same test holes through both
  (with the same random numbers) and fails if any differ. Run it after changing the physics on
  either side, and change the other side to match.
- Play data: the export also writes `play.json` (holes, tees, pins, drop zones, the trees' and
  buildings' shapes for the ball, in the game's course metres) and `surfaces.png` (the game's own
  surfaces every half metre). `OTGPlayCourse` plays on them, on the export's heightmap: the ground
  Unreal draws. `node unreal/tests/physics/course_check.mjs [course id]` plays shots from every hole
  through the JS and the C++ on that data (they must agree), and says how far the ball finishes from
  where the web game's own course puts it.
- Course packs: `powershell -File unreal/make_pack.ps1 <course id> [...]` builds each exported
  course into the game project headless (`pack_course.py`: a level with its landscape from the
  export's heightmap, everything `build_course.py` builds, the play data as `CourseData`, and a label
  that gives the course a pak of its own); `make_pack.ps1 -Package` packages the game with every
  course built so far into `unreal/OpenTeeGolf/Dist/Windows`. The game finds them (console
  `OTG.Courses`, `OTG.Play <id>`; `-course=<id>` on the command line opens one at start).
  The game project needs the Fab trees in its `Content` folder (copied from the test project, not in
  git). A pack is made with the game it's packaged with: add a course, package again.
- Launch monitors: `OTGLaunchMonitorSubsystem` takes shots the same ways the web version does,
  ported from `server.js`, `lib/mevo.js` and `lib/frp.js`:
  - OpenConnect on TCP 921, for FS Golf. It's always listening, and it reports when another
    program already has the port.
  - The Mevo+ directly over its Wi-Fi. The client handshakes, sets the detection mode by club and
    distance, applies the saved setup, arms on the player's turn, and stays disarmed after a shot
    until the next turn. It reconnects when the Mevo+ drops, disarms it when the game quits, and
    logs every frame to `Saved/Logs/mevo-log.txt`.
  - A Garmin R10 or Square Golf through their bridge programs. The game looks for the program in a
    `bridges` folder next to the game, in the game project, or in the web version's `bridges`
    folder, which the web version fills on first use. The game doesn't download it itself yet.
  - Any Flight Relay Protocol device at an address.

  The choice of connection and the Mevo+ setup are saved in `GameUserSettings.ini`. Console
  commands: `OTG.LM.Status`, `OTG.LM.Connect fsgolf|direct|r10|square|frp [address]`,
  `OTG.LM.Player <club> [yards] [RH|LH]`, `OTG.LM.Turn 0|1`, `OTG.LM.TestShot [mph vla hla spin
  axis]`. On the command line, `-lm=<kind>` picks the connection for one run.
  `node unreal/tests/launchmonitor/lm_check.mjs` runs the game headless against the web version's
  fake Mevo+, fake FRP device and fake bridge, plus an OpenConnect shot, and checks every shot
  against what the web version makes of it. It also checks the Mevo+ dropping out and quitting
  while armed. It needs the editor target built and no editor open.
- The round (`Source/OpenTeeGolf/.../Game/`): `OTGGameMode` starts a round when a course's level
  opens (`-course=<id>`, optionally `-hole=<n>`). `AOTGRound` plays it the way `main.js` does:
  - The hole flow and aiming, with the auto club and the lie's effect on the strike.
  - Penalties: stroke and distance for out of bounds, and a drop for water (at the export's drop
    zone).
  - Free relief from cart paths, holing out, picking up at par + 5, the scorecard, then the next
    hole.

  Shots come from the launch monitor, and the physics is the C++ port. You see the ball, its
  tracer, the aim marker, the flag and cup, three cameras (the hole from above the tee, behind the
  ball, following it in flight) and a Slate HUD: the hole, the lie, the club, the shot data, the
  launch monitor's status, banners and the scorecard.

  Keys: Left/Right aim (Shift: faster), Up/Down club, Space test shot, Enter skip / next hole,
  Tab scorecard, Backspace mulligan. Console: `OTG.Round.TestShot`, `OTG.Round.Aim <deg>`,
  `OTG.Round.Club 1|-1`, `OTG.Round.Continue`, `OTG.Round.Drop <yd>` (testing), `OTG.Shot <name>`
  (a screenshot). For checking without anyone at the keyboard, `-roundexec="<s>:<command>|..."` runs
  commands at set times after the round starts, and `-RenderOffscreen` renders without a window.
- Menus and players (`SOTGMenus`, `OTGPlayers`): the game starts on a menu with three pages.
  - Play: the course pack, tees, pins, starting hole, 18 or 9 holes, who's playing (in tee-off
    order, with teams), the game and whether handicaps count.
  - Players: name, colour, handicap, hand and own tees.
  - Launch monitor: the connection, its live status, and the Mevo+ setup and arm-on-turn.

  `FOTGGame` is `players.js`'s round in C++, with all seven formats and handicap strokes by stroke
  index. Whoever's away plays next, and the honour goes to the best score on the hole before. The
  other balls lie in their colours, a leaderboard shows at the top right, and the scorecard has a
  row per side. Each player's finished rounds are added to their profile.

  Esc pauses the round: Resume, Scorecard, Quit to the menu, and the launch monitor panel.
  Profiles and the last setup are kept in `Saved/OpenTeeGolf/players.json` and `setup.json`.
  Console: `OTG.Menu.Start`, `OTG.Menu.Page play|players|lm`, `OTG.Round.Pause`.
- Turf in the packaged game: the grass maps are built at runtime (`grass.GrassMap.UseRuntimeGeneration`
  in `DefaultEngine.ini`), since the headless pack builder never bakes them. Every material
  `build_course.py` makes declares its Nanite and instanced-mesh use up front (`with_usage`): the
  editor adds a missing flag as it goes, but a packaged game draws such a material as the grey
  default. The blade data (height up the blade, shade, dry tip) and the buildings' colours also go
  in UV channels as well as vertex colours (`colours_in_uvs`), and the materials read the UVs.
- What the ball hits (`PlayCollider`, `OTGPlayCollider.cpp`): the web game's collision world without
  Jolt.
  - The ground: the heightmap.
  - Tree trunks: a metre into the ground.
  - Buildings and cars: their footprint's convex outline.
  - The flagstick.

  Each is grown by the ball's radius and cast analytically. With it the physics bounces off
  trunks, walls and the pin as the web game does ("Off the flagstick!").
- Weather (`OTGWeather`): the menu's Conditions are Live (Open-Meteo at the course's location; breezy
  until it answers or if it can't), Calm, Breezy, Windy, Overcast, Fog, Rain, Storm, or Custom
  (mph, and into you / behind / right to left / left to right / random per hole).
  - The physics plays a gust each shot, the air (temperature, humidity, pressure) and a wet ground
    in rain.
  - The HUD gives the wind against your aim ("Wind 18 mph from the right").
  - The sky follows it: cloud cover thickens the level's volumetric clouds and greys the sky
    (less blue scattering, more haze, a dimmer sun and sky light). Rain falls as streaks round the
    camera, blown by the wind (`M_Rain`, made by `make_game_assets.py`: `make_pack.ps1 -Assets`, and
    before every `-Package`). A storm darkens the clouds, and fog and rain thicken the fog.
- On the round's screen (as the web version's):
  - The minimap: the course photo turned so the hole runs up it, with the ball, the aim line and
    point, the pin, the other players' balls and the yardages (to the aim point and what it leaves,
    or under the cursor). Click or drag on it to aim, or click the ground itself.
  - The wind box: an arrow against the aim (up: helping), speed and gusts, "into you / helping /
    cross", and the weather.
  - T opens the test shot panel: Driver, 7 iron, Wedge, Chip and Putt (sized to the hole) presets,
    ball speed, launch, direction, spin and axis to change, and Hit, played as if the launch
    monitor had sent it.

  `-weather=<key>` overrides it for a run; `OTG.Round.CastTest` (testing) logs what a ball flying
  at the flagstick, the nearest trunk and the ground meets.
- Settings (the menu's last page): full screen or a window, the resolution, the graphics quality
  (Unreal's scalability levels, or "Pick for this PC"), vertical sync - saved in
  `GameUserSettings.ini`. The menu's footer gives the version and where the course data comes from.
- The release: `powershell -ExecutionPolicy Bypass -File unreal\make_release.ps1` packages the game
  in Shipping with every course pack built so far (and the version from `package.json`, the
  window icon `Build/Windows/Application.ico`). It then adds the Garmin R10 and Square Golf
  bridges (`bridges/`, beside the game, where it looks for them) with their licences, a player
  README and the game's notices (`release/README.txt`, `release/NOTICE.md`) and the licence,
  into `dist/OpenTeeGolf-Unreal-<version>-win-x64.zip`. Unzip anywhere and run `OpenTeeGolf.exe`
  (a PC without the Visual C++ runtime needs Microsoft's redistributable; the player README says
  where). The Shipping game keeps its saves in `%LOCALAPPDATA%\OpenTeeGolf\Saved`. Before sharing
  it, read `release/NOTICE.md`:
  a pack of a course outside the US carries Esri imagery (personal, non-commercial use only), and
  the Fab trees may only ship inside the game.
- The look (`AOTGRound::ApplyLook`): a clear summer day, as golf games show one. The round
  grades the picture (a touch darker exposure, more saturation and contrast, warmer white
  balance, a light vignette), strengthens and warms the sun and lights each hole from behind the
  player, takes haze out of the sky, and lifts the trees' shade (Lumen sky light let into the
  crowns, lifted shadows, a stronger sky light). Every setting is a console variable
  (`OTG.Look.Exposure`, `Saturation`, `Contrast`, `Temp`, `Sun`, `SunWarm`, `SunPitch`, `SunSide`,
  `Haze`, `Fog`, `Vignette`, `TreeLift`, `Shadows`, `Sky`), applied live. The turf and ground colours
  (`TURF`, `TURF_GRASS` in `build_course.py`) are warm yellow-green fairways and greens and a
  darker olive rough.
- Tall native grass: `derive_layers.py` (run by `make_pack.ps1`, system Python with numpy and
  Pillow) writes `layer_native.png` from the export's play data:
  - the web game's deep rough and native ground, and the ground under every tree's crown;
  - never a fairway, tee, green, fringe, first cut, path, bunker or water.

  `build_course.py` grows wild grass there: clumps of tall thin blades (35-75 cm), some gone to
  straw, many with seed heads, drawn to 130 m.
- In the round, as well:
  - F, the free view: fly round the course.
    - W A S D or the arrows fly; Q and E (or Space) go down and up; Shift goes faster.
    - Hold the right mouse button to look round.
    - It flies faster the higher you are. F or Esc comes back.
  - P, the frame rate: frames a second and ms a frame; the game, draw and GPU ms; the
    resolution and the quality level. Green from 55 fps, amber from 30, red below.
  - L, the weather panel: the conditions change at once.
    - A preset, or the course's live weather.
    - Sliders for the wind's speed and direction against the hole, the cloud, rain, fog and the
      temperature.
    - The physics plays them from the next shot.
  - The flag is cloth (`AOTGRound::UpdateFlag`): it hangs limp in calm air and flies out downwind,
    rippling and fluttering more the harder it blows and with each gust.
- Running water: the water material reads a stream from its own surface's slope.
  - Running water is roughened, and its ripple pattern travels downstream: two copies half a
    cycle apart, crossfaded so it never visibly resets. Flat ponds keep their wind ripples.
  - `STREAM_*` in `build_course.py` set how steep a stream must be to run fully, how fast it
    runs, the ripples' size and how rough it is.
  - The turf now stops at the water (it used to grow in the creek beds).
- The menu: the chosen course's aerial photo behind it, a course card with its picture, rounded
  buttons that light under the mouse, and the page shown lit in the tabs.
- Testing commands: `OTG.Round.FreeView`, `OTG.Round.Fly`, `OTG.Round.LookAt <X Y Z | pin>`,
  `OTG.Round.Perf`, `OTG.Round.WeatherPanel`, `OTG.Round.Weather <key | wind | from | cloud | rain | fog> [value]`.
- Still to come in the game itself: the time of day, putting aids, the Mevo+ camera view, a darker
  stony bed under the creeks.

Build: `Engine\Build\BatchFiles\Build.bat OpenTeeGolf Win64 Development -Project=<path>\OpenTeeGolf.uproject`
(the editor target, `OpenTeeGolfEditor`, can't build while another Unreal Editor with Live Coding is
open).

## Where it stands

- Looks good: the Megascans trees, the turf (thick rough, crisp fairway edges), round bunkers, the
  terrain, the cart paths, and ponds with smooth shorelines, moving ripples and reflections.
- Far off, past where the rough's blades are drawn, the rough is a little lighter than up close.
- Steep pond banks seen from high up can still show small steps along the shore.
- Mown turf: no mowing stripes yet; bunkers have no lip.
- Conifers exported as static meshes don't sway.
- Not started: the land beyond the course on the horizon.
