# Tidewright Water

Ocean, lake and river water with surf, an underwater view and ground mist, for Unreal Engine 5.8.

Status: **0.6.0, sixth milestone.** The sections marked *planned* are not built yet.

## Quick start

1. Enable the **Tidewright Water** plugin (it enables the engine's Procedural Mesh Component and Niagara plugins).
2. In the Place Actors panel search for **Tidewright Water** and drag it into the level. Its height is the rest level of the water.
3. In the Details panel pick a **Preset**. The preset's values are copied into the actor, where every one of them can still be changed.
4. The level needs a sky to reflect: a Directional Light, Sky Atmosphere and a Sky Light with Real Time Capture.

The demo level is `/Tidewright/Maps/TidewrightDemo`: open sea to the west, a sandy coast with surf to the east, and a river running down a misty valley to the sea.

## Presets

| Asset | Sea state |
| --- | --- |
| `TWP_GlassLake` | Sheltered inland water, ripples only, no foam |
| `TWP_BreezyBay` | Short lively coastal waves |
| `TWP_OpenSea` | Steady wind over a gentle swell |
| `TWP_HeavySea` | Strong wind, long heavy swell, streaks of foam |
| `TWP_Storm` | Full gale, very large waves, persistent foam |

Presets are Data Assets. To make your own: Content Browser > Add > Miscellaneous > Data Asset > **Tidewright Preset**.
At runtime call **Apply Preset** on the water actor with a blend time to move smoothly between sea states.

## Settings

Every setting has a tooltip in the editor. The important ones:

**Waves**

- *Wind Speed, Wind Direction, Fetch*: the wind sea. Wave height is derived from wind speed and fetch using measured ocean data, so 10 m/s over 200 km gives roughly the 2 m sea it would give in reality. *Height Scale* overrides that.
- *Swell Height, Swell Wavelength, Swell Direction*: long waves from a distant storm, independent of the local wind.
- *Choppiness*: how sharply crests are pinched.
- *Depth*: shallow water slows and shortens long waves.
- *Pattern Size*: size of the largest of the four simulated wave bands. It should be several times the longest wave. Changing it is not cross-faded.
- *Foam Coverage*: how readily whitecaps form on steep crests. Rougher seas make more foam at the same setting. Around 0.6 to 0.75 gives scattered whitecaps; 0 turns foam off. *Foam Lifetime*: how long foam lingers behind a crest.

**Look**

- *Scatter Color, Scatter Density, Absorption*: the colour and clarity of the water body, per metre.
- *Roughness*, *Distance Roughening*, *Normal Strength*: the surface up close and towards the horizon.
- *Foam Color, Foam Amount, Foam Pattern Size*.

**Mode**

- *Ocean*: endless. The surface mesh follows the camera.
- *Lake*: a rectangle of *Lake Size* centred on the actor, aligned to the world axes.

**Quality**

- *Simulation Quality*: 64, 128 or 256 texels per wave band. This is CPU cost and rises four-fold per step.
- *Surface Segments*, *View Distance*: density and reach of the surface mesh.

**Animation**

- *Time Scale*, *Seed*.
- *Use Manual Time* and *Manual Time*: key Manual Time in Sequencer for exactly repeatable renders. Wave shape depends only on time and seed; foam also depends on what happened in the preceding seconds.

## Shorelines

Without a shoreline the water passes straight through land at full height. A **Tidewright Shoreline** actor makes a stretch of coast behave like one.

1. Drag a **Tidewright Shoreline** into the level and scale its box so it covers the coast: some land, the waterline, and the shallows out to where the bottom is a few metres deep. The box must reach above and below the waterline.
2. Press **Rebuild Shoreline** in the Details panel. The ground inside the box is scanned from above (landscapes and any mesh with collision), and *Scan Status* reports what was found. Press it again after moving the box or editing the terrain. The scan is saved with the level.

What the water then does inside the box:

- **Wave damping.** Open-water waves flatten as the water shallows, long waves first, and are gone at the waterline. *Wave Damping Depth* is the depth at which they reach full height.
- **Surf.** Lines of waves follow the shape of the coast in to the beach, grow as the bottom rises, and break at *Break Depth*. *Surf Height*, *Surf Wavelength* and *Surf Period* set their size, spacing and timing.
- **Run-up.** The water level at the beach rises as each wave lands and drains before the next (*Run Up Height*). On a gentle beach a small rise sends the waterline a long way up the sand.
- **Foam.** Broken surf carries foam to the beach, and foam lines the water's edge wherever it is shallower than *Shore Foam Depth*, including around rocks.

Notes:

- One shoreline shapes the water at a time: the scanned one nearest the camera. Use one box per coast, or one large box.
- A rotated box scans the upright rectangle around it.
- *Scan Resolution* is the number of samples per side. Aim for a sample every one to two metres.
- Turn on *Rebuild On Begin Play* if the terrain is generated at runtime.
- Do not enable Nanite on the ground mesh unless its collision matches what is drawn. The scan reads collision.
- Buoyancy and the surface queries include the damping, surf and run-up.

## Underwater

When the camera goes below the surface the water actor switches on its underwater view by itself. Nothing needs placing.

- **Fog and colour.** Distant things fade into the water using the water's own *Absorption* and *Scatter Color*, so murky water is murky from inside too. *Fog Density* scales how far you can see, *Brightness* how bright the water is, and *Light Falloff Depth* how quickly it darkens as the camera goes deeper.
- **Waterline.** With the camera half in the water, the surface crosses the screen where it really is, following the waves, with a thin dark line where it meets the lens (*Waterline Strength*).
- **The surface from below.** Looking steeply up you see the bright sky through a round window with a rippling edge; further out the surface mirrors the deep water. *Sky Glow* sets the brightness of the window.
- **Blueprint.** *Is View Underwater* and *Get View Depth* on the water actor let you switch audio or gameplay.

Notes:

- The view follows the camera that rendered the previous frame, so the waterline trails a very fast camera by one frame. With split screen it follows the first view.
- The effect is a post-process material (`M_Tidewright_Underwater`); swap it under *Advanced* if you want your own.
- Not included yet: light shafts, caustics on the seabed, and bubbles.

## Rivers

A **Tidewright River** is water drawn along a spline. Its surface follows the spline, so it can run downhill, which the sea and lakes cannot.

1. Drag a **Tidewright River** into the level and lay its spline points down the river bed, first point upstream.
2. Press **Snap To Ground**. Each point drops onto the ground and is lifted by *Water Depth*; points are then lowered where needed so the water never runs uphill.
3. Set *Width*. To widen or narrow the river at one place, change that spline point's Y scale.

What the river does:

- **Current.** Ripples and foam are carried downstream at *Flow Speed*, slower at the banks and faster where the bed steepens.
- **Foam.** Along both banks (*Bank Foam Width*) and wherever the downhill slope passes *Rapids Slope*.
- **Colour.** Its own *Scatter Color* and *Absorption*, so a silty river can run into a clear sea.
- **Floating objects.** Anything with a Tidewright Buoyancy component floats on the river and is carried by the current. **Sample River** returns the surface, its normal and the water's velocity at a point.

Notes:

- The surface is flat from bank to bank. The banks are wherever the terrain rises through it, so make the river slightly wider than the channel.
- The current follows the spline. It does not flow around rocks or eddy behind them: this is a flow map along the channel, not a fluid simulation.
- End the last spline point just above the sea or lake it runs into. There is a small step where they meet; surf foam usually hides it.
- Rivers have no underwater view yet.

## Mist

A **Tidewright Mist** is a layer of mist that lies on whatever is beneath it.

1. Drag a **Tidewright Mist** into the level and scale the box over the area. The box needs to reach below the lowest ground and a few times *Thickness* above the highest ground you want misty.
2. Press **Rebuild Mist**. The ground, the sea's rest level and any rivers in the box are scanned, and the mist then follows them. *Scan Status* reports what was found. Without a scan the mist lies flat on the bottom of the box.

Settings:

- *Density* and *Thickness*: how thick the mist is at the surface and how quickly it thins with height.
- *Pooling Height*: makes the mist gather in valleys and over water instead of covering hilltops equally.
- *Patchiness* and *Patch Size*: an even layer, or drifting banks with clear gaps.
- *Drift Speed* and *Drift Direction*.
- *Color* and *Brightness*: the mist is not lit by the scene, so set these to suit the time of day.

Notes:

- The mist is drawn by marching eight steps through the box per pixel. It costs most when it fills the screen.
- It is drawn over everything inside the box, including other transparent things such as glass or particles.
- A rotated box scans the upright box around it.

## Buoyancy

Add a **Tidewright Buoyancy** component to any actor. It works on the sea, lakes and rivers.

- *Physics* mode: the root component must simulate physics. Lift and drag are applied at each float point. Float points are generated from the bounds if you leave the list empty.
- *Ride* mode: no physics. The actor is placed on the surface and tilts with the waves.

**Tidewright Floating Prop** is a ready-made physics mesh with the component already set up.

There are three ways to make something float. Pick by what the thing is.

| Way | Use it for | Cost |
| --- | --- | --- |
| **Tidewright Buoyancy** component (above) | Boats, crates, anything that must collide or be stood on | CPU per object |
| **MF_Tidewright_Float** material function | Many small things that only need to look right: planks, leaves, lily pads, litter | None on the CPU |
| **Tidewright Water** Niagara data interface | Particles: debris, embers that die at the surface, splashes | CPU per particle |

### Material function

Open the mesh's material, add **MF_Tidewright_Float** and connect its output to **World Position Offset**. Place the mesh anywhere over the water; its height does not matter.

- The whole mesh moves as one piece: up and down, sideways with the surge, and tilted to the slope beneath its pivot.
- *Height Offset* raises or sinks it. *Tilt* scales the lean. *Pivot* defaults to the object's position; for instanced or merged meshes feed it a per-instance position.
- It reads the two largest wave bands, which carry nearly all of the height. Small ripples do not move it.
- The mesh's bounds do not move with it. In rough seas raise **Bounds Scale** on the component or it may be culled while still on screen.
- Collision does not move. It ignores shorelines, lake edges and rivers: it always floats at the sea's level.
- Keep *Share Waves With Materials* (water actor, Advanced) on. With two water actors in a level, turn it off on one.

### Niagara

In a CPU emitter add a **Tidewright Water** data interface parameter. It offers three functions, each taking a particle position:

- **Get Water Height**: the Z of the surface above or below. The simplest way to make a particle float.
- **Get Water Velocity**: the current in a river; zero on the sea.
- **Sample Water Surface**: everything at once: surface position, surface normal, water velocity, depth of the position below the surface in centimetres, and whether there is water there at all.

- Float: set the particle's Z to the surface position's Z.
- Drift downstream: add the water velocity.
- Die or splash at the surface: test Depth > 0.
- CPU emitters only. Systems using it simulate after the water has updated each frame.
- Call the functions from a Scratch Pad module or a Custom HLSL node that has a Tidewright Water input. They cannot be called from a one-line custom expression on a module input; Niagara does not allow that for any data interface.
- No ready-made Niagara system or module is included yet.

From Blueprint or C++ the water actor also exposes **Sample Water Surface**, **Get Water Height At Location**, **Is Location Underwater** and **Get Significant Wave Height**. These read exactly the data that is drawn, so objects sit on the visible waves.

## How it works

- Four bands of a JONSWAP wind spectrum plus a swell band are simulated on the CPU with an inverse FFT each frame. Band sizes share no common multiple, which hides repetition.
- The results are uploaded as displacement and derivative textures with full mip chains.
- A camera-centred radial mesh is displaced in the vertex shader. Each band is read at the mip that matches the local vertex spacing, so distant geometry fades bands out instead of shimmering.
- Normals come from the derivative textures per pixel. Slope detail that a pixel can no longer resolve is converted to roughness, which widens the sun's glitter path towards the horizon.
- Foam forms where the horizontal displacement squeezes the surface (the Jacobian of the displacement) further than usual for that sea, accumulates, and decays over *Foam Lifetime*. It wears away into lace stretched along the wind rather than fading evenly. The finest ripples are ignored, so whitecaps sit on real crests.
- A shoreline stores depth, distance to the waterline and direction to shore in one texture. The material and the buoyancy code run the same surf arithmetic from it.
- The mesh follows the camera, but the material cancels that motion in the velocity buffer, so temporal anti-aliasing and upscaling treat the water as fixed in the world.
- Shading uses the engine's Single Layer Water model.

## Performance

Measured in a packaged Development game at 1280 x 720 on an Intel integrated GPU, the slowest hardware this is likely to meet. Treat the numbers as relative costs, not as what a gaming GPU will do.

| Scene | Frames per second | GPU ms | Wave simulation, CPU ms |
| --- | --- | --- | --- |
| Demo level with all Tidewright actors removed | 57 | 15.8 | 0 |
| Open sea | 37 | 24.0 | 4.7 |
| Storm | 37 | 24.4 | 4.7 |
| Beach with surf, from above | 34 | 26.9 | 3.8 |
| River, no mist | 41 | 21.9 | 3.9 |
| River with mist | 37 | 24.5 | 3.8 |
| Underwater, looking up | 34 | 27.0 | 3.6 |

- The water surface costs about 8 ms of GPU time on this hardware, mist about 2.5 ms more when it covers part of the screen, the underwater view about 3 ms.
- The wave simulation costs about 4 ms of CPU at Medium quality, on the game thread. Low is about a quarter of that and High about four times. **Get Last Simulation Milliseconds** on the water actor reports it live.
- Sequencer and Movie Render Queue: a 48 frame shot with four temporal samples per frame renders correctly. For renders that must match exactly from run to run, turn on *Use Manual Time* and key *Manual Time*.

## Requirements and limits

- Unreal Engine 5.8. Windows; Mac and Linux are listed but untested.
- Simulation runs on the game thread. Medium quality costs a few milliseconds of CPU per frame.
- Wave textures are anchored at the world origin and use single precision; very far from the origin (tens of kilometres) detail will degrade.
- Lakes are axis-aligned rectangles.
- Surf is a shaped travelling wave, not a fluid simulation: it does not curl over, splash or react to objects.
- Sand is not darkened where the water has been; that belongs to the ground material.

## Planned

- Underwater light shafts and caustics.
- A ready-made Niagara debris system.
- Water that flows around obstacles and reacts to characters.
- Underwater view for rivers.

## Regenerating the content

All plugin content is generated by scripts in the host project's `Tools` folder: `make_textures.ps1` (source images), `build_assets.py` (textures, materials, presets, the demo coast mesh) and `build_demo_level.py` (demo map). Run `build_demo_level.py` inside a running editor: the shoreline scan needs collision, which a headless commandlet does not provide.
