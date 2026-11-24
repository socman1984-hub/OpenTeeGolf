# OTGBallPhysics

A golf ball's flight, bounce and roll, and a course for it to play on. It's the web version's
`public/js/openfairway.js` and `physics.js` in C++, kept line for line, so a shot plays the same in
both. It's plain C++17 with no Unreal types, so the parity tests (`unreal/tests/physics`) build the
same files outside Unreal.

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

`node unreal/tests/physics/parity.mjs` plays 86 shots through this code and through the web
version's JavaScript, and compares where they stop.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder. It's enabled by default. Add
`"OTGBallPhysics"` to your module's dependencies in its `.Build.cs`.
