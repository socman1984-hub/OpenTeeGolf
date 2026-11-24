# Course

Courses as data: what a course is, where courses are, and the files they're made from.

| Class | What it does |
|---|---|
| `UOTGCourseData` (`OTGCourseData.h`) | One course's play data: holes, tees, pins, the surfaces, the heightmap, trees and buildings for the ball, and the tree design. `FillPlayCourse` and `FillTrees` fill the ball physics' `otg::PlayCourse` (Plugins/OTGBallPhysics). |
| `UOTGCourseSubsystem` (`OTGCourseSubsystem.h`) | The courses the game has: course packs and exported folders (`Documents\OpenTeeGolf\Courses`). `PlayCourse(id)`, `PlayRange()`, and the command line's `-course=`, `-range` and `-shot=`. |
| `FOTGCourseSource` (`OTGCourseSource.h`) | A course's source files as the course editor works on them (`<id>.course.json`, `<id>.real.json`). |
| `otg::QuickUpdate` (`OTGQuickUpdate.h`) | The course editor's changes put into the game in seconds, without the web game's full export. |
| `OTGTextures.h` | Pictures from an export's folder (PNG, JPEG) made into textures at run time. |

The `.glb` reader is its own plugin, `Plugins/OTGGlb`.
