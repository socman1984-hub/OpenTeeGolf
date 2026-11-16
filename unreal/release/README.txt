OpenTeeGolf (Unreal Engine edition)
===================================

Golf on real courses with your launch monitor, in Unreal Engine 5.

Starting
--------
Run OpenTeeGolf-Setup-<version>.exe (no admin needed; if Windows says it protected your PC, choose
More info, Run anyway), or unzip the game anywhere and double-click OpenTeeGolf.exe. On its first
start the game downloads its scenery (about 3 GB) and restarts once. After that it keeps itself up
to date: when it starts it checks for a new version and downloads only what changed (Settings >
Updates to check now). (If Windows says a DLL such as VCRUNTIME140 or MSVCP140 is missing,
install Microsoft's "Visual C++ Redistributable" for x64 from https://aka.ms/vs/17/release/vc_redist.x64.exe
and start it again. The first start takes longer while the graphics card prepares its shaders.)

You need a graphics card that runs DirectX 12 (a recent NVIDIA, AMD or Intel card) and about 4 GB
of free disk space.

The menu
--------
Play            Pick the course from the list on the left: type in the search box to find one by
                name or place (Enter picks the first match), and sort by A-Z, Recently played or
                Newest. The course picked shows on the right with its holes, par and length. Then
                set up the round: the tees and pins, 18 or 9 holes and where to start (hole 1, 10
                or any); the conditions (live weather at the course, a preset, or a custom wind);
                who's playing (click players to add them - the order you pick them is the order
                they tee off - and A/B for teams); the game (stroke play, Stableford, match play,
                skins, best ball, scramble, alternate shot) and whether handicaps count. The bar
                at the bottom sums it up: "Start the round".
Practice        A driving range and a putt-putt course (coming soon).
New course      Find a real course on the world map and build it (see Courses, below).
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
R                              the course designer, changed as you watch. Grass: what the rough and
                               the tall grass are made of (which grasses, how many, how tall, how
                               far out, in what patches, their colour - golder or bluer, stronger
                               or softer, lighter or darker, within what grass can look like (the
                               Megascans plants: stronger or softer, lighter or darker); add
                               any Megascans plant to the tall grass, or heather, bracken and
                               wildflowers - these keep their own colours), "Show me" flies to
                               where it grows. Trees and rocks: each species' model (Scots pine
                               and hill oak among them), size and how many (more
                               grows others round them; the ball meets the trees as you see them),
                               "Show me" flies to one. And one at a time: click a tree on the course
                               to change its species, model, height or leaf colour (autumn to
                               blue-green; broadleaf trees - the conifers keep theirs), a rock its
                               size and look (mossy, grey, river stone, volcanic ... or all the
                               course's rocks at once), or take either out; or pick Add a tree
                               (any species and height) or Add a rock and click the ground. "Save
                               for this course" keeps it (grass.json, trees_design.json in the
                               course's folder)
Esc                            pause: resume, scorecard, launch monitor, quit to the menu

Driving range
-------------
Practice: driving range (at the top of the Play page) opens the range: target greens with flags
at 50, 100, 150, 200 and 250 yards, distance boards every 50 yards, mountains beyond. Hit from the
mat with your launch monitor, or T / Space for test shots. After each shot the panel on the right
shows carry, total, offline, apex, ball speed, launch, direction, spin and spin axis, with your
recent shots and averages for the club in hand (gold marks on the range show where they stopped).
Up / Down change club (the aim moves to its carry), Left / Right or a click aim. A new ball comes
a few seconds after the last one stops; Enter or Backspace for one at once. Tab hides the
numbers; L the weather and time of day. The game makes the range itself the first time (in
%LOCALAPPDATA%\OpenTeeGolf\Saved\Range). Start the game with -range to go straight there.

The map at the top right shows the hole, your aim and the yardages; the box above it the wind
against your aim (the arrow points the way it blows: up is helping).

Your players, last setup and settings are saved in %LOCALAPPDATA%\OpenTeeGolf\Saved (type that
in Explorer's address bar to open it); a new version of the game keeps them.

Courses
-------
The game comes with no courses: you make your own on the New course page. Search for a course
by name or town, or drag and zoom the map (the map or the aerial photos) and click its dot - every
golf course mapped in OpenStreetMap is one; gold ones you've built. The game shows what's mapped
there (and, at a club with more than one course, asks which holes), then Build downloads the map
data, the ground's heights and aerial photos, builds the course and gets it ready to play (about 5
to 20 minutes; you need to be online, and Microsoft Edge or Google Chrome installed - Windows has
Edge). Then Play it. You can pick more courses while one builds: each goes in a queue, built
in the background (two at a time) while you play, and the round tells you when one is ready; if
you close the game, the rest carry on next time. Edit opens the course editor: the holes (tees, aim points, pins, par), the
shapes (greens, fairways, bunkers, tees, water, penalty areas, the boundary, tree areas), trees,
tall grass, buildings and the course's looks, on its aerial photo; "Save and update the game"
puts most changes in the game in seconds (the holes, greens, fairways, tees, bunkers, trees and
tall grass); "Put in the game (full)" gets it all ready again, water and the rest (5-20 minutes,
in the queue). Hover over a button for what it does. "Courses you've built" exports one again, brings it up to date from
OpenStreetMap, or deletes it. Built courses go in Documents\OpenTeeGolf\Courses, their
sources in Documents\OpenTeeGolf\CourseSources. The first time a course is opened the game builds
it (a loading screen shows how far along it is: under a minute for most courses). Courses exported
from the web version of OpenTeeGolf (Export to Unreal) still work too.

Settings > Courses folder shows where the game looks, opens it, and can point it somewhere else.
To share a course, copy its folder into someone else's Courses folder.

OpenTeeGolf is free software; see LICENSE-OpenTeeGolf.txt. Third-party software, assets and the
course data are listed in NOTICE.md.
