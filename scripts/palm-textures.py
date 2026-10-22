# Palm pictures for the trees (public/textures/trees/palm_*), drawn from scratch (no scans):
#   palm_leaves.png       - four fronds on a transparent background, one in each quarter:
#                           bottom left  green feather frond (date and coconut palms)
#                           bottom right dead (brown) feather frond
#                           top left     green fan frond on its stalk (cabbage and Mexican fan palms)
#                           top right    dead fan frond (the hanging skirt)
#                           each with its base at the bottom middle of its quarter
#   palm_bark_color.jpg   - three bands across the picture, each tiling round a trunk on its own:
#   palm_bark_normal.jpg    u 0.0-0.4  smooth grey trunk with leaf-scar rings (Mexican fan, coconut)
#                           u 0.4-0.8  criss-cross "boots", the old leaf bases (cabbage, date)
#                           u 0.8-1.0  flat colour: coconuts (top half), frond-base fibre (bottom)
# palm.js maps its geometry onto these exact places, so keep the layout if you redraw them.
#   python scripts/palm-textures.py public/textures/trees
# Needs Python 3 with Pillow and NumPy.
import sys, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

out = sys.argv[1] if len(sys.argv) > 1 else '.'
random.seed(7)
rng = np.random.default_rng(7)

# ------------------------------------------------------------------ fronds
S = 2048            # drawn at twice the size, then halved (smooth edges)
C = S // 2          # one quarter
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

GREEN = [(108, 150, 62), (96, 140, 58), (122, 158, 70), (104, 146, 76)]
DEAD = [(236, 182, 124), (226, 170, 114), (244, 196, 136), (214, 160, 108)]   # (the game darkens leaves; these end up tan)

def jit(c, k=10):
    return tuple(max(0, min(255, v + random.randint(-k, k))) for v in c) + (255,)

def feather(ox, oy, cols):
    """A pinnate frond: a stalk up the middle, leaflets angled up and out, longest mid-way.
    ox, oy: top-left of the quarter (image coordinates, y down)."""
    cx, base, tip = ox + C / 2, oy + C - 12, oy + 14
    L = base - tip
    d.line([(cx, base), (cx, tip)], fill=jit((150, 150, 90)), width=10)
    n = 46
    for side in (-1, 1):
        for i in range(n):
            t = 0.14 + 0.85 * (i + random.random() * 0.4) / n          # along the frond
            y0 = base - t * L
            length = (C * 0.47) * math.sin(math.pi * min(0.97, 0.08 + t * 0.95)) ** 0.8 * (0.88 + 0.24 * random.random())
            ang = math.radians(68 - 30 * t + random.uniform(-5, 5))     # from the stalk: out at the base, swept forward at the tip
            ex, ey = cx + side * math.sin(ang) * length, y0 - math.cos(ang) * length
            w = 9 + 6 * (1 - t)
            px, py = -math.cos(ang) * w * side, -math.sin(ang) * w     # across the leaflet
            mx, my = (cx + ex) / 2, (y0 + ey) / 2
            d.polygon([(cx, y0), (mx + px, my + py), (ex, ey), (mx - px, my - py)], fill=jit(random.choice(cols)))
            d.line([(cx, y0), (ex, ey)], fill=jit(tuple(int(v * 0.8) for v in cols[0])), width=2)

def fan(ox, oy, cols, ragged):
    """A costapalmate fan on a long stalk: stiff segments fanning out from the top of the stalk,
    split apart towards their tips (with hanging threads on the Mexican fan palm)."""
    cx, base = ox + C / 2, oy + C - 12
    hx, hy = cx, oy + C * 0.47                                     # where the fan starts
    d.line([(cx, base), (hx, hy)], fill=jit((150, 145, 90)), width=16)
    R = C * 0.45
    n = 44
    for i in range(n):
        a0 = math.radians(-112 + 224 * i / n)
        a1 = math.radians(-112 + 224 * (i + 1) / n)
        am = (a0 + a1) / 2
        r = R * (0.88 + 0.12 * random.random())
        split = 0.62 + 0.12 * random.random()                        # segments free from here out
        pts = [(hx, hy)]
        for f, a in ((split, a0), (1, am - 0.03), (1, am + 0.03), (split, a1)):
            pts.append((hx + math.sin(a) * r * f, hy - math.cos(a) * r * f))
        d.polygon(pts, fill=jit(random.choice(cols), 14))
        d.line([(hx, hy), (hx + math.sin(am) * r, hy - math.cos(am) * r)], fill=jit(tuple(int(v * 0.82) for v in cols[0])), width=3)
        if ragged and random.random() < 0.6:                       # threads from the split ends
            x0, y0 = hx + math.sin(am) * r * 0.8, hy - math.cos(am) * r * 0.8
            d.line([(x0, y0), (x0 + random.uniform(-14, 14), y0 + random.uniform(20, 70))], fill=jit((220, 205, 170)), width=2)

