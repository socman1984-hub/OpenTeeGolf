# OTGBallPhysics

A golf ball's flight, bounce and roll, and a course for it to play on. The ball on the course is the
web version's `physics.js` in C++; its flight, impacts and roll are OpenFairway (the OpenFairwayPhysics
plugin of [uopengolf](https://github.com/digitalhand/uopengolf), MIT) at revision `e8c318e3`: drag and
lift fitted to tour averages, launch-monitor shots and a trajectory calculator, integrated with RK4 at
120 Hz; turf landings read from each impact's angle and backspin; and roll fitted to launch-monitor
roll-out. That's newer than the web version's `openfairway.js`. It's plain C++17 with no Unreal types,
so the tests (`unreal/tests/physics`) build the same files outside Unreal.

## Main pieces

| File | What it is |
|---|---|
| `Physics/OTGBallPhysics.h` | `otg::SimulateShot`: a shot (speed, launch, spin) from a point on a `Course`, with wind, air and trees. The result is the path, where it stopped and why (`rest`, `holed`, `water`, `oob`), carry, total and apex. `otg::of` is OpenFairway (aerodynamics, impacts, roll). `Surface` names the lies. |
| `Physics/OTGPlayCourse.h` | `otg::PlayCourse`: a heightmap with surfaces every half metre. `PlayTrees` and `PlayCollider` hold what the ball can hit (trunks, buildings, the flagstick). |

Course space: x east, z north, y up, in metres. A heading of 0 points along +z.

## How the game uses it

`UOTGCourseData` fills a `PlayCourse` (`FillPlayCourse`) from a course's `play.json` and `surfaces.png`. `AOTGRound`
calls `SimulateShot` for each shot and animates the path it returns.

## Checking it

`node unreal/tests/physics/carry_check.mjs` flies OpenFairway's reference launches (its
`Docs/Accuracy.md`) through this code and prints carry, apex and roll beside the references; the
carries and apexes match OpenFairway's own to 0.1 yd and 0.1 ft. `parity.mjs` (86 shots through this and
the web version's JavaScript) compared the two while they were the same physics; since the newer
OpenFairway they differ by design.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder. It's enabled by default. Add
`"OTGBallPhysics"` to your module's dependencies in its `.Build.cs`.
