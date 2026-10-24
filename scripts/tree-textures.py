# Leaf and bark pictures for the trees grown by public/js/grow.js, drawn from scratch (no scans):
#   <kind>_leaves.png      - 1024 x 1024, four pictures on a transparent background, one per
#                            quarter, each with its base at the bottom middle of its quarter:
#                              bottom left, bottom right  foliage (two versions of the leafy twig)
#                              top left, top right        extras: Spanish moss (live oak),
#                                                         flowers and fruit (magnolia, saguaro),
#                                                         otherwise more foliage
#   <kind>_bark_color.jpg  - 1024 x 1024, tiling both ways
#   <kind>_bark_normal.jpg
# Leaf colours are the real ones (grow.js's trees draw them as they are; see `raw` in trees.js).
# grow.js maps its cards onto these quarters, so keep the layout if you redraw them.
# In the game, most of these have since been replaced by photo scans: the barks of live oak,
# cypress, bald cypress, mesquite, sycamore and magnolia (Poly Haven, see NOTICE.md), and the
# foliage of live oak, magnolia, sycamore and both cypresses (scripts/scan-leaves.py, run after
# this). Running this again draws them all afresh.
#   python scripts/tree-textures.py public/textures/trees [kind ...]
# Kinds: liveoak cypress baldcypress mesquite paloverde saguaro sycamore magnolia
# Needs Python 3 with Pillow and NumPy.
import sys, math, random
import numpy as np
from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else '.'
only = set(sys.argv[2:])
S = 2048            # leaves are drawn at twice the size, then halved (smooth edges)
Q = S // 2          # one quarter
QUARTERS = {'bl': (0, Q), 'br': (Q, Q), 'tl': (0, 0), 'tr': (Q, 0)}

def jit(c, k=12):
    return tuple(max(0, min(255, int(v + random.randint(-k, k)))) for v in c) + (255,)

def rot(pts, ang, x, y):
    c, s = math.cos(ang), math.sin(ang)
    return [(x + px * c - py * s, y + px * s + py * c) for px, py in pts]

# ------------------------------------------------------------------ leaf shapes
# Local coordinates: base at (0, 0), pointing up (-y), length L

def ellipse_leaf(L, W, tip=1.0):
    pts = []
    for i in range(17):
        t = i / 16
        pts.append((W / 2 * math.sin(math.pi * t) ** 0.85 * (1 - 0.35 * t * tip), -L * t))
    return pts + [(-x, y) for x, y in reversed(pts)]

def palmate_leaf(L, lobes=((0, 1.0), (0.95, 0.8), (-0.95, 0.8), (1.9, 0.45), (-1.9, 0.45)), notch=0.55, teeth=True):
    """Lobed leaf (sycamore): radius round the base of the blade, pointed lobes, coarse teeth."""
    pts = []
    for i in range(240):
        th = -math.pi + 2 * math.pi * i / 240
        r = notch * 0.5
        for a, l in lobes:
            d = math.atan2(math.sin(th - a), math.cos(th - a))
            if abs(d) < 0.55: r = max(r, l * (1 - abs(d) / 0.55) ** 0.6)
        if teeth: r *= 1 + 0.05 * math.sin(th * 26)
        pts.append((math.sin(th) * r * L * 0.55, -L * 0.45 - math.cos(th) * r * L * 0.55))
    return pts

