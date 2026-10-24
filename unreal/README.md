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
| `heightmap.png` | 16-bit terrain sized for Unreal's Landscape (1 m between samples, 2 m on big courses); pond beds carved deeper than the game's |
| `photo.jpg` | The course picture, with the wider photo round it, over exactly the landscape's area |
| `layer_*.png` | Fairway (with green fringes), green, tee, bunker, water and rough as distance fields, at twice the heightmap's samples |
| `trees.json` | Every tree: position, height, crown, species, and the game's model, turn and size |
| `tree_N.glb`, `tree_N_*.png/jpg`, `tree_models.json` | The game's own tree models and pictures (used for conifers when there are no Unreal ones) |
| `grass_tuft.glb` | A grass tuft (used only without Unreal grass meshes) |
| `water.glb`, `water.json` | Every pond and stream surface at its level |
| `water_bodies.json` | The same as lake outlines and river centre lines, for Unreal's Water plugin (not used by default) |
| `props_*.glb`, `props.json` | Cart paths, bridges, buildings and cars |
| `holes.json` | Tees, pins and aim points per hole |
| `course.json` | Sizes, the Landscape import settings, how the coordinates map |

## 2. Unreal project setup (once)

1. Unreal Engine 5.4 or later; a **Games > Blank** project, Starter Content off.
2. **Edit > Plugins**: enable **Python Editor Script Plugin** (and **Procedural Vegetation Editor**
   for Megaplants). Restart.
3. From **Fab** (free), add to the project: **European Beech** and **European Hornbeam** (whole
   broadleaf trees), the **Megaplants Norway Spruce** and **Baltic Pine**, and the
   **MW Landscape Auto Material** (its grass meshes make the rough).
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
5. Run `build_course.py` again. It builds, each part reported in the Output Log:
   - the ground: turf colours lit by the photo's light and shade, the game's lawn, rough and sand
     textures for detail, the surfaces' edges from the distance fields; mown rough grass (6-11 cm)
   - the trees: Unreal's beech and hornbeam for broadleaf trees and the exported spruces and pines
     for conifers, each where the game plants it, at its height
   - the water: the game's water surfaces with Single Layer Water shading
   - cart paths, bridges, buildings and cars
   - a marker on every tee and pin
6. At the top of `build_course.py`: `BUILD` picks which parts to rebuild (the ground is the slowest),
   `REIMPORT = True` brings in files from a new export, and the rough, water and texture-memory
   settings are there too.

## Where it stands

- Looks good: the Megascans trees, the rough's grass, the terrain and the cart paths.
- Water: right shape and level, but the colour and shoreline don't look natural yet. (Unreal's Water
  plugin lakes and rivers, `USE_WATER_BODIES = True`, built a wrong water zone and carved the
  landscape; left off.)
- Mown turf: fairways and greens are flat in colour, with no mowing pattern.
- Bunkers: sharp outlines now; still flat sand with no lip.
- Conifers exported as static meshes don't sway.
- Not started: the land beyond the course on the horizon, and shots from the launch monitor.
