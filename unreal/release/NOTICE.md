# Third-party software, assets and data in the OpenTeeGolf Unreal game

OpenTeeGolf's own code is by the OpenTeeGolf authors (see `LICENSE-OpenTeeGolf.txt` for the
project's licence). This game is built with Unreal Engine and includes the following.

## Engine

| Component | Terms |
| --- | --- |
| [Unreal Engine](https://www.unrealengine.com) 5.8 (Epic Games, Inc.) | Unreal Engine End User License Agreement. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere. The engine's own third-party notices are in `Engine/Binaries/ThirdParty` and the engine's licence files. |

## Software (ported into the game's code)

| Component | Licence |
| --- | --- |
| [OpenFairwayPhysics](https://github.com/digitalhand/uopengolf/tree/main/Plugins/OpenFairwayPhysics), part of uopengolf (Copyright (c) 2026 uopengolf contributors; equations and calibration from [ogolf-physics](https://github.com/digitalhand/ogolf-physics)' OpenFairway): the ball physics, ported to C++ through the web version's JavaScript port | MIT |
| [ironsight](https://github.com/divotmaker/ironsight) (Copyright 2026 Eric Thill): the Mevo+ protocol | MIT OR Apache-2.0 (used under MIT) |
| [Flight Relay Protocol](https://github.com/flightrelay/spec) 0.1: the shot format the game reads from FRP launch monitors and bridges | CC0 1.0 |

## Programs included

| Program | Licence |
| --- | --- |
| [10over](https://github.com/divotmaker/10over) 0.2 (Copyright 2026 Eric Thill): `bridges/tenover-frp`, the Garmin R10 bridge | MIT OR Apache-2.0 (used under MIT, see `bridges/10over-LICENSE-MIT.txt`) |
| [allsquare](https://github.com/divotmaker/allsquare) 0.2 (Copyright 2026 Eric Thill): `bridges/allsquare-frp`, the Square Golf bridge | MIT OR Apache-2.0 (used under MIT, see `bridges/allsquare-LICENSE-MIT.txt`) |

## Assets

| Asset | Terms |
| --- | --- |
| Trees and plants from [Fab](https://www.fab.com) (European Beech, European Hornbeam, the Megaplant Library; Quixel Megascans) | Fab Standard License: included in this game in compiled form only. They may not be extracted or redistributed on their own. |
| Engine sky, clouds and basic shapes | Part of Unreal Engine (see above) |

## Course data

Each course pack is built from data the web version of OpenTeeGolf downloads for that course:

| Data | Source | Terms |
| --- | --- | --- |
| Course features, cart paths, roads, buildings | [OpenStreetMap](https://www.openstreetmap.org/copyright) | © OpenStreetMap contributors, [ODbL 1.0](https://opendatacommons.org/licenses/odbl/). The course packs are derived databases under the ODbL. |
| Elevation (US courses) | [USGS 3D Elevation Program](https://www.usgs.gov/3d-elevation-program) | Public domain |
| Elevation (elsewhere) | [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen Terrarium) | Various open sources; see the registry page for attribution |
| Aerial photos (US courses) | USGS/USDA [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Public domain |
| Aerial photos (elsewhere) | [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) | Esri, Maxar, Earthstar Geographics and the GIS User Community. Personal, non-commercial use: a pack of a course outside the US is not for sharing. |
| Tree heights | [Meta & World Resources Institute, High Resolution Canopy Height Maps](https://registry.opendata.aws/dataforgood-fb-forests/) | CC BY 4.0 |
| Live weather (fetched while playing) | [Open-Meteo](https://open-meteo.com) | Data CC BY 4.0; the free API is for non-commercial use |

The courses in this release: Majestic Springs Golf Club (Ohio, US).
