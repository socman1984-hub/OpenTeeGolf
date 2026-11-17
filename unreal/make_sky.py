# The game's own sky, made headless in the game project (make_game_assets.py runs this):
#
#   M_Clouds    the volumetric clouds' material (AOTGRound's weather drives it): where clouds form
#               comes from a weather map drifting with the wind - big patches of value noise, their
#               cover growing with the weather's cloud - and how they're built from the cloud kinds:
#               heaped cumulus with flat bases and rounded tops, or (overcast, rain) a low flat stratus
#               deck. The engine's cloud noise (EngineSky) carves their shapes and frays their edges.
#
# (MPC_Weather - Wet, for the ground - is build_course.py's: the ground's material is made there.)

import unreal

FOLDER = "/Game/OpenTeeGolf/Game"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

SHAPE = "/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape128.T_VolumeNoiseShape128"
DETAIL = "/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseErosion32.T_VolumeNoiseErosion32"


def log(msg):
    unreal.log("[OpenTeeGolf sky] " + msg)


# The clouds' density at a point (per metre, before Density): P world position (cm), T time (s);
# Bottom, Thick the layer (m: the cloud component's own, set alike); Cover 0..1; Stratus 0..1 (the
# flat deck); Wind (m/s, X east Y south); Shape, Detail the noise.
HLSL = r"""
// (value noise, smooth, 0..1 about 0.5 - its spread known, unlike a texture's)
struct FNoise
{
    float H(float3 p) { p = frac(p * 0.3183099 + 0.1); p *= 17.0; return frac(p.x * p.y * p.z * (p.x + p.y + p.z)); }
    float N(float3 x)
    {
        float3 i = floor(x), f = frac(x);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(H(i), H(i + float3(1, 0, 0)), f.x), lerp(H(i + float3(0, 1, 0)), H(i + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(H(i + float3(0, 0, 1)), H(i + float3(1, 0, 1)), f.x), lerp(H(i + float3(0, 1, 1)), H(i + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    float Fbm(float3 x) { return (N(x) * 0.5 + N(x * 2.03 + 3.1) * 0.25 + N(x * 4.01 + 7.7) * 0.125 + N(x * 8.05 + 1.3) * 0.0625) / 0.9375; }
};
FNoise Nz;
float3 pm = P / 100.0;
float2 drift = Wind * T;
float h = saturate((pm.z - Bottom) / Thick);
// the weather map: where clouds may be, big drifting patches (km across), spread to 0..1
float w = Nz.Fbm(float3((pm.xy - drift) / 7000.0, 0.5));
w = saturate((w - 0.3) / 0.4);
float cov = saturate((w - (1.0 - Cover)) * 2.2 + Cover * Cover * 0.3);
// the cloud kinds' heights in the layer: cumulus - a flat base, a rounded top that tapers; stratus -
// a thin deck low in it
float cu = smoothstep(0.0, 0.07, h) * smoothstep(1.0, 0.45, h) * lerp(1.0, smoothstep(0.85, 0.2, h), 0.5);
float st = smoothstep(0.0, 0.03, h) * smoothstep(0.28, 0.1, h);
float prof = lerp(cu, st, Stratus);
// the shapes: the noise in 3D, carried by the wind (a little faster up high), heaps where it's dense
float3 q = (pm - float3(drift * (1.0 + h * 0.4), 0)) / 14000.0;
float s = Shape.SampleLevel(ShapeSampler, q, 0).r;
float c = cov * prof;
float d = saturate((s + c - 1.0) * 3.0 + Stratus * st * cov * 0.6);
// frayed edges: the finer noise eats into the thin parts
float e = Detail.SampleLevel(DetailSampler, q * 4.3 +float3(0.17, 0.41, 0.73) + float3(0, 0, T * 0.002), 0).r;
d = saturate(d - (1.0 - d) * e * 0.4);
// (Fill: a test - the noise as it comes (1), or the layer solid (2))
if (Fill > 1.5) return 1.0;
if (Fill > 0.5) return s;
return d;
"""


