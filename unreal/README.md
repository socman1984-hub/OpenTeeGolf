# OpenTeeGolf in Unreal Engine 5: prototype

A test of how an OpenTeeGolf course looks in Unreal Engine 5. The game builds the course as usual and
exports it; `build_course.py` turns the export into an Unreal level: the real terrain, the course's
surfaces, Unreal's own trees and grass, the water, paths and buildings, and every tee and pin.

Status: **prototype, on hold.** Built and looked at for Majestic Springs. Water and the mown turf
still need work; see "Where it stands" below.

## 1. Export a course from the game

**The easy way: one click.** In the web version's menu, click **Export to Unreal** on the course's
card (next to Edit; courses built from OpenStreetMap only). It loads the course to its first tee
out of sight and exports it, behind an export screen (the stages ticked off, one progress bar, what
it's doing, and a random fact every 12 seconds from `public/js/facts.js`). The server writes the
export to `exports/<course id>/` and to the Unreal game's courses folder,
`Documents\OpenTeeGolf\Courses\<course id>\`. That's all: the game builds the course itself
when it's opened (section 5). Nothing is built in the Unreal Editor, and courses never go in git.

By hand:

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
| `layer_native/path/fringe/stream/mow.png`, `boulders.json` | Worked out from the play data by `public/js/derive-layers.js`: the tall native grass, the cart paths, the green fringes, the stream beds, the mowing directions and where boulders lie |

## 2. Unreal project setup (once)

1. Unreal Engine 5.4 or later; a **Games > Blank** project, Starter Content off.
2. **Edit > Plugins**: enable **Python Editor Script Plugin** (and **Procedural Vegetation Editor**
   for Megaplants). Restart.
3. From **Fab** (free), add to the project: **European Beech** and **European Hornbeam** (whole
   broadleaf trees) and the **Megaplants Norway Spruce** and **Baltic Pine**. (The grass is made
   by the script: no grass pack needed.) Optional: Epic's **Open World Demo Collection** (KiteDemo,
   in `Content/KiteDemo`) for the heather, wildflowers, Scots pine, hill oak and rocks below.
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
version, built by the game itself when they're opened, played without a browser or the editor.
Everything the golf needs runs inside the game, in C++.

- Done: the project (Unreal 5.8, C++; needs Visual Studio with "Game development with C++"), and
  the ball physics, `Plugins/OTGBallPhysics` (`OTGBallPhysics.*`): `public/js/openfairway.js`
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
- Courses at run time: the game lists every folder with a `play.json` in its courses folder
  (`Documents\OpenTeeGolf\Courses`; Settings > Courses folder, `CoursesFolder=` under
  `[OpenTeeGolf]` in `GameUserSettings.ini`, or `-courses=<folder>` on the command line). Opening one
  reads its play data (`UOTGCourseData::LoadFromExport`) and opens the template level
  `/Game/OpenTeeGolf/Game/L_Course` (sky, sun, fog, clouds), where `AOTGRuntimeCourse` builds the
  course a step a frame behind a loading screen (`SOTGLoading`): the photo, masks and mowing map as
  textures with mips (`OTGTextures`), the ground as tiled static meshes with four levels of detail,
  the trees (the Fab broadleaves and Megaplant conifers, chosen by height, with the course's
  stand-in materials), boulders, water and props from the export's `.glb` files (`OTGGlb`), and the
  grass grown round the camera in 16 m cells on worker threads (`OTGGrass`, the same kinds and
  densities as `build_course.py`'s `TURF_GRASS`). The materials, turf patches and boulder meshes it
  uses are made once into `/Game/OpenTeeGolf/Game/Runtime` by `make_game_assets.py`
  (`powershell -File unreal/make_assets.ps1`, which runs `build_course.py` in its `OTG_RUNTIME`
  mode). Console `OTG.Courses`, `OTG.Play <id>`; `-course=<id>` on the command line opens one at
  start. The game project needs the Fab trees in its `Content` folder (not in git).
- The driving range (`Round/OTGRange.*`, `UI/SOTGRange.*`): a practice ground the game makes itself, no
  download or export. `otg::range::Ensure` writes it as an export folder in `Saved/Range` (play.json,
  heightmap, surfaces, photo, masks, mowing, trees, and `farland.json` with made-up mountains: a valley,
  foothills and snowy peaks 10-30 km off, highest beyond the far end) the first time, and again only
  when its layout's version changes; `UOTGCourseSubsystem::PlayRange` opens it in `L_Course` like any
  exported course. Laid out as the web version's (`practice.js` drivingRange): a mown fan from a tee
  line 64 m wide out to a bank at 330 m, target greens with flags at 50, 100, 150, 200 and 250 yd
  (the 150's is the round's own flag and cup), two bunkers, woods down both sides. `AOTGRangeProps`
  adds the hitting mats, the other flags (blown downwind) and a distance board every 50 yd down both
  sides (figures made of blocks: engine shapes only, so nothing extra to cook). `AOTGRound` plays it
  in its range mode: one player, the ball teed on the middle mat, the club as picked (7 iron to
  start, no putter), the aim at the club's carry; after each shot the range panel (where the minimap
  is) shows carry, total, offline, apex, ball speed, launch, direction, spin and axis, and the club's
  recent shots with averages and spread; gold marks show where the club's last ten stopped. A new
  ball comes 3.5 s after the last stops (Enter or Backspace: at once; Space on a resting ball tees a
  new one and hits it). Tab hides the panel; L (weather and time of day), T, F and the launch monitor
  work as on a course; "live" weather is calm there. Open it with Driving range on the menu's
  Practice tab, `OTG.Range` (console) or `-range` (command line); `OTG.Range.NewBall` for testing.
  Course packs (levels built in the editor under `/Game/Courses`) are retired: the editor tools
  are still there, but packs are never cooked into the game.
- The grass designer (R in a round, `SOTGGrassDesigner`, phase 1): the grass kinds are no longer a
  fixed table: `otg::FGrass::DefaultKinds()` is the game's own (plus tall wild grass, golden fescue
  and tussocks from `TURF_GRASS`, off until picked), and each course's `grass.json` (in its folder)
  changes them - which grow and where, density, height and its spread, draw distance, patch size
  and cover - and adds Megascans plants (any folder under `Content/Fab/Megascans/Plants`, all
  cooked) or KiteDemo plants (heather, bracken, yarrow, buttercups, field scabious, bog myrtle,
  field grass, fallen leaves: `OTGKiteDemo.h`, a kind's `Fab` entry `kite_<name>`) to the tall grass.
  Heather is one of the game's own kinds (`heather`, in its own patches through the tall grass);
  KiteDemo plants keep their own colours (no tint in their materials: the designer says so). `AOTGRuntimeCourse::SetGrassKinds` grows them again at once; the
  far-off copies (`_far`) follow their near kinds (`FGrass::Derive`). The rough stays one even
  colour: only kinds in its colours (`bEven`: rough, rough_clump) may grow on it. Console:
  `OTG.Grass.List`, `OTG.Grass.Set <kind> on|surface|density|height|vary|out|patch|cover|hue|saturation|brightness <v>`,
  `OTG.Grass.Add <folder>`, `OTG.Grass.Save`, `OTG.Grass.Reset`, `OTG.Grass.Show <kind>`.
  Each kind's colour (`FGrassKind::Hue`, `Sat`, `Bright`: the hue turned up to 40° either way, its
  strength 40-160%, its brightness 50-160%, saved in `grass.json` as `hue`, `saturation`,
  `brightness`) changes as you drag, with nothing grown again (`AOTGRuntimeCourse::SetGrassColours`):
  our turf's materials take a `Tint` multiplier (`build_course.py` `turf_material`: all three), and a
  Megascans plant's materials are copied (`GrassSwaps`) with their `Base Color Controls` set - x the
  colour's strength (under 1 it washes out: kept to 0.7-1.24), y its brightness (0.65-1.2); z
  washes it to white and is left alone. Their `Base Color Tint` hardly turns the hue (even a strong
  green barely shows), so a Megascans plant keeps its own hue and the designer hides that slider for
  it. "Show me" on a kind flies the free view to where it grows thickly nearby (`FGrass::FindNear`:
  rings out from the camera, the same odds the grass is placed with, `FGrass::Chance`). Testing:
  `OTG.Grass.Tints` logs the tinted materials and what the plants are drawn with,
  `OTG.Grass.Param <kind> <vector_param> r g b` sets one on a kind's Megascans materials.
- Quick updates (phase 5, `otg::QuickUpdate`, the editor's "Save and update the game"): the course's
  edits put in its export folder in seconds, without the web game's export - the holes (play.json,
  holes.json, with course.js's path and green radius), the play surfaces and the green/fairway/tee/
  bunker/rough masks round greens, fairways, tees and bunkers that changed (surfaceAt's rules:
  fringe 1.5 m, first cut 2 m, paths kept; masks as exact distances, 0.5 on the outline, 3 m either
  way), new bunkers' hollows (heightmap.png), tall grass strokes added, trees removed, your own
  added or moved and new tree areas planted (trees.json and play.json's trees together), then
  layer_native/fringe/mow.png made again from the play data (a port of derive-layers.js). Water,
  the boundary, penalty areas, tree areas changed or removed, strokes or trees taken back,
  building types, species and looks wait for the full export (it says which). It compares with the
  course as the editor opened it (the export folder is taken to match the saved course then).
  `-editortestquick` with `-editortestall` runs it.
- The course editor (phase 4; Edit on a built course in New course, `SOTGCourseEditor`,
  `SOTGEditCanvas`): the web editor's map and panels - holes (number, par, handicap, the eight
  tees with yardages, two aim points, the green's centre, four pins, Spread pins, add and delete),
  shapes (draw, pick, drag corners, add corners, move, change kind, delete; water as a stream;
  penalty areas with OOB inside; tree areas with density, species and heights), trees (add, pick,
  move, remove brush, remove or replace found ones, shift-drag a box, review those in the line of
  play, species on the course on/off), tall grass brush and rubber, buildings' types, the course's
  looks (name, location, stimp, game or photo colours, rough and tall grass heights, mowing), the
  shrubs, flowers and grasses, trees coloured by species, mowing swatches, the tall grass the game found
  in the photo (`/api/courses/<id>/heather`) under your strokes, undo, Save, "Save and put in the game" (the export). Its picture:
  the export's photo, or shading from the source's heights. The found trees come from the course's
  play data (when it's in the game), the buildings from the helper. Panels are made again from an
  active timer, not Tick (made while drawing, they're drawn once unmeasured). `-ExecCmds="OTG.Menu.Edit
  <id>" -editortest=<id> [-editortestfix | -editortestall]` (a par of 10+ set to 4 / each tool's
  change, two undone) saves and quits, with `Saved/Screenshots/editor_*.png`.
