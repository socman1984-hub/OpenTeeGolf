# Meridian Sky: Reference

Every setting, Blueprint function and event on the **Meridian Sky** actor (`AMeridianSkyActor`). For an introduction,
see [UserGuide.md](UserGuide.md).

Settings marked **R** are replicated from the server to clients.

## Settings

### Time

| Setting | Default | Notes |
|---|---|---|
| Time Of Day **R** | 10 | Hours, 0 to 24. Can be keyed in Sequencer. |
| Animate Time Of Day **R** | off | Advance time automatically during play. |
| Animate In Editor | off | Also advance time in the editor viewport. |
| Day Length Minutes **R** | 10 | Real minutes for twelve hours of daytime. |
| Night Length Minutes **R** | 5 | Real minutes for twelve hours of night. |

### Sun and Moon

| Setting | Default | Notes |
|---|---|---|
| Real World Simulation **R** | off | Place the sun and moon from location and date instead of the simple arc. |
| North Yaw | 0 | Yaw of true north in the level. At 0, north is +X and east is +Y. |

**Simulation** (used when Real World Simulation is on)

| Setting | Default | Notes |
|---|---|---|
| Latitude **R** | 40.7 | Degrees, positive north. |
| Longitude **R** | -74 | Degrees, positive east. |
| UTC Offset Hours **R** | -5 | Hours that local time is ahead of UTC. Daylight saving is not applied for you. |
| Year, Month, Day **R** | 2026-06-21 | The date. It advances at midnight when time is animated. |

**Simple** (used when Real World Simulation is off)

| Setting | Default | Notes |
|---|---|---|
| Sunrise Time | 6 | Hour the sun rises. |
| Sunset Time | 18 | Hour the sun sets. |
| Sun Path Tilt | 35 | Degrees the sun's arc leans from overhead. The noon sun is at 90 minus this. |
| Simple Moon Phase **R** | 0.5 | 0 = new, 0.5 = full. Also sets where the moon sits relative to the sun. |

### Lighting

| Setting | Default | Notes |
|---|---|---|
| Sun Intensity | 10 | Lux, before the atmosphere dims it. |
| Moon Intensity | 0.1 | Lux for a full moon; scales down with phase. |
| Moon Color | pale blue | Colour of moonlight. |
| Sky Light Intensity | 1 | Strength of ambient light from the sky. |
| Cloudy Sky Light Boost | 2 | Multiplier on sky light under full cloud cover. No effect below about 60% cover. |

### Weather

| Setting | Default | Notes |
|---|---|---|
| Weather Type | Partly Cloudy | Built-in condition. Changing it in the editor applies it immediately. |
| Weather **R** | (from type) | The seven live values; see Weather state below. Can be keyed in Sequencer. |
| Wind Direction **R** | 30 | Yaw, in degrees, that the wind blows towards. |
| Preview Weather In Editor | on | Run precipitation and lightning in the editor viewport. |
| Weather Parameters | MPC_MeridianWeather | Material parameter collection that receives weather values. |

**Surfaces**

| Setting | Default | Notes |
|---|---|---|
| Wetness **R** | 0 | 0 to 1. Updated by the actor during play. |
| Snow Cover **R** | 0 | 0 to 1. Updated by the actor during play. |
| Wetness Build Seconds | 40 | Seconds of full rain to soak surfaces. |
| Dry Seconds | 240 | Seconds for soaked surfaces to dry. |
| Snow Build Seconds | 180 | Seconds of full snowfall for full cover. |
| Snow Melt Seconds | 400 | Seconds for full cover to melt in dry weather. |

### Precipitation

| Setting | Default | Notes |
|---|---|---|
| Rain System | NS_MeridianRain | Niagara system. Receives user parameters `Intensity` (float) and `Wind` (vector, cm/s). |
| Snow System | NS_MeridianSnow | As above. |

### Lightning

| Setting | Default | Notes |
|---|---|---|
| Lightning Flash Intensity | 25 | Peak lux of a nearby flash. |
| Lightning Color | pale blue | Colour of the flash and the bolt. |
| Lightning Distance Range | 300 to 6000 | Nearest and farthest strike distance, in metres. |
| Thunder Sound | none | Played after each strike, delayed by distance. |
| Lightning Material | M_MeridianLightning | Draws the bolt. Receives `Seed`, `Intensity`, `Colour`. |
| Bolt Brightness | 40 | Brightness of the visible bolt. |

How often lightning strikes is part of the weather (Lightning Rate), not a setting here.

### Night Sky

| Setting | Default | Notes |
|---|---|---|
| Sky Material | M_MeridianSky | Draws atmosphere, sun disc, stars, moon and aurora. Clear for the plain engine sky. |
| Star Brightness | 3.5 | Brightness of stars on a clear night. |
| Moon Brightness | 0.45 | Brightness of the moon's disc; separate from the light it casts. |
| Moon Angular Diameter | 2 | Degrees. The real moon is about 0.5. |
| Aurora Strength **R** | 0 | 0 to 1. Night only. Can be keyed in Sequencer. |
| Aurora Brightness | 1 | Brightness at full strength. |

### Sound

