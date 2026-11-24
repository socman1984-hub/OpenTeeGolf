# OTGMinimap

A golf hole's minimap, as the web version's `main.js drawMinimap` draws it. The course's aerial
photo is turned so the hole runs up the map, with the tee at the bottom and the green at the top. On
it are the ball, the aim line and point, the pin and the other players' balls. It shows the yardages
to the aim point and what's left to the pin, or to wherever the cursor is. Click or drag on it to aim.

## Main class

`SOTGMinimap` (`Minimap/SOTGMinimap.h`), a Slate leaf widget:

- `SetCourse(photo, x0, z1, sizeM, surfaceName)`: the photo over the land, its north-west corner and
  size in course metres. `surfaceName(x, z)` names the lie under the cursor.
- `SetState(FOTGMapState)`: the ball, aim, pin and tee (course x, z in metres), whether the ball is
  on the green, and the other balls with their colours. Call it whenever they change.
- `OnAim(x, z)`: called when the player clicks or drags on the map.

It depends only on the engine and OTGUiKit; it knows nothing about the game's types.

## How the game uses it

`SOTGHud` puts it at the HUD's right. `AOTGRound` (`FOTGRoundHud::UpdateMap`) sets its state, and
`OnAim` turns the aim.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder together with OTGUiKit. It's enabled by default. Add
`"OTGMinimap"` to your module's dependencies in its `.Build.cs`.
