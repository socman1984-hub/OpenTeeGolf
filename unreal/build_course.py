# Build an OpenTeeGolf course in Unreal Engine 5 from the game's export (a prototype).
# Run inside the Unreal Editor (Tools > Execute Python Script), with the "Python Editor Script
# Plugin" enabled. See README.md next to this file for the steps.
#
#   1. Run with EXPORT_DIR set: it prints the Landscape import settings for heightmap.png.
#   2. Import the landscape by hand with those settings (Landscape mode > Manage > Import from File).
#   3. Run again. It builds:
#        - the ground: the aerial photo for colour, the game's lawn, rough and sand textures for
#          close-up detail and relief, blended by the course's surface masks
#        - 3D grass on the rough (Unreal's landscape grass)
#        - the trees: the game's own tree models, each where the game plants it, turned and sized
#          the same (placeholder shapes if the models weren't exported)
#        - the water: every pond and stream surface at its level
#        - a marker on every tee and pin
#      Each part is reported in the Output Log; one failing doesn't stop the rest.
#
# Coordinates (from the export): X east, Y south, Z up, centimetres; the landscape's north-west
# corner at 0, 0.

import json
import os
import traceback
import unreal

# ---- set this to the course's export folder (OpenTeeGolf/exports/<course id>)
EXPORT_DIR = r"C:\Users\rartz\Desktop\Claude Projects\GolfSim\exports\Majestic_Springs_Golf_Club"

# Which parts to build on this run (False: leave what's in the level as it is). Building only what
# changed is much quicker: the ground's material takes the longest (Unreal compiles its shaders).
BUILD = {"ground": True, "trees": False, "water": False, "markers": False, "props": False}
# Imported pictures and models are reused on later runs; True imports them all again (after a new
# export of the course from the game)
REIMPORT = True

# The game's ground textures (CC0), next to the exports folder
GAME_TEXTURES = os.path.normpath(os.path.join(EXPORT_DIR, "..", "..", "public", "textures"))
# Their average colours (linear), as in the game (scene.js GROUND_MEAN): dividing by them keeps
# the photo's colour and adds only the textures' detail
GROUND_MEAN = {"lawn": (0.131, 0.23, 0.025), "rough": (0.118, 0.152, 0.03), "sand": (0.462, 0.342, 0.165)}
DETAIL_TILE_M = 2.5       # metres each ground texture covers
# The game's turf colours (linear; scene.js terrain shader): the photo gives only light and shade on
# the grass, the colour is the turf's own - deep, even greens rather than the photo's lime and grey
TURF = {"fairway": (0.06, 0.112, 0.022), "green": (0.074, 0.136, 0.026), "rough": (0.04, 0.072, 0.024), "bunker": (0.52, 0.41, 0.25),
        "water": (0.03, 0.03, 0.018)}   # (the pond floor under Unreal's water: dark silt, not the photo's pale water)
TURF_AMOUNT = 0.88        # how much of the turf colour over the photo's (the rest the photo's own)
TREE_CULL_CM = (90000, 130000)   # trees fade out between 900 and 1300 m
CONIFERS = {"pine", "spruce", "cypress", "baldcypress"}

# Unreal's own trees (from Fab), used in place of the game's broadleaf trees: every whole-tree mesh
# in these folders (the wind-animated ones). Each game tree gets the one closest to its height, so
# small trees aren't stretched giants; conifers (none in these packs) keep the game's models.
# Leave the list empty to use the game's models for every tree.
UE_BROADLEAF_FOLDERS = ["/Game/EuropeanBeech/Geometry/SimpleWind", "/Game/EuropeanHornbeam/Geometry/SimpleWind"]
UE_TREE_KINDS = ("_Forest_", "_Field_")      # (not the saplings and seedlings)
# Conifers: whole trees exported from the Megaplants' Procedural Vegetation Editor (its Export saves
# "Tree_..." meshes next to the graph, or put them in /Game/OTG_Conifers); the game's own models if
# there are none. (Only meshes named Tree_ or SM_: not the branch and twig parts beside them.)
UE_CONIFER_FOLDERS = ["/Game/OTG_Conifers", "/Game/Megaplant_Library/Tree_Norway_Spruce", "/Game/Megaplant_Library/Tree_Baltic_Pine"]
UE_CONIFER_NAMES = ("Tree_", "SM_")
# Grass for the rough: an existing LandscapeGrassType (the MW Landscape Auto Material's), or our own
# tuft if it isn't there
UE_GRASS_TYPE = "/Game/MWLandscapeAutoMaterial/Procedurals/LGT_MWAM_Grass"
# Golf rough, built from those grass meshes (MW's own grass type is meadow grass, 25-58 cm and
# wild): mown to ROUGH_HEIGHT_CM, thick and even. ROUGH_DENSITY: tufts per 10 x 10 m; ROUGH_DRAW_M:
# how far out it's drawn (beyond, the ground's colour carries the rough).
ROUGH_MESHES = ["/Game/MWLandscapeAutoMaterial/Meshes/Plants/SM_MWAM_GrassD", "/Game/MWLandscapeAutoMaterial/Meshes/Plants/SM_MWAM_GrassC"]
ROUGH_HEIGHT_CM = (6, 11)
ROUGH_DENSITY = 9000
ROUGH_DRAW_M = (25, 40)


