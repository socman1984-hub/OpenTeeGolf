# Build a course pack's content in the OpenTeeGolf game project (unreal/OpenTeeGolf), from an export
# of the web version (public/js/export-unreal.js). Run headless by make_pack.ps1 - not by hand:
#
#   UnrealEditor-Cmd OpenTeeGolf.uproject -run=pythonscript -script=pack_course.py  (OTG_EXPORT_DIR set)
#
# Into /Game/Courses/<course id>/: a level with the course's landscape from heightmap.png and
# everything build_course.py builds on it (ground, turf, trees, water, paths, buildings, tees and
# pins), the play data as CourseData (UOTGCourseData), and a PrimaryAssetLabel that puts all of it in
# a chunk of its own: the course's pak when the game is packaged.

import json
import os
import traceback
import zlib

import unreal

EXPORT_DIR = os.environ["OTG_EXPORT_DIR"]


def log(msg):
    unreal.log("[OpenTeeGolf pack] " + msg)


def chunk_id(course_id):
    """A stable chunk number for the course (1000-9999): the same course always lands in the same pak"""
    return 1000 + zlib.crc32(course_id.encode("utf-8")) % 9000


meta = json.load(open(os.path.join(EXPORT_DIR, "course.json"), encoding="utf-8"))
if not os.path.exists(os.path.join(EXPORT_DIR, "play.json")):
    raise RuntimeError("No play.json in %s: export the course again from the game (Ctrl+Shift+U)" % EXPORT_DIR)
cid = meta["id"]
PKG = "/Game/Courses/" + cid
LEVEL = PKG + "/" + cid
eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# ---- a fresh level: the basic template's sky and lights, no floor
if os.path.exists(os.path.join(EXPORT_DIR, "pack_done.txt")):
    os.remove(os.path.join(EXPORT_DIR, "pack_done.txt"))
if eal.does_directory_exist(PKG):
    log("Replacing the course's earlier pack content")
    eal.delete_directory(PKG)
if not les.new_level_from_template(LEVEL, "/Engine/Maps/Templates/Template_Default"):
    les.new_level(LEVEL)
for a in actors.get_all_level_actors():
    if a.get_actor_label().lower().startswith("floor"):
        actors.destroy_actor(a)

# ---- the landscape, its north-west corner at the origin
L = meta["landscape"]
corner = unreal.Vector(0, 0, L["locationZ"])
land = unreal.OTGPackTools.create_landscape(os.path.join(EXPORT_DIR, "heightmap.png"), corner, unreal.Vector(L["scaleXY"], L["scaleXY"], L["scaleZ"]))
if not land:
    raise RuntimeError("The landscape wasn't made")

# ---- everything on it: build_course.py, with this course's export and content folder
os.environ["OTG_CONTENT"] = PKG + "/Build"
build = os.path.join(os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.environ["OTG_SCRIPT_DIR"], "build_course.py")
exec(compile(open(build, encoding="utf-8").read(), build, "exec"), {"__name__": "__main__", "__file__": build})

# ---- the play data, and the label that makes the folder a pak of its own
origin = unreal.Vector(corner.x, corner.y, corner.z - L["locationZ"])
data = unreal.OTGPackTools.create_course_data(EXPORT_DIR, PKG, "CourseData", LEVEL + "." + cid, origin)
if not data:
    raise RuntimeError("The course data wasn't made")
label = unreal.AssetToolsHelpers.get_asset_tools().create_asset("PackLabel", PKG, unreal.PrimaryAssetLabel, unreal.DataAssetFactory())
rules = label.get_editor_property("rules")
rules.set_editor_property("chunk_id", chunk_id(cid))
rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
rules.set_editor_property("apply_recursively", True)
label.set_editor_property("rules", rules)
label.set_editor_property("label_assets_in_my_directory", True)
eal.save_loaded_asset(label)

les.save_current_level()
eal.save_directory(PKG)
log("Pack content for %s in %s (chunk %d)" % (meta["name"], PKG, chunk_id(cid)))
# (for make_pack.ps1: the end of the log isn't always written when Unreal quits)
open(os.path.join(EXPORT_DIR, "pack_done.txt"), "w").write("%s %d" % (cid, chunk_id(cid)))
