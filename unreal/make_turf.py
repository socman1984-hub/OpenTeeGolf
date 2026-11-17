# The game's own turf: blades of grass at each mowing height, made here from nothing (no bought
# pack, so it can live in the public repo) - the greens, the collars round them, the fairways and
# tees, the first cut beside them, the rough and the tall grass. Run by make_game_assets.py right
# after build_course.py's game-assets mode, in build_course.py's own namespace (its helpers -
# new_material, param_sample, wind_offset, mow_shade, write_glb, make_turf_type - and its
# TURF_GRASS table, which this updates): every kind in TURF_GRASS is made again here.
#
# What's different from build_course.py's first turf:
#   - the blades are opaque: no masked cards. A blade belongs to its cut by where it stands - each
#     vertex reads the course's masks where it is (the distance fields, 0.5 on each outline) and a
#     blade outside its cut is folded down under the ground (world position offset), so the line
#     between two cuts is as crisp as the masks, softened only by each blade's own few centimetres
#     either way (its random value), as a real mowing line is
#   - every mowing height has its own blades: the greens (4-5 mm, the finest), the collar (1 cm),
#     the fairways and tees (1.5 cm), the first cut (3 cm, a band FirstCutM wide round them - the
#     game sets it to 0 when the first cut is off, and the rough grows to the fairway then), the
#     rough (longer and clumpier) and the tall grass
#   - mown blades are cut square across the top, not pointed; they lean with the mower in stripes
#     (layer_mow: the bands of the ground's own stripes, which way each band was mown) and are
#     lighter or darker as the ground's stripes are, so blades and ground show one pattern
#   - the colour is the ground's own surface colour (build_course.py TURF / FRINGE), darker at the
#     root and lighter at the tip, each blade a little different, lit through from behind (two-sided
#     foliage), with the designer's colour on top (Tint, and TurfTint: the fairway or green look)
#   - the wind comes from MPC_Wind (wind_offset), as the trees' does
#
# Keep TURF_CUTS' draw distances and densities the same as OTGGrass.cpp's DefaultKinds.

import math
import random
import traceback
import zlib

import unreal

