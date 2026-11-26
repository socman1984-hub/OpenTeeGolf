# The game's own assets (not a course's), made headless in the game project: run by make_assets.ps1
# before packaging (and with -Assets on its own). Into /Game/OpenTeeGolf/Game, cooked with the base
# game (DefaultGame.ini DirectoriesToAlwaysCook).
#
#   M_Rain: rain streaks - see-through, lit by nothing, fading in and out along each streak (the
#           round draws the streaks round the camera: AOTGRound, rain)

import os
import unreal

FOLDER = "/Game/OpenTeeGolf/Game"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[OpenTeeGolf assets] " + msg)


def material(name):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def rain():
    m = material("M_Rain")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    for prop in ("used_with_nanite", "used_with_instanced_static_meshes"):
        try:
            m.set_editor_property(prop, True)
        except Exception:
            pass
    E = lambda cls, x, y: mel.create_material_expression(m, cls, x, y)
    tint = E(unreal.MaterialExpressionVectorParameter, -600, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.75, 0.78, 0.82, 1))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # opacity: the parameter, times a fade along the streak (V: 0 at its top, 1 at its bottom) and
    # across it (U)
    op = E(unreal.MaterialExpressionScalarParameter, -600, 200)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.35)
    uv = E(unreal.MaterialExpressionTextureCoordinate, -900, 300)
    v = E(unreal.MaterialExpressionComponentMask, -750, 300)
    v.set_editor_property("g", True)
    mel.connect_material_expressions(uv, "", v, "")
    u = E(unreal.MaterialExpressionComponentMask, -750, 400)
    u.set_editor_property("r", True)
    mel.connect_material_expressions(uv, "", u, "")
    # across: 1 - |2u - 1|
    two = E(unreal.MaterialExpressionConstant, -750, 500)
    two.set_editor_property("r", 2.0)
    mul = E(unreal.MaterialExpressionMultiply, -650, 450)
    mel.connect_material_expressions(u, "", mul, "A")
    mel.connect_material_expressions(two, "", mul, "B")
    one = E(unreal.MaterialExpressionConstant, -650, 550)
    one.set_editor_property("r", 1.0)
    sub = E(unreal.MaterialExpressionSubtract, -550, 450)
    mel.connect_material_expressions(mul, "", sub, "A")
    mel.connect_material_expressions(one, "", sub, "B")
    ab = E(unreal.MaterialExpressionAbs, -450, 450)
    mel.connect_material_expressions(sub, "", ab, "")
    across = E(unreal.MaterialExpressionOneMinus, -350, 450)
    mel.connect_material_expressions(ab, "", across, "")
    f1 = E(unreal.MaterialExpressionMultiply, -250, 300)
    mel.connect_material_expressions(v, "", f1, "A")
    mel.connect_material_expressions(across, "", f1, "B")
    f2 = E(unreal.MaterialExpressionMultiply, -150, 250)
    mel.connect_material_expressions(op, "", f2, "A")
    mel.connect_material_expressions(f1, "", f2, "B")
    mel.connect_material_property(f2, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_Rain made")


def course_level():
    """L_Course: the level an exported course is built in at run time (AOTGRuntimeCourse): the basic
    template's sky, sun, sky light, fog and clouds, as a course pack's level has them; no floor."""
    path = FOLDER + "/L_Course"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    if not les.new_level_from_template(path, "/Engine/Maps/Templates/Template_Default"):
        les.new_level(path)
    for a in actors.get_all_level_actors():
        if a.get_actor_label().lower().startswith("floor"):
            actors.destroy_actor(a)
    les.save_current_level()
    log("L_Course made")


# The scenery in pieces of its own (pakchunk<n> files), so the game's download is several files
# under GitHub's 2 GB limit and an update fetches only the pieces that changed: the game itself is
# chunk 0; each tree folder gets a label putting what's cooked from it, and all that uses, in its
# chunk. The tree folders aren't in git, so the labels are made here, before every release.
# KiteDemo (the heather, wildflowers, Scots pine, hill oak and rocks: OTGKiteDemo.h) is chunk 5.
# Each label names its assets one by one (not "everything in my folder"): the labels count in the
# cook (DefaultGame.ini: not editor-only), so a folder-wide one would cook the whole pack. What's
# named is what DefaultGame.ini's DirectoriesToAlwaysCook has in the pack, and for Megaplant the
# spruce's and pine's SM_ meshes (the trees the game draws: OTGRuntimeCourse.cpp ConiferFolders) -
# not its folders, whose skeletal trees and branches and procedural-vegetation sources are only for
# editing, and were half its download.
SCENERY_CHUNKS = (("/Game/EuropeanBeech", 1), ("/Game/EuropeanHornbeam", 2), ("/Game/Megaplant_Library", 3), ("/Game/Fab", 4), ("/Game/KiteDemo", 5))
NAMED_MESHES = {"/Game/Megaplant_Library": ("/Game/Megaplant_Library/Tree_Norway_Spruce", "/Game/Megaplant_Library/Tree_Baltic_Pine")}


def always_cooked():
    ini = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_config_dir()), "DefaultGame.ini")
    out = []
    for line in open(ini, encoding="utf-8-sig"):
        line = line.strip()
        if line.startswith("+DirectoriesToAlwaysCook=") and 'Path="' in line:
            out.append(line.split('Path="')[1].split('"')[0])
    return out


