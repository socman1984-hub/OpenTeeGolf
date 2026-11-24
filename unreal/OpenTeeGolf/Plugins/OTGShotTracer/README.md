# OTGShotTracer

A golf ball's flight drawn as a ribbon. The path so far is thinned to about a point a metre and
drawn 2 cm above it as a strip facing the camera, wider the farther it is so it still reads from the
tee. Both faces are drawn.

## Main class

`UOTGShotTracerComponent` (`Tracer/OTGShotTracerComponent.h`), a procedural mesh component:

- `RibbonMaterial`: the ribbon's material. Set it before drawing.
- `Draw(path, head, cameraPos)`: the path's points (the level's cm, in order, up to where the ball
  is), the ball itself, and where the camera is. Call it every frame while the ball flies.
- `Clear()`: when the next shot starts.

## How the game uses it

`AOTGRound` creates it as a default subobject (`Tracer`), gives it an orange unlit material, and
calls `Draw` from the shot's path as the ball flies.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder (it needs the engine's Procedural Mesh Component plugin, enabled by its .uplugin). It's enabled by default. Add
`"OTGShotTracer"` to your module's dependencies in its `.Build.cs`.
