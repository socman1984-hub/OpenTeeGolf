# App

The game's frame: what runs before and around a round.

| Class | What it does |
|---|---|
| `AOTGGameMode` (`OTGGameMode.h`) | In the start level, shows the main menu (`UI/Menus`). In a course's level, starts the round (`Round/OTGRound.h`), the course built first if it comes from an export. |
| `UOTGHelperSubsystem` (`OTGHelperSubsystem.h`) | The course helper: the web version's Node server run hidden, on this computer only. The course builder and editor find, build, export and save courses through it. |
| `UOTGUpdateSubsystem` (`OTGUpdateSubsystem.h`) | Keeps the game up to date from its GitHub releases, downloading only the files that changed. |

The module itself is `OpenTeeGolfModule.cpp`, one line.
