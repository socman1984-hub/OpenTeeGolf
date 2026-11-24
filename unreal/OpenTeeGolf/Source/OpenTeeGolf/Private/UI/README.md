# UI

The game's Slate widgets. Their look comes from `OTGUi.h` (Plugins/OTGUiKit).

| Folder | Widgets |
|---|---|
| `Menus/` | `SOTGMainMenu` (Play, Practice, Players, Settings, What's new; one file per page) and `SOTGLaunchMonitorPanel`. Their shared look is in `SOTGMenuStyle.h`. |
| `Builder/` | `SOTGCourseBuilder` (the New course page) and `SOTGWorldMap` (the map you find a course on). |
| `Designer/` | `SOTGCourseDesigner` (R in a round), with `SOTGGrassDesigner` and `SOTGTreeDesigner` on its tabs. |
| `Editor/` | `SOTGCourseEditor` (the course editor), `SOTGEditCanvas` (its map), `SOTGMowSwatch`. |
| `Hud/` | `SOTGHud` (the round's HUD), `SOTGPanels` (the weather and test-shot panels), `SOTGRange` (the range panel). |
| (here) | `SOTGLoading` (the loading screen). |

The minimap and the wind box are plugins (Plugins/OTGMinimap, Plugins/OTGWindIndicator). Each
folder's `*Shared.h` or `*Style.h` holds helpers private to that folder.