- Finding and building a course (the menu's New course page, `SOTGCourseBuilder`, phase 3): a world
  map (`SOTGWorldMap`: OpenStreetMap or Esri's aerial photos, tiles fetched and kept on disk by the
  helper, `/api/helper/tile`), the world's mapped courses as dots (`/api/builder/world`, paged while
  the helper gathers them), a search, the picked course's check (holes, greens ..., the set of
  holes at a club with several), Build, Play it; the courses built, to export again, update or delete.
  Builds, updates and exports all go in the helper's queue (`lib/queue.js`, `/api/helper/queue`;
  the game's `UOTGHelperSubsystem::GetQueue`): two builds at a time, one export at a time (the next
  course builds meanwhile), each built course then put in the game. It's kept in
  `<data>/queue.json` and carries on when the game is started again (the helper started at once if
  anything's left). The page's queue card shows each one (a clock, its step, its last messages; stop,
  remove, try again, play it); a round shows a toast when one is ready. The editor's full export is
  queued too. `-ExecCmds="OTG.Menu.Page build" -buildertest=<words> [-buildertestbuild
  [-buildertestexport=<id>]]` searches, picks the first, takes `Saved/Screenshots/builder_*.png`
  (and builds and exports it, with another course's export queued alongside), then quits.
- The tree designer (R, the "Trees and rocks" tab, `SOTGTreeDesigner`, phase 2): each course's
  `trees_design.json` (`FOTGTreeDesign`): per species the model it's drawn with (the game's choice
  by height, or beech, hornbeam, Norway spruce, Baltic pine, and KiteDemo's Scots pine and hill
  oak), its size and how many (fewer: taken out by a hash of the position; more: copies round each
  tree, never on mown grass, sand, paths or water), all trees together, and the rocks (on, size,
  how many, and their look: the game's six stones or one of KiteDemo's - mossy, grey, river,
  field, big weathered, volcanic - `rocks.model`; a picked rock can have its own, `rockEdits[].model`).
  A KiteDemo model or look loads only when a course uses it; without the pack, a tree drawn as one
  is drawn as its species and a rock as the game's stones. `make_game_assets.py` (`kite_usage`)
  marks the pack's materials for instancing (else Unreal draws its grey default). `trees.json` (drawn) and
  `play.json`'s trees (the ball's) are the same trees in the same order, so both go through
  `UOTGCourseData::ForEachDesignedTree`: the ball meets the trees as drawn
  (`AOTGRound::RefreshTrees` after each change). "Show me" flies the free view to the nearest tree
  of a species. Console: `OTG.Trees.List`, `OTG.Trees.Set <species|all|rocks>
  model|size|density|on <v>`, `OTG.Trees.Save`, `OTG.Trees.Show <species>`,
  `OTG.Round.GrassDesigner trees` (the designer on that tab).
  One at a time, by clicking on the course with the trees tab open (in the free view too; the
  click goes to `AOTGRound::DesignClick` instead of aiming): Pick one (a tree: its species, model,
  height, leaf colour and brightness; a rock: its size; either taken out or put back as it was),
  Add a tree (any species, 2-40 m), Add a rock, Take out. Kept in `trees_design.json` as `trees`
  and `rockEdits` (`FOTGTreeEdit`, `FOTGRockEdit`), each found again by where it stands (trees in
  course metres, rocks in the level's metres as boulders.json has them, to 15 cm).
  `ForEachDesignedTree` passes each tree's edit, and the added trees last (`Src` -1, a trunk and a
  crown for the ball from the species and height: `UOTGCourseData::AddedShape`), so the ball meets
  them too. A tree's leaf colour is per-instance data (three floats, a multiplier less one) that
  `M_TreeLeaves` (the stand-in leaves, `tree_parent_material`) multiplies in: trees drawn with their
  own pack material (the conifers: no stand-in) keep their colour, and the panel says so
  (`FOTGPicked::bColourable`). Each change draws every tree again (about a second on a course of
  5000). Picking (`AOTGRuntimeCourse::Pick`) is against
  each tree's trunk line and crown reach and each rock's size, nearest along the line before the
  ground. Testing: `OTG.Trees.Click <0..1 across> <0..1 down> [pick|tree|rock|out]` clicks there,
  `OTG.Trees.Edit size|height|hue|bright|kind|model|out|reset [v]` changes the picked one, and
  `OTG.Later <s> <command>` runs a command later (the screenshot's `-shotcmd` all run at once).
- The course helper (making courses from the game, phase 0 of moving the course builder and editor
  into it): `UOTGHelperSubsystem` runs the web version's server hidden (`OTG_HELPER=1`,
  `lib/helper.js`: this computer only, no launch monitor listeners, gone with the game) on a free
  port, and the game finds, checks, builds and exports courses through it. The export runs the
  web game's own Export to Unreal in headless Edge or Chrome (`/?export=<id>`; a fresh browser
  profile each time), which reports how far it is; the game asks twice a second. In a development
  copy it uses the repository's own `courses` folder and Node; a release carries it in `Helper\`
  with its own Node.js (`make_release.ps1` stages it with `scripts/package-win.ps1 -NoZip`) and
  keeps the courses' sources and downloads in `Documents\OpenTeeGolf\CourseSources` and
  `HelperData`. `FOTGCourseSource` reads a course's source as the web editor does and saves it
  through the helper (the same body as the editor's Save). Console: `OTG.Helper.Start`, `.Status`,
  `.Search <words>`, `.Check <way|relation> <id>`, `.Build <way|relation> <id> [name]`,
  `.Export <id> [visible]`, `.Cancel`, `.Load <id> [save]`; `-helperexport=<id>` exports a course
  and quits (for testing).
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
- The round (`Source/OpenTeeGolf/.../Round/`): `OTGGameMode` starts a round when a course's level
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
- The putt-putt (`Round/OTGMiniGolf.*`): nine holes of mini golf the game makes itself, with no
  course folder. The first six are the web version's (`public/js/practice.js` `puttPutt`): Warm-up,
  Dogleg, The ramp, The windmill, Tunnel and volcano, Zig-zag. Three are new: Camel humps, The pond
  (a narrow bridge over water) and The big bend (a banked U-turn). Each is par 2 or 3.
  - The play data: `AOTGMiniGolf::MakeCourse` fills a `UOTGCourseData` (`Generated` "minigolf") with
    the holes and their names, a 5 cm heightmap (ramps, humps, the volcano, the pond's dip, the
    bank) and 2.5 cm surfaces (felt is green, the pond water, the lawn tee). Every board, post and
    block is a `bumper` solid, and the slope is measured over 12 cm (`NormalSpanM`) because the
    ramps are short.
  - The windmill's sails are physics movers: `otg::ShotOptions::movers`, as `physics.js` has them.
    The parity test doesn't use them.
  - Drawing it: `AOTGMiniGolf` draws the course in `L_Course` from procedural meshes and the
    engine's basic shapes. That covers the felt draped over the ground, the boards in each hole's
    colour with white caps, posts, the windmill (its sails turning by the same clock the ball's
    movers use), the stone tunnel, the volcano's rocks, the pond's bridge and lily pads, signs,
    flower beds, a picket fence, trees and far hills.
  - In the round, `AOTGRound` (`bMini`) makes every shot a putt and aims the first putt at the
    board to bank off. Anywhere off the felt, or in the pond, costs a stroke and you replay from
    where you were. A hole ends at 6 strokes. Other differences: calm air, no slope grid, a short
    flag, no minimap, distances in feet, and cameras sized to the hole. The test shot's putt allows
    for the turn and for any climb.
  - Opening it: `-minigolf` (or `-course=minigolf`) on the command line, `OTG.MiniGolf` in the
    console, or Putt-putt on the menu's Practice tab, which uses the players and game picked on the Play tab.
- Menus and players (`SOTGMenus`, `OTGPlayers`): the game starts on a menu with these pages.
  - Play: on the left the courses, with a search (every word typed must be in the name or the
    place) and three orders: A-Z, recently played (`setup.json`'s `lastPlayed`, set by Start, and
    the players' saved rounds) and newest (the course folder's creation time). Thumbnails load one
    a frame as their rows come into view, so a long list opens at once. On the right the course
    picked (its photo, place, holes, par, length from the tees picked, when it was last played),
    the round's options in four cards (Round: tees, pins, 18 or 9, start hole; Conditions;
    Players, in tee-off order with teams; Game: the format and handicaps), and a bar with what's
    set and Start the round. With no courses it says how to build the first one.
  - Practice: Driving range and Putt-putt (`SOTGMainMenu::SetPracticeMode` gives an entry its start;
    a new name adds one, shown "Coming soon" until it has one).
  - New course: find and build one (below).
  - Players: name, colour, handicap, hand and own tees.
  - Launch monitor: the connection, its live status, and the Mevo+ setup and arm-on-turn.

  `FOTGGame` is `players.js`'s round in C++, with all seven formats and handicap strokes by stroke
  index. Whoever's away plays next, and the honour goes to the best score on the hole before. The
  other balls lie in their colours, a leaderboard shows at the top right, and the scorecard has a
  row per side. Each player's finished rounds are added to their profile.

  Esc pauses the round: Resume, Scorecard, Quit to the menu, and the launch monitor panel.
  Profiles and the last setup are kept in `Saved/OpenTeeGolf/players.json` and `setup.json`.
  Console: `OTG.Menu.Start`, `OTG.Menu.Page play|practice|build|players|lm|settings`, `OTG.Round.Pause`.
  Testing the Play page: `-menusearch=<words>` types them in the search, `-menusort=name|recent|added`.
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
    camera, blown by the wind (`M_Rain`, made by `make_game_assets.py`: `make_assets.ps1`, and
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
- The sky and the rough come from two project plugins in `OpenTeeGolf/Plugins`:
  - **Meridian Sky** (`OTGSky.cpp`): one actor draws the sun, sky, sky light, clouds and lightning,
    in place of `L_Course`'s template ones. The round still owns the weather and the time of day and
    drives the sky from them. The sun sits where it really is for the course's latitude and
    longitude on midsummer's day; "sun behind you" turns the sky so the sun shines down each hole.
    The rain streaks, the height fog, the thunder and the colour grade stay the game's own.
  - **Greensward** (`OTGGrass.cpp`): the deep rough (under the tall grass) is its 15 to 30 cm
    Nanite rough mats, tiled in the game's own streamed grass cells, with no wind, so the rough
    meets it in a clear step up in height. The rough itself stays our own blades: their material
    folds them away exactly on each outline, so the lines round tees, collars and bunkers are smooth.
- The turf's colours come from one palette (`OTGPalette.h`, `OTG.Palette.*`): one hue for every cut,
  the green lightest, then the fairways and tees, the collars, the rough. It feeds the ground's
  material (`TurfTint_<surface>`), our blades and Greensward's mats; the grass designer's colours turn
  it as before. `python unreal/tests/turf_colours.py --course <id> --hole <n>` photographs each cut
  from straight above (`OTG.Look.Swatch`) and checks that they're one hue and step down in lightness.
  - For comparing looks and frame times, `-oldsky` gives the level's own sky and `-oldgrass` gives
    the game's own short meadow grass in the deep rough. `OTG.Sky.CloudBoost` sets the sky light under full cloud.
- Settings (the menu's last page): full screen or a window, the resolution, the graphics quality
  (Unreal's scalability levels, or "Pick for this PC"), vertical sync - saved in
  `GameUserSettings.ini`. The menu's footer gives the version and where the course data comes from.
- The release: `powershell -ExecutionPolicy Bypass -File unreal\make_release.ps1` packages the game
  in Shipping with no courses (players export their own; `DefaultGame.ini` cooks the two maps, the
  game's assets and the tree meshes, never `/Game/Courses`), with the version from `package.json`, the
  window icon `Build/Windows/Application.ico`). It then adds the Garmin R10 and Square Golf
  bridges (`bridges/`, beside the game, where it looks for them) with their licences, a player
  README and the game's notices (`release/README.txt`, `release/NOTICE.md`) and the licence,
  into `dist/OpenTeeGolf-Unreal-<version>-win-x64.zip`. Unzip anywhere and run `OpenTeeGolf.exe`
  (a PC without the Visual C++ runtime needs Microsoft's redistributable; the player README says
  where). The Shipping game keeps its saves in `%LOCALAPPDATA%\OpenTeeGolf\Saved`. Before sharing
  it, read `release/NOTICE.md`:
  the Fab trees may only ship inside the game. (Exported courses outside the US carry Esri imagery,
  for personal, non-commercial use only: they stay on the player's own PC.)
- The look (`AOTGRound::ApplyLook`): a clear summer day, as golf games show one. The round
  grades the picture (a touch darker exposure, more saturation and contrast, warmer white
  balance, a light vignette), strengthens and warms the sun and lights each hole from behind the
  player, takes haze out of the sky, and lifts the trees' shade (Lumen sky light let into the
  crowns, lifted shadows, a stronger sky light). Every setting is a console variable
  (`OTG.Look.Exposure`, `Saturation`, `Contrast`, `Temp`, `Sun`, `SunWarm`, `SunPitch`, `SunSide`,
  `Haze`, `Fog`, `Vignette`, `TreeLift`, `Shadows`, `Sky`), applied live. The turf and ground colours
  (`TURF`, `TURF_GRASS` in `build_course.py`) are warm yellow-green fairways and greens and a
  darker olive rough.
- Tall native grass: `public/js/derive-layers.js` (run by the export) writes `layer_native.png` from the export's play data:
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
- See-through water and a real stream bed.
  - The water used to be opaque (a dark mirror) for two reasons, both fixed in `build_course.py`:
    - Nanite can't draw Single Layer Water, so the water mesh is now a non-Nanite mesh.
    - With Substrate on, a material made by script keeps the Default Lit conversion it got when
      it was new. The water is now wired to Substrate's own Single Layer Water node.
  - The water's coefficients are written per metre and converted to Unreal's per centimetre.
  - Streams get their own clearer, slightly amber water (`STREAM_ABSORPTION`, `STREAM_SCATTERING`).
    Ponds stay a murky green (`WATER_*`).
  - Under all water there's a bed of rounded pebbles and gravel over silt. Two sizes of stones
    in patches, silt in other patches, darker toward the middle (`BED_*`).
  - The pebbles come from `make_bed_texture.py`, which writes `textures/creekbed_color.png` and
    `creekbed_normal.png`. It's generated from scratch, with no outside images.
- Banks, beds and boulders.
  - `derive-layers.js` now also writes `layer_stream.png` (where the water's surface slopes: a
    stream) and `boulders.json` (groups of stones along the creeks, a few by the ponds).
  - Pebbles lie only on a stream's flat, deeper middle (`BED_FLAT`, `BED_INNER`). Ponds, a
    stream's sloping sides and the banks are mud (`creekmud_*.png`, also from `make_bed_texture.py`),
    with a few gravel patches in a pond's shallows (`POND_GRAVEL`).
  - Round the water, a strip of bare wet mud whose width wanders along the shore and in places
    is none, so the grass reaches the edge there (`BANK_M`, `BANK_DARK`). No grass grows on it.
  - Boulders: six stones made by `build_course.py` (`rock_mesh`), the rock texture laid on from
    three sides with moss on their tops (`BOULDER_*`). They're scenery: the ball doesn't hit them.
- Grass.
  - Darker, more natural turf colours, both the ground (`TURF`) and the blades (`TURF_GRASS`).
    Less of the photo's blotching on the turf (`TURF_SHADE`).
  - Fairways and tees: short, even blades on a tighter grid with little jitter (`jitter`,
    `scale`, `shade` in `TURF_GRASS`).
  - Grass shrinks into the ground as it nears its draw distance instead of popping in and out
    (`GRASS_FADE`). The tall grass is drawn further (140 to 200 m), and the ground under it is
    coloured like it (`TURF["native"]`).
- The broadleaf trees were near-black: the Fab beech and hornbeam materials are instances of a
  Megascans master (`/Game/MSPresets/MS_Foliage_Material/MA_Foliage`) that isn't in the project,
  so they drew with the fallback. `build_course.py` now gives those trees stand-in materials
  (`M_TreeLeaves`, `M_TreeBark`) with each material's own textures: cut-out leaves lit through,
  and a gentle sway (`LEAF_*`, `BARK_TINT`). Adding the pack's `MSPresets` folder would restore
  the originals: the stand-ins are used only while the master is missing.
  - The stand-ins draw with the parent's placeholder textures unless refreshed after their
    textures are set (`update_material_instance`): that was the grey-white trees.
- Wind in the trees and grass: the game writes the hole's wind (way, m/s with gusts) into
  `/Game/OpenTeeGolf/Game/MPC_Wind` every frame. The trees lean, sway and flutter branch by
  branch (`LEAF_SWAY`, `wind_offset`); the grass bends in waves running downwind
  (`GRASS_BEND`, `GRASS_WAVE_CM`). Fully bent at `WIND_FULL_MPS`. A tree's sway is sized by its
  height in the world (`world_height`: its model's height with the instance's scale).
  - The pines (the procedural-vegetation Baltic pines) don't move. Their static meshes have no
    wind, and their master material puts its own per-vertex offset into its attribute bundle.
    Replacing that bundle's per-vertex attributes to add ours smeared the needle cards, and a
    script can't read the existing offset to add to it. A conifer pack with wind built in (as
    the Fab beech and hornbeam have) would sway with the rest.
- The meadow ("native"): tall fescue and shorter, greener wild grass take turns in patches, with
  clumps of yellow, white and purple wildflowers (`native_short`, `flowers_*` in `TURF_GRASS`;
  `patch`: where each grows). All made by the script, no outside assets.
- Patch thresholds read the rock texture's lightness weighted by `PATCH_LUM_W`: its linear
  lightness is only about 0.07 to 0.12, and unweighted every patch (bed stones, silt, the bank's
  width, the meadow) came out all or nothing.
- Cart paths: `derive-layers.js` writes `layer_path.png`; the ground paints the paths in
  (`PATH_TINT`), no grass grows on them or pokes through, and their mesh sits a few cm into the
  ground (`PATH_SINK_CM`) so it never floats.
- The flag: its back is its own copy of the cloth, facing the other way (one surface for both
  sides was lit wrongly from behind and flickered in its shadow).
- Smoother hill shadows in a low sun (`OTG.Look.SunSoft`, `OTG.Look.SlopeBias`): the ground's
  facets cast a comb of teeth along a shadow's edge.
- Free view: Shift flies 6 times as fast (was 3).
- Aiming (a click on the course, the minimap, the arrow keys): the view swings round the ball to
  face the new aim, keeping its height and distance (it used to slide straight across, dipping
  low past the ball on a big turn).
- Video memory on big courses: the surface masks are always in memory, so they're capped at
  `MASK_MAX_PX` (4096; mowing directions half that). A big course's export writes them at 8192,
  a dozen of them about 1 GB; capped, about 0.25 GB. Unreal's own use on Banff Springs (26 holes,
  about 13,000 trees) is about 5 GB. Its "video memory exhausted" warning means the card is
  shared: the web version with a course loaded in a browser holds several GB too, so close it
  while playing a big course in Unreal. (`rhi.DumpMemory`'s total counts reserved, uncommitted
  buffer space - 9.6 GB of its 14.8 GB on Banff - that isn't memory in use.)
- Mowing stripes and the green's fringe.
  - `derive-layers.js` writes `layer_mow.png`: on the mown surfaces, the line of play of the
    nearest hole (tee, aim points, green) as a direction, and `layer_fringe.png`.
  - The ground and the fairway blades draw bands along it (`MOW_WIDTH_M`: 3.2 m on fairways and
    tees, 1.2 m on greens; `MOW_CONTRAST`). As real stripes, which band is light depends on which
    way you look along them, and they fade looking across.
  - The fringe round each green is a deeper green (`FRINGE`), with no stripes.
- Time of day: a slider in the weather panel (L), 5:30 am to 7:30 pm. The sun is placed where it
  is at that hour for the course's latitude on a midsummer day, dimming through dawn and dusk.
  "Sun behind you" goes back to lighting each hole from behind the player.
- The menu: the chosen course's aerial photo behind it, darkened; rounded glass cards, small
  capital labels, chips for each choice (the picked one green, tees with their colour), and the
  page shown underlined in the tabs.
- Testing commands: `OTG.Round.FreeView`, `OTG.Round.Fly`, `OTG.Round.LookAt <X Y Z | pin>`,
  `OTG.Round.Perf`, `OTG.Round.WeatherPanel`, `OTG.Round.Weather <key | wind | from | cloud | rain | fog> [value]`,
  `OTG.Round.Time <hour | auto>`.
- Still to come in the game itself: putting aids, the Mevo+ camera view.

- Updates (`OTGUpdateSubsystem`): the game keeps itself up to date from GitHub releases tagged
  `unreal-v<version>`, downloading only the files that changed. The content is cooked in chunks so
  every file is under GitHub's 2 GB limit: the game is `pakchunk0`, the tree scenery `pakchunk1`-`3`,
  the Megascans plants `pakchunk4` and KiteDemo's pieces `pakchunk5` (only the folders the game
  uses: `DefaultGame.ini`, `KITE_DIRS`) (labels made by `make_game_assets.py`). `make_release.ps1` writes `manifest.json` (every file's
  size and SHA-1, `release_manifest.py`); `python unreal/publish_release.py` (needs the GitHub CLI,
  signed in; `--dry-run` to see what would go up) uploads only the files no earlier release has,
  named by their SHA-1, plus the file list, the installer (`installer/OpenTeeGolf.iss`, Inno Setup 6:
  installs per user, no admin, with shortcuts and an uninstaller) and a starter zip, both without
  the scenery, and never marks
  the release "latest" (that stays the web version's). On start the game reads the newest release's
  list, downloads what differs or is missing (the scenery, on a first start from the starter zip)
  into `Update\staging`, checks each file, and on "Restart now" an `apply.cmd` swaps them in after
  the game closes. Raise the version in `package.json` for each release. Testing: `-updateurl=` a
  manifest's address, `-updateauto` (download and restart without asking), `-noupdate`.

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
