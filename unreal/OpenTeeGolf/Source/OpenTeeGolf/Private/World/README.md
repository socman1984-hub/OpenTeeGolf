# World

The course built in the level at run time from an exported folder, without the Unreal Editor.

`AOTGRuntimeCourse` (`Public/World/OTGRuntimeCourse.h`) holds what the parts share: the course data,
its pictures and the ground material. It builds the parts in order, all at once (`Build`) or a step
a frame (`BuildInSteps`, for the loading screen), and gives the rest of the game one place to ask.
Each part is a class of its own (`Public/World/OTGCourseParts.h`):

| Part | Files | What it builds |
|---|---|---|
| `FOTGCourseGround` | `OTGCourseGround.cpp` | The heightmap as tiles of mesh (four levels of detail), the ponds' banks lifted clear of the water, the ground's height anywhere. |
| `FOTGCourseWater` | `OTGCourseWater.cpp`, `OTGWater.cpp`, `OTGRiverRocks.cpp` | Ponds, creeks and the sea, as Tidewright water or the game's own; the water's height over a point. |
| `FOTGCourseTrees` | `OTGCourseTrees.cpp` | Trees and rocks as the tree design has them, and picking them in the tree designer. |
| `FOTGCourseFarLand` | `OTGCourseFarLand.cpp` | The land beyond the course: the real land round it, or hills made up. |
| `FOTGCourseProps` | `OTGCourseProps.cpp`, `OTGHouses.cpp`, `OTGCars.cpp` | Cart paths, bridges, buildings (Hearthline houses) and parked cars. |
| `FOTGCourseTurf` | `OTGCourseGrass.cpp` | The grass and the turf's colours, as the grass designer has them, and the `OTG.Grass.*` commands. |

Each part reaches the course through its `Course` pointer. The parts are plain structs; the course
reports their objects to the garbage collector (`AddReferencedObjects`).

## Also here

| File | What it is |
|---|---|
| `OTGGrass.h`, `OTGGrass.cpp`, `OTGGrassKinds.cpp` | `otg::FGrass`: grass streamed round the camera in cells, placed off the game thread. Its kinds are the game's own blades, Megascans plants and Greensward mats. |
| `OTGTreeDesign.h` | `FOTGTreeDesign`: the tree designer's changes, saved per course. |
| `OTGPalette.h` | The turf's palette: one green, each cut set apart by its lightness. |
| `OTGKiteDemo.h` | The KiteDemo pack's pieces the game uses. |
| `OTGTurfBake.cpp` | `AOTGRuntimeCourse::BakeTurf`: the grass photographed into turf textures (a tool). |
| `OTGCourseShared.h` | Helpers shared by these files (private). |
