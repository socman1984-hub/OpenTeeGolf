# The game's own assets (not a course's), made headless in the game project: run by make_pack.ps1
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


if not eal.does_directory_exist(FOLDER):
    eal.make_directory(FOLDER)
rain()
open(os.path.join(os.environ.get("OTG_SCRIPT_DIR", "."), "assets_done.txt"), "w").write("ok")