def named_assets(folder):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for d in always_cooked():
        if d == folder or d.startswith(folder + "/"):
            for a in ar.get_assets_by_path(d, recursive=True):
                if str(a.asset_class_path.asset_name) != "PrimaryAssetLabel":
                    out.append(str(a.package_name))
    for f in NAMED_MESHES.get(folder, ()):
        for a in ar.get_assets_by_path(f, recursive=True):
            if str(a.asset_class_path.asset_name) == "StaticMesh" and str(a.asset_name).startswith("SM_"):
                out.append(str(a.package_name))
    return [eal.load_asset(p) for p in sorted(set(out))]


def scenery_chunks():
    for folder, chunk in SCENERY_CHUNKS:
        if not eal.does_directory_exist(folder):
            unreal.log_warning("[OpenTeeGolf assets] No %s: its chunk is skipped" % folder)
            continue
        # (a label's name is its id: each its own)
        name = "OTG_Chunk%d" % chunk
        for old in (folder + "/OTG_ChunkLabel", folder + "/" + name):
            if eal.does_asset_exist(old):
                eal.delete_asset(old)
        label = tools.create_asset(name, folder, unreal.PrimaryAssetLabel, unreal.DataAssetFactory())
        rules = label.get_editor_property("rules")
        rules.set_editor_property("chunk_id", chunk)
        rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
        rules.set_editor_property("apply_recursively", True)
        label.set_editor_property("rules", rules)
        named = named_assets(folder)
        label.set_editor_property("label_assets_in_my_directory", False)
        label.set_editor_property("explicit_assets", named)
        eal.save_loaded_asset(label)
        log("%s: chunk %d, %d assets named" % (folder, chunk, len(named)))
        if not named:
            unreal.log_warning("[OpenTeeGolf assets] %s: nothing named for its chunk" % folder)


def fab_usage():
    """The Megascans plants (Content/Fab) are drawn as instances (OTGGrass.cpp), and their materials
    - the far billboards' above all - must say they may be: without it Unreal draws them with its
    default material, dark smudges across the meadows. Their base materials get the flag (Fab's
    content isn't in git, so it's set here each time)."""
    if not eal.does_directory_exist("/Game/Fab"):
        return
    done = set()
    for path in eal.list_assets("/Game/Fab/Megascans/Plants", recursive=True, include_folder=False):
        if not path.split(".")[-1].startswith("MI_"):
            continue
        mi = eal.load_asset(path)
        base = mi.get_base_material() if mi else None
        if not base or base.get_path_name() in done:
            continue
        done.add(base.get_path_name())
        changed = False
        for flag in ("used_with_instanced_static_meshes",):
            if not base.get_editor_property(flag):
                base.set_editor_property(flag, True)
                changed = True
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(base)
            eal.save_loaded_asset(base)
        log("Fab material %s: instanced ok%s" % (base.get_name(), " (set)" if changed else ""))