def load(name):
    with open(os.path.join(EXPORT_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def exists(name):
    return os.path.exists(os.path.join(EXPORT_DIR, name))


meta = load("course.json")
CONTENT = "/Game/OpenTeeGolf/" + meta["id"]
assets = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[OpenTeeGolf] " + msg)


def warn(msg):
    unreal.log_warning("[OpenTeeGolf] " + msg)


def step(name, fn):
    """Run one part of the build; report and carry on if it fails."""
    try:
        fn()
    except Exception:
        warn("%s failed:\n%s" % (name, traceback.format_exc()))


# ------------------------------------------------------------------ the landscape

def print_landscape_settings():
    L = meta["landscape"]
    log("Course: %s - %d x %d samples, %g m between samples (%.0f m across)" % (
        meta["name"], meta["samples"], meta["samples"], meta["metresPerSample"], meta["sizeMetres"]))
    log("Landscape mode > Manage > New > Import from File: %s" % os.path.join(EXPORT_DIR, "heightmap.png"))
    log("  Location: X 0, Y 0, Z %s" % L["locationZ"])
    log("  Scale:    X %s, Y %s, Z %s" % (L["scaleXY"], L["scaleXY"], L["scaleZ"]))
    log("  Resolution: %d x %d (keep the size the file has)" % (meta["samples"], meta["samples"]))


def find_landscape():
    """The landscape (preferring the Landscape itself over its streaming proxies)."""
    proxies = []
    for a in actors.get_all_level_actors():
        if isinstance(a, unreal.Landscape):
            return a
        if isinstance(a, unreal.LandscapeProxy):
            proxies.append(a)
    return proxies[0] if proxies else None


# Where the landscape really is. Unreal centres a new landscape on the Location typed in, so its
# north-west corner (the export's 0, 0) is wherever the actor ended up; the export's heights assume
# the actor at Z = landscape.locationZ. Everything placed is shifted by this.
OFFSET = unreal.Vector(0, 0, 0)


def measure_offset(land):
    global OFFSET
    loc = land.get_actor_location()
    OFFSET = unreal.Vector(loc.x, loc.y, loc.z - meta["landscape"]["locationZ"])
    log("Landscape corner at %.0f, %.0f (Z offset %.0f cm): placing everything from there" % (OFFSET.x, OFFSET.y, OFFSET.z))


def at(x, y, z):
    return unreal.Vector(x + OFFSET.x, y + OFFSET.y, z + OFFSET.z)


# ------------------------------------------------------------------ importing

def import_file(path, folder, name=None):
    """Import a file into folder; returns the assets now in that folder. (A model already imported
    into its own folder is reused unless REIMPORT.)"""
    if not name and not REIMPORT and unreal.EditorAssetLibrary.does_directory_exist(folder):
        have = [unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)]
        if any(isinstance(a, unreal.StaticMesh) for a in have):
            return have
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    if name:
        task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    return [unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)]


def import_texture(path, name, kind="color"):
    """kind: color (sRGB), mask (linear greyscale) or normal"""
    if not REIMPORT and unreal.EditorAssetLibrary.does_asset_exist(CONTENT + "/Textures/" + name):
        return unreal.load_asset(CONTENT + "/Textures/" + name)
    import_file(path, CONTENT + "/Textures", name)
    t = unreal.load_asset(CONTENT + "/Textures/" + name)
    if t and kind != "color":
        t.set_editor_property("srgb", False)
        t.set_editor_property("compression_settings",
                              unreal.TextureCompressionSettings.TC_GRAYSCALE if kind == "mask" else unreal.TextureCompressionSettings.TC_NORMALMAP)
        if kind == "mask":
            # (always at full detail: streamed down to a smaller copy, the course's edges go blocky)
            for prop, val in (("never_stream", True), ("lod_group", unreal.TextureGroup.TEXTUREGROUP_TERRAIN_WEIGHTMAP)):
                try:
                    t.set_editor_property(prop, val)
                except Exception:
                    pass
        unreal.EditorAssetLibrary.save_loaded_asset(t)
    return t


def new_material(name):
    path = CONTENT + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return assets.create_asset(name, CONTENT, unreal.Material, unreal.MaterialFactoryNew())


def static_meshes(found):
    return [a for a in found if isinstance(a, unreal.StaticMesh)]


def mesh_box(meshes):
    """Combined bounding box (min, max Vectors, centimetres) of meshes as imported."""
    lo, hi = [1e18] * 3, [-1e18] * 3
    for m in meshes:
        b = m.get_bounding_box()
        for i, k in enumerate("xyz"):
            lo[i] = min(lo[i], getattr(b.min, k))
            hi[i] = max(hi[i], getattr(b.max, k))
    return lo, hi


# ------------------------------------------------------------------ the ground

