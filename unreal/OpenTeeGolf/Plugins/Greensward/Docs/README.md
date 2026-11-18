# Greensward — Lawns, Pitches and Meadows

Greensward fills spline outlines with dense, real-geometry Nanite grass: cut lawns, sports pitches,
golf greens, rough and meadows. Blades are opaque triangles (no masked cards), so they render
cheaply under Nanite and light correctly under Lumen.

Status: 0.3. See "Not yet included" at the end.

## Requirements

- Unreal Engine 5.8, Windows, DirectX 12 with Nanite enabled.
- The engine's **Procedural Content Generation Framework (PCG)** plugin. It is enabled automatically.

## Quick start

1. Enable the **Greensward** plugin and restart the editor.
2. Open `/Greensward/Maps/GreenswardDemo` to see every feature in one level.
3. In your own level, drag a **Greensward Lawn** actor above your landscape.
4. Shape its spline outline, add one or more meshes from `/Greensward/Mats` to **Mats**, and press **Rebuild**.
5. Drop one **Greensward Wind & Weather Controller** into the level to drive wind, season, dryness, snow and wetness.

## Greensward Lawn

| Property | What it does |
| --- | --- |
| Mats | Grass mats to scatter, with relative weights. Mixing two or three similar mats hides repetition. |
| Mat Size | Edge length of one mat in cm: 50 for stock mats, 25 for the putting-green mats. |
| Override Material | Use your own material instance for this lawn. |
| Cut Out Lawns | Other lawns whose area is removed from this one (a green inside a fairway). |
| Snap To Ground | Drops every mat onto the ground and tilts it to the slope. |
| Edge Subdivisions | How finely cells on the outline are split: 0 = 50 cm steps, 2 = 12.5 cm steps. |
| Edge Raggedness | 0 = a crisp edge for pitches and greens. 1 = loose, shorter, off-grid tufts for rough and meadow. |
| Surface Offset | Lifts mats slightly (1 cm by default) so bumpy terrain does not poke through the turf base. |
| Stripe Mode / Width / Angle / Contrast | Mowing pattern: none, single stripes, or a double-cut checkerboard. |
| Tint | Colour multiplier for the whole lawn. |
| Wind Disable Distance | Beyond this distance wind animation stops, which saves a lot of GPU time. |
| Rebuild / Clear | Refill or empty the outline. |

Blueprint: `Rebuild`, `Clear`, `ApplyAppearance`, `SetRectangle`, `IsPointInside`, `GetInstanceCount`.

## Wind & Weather Controller

The actor's forward (red) axis is the wind direction.

| Property | Range | Effect |
| --- | --- | --- |
| Wind Strength | 0 – 1.5 | Still air to strong wind. |
| Wind Speed | 0 – 3 | How fast gusts cross the grass. |
| Season | -1 – 1 | Spring green, summer, autumn yellowing. |
| Dryness | 0 – 1 | Extra blades turn to straw, in natural patches. |
| Snow | 0 – 1 | Tips whiten first, then the grass is buried. |
| Wetness | 0 – 1 | Darker, glossier blades. |

All values can be keyed in Sequencer or set from Blueprint. They are written to the
`MPC_Greensward` material parameter collection.

## Materials

- `M_Greensward_Grass` — master material.
- `MI_Greensward_Grass_Wind` — used by grass taller than 5 cm.
- `MI_Greensward_Grass_Static` — wind compiled out; used by short lawns for best performance.

Useful instance parameters: `RootColour`, `TipColour`, `DryColour`, `MicroVariation`,
`MacroVariation`, `MacroTiling`, `StripeLean`, and the overlay group (`OverlayTexture`,
`OverlayOrigin`, `OverlaySize`, `OverlayStrength`) which colours the grass from an aerial image
mapped over a world-space rectangle.

## Landscape material

`M_Greensward_Landscape` (use the instance `MI_Greensward_Landscape`) blends seven layers by height:
**Grass, DryGrass, Soil, Mud, Sand, Gravel, Clay**. Layer info assets are in `/Greensward/Landscape`.

- Per layer: `<Layer>Tiling`, `<Layer>Tint`, `<Layer>Roughness`.
- `FarBlendStart` / `FarBlendRange`: beyond this distance each layer fades to a larger-scale sample, which hides texture repetition.
- `MacroVariation`, `MacroTiling`: broad colour variation.
- The overlay group uses the same parameter names as the grass material. Give both the same image, origin and size and lawn and ground take the same colours.
- Snow and wetness from the Wind & Weather Controller affect the ground as well as the grass.

