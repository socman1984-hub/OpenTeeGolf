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

## Where it stands

- Looks good: the Megascans trees, the turf (thick rough, crisp fairway edges), round bunkers, the
  terrain, the cart paths, and ponds with smooth shorelines, moving ripples and reflections.
- Far off, past where the rough's blades are drawn, the rough is a little lighter than up close.
- Steep pond banks seen from high up can still show small steps along the shore.
- The whole picture is bright (the level's own sky and exposure).
- Mown turf: no mowing stripes yet; bunkers have no lip.
- Conifers exported as static meshes don't sway.
- Not started: the land beyond the course on the horizon.