def build_ground(land):
    """Landscape material: the photo for colour; lawn, rough and sand textures for detail and
    relief up close, picked by the surface masks; 3D grass on the rough."""
    N = meta["samples"]
    photo = import_texture(os.path.join(EXPORT_DIR, "photo.jpg"), "T_CoursePhoto")
    masks = {k: import_texture(os.path.join(EXPORT_DIR, "layer_%s.png" % k), "T_Mask_" + k, "mask")
             for k in ("fairway", "green", "tee", "bunker", "water", "rough")}
    tex = {}
    for k in ("lawn", "rough", "sand"):
        tex[k] = import_texture(os.path.join(GAME_TEXTURES, k + "_color.jpg"), "T_%s_color" % k)
        tex[k + "_n"] = import_texture(os.path.join(GAME_TEXTURES, k + "_normal.jpg"), "T_%s_normal" % k, "normal")

    mat = new_material("M_CourseGround")
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)

    def coords(scale, y):
        c = E(unreal.MaterialExpressionLandscapeLayerCoords, -2200, y)
        c.set_editor_property("mapping_scale", float(scale))
        return c

    def sample(t, uv, x, y, sampler):
        s = E(unreal.MaterialExpressionTextureSample, x, y)
        s.set_editor_property("texture", t)
        s.set_editor_property("sampler_type", sampler)
        mel.connect_material_expressions(uv, "", s, "UVs")
        return s

    def op(cls, a, b, x, y, a_out="", b_out=""):
        n = E(cls, x, y)
        mel.connect_material_expressions(a, a_out, n, "A")
        mel.connect_material_expressions(b, b_out, n, "B")
        return n

    def const3(rgb, x, y):
        c = E(unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
        return c

    def const(v, x, y):
        c = E(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", v)
        return c

    S = unreal.MaterialSamplerType
    big = coords(N - 1, 0)                                      # 0..1 over the whole landscape
    fine = coords(DETAIL_TILE_M / meta["metresPerSample"], 900)  # one texture every few metres
    ph = sample(photo, big, -1800, -400, S.SAMPLERTYPE_COLOR)
    m = {k: sample(t, big, -2000, 200 + i * 220, S.SAMPLERTYPE_LINEAR_GRAYSCALE) for i, (k, t) in enumerate(masks.items())}
    # Crisp, smooth edges: the masks are distance fields (0.5 on each outline, 3 m either side
    # spread over 0..1), so a narrow step round 0.5 - about 30 cm wide - follows the true outline
    for i, k in enumerate(list(m)):
        ss = E(unreal.MaterialExpressionSmoothStep, -1800, 200 + i * 220)
        mel.connect_material_expressions(const(0.475, -1950, 150 + i * 220), "", ss, "Min")
        mel.connect_material_expressions(const(0.525, -1950, 250 + i * 220), "", ss, "Max")
        mel.connect_material_expressions(m[k], "R", ss, "Value")
        m[k] = ss
    mown = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionAdd, m["fairway"], m["green"], -1500, 300, "", ""), m["tee"], -1300, 300, "", "")
    # detail: each texture over its own average colour, weighted by its surface
    detail = None
    normal = None
    for i, (k, w, wout) in enumerate((("lawn", mown, ""), ("rough", m["rough"], ""), ("sand", m["bunker"], ""))):
        c = sample(tex[k], fine, -1800, 1600 + i * 500, S.SAMPLERTYPE_COLOR)
        n = sample(tex[k + "_n"], fine, -1800, 1850 + i * 500, S.SAMPLERTYPE_NORMAL)
        r = op(unreal.MaterialExpressionDivide, c, const3(GROUND_MEAN[k], -1600, 1700 + i * 500), -1400, 1600 + i * 500, "RGB")
        dk = op(unreal.MaterialExpressionMultiply, r, w, -1200, 1600 + i * 500, "", wout)
        nk = op(unreal.MaterialExpressionMultiply, n, w, -1200, 1850 + i * 500, "RGB", wout)
        detail = dk if detail is None else op(unreal.MaterialExpressionAdd, detail, dk, -1000, 1600 + i * 500)
        normal = nk if normal is None else op(unreal.MaterialExpressionAdd, normal, nk, -1000, 1850 + i * 500)
    # (water: no detail, flat)
    detail = op(unreal.MaterialExpressionAdd, detail, m["water"], -900, 2700, "", "")
    normal = op(unreal.MaterialExpressionAdd, normal, op(unreal.MaterialExpressionMultiply, const3((0, 0, 1), -1200, 3000), m["water"], -1100, 3000, "", ""), -900, 2950)
    mix = E(unreal.MaterialExpressionLinearInterpolate, -700, 1000)
    mel.connect_material_expressions(const(1.0, -900, 950), "", mix, "A")
    mel.connect_material_expressions(detail, "", mix, "B")
    mel.connect_material_expressions(const(0.85, -900, 1150), "", mix, "Alpha")
    # Turf colour: each surface's own, lit by the photo's light and shade (its brightness over 0.3,
    # kept within 0.65..1.35), over the photo's colour except on water
    lum = E(unreal.MaterialExpressionDotProduct, -1500, -600)
    mel.connect_material_expressions(ph, "RGB", lum, "A")
    mel.connect_material_expressions(const3((0.2126, 0.7152, 0.0722), -1700, -550), "", lum, "B")
    # (between 0.65 and 1.35: a Max then a Min, whose inputs are plain A and B)
    shade = op(unreal.MaterialExpressionMin,
               op(unreal.MaterialExpressionMax, op(unreal.MaterialExpressionDivide, lum, const(0.3, -1500, -450), -1300, -600), const(0.65, -1300, -450), -1200, -600),
               const(1.35, -1200, -450), -1100, -600)
    fair_tee = op(unreal.MaterialExpressionAdd, m["fairway"], m["tee"], -1500, -300, "", "")
    turf = None
    for i, (k, w, wout) in enumerate((("fairway", fair_tee, ""), ("green", m["green"], ""), ("rough", m["rough"], ""), ("bunker", m["bunker"], ""), ("water", m["water"], ""))):
        t = op(unreal.MaterialExpressionMultiply, const3(TURF[k], -1300, -300 + i * 120), w, -1100, -300 + i * 120, "", wout)
        turf = t if turf is None else op(unreal.MaterialExpressionAdd, turf, t, -900, -300 + i * 120)
    turf = op(unreal.MaterialExpressionMultiply, turf, shade, -800, -400)
    amount = const(TURF_AMOUNT, -800, -100)
    graded = E(unreal.MaterialExpressionLinearInterpolate, -600, -300)
    mel.connect_material_expressions(ph, "RGB", graded, "A")
    mel.connect_material_expressions(turf, "", graded, "B")
    mel.connect_material_expressions(amount, "", graded, "Alpha")
    base = op(unreal.MaterialExpressionMultiply, graded, mix, -400, 0)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(const(0.88, -400, 400), "", unreal.MaterialProperty.MP_ROUGHNESS)

    # 3D grass on the rough
    try:
        gt = make_grass_type()
        if gt:
            go = E(unreal.MaterialExpressionLandscapeGrassOutput, -400, 700)
            gi = unreal.GrassInput()
            gi.set_editor_property("name", "Rough")
            gi.set_editor_property("grass_type", gt)
            go.set_editor_property("grass_types", [gi])
            mel.connect_material_expressions(m["rough"], "", go, "Rough")
            log("Grass on the rough: set up")
    except Exception:
        warn("Grass skipped:\n" + traceback.format_exc())

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    land.set_editor_property("landscape_material", mat)
    try:
        n = mel.get_num_material_expressions(mat)
        log("Ground material: %d nodes" % n)
    except Exception:
        pass
    log("Ground material: turf colours, the photo's light and shade, detail textures and masks")


def grass_material():
    """The rough's grass blades: the rough's green, both sides, little shine."""
    mat = new_material("M_Grass")
    mat.set_editor_property("two_sided", True)
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)
    col = E(unreal.MaterialExpressionConstant3Vector, -400, 0)
    col.set_editor_property("constant", unreal.LinearColor(0.055, 0.1, 0.03, 1))
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for prop, v, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.85, 200), (unreal.MaterialProperty.MP_SPECULAR, 0.15, 300)):
        c = E(unreal.MaterialExpressionConstant, -400, y)
        c.set_editor_property("r", v)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def make_rough_type():
    """A grass type for mown golf rough from ROUGH_MESHES (see there), or None without them."""
    meshes = [unreal.load_asset(p) for p in ROUGH_MESHES if unreal.EditorAssetLibrary.does_asset_exist(p)]
    meshes = [m for m in meshes if isinstance(m, unreal.StaticMesh)]
    if not meshes:
        return None
    path = CONTENT + "/GT_MownRough"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    gt = assets.create_asset("GT_MownRough", CONTENT, unreal.LandscapeGrassType, unreal.LandscapeGrassTypeFactory())
    varieties = []
    for m in meshes:
        b = m.get_bounding_box()
        h = max(1.0, b.max.z - b.min.z)
        v = unreal.GrassVariety()
        v.set_editor_property("grass_mesh", m)
        for prop, val in (("grass_density", unreal.PerPlatformFloat(default=float(ROUGH_DENSITY) / len(meshes))),
                          ("start_cull_distance", unreal.PerPlatformInt(default=int(ROUGH_DRAW_M[0] * 100))),
                          ("end_cull_distance", unreal.PerPlatformInt(default=int(ROUGH_DRAW_M[1] * 100))),
                          ("random_rotation", True), ("align_to_surface", True), ("use_grid", True),
                          ("placement_jitter", 1.0), ("scaling", unreal.GrassScaling.UNIFORM),
                          ("scale_x", unreal.FloatInterval(ROUGH_HEIGHT_CM[0] / h, ROUGH_HEIGHT_CM[1] / h)),
                          ("cast_dynamic_shadow", False)):
            try:
                v.set_editor_property(prop, val)
            except Exception:
                warn("Rough setting %s not set" % prop)
        varieties.append(v)
    gt.set_editor_property("grass_varieties", varieties)
    unreal.EditorAssetLibrary.save_loaded_asset(gt)
    log("Grass: mown rough, %d-%d cm, %d tufts per 100 m2, drawn to %d m" % (ROUGH_HEIGHT_CM[0], ROUGH_HEIGHT_CM[1], ROUGH_DENSITY, ROUGH_DRAW_M[1]))
    return gt


