# OTGWindIndicator

The wind box, as the web version's `main.js` shows it: an arrow showing which way the wind blows
against the aim (up is helping, down is into you), and two lines beside it, its speed and gusts
and how it plays.

## Main classes

- `SOTGWind` (`Wind/SOTGWind.h`): the box. `Set(arrowDegrees, calm, line1, line2)`, where the arrow
  is in degrees clockwise from up, and calm hides it.
- `SOTGWindArrow`: just the arrow in its ring, `SetAngle(degrees, calm)`.

They depend only on the engine and OTGUiKit.

## How the game uses it

`SOTGHud` shows it above the minimap. `AOTGRound` (`FOTGRoundHud::UpdateWindBox`) sets it from
the hole's wind and the aim.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder together with OTGUiKit. It's enabled by default. Add
`"OTGWindIndicator"` to your module's dependencies in its `.Build.cs`.
