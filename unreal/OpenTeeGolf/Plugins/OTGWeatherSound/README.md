# OTGWeatherSound

The weather's sound, made as it plays: no recordings, nothing to license.

- **Rain:** a soft steady hiss over a low wash, with a patter of tiny drop ticks, as heavy as the rain.
- **Wind:** a low rush rising and falling with the gusts.
- **Thunder:** a crack when the strike is near, then a long rolling rumble.

It's stereo, with each side its own noise, so the rain is all round. Its strength drifts slowly at
random, so nothing in it repeats.

## Main class

`UOTGWeatherSound` (`WeatherSound/OTGWeatherSound.h`), a procedural sound wave:

- `SetLevels(rain, wind)`, each 0 to 1. It's safe to call every frame.
- `Thunder(delaySeconds, loud)` queues a clap. Delay it by the distance: a second for each 343 m.

## How the game uses it

`AOTGRound` (`FOTGRoundScene::UpdateStorm`) creates one, plays it with
`UGameplayStatics::CreateSound2D`, sets the levels from the weather each frame, and queues thunder
for each lightning strike.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder (it needs the engine's AudioExtensions module). It's enabled by default. Add
`"OTGWeatherSound"` to your module's dependencies in its `.Build.cs`.
