# Third-party software and data

OpenTeeGolf is free software under the GNU General Public License v3 (see `LICENSE`).

## Software

| Component | License |
| --- | --- |
| [three.js](https://threejs.org) 0.186 (WebGPU rendering) | MIT |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) via [JoltPhysics.js](https://github.com/jrouwe/JoltPhysics.js) 1.1 (collisions) | MIT |
| [Node.js](https://nodejs.org) (bundled in the Windows release zip) | MIT and others, see `node/LICENSE` in the zip |

## Data downloaded at run time

OpenTeeGolf ships no maps, photos or course data. Each copy downloads data from these services for
its own player and caches it on that computer. If you share built courses or cached files, you
are responsible for following these terms.

| Data | Source | Terms |
| --- | --- | --- |
| Course features, cart paths, buildings, search | [OpenStreetMap](https://www.openstreetmap.org/copyright) via Overpass, Nominatim, Photon and the OSM API | © OpenStreetMap contributors, [ODbL 1.0](https://opendatacommons.org/licenses/odbl/). Follow each service's usage policy. |
| Elevation (US) | [USGS 3D Elevation Program](https://www.usgs.gov/3d-elevation-program) | Public domain |
| Elevation (elsewhere) | [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen Terrarium) | Various open sources, see the registry page for attribution |
| Aerial photos (US) | USGS/USDA [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Public domain |
| Aerial photos (elsewhere) | [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) | Esri, Maxar, Earthstar Geographics and the GIS User Community. Personal, non-commercial use; attribution shown in-game. |
| Weather | [Open-Meteo](https://open-meteo.com) | Data CC BY 4.0; the free API is for non-commercial use |
| Course pictures | Wikimedia Commons, Wikidata, Wikipedia, or the course's own website | Wikimedia files carry their own licences (credited on each card). Website pictures belong to the course and are only shown locally to the player. |

Built courses (`courses/`) contain OpenStreetMap-derived data. If you publish them, they must be
shared under the ODbL with attribution to OpenStreetMap contributors.