# mowing heights (cm) and the rest, per kind. Keys as build_course.py's TURF_GRASS, plus:
#   keep: which cut the blades belong to (see KEEP)
#   segs: segments up a blade; cut: a mown blade's top as a share of its width (None: pointed)
#   up: how much a blade's normal leans up to the sky (mown turf lit as a surface, not speckled)
#   mow: the stripes' kind ("fairway", "green") or None; mow_lean: how far a striped blade leans with
#   the mower, as a share of its height
#   clump_mix: the share of blades in clumps (the rest spread evenly)
#   patch_cm: a round patch this wide (blades thinning out at its edge, so the patches overlap
#   without seams)
TURF_CUTS = {
    # (the mown cuts tile: square patches, patch_cm a side, on an exact grid as dense as that -
    # OTGGrass.cpp's density is 1e4 / patch_cm^2 per 100 m2 - turned by quarter turns, so they cover
    # the ground evenly, no clumps or gaps)
    "green": {"keep": "green", "square": True, "height_cm": (0.4, 0.6), "width_mm": (1.0, 1.5), "lean": 0.15, "blades": 520, "segs": 2, "cut": 0.75, "up": 0.97,
              "patch_cm": 20, "density": 2500, "draw_m": (14, 24), "shade": (0.88, 1.12),
              "mow": "green", "mow_lean": 0.3,
              "root": (0.032, 0.052, 0.014), "tip": (0.058, 0.096, 0.025), "dry": (0.09, 0.1, 0.035), "dry_share": 0.0, "vary": 0.1},
    "fringe": {"keep": "fringe", "square": True, "height_cm": (0.8, 1.1), "width_mm": (1.3, 1.9), "lean": 0.18, "blades": 480, "segs": 2, "cut": 0.7, "up": 0.96,
               "patch_cm": 20, "density": 2500, "draw_m": (18, 30), "shade": (0.88, 1.12),
               "root": (0.027, 0.043, 0.011), "tip": (0.052, 0.083, 0.022), "dry": (0.09, 0.1, 0.035), "dry_share": 0.005, "vary": 0.1},
    "fairway": {"keep": "fairway", "square": True, "height_cm": (0.9, 1.3), "width_mm": (1.5, 2.2), "lean": 0.2, "blades": 460, "segs": 2, "cut": 0.7, "up": 0.96,
                "patch_cm": 20, "density": 2500, "draw_m": (24, 38), "shade": (0.88, 1.12),
                "mow": "fairway", "mow_lean": 0.35,
                "root": (0.03, 0.046, 0.012), "tip": (0.058, 0.089, 0.023), "dry": (0.11, 0.12, 0.045), "dry_share": 0.01, "vary": 0.1},
    "cut1": {"keep": "cut1", "square": True, "masks": ["rough"], "height_cm": (2.2, 3.4), "width_mm": (2.0, 3.0), "lean": 0.3, "blades": 340, "segs": 3, "cut": 0.6, "up": 0.94,
             "patch_cm": 20, "density": 2500, "draw_m": (40, 60), "shade": (0.85, 1.15),
             "root": (0.034, 0.052, 0.014), "tip": (0.075, 0.11, 0.027), "dry": (0.11, 0.12, 0.045), "dry_share": 0.01, "vary": 0.1},
    # the rough: longer, clumpier, pointed (it grows back between cuts), still one even green
    "rough": {"keep": "rough", "masks": ["rough"], "height_cm": (5.5, 11.0), "width_mm": (3.0, 5.0), "lean": 0.5, "blades": 320, "segs": 4, "up": 0.92,
              "patch_cm": 30, "density": 2400, "draw_m": (70, 100), "clumps": 6, "clump_cm": 4.5, "clump_mix": 0.55,
              "root": (0.034, 0.055, 0.014), "tip": (0.092, 0.136, 0.032), "dry": (0.13, 0.135, 0.045), "dry_share": 0.02, "vary": 0.1},
}
# (build_course.py's own kinds: the rough's tufts, the far-off rough and the tall grass. Kept, made
# again here with these blades: opaque, more segments up the long ones)
KEEP_OF = {"rough_clump": "rough", "rough_far": "rough"}
SEGS_OF = {"rough_far": 2, "rough_clump": 4}
# a striped blade's lean and the stripes' width per stripe kind (build_course.py MOW_WIDTH_M)
FIRST_CUT_M = 1.5          # the first cut's width (the game's FirstCutM; 0 when it's off)
BLADE_JITTER = 0.008       # each blade's own edge, either way (mask units: 1/6 a metre each; 0.008 = 5 cm)


def _rand_for(kind):
    return random.Random(7 + zlib.crc32(kind.encode()) % 997)


