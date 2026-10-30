# Third-party software and data

OpenTeeGolf is free software under the GNU General Public License v3 (see `LICENSE`).

## Software

| Component | License |
| --- | --- |
| [three.js](https://threejs.org) 0.186 (WebGPU rendering) | MIT |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) via [JoltPhysics.js](https://github.com/jrouwe/JoltPhysics.js) 1.1 (collisions) | MIT |
| [EZ-Tree](https://github.com/dgreenheck/ez-tree) 1.1 (procedural trees, with its bark and leaf textures) | MIT |
| [geotiff.js](https://github.com/geotiffjs/geotiff.js) 3.0 (reading tree-height maps) | MIT |
| [ironsight](https://github.com/divotmaker/ironsight) (Copyright 2026 Eric Thill): the Mevo+ protocol, ported to JavaScript in `lib/mevo.js` | MIT OR Apache-2.0 (used under MIT) |
| [Creek Water Studio](https://github.com/bjlin-meshy/creek-water-studio) (bjlin-meshy): the ideas behind the clear creek water, its stony bed, the foam and leaves drifting on it and the creek sounds (`public/js/flotsam.js`, `public/js/creek-sound.js`, stream water in `public/js/scene.js`) | MIT |
| [Koi Pond Garden](https://github.com/souranyp-stack/koi-pond-garden) (Copyright 2026 Sourany Phomhome): the ideas behind the ponds you see into, their lily pads, fish and ripples (`public/js/pond.js`, pond water in `public/js/scene.js`) | MIT |
| [10over](https://github.com/divotmaker/10over) 0.2 (Copyright 2026 Eric Thill): `tenover-frp`, the Garmin R10 bridge, run by `lib/bridges.js` and bundled in `bridges/` in the Windows release zip | MIT OR Apache-2.0 (used under MIT, see `bridges/10over-LICENSE-MIT.txt`) |
| [allsquare](https://github.com/divotmaker/allsquare) 0.2 (Copyright 2026 Eric Thill): `allsquare-frp`, the Square Golf bridge, run by `lib/bridges.js` and bundled in `bridges/` in the Windows release zip | MIT OR Apache-2.0 (used under MIT, see `bridges/allsquare-LICENSE-MIT.txt`) |
| [Flight Relay Protocol](https://github.com/flightrelay/spec) 0.1 (the shot format `lib/frp.js` reads) | CC0 1.0 |
| [Node.js](https://nodejs.org) (bundled in the Windows release zip) | MIT and others, see `node/LICENSE` in the zip |

## Textures (in `public/textures/`)

| File | Source | License |
| --- | --- | --- |
| `lawn_*.jpg` | [ambientCG Grass005](https://ambientcg.com/view?id=Grass005) | CC0 1.0 |
| `rough_*.jpg` | [ambientCG Grass004](https://ambientcg.com/view?id=Grass004) | CC0 1.0 |
| `sand_*.jpg` | [ambientCG Ground080](https://ambientcg.com/view?id=Ground080) | CC0 1.0 |
| `asphalt_*.jpg` | [ambientCG Asphalt031](https://ambientcg.com/view?id=Asphalt031) | CC0 1.0 |
| `mud_color.jpg` | [ambientCG Ground094B](https://ambientcg.com/view?id=Ground094B) | CC0 1.0 |
| `rock_color.jpg` | [ambientCG Rock058](https://ambientcg.com/view?id=Rock058) | CC0 1.0 |
| `trees/maple_leaves.png`, `trees/willow_leaves.png`, `trees/birch_leaves.png` | Twigs made with `scripts/leaf-twigs.py` from [ambientCG LeafSet010](https://ambientcg.com/view?id=LeafSet010), [LeafSet013](https://ambientcg.com/view?id=LeafSet013) and [LeafSet014](https://ambientcg.com/view?id=LeafSet014) | CC0 1.0 |
| `trees/ash_bark_*.jpg`, `trees/spruce_bark_*.jpg` | [ambientCG Bark001](https://ambientcg.com/view?id=Bark001), [Bark006](https://ambientcg.com/view?id=Bark006) | CC0 1.0 |
| `trees/maple_bark_*.jpg`, `trees/poplar_bark_*.jpg`, `trees/pine_bark_*.jpg` | [Poly Haven Trident Maple Bark](https://polyhaven.com/a/trident_maple_bark), [Bark Willow 02](https://polyhaven.com/a/bark_willow_02), [Pine Bark](https://polyhaven.com/a/pine_bark) | CC0 1.0 |
| `trees/palm_leaves.png`, `trees/palm_bark_*.jpg` | Drawn by `scripts/palm-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `trees/{liveoak,magnolia,sycamore,cypress,baldcypress}_leaves.png` (foliage) | Twigs made with `scripts/scan-leaves.py` from [ambientCG LeafSet001](https://ambientcg.com/view?id=LeafSet001), [LeafSet022](https://ambientcg.com/view?id=LeafSet022), [LeafSet025](https://ambientcg.com/view?id=LeafSet025) and [LeafSet019](https://ambientcg.com/view?id=LeafSet019); the moss and flowers drawn by `scripts/tree-textures.py` | CC0 1.0 (scans) |
| `trees/sycamore_bark_*.jpg`, `trees/liveoak_bark_*.jpg`, `trees/baldcypress_bark_*.jpg`, `trees/cypress_bark_*.jpg`, `trees/magnolia_bark_*.jpg`, `trees/mesquite_bark_*.jpg` | [Poly Haven Bark Platanus](https://polyhaven.com/a/bark_platanus), [Jolcham Oak Bark 01](https://polyhaven.com/a/jolcham_oak_bark_01), [Metasequoia Bark](https://polyhaven.com/a/metasequoia_bark), [Japanese Cedar Bark](https://polyhaven.com/a/japanese_cedar_bark), [Chinese Hackberry Bark](https://polyhaven.com/a/chinese_hackberry_bark), [Bark Willow](https://polyhaven.com/a/bark_willow) | CC0 1.0 |
| `trees/{mesquite,paloverde,saguaro}_leaves.png`, `trees/{paloverde,saguaro}_bark_*.jpg` | Drawn by `scripts/tree-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `trees/{redmaple,tuliptree,loblolly}_{leaves.png,bark_*.jpg}` | Drawn by `scripts/tree-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `trees/{redcedar,douglasfir,ponderosa,beech}_{leaves.png,bark_*.jpg}` | Drawn by `scripts/tree-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `trees/{dogwood,crapemyrtle,elm,hemlock,juniper,olive,holly}_{leaves.png,bark_*.jpg}` | Drawn by `scripts/tree-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `plants/plants.png` (shrubs, ferns, ornamental grasses, flowers, desert plants) | Drawn by `scripts/plant-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `water_normal.jpg` | [three.js](https://github.com/mrdoob/three.js/blob/r186/examples/textures/waternormals.jpg) example textures | MIT |

The ambientCG and Poly Haven textures were resized to 1K and recompressed; CC0 needs no attribution, but credit is given here anyway.

## Sounds (in `public/audio/`)

| File | Source | License |
|---|---|---|
| `trickle.mp3` | Trimmed from [Water Trickle](https://freesound.org/people/TheWoodlandNomad/sounds/363120/) by TheWoodlandNomad | CC0 1.0 |
| `creek.mp3` | Trimmed from [small river in the forest very close sound effect](https://freesound.org/people/Garuda1982/sounds/577837/) by Garuda1982 | CC0 1.0 |
| `rapids.mp3` | Trimmed from [Mountain River 001.wav](https://freesound.org/people/Auxide_Audio/sounds/588179/) by Auxide_Audio | CC0 1.0 |

The loops were trimmed by [Creek Water Studio](https://github.com/bjlin-meshy/creek-water-studio) and taken from there; CC0 needs no attribution, but credit is given here anyway.

## Data downloaded at run time

OpenTeeGolf ships no maps, photos or course data. Each copy downloads data from these services for
its own player and caches it on that computer. If you share built courses or cached files, you
are responsible for following these terms.

| Data | Source | Terms |
| --- | --- | --- |
| Course features, cart paths, roads, car parks, buildings, search | [OpenStreetMap](https://www.openstreetmap.org/copyright) via Overpass, Nominatim, Photon and the OSM API | © OpenStreetMap contributors, [ODbL 1.0](https://opendatacommons.org/licenses/odbl/). Follow each service's usage policy. |
| Elevation (US) | [USGS 3D Elevation Program](https://www.usgs.gov/3d-elevation-program) | Public domain |
| Elevation (elsewhere) | [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen Terrarium) | Various open sources, see the registry page for attribution |
| Aerial photos (US) | USGS/USDA [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Public domain |
| Aerial photos (elsewhere) | [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) | Esri, Maxar, Earthstar Geographics and the GIS User Community. Personal, non-commercial use; attribution shown in-game. |
| Distant land's colours (beyond the course) | [Sentinel-2 cloudless 2020](https://s2maps.eu) by EOX IT Services GmbH (contains modified Copernicus Sentinel data 2020) | CC BY-NC-SA 4.0. Non-commercial; attribution shown in-game. |
| Tree heights | [Meta & World Resources Institute, High Resolution Canopy Height Maps](https://registry.opendata.aws/dataforgood-fb-forests/) | CC BY 4.0; credited in-game |
| Weather | [Open-Meteo](https://open-meteo.com) | Data CC BY 4.0; the free API is for non-commercial use |
| Course pictures | Wikimedia Commons, Wikidata, Wikipedia, or the course's own website | Wikimedia files carry their own licences (credited on each card). Website pictures belong to the course and are only shown locally to the player. |

Built courses (`courses/`) contain OpenStreetMap-derived data. If you publish them, they must be
shared under the ODbL with attribution to OpenStreetMap contributors.