To paint the layers, use the normal Landscape Paint tool. To generate a landscape from code, call
**Create Landscape** (`unreal.GreenswardEditorLibrary.create_landscape`) with a list of
`GreenswardLandscapePatch` entries: each paints a rectangle or ellipse with a layer, and can level the
ground under it (pitches, courts) or sink it (bunkers).

A lawn with no mats is still a valid cut-out: outline a bunker with one and add it to the fairway's
**Cut Out Lawns**.

## Pitch markings

Drag a **Greensward Pitch Marking** actor over a pitch. It is a decal at true size, so it paints the
grass blades, clay or bare ground beneath it.

| Marking | Painted area (m) |
| --- | --- |
| Soccer Full | 105 x 68 |
| Soccer Small | 40 x 20 |
| American Football | 120 x 53.3 yd, with yard lines, hash marks and numbers |
| Rugby Union | 100 x 70 plus in-goals |
| Rugby League | 100 x 68 plus in-goals |
| Tennis Single | 23.77 x 10.97 |
| Tennis Triple | three courts, 17 m apart |
| Hockey | 91.4 x 55 |

- **Scale** resizes the whole marking (1 = regulation size).
- The pitch length runs along world X. Change the actor's **Yaw** to turn it.
- Line colour, opacity and wear are parameters of `M_Greensward_Marking`; make an instance and assign it
  to **Material Override**.

## PCG graphs

Nine graphs in `/Greensward/PCG` fill a closed spline with grass through the PCG framework:
`PCG_GS_Lawn`, `PCG_GS_Pitch_Striped`, `PCG_GS_Lawn_Checker`, `PCG_GS_Golf_Green`,
`PCG_GS_Golf_Fairway`, `PCG_GS_Golf_Rough`, `PCG_GS_Tennis_Lawn`, `PCG_GS_Meadow`, `PCG_GS_Dry_Lawn`.

1. Drag a **Greensward PCG Area** actor into the level and shape its spline. (Any actor with a closed
   spline and a PCG component works.)
2. Set the PCG component's graph to one of the graphs above and press **Generate**.

Each graph samples a 50 cm grid inside the spline (25 cm for the green), gives every mat a random
quarter turn, projects it onto the landscape, lifts it 1 cm and spawns the mats. They are short:
duplicate one and change the mesh list to make your own.

Mats spawned by PCG have no lawn actor to set their mowing pattern, so the graphs use material
instances instead: `MI_Greensward_Grass_Striped` and `MI_Greensward_Grass_Checker`. The parameters
are `InstanceStripeMode` (0 none, 1 stripes, 2 checkerboard), `InstanceStripeWidth`,
`InstanceStripeAngle` and `InstanceStripeContrast`. The same instances work for mats placed with the
foliage tool or by hand.

Tip: make the outline a few centimetres off a multiple of 50 cm. A sample row that lands exactly on a
straight edge can be skipped by the PCG spline sampler.

## Making your own mats

Call **Generate Grass Mat** (Editor Utility Blueprint or Python,
`unreal.GreenswardEditorLibrary.generate_grass_mat`) with a `GreenswardMatSettings` recipe:
height, mown or unmown, blade count, width, lean, bend, dry fraction, clumping, seed stalks and
an optional turf base layer that stops the ground showing between blades.
The result is a Nanite static mesh saved to the folder you name.

## Automated tests

The plugin ships ten automation tests under **Greensward** (Tools > Session Frontend > Automation):
stock assets, the mat generator, lawn filling, cut-outs, edges, appearance, ground snapping, pitch
markings, the weather controller and landscape creation. From a command line:

```
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests Greensward;Quit" -unattended -nullrhi
```

## Known limitations

- Season and dryness change the grass mats, not the landscape layers. Paint DryGrass where you want
  dry ground.
- At a crisp lawn edge the turf base shows as a thin rim that keeps its own colour under snow.
- PCG areas have 50 cm stepped edges; the smooth and ragged edge options are on the Greensward Lawn actor.

## Not yet included in 0.3

Trees, flags, a World Partition demo map and runtime (GPU) PCG generation. The PCG graphs do not yet
cut one area out of another (use the Greensward Lawn actor for a green inside a fairway). The ground
textures are procedural, not photographic.