# The KiteDemo pack's folders the game uses (OTGKiteDemo.h; DefaultGame.ini cooks the same ones)
KITE_DIRS = ("Foliage/Flowers/Heather", "Foliage/Flowers/Yarrow", "Foliage/Flowers/Buttercup", "Foliage/Flowers/FieldScabious",
             "Foliage/Ferns", "Foliage/BogMyrtle_01", "Foliage/BogMyrtleBush_01", "Foliage/BogMyrtleBush_02", "Foliage/Grass/FieldGrass",
             "Foliage/Leaves", "Trees/ScotsPine_01", "Trees/ScotsPineTall_01", "Trees/HillTree_02", "Trees/HillTree_Tall_02",
             "Rocks/Medium_Boulder_001", "Rocks/Medium_Boulder_002", "Rocks/River_Rock_01", "Rocks/Boulder_05a",
             "Rocks/LargePlainsBoulder002", "Rocks/Large_Volcanic_Rock_001")


def kite_usage():
    """KiteDemo's plants, trees and rocks are drawn as instances too (OTGRuntimeCourse.cpp), and its
    trees' and rocks' materials don't say they may be (Unreal's grey default instead). Their base
    materials get the flag. KiteDemo isn't in git: done here each time. (Its meshes, made in an
    older Unreal, ask to be saved again - they're rebuilt at each load - but saving one while its
    build is still reading it brings the editor down: left as they are.)"""
    if not eal.does_directory_exist("/Game/KiteDemo"):
        return
    done = set()
    for d in KITE_DIRS:
        folder = "/Game/KiteDemo/Environments/" + d
        if not eal.does_directory_exist(folder):
            unreal.log_warning("[OpenTeeGolf assets] No %s" % folder)
            continue
        for path in eal.list_assets(folder, recursive=True, include_folder=False):
            if eal.find_asset_data(path).asset_class_path.asset_name not in ("Material", "MaterialInstanceConstant"):
                continue
            a = eal.load_asset(path)
            if not isinstance(a, unreal.MaterialInterface):
                continue
            base = a.get_base_material()
            if not base or base.get_path_name() in done:
                continue
            done.add(base.get_path_name())
            if not base.get_editor_property("used_with_instanced_static_meshes"):
                base.set_editor_property("used_with_instanced_static_meshes", True)
                unreal.MaterialEditingLibrary.recompile_material(base)
                eal.save_loaded_asset(base)
                log("KiteDemo material %s: instanced (set)" % base.get_name())
    log("KiteDemo: %d materials checked" % len(done))


# The Megascans plants' colours (OTGGrass.cpp's kinds), multiplied in by their material's own tint
# ("Tint Base Color" on, "Base Color Tint": 0.5 grey leaves a colour as it is). As they come, each
# plant is its own colour, so each kind's plants get one tint, pulling them to one colour (the rough's
# tufts are the game's own grass now, in the rough's colours: build_course.py's rough_clump):
#   the tall grass: heathland - fescue a muted heather brown with a little mauve, the wild and tundra
#   grass between its drifts an olive green (a bronze olive read as pale yellow clumps from afar)
FAB_TINTS = {
    (0.12, 0.085, 0.09): ("Tall_Grass_rbEkI", "Tall_Grass_rbEkP", "Tall_Grass_rbEla"),
    (0.075, 0.095, 0.05): ("Wild_Grass_vczndjqja", "Tundra_Grass_wk2oaeyqx", "Grass_smdrailr", "Grass_slBm1"),
}


