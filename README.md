# OpenTeeGolf

> **⚠️ ALPHA:** OpenTeeGolf is new and still changing fast. Expect bugs and rough edges, and
> please report problems on the [Discord](https://discord.gg/YNsyWkCayd) or as a
> [GitHub issue](https://github.com/socman1984-hub/OpenTeeGolf/issues).

A free, open-source golf simulator for the **FlightScope Mevo+**. It runs in your web browser
and plays **any golf course mapped in OpenStreetMap**: search for a course, build it in about a
minute, and play it on real lidar terrain under real aerial photos.

It renders with **three.js on WebGPU** and uses **Jolt Physics** for collisions, on top of its own
ball-flight model calibrated to launch monitor data.

OpenTeeGolf is licensed under the **GNU GPL v3** (see [LICENSE](LICENSE)). You can use, study,
change and share it, and any version you share must stay open source under the same license.

**Website:** https://socman1984-hub.github.io/OpenTeeGolf/ · **Discord:** https://discord.gg/YNsyWkCayd

## Download and play (Windows)

1. Download the latest `OpenTeeGolf-<version>-win-x64.zip` from the [Releases page](https://github.com/socman1984-hub/OpenTeeGolf/releases).
2. Unzip it anywhere, for example to your Desktop.
3. Double-click **Start OpenTeeGolf.bat**. Your browser opens at http://localhost:8080.

Nothing else needs to be installed: the zip includes Node.js. The first time, Windows may show
**"Windows protected your PC"**: click *More info → Run anyway*. When the Windows Firewall asks
about Node.js, allow it on private networks so the Mevo+ app can send shots.

### Updates

When a new version is released, the game's menu says so. Click **Update now**: the game downloads
the new zip from GitHub, checks it against the fingerprint (SHA-256) GitHub publishes, swaps in the
new files and restarts. Your built courses (`courses/`) and settings (`data/`) are kept. The bottom
of the menu shows your version and has **Check for updates**. (A copy run from source, such as a
git checkout, only shows that an update exists; update it with `git pull`.)

## Browser

Use a current Chrome or Edge (WebGPU is on by default). Browsers without WebGPU fall back to
WebGL 2 automatically, and adding `?webgl` to the address forces that fallback. The FPS overlay
(**P**) shows which one is in use.

## Running from source

Requires [Node.js](https://nodejs.org) 18 or newer.

```bash
npm install
npm start
```

Then open http://localhost:8080. On Windows, if PowerShell blocks `npm`, use `npm.cmd start` or
double-click `Start OpenTeeGolf.bat`.

## Your courses

The main menu shows every course you've built. With a lot of courses:

* **Search** by name or place (state names match abbreviations, so "ohio" finds "Batavia, OH").
* **Sort** by recently played, name, newest built or location.
* Tap **☆** to make a course a favorite, then use **★ Favorites** to show only those.

Each course gets a picture automatically. OpenTeeGolf looks for a freely licensed photo on
Wikimedia Commons / Wikidata / Wikipedia (credited on the card), then for the share picture on
the course's own website. If neither has one, the card shows the course's aerial view. Hover a
card and click **📷** to choose your own picture or to look online again.

Your courses are saved in the `courses/` folder. To move them to another computer, copy that
folder.

## Building courses

From the main menu choose **+ Build a course from OpenStreetMap**, or open
http://localhost:8080/builder.html.

1. **Search** by name. OpenTeeGolf checks that the course exists in OpenStreetMap. If the name only
   matches something like a clubhouse or a town, it lists the golf courses around that place.
2. **Check** shows what's mapped: hole lines, greens, fairways, bunkers, tee boxes and water. It
   rates the mapping as well mapped, partly mapped or barely mapped. Facilities with several
   courses (for example a 27-hole club) let you pick which one to build.
3. **Build** makes a playable course in `courses/<name>/`:
   * Holes come from OpenStreetMap's numbered hole lines (tee → green, with par and handicap
     when they're mapped). Bends in the line become aim points.
   * Tee boxes along each hole's first leg become Black/Blue/White/Green/Red tees, ordered from
     the back.
   * Four pins (Thu–Sun) are spread over each mapped green.
   * Water becomes penalty areas, and the course boundary becomes out of bounds.
   * Terrain is USGS 1 m lidar in the US. Elsewhere it uses global elevation tiles (~4 m) from
     AWS Open Data.
   * If hole lines aren't mapped, holes are guessed by following each green's fairway back to
     the tee boxes behind it. Those courses are marked **needs review**.

If a course is missing or badly mapped, you can improve it for everyone by editing
[OpenStreetMap](https://www.openstreetmap.org) and rebuilding it.

### Course editor

Open a built course from the builder or the **Edit** link in the main menu
(`/editor.html?course=…`). It shows a top-down map over the terrain.

* **Holes:** pick a hole, set its number, par and handicap. Drag tees, aim points, the green
  center and the four pins, or use **Place** to put one anywhere (including tee types that
  don't exist yet). Add or delete holes. **Spread pins across green** re-seeds the pins.
* **Shapes:** draw greens, fairways, bunkers, tee boxes, water, penalty areas or the
  out-of-bounds line. Click points, then double-click or press Enter to finish. Click a shape to
  select it, drag its corners, drag the small circles to add a corner, or Alt+click to remove
  one. Drag inside a selected shape to move the whole thing (handy for lining it up with the
  aerial photo), or nudge it with the arrow keys (25 cm, Shift for 1 m). You can also change its
  type or delete it. Tick **Stream or river** on a water shape to make it running water rather
  than a flat pond.
* **Mowing:** under **Course**, choose the mowing pattern for the fairways and for the greens:
  straight stripes, diagonal stripes, checkerboard, diamonds or none. A small preview shows each
  one. New courses start with straight stripes on fairways and a checkerboard on greens.
* **Trees:** the map shows every tree the game will plant. Dark circles come from the tree-height
  map and OpenStreetMap, amber dashed ones were guessed from the aerial photo, and bright ones are
  your own. Click a found tree to **Remove** it (for a dark patch that isn't really a tree) or
  **Replace** it with one of your own to move or change. Removed trees show as a red cross; click
  one to put the tree back. You can also place single trees and draw tree areas with a species
  and height range.
* **Tree review:** after building a new course the editor opens on a review of the trees found
  automatically that stand in the line of play (within a few metres of the line from the tee
  through the aim points to the green). It goes hole by hole, with trees close together as one
  group: keep or remove each, or keep or remove the rest, then save. **Review trees in the line
  of play** under Trees runs it again any time.
* Ctrl+Z undoes, Ctrl+S saves, and **Play** opens the course in the simulator. Heights for
  everything you place come from the terrain automatically.

The first search or check can be slow when the public OpenStreetMap servers are busy. Results
are cached in `data/cache`.

## Connecting the Mevo+

Choose how in the menu under **Launch monitor**.

### Direct (no FS Golf, experimental)

OpenTeeGolf can talk to the Mevo+ (or Mevo Gen2) itself, so FS Golf isn't needed:

1. Turn on the Mevo+ and close FS Golf (only one program can use the radar at a time).
2. Join this PC to the Mevo+ Wi-Fi network (**FS M2-xxxxxx**; the password is the device's
   9-digit serial number).
3. In the menu, set **Launch monitor** to *Mevo+ direct*. The status box shows *Looking for the
   Mevo+*, *Connecting*, *Arming*, then *ready* with the battery level.

OpenTeeGolf switches the radar between full-swing, chipping (under 40 yards) and putting modes
as you play, and re-arms it after every shot. The protocol comes from the open-source
[ironsight](https://github.com/divotmaker/ironsight) project, which worked it out from the Wi-Fi
traffic between the Mevo+ and FS Golf; OpenTeeGolf carries a JavaScript port of it
(`lib/mevo.js`). Like ironsight, it doesn't touch FlightScope's licensing or paid features.
It hasn't been tried on every firmware version yet, so if it misbehaves, switch back to FS Golf
and please report what the status box said. For development without the device,
`node test/fake-mevo.js 5100` runs a simulated Mevo+ (set the host to `127.0.0.1` with
`POST /api/lm {"connection":"direct","host":"127.0.0.1","port":5100}`).

**Mevo+ setup page** (menu link, or *setup* next to the status box in a round): with the direct
connection it shows the Mevo+ camera live with alignment guides, a level for tilt and roll,
battery, temperature, status and the last shot. It also holds the setup sent to the radar:
distance from the Mevo+ to the ball, hitting-surface and tee height, standard or RCT balls, how
much of the flight must be tracked, and the detection mode (automatic by club, or always full
swing / outdoor). The live view relays the camera's own video stream; it can take 10-30 s to
start while the camera warms up.

### Through FS Golf

The Mevo+ sends shots to simulators through FlightScope's **FS Golf** PC app, using the
**OpenConnect** protocol (JSON over TCP port 921). OpenTeeGolf listens on that port:

1. Close any other simulator software. Only one program can use port 921.
2. Start OpenTeeGolf. The console shows `OpenConnect listener on port 921`.
3. In FS Golf, choose its OpenConnect simulator connection (FS Golf names it after the
   simulator it was made for). The address is `127.0.0.1`, port `921`. If FS Golf runs on a
   different PC, use this PC's IP address instead and allow Node through the Windows Firewall.
4. The status box at the top right turns green ("ready") when the Mevo+ is connected.

OpenTeeGolf sends the selected club and the distance to the pin back to FS Golf. Clubs are
auto-selected (putter on the green), and you can change them in the Club dropdown.

To test without a launch monitor, press **T** for the test-shot panel, or send a fake Mevo+
shot through the real network path:

```bash
node test/send-shot.js 150 12 0 2700 0
```

(arguments: ball speed in mph, launch angle, direction (+ = right), total spin in rpm, spin axis (+ = right))

## Controls

| Key | Action |
| --- | --- |
| Click the ground | Aim at that spot. A yellow ring and label show the target distance |
| Click / drag the minimap | Aim at that spot |
| ← / → | Fine-tune the aim (hold Shift for finer) |
| F | Free view: fly anywhere on the course (also the **Free view** button) |
| R | Re-aim at the pin |
| V | Toggle overhead view |
| Space | Skip the ball-flight animation |
| M | Mulligan |
| T | Test-shot panel |
| P | FPS / performance overlay |
| H | Previous shot traces on/off (also a menu checkbox) |
| G | Green slope grid on/off |
| C | Scorecard |
| N | Next hole (or pick up) |
| Esc | Menu |

**Free view:**
* Drag to move, right-drag to turn, and scroll to zoom.
* WASD flies around, Q/E moves down/up (Space also goes up), and Shift goes faster.
* Clicking the ground or the minimap still aims your shot; the camera stays where it is.
* F or Esc returns to the ball. A shot from the launch monitor also brings you back
  automatically.

## Graphics quality

The menu's **Graphics** option trades looks for speed. It's remembered between visits.

| Setting | Resolution | Shadows | Trees / heather / close grass |
| --- | --- | --- | --- |
| High | up to 2× | 4096 | all |
| Balanced (default) | up to 1.25× | 2048 | ~85% / 70% / 20k tufts |
| Performance | 1× | 1024 | ~60% / 40% / 9k tufts |

Press **P** (or tick **Show FPS**) for FPS, the slowest recent frame, draw calls and triangle
count. If free view stutters, try Balanced or Performance.

## Weather

Choose the weather in the menu:
* **Live** (the default): the real current conditions at the course, from Open-Meteo (free, no
  account needed). If they can't be fetched, or the course has no location, it plays breezy.
* **Presets:** Calm, Breezy, Windy or Rain.
* **Custom:** set the wind speed and direction yourself (into you, behind you, across, or
  random per hole).

How it affects play:
* **Wind:** it's measured 10 m up, so it's weaker near the ground and stronger at a high ball's
  apex. Every shot gets its own gust between the steady wind and the gust speed, with a little
  change in direction. A 10 mph headwind costs a 150 mph drive about 12 yd of carry, and a
  10 mph crosswind moves it about 17 yd.
* **Air density:** temperature, humidity and air pressure change it, as does altitude. Cold,
  dry or heavy air means shorter shots: a drive at 5 °C carries about 4 yd less than at 20 °C.
* **Rain:** wet ground gives softer landings and slower greens, and roll-out drops by about a
  third.
* **What you see:** the flag streams downwind, lifting and snapping harder as the wind builds.
  Rain falls with the wind, and cloud cover dims the sun and hazes the sky. The HUD shows the
  wind (helping, into you or cross), gusts, temperature and wet ground.

## Putting

* **Auto-putt mode** (menu → Putting): once the ball stops on the green, the hole is finished.
  It counts 1 putt inside the circle drawn around the hole (6/10/15/20 ft, your choice) and 2
  putts outside. Missing the green means you play on until you're on it.
* **Gimme** is off by default. When a gimme, auto-putt or pick-up ends a hole, the banner says
  so and the ball is shown rolling into the cup.
* On or near the green, a **slope grid** is draped over the surface. Lines are coloured by
  steepness: white is under ~1%, then yellow, orange and red (3%+). Dots slide downhill, faster
  where it's steeper. **G** toggles the grid.
* The label at the hole shows the putt length and how much it rises or falls, for example
  "15 ft · 3.1 in downhill".
* **The cup:** a ball drops if it's slow enough to fall before reaching the far edge. That's
  about 1.6 m/s for a centred putt, as in real cup studies. Faster or off-centre putts catch the
  lip and spin out, and balls brushing the rim curl around it. A ball that stops over the hole
  falls in, chip-ins drop, and holed balls visibly sink into the cup.

## Graphics

Built courses look like the real place:

* **Aerial imagery** is draped over the terrain. In the US it's USGS/USDA NAIP orthophotos
  (about 0.3–0.6 m per pixel, public domain). Elsewhere it's Esri World Imagery (attribution
  shown in-game; personal, non-commercial use). The photos are colour-balanced automatically,
  then fairways get mowing stripes, greens are smoothed, and bunkers get clean sand.
* **Trees** stand where they really are: GolfSim reads a worldwide 1 m tree-height map (Meta &
  World Resources Institute) and puts a tree at every crown top, with its real height and spread.
  The aerial photo fills the gaps: tree crowns it shows (the darkest, most textured vegetation)
  get a tree too, wherever none stands yet. That catches trees the height map misses or that
  were planted since. Wrong guesses can be removed in the course editor.
* **Tree models** are procedural (EZ-Tree) in nine species: oak, maple, ash, birch, aspen,
  poplar, willow, pine and spruce, in 24 variants. Each tree is turned, stretched, leaned and
  tinted on its own, with photo-scanned bark and leaf textures (maple, willow and birch leaves
  and five of the barks are CC0 scans from ambientCG and Poly Haven) and swaying leaves. Crowns
  are lit as a whole: darker inside and underneath, and glowing yellow-green when the sun is
  behind them. Close trees are fully detailed, further ones lighter, and distant ones are
  pictures of the real models (taken from 24 directions when the course loads, blended as you
  move, lit by the sun). Trees on the edge of a wood, the ones you actually see, stay detailed
  much further out than trees inside it.
* **Undergrowth:** the outside edges of dense woods (what you see from the course) are lined with
  low leafy shrubs, so a wood reads as thick rather than a row of trunks. Lone trees, thin rows
  and the inside of woods get none.
* **Seeing past trees:** trees between the camera and the ball, and close ones in your line of
  sight, fade to a see-through stipple so you can see your shot and still know they are there.
* **Buildings** are 3D outlines from OpenStreetMap. Coverage varies by area.
* **Roads** near the course come from OpenStreetMap: asphalt, and on main roads white edge lines
  and a centre line (double yellow in the Americas, dashed white elsewhere) with lane lines.
  They stop where the course's terrain ends. The ball plays them like a cart path.
* **Car parks** come from OpenStreetMap too: paved ones get fresh asphalt and painted bays (laid
  along the mapped aisles, or in standard rows). Cars are parked where the aerial photo shows one,
  in roughly its colour; grass and dirt overflow fields stay empty unless the photo shows them full.
  The ball bounces off cars.
* **Surroundings:** a ring of lower-resolution imagery about 1.5 km wide extends the view to
  the horizon.
* **Close-up ground:** photo-scanned grass, sand and asphalt textures (CC0, from ambientCG) with
  normal maps, blended over the aerial photo near the camera.
* **Water:** solid, with drifting ripples and a live mirror image of the trees, sky and clouds
  that gets stronger at low viewing angles. The Performance setting turns the mirror off.
  Streams and rivers run downhill: their surface follows the slope of the banks, the ripples are
  carried downstream, and the steeper the stretch, the faster it flows (with streaks of foam on
  fast water).
* **Tall grass:** unmown areas seen in the photo are a meadow of four kinds of grass (fine
  bunch grass, taller grass with seed heads, broader leafy grass, and grass flattened by the
  weather), in drifts of green and straw, over tawny ground. It's densest around the camera,
  thinning out smoothly with distance.
* **Rough up close:** a carpet of real grass blades covers the rough around the camera, fading
  into the ground's texture further out. The rough is thickest and tallest right where the
  fairway (or fringe, green or tee) ends, so the edge stands out, and thins over about 20 m.
* **Lighting:** real-time sun shadows, sky reflections, and grass tufts around the ball (kept
  off cart paths and out of water).

Imagery, buildings, cart paths, roads, car parks and tree heights download the first time a course is opened;
the loading screen shows each step with its progress and warns if one stops moving. This can take a few
minutes, because the USGS server renders the photos on request. They're then cached next to
the course (`*.sat/`).

## Rules and physics

* Ball flight uses drag and Magnus lift calibrated to TrackMan PGA Tour averages, with air
  density for the course altitude. Run `npm test` to check it.
* Landing uses an impact model where backspin checks the ball, then rolling friction and slope.
  Green speed is set by the stimp value.
* **Surfaces play differently:** each one has its own bounce, grip and roll, and changes your
  next strike.

  | Surface | Driver roll-out | Next shot |
  | --- | --- | --- |
  | Fairway / tee | ~18 yd | full speed and spin |
  | First cut (2 m collar around fairways) | ~14 yd | −3% speed, −12% spin |
  | Rough | ~8 yd | −10% speed, −38% spin |
  | Deep rough (under trees) | ~5 yd | −22% speed, −55% spin |
  | Bunker | ~2 yd | −15% speed, −20% spin |
  | Green | fast, set by the stimp | putting |

* **Where the surfaces come from:** OpenStreetMap supplies the greens, fairways, tees, bunkers
  and water. Where a hole's fairway isn't mapped, OpenTeeGolf reads it from the aerial photo
  (mown grass is smooth and bright, and the rules are learned from that course's mapped
  fairways). Tree cover in the photo plays as deep rough.
* **Cart paths** come from OpenStreetMap (golf cart paths and paths inside the course). They're
  also detected in the aerial photo as long, thin, pale strips when the map has few. They look
  like pavement, and the ball bounces high and runs a long way on them. A ball that stops on a
  path gets free relief at the nearest point off it, no nearer the hole.
* **Solid objects** (Jolt Physics): the ground, tree trunks, buildings, parked cars and the flagstick are
  bodies in a Jolt collision world, and every step of the ball's flight and roll is checked
  against them. The ground in Jolt is a height field that matches the rendered terrain to within
  a couple of centimetres. A ball that hits the flagstick bounces off it; a gentle putt that
  rolls into it drops. How the ball flies, bounces and rolls is still the calibrated model above.
* **Trees:** trunks are solid, and the ball bounces off. Leaves slow it and nudge it off line.
  Deeper in the crown it can hit a branch and get knocked down. After the shot a message says
  "Off the trunk!", "Hit a branch!" or "Clipped the leaves."
* **Feedback:** the lie badge shows your current surface. After each shot a message says where
  the ball landed and where it finished. The aim label shows the surface at your target.
* Water: 1 stroke penalty, drop at the point of entry. OOB: stroke and distance.
* Optional gimmes, and a maximum score of par + 5 per hole.

## Data sources

OpenTeeGolf contains no map, photo or course data. Each copy downloads what it needs, for its own
player, from these free public services (details and terms in [NOTICE.md](NOTICE.md)):

* Course shapes and buildings: © OpenStreetMap contributors (ODbL)
* Elevation: USGS 3DEP (public domain) and AWS Terrain Tiles
* Tree heights: Meta & World Resources Institute canopy height maps (CC BY 4.0)
* Aerial photos: USGS/USDA NAIP (public domain) and Esri World Imagery
* Weather: Open-Meteo (CC BY 4.0)
* Course pictures: Wikimedia Commons / Wikidata / Wikipedia, or the course's own website

Please don't commit built courses (`courses/`), downloaded imagery or caches to the repository.
They contain third-party data with their own licences; anyone can rebuild a course in a minute.

## Contributing

Bug reports and pull requests are welcome, and the [Discord](https://discord.gg/YNsyWkCayd) is the
place for questions and ideas. Before sending a change:

```bash
npm test                                    # ball-flight calibration + Jolt collision checks
node test/collision-test.js courses/<name>/<name>.course.json   # also check a real course
node test/course-map.js courses/<name>/<name>.course.json map.png courses/<name>/<name>.real.json
```

To release a new version, update `version` in `package.json` and push a tag such as `v0.2.0`.
The GitHub workflow builds the Windows zip and attaches it to a release. You can also build it
locally with `npm run package` (Windows; output in `dist/`).

## Files

* `server.js`: web server, course list, OpenConnect listener (port 921), and live events to
  the browser
* `lib/builder.js`: builds a playable course from OpenStreetMap
* `lib/realdata.js`: OpenStreetMap search/download, elevation (USGS 3DEP or global tiles), terrain grid
* `lib/imagery.js`, `lib/buildings.js`, `lib/paths.js`, `lib/roads.js`, `lib/parking.js`: aerial
  imagery (NAIP / Esri), buildings, cart paths, roads and car parks, cached per course
* `lib/coursephoto.js`: finds a picture for each course
* `public/js/course.js`: course file + real data → terrain, surfaces and hazards
* `public/js/physics.js`: flight, bounce and roll
* `public/js/collision.js`: the Jolt Physics collision world (terrain, trunks, buildings, flagstick)
* `public/js/scene.js`: three.js WebGPU rendering (terrain shading in TSL)
* `public/js/main.js`: menu, game flow, rules and HUD
* `public/builder.html`, `public/editor.html` (+ `js/builder.js`, `js/editor.js`, `js/coursefile.js`):
  the builder and editor pages
* `test/`: physics calibration, a course-map renderer and a fake launch monitor
* `scripts/package-win.ps1`: builds the portable Windows zip
* `scripts/leaf-twigs.py`: builds the maple, willow and birch leaf pictures from scanned leaves (see the file for how)
