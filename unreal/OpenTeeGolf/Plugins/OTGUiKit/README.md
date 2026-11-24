# OTGUiKit

The game's look in Slate, shared by the game's own widgets and the widget plugins (OTGMinimap,
OTGWindIndicator). It's one header, `UI/OTGUi.h`, with everything in `namespace otg::ui`:

- fonts: `Font(size, weight)`
- colours: `Panel`, `Solid`, `Ink`, `Soft`, `Gold`, `Warn`, `Green`
- `Box(content)`: a dark glass panel
- `Text(string, size, colour)`: a text block
- `Button(label, onClick, size, on)` and `ButtonStyle(on)`: rounded buttons, the "on" ones green

## How it's used

`#include "UI/OTGUi.h"`, then `using namespace otg::ui;` and build widgets with these helpers.
Change a colour here and the HUD, the menus and the plugins all follow.

## Taking it into another project

Copy this folder into your project's `Plugins/` folder. It's enabled by default. Add
`"OTGUiKit"` to your module's dependencies in its `.Build.cs`.