def make_grass_type():
    try:
        gt = make_rough_type()
        if gt:
            return gt
    except Exception:
        warn("Mown rough not made, using the grass type instead: " + traceback.format_exc())
    if UE_GRASS_TYPE and unreal.EditorAssetLibrary.does_asset_exist(UE_GRASS_TYPE):
        log("Grass: " + UE_GRASS_TYPE)
        return unreal.load_asset(UE_GRASS_TYPE)
    if not exists("grass_tuft.glb"):
        return None
    found = static_meshes(import_file(os.path.join(EXPORT_DIR, "grass_tuft.glb"), CONTENT + "/Grass"))
    if not found:
        warn("Grass tuft didn't import")
        return None
    tuft = found[0]
    try:
        tuft.set_material(0, grass_material())
    except Exception:
        warn("Grass material not set:\n" + traceback.format_exc())
    path = CONTENT + "/GT_Rough"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    gt = assets.create_asset("GT_Rough", CONTENT, unreal.LandscapeGrassType, unreal.LandscapeGrassTypeFactory())
    v = unreal.GrassVariety()
    v.set_editor_property("grass_mesh", tuft)
    for prop, val in (("grass_density", unreal.PerPlatformFloat(default=1500.0)),
                      ("start_cull_distance", unreal.PerPlatformInt(default=3000)),
                      ("end_cull_distance", unreal.PerPlatformInt(default=6000)),
                      ("random_rotation", True), ("align_to_surface", True), ("use_grid", False),
                      ("scale_x", unreal.FloatInterval(0.8, 1.6))):
        try:
            v.set_editor_property(prop, val)
        except Exception:
            warn("Grass setting %s not set" % prop)
    gt.set_editor_property("grass_varieties", [v])
    unreal.EditorAssetLibrary.save_loaded_asset(gt)
    return gt


# ------------------------------------------------------------------ instanced meshes

def new_actor(label):
    for old in actors.get_all_level_actors():
        if old.get_actor_label() == label:
            actors.destroy_actor(old)
    a = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    a.set_actor_label(label)
    return a


def add_instances(actor, mesh, transforms, label, cull=None):
    """A HierarchicalInstancedStaticMeshComponent on actor holding all the transforms."""
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_instance(actor)[0]
    params = unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent, blueprint_context=None)
    handle, fail = sds.add_new_subobject(params)
    if not fail.is_empty():
        raise RuntimeError("Could not add %s: %s" % (label, fail))
    sds.rename_subobject(handle, unreal.Text(label))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
    comp.set_static_mesh(mesh)
    comp.set_editor_property("cast_shadow", True)
    if cull:
        comp.set_editor_property("instance_start_cull_distance", cull[0])
        comp.set_editor_property("instance_end_cull_distance", cull[1])
    comp.add_instances(transforms, False, True)
    return comp


def xf(loc, yaw, sx, sy, sz):
    return unreal.Transform(location=loc, rotation=unreal.Rotator(0, 0, yaw), scale=unreal.Vector(sx, sy, sz))


# ------------------------------------------------------------------ trees

def add_lods(mesh):
    """Lower-detail versions for distance (the game's models are detailed up close)."""
    try:
        sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        opts = unreal.StaticMeshReductionOptions()
        opts.set_editor_property("auto_compute_lod_screen_size", False)
        opts.set_editor_property("reduction_settings", [
            unreal.StaticMeshReductionSettings(1.0, 1.0),
            unreal.StaticMeshReductionSettings(0.4, 0.35),
            unreal.StaticMeshReductionSettings(0.15, 0.15),
            unreal.StaticMeshReductionSettings(0.05, 0.05),
        ])
        sms.set_lods(mesh, opts)
    except Exception:
        warn("LODs not made for %s: %s" % (mesh.get_name(), traceback.format_exc().splitlines()[-1]))


