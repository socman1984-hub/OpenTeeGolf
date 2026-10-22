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
| `trees/{liveoak,cypress,baldcypress,mesquite,paloverde,saguaro,sycamore,magnolia}_leaves.png`, `..._bark_*.jpg` | Drawn by `scripts/tree-textures.py` (no source images) | Part of OpenTeeGolf (GPL-3.0-or-later) |
| `water_normal.jpg` | [three.js](https://github.com/mrdoob/three.js/blob/r186/examples/textures/waternormals.jpg) example textures | MIT |

The ambientCG and Poly Haven textures were resized to 1K and recompressed; CC0 needs no attribution, but credit is given here anyway.

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
| Tree heights | [Meta & World Resources Institute, High Resolution Canopy Height Maps](https://registry.opendata.aws/dataforgood-fb-forests/) | CC BY 4.0; credited in-game |
| Weather | [Open-Meteo](https://open-meteo.com) | Data CC BY 4.0; the free API is for non-commercial use |
| Course pictures | Wikimedia Commons, Wikidata, Wikipedia, or the course's own website | Wikimedia files carry their own licences (credited on each card). Website pictures belong to the course and are only shown locally to the player. |

Built courses (`courses/`) contain OpenStreetMap-derived data. If you publish them, they must be
shared under the ODbL with attribution to OpenStreetMap contributors.
