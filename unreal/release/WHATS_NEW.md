# What's new in OpenTeeGolf for Unreal Engine

The newest version first. The game shows this on its What's new page. make_release.ps1 copies it next to
OpenTeeGolf.exe, and publish_release.py puts each version's part on its GitHub release.
A version's heading is "## <version> - <date>", and its changes are "- " lines under it.

## 0.26.2 - 25 November 2026
- rēlā: a new choice in the Launch monitor panel. rēlā (docs.rela.golf) connects many launch monitors, such as SkyTrak, GC2, FlightScope, Nova, Mevo+, R10 and Square. In rēlā choose GSPro as the simulator and, under Settings > Simulator Connection, this PC (127.0.0.1) and port 921.

## 0.26.1 - 25 November 2026
- New course is now called Course builder. A course already in the game is edited from the Play page: pick it, then Edit this course. Update its map data is there too.
- The Course builder no longer looks up what's mapped as soon as you pick a course: press See what's mapped there, and a moving bar shows it's working.
- Grass biomes: each kind has a checkbox and stays in its place in the list when turned on or off. Each is folded to one line until you open it (+), or use Open all and Fold all.
- Grass biomes and forests: fly round the preview with W A S D, Q / E down and up, the right mouse button to look and Shift to go faster. The camera no longer goes under the grass.
- The course editor's choices of grass biome and forest are dropdown lists.
- The sea mist draws properly again instead of with a plain stand-in material.

## 0.26.0 - 25 November 2026
- Mevo+ live view in the game: the Launch monitor panel shows the Mevo+ camera, with the "ball here" guide, to help you line up the ball. It works whether or not the Mevo+ is armed.
- Mevo+ fast shots (Launch monitor setup, off by default): full swings take off sooner. The ball starts from the radar's first reading and waits a moment for the measured spin (0.7 s unless you change it, from 0.6 to 3 s).
- The shot data shows both of the Mevo+'s spin readings, the first estimate and the final one.
- Full swings always use the Mevo+'s net and simulator mode, which reads spin on every shot and finishes sooner. The setup no longer asks you to choose a detection mode.

## 0.25.0 - 25 November 2026
- Grass biomes: in New course > Grass biomes, mix the tall grasses, wildflowers, heather and ferns into your own "biome" (how many of each, how tall, in what patches, what colour) and see it grow on a preview plot as you change it. Five come ready made: heathland, wildflower meadow, links dunes, woodland floor and the game's own tall grass.
- Forests: in New course > Forests, mix tree species into a forest (how many of each, drawn as which model, how tall, how close, and the grass under them), with a live preview. Four come ready made.
- Your biomes and forests are kept for every course, in Documents\OpenTeeGolf\Library.
- In the course editor, give a course's tall grass a biome, or draw areas with their own (the rough stays one even colour).
- In the course editor, click inside a group of trees to mark it as a forest (or draw one), then make it thicker or thinner, its trees bigger or smaller, or give it one of your forests. It's in the game the next time you play the course: no rebuild needed.

## 0.24.0 - 20 November 2026
- Creeks are full of water right to their ends: no more dry dirt where a creek meets a pond, and the creek joins the pond at its level.
- A pond that sits above a creek is held back by a grassy bank instead of its water hanging in the air.
- Creek banks are gentle grassy slopes instead of steep dirt cliffs, and a wide river's bed is full of water instead of bare dirt.
- Rain makes small rings on ponds and creeks, more of them the harder it rains.
- Parked cars in the car parks, in a car park's usual mix of colours.
- Greens are smooth turf, without dark specks.
- The rough looks like grass much further out, all the way round a long hole's green, and the game runs a little faster.
- Grass at the edge between two cuts no longer shows pale slivers.

## 0.23.0 - 20 November 2026
- More natural grass colour: a true living-lawn green instead of lime, a touch darker on every cut.
- Fairways and tees are a smooth mown carpet, without separate blades sticking up.
- Ponds have smooth, rounded edges, with the water and grass meeting all the way round: no jagged shoreline, no ledge and no gap between the water and the bank.
- The plain box cars are gone from the car parks (cars from a car pack can stand there instead).

## 0.22.0 - 20 November 2026
- Brighter, more natural grass: fresher lime greens on every cut, and a longer, lusher rough that glows when the sun is behind it.
- The rough looks like real grass all the way out: its ground is now made from the game's own grass, not a flat green.
- Water flows around rocks in creeks instead of running straight through them.
- A splash and spreading ripples when your ball goes in the water.
- A sandy beach along the sea, and the water's edge follows the shore smoothly.
- Ponds fill right up to their banks, and are darker and greener the deeper they are.
- Cart paths have smooth, straight edges.
- Rain: no more wall of rain in the distance, splashes only on hard ground, and a natural rain sound.
- Wildflowers no longer show as yellow blotches from afar.

## 0.21.0 - 19 November 2026
- Real houses round the course: the homes along the fairways are now proper houses, with roofs, windows, porches and garages, instead of plain blocks.
- Calmer shores: no more white surf washing up over dry land at the sea.
- Ponds fill right up to their banks, and creeks run without a white edge of foam.
- Darker, greener rivers and streams, getting darker the deeper they are.
- Rocks in a creek now sit on its bed instead of floating.
- The deep rough blends into the rough instead of meeting it at a hard line.
- The camera stays above the water.