feather(0, C, GREEN)                 # bottom left
feather(C, C, DEAD)                  # bottom right
fan(0, 0, GREEN, ragged=True)        # top left
fan(C, 0, DEAD, ragged=False)        # top right
img = img.resize((S // 2, S // 2), Image.LANCZOS)
img.save(f'{out}/palm_leaves.png', optimize=True)

# ------------------------------------------------------------------ bark
W = H = 1024
A, B = int(W * 0.4), int(W * 0.8)

def pnoise(w, h, cells_x, cells_y, seed):
    """Smooth noise that tiles: random values on a grid, wrapped, cubic-blended."""
    g = np.random.default_rng(seed).random((cells_y, cells_x))
    ys, xs = np.linspace(0, cells_y, h, endpoint=False), np.linspace(0, cells_x, w, endpoint=False)
    yi, xi = np.floor(ys).astype(int), np.floor(xs).astype(int)
    fy, fx = ys - yi, xs - xi
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[yi % cells_y][:, xi % cells_x]; b = g[yi % cells_y][:, (xi + 1) % cells_x]
    c = g[(yi + 1) % cells_y][:, xi % cells_x]; e = g[(yi + 1) % cells_y][:, (xi + 1) % cells_x]
    top = a + (b - a) * fx[None, :]; bot = c + (e - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]

def fbm(w, h, base_x, base_y, seed):
    return sum(pnoise(w, h, base_x * 2 ** k, base_y * 2 ** k, seed + k) / 2 ** k for k in range(4)) / 1.875

# Rings: horizontal leaf scars every ~5 cm (the band is about 2 m tall), fine vertical fibres
w = A
y = np.arange(H)[:, None] / H
x = np.arange(w)[None, :] / w
wob = fbm(w, H, 4, 8, 11) - 0.5
ring = np.abs(np.sin(math.pi * (y * 40 + wob * 0.25))) ** 6
fib = fbm(w, H, 64, 6, 21)
h_r = 0.55 - 0.35 * ring + 0.12 * (fib - 0.5) + 0.1 * (fbm(w, H, 6, 12, 31) - 0.5)
col_r = np.stack([h_r * 150 + 30, h_r * 142 + 26, h_r * 128 + 22], -1)

# Boots: a lattice of old leaf bases, 4 across the band and 8 down it, each a wedge whose upper
# edge stands out (and catches the light) over the one above
w = B - A
y = np.arange(H)[:, None] / H * 8
x = np.arange(w)[None, :] / w * 4
u = x + y * 0.5                       # diagonal lattice
v = y - x * 0.5 * 0 + 0
cu, cv = u % 1, (y + 0.5 * np.floor(u)) % 1
edge = np.clip(1 - cv * 1.15, 0, 1) ** 0.7 * (0.6 + 0.4 * np.sin(math.pi * cu))
gap = (cv > 0.88) | (cu < 0.05) | (cu > 0.95)
h_b = 0.25 + 0.6 * edge + 0.15 * (fbm(w, H, 32, 16, 41) - 0.5)
h_b[gap] *= 0.35
col_b = np.stack([h_b * 150 + 40, h_b * 112 + 30, h_b * 76 + 20], -1)

# Flat patches: coconuts (top half of the image = top of the uv), fibre (bottom)
w = W - B
col_c = np.zeros((H, w, 3))
n = fbm(w, H, 4, 16, 51)
col_c[: H // 2] = np.stack([96 + 30 * n[: H // 2], 104 + 26 * n[: H // 2], 46 + 14 * n[: H // 2]], -1)
col_c[H // 2:] = np.stack([118 + 40 * n[H // 2:], 88 + 30 * n[H // 2:], 56 + 20 * n[H // 2:]], -1)
h_c = 0.5 + 0.1 * (n - 0.5)

color = np.concatenate([col_r, col_b, col_c], 1).clip(0, 255).astype(np.uint8)
Image.fromarray(color).save(f'{out}/palm_bark_color.jpg', quality=88)

# Normals from the height, each band on its own (so their edges don't make a ridge)
def normals(h, k):
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * k
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * k
    n = np.stack([-gx, gy, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n
nm = np.concatenate([normals(h_r, 6), normals(h_b, 10), normals(h_c, 1)], 1)
Image.fromarray(((nm * 0.5 + 0.5) * 255).astype(np.uint8)).save(f'{out}/palm_bark_normal.jpg', quality=90)
print('wrote palm_leaves.png, palm_bark_color.jpg, palm_bark_normal.jpg to', out)