def material(name):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def clouds():
    m = material("M_Clouds")
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    for prop in ("used_with_volumetric_cloud",):
        try:
            m.set_editor_property(prop, True)
        except Exception as e:
            unreal.log_warning("[OpenTeeGolf sky] %s: %s" % (prop, e))
    E = lambda cls, x, y: mel.create_material_expression(m, cls, x, y)

    def scalar(name, value, y):
        p = E(unreal.MaterialExpressionScalarParameter, -900, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        return p

    def texobj(path, y):
        t = E(unreal.MaterialExpressionTextureObject, -900, y)
        # (engine content isn't in the asset registry headless: loaded by its path)
        tex = unreal.load_object(None, path)
        if not tex:
            raise RuntimeError("No " + path)
        t.set_editor_property("texture", tex)
        return t

    cust = E(unreal.MaterialExpressionCustom, -400, 0)
    cust.set_editor_property("code", HLSL)
    cust.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    cust.set_editor_property("description", "Cloud density")
    names = ["P", "T", "Bottom", "Thick", "Cover", "Stratus", "Wind", "Shape", "Detail", "Fill"]
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    cust.set_editor_property("inputs", ins)
    wp = E(unreal.MaterialExpressionWorldPosition, -900, -400)
    mel.connect_material_expressions(wp, "", cust, "P")
    t = E(unreal.MaterialExpressionTime, -900, -300)
    mel.connect_material_expressions(t, "", cust, "T")
    mel.connect_material_expressions(scalar("Bottom", 1500.0, -200), "", cust, "Bottom")
    mel.connect_material_expressions(scalar("Thick", 3000.0, -100), "", cust, "Thick")
    mel.connect_material_expressions(scalar("Cover", 0.35, 0), "", cust, "Cover")
    mel.connect_material_expressions(scalar("Stratus", 0.0, 100), "", cust, "Stratus")
    wind = E(unreal.MaterialExpressionAppendVector, -700, 200)
    mel.connect_material_expressions(scalar("WindX", 6.0, 200), "", wind, "A")
    mel.connect_material_expressions(scalar("WindY", 2.0, 300), "", wind, "B")
    mel.connect_material_expressions(wind, "", cust, "Wind")
    mel.connect_material_expressions(texobj(SHAPE, 400), "", cust, "Shape")
    mel.connect_material_expressions(texobj(DETAIL, 550), "", cust, "Detail")
    mel.connect_material_expressions(scalar("Fill", 0.0, 800), "", cust, "Fill")
    # extinction: the density times how thick the cloud is (Density, per metre)
    ext = E(unreal.MaterialExpressionMultiply, -150, 0)
    mel.connect_material_expressions(cust, "", ext, "A")
    mel.connect_material_expressions(scalar("Density", 0.06, 700), "", ext, "B")
    alb = E(unreal.MaterialExpressionVectorParameter, -400, 300)
    alb.set_editor_property("parameter_name", "Albedo")
    alb.set_editor_property("default_value", unreal.LinearColor(0.92, 0.92, 0.92, 1))
    # albedo and extinction on the old pins, as the engine's own clouds have them (Substrate takes
    # them over as they are)
    mel.connect_material_property(ext, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(alb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # lightning inside the clouds (Flash, 0..1: a storm's, AOTGRound's UpdateStorm) - a cold white
    # glow where they're thick
    glow = E(unreal.MaterialExpressionMultiply, -150, 550)
    mel.connect_material_expressions(cust, "", glow, "A")
    mel.connect_material_expressions(scalar("Flash", 0.0, 900), "", glow, "B")
    tint = E(unreal.MaterialExpressionConstant3Vector, -150, 650)
    tint.set_editor_property("constant", unreal.LinearColor(6.0, 6.5, 8.0, 1))
    em = E(unreal.MaterialExpressionMultiply, -50, 600)
    mel.connect_material_expressions(glow, "", em, "A")
    mel.connect_material_expressions(tint, "", em, "B")
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # the cloud renderer's own output: where clouds may be at all (without it, nowhere - nothing is
    # drawn), the light's scattering forward through them (a bright silver edge towards the sun) and
    # the light bounced inside them (soft, bright cloud rather than flat grey)
    adv = E(unreal.MaterialExpressionVolumetricAdvancedMaterialOutput, 0, 300)
    for prop, value in (("multi_scattering_approximation_octave_count", 2), ("const_phase_g", 0.6), ("const_phase_g2", -0.3),
                        ("const_phase_blend", 0.6), ("const_multi_scattering_contribution", 0.6), ("const_multi_scattering_occlusion", 0.4),
                        ("const_multi_scattering_eccentricity", 0.6), ("ground_contribution", True)):
        try:
            adv.set_editor_property(prop, value)
        except Exception as e:
            unreal.log_warning("[OpenTeeGolf sky] %s: %s" % (prop, e))
    one = E(unreal.MaterialExpressionConstant3Vector, -150, 400)
    one.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
    mel.connect_material_expressions(one, "", adv, "ConservativeDensity")
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_Clouds made")


# The rain far off (M_RainCurtain, on rings round the camera: AOTGRound's UpdateRainFar): U across
# the sheet in streak columns, V down it (0 its top); streaks falling, each column at its own pace,
# over a grey haze, fading out at the top and into the ground
CURTAIN_HLSL = r"""
float cols = UV.x * 220.0;
float col = floor(cols);
float hs = frac(sin(col * 12.9898 + 78.233) * 43758.5453);
float y = UV.y * (5.0 + hs * 4.0) - T * Speed * (0.8 + hs * 0.5) + hs * 17.0;
float seg = frac(y);
float streak = smoothstep(0.0, 0.1, seg) * smoothstep(0.7, 0.25, seg);
float across = pow(1.0 - abs(frac(cols) - 0.5) * 2.0, 6.0);
float fade = smoothstep(0.0, 0.45, UV.y) * smoothstep(1.0, 0.85, UV.y);
return saturate(streak * across * 0.35 + 0.18) * fade * Opacity;
"""


def rain_curtain():
    m = material("M_RainCurtain")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    E = lambda cls, x, y: mel.create_material_expression(m, cls, x, y)

    def scalar(name, value, y):
        p = E(unreal.MaterialExpressionScalarParameter, -900, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        return p
    cust = E(unreal.MaterialExpressionCustom, -400, 0)
    cust.set_editor_property("code", CURTAIN_HLSL)
    cust.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    cust.set_editor_property("description", "Rain curtain")
    ins = []
    for n in ("UV", "T", "Speed", "Opacity"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    cust.set_editor_property("inputs", ins)
    mel.connect_material_expressions(E(unreal.MaterialExpressionTextureCoordinate, -900, -200), "", cust, "UV")
    mel.connect_material_expressions(E(unreal.MaterialExpressionTime, -900, -100), "", cust, "T")
    mel.connect_material_expressions(scalar("Speed", 1.5, 0), "", cust, "Speed")
    mel.connect_material_expressions(scalar("Opacity", 0.25, 100), "", cust, "Opacity")
    mel.connect_material_property(cust, "", unreal.MaterialProperty.MP_OPACITY)
    tint = E(unreal.MaterialExpressionVectorParameter, -400, 200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.42, 0.44, 0.48, 1))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_RainCurtain made")


if not eal.does_directory_exist(FOLDER):
    eal.make_directory(FOLDER)
clouds()
rain_curtain()
