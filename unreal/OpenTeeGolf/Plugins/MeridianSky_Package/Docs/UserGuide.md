# Meridian Sky: User Guide

Meridian Sky is a single actor that gives a level a complete sky and weather system: sun, moon, stars, atmosphere,
volumetric clouds, fog, rain, snow, wind, lightning and auroras. Everything follows two things you control:
**Time Of Day** and **Weather**.

For the full list of settings, functions and events, see [Reference.md](Reference.md).

## Requirements

- Unreal Engine 5.8. Other versions have not been tested.
- The Niagara plugin (enabled automatically as a dependency).
- A level where +Z is up. Spherical or planetary worlds are not supported.

## Quick start

1. **Remove the level's existing sky.** Delete any Directional Light, Sky Atmosphere, Sky Light, Exponential Height
   Fog and Volumetric Cloud actors. Meridian Sky brings its own, and two sets will fight each other.
2. **Place the actor.** In the Place Actors panel, search for **Meridian Sky** and drag it into the level. Its
   position does not matter.
3. **Set the time.** Select the actor and change **Time Of Day** (0 to 24 hours) in the Details panel. The sun, moon,
   stars, lighting and fog all follow.
4. **Set the weather.** Change **Weather Type** to one of the built-in conditions.
5. **Press Play.** Rain, snow and lightning run during play. They are also meant to preview in the editor viewport
   (with **Preview Weather In Editor** on and the viewport in Realtime mode), but Play is the reliable way to see them.

### One project setting to check

For fog to take the sky's colour in fair weather, enable **Support Sky Atmosphere Affecting Height Fog** in
Project Settings > Rendering, or add this to `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.RendererSettings]
r.SupportSkyAtmosphereAffectsHeightFog=True
```

## Time of day

| Setting | What it does |
|---|---|
| Time Of Day | The current time, 0 to 24 hours. |
| Animate Time Of Day | Advances time automatically during play. |
| Animate In Editor | Also advances time in the editor viewport. |
| Day Length Minutes | Real minutes that twelve hours of daytime take. |
| Night Length Minutes | Real minutes that twelve hours of night take. |

Day and night run at separate speeds so you can have long days and short nights, or the reverse.

### Where the sun and moon go

There are two modes, chosen with **Real World Simulation**.

- **Off (simple mode).** The sun rises in the east at **Sunrise Time**, sets in the west at **Sunset Time**, and its
  arc leans south by **Sun Path Tilt**. The moon's position follows **Simple Moon Phase**: 0 is a new moon, 0.5 is a
  full moon that rises at sunset.
- **On.** The sun and moon are placed where they really are for a **Latitude**, **Longitude**, date and
  **UTC Offset Hours**. Seasons, day length, midnight sun and moon phases all follow from those.

**North Yaw** sets which way is north in your level. At 0, north is +X.

## Weather

**Weather Type** picks a built-in condition. Each one is a set of seven values, which you can also edit directly
under **Weather** for a custom look.

| Weather Type | Cloud cover | Storminess | Fog | Rain | Snow | Wind (m/s) | Lightning (per minute) |
|---|---|---|---|---|---|---|---|
| Clear | 0.05 | 0 | 0 | 0 | 0 | 2 | 0 |
| Partly Cloudy | 0.40 | 0 | 0 | 0 | 0 | 4 | 0 |
| Overcast | 0.90 | 0.2 | 0.05 | 0 | 0 | 5 | 0 |
| Foggy | 0.70 | 0 | 0.85 | 0 | 0 | 1 | 0 |
| Light Rain | 0.85 | 0.3 | 0.10 | 0.30 | 0 | 5 | 0 |
| Heavy Rain | 1.00 | 0.7 | 0.15 | 0.85 | 0 | 10 | 0 |
| Thunderstorm | 1.00 | 1.0 | 0.15 | 1.00 | 0 | 16 | 6 |
| Light Snow | 0.85 | 0.1 | 0.25 | 0 | 0.30 | 3 | 0 |
| Blizzard | 1.00 | 0.5 | 0.70 | 0 | 1.00 | 20 | 0 |

### Changing weather during play

Call **Change Weather** on the actor from Blueprint, with a weather type and a transition time in seconds. The
weather blends smoothly from wherever it is now to the new condition. Calling it again part-way through redirects
the blend without a jump. A transition time of 0 switches instantly.

For conditions the built-in types don't cover, either call **Change Weather To State** with your own values, or
create a **Meridian Weather Preset** data asset and call **Change Weather To Preset**.

### Wet and snowy surfaces

The actor tracks two values that build up and fade over time:

- **Wetness** rises while it rains and dries afterwards.
- **Snow Cover** rises while it snows and melts afterwards. Rain melts it up to four times faster.