def fab_tints():
    """Fab's content isn't in git, so the tints are set here each time: on each plant's material and
    its billboard's (the same colour far off)."""
    if not eal.does_directory_exist("/Game/Fab"):
        return
    mel = unreal.MaterialEditingLibrary
    for tint, plants in FAB_TINTS.items():
        for plant in plants:
            folder = "/Game/Fab/Megascans/Plants/" + plant
            if not eal.does_directory_exist(folder):
                unreal.log_warning("[OpenTeeGolf assets] No %s: not tinted" % folder)
                continue
            for path in eal.list_assets(folder, recursive=True, include_folder=False):
                if not path.split(".")[-1].startswith("MI_"):
                    continue
                mi = eal.load_asset(path)
                if not isinstance(mi, unreal.MaterialInstanceConstant):
                    continue
                try:
                    mel.set_material_instance_static_switch_parameter_value(mi, "Tint Base Color", True)
                    mel.set_material_instance_vector_parameter_value(mi, "Base Color Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 0.0))
                    # (and the light through the blades, the same way: untinted, a clump lit from
                    # behind still glowed its own yellow)
                    mel.set_material_instance_static_switch_parameter_value(mi, "Tint Translucency", True)
                    mel.set_material_instance_vector_parameter_value(mi, "Translucency Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 0.0))
                    mel.update_material_instance(mi)
                    eal.save_loaded_asset(mi)
                    log("Fab tint %s: %s" % (mi.get_name(), tint))
                except Exception as e:
                    unreal.log_warning("[OpenTeeGolf assets] %s not tinted: %s" % (path, e))


def grass_icons():
    """The grass biome studio's pictures (unreal/grass_icons/<kind>.png, from grass_icons.ps1): UI
    textures, /Game/OpenTeeGolf/Game/Runtime/GrassIcons/T_GrassIcon_<kind>, which the studio shows beside
    each kind's name (SOTGStudio::Picture). Small and uncompressed-looking: no mips, the UI's group."""
    src = os.path.join(os.environ.get("OTG_SCRIPT_DIR", os.path.dirname(os.path.abspath(__file__))), "grass_icons")
    if not os.path.isdir(src):
        return
    dest = FOLDER + "/Runtime/GrassIcons"
    # (made afresh: a picture dropped from grass_icons goes from the game too)
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    tasks = []
    for f in sorted(os.listdir(src)):
        if not f.lower().endswith(".png"):
            continue
        safe = "".join(c if c.isalnum() else "_" for c in f[:-4])
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, f)
        t.destination_path = dest
        t.destination_name = "T_GrassIcon_" + safe
        t.replace_existing = True
        t.automated = True
        t.save = False
        tasks.append(t)
    tools.import_asset_tasks(tasks)
    for t in tasks:
        path = dest + "/" + t.destination_name
        tex = eal.load_asset(path)
        if not tex:
            unreal.log_warning("[OpenTeeGolf assets] grass picture not made: %s" % path)
            continue
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("srgb", True)
        eal.save_loaded_asset(tex)
    log("grass pictures: %d" % len(tasks))


if not eal.does_directory_exist(FOLDER):
    eal.make_directory(FOLDER)
grass_icons()
fab_usage()
# (the Tidewright water's material uses CalculateLevelOfDetail in Custom HLSL, which ray-tracing
# hit shaders can't: with the game's ray tracing on, the whole material failed and the water drew as
# nothing. Guarded to level 0 there - OTGPackTools::GuardRayTracingLod; already done, it does nothing)
try:
    log("Tidewright ray-tracing guard: %d nodes" % unreal.OTGPackTools.guard_ray_tracing_lod("/Tidewright"))
except Exception as e:
    unreal.log_warning("[OpenTeeGolf assets] Tidewright ray-tracing guard: %s" % e)
fab_tints()
kite_usage()
rain()
# the clouds: make_sky.py
_sky = os.path.join(os.environ.get("OTG_SCRIPT_DIR", os.path.dirname(os.path.abspath(__file__))), "make_sky.py")
exec(compile(open(_sky, encoding="utf-8").read(), _sky, "exec"), {"__name__": "__main__", "__file__": _sky})
course_level()
scenery_chunks()
# the pieces exported courses are built from at run time: build_course.py in game-assets mode
os.environ["OTG_RUNTIME"] = "1"
os.environ["OTG_CONTENT"] = FOLDER + "/Runtime"
_build = os.path.join(os.environ.get("OTG_SCRIPT_DIR", os.path.dirname(os.path.abspath(__file__))), "build_course.py")
_bg = {"__name__": "__main__", "__file__": _build}
exec(compile(open(_build, encoding="utf-8").read(), _build, "exec"), _bg)
# the game's own turf, every mowing height (make_turf.py: in build_course.py's namespace, its helpers)
_turf = os.path.join(os.environ.get("OTG_SCRIPT_DIR", os.path.dirname(os.path.abspath(__file__))), "make_turf.py")
exec(compile(open(_turf, encoding="utf-8").read(), _turf, "exec"), _bg)
open(os.path.join(os.environ.get("OTG_SCRIPT_DIR", "."), "assets_done.txt"), "w").write("ok")