## 0.20.0 - 19 November 2026
- New water everywhere (Tidewright): ponds and lakes ripple in the breeze and mirror the trees, rivers and streams flow downhill between their banks, and the sea rolls in with real waves, whitecaps and surf breaking along the shore.
- Water hazards play just as before.
- Runs as fast as before.

## 0.19.1 - 18 November 2026
- Smooth grass lines: the rough meets the tees, fairways, collars and bunkers in a clean curve again, not a jagged one.
- Matching colours: every cut is now the same fresh green, lighter the shorter it's mown - the greens lightest, then the fairways and tees, then the rough - so the course reads as one lawn.
- A deep rough: thick, knee-high grass under the tall grass, so you can see where the rough ends.
- Runs as fast as before.

## 0.19.0 - 17 November 2026
- A new sky (Meridian Sky): the sun sits where it really is over each course at the time of day you pick, so every course gets its own light, and evening rounds end in a golden sunset.
- New clouds, from a few fair-weather puffs to a grey overcast deck and dark storm clouds, drifting with the wind and casting their shadows across the course.
- Storms: you can now see the lightning bolts, and the thunder follows.
- New rough (Greensward): denser, even-coloured grass that meets the first cut, the collars and the bunkers with a natural ragged edge.
- Runs as fast as before, and on some courses a little faster.

## 0.18.0 - 17 November 2026
- A new sky: real clouds that drift with the wind - a few fair-weather clouds, big heaped cumulus, a grey overcast layer, dark storm clouds - and their shadows move across the course.
- Rain looks like rain: a grey sky, thinner drops, splashes on the ground, rings on the ponds, and grey curtains of rain over the far end of the hole.
- The course gets wet in the rain: the grass, paths and roofs darken and shine, and dry slowly once it stops.
- Storms: lightning lights up the clouds and the course, and the thunder follows.
- Sound: rain, wind that rises and falls with the gusts, and thunder.
- "Changing" weather: showers come and go during the round (now and then a storm). Live weather is fetched again every 15 minutes.
- New grass: fine, dense turf cut to its height - greens, collars, fairways and tees, a first cut, then the rough - with crisp mowing lines between them and the stripes in the blades themselves.
- Every surface plays differently: the ball stops quickly in the rough, plugs in sand, and bounces and runs on cart paths.
- Roads and car parks are drawn from the map (export the course again to get them). The heather is off unless you turn it on.
- Water: no more lines across streams or brown patches in ponds.
- The shot data tiles placed on the right sit under the minimap, not behind it.
- Overcast and rain skies are grey, not blue with sunshine.

## 0.17.3 - 16 November 2026
- Shot data: ball speed, club speed, smash, launch, direction, spin and more as tiles after each shot. Press D to pick which show and where on the screen.
- The course's name, and the ball's lie (fairway, rough, bunker...) on its own line in the corner.
- Over the flag: how far to the pin, and how far uphill or downhill it is.
- The course editor shows the trees you took out or added in a round (R), and can put them back.
- Saving in the course editor now also sends the game changes it hadn't received yet, such as removed bunkers.
- A "Join the Discord" button and a What's new page on the menu.

## 0.17.2 - 16 November 2026
- Players page: pick each player's colour from swatches, and the chosen hand and tees are highlighted.
- New course: every course you've built is listed, including ones made with the web version, and each can be edited.
- Play page: "Edit this course" opens the picked course in the editor.
- World map: tick boxes show or hide built courses, unbuilt ones and search results.
- Course designer: the tree or rock you click is marked with a gold arrow and a ring.
- Course designer: "Drawn as" and "Looks like" are drop-down lists.
- Course designer: the lists keep their place when you change something.
- Course designer: recolour the fairways (and tees) and the greens, saved for each course.

## 0.17.1 - 16 November 2026
- The installer is about half the size (760 MB).
- Fixed a crash when a queued course finished building after the New course page was closed.

## 0.17.0 - 16 November 2026
- A new main page: search and sort your courses, the picked course up top, and the round's options in cards.
- Practice: a driving range with target greens, every shot's numbers and averages per club.
- Practice: nine holes of putt-putt, with a windmill, tunnel, ramps and a pond.
- Course designer: colour the rough and tall grass, and fly to where a grass grows.
- Course designer: change, add or remove trees and rocks one by one.
- Heather, wildflowers, Scots pine, hill oak and rocks to plant.

## 0.16.3 - 15 November 2026
- Build courses in the background: queue several and keep playing.
- The course editor's side panel is on tabs, one section at a time.
- Fixed a crash drawing a course's rough.
- Changing the time of day under cloud no longer turns the sky black.

## 0.16.2 - 15 November 2026
- New course: the world's courses grouped on the map, and how a build is going.

## 0.16.1 - 15 November 2026
- Building courses works in the installed game.

## 0.16.0 - 15 November 2026
- New course: find a real course on a world map and build it in the game.
- The course editor: holes, greens, fairways, bunkers, water, trees, tall grass and buildings, with most changes in the game in seconds.
- The course designer (R in a round): the rough and tall grass, and each tree species' model, size and number.
- The sea beside seaside courses.
