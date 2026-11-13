OpenTeeGolf (Unreal Engine edition)
===================================

Golf on real courses with your launch monitor, in Unreal Engine 5.

Starting
--------
Double-click OpenTeeGolf.exe. (If Windows says a DLL such as VCRUNTIME140 or MSVCP140 is missing,
install Microsoft's "Visual C++ Redistributable" for x64 from https://aka.ms/vs/17/release/vc_redist.x64.exe
and start it again. The first start takes longer while the graphics card prepares its shaders.)

You need a graphics card that runs DirectX 12 (a recent NVIDIA, AMD or Intel card) and about 4 GB
of free disk space.

The menu
--------
Play            Pick the course, the tees and pins, where to start (hole 1, 10 or any) and 18 or 9
                holes; who's playing (click players to add them - the order you pick them is the
                order they tee off - and A/B for teams); the game (stroke play, Stableford, match
                play, skins, best ball, scramble, alternate shot) and whether handicaps count; and
                the conditions (live weather at the course, a preset, or a custom wind). Then
                "Start the round".
Players         Each player's name, colour, handicap, hand and own tees. Their finished rounds are
                kept with them.
Launch monitor  How your shots arrive, its status, and the Mevo+ setup (below).
Settings        Full screen or a window, the resolution, the graphics quality (lower it if the game
                runs slowly), vertical sync.

Your launch monitor
-------------------
FS Golf (OpenConnect)  In FS Golf, connect to this PC (OpenConnect, port 921). Close anything else
                       that uses that port (another simulator, the web version of OpenTeeGolf).
Mevo+ directly         Close FS Golf, join this PC to the Mevo+'s own Wi-Fi (FS M2-...), and enter
                       your setup: the distance from the Mevo+ to the ball, the mat height, the tee
                       height. "Arm only on my turn" keeps it quiet between shots.
Garmin R10             Pair the R10 in Windows Bluetooth settings first. The game runs its bridge
                       program (bridges\tenover-frp...) itself.
Square Golf            Turn it on; the game runs its bridge program (bridges\allsquare-frp...).
Other (FRP)            Any launch monitor that speaks the Flight Relay Protocol: enter its address.

The status turns green when the launch monitor is ready. No launch monitor? Press T on the course
for the test shot panel.

On the course
-------------
Click the ground or the map    aim there (drag on the map too)
Left / Right (or A / D)        fine aim (Shift: faster)
Up / Down (or W / S)           a longer / shorter club (the game picks one for the distance)
T                              test shot panel: presets and the numbers, then Hit
Space                          a quick test shot with the club in hand
Enter                          skip the ball's flight; the next hole
Tab                            the scorecard
Backspace                      mulligan
Esc                            pause: resume, scorecard, launch monitor, quit to the menu

The map at the top right shows the hole, your aim and the yardages; the box above it the wind
against your aim (the arrow points the way it blows: up is helping).

Your players, last setup and settings are saved in %LOCALAPPDATA%\OpenTeeGolf\Saved (type that
in Explorer's address bar to open it); a new version of the game keeps them.

More courses
------------
Courses are built from the web version of OpenTeeGolf (Ctrl+Shift+U exports one) and packed into
the game with unreal\make_pack.ps1; see unreal\README.md in the OpenTeeGolf project.

OpenTeeGolf is free software; see LICENSE-OpenTeeGolf.txt. Third-party software, assets and the
course data are listed in NOTICE.md.