def draw_leaf(d, shape, x, y, ang, col, vein=None):
    d.polygon(rot(shape, ang, x, y), fill=col)
    if vein:
        tip = shape[len(shape) // 2] if len(shape) > 34 else (0, min(p[1] for p in shape))
        d.line(rot([(0, 0), (0, tip[1] * 0.92)], ang, x, y), fill=vein, width=2)

# ------------------------------------------------------------------ twigs

def twig(d, ox, oy, leaf, n, cols, stem=(92, 76, 58), side=4, spread=0.75, up=0.94, size=1.0, vein=None, seed=0):
    """A leafy twig: a main stem up the quarter with side shoots, leaves along all of them."""
    rnd = random.Random(seed)
    cx, by = ox + Q / 2, oy + Q - 10
    stems = []
    # main stem: a gentle curve to near the top
    pts = [(cx, by)]
    x, y, a = cx, by, rnd.uniform(-0.15, 0.15)
    for i in range(12):
        a += rnd.uniform(-0.12, 0.12)
        x += math.sin(a) * Q * up / 12; y -= math.cos(a) * Q * up / 12
        pts.append((x, y))
    stems.append(pts)
    for k in range(side):
        j = rnd.randint(3, 8); sx, sy = pts[j]
        sa = (1 if k % 2 else -1) * rnd.uniform(0.5, 0.9) * spread + a * 0.3
        length = Q * rnd.uniform(0.25, 0.42)
        sp = [(sx, sy)]
        for i in range(6):
            sa *= 0.92
            sx += math.sin(sa) * length / 6; sy -= math.cos(sa) * length / 6
            sx = min(ox + Q - 30, max(ox + 30, sx))
            sp.append((sx, sy))
        stems.append(sp)
    for s in stems:
        d.line(s, fill=jit(stem, 6), width=7 if s is stems[0] else 4)
    # leaves along the stems, more towards their ends, alternating sides
    for s in stems:
        for i in range(n):
            t = 0.25 + 0.75 * (i + rnd.random() * 0.6) / n
            k = min(len(s) - 2, int(t * (len(s) - 1)))
            (x0, y0), (x1, y1) = s[k], s[k + 1]
            f = t * (len(s) - 1) - k
            x, y = x0 + (x1 - x0) * f, y0 + (y1 - y0) * f
            base = math.atan2(x1 - x0, -(y1 - y0))
            ang = base + (1 if i % 2 else -1) * rnd.uniform(0.5, 1.0)
            draw_leaf(d, leaf(rnd, size), x, y, ang, jit(rnd.choice(cols), 10), vein)
        # one at the tip
        (x0, y0), (x1, y1) = s[-2], s[-1]
        draw_leaf(d, leaf(rnd, size), x1, y1, math.atan2(x1 - x0, -(y1 - y0)), jit(rnd.choice(cols), 10), vein)

def compound(d, ox, oy, cols, stem, n_pinnae, leaflets, leaflet_len, seed, flowers=None):
    """Feathery compound leaves (mesquite, palo verde): pairs of pinnae, each a row of tiny leaflets."""
    rnd = random.Random(seed)
    cx, by = ox + Q / 2, oy + Q - 10
    d.line([(cx, by), (cx + rnd.uniform(-30, 30), oy + Q * 0.55)], fill=jit(stem, 6), width=6)
    hubs = [(cx + rnd.uniform(-40, 40), oy + Q * rnd.uniform(0.45, 0.62)) for _ in range(3)]
    for hx, hy in hubs:
        d.line([(cx, oy + Q * 0.6), (hx, hy)], fill=jit(stem, 6), width=5)
        for p in range(n_pinnae):
            a = -1.3 + 2.6 * (p + 0.5) / n_pinnae + rnd.uniform(-0.1, 0.1)
            L = Q * rnd.uniform(0.28, 0.42)
            ex, ey = hx + math.sin(a) * L, hy - math.cos(a) * L
            d.line([(hx, hy), (ex, ey)], fill=jit(stem, 6), width=3)
            for k in range(leaflets):
                t = 0.15 + 0.85 * k / leaflets
                px, py = hx + (ex - hx) * t, hy + (ey - hy) * t
                for sgn in (-1, 1):
                    draw_leaf(d, ellipse_leaf(leaflet_len, leaflet_len * 0.42), px, py, a + sgn * 1.2, jit(rnd.choice(cols), 10))
    if flowers:
        for _ in range(26):
            x, y = cx + rnd.uniform(-Q * 0.38, Q * 0.38), oy + rnd.uniform(Q * 0.1, Q * 0.6)
            r = rnd.uniform(9, 15)
            d.ellipse([x - r, y - r, x + r, y + r], fill=jit(flowers, 12))

def spray(d, ox, oy, col, seed, needle=False):
    """Conifer foliage. Scale sprays (Monterey cypress): dense, branching rope-like shoots.
    needle=True (bald cypress): flat sprays of short soft needles in two rows."""
    rnd = random.Random(seed)
    cx, by = ox + Q / 2, oy + Q - 10
    inside = lambda x, y: ox + 16 < x < ox + Q - 16 and oy + 16 < y < oy + Q - 4
    def shoot(x, y, a, L, w, depth):
        n = 6
        for i in range(n):
            a += rnd.uniform(-0.12, 0.12)
            nx, ny = x + math.sin(a) * L / n, y - math.cos(a) * L / n
            if not inside(nx, ny): return
            d.line([(x, y), (nx, ny)], fill=jit(col, 14), width=max(3, int(w)))
            if needle and depth >= 1:
                for sgn in (-1, 1):
                    na = a + sgn * 1.2
                    ex, ey = nx + math.sin(na) * 30, ny - math.cos(na) * 30
                    if inside(ex, ey): d.line([(nx, ny), (ex, ey)], fill=jit(col, 16), width=4)
            if depth < 3 and rnd.random() < (0.6 if not needle else 0.5):
                for sgn in ((-1, 1) if rnd.random() < 0.5 else (rnd.choice((-1, 1)),)):
                    shoot(nx, ny, a + sgn * rnd.uniform(0.45, 0.85), L * rnd.uniform(0.42, 0.58), w * 0.78, depth + 1)
            x, y = nx, ny
    for k in range(3):
        shoot(cx + rnd.uniform(-20, 20), by, rnd.uniform(-0.3, 0.3), Q * rnd.uniform(0.55, 0.68), 20 if not needle else 6, 0)

def moss(d, ox, oy, seed):
    """Spanish moss: grey-green strands hanging from the top of the quarter, tangled and thinning out."""
    rnd = random.Random(seed)
    for i in range(110):
        x = ox + Q / 2 + rnd.gauss(0, Q * 0.13)
        y = oy + 8
        L = Q * rnd.uniform(0.35, 0.97)
        pts = [(x, y)]
        a = rnd.uniform(-0.2, 0.2)
        for k in range(30):
            a = a * 0.8 + rnd.uniform(-0.25, 0.25)
            x += math.sin(a) * 6; y += L / 30
            pts.append((x, y))
        d.line(pts, fill=jit((152, 160, 136), 16), width=rnd.choice((3, 4, 5)))
        # little curls along the strand
        for k in range(0, 30, 4):
            px, py = pts[k]
            d.line([(px, py), (px + rnd.uniform(-14, 14), py + rnd.uniform(6, 18))], fill=jit((142, 150, 128), 14), width=3)

def flower_magnolia(d, ox, oy, seed):
    rnd = random.Random(seed)
    cx, cy = ox + Q / 2, oy + Q * 0.62
    for k in range(9):
        a = -1.2 + 2.4 * k / 8 + rnd.uniform(-0.1, 0.1)
        draw_leaf(d, ellipse_leaf(Q * 0.36, Q * 0.17, tip=0.3), cx, cy + Q * 0.12, a * 0.8, jit((246, 242, 228), 6), vein=(226, 220, 200))
    d.ellipse([cx - 30, cy - 70, cx + 30, cy + 10], fill=jit((214, 196, 120), 6))
    d.line([(cx, oy + Q - 10), (cx, cy + Q * 0.12)], fill=(90, 76, 56), width=10)

def flowers_saguaro(d, ox, oy, seed, fruit=False):
    rnd = random.Random(seed)
    for k in range(5):
        x, y = ox + Q * (0.2 + 0.6 * k / 4) + rnd.uniform(-20, 20), oy + Q * rnd.uniform(0.45, 0.7)
        d.line([(x, oy + Q - 10), (x, y)], fill=(86, 120, 70), width=26)
        if fruit:
            d.ellipse([x - 40, y - 70, x + 40, y + 20], fill=jit((190, 46, 52), 12))
        else:
            for p in range(8):
                a = -1.4 + 2.8 * p / 7
                draw_leaf(d, ellipse_leaf(110, 70, tip=0.2), x, y, a, jit((248, 246, 236), 5))
            d.ellipse([x - 26, y - 34, x + 26, y + 6], fill=jit((236, 206, 88), 8))

# ------------------------------------------------------------------ the species' leaves

def L_small_oak(rnd, s): return ellipse_leaf(rnd.uniform(60, 80) * s, rnd.uniform(26, 34) * s, 0.6)
def L_magnolia(rnd, s): return ellipse_leaf(rnd.uniform(230, 300) * s, rnd.uniform(90, 120) * s, 0.7)
def L_sycamore(rnd, s): return palmate_leaf(rnd.uniform(230, 290) * s)

LEAVES = {
    'liveoak': lambda d: (
        twig(d, *QUARTERS['bl'], L_small_oak, 34, [(58, 76, 38), (66, 84, 42), (52, 70, 36), (92, 104, 72)], stem=(80, 70, 58), side=6, spread=1.0, seed=1),
        twig(d, *QUARTERS['br'], L_small_oak, 32, [(62, 80, 40), (56, 72, 38), (90, 102, 70)], stem=(80, 70, 58), side=6, spread=1.0, seed=2),
        moss(d, *QUARTERS['tl'], 3), moss(d, *QUARTERS['tr'], 4)),
    'cypress': lambda d: [spray(d, *QUARTERS[q], (40, 64, 38), i) for i, q in enumerate(QUARTERS)],
    'baldcypress': lambda d: [spray(d, *QUARTERS[q], (106, 146, 66), 10 + i, needle=True) for i, q in enumerate(QUARTERS)],
    'mesquite': lambda d: [compound(d, *QUARTERS[q], [(104, 134, 76), (96, 126, 72), (118, 144, 84)], (70, 56, 44), 6, 11, 22, 20 + i) for i, q in enumerate(QUARTERS)],
    'paloverde': lambda d: [compound(d, *QUARTERS[q], [(150, 172, 82), (138, 164, 76)], (120, 156, 84), 7, 9, 30, 30 + i,
                                     flowers=(242, 214, 60) if q in ('tl', 'tr') else None) for i, q in enumerate(QUARTERS)],
    'sycamore': lambda d: [twig(d, *QUARTERS[q], L_sycamore, 3, [(88, 128, 54), (96, 136, 60), (80, 118, 50)], stem=(110, 96, 70), side=3,
                                spread=0.95, size=1.0, vein=(132, 160, 92), seed=40 + i) for i, q in enumerate(QUARTERS)],
    'magnolia': lambda d: (
        twig(d, *QUARTERS['bl'], L_magnolia, 4, [(40, 72, 36), (46, 80, 40), (36, 64, 32), (128, 92, 56)], stem=(96, 84, 64), side=3, spread=0.9, vein=(92, 122, 70), seed=50),
        twig(d, *QUARTERS['br'], L_magnolia, 4, [(42, 76, 38), (38, 68, 34), (50, 84, 42)], stem=(96, 84, 64), side=3, spread=0.9, vein=(92, 122, 70), seed=51),
        flower_magnolia(d, *QUARTERS['tl'], 52),
        twig(d, *QUARTERS['tr'], L_magnolia, 4, [(40, 72, 36), (46, 80, 40)], stem=(96, 84, 64), side=3, spread=0.9, vein=(92, 122, 70), seed=53)),
    'saguaro': lambda d: (flowers_saguaro(d, *QUARTERS['tl'], 60), flowers_saguaro(d, *QUARTERS['tr'], 61, fruit=True),
                          flowers_saguaro(d, *QUARTERS['bl'], 62), flowers_saguaro(d, *QUARTERS['br'], 63, fruit=True)),
}

# ------------------------------------------------------------------ bark

N = 1024
def pnoise(cells_x, cells_y, seed, w=N, h=N):
    g = np.random.default_rng(seed).random((cells_y, cells_x))
    ys, xs = np.linspace(0, cells_y, h, endpoint=False), np.linspace(0, cells_x, w, endpoint=False)
    yi, xi = np.floor(ys).astype(int), np.floor(xs).astype(int)
    fy, fx = ys - yi, xs - xi
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[yi % cells_y][:, xi % cells_x]; b = g[yi % cells_y][:, (xi + 1) % cells_x]
    c = g[(yi + 1) % cells_y][:, xi % cells_x]; e = g[(yi + 1) % cells_y][:, (xi + 1) % cells_x]
    top = a + (b - a) * fx[None, :]; bot = c + (e - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]

def fbm(cx, cy, seed, octaves=4):
    return sum(pnoise(cx * 2 ** k, cy * 2 ** k, seed + k) / 2 ** k for k in range(octaves)) / (2 - 2 ** (1 - octaves))

Y, X = np.mgrid[0:N, 0:N] / N

def plates(cols, rows, seed, crack=0.08):
    """Blocky bark plates between deep cracks (live oak, mesquite): a brick-like grid of cells,
    warped, each its own height."""
    wx = (fbm(4, 4, seed) - 0.5) * 1.6 / cols
    wy = (fbm(4, 4, seed + 9) - 0.5) * 1.6 / rows
    gx = (X + wx) * cols
    row = np.floor((Y + wy) * rows)
    gx = gx + 0.5 * (row % 2)
    fx, fy = gx % 1, ((Y + wy) * rows) % 1
    edge = np.minimum(np.minimum(fx, 1 - fx) * 2.2, np.minimum(fy, 1 - fy))
    cell = np.random.default_rng(seed).random((rows, cols * 2 + 2))
    lvl = cell[row.astype(int) % rows, np.floor(gx).astype(int) % (cols * 2 + 2)]
    return np.clip(edge / crack, 0, 1) ** 0.6 * (0.7 + 0.3 * lvl)

def fibres(freq, seed, warp=0.08):
    """Long vertical fibres and ridges (cypresses): stripes bent by noise."""
    w = (fbm(3, 2, seed) - 0.5) * warp
    return 0.5 + 0.5 * np.sin((X + w + (fbm(6, 1, seed + 5) - 0.5) * 0.04) * freq * 2 * math.pi)

def tint(h, lo, hi):
    lo, hi = np.array(lo, float), np.array(hi, float)
    return lo[None, None, :] + (hi - lo)[None, None, :] * h[..., None]

def bark_liveoak():
    h = plates(10, 13, 1, 0.09) * (0.8 + 0.2 * fbm(16, 16, 2))
    return tint(h, (22, 20, 18), (98, 90, 82)), h, 9

def bark_cypress():
    f = fibres(14, 3, 0.12) * 0.6 + fibres(37, 4, 0.06) * 0.4
    h = f * (0.75 + 0.25 * fbm(4, 10, 5))
    return tint(h, (52, 44, 38), (138, 124, 108)), h, 6

def bark_baldcypress():
    f = fibres(22, 6, 0.1) * 0.55 + fibres(60, 7, 0.05) * 0.45
    h = f * (0.7 + 0.3 * fbm(3, 12, 8))
    return tint(h, (70, 42, 30), (176, 120, 90)), h, 6

def bark_mesquite():
    h = (plates(12, 3, 9, 0.1) * 0.35 + fibres(18, 10, 0.14) * 0.65) * (0.75 + 0.25 * fbm(8, 8, 11))
    return tint(h, (30, 22, 18), (110, 86, 68)), h, 8

def bark_paloverde():
    m = fbm(6, 6, 12)
    h = 0.6 + 0.2 * m
    len_ = (np.abs(((Y * 40 + fbm(2, 2, 13) * 2) % 1) - 0.5) < 0.04) & (fbm(30, 8, 14) > 0.62)
    c = tint(m, (104, 140, 70), (150, 178, 96))
    c[len_] = c[len_] * 0.7
    h = np.where(len_, h - 0.15, h)
    return c, h, 2

def bark_sycamore():
    a, b = fbm(5, 5, 15), fbm(7, 7, 16)
    c = np.zeros((N, N, 3))
    cream, grey, olive, tan = np.array([226, 222, 204]), np.array([176, 172, 156]), np.array([128, 132, 98]), np.array([150, 128, 96])
    c[:] = cream
    c[a < 0.48] = grey; c[(a < 0.38)] = olive; c[(b < 0.36) & (a >= 0.48)] = tan
    c = c * (0.92 + 0.08 * fbm(32, 32, 17))[..., None]
    h = 0.5 + 0.1 * (a < 0.48) + 0.05 * fbm(32, 32, 17)
    return c, h, 3

def bark_magnolia():
    m = fbm(6, 6, 18)
    h = 0.55 + 0.12 * m + 0.05 * fbm(40, 40, 19)
    dots = fbm(60, 60, 20) > 0.74
    c = tint(m, (112, 108, 100), (164, 160, 150))
    c[dots] = c[dots] * 0.8
    return c, h, 3

RIBS = 16
def bark_saguaro():
    u = (X * RIBS) % 1
    crest = np.cos((u - 0.5) * math.pi) ** 2
    h = 0.15 + 0.85 * crest
    c = tint(h, (46, 76, 44), (96, 132, 82)) * (0.92 + 0.08 * fbm(4, 12, 21))[..., None]
    # areoles: little felt pads with spines, down each rib's crest
    v = (Y * 36 + 0.5 * np.floor(X * RIBS)) % 1
    pad = (np.abs(u - 0.5) < 0.09) & (np.abs(v - 0.5) < 0.08)
    c[pad] = (196, 184, 160)
    spine = (np.abs(u - 0.5) < 0.3) & (np.abs(v - 0.5 - (u - 0.5) * 0.4) < 0.012)
    c[spine] = (214, 206, 186)
    return c, h, 7

BARKS = {'liveoak': bark_liveoak, 'cypress': bark_cypress, 'baldcypress': bark_baldcypress, 'mesquite': bark_mesquite,
         'paloverde': bark_paloverde, 'sycamore': bark_sycamore, 'magnolia': bark_magnolia, 'saguaro': bark_saguaro}

def normals(h, k):
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * k
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * k
    n = np.stack([-gx, gy, np.ones_like(h)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)

for kind in LEAVES:
    if only and kind not in only: continue
    random.seed(sum(map(ord, kind)))
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    LEAVES[kind](ImageDraw.Draw(img))
    img.resize((S // 2, S // 2), Image.LANCZOS).save(f'{out}/{kind}_leaves.png', optimize=True)
    c, h, k = BARKS[kind]()
    Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)).save(f'{out}/{kind}_bark_color.jpg', quality=88)
    Image.fromarray(((normals(h, k) * 0.5 + 0.5) * 255).astype(np.uint8)).save(f'{out}/{kind}_bark_normal.jpg', quality=90)
    print('wrote', kind)