| Setting | Default | Notes |
|---|---|---|
| Rain Loop Sound | none | Looping ambience; volume follows rain. |
| Wind Loop Sound | none | Looping ambience; volume follows wind speed. |

### Exposure

| Setting | Default | Notes |
|---|---|---|
| Limit Exposure | on | Clamp auto exposure so night stays dark. Applies level-wide at low priority. |
| Exposure Range EV | 0 to 6 | Darkest and brightest scene the camera adapts to, in EV100. |

### Clouds

| Setting | Default | Notes |
|---|---|---|
| Cloud Material | M_MeridianClouds | Must be a Volume material with *Used with Volumetric Cloud* enabled. |
| Cloud Color | near white | Cloud colour in fair weather. |
| Storm Cloud Color | grey | Cloud colour at full storminess. |
| Cloud Drift Scale | 3 | Cloud speed relative to ground wind. |

**Advanced** (only needed with a custom cloud material)

| Setting | Default | Notes |
|---|---|---|
| Coverage Parameter Name | `Coverage` | Scalar that receives cloud cover. |
| Coverage Parameter Range | 0 to 1 | Values written at cover 0 and 1. |
| Density Parameter Name | `Density` | Scalar that receives cloud density. |
| Cloud Density Range | 0.01 to 0.012 | Values written at storminess 0 and 1. |
| Cloud Color Parameter Name | `CloudColor` | Vector that receives the cloud colour. |
| Storm Parameter Name | `Storminess` | Scalar that receives storminess. |
| Wind Offset Parameter Name | `WindOffset` | Vector that receives accumulated cloud drift, in cm. |

Set a name to None to stop the actor writing that parameter.

### Fog

| Setting | Default | Notes |
|---|---|---|
| Bad Weather Fog Color | blue-grey | Fog colour under cloud, rain or snow. In clear weather fog takes the sky's colour. |
| Fog Brightness | 1 | Brightness of bad-weather fog in daylight. |
| Fog Density Range (advanced) | 0.005 to 0.25 | Height fog density at fogginess 0 and 1. |

## Weather state

`Meridian Weather State` is the struct behind the **Weather** setting, weather presets, and
**Change Weather To State**.

| Field | Range | Meaning |
|---|---|---|
| Cloud Coverage | 0 to 1 | Clear sky to overcast. |
| Storminess | 0 to 1 | Fair-weather cloud to dark, thick storm cloud. |
| Fogginess | 0 to 1 | Clear air to thick fog. |
| Rain | 0 to 1 | Dry to downpour. |
| Snow | 0 to 1 | None to heavy snowfall. |
| Wind Speed | 0 to 40 | Metres per second. 5 is a breeze, 15 a gale. |
| Lightning Rate | 0 to 60 | Average strikes per minute. |

**Meridian Weather Preset** is a data asset holding one weather state. Create one from the Content Browser under
Miscellaneous > Data Asset.

## Blueprint functions

### Changing things

| Function | Purpose |
|---|---|
| Set Time Of Day (hours) | Jump to a time. Values outside 0 to 24 wrap. Does not fire sunrise or sunset events. |
| Set Date (year, month, day) | Set the date. Impossible dates are clamped to the month's last day. |
| Change Weather (type, seconds) | Blend to a built-in condition. 0 seconds switches instantly. |
| Change Weather To State (state, seconds) | Blend to custom values. |
| Change Weather To Preset (preset, seconds) | Blend to a preset asset. |
| Set Cloud Coverage (0 to 1) | Change cloud cover alone, keeping the rest of the weather. |
| Set Fogginess (0 to 1) | Change fog alone. |
| Set Aurora Strength (0 to 1) | Turn the aurora up or down. |
| Trigger Lightning Strike | Strike now, in any weather. |
| Update Sky | Reapply everything after changing settings directly from Blueprint. |

In multiplayer, call these on the server.

### Reading things

| Function | Returns |
|---|---|
| Is Daytime | True while the sun is above the horizon. |
| Get Sun Direction | Unit vector from the level towards the sun. |
| Get Moon Direction | Unit vector towards the moon. |
| Get Celestial State | Sun and moon azimuth and elevation, moon phase and illumination, sidereal angle. |
| Get Displayed Weather | The weather as shown on this machine (smoothed on clients). |
| Get Wind Velocity | Wind as a vector in cm/s. |

## Events

Bind to these on the actor in Blueprint.

| Event | Fires when |
|---|---|
| On Sunrise | The sun crosses above the horizon as time advances. |
| On Sunset | The sun crosses below the horizon as time advances. |
| On Hour Changed (hour) | The hour of the day changes. |
| On Day Changed | Midnight passes. |
| On Lightning Strike (location, distance in metres) | Lightning strikes. Fires on every machine. |

Time events fire during play only, and not for jumps made with Set Time Of Day.

## Automated tests

The plugin includes automated tests for sun and moon positions, weather blending and transitions, surface wetness
and snow, and time of day. They appear under **MeridianSky** in the Session Frontend's Automation tab, or run from
the command line with the editor closed:

```
UnrealEditor-Cmd.exe <YourProject>.uproject -ExecCmds="Automation RunTests MeridianSky; Quit" -unattended -nullrdr -nosplash -nosound
```