def tree_master(name, leaves):
    """Leaves: a picture parameter cut out by its alpha, both sides, barely any shine (a shiny crown
    reads pale blue-white). Bark: a picture and its relief."""
    mat = new_material(name)
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)
    S = unreal.MaterialSamplerType
    if leaves:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("opacity_mask_clip_value", 0.4)
        t = E(unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
        t.set_editor_property("parameter_name", "Leaves")
        t.set_editor_property("sampler_type", S.SAMPLERTYPE_COLOR)
        t.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
        mel.connect_material_property(t, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(t, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        shine = (0.85, 0.1)
    else:
        t = E(unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
        t.set_editor_property("parameter_name", "Bark")
        t.set_editor_property("sampler_type", S.SAMPLERTYPE_COLOR)
        t.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
        mel.connect_material_property(t, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        n = E(unreal.MaterialExpressionTextureSampleParameter2D, -500, 300)
        n.set_editor_property("parameter_name", "BarkNormal")
        n.set_editor_property("sampler_type", S.SAMPLERTYPE_NORMAL)
        n.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
        mel.connect_material_property(n, "RGB", unreal.MaterialProperty.MP_NORMAL)
        shine = (0.95, 0.2)
    for prop, v, y in ((unreal.MaterialProperty.MP_ROUGHNESS, shine[0], 600), (unreal.MaterialProperty.MP_SPECULAR, shine[1], 700)):
        c = E(unreal.MaterialExpressionConstant, -400, y)
        c.set_editor_property("r", v)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def tree_instance(name, folder, master, textures):
    """A material instance of master with its picture parameters set (textures: {param: Texture})."""
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mi = assets.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for param, t in textures.items():
        if t:
            mel.set_material_instance_texture_parameter_value(mi, param, t)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def dress_tree(meshes, md, folder, masters):
    """The tree's bark and leaves get our materials (the exported pictures, the game's colours)."""
    def pic(key, kind="color"):
        f = md.get(key)
        return import_texture(os.path.join(EXPORT_DIR, f), os.path.splitext(f)[0], kind) if f else None
    leaf_mi = tree_instance("MI_Leaves_%d" % md["variant"], folder, masters[0], {"Leaves": pic("leaves")}) if md.get("leaves") else None
    bark_mi = tree_instance("MI_Bark_%d" % md["variant"], folder, masters[1], {"Bark": pic("bark"), "BarkNormal": pic("barkNormal", "normal")}) if md.get("bark") else None
    for mesh in meshes:
        slots = mesh.get_editor_property("static_materials")
        for i, sm in enumerate(slots):
            label = (str(sm.get_editor_property("material_slot_name")) + " " + mesh.get_name()).lower()
            mi = leaf_mi if "lea" in label else bark_mi if "bark" in label else None
            if mi:
                mesh.set_material(i, mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)


def ue_broadleaf(folders=None, kinds=UE_TREE_KINDS):
    """Unreal's whole trees in folders: [(mesh, height cm)], shortest first."""
    out = []
    for folder in folders or UE_BROADLEAF_FOLDERS:
        if not unreal.EditorAssetLibrary.does_directory_exist(folder):
            continue
        for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
            name = path.split("/")[-1]
            if kinds and not any(k in name for k in kinds):
                continue
            m = unreal.load_asset(path.split(".")[0])
            if isinstance(m, unreal.StaticMesh):
                b = m.get_bounding_box()
                out.append((m, b.max.z - b.min.z))
    return sorted(out, key=lambda e: e[1])


def hash01(x, y, k):
    import math
    v = math.sin(x * 12.9898 * k + y * 78.233) * 43758.5453
    return v - math.floor(v)


def plant_ue_broadleaf(actor, trees, choices):
    """Each tree: one of the three meshes nearest its height (picked by its position, so the woods
    mix), scaled to the game's height within reason, turned at random."""
    by_mesh = {}
    for t in trees:
        h = t["h"]
        near = sorted(choices, key=lambda e: abs(e[1] - h))[:3]
        mesh, mh = near[int(hash01(t["x"], t["y"], 3.1) * len(near)) % len(near)]
        s = max(0.7, min(1.4, h / max(1.0, mh))) * (0.92 + 0.16 * hash01(t["x"], t["y"], 5.3))
        yaw = hash01(t["x"], t["y"], 2.9) * 360
        by_mesh.setdefault(mesh, []).append(xf(at(t["x"], t["y"], t["z"] - 10), yaw, s, s, s))
    for mesh, xs in by_mesh.items():
        add_instances(actor, mesh, xs, mesh.get_name(), TREE_CULL_CM)
        log("Trees: %s x %d" % (mesh.get_name(), len(xs)))
    return sum(len(v) for v in by_mesh.values())


def plant_trees():
    trees = load("trees.json")
    models = load("tree_models.json") if exists("tree_models.json") else []
    if not models:
        plant_placeholders(trees)
        return
    actor = new_actor("OTG_Trees")
    planted = 0
    # Unreal's broadleaf trees where there are any; the game's models for the rest
    ue = ue_broadleaf()
    if ue:
        log("Using %d Unreal tree models for broadleaf trees" % len(ue))
        broad = [t for t in trees if t.get("kind") not in CONIFERS]
        planted += plant_ue_broadleaf(actor, broad, ue)
        trees = [t for t in trees if t.get("kind") in CONIFERS]
    cones = [c for c in ue_broadleaf(UE_CONIFER_FOLDERS, None) if c[0].get_name().startswith(UE_CONIFER_NAMES) and c[1] > 300]
    if cones and trees:
        log("Using %d Unreal conifer models" % len(cones))
        # (pines get the pine models and the rest the spruces, where there are both)
        pines = [c for c in cones if "pine" in c[0].get_name().lower()]
        spruces = [c for c in cones if "pine" not in c[0].get_name().lower()]
        for want, pool in (("pine", pines or spruces), (None, spruces or pines)):
            group = [t for t in trees if (t.get("kind") == "pine") == (want == "pine")]
            if group:
                planted += plant_ue_broadleaf(actor, group, pool)
        trees = []
    masters = (tree_master("M_TreeLeaves", True), tree_master("M_TreeBark", False))
    by_variant = {}
    for t in trees:
        by_variant.setdefault(t.get("variant"), []).append(t)
    for md in models:
        vi, group = md["variant"], by_variant.get(md["variant"], [])
        if not group:
            continue
        meshes = static_meshes(import_file(os.path.join(EXPORT_DIR, md["file"]), "%s/Trees/tree_%d" % (CONTENT, vi)))
        if not meshes:
            warn("Tree model %s didn't import" % md["file"])
            continue
        # (the importer's units: scale so the model is as tall as the game's, in centimetres)
        lo, hi = mesh_box(meshes)
        f = (md["height"] * 100.0) / max(1e-3, hi[2] - lo[2])
        try:
            dress_tree(meshes, md, "%s/Trees/tree_%d" % (CONTENT, vi), masters)
        except Exception:
            warn("Tree %d materials: %s" % (vi, traceback.format_exc()))
        for mesh in meshes:
            add_lods(mesh)
        xs = [xf(at(t["x"], t["y"], t["z"] - 15), t.get("yaw", 0), t["sx"] * f, t["sy"] * f, t["sz"] * f) for t in group]
        for k, mesh in enumerate(meshes):
            add_instances(actor, mesh, xs, "%s_%d_%d" % (md["kind"], vi, k), TREE_CULL_CM)
        planted += len(group)
        log("Trees: %s (model %d) x %d" % (md["kind"], vi, len(group)))
    log("Planted %d trees from %d models" % (planted, len(models)))


def plant_placeholders(trees):
    """Placeholders sized like the game's trees: conifers a cone, broadleaf trees a trunk and a
    round crown."""
    cone = unreal.load_asset("/Engine/BasicShapes/Cone")
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
    cyl = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    con, crowns, trunks = [], [], []
    for t in trees:
        h, r = max(300, t["h"]), max(100, t["r"])
        if t.get("kind") in CONIFERS:
            con.append(xf(at(t["x"], t["y"], t["z"] + h * 0.55), 0, r * 2 / 100, r * 2 / 100, h * 0.9 / 100))
            trunks.append(xf(at(t["x"], t["y"], t["z"] + h * 0.1), 0, 0.25, 0.25, h * 0.2 / 100))
        else:
            crowns.append(xf(at(t["x"], t["y"], t["z"] + h * 0.62), 0, r * 2 / 100, r * 2 / 100, h * 0.6 / 100))
            trunks.append(xf(at(t["x"], t["y"], t["z"] + h * 0.2), 0, 0.3, 0.3, h * 0.4 / 100))
    actor = new_actor("OTG_Trees")
    for mesh, xs, label in ((cone, con, "Conifers"), (sphere, crowns, "Crowns"), (cyl, trunks, "Trunks")):
        if xs:
            add_instances(actor, mesh, xs, label)
    log("Planted %d placeholder trees (no tree models in the export)" % len(trees))


# ------------------------------------------------------------------ water

# Water: the game's own water surfaces (exact shapes and levels) with Unreal's Single Layer Water
# shading - absorption with depth, scattering, refraction, reflections, as the Water plugin draws
# water - rather than the Water plugin's lake and river bodies (True), whose water zone needs more
# setting up than a script can do reliably.
USE_WATER_BODIES = False
# How the water takes up light per metre (red, green, blue): red goes first, so deep water turns
# blue-green; and how much it scatters back (a little, green-blue: a clean pond, not milky)
WATER_ABSORPTION = (0.45, 0.075, 0.11)
WATER_SCATTERING = (0.012, 0.03, 0.028)
WATER_RIPPLES = 0.3   # 0 a mirror .. 1 the full ripple picture


def build_water():
    """The course's ponds and streams (see USE_WATER_BODIES)."""
    if USE_WATER_BODIES and exists("water_bodies.json"):
        try:
            if build_water_bodies():
                return
        except Exception:
            warn("Unreal water bodies failed, using the water mesh instead:\n" + traceback.format_exc())
    build_water_mesh()


def first_ok(obj, names, value):
    """Set the first of several property names this Unreal version has."""
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return n
        except Exception:
            pass
    return None


def build_water_bodies():
    bodies = load("water_bodies.json")
    if not bodies:
        log("No water on this course")
        return True
    if not hasattr(unreal, "WaterBodyLake"):
        warn("The Water plugin isn't enabled (Edit > Plugins > Water)")
        return False
    for old in actors.get_all_level_actors():
        if old.get_actor_label().startswith(("OTG_Lake", "OTG_River", "OTG_WaterZone", "OTG_Water")):
            actors.destroy_actor(old)
    # One water zone over the whole landscape (Unreal draws water bodies through a zone)
    size = meta["sizeMetres"] * 100
    zone = actors.spawn_actor_from_class(unreal.WaterZone, at(size / 2, size / 2, 0))
    zone.set_actor_label("OTG_WaterZone")
    first_ok(zone, ("zone_extent",), unreal.Vector2D(size * 1.05, size * 1.05))
    lakes = rivers = 0
    for i, b in enumerate(bodies):
        pts = b["points"]
        river = b["type"] == "river"
        cls = unreal.WaterBodyRiver if river else unreal.WaterBodyLake
        first = pts[0]
        a = actors.spawn_actor_from_class(cls, at(first["x"], first["y"], first["z"]))
        a.set_actor_label("OTG_%s_%d" % ("River" if river else "Lake", i))
        # the game's terrain already has the water carved in: the water mustn't reshape it
        comp = None
        try:
            comp = a.get_water_body_component()
        except Exception:
            pass
        for target in filter(None, (comp, a)):
            first_ok(target, ("affects_landscape", "b_affects_landscape"), False)
        spline = a.get_water_spline()
        if river:
            avg_w = sum(q["w"] for q in pts) / len(pts)
            try:
                d = spline.get_editor_property("water_spline_defaults")
                first_ok(d, ("default_width",), float(avg_w))
                first_ok(d, ("default_depth",), 150.0)
                spline.set_editor_property("water_spline_defaults", d)
            except Exception:
                warn("River width not set: " + traceback.format_exc().splitlines()[-1])
        spline.clear_spline_points(False)
        for q in pts:
            spline.add_spline_point(at(q["x"], q["y"], q["z"]), unreal.SplineCoordinateSpace.WORLD, False)
        if not river:
            spline.set_closed_loop(True, False)
        spline.update_spline()
        if river:
            set_river_widths(a, pts)
            rivers += 1
        else:
            lakes += 1
        # (nudge the actor so the editor rebuilds the water's shape and mesh)
        a.set_actor_location(a.get_actor_location(), False, False)
    # The Water plugin adds a "water brush" that carves the landscape round every water body
    # (flattening the banks to the water's level, which also makes every pond look a few centimetres
    # deep). The game's terrain already has the water carved in, so the brush goes: the landscape's
    # own heights come back, and the water its real depth.
    removed = 0
    for x in actors.get_all_level_actors():
        if "WaterBrushManager" in x.get_class().get_name() or "WaterBrushManager" in x.get_actor_label():
            actors.destroy_actor(x)
            removed += 1
    # (and the landscape edit layer it carved into, named "Water"; the base layer is the terrain)
    try:
        land = find_landscape()
        for name in ("Water",):
            if land and hasattr(land, "delete_layer"):
                land.delete_layer(name)
                removed += 1
    except Exception:
        pass
    if removed:
        log("Water: removed the water brush, so the landscape keeps its own heights")
    log("Water: %d lakes and %d rivers (Unreal water bodies)" % (lakes, rivers))
    return True


def set_river_widths(actor, pts):
    """Each river point its own width (the river's metadata); left at the average if not possible."""
    try:
        md = actor.get_editor_property("water_spline_metadata")
        curve = md.get_editor_property("river_width")
        cps = curve.get_editor_property("points")
        for k, q in enumerate(pts[:len(cps)]):
            cp = cps[k]
            cp.set_editor_property("out_val", float(q["w"]))
            cps[k] = cp
        curve.set_editor_property("points", cps)
        md.set_editor_property("river_width", curve)
    except Exception:
        pass


def build_water_mesh():
    # (anything an earlier run made with the Water plugin goes: its lakes, rivers, zone and brush)
    for old in actors.get_all_level_actors():
        lab, cls = old.get_actor_label(), old.get_class().get_name()
        if lab.startswith(("OTG_Lake", "OTG_River", "OTG_WaterZone")) or "WaterBrushManager" in cls or cls in ("WaterZone",):
            actors.destroy_actor(old)
    if not exists("water.glb"):
        log("No water on this course")
        return
    info = load("water.json")
    meshes = static_meshes(import_file(os.path.join(EXPORT_DIR, "water.glb"), CONTENT + "/Water"))
    if not meshes:
        warn("Water didn't import")
        return
    mesh = meshes[0]
    lo, hi = mesh_box([mesh])
    # Line the import up with where the water should be: try each quarter turn, mirrored or not,
    # with the scale from the sizes, and keep the one whose bounds match best
    want_lo = (info["min"]["x"], info["min"]["y"], info["min"]["z"])
    want_hi = (info["max"]["x"], info["max"]["y"], info["max"]["z"])
    best = None
    import math
    for mirror in (1, -1):
        for yaw in (0, 90, 180, 270):
            c, s = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
            corners = []
            for x in (lo[0], hi[0]):
                for y in (lo[1], hi[1]):
                    mx = x * mirror
                    corners.append((mx * c - y * s, mx * s + y * c))
            ex = max(p[0] for p in corners) - min(p[0] for p in corners)
            ey = max(p[1] for p in corners) - min(p[1] for p in corners)
            scale = ((want_hi[0] - want_lo[0]) / max(1e-3, ex) + (want_hi[1] - want_lo[1]) / max(1e-3, ey)) / 2
            err = abs(ex * scale - (want_hi[0] - want_lo[0])) + abs(ey * scale - (want_hi[1] - want_lo[1]))
            if best is None or err < best[0]:
                best = (err, mirror, yaw, scale, min(p[0] for p in corners), min(p[1] for p in corners))
    err, mirror, yaw, scale, minx, miny = best
    loc = at(want_lo[0] - minx * scale, want_lo[1] - miny * scale, want_lo[2] - lo[2] * scale + 3)
    for old in actors.get_all_level_actors():
        if old.get_actor_label() == "OTG_Water":
            actors.destroy_actor(old)
    a = actors.spawn_actor_from_object(mesh, loc)
    a.set_actor_label("OTG_Water")
    a.set_actor_rotation(unreal.Rotator(0, 0, yaw), False)
    a.set_actor_scale3d(unreal.Vector(scale * mirror, scale, scale))
    try:
        a.static_mesh_component.set_material(0, water_material())
    except Exception:
        warn("Water material not set:\n" + traceback.format_exc())
    log("Water: %d triangles (lined up: turn %d, %s, scale %.3g, fit error %.0f cm)" % (
        info["triangles"], yaw, "mirrored" if mirror < 0 else "not mirrored", scale, err))


def water_material():
    """Single Layer Water: the shading Unreal's own water uses - the water takes up light with depth
    (WATER_ABSORPTION) and scatters a little back (WATER_SCATTERING), refracts what's beneath and
    reflects the sky. Two layers of ripples drift different ways, so the pattern doesn't repeat."""
    mat = new_material("M_Water")
    first_ok(mat, ("shading_model",), unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    # (both sides: where a stream runs down a slope its triangles can face the other way)
    mat.set_editor_property("two_sided", True)
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)

    def const(v, x, y):
        c = E(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", v)
        return c

    def const3(rgb, x, y):
        c = E(unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
        return c

    mel.connect_material_property(const3((0.0, 0.0, 0.0), -400, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(const(0.02, -400, 120), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(const(1.0, -400, 200), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(const(0.0, -400, 280), "", unreal.MaterialProperty.MP_METALLIC)
    # the water itself
    out = E(unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -200, 500)

    def pin(node, names):
        for n in names:
            if mel.connect_material_expressions(node, "", out, n):
                return True
        warn("Water: no '%s' input on the water output" % names[0])
        return False

    pin(const3(WATER_SCATTERING, -500, 450), ("Scattering Coefficients", "ScatteringCoefficients"))
    pin(const3(WATER_ABSORPTION, -500, 550), ("Absorption Coefficients", "AbsorptionCoefficients"))
    pin(const(0.1, -500, 650), ("Phase G", "PhaseG"))
    # ripples: the game's ripple picture twice, at two sizes drifting different ways, averaged
    try:
        nt = import_texture(os.path.join(GAME_TEXTURES, "water_normal.jpg"), "T_water_normal", "normal")
        wp = E(unreal.MaterialExpressionWorldPosition, -1800, 900)
        xy = E(unreal.MaterialExpressionComponentMask, -1600, 900)
        xy.set_editor_property("r", True)
        xy.set_editor_property("g", True)
        mel.connect_material_expressions(wp, "", xy, "")
        layers = []
        for k, (size, sx, sy) in enumerate(((300.0, 0.008, 0.005), (110.0, -0.006, 0.009))):
            div = E(unreal.MaterialExpressionDivide, -1400, 900 + k * 250)
            mel.connect_material_expressions(xy, "", div, "A")
            mel.connect_material_expressions(const(size, -1600, 1000 + k * 250), "", div, "B")
            pan = E(unreal.MaterialExpressionPanner, -1200, 900 + k * 250)
            pan.set_editor_property("speed_x", sx)
            pan.set_editor_property("speed_y", sy)
            mel.connect_material_expressions(div, "", pan, "Coordinate")
            n = E(unreal.MaterialExpressionTextureSample, -1000, 900 + k * 250)
            n.set_editor_property("texture", nt)
            n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            mel.connect_material_expressions(pan, "", n, "UVs")
            layers.append(n)
        add = E(unreal.MaterialExpressionAdd, -800, 1000)
        mel.connect_material_expressions(layers[0], "RGB", add, "A")
        mel.connect_material_expressions(layers[1], "RGB", add, "B")
        # calm water: the ripples a third as strong (the normal mostly straight up), so the surface
        # mirrors the sky and the trees round it rather than breaking them up
        calm = E(unreal.MaterialExpressionLinearInterpolate, -700, 1000)
        mel.connect_material_expressions(const3((0.0, 0.0, 2.0), -900, 1150), "", calm, "A")
        mel.connect_material_expressions(add, "", calm, "B")
        mel.connect_material_expressions(const(WATER_RIPPLES, -900, 1250), "", calm, "Alpha")
        nrm = E(unreal.MaterialExpressionNormalize, -600, 1000)
        mel.connect_material_expressions(calm, "", nrm, "")
        mel.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    except Exception:
        warn("Water ripples skipped:\n" + traceback.format_exc())
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


# ------------------------------------------------------------------ built things

def line_up(meshes, info):
    """Where an imported model in the export's frame goes: (location, yaw, mirror, scale), found by
    matching its bounds to the bounds the export wrote (each quarter turn, mirrored or not)."""
    import math
    lo, hi = mesh_box(meshes)
    want_lo = (info["min"]["x"], info["min"]["y"], info["min"]["z"])
    want_hi = (info["max"]["x"], info["max"]["y"], info["max"]["z"])
    best = None
    for mirror in (1, -1):
        for yaw in (0, 90, 180, 270):
            c, sn = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
            corners = [((x * mirror) * c - y * sn, (x * mirror) * sn + y * c) for x in (lo[0], hi[0]) for y in (lo[1], hi[1])]
            ex = max(q[0] for q in corners) - min(q[0] for q in corners)
            ey = max(q[1] for q in corners) - min(q[1] for q in corners)
            scale = ((want_hi[0] - want_lo[0]) / max(1e-3, ex) + (want_hi[1] - want_lo[1]) / max(1e-3, ey)) / 2
            err = abs(ex * scale - (want_hi[0] - want_lo[0])) + abs(ey * scale - (want_hi[1] - want_lo[1]))
            if best is None or err < best[0]:
                best = (err, mirror, yaw, scale, min(q[0] for q in corners), min(q[1] for q in corners))
    err, mirror, yaw, scale, minx, miny = best
    loc = at(want_lo[0] - minx * scale, want_lo[1] - miny * scale, want_lo[2] - lo[2] * scale)
    return loc, yaw, mirror, scale, err


def prop_material(kind):
    """Paths: asphalt (or gravel) with the game's asphalt grain; everything else its own colours
    (per vertex, from the game)."""
    name = "M_Prop_" + kind
    mat = new_material(name)
    mat.set_editor_property("two_sided", True)
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)

    def const(v, x, y):
        c = E(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", v)
        return c

    if kind.startswith("path"):
        tint = (0.36, 0.355, 0.33) if kind == "path" else (0.42, 0.355, 0.27)
        tex = import_texture(os.path.join(GAME_TEXTURES, "asphalt_color.jpg"), "T_asphalt_color")
        wp = E(unreal.MaterialExpressionWorldPosition, -1200, 0)
        xy = E(unreal.MaterialExpressionComponentMask, -1000, 0)
        xy.set_editor_property("r", True)
        xy.set_editor_property("g", True)
        mel.connect_material_expressions(wp, "", xy, "")
        div = E(unreal.MaterialExpressionDivide, -800, 0)
        mel.connect_material_expressions(xy, "", div, "A")
        mel.connect_material_expressions(const(300.0, -1000, 150), "", div, "B")
        t = E(unreal.MaterialExpressionTextureSample, -600, 0)
        t.set_editor_property("texture", tex)
        mel.connect_material_expressions(div, "", t, "UVs")
        c = E(unreal.MaterialExpressionConstant3Vector, -600, 250)
        c.set_editor_property("constant", unreal.LinearColor(tint[0] / 0.208, tint[1] / 0.208, tint[2] / 0.208, 1))
        mul = E(unreal.MaterialExpressionMultiply, -400, 0)
        mel.connect_material_expressions(t, "RGB", mul, "A")
        mel.connect_material_expressions(c, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = 0.85
    else:
        vc = E(unreal.MaterialExpressionVertexColor, -400, 0)
        mel.connect_material_property(vc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = 0.35 if kind == "car" else 0.85
    mel.connect_material_property(const(rough, -400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    if kind == "car":
        mel.connect_material_property(const(0.3, -400, 400), "", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def build_props():
    """Cart paths, bridges, buildings and cars, as the game builds them."""
    if not exists("props.json"):
        log("No buildings, paths or cars in this export (export the course again)")
        return
    for old in actors.get_all_level_actors():
        if old.get_actor_label().startswith("OTG_Prop"):
            actors.destroy_actor(old)
    for info in load("props.json"):
        kind = info["kind"]
        meshes = static_meshes(import_file(os.path.join(EXPORT_DIR, info["file"]), "%s/Props/%s" % (CONTENT, kind)))
        if not meshes:
            warn("%s didn't import" % info["file"])
            continue
        loc, yaw, mirror, scale, err = line_up(meshes, info)
        mat = prop_material(kind)
        for k, mesh in enumerate(meshes):
            for i in range(len(mesh.get_editor_property("static_materials"))):
                mesh.set_material(i, mat)
            a = actors.spawn_actor_from_object(mesh, loc + unreal.Vector(0, 0, 2 if kind.startswith("path") else 0))
            a.set_actor_label("OTG_Prop_%s_%d" % (kind, k))
            a.set_actor_rotation(unreal.Rotator(0, 0, yaw), False)
            a.set_actor_scale3d(unreal.Vector(scale * mirror, scale, scale))
        log("Built: %s, %d triangles (fit %.0f cm)" % (kind, info["triangles"], err))


# ------------------------------------------------------------------ tees and pins

def mark_holes():
    """A flat box on every tee and a flagstick on every pin, labelled 'Hole 3 Blue tee' etc."""
    holes = load("holes.json")
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    cyl = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    for old in actors.get_all_level_actors():
        if old.get_actor_label().startswith("OTG_Hole"):
            actors.destroy_actor(old)
    n = 0
    for h in holes:
        for kind, p in (h.get("tees") or {}).items():
            if not p:
                continue
            a = actors.spawn_actor_from_object(cube, at(p["x"], p["y"], p["z"] + 2))
            a.set_actor_scale3d(unreal.Vector(0.4, 0.4, 0.04))
            a.set_actor_label("OTG_Hole%d %s tee" % (h["number"], kind))
            n += 1
        for day, p in (h.get("pins") or {}).items():
            if not p:
                continue
            a = actors.spawn_actor_from_object(cyl, at(p["x"], p["y"], p["z"] + 115))
            a.set_actor_scale3d(unreal.Vector(0.03, 0.03, 2.3))
            a.set_actor_label("OTG_Hole%d %s pin" % (h["number"], day))
            n += 1
    log("Marked %d tees and pins on %d holes" % (n, len(holes)))


# ------------------------------------------------------------------ build

# Graphics memory for textures (MB). The Megascans trees and the course photo need more than
# Unreal's default ("TEXTURE STREAMING POOL OVER BUDGET": textures load blurry). Applied now and saved
# in the project's config, so it holds after a restart. Lower it (3000) on an 8 GB graphics card.
TEXTURE_POOL_MB = 4000


def set_texture_pool():
    unreal.SystemLibrary.execute_console_command(None, "r.Streaming.PoolSize %d" % TEXTURE_POOL_MB)
    try:
        ini = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_config_dir()), "DefaultEngine.ini")
        text = open(ini, encoding="utf-8").read() if os.path.exists(ini) else ""
        line = "r.Streaming.PoolSize=%d" % TEXTURE_POOL_MB
        if line in text:
            return
        import re
        text = re.sub(r"(?m)^r\.Streaming\.PoolSize=.*\n?", "", text)
        head = "[/Script/Engine.RendererSettings]"
        text = text.replace(head, head + "\n" + line, 1) if head in text else text.rstrip() + "\n\n" + head + "\n" + line + "\n"
        open(ini, "w", encoding="utf-8").write(text)
        log("Texture pool set to %d MB (saved in %s)" % (TEXTURE_POOL_MB, ini))
    except Exception:
        warn("Texture pool not saved to the config: " + traceback.format_exc().splitlines()[-1])


def build():
    set_texture_pool()
    land = find_landscape()
    if not land:
        print_landscape_settings()
        log("No landscape in this level yet: import heightmap.png with the settings above, then run this script again.")
        return
    measure_offset(land)
    import time
    for key, name, fn in (("ground", "Ground", lambda: build_ground(land)), ("trees", "Trees", plant_trees),
                          ("water", "Water", build_water), ("props", "Buildings, paths, cars", build_props),
                          ("markers", "Tees and pins", mark_holes)):
        if not BUILD.get(key):
            log("%s: skipped (BUILD)" % name)
            continue
        t0 = time.time()
        step(name, fn)
        log("%s took %.0f s" % (name, time.time() - t0))
    unreal.EditorAssetLibrary.save_directory(CONTENT)
    log("Done. Tee 1: look for 'OTG_Hole1 ... tee' in the Outliner.")


build()
