# App

The game's frame: what runs before and around a round.

| Class | What it does |
|---|---|
| `AOTGGameMode` (`OTGGameMode.h`) | In the start level, shows the main menu (`UI/Menus`). In a course's level, starts the round (`Round/OTGRound.h`), the course built first if it comes from an export. |
| `UOTGHelperSubsystem` (`OTGHelperSubsystem.h`) | The course helper: the web version's Node server run hidden, on this computer only. The course builder and editor find, build, export and save courses through it. |
| `UOTGNetSubsystem` (`OTGNetSubsystem.h`) | Online games over direct IP: hosting (TCP port 7787 unless changed) and joining, the lobby, the host's course checked file by file and downloaded to the players who need it (only the files they don't have), and the round's messages passed between the games. |
| `otg::sync` (`OTGCourseSync.h`, private) | A course folder's files compared with the host's: sizes and SHA-1s, what to keep, copy from another course here or download, and the marker (`.otg_shared.json`) that names a saved copy of another player's version. |
| `UOTGUpdateSubsystem` (`OTGUpdateSubsystem.h`) | Keeps the game up to date from its GitHub releases, downloading only the files that changed. |

The module itself is `OpenTeeGolfModule.cpp`, one line.