To use them in your own materials, read them from the material parameter collection
`MPC_MeridianWeather` (in the plugin's Materials folder). It carries these values, updated every frame:

| Parameter | Type | Meaning |
|---|---|---|
| Wetness | Scalar | 0 dry to 1 soaked |
| SnowCover | Scalar | 0 bare to 1 covered |
| Rain | Scalar | Current rain intensity, 0 to 1 |
| Snow | Scalar | Current snow intensity, 0 to 1 |
| WindSpeed | Scalar | Metres per second |
| CloudCoverage | Scalar | 0 to 1 |
| Fogginess | Scalar | 0 to 1 |
| TimeOfDay | Scalar | 0 to 24 |
| SunElevation | Scalar | Degrees above the horizon; negative at night |
| WindDirection | Vector | Unit vector the wind blows towards |

A typical use: lerp a surface's roughness down and darken its base colour by `Wetness`, and blend towards a snow
material by `SnowCover` on upward-facing surfaces.

### Wind

**Wind Direction** is the yaw the wind blows towards. Wind speed comes from the weather. Together they slant the
rain, carry the snow, move the clouds, and drive the engine's wind source, which affects foliage and cloth set up
to respond to wind.

## Night sky

- **Stars** fade in through dusk and turn slowly overhead through the night. **Star Brightness** scales them.
- **The moon** shows its true phase for the sun's position. **Moon Angular Diameter** sets its size in the sky; the
  real moon is about 0.5 degrees, and the default of 2 is deliberately larger.
- **Auroras** appear when **Aurora Strength** is above 0. They only show at night.

Nights are kept dark by **Limit Exposure**, which stops the camera's auto exposure from brightening moonlight to
daylight levels. It applies to the whole level through a low-priority unbound post process volume, so your own
post process volumes override it. If nights are too dark or too bright, adjust the first value of
**Exposure Range EV**: higher is darker.

## Sound

No audio ships with the plugin. Three slots take your own sounds:

| Slot | Behaviour |
|---|---|
| Rain Loop Sound | Loops while it rains; volume follows rain intensity. |
| Wind Loop Sound | Loops in wind above 3 m/s; reaches full volume at 18 m/s. |
| Thunder Sound | Plays after each lightning strike, delayed by the time sound takes to travel the strike distance, and quieter for distant strikes. |

Use looping sound assets for the two loop slots.

## Multiplayer

The actor replicates. Time, date, weather, wind direction, wetness, snow cover and aurora strength are owned by the
server and followed by clients, and lightning strikes are sent to every client.

Call the functions that change state (**Set Time Of Day**, **Set Date**, **Change Weather**,
**Set Aurora Strength**, **Trigger Lightning Strike**) on the server.

Cloud positions are the one thing that is not synchronised: each machine drifts its own clouds with the wind, so
two players will not see identical cloud shapes.

## Using your own art

Each visual has a slot you can replace.

- **Cloud Material.** Must use the Volume material domain and have *Used with Volumetric Cloud* enabled; the actor
  ignores any other material and logs a warning. It is driven through the parameters named under Clouds > Advanced
  (by default `Coverage`, `Density`, `CloudColor`, `Storminess` and `WindOffset`). Clear a name to stop the actor
  writing that parameter.
- **Rain System / Snow System.** Niagara systems that receive a float user parameter `Intensity` (0 to 1) and a
  vector user parameter `Wind` (cm/s). The actor moves them to the camera every frame.
- **Sky Material.** Draws the atmosphere, stars, moon and aurora on the sky dome. Clear it to fall back to the plain
  engine sky with no stars or moon disc.
- **Lightning Material.** Draws the bolt on a camera-facing card. It receives `Seed`, `Intensity` and `Colour`.

The shipped shaders are kept as readable source in the plugin's `Docs/ShaderSource` folder (`CloudShape.hlsl`,
`NightSky.hlsl`, `LightningBolt.hlsl`) as a starting point. Each is the body of a Custom node in the matching
material.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| The level is far too bright, or lighting looks doubled | The level's original sky actors are still there. Delete them. |
| No clouds, and a warning in the Output Log about the cloud material | The cloud material is not a Volume material flagged for volumetric cloud use. |
| Rain or snow doesn't appear in the editor viewport | Editor preview needs the viewport in Realtime mode and can stop while the editor is in the background. Press Play to see it reliably. |
| Fog is a flat colour that ignores the time of day | The project setting under "One project setting to check" is off. |
| Night is too bright | Another post process volume is overriding exposure, or Limit Exposure is off. |
| Overcast or storm scenes are too dark on the ground | Raise Cloudy Sky Light Boost. |
| Clouds look grainy for a moment after a big change or a camera cut | Volumetric clouds are built up over several frames and settle shortly afterwards. |

## Known limitations

- Flat worlds only; no planetary or spherical levels.
- Rain and snow do not stop under roofs or indoors.
- Only volumetric clouds are provided; there is no 2D or static cloud mode for low-end platforms.
- Cloud positions differ between players in multiplayer.
- The aurora is drawn on the sky dome, behind the clouds, and there is no control for where in the sky it sits.
- The moon's surface markings are a simple pattern, not the real lunar face.
- The plugin depends on engine content: the cloud noise texture, the sky sphere mesh and the plane mesh.
- Performance has not been profiled, and the plugin has only been run on Windows with DirectX 12.