def cut_patch(kind, cfg, seed=7):
    """A round patch of blades, cfg["patch_cm"] across (y up, metres - write_glb's frame): each blade a
    strip curving over as it leans, tapering to a point (the long grass) or cut square across (cfg
    "cut": the mown), twisting a little, darker at the root. Thinner towards the patch's edge, so the
    patches the game scatters overlap without a seam. Vertex colours: how far up, the blade's random
    value, whether it's dry (write_glb puts them in the UVs)."""
    rnd = _rand_for(kind)
    R = cfg["patch_cm"] / 200.0
    segs = cfg.get("segs", SEGS_OF.get(kind, 3))
    cut = cfg.get("cut")
    up = cfg.get("up", 0.93)
    pos, nrm, col, idx = [], [], [], []

    def disk():
        # (uniform over the disk, then thinned by a soft edge: a blade near the rim kept less often)
        while True:
            r = R * math.sqrt(rnd.random())
            a = rnd.random() * math.tau
            if rnd.random() < 1.0 - 0.7 * max(0.0, (r / R - 0.6) / 0.4):
                return r * math.cos(a), r * math.sin(a)

    if cfg.get("square"):
        def disk():
            return (rnd.random() - 0.5) * 2 * R, (rnd.random() - 0.5) * 2 * R
    clumps = [disk() for _ in range(cfg.get("clumps", 0))]
    mix = cfg.get("clump_mix", 1.0)
    for _ in range(cfg["blades"]):
        if clumps and rnd.random() < mix:
            cx, cz = clumps[rnd.randrange(len(clumps))]
            x, z = cx + rnd.gauss(0, cfg["clump_cm"] / 100.0), cz + rnd.gauss(0, cfg["clump_cm"] / 100.0)
            d = math.hypot(x, z)
            if d > R:
                x, z = x * R / d, z * R / d
        else:
            x, z = disk()
        h = rnd.uniform(*cfg["height_cm"]) / 100.0
        w = rnd.uniform(*cfg["width_mm"]) / 1000.0
        face = rnd.random() * math.tau
        bend = rnd.random() * math.tau
        lean = rnd.uniform(0.15, 1.0) * cfg["lean"] * h
        twist = rnd.uniform(-0.7, 0.7)
        bx, bz = math.cos(bend), math.sin(bend)
        v = rnd.random()
        dry = 1.0 if rnd.random() < cfg.get("dry_share", 0.0) else 0.0
        base = len(pos) // 3
        rows = []
        for i in range(segs + 1):
            t = i / segs
            cxp, czp = x + bx * lean * t * t, z + bz * lean * t * t
            cy = h * t * (1 - 0.25 * (lean / max(h, 1e-6)) * t)
            ang = face + twist * t
            fx, fz = math.cos(ang), math.sin(ang)
            if cut is None:
                half = 0.5 * w * (1 - t ** 1.6)
            else:
                half = 0.5 * w * (1 - (1 - cut) * t)
            # (the face's normal, leaned well up toward the sky: mown turf lit as one surface)
            nx, nz = -fz, fx
            n = (nx * (1 - up), up, nz * (1 - up))
            ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
            pts = [(cxp, cy, czp)] if half <= 1e-6 else [(cxp - fx * half, cy, czp - fz * half), (cxp + fx * half, cy, czp + fz * half)]
            row = []
            for p in pts:
                row.append(len(pos) // 3)
                pos += [p[0], p[1], p[2]]
                nrm += [n[0] / ln, n[1] / ln, n[2] / ln]
                col += [t, v, dry, 1.0]
            rows.append(row)
        for a, b in zip(rows, rows[1:]):
            if len(b) == 1:
                idx += [a[0], b[0], a[1]]
            else:
                idx += [a[0], b[0], a[1], a[1], b[0], b[1]]
        # a seed head on some (the tall grass, the wildflowers): a slim spindle past the tip, nodding
        # over the way the blade leans - two crossed strips, so it reads from any side
        if rnd.random() < cfg.get("seed_share", 0):
            tx, ty, tz = pos[rows[-1][0] * 3], pos[rows[-1][0] * 3 + 1], pos[rows[-1][0] * 3 + 2]
            L = rnd.uniform(*cfg.get("head_len", (0.06, 0.11)))
            hx, hy, hz = tx + bx * L * 0.45, ty + L * 0.85, tz + bz * L * 0.45
            sb = len(pos) // 3
            fx, fz = math.cos(face), math.sin(face)
            for (ax, az) in ((fx, fz), (-fz, fx)):
                wd = rnd.uniform(*cfg.get("head_w", (0.004, 0.007)))
                for (px_, py_, pz_), half in (((tx, ty, tz), 0.0), (((tx + hx) / 2, (ty + hy) / 2, (tz + hz) / 2), wd), ((hx, hy, hz), 0.0)):
                    for sgn in (-1, 1):
                        pos += [px_ + ax * half * sgn, py_, pz_ + az * half * sgn]
                        nrm += [0.0, 1.0, 0.0]
                        col += [1.0, v, 1.0, 1.0]
            for k in (0, 6):
                q = sb + k
                idx += [q, q + 2, q + 1, q + 1, q + 2, q + 3, q + 2, q + 4, q + 3, q + 3, q + 4, q + 5]
    return pos, nrm, col, idx


# Which blades stay (HLSL, per vertex: the masks where it stands, t its blade's own outline - 0.5,
# a few cm either way). Anything else is folded under the ground.
KEEP = {
    "green": ("Green",), "fringe": ("Fringe", "Green", "Bunker"),
    "fairway": ("Fairway", "Tee", "Green", "Fringe", "Bunker", "Path", "Road"),
    "cut1": ("Fairway", "Tee", "Fringe", "Green", "Native", "Bunker", "Water", "Path", "Road"),
    "rough": ("Rough", "Fairway", "Tee", "Fringe", "Green", "Bunker", "Water", "Path", "Road"),
    "native": ("Native", "Fairway", "Tee", "Fringe", "Green", "Water", "Path", "Road"),
}
KEEP_CODE = {
    "green": "k *= step(t, Green);",
    "fringe": "k *= step(t, Fringe) * step(Green, t);",
    "fairway": "k *= step(t, max(Fairway, Tee)) * step(Green, t) * step(Fringe, t);",
    # (the first cut: a band FirstCutM wide outside the fairways, tees and collars - none when it's 0)
    "cut1": "float m = max(max(Fairway, Tee), Fringe); k *= step(t - B, m) * step(m, t) * step(Green, t) * step(Native, 0.5) * step(0.001, B);",
    "rough": "float m = max(max(Fairway, Tee), Fringe); k *= step(t, Rough) * step(m, t - B) * step(Green, t);",
    "native": "float m = max(max(max(Fairway, Tee), Fringe), Green); k *= step(t, Native) * step(m, t);",
}


def keep_rule(kind, cfg):
    if cfg.get("keep"):
        return cfg["keep"]
    if kind in KEEP_OF:
        return KEEP_OF[kind]
    return "native" if "native" in cfg["masks"] else "rough"


def cut_material(kind, land_size_cm):
    """The blades' material: opaque, two-sided foliage (lit through from behind). The blades not in
    their cut folded under the ground, the rest leaning in the wind (MPC_Wind) and with the mower,
    shrinking into the ground far off; coloured as the ground's surface under them."""
    cfg = TURF_GRASS[kind]
    rule = keep_rule(kind, cfg)
    mat = new_material("M_Turf_" + kind)
    mat.set_editor_property("two_sided", True)
    first_ok(mat, ("shading_model",), unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    hmax = cfg["height_cm"][1] * cfg.get("scale", (1, 1.2))[1]
    first_ok(mat, ("max_world_position_offset_displacement",), float(hmax * 1.6 + 4))
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)
    S = unreal.MaterialSamplerType

    def const(v, x=-1600, y=0):
        c = E(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", v)
        return c

    def const3(rgb, x=-1600, y=0):
        c = E(unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
        return c

    def op(cls, a, b, x, y, a_out="", b_out=""):
        n = E(cls, x, y)
        mel.connect_material_expressions(a, a_out, n, "A")
        mel.connect_material_expressions(b, b_out, n, "B")
        return n

    def lerp(a, b, t, x, y, t_out=""):
        n = E(unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        mel.connect_material_expressions(t, t_out, n, "Alpha")
        return n

    def custom(code, out_type, inputs, x, y):
        c = E(unreal.MaterialExpressionCustom, x, y)
        c.set_editor_property("code", code)
        c.set_editor_property("output_type", out_type)
        ins = []
        for n, _, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for n, node, out in inputs:
            mel.connect_material_expressions(node, out, c, n)
        return c

    def scalar(name, v, x, y):
        p = E(unreal.MaterialExpressionScalarParameter, x, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(v))
        return p

    def channel(index, comp, x, y):
        tc = E(unreal.MaterialExpressionTextureCoordinate, x - 150, y)
        tc.set_editor_property("coordinate_index", index)
        m = E(unreal.MaterialExpressionComponentMask, x, y)
        m.set_editor_property("r", comp == "r")
        m.set_editor_property("g", comp == "g")
        mel.connect_material_expressions(tc, "", m, "")
        return m

    # where this point is on the course, 0..1 (the level's origin is the course's north-west corner)
    wp = E(unreal.MaterialExpressionWorldPosition, -2400, 0)
    try:
        wp.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    except Exception:
        pass
    xy = E(unreal.MaterialExpressionComponentMask, -2250, 0)
    xy.set_editor_property("r", True)
    xy.set_editor_property("g", True)
    mel.connect_material_expressions(wp, "", xy, "")
    size = scalar("LandSizeCm", land_size_cm, -2250, 100)
    uv = op(unreal.MaterialExpressionDivide, xy, size, -2100, 0)
    t_up, t_rand, t_dry = channel(0, "r", -1700, 260), channel(0, "g", -1700, 320), channel(1, "r", -1700, 380)

    # which blades stay: the masks where each vertex stands
    names = KEEP[rule]
    samples = []
    for i, nm in enumerate(names):
        s = param_sample(mat, "Mask_" + nm.lower(), uv, -1900, 1800 + i * 160, S.SAMPLERTYPE_LINEAR_GRAYSCALE)
        samples.append((nm, s, "R"))
    band = op(unreal.MaterialExpressionDivide, scalar("FirstCutM", FIRST_CUT_M, -1900, 3400), const(6.0, -1900, 3460), -1750, 3400)
    code = "float t = 0.5 + (Rand - 0.5) * %.4f;\nfloat k = 1;\n" % (2 * BLADE_JITTER)
    if "Bunker" in names:
        code += "k *= step(Bunker, t);\n"
    for nm in ("Water", "Path", "Road"):
        if nm in names:
            code += "k *= step(%s, 0.5);\n" % nm
    code += KEEP_CODE[rule] + "\nreturn k;"
    keep = custom(code, unreal.CustomMaterialOutputType.CMOT_FLOAT1, samples + [("Rand", t_rand, ""), ("B", band, "")], -1500, 1800)

    # how high above its foot, in the world (the instance's size in it)
    lz = world_height(mat)
    # far off, shrinking into the ground (GRASS_FADE of the way from the first draw distance to the
    # second), so the grass thins away instead of popping
    d0 = cfg["draw_m"][0] * 100.0
    d1 = d0 + (cfg["draw_m"][1] * 100.0 - d0) * GRASS_FADE
    dist = E(unreal.MaterialExpressionDistance, -1600, 1000)
    mel.connect_material_expressions(E(unreal.MaterialExpressionCameraPositionWS, -1750, 1000), "", dist, "A")
    mel.connect_material_expressions(wp, "", dist, "B")
    fade = E(unreal.MaterialExpressionSmoothStep, -1450, 1000)
    mel.connect_material_expressions(const(d0, -1600, 1100), "", fade, "Min")
    mel.connect_material_expressions(const(d1, -1600, 1150), "", fade, "Max")
    mel.connect_material_expressions(dist, "", fade, "Value")
    try:
        wind = wind_offset(mat, lz, GRASS_BEND / hmax, 0.25, GRASS_WAVE_CM)
    except Exception:
        warn("Turf %s: no wind (%s)" % (kind, traceback.format_exc().splitlines()[-1]))
        wind = const3((0, 0, 0), -1400, 1300)
    mow = None
    if cfg.get("mow"):
        mow = param_sample(mat, "Mow", uv, -1900, 1500, S.SAMPLERTYPE_LINEAR_COLOR)
    ins = [("K", keep, ""), ("LZ", lz, ""), ("Fade", fade, ""), ("Wind", wind, "")]
    code = "float3 o = Wind;\n"
    if mow is not None:
        # (the band this blade stands in, as build_course.py mow_shade has it: the blades lean away
        # from you along a light band, toward you along a dark one)
        width = MOW_WIDTH_M[1 if cfg["mow"] == "green" else 0] * 100.0
        code += ("float2 d = normalize(Mow.rg * 2 - 1 + float2(1e-4, 0));\n"
                 "float2 p = float2(-d.y, d.x);\n"
                 "float tri = abs(frac(dot(WP.xy, p) / %.1f) * 2 - 1);\n"
                 "float pm = smoothstep(0.42, 0.58, tri) * 2 - 1;\n"
                 "o.xy -= d * pm * LZ * LZ * %.5f;\n") % (width, cfg.get("mow_lean", 0.3) / hmax)
        ins += [("Mow", mow, "RGB"), ("WP", wp, "")]
    code += "o *= K;\no.z -= (1 - K) * (LZ + 3) + LZ * Fade;\nreturn o;"
    wpo = custom(code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ins, -1100, 1200)
    mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    # colour: root to tip (a little past linear: most of what shows is the upper blade), a dry tip on
    # a few, each blade lighter or darker; the photo's light and shade, as the ground has it
    up_c = E(unreal.MaterialExpressionPower, -1550, 260)
    mel.connect_material_expressions(t_up, "", up_c, "Base")
    mel.connect_material_expressions(const(0.8, -1650, 300), "", up_c, "Exp")
    col = lerp(const3(cfg["root"], -1450, 200), const3(cfg["tip"], -1450, 240), up_c, -1300, 250)
    dried = lerp(col, op(unreal.MaterialExpressionMultiply, const3(cfg["dry"], -1450, 420), t_up, -1350, 420), t_dry, -1150, 300)
    vary = op(unreal.MaterialExpressionAdd, const(1.0 - cfg["vary"], -1300, 480), op(unreal.MaterialExpressionMultiply, t_rand, const(2 * cfg["vary"], -1450, 520), -1300, 520), -1150, 480)
    col = op(unreal.MaterialExpressionMultiply, dried, vary, -1000, 350)
    ph = param_sample(mat, "Photo", uv, -1900, 600, S.SAMPLERTYPE_COLOR)
    photo_blur(ph)
    lum = E(unreal.MaterialExpressionDotProduct, -1700, 600)
    mel.connect_material_expressions(ph, "RGB", lum, "A")
    mel.connect_material_expressions(const3((0.2126, 0.7152, 0.0722), -1850, 700), "", lum, "B")
    lo_hi = cfg.get("shade", (0.75, 1.25))
    shade = op(unreal.MaterialExpressionMin,
               op(unreal.MaterialExpressionMax, op(unreal.MaterialExpressionDivide, lum, const(0.3, -1700, 700), -1550, 600), const(lo_hi[0], -1550, 700), -1400, 600),
               const(lo_hi[1], -1400, 700), -1250, 600)
    col = op(unreal.MaterialExpressionMultiply, col, shade, -900, 400)
    if mow is not None:
        try:
            f = mow_shade(mat, mow, const(1.0 if cfg["mow"] == "green" else 0.0, -1500, 2900))
            col = op(unreal.MaterialExpressionMultiply, col, f, -800, 450)
        except Exception:
            warn("Turf %s: no mowing stripes (%s)" % (kind, traceback.format_exc().splitlines()[-1]))
    # the designer's colours: the kind's own (Tint), and for the mown cuts the fairway's or the
    # green's look (TurfTint, as the ground under them takes it: AOTGRuntimeCourse::SetTurfLook)
    for i, pname in enumerate(("Tint", "TurfTint")):
        p = E(unreal.MaterialExpressionVectorParameter, -900, 250 - i * 80)
        p.set_editor_property("parameter_name", pname)
        p.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
        col = op(unreal.MaterialExpressionMultiply, col, p, -700, 350 - i * 40, "", "RGB")
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # lit through from behind: a warmer, yellower green
    mel.connect_material_property(op(unreal.MaterialExpressionMultiply, col, const3((0.95, 1.05, 0.5), -650, 600), -500, 560), "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    # (in the turf's own shade low down)
    ao = lerp(const(0.6, -650, 750), const(1.0, -650, 790), up_c, -500, 760)
    try:
        mel.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    except Exception:
        pass
    mel.connect_material_property(const(0.58 if cfg.get("cut") else 0.65, -500, 820), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(const(0.4, -500, 880), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def make_cut_turf():
    """Every kind of turf made again with these blades and this material (build_course.py's
    make_turf_type, which calls turf_patch and turf_material: these, from here on)."""
    global turf_patch, turf_material
    turf_patch = cut_patch
    turf_material = cut_material
    for k, cfg in TURF_CUTS.items():
        cfg = dict(cfg)
        cfg.setdefault("masks", {"green": ["green"], "fringe": ["fringe"], "fairway": ["fairway", "tee"]}.get(k, ["rough"]))
        TURF_GRASS[k] = cfg
    # (the far-off rough from the new rough, as build_course.py makes it)
    far = dict(TURF_GRASS["rough"])
    far.update({"blades": max(40, far["blades"] // 4), "width_mm": (far["width_mm"][0] * 2.0, far["width_mm"][1] * 2.2),
                "patch_cm": far["patch_cm"] * 2, "density": far["density"] / 12.0, "draw_m": (170.0, 220.0), "scale": (1.1, 1.45),
                "clumps": 0, "segs": 2})
    TURF_GRASS["rough_far"] = far
    only = [k.strip() for k in os.environ.get("OTG_TURF_ONLY", "").split(",") if k.strip()]
    for kind in TURF_GRASS:
        if only and kind not in only:
            continue
        made = []
        step("Turf " + kind, lambda kind=kind: made.append(make_turf_type(kind)))
        # (Nanite keeps the thin blades' area as it simplifies them far off, instead of losing them)
        try:
            mesh = made[0]
            ns = mesh.get_editor_property("nanite_settings")
            ns.set_editor_property("preserve_area", True)
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        except Exception:
            pass
    log("Turf (make_turf.py): %s" % ", ".join(k for k in TURF_GRASS if not only or k in only))


if RUNTIME:
    make_cut_turf()
