# Pictures for the shrubs, grasses and flowers planted by public/js/plants.js, drawn from scratch
# (no scans), in their true colours:
#   plants.png - 2048 x 2048, a 4 x 4 grid of 512-pixel cells on a transparent background
#     row 0 (top): boxwood, azalea (pink), rhododendron (purple), holly (red berries)
#     row 1:       fern frond, fountain grass, pampas grass, pink muhly grass
#     row 2:       flowers: red, yellow, purple spikes (salvia), white and pink
#     row 3:       sagebrush, yucca, saw palmetto fan, prickly pear
# Shrub cells (boxwood, azalea, rhododendron, holly, sagebrush) are round leafy clusters filling the
# cell, for cards set over a dome; the others stand on the bottom middle of their cell, for upright
# cards. plants.js maps its cards onto these cells (CELL there), so keep the layout if you redraw.
#   python scripts/plant-textures.py public/textures/plants
# Needs Python 3 with Pillow.
import sys, math, random
from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else '.'
C = 1024            # one cell, drawn at twice the size and halved (smooth edges)
S = C * 4

def jit(c, k=12, rnd=random):
    return tuple(max(0, min(255, int(v + rnd.randint(-k, k)))) for v in c[:3]) + (255,)

def rot(pts, ang, x, y):
    c, s = math.cos(ang), math.sin(ang)
    return [(x + px * c - py * s, y + px * s + py * c) for px, py in pts]

def ellipse_leaf(L, W, tip=1.0):
    """Leaf outline, base at (0, 0), pointing up (-y)."""
    pts = []
    for i in range(17):
        t = i / 16
        pts.append((W / 2 * math.sin(math.pi * t) ** 0.85 * (1 - 0.35 * t * tip), -L * t))
    return pts + [(-x, y) for x, y in reversed(pts)]

def leaf(d, x, y, ang, L, W, col, tip=1.0, vein=None):
    d.polygon(rot(ellipse_leaf(L, W, tip), ang, x, y), fill=col)
    if vein: d.line(rot([(0, 0), (0, -L * 0.85)], ang, x, y), fill=vein, width=2)

def disc(d, x, y, r, col):
    d.ellipse([x - r, y - r, x + r, y + r], fill=col)

def blob_points(rnd, ox, oy, n, fill=0.44):
    """Spots inside a round cluster in the cell, denser towards the middle."""
    cx, cy = ox + C / 2, oy + C / 2
    for _ in range(n):
        a, r = rnd.uniform(0, 2 * math.pi), C * fill * math.sqrt(rnd.random())
        yield cx + math.cos(a) * r, cy + math.sin(a) * r, a

# ------------------------------------------------------------------ shrubs (round clusters)

def boxwood(d, ox, oy, rnd):
    for x, y, a in blob_points(rnd, ox, oy, 1500):
        leaf(d, x, y, a + rnd.uniform(-1, 1), rnd.uniform(36, 50), rnd.uniform(22, 30), jit(rnd.choice([(46, 84, 34), (56, 96, 40), (38, 72, 30), (70, 108, 46)]), 10, rnd), 0.3)

def azalea(d, ox, oy, rnd, petals, leaves=(48, 82, 36)):
    for x, y, a in blob_points(rnd, ox, oy, 650):
        leaf(d, x, y, a + rnd.uniform(-1, 1), rnd.uniform(50, 70), rnd.uniform(22, 30), jit(rnd.choice([leaves, (58, 92, 42), (40, 72, 32)]), 10, rnd), 0.6)
    for x, y, _ in blob_points(rnd, ox, oy, 150, 0.42):
        col = jit(rnd.choice(petals), 12, rnd)
        a0 = rnd.uniform(0, 2 * math.pi)
        for k in range(5):
            leaf(d, x, y, a0 + k * 2 * math.pi / 5, 34, 30, col, -0.4)
        disc(d, x, y, 7, tuple(int(v * 0.75) for v in col[:3]) + (255,))

def rhododendron(d, ox, oy, rnd):
    for x, y, a in blob_points(rnd, ox, oy, 260):
        leaf(d, x, y, a + rnd.uniform(-0.6, 0.6), rnd.uniform(120, 160), rnd.uniform(40, 52), jit(rnd.choice([(34, 70, 34), (42, 80, 38), (30, 60, 30)]), 8, rnd), 0.7, vein=(70, 104, 62))
    # trusses: balls of trumpet flowers
    for x, y, _ in blob_points(rnd, ox, oy, 14, 0.36):
        for _ in range(18):
            fx, fy = x + rnd.gauss(0, 30), y + rnd.gauss(0, 30)
            col = jit(rnd.choice([(170, 70, 170), (190, 92, 186), (150, 58, 150)]), 12, rnd)
            a0 = rnd.uniform(0, 2 * math.pi)
            for k in range(5):
                leaf(d, fx, fy, a0 + k * 2 * math.pi / 5, 30, 26, col, -0.4)
            disc(d, fx, fy, 5, (230, 200, 120, 255))

def holly(d, ox, oy, rnd):
    for x, y, a in blob_points(rnd, ox, oy, 420):
        L, W = rnd.uniform(80, 100), rnd.uniform(40, 50)
        right = [((W * 0.5 * math.sin(math.pi * i / 12) ** 0.7) * (1.25 if i % 2 else 0.85), -L * i / 12) for i in range(13)]
        d.polygon(rot(right + [(-px, py) for px, py in reversed(right)], a + rnd.uniform(-1, 1), x, y), fill=jit(rnd.choice([(26, 64, 34), (32, 74, 40), (40, 84, 46)]), 8, rnd))
    for x, y, _ in blob_points(rnd, ox, oy, 70, 0.4):
        for _ in range(rnd.randint(2, 4)):
            bx, by = x + rnd.uniform(-16, 16), y + rnd.uniform(-16, 16)
            disc(d, bx, by, rnd.uniform(9, 12), jit((196, 30, 34), 14, rnd))

def sagebrush(d, ox, oy, rnd):
    # grey-blue, finely divided leaves on many twiggy stems
    for x, y, a in blob_points(rnd, ox, oy, 2200):
        leaf(d, x, y, a + rnd.uniform(-1.2, 1.2), rnd.uniform(28, 40), rnd.uniform(9, 13), jit(rnd.choice([(150, 162, 140), (132, 146, 126), (170, 178, 158), (118, 130, 112)]), 10, rnd), 0.2)

# ------------------------------------------------------------------ upright plants (base at the bottom middle)

def fern(d, ox, oy, rnd):
    bx, by = ox + C / 2, oy + C - 8
    pts, x, y, a = [], bx, by, 0
    for i in range(40):
        a += 0.012
        x += math.sin(a) * C * 0.024; y -= math.cos(a) * C * 0.024
        pts.append((x, y, a))
    d.line([(bx, by)] + [(p[0], p[1]) for p in pts], fill=(80, 96, 50, 255), width=8)
    for i, (x, y, a) in enumerate(pts[3:], 3):
        t = i / len(pts)
        L = C * 0.2 * math.sin(math.pi * min(1, t * 1.05)) ** 0.7 + 10
        for side in (-1, 1):
            pa = a + side * 1.15
            ex, ey = x + math.sin(pa) * L, y - math.cos(pa) * L
            d.line([(x, y), (ex, ey)], fill=(70, 110, 46, 255), width=3)
            n = max(2, int(L / 15))
            for k in range(n):
                f = (k + 0.5) / n
                px, py = x + (ex - x) * f, y + (ey - y) * f
                for s2 in (-1, 1):
                    leaf(d, px, py, pa + s2 * 1.2, 22 * (1 - f * 0.5) + 6, 11, jit((66, 120, 46), 10, rnd), 0.2)

def grass_blades(d, ox, oy, rnd, n, cols, height=0.92, spread=1.0, width=7):
    bx, by = ox + C / 2, oy + C - 6
    tips = []
    for _ in range(n):
        a = rnd.gauss(0, 0.42) * spread
        L = C * height * rnd.uniform(0.55, 1.0)
        x, y, pts = bx + rnd.uniform(-24, 24), by, []
        ang = a * 0.3
        for k in range(14):
            ang += a * 0.12 + 0.012 * math.copysign(1, a)
            x += math.sin(ang) * L / 14; y -= math.cos(ang) * L / 14
            pts.append((x, y))
            if not (ox + 4 < x < ox + C - 4 and oy + 4 < y): break
        d.line([(bx, by)] + pts, fill=jit(rnd.choice(cols), 12, rnd), width=width)
        tips.append(pts[-1])
    return tips

def plume(d, x, y, rnd, cols, L, W, ang=0.0, n=160):
    for _ in range(n):
        t = rnd.random()
        px, py = x + rnd.gauss(0, W * (0.4 + 0.6 * math.sin(math.pi * t))), y - t * L
        px, py = x + (px - x) * math.cos(ang) - (py - y) * math.sin(ang), y + (px - x) * math.sin(ang) + (py - y) * math.cos(ang)
        disc(d, px, py, rnd.uniform(4, 8), jit(rnd.choice(cols), 10, rnd))

def fountain_grass(d, ox, oy, rnd):
    tips = grass_blades(d, ox, oy, rnd, 120, [(96, 128, 64), (112, 140, 72), (130, 150, 86)], 0.8, 1.2, 6)
    for x, y in tips[::5]:
        plume(d, x, y + 40, rnd, [(196, 176, 140), (210, 192, 158), (176, 150, 118)], 90, 14, rnd.uniform(-0.5, 0.5), 90)

def pampas(d, ox, oy, rnd):
    grass_blades(d, ox, oy, rnd, 110, [(104, 128, 72), (122, 140, 84), (136, 146, 96)], 0.6, 1.3, 7)
    bx, by = ox + C / 2, oy + C - 6
    for _ in range(9):
        a = rnd.gauss(0, 0.16)
        L = C * rnd.uniform(0.62, 0.8)
        ex, ey = bx + math.sin(a) * L, by - math.cos(a) * L
        d.line([(bx, by), (ex, ey)], fill=(170, 160, 120, 255), width=6)
        plume(d, ex, ey + C * 0.2, rnd, [(238, 232, 214), (226, 216, 190), (246, 242, 230)], C * 0.26, 34, a, 420)

def pink_muhly(d, ox, oy, rnd):
    grass_blades(d, ox, oy, rnd, 90, [(92, 120, 70), (104, 132, 76)], 0.45, 1.2, 5)
    # a pink haze of fine seed heads over the clump
    cx, cy = ox + C / 2, oy + C * 0.5
    for _ in range(4200):
        a, r = rnd.uniform(0, 2 * math.pi), math.sqrt(rnd.random())
        x, y = cx + math.cos(a) * r * C * 0.44, cy + math.sin(a) * r * C * 0.34 - C * 0.04
        if y > oy + C * 0.86: continue
        d.line([(x, y), (x + rnd.uniform(-14, 14), y + rnd.uniform(-16, 16))], fill=jit(rnd.choice([(214, 120, 170), (226, 146, 186), (196, 104, 156)]), 14, rnd), width=8)

def flowers(d, ox, oy, rnd, cols, kind='round'):
    """A clump of flowers: leaves low down, heads on short stems above them."""
    bx, by = ox + C / 2, oy + C - 6
    for _ in range(150):
        x, y = bx + rnd.gauss(0, C * 0.2), by - abs(rnd.gauss(0, C * 0.22))
        leaf(d, x, y + 20, rnd.uniform(-1.3, 1.3), rnd.uniform(70, 110), rnd.uniform(34, 46), jit(rnd.choice([(52, 96, 40), (62, 108, 46), (44, 84, 36)]), 10, rnd), 0.6)
    for _ in range(80 if kind == 'round' else 34):
        x, top = bx + rnd.gauss(0, C * 0.2), oy + C * rnd.uniform(0.3, 0.62)
        x = min(ox + C - 40, max(ox + 40, x))
        d.line([(x, by - 30), (x, top)], fill=(70, 110, 50, 255), width=5)
        col = jit(rnd.choice(cols), 14, rnd)
        if kind == 'spike':
            for k in range(14):
                fy = top + k * 16
                for s in (-1, 1): leaf(d, x, fy, s * 1.1, 24, 16, col, -0.2)
        else:
            a0 = rnd.uniform(0, 2 * math.pi)
            for k in range(6):
                leaf(d, x, top, a0 + k * math.pi / 3, 40, 34, col, -0.4)
            disc(d, x, top, 10, (70, 50, 30, 255) if col[0] > 200 and col[1] > 160 else (236, 210, 90, 255))

def yucca(d, ox, oy, rnd):
    bx, by = ox + C / 2, oy + C - 6
    # a tall stalk of creamy bell flowers
    top = oy + C * 0.06
    d.line([(bx, by - C * 0.3), (bx, top)], fill=(120, 130, 80, 255), width=12)
    for k in range(70):
        t = rnd.uniform(0, 0.42)
        y = top + t * C
        x = bx + rnd.gauss(0, 34 * (0.3 + t * 2))
        disc(d, x, y, rnd.uniform(12, 17), jit((238, 234, 206), 8, rnd))
    # stiff sword leaves from the bottom middle
    for _ in range(64):
        a = rnd.uniform(-1.35, 1.35)
        L = C * rnd.uniform(0.38, 0.56) * (1 - 0.3 * abs(a) / 1.35)
        W = rnd.uniform(26, 36)
        tipx, tipy = bx + math.sin(a) * L, by - math.cos(a) * L
        nx, ny = math.cos(a), math.sin(a)
        d.polygon([(bx - nx * W / 2, by - ny * W / 2), (bx + nx * W / 2, by + ny * W / 2), (tipx, tipy)], fill=jit(rnd.choice([(90, 116, 76), (104, 128, 84), (78, 104, 68)]), 10, rnd))

def palmetto(d, ox, oy, rnd):
    """Saw palmetto: a stiff fan of folded leaflets on a stalk."""
    bx, by = ox + C / 2, oy + C - 6
    hx, hy = bx, oy + C * 0.56
    d.line([(bx, by), (hx, hy)], fill=(110, 116, 70, 255), width=12)
    for k in range(25):
        a = -1.5 + 3.0 * k / 24 + rnd.uniform(-0.03, 0.03)
        L = C * rnd.uniform(0.42, 0.48)
        tx, ty = hx + math.sin(a) * L, hy - math.cos(a) * L
        nx, ny = math.cos(a), math.sin(a)
        col = jit(rnd.choice([(78, 116, 66), (88, 128, 72), (70, 104, 60)]), 8, rnd)
        d.polygon([(hx - nx * 6, hy - ny * 6), (hx + nx * 6, hy + ny * 6), (tx + nx * 14, ty + ny * 14), (tx, ty - 6), (tx - nx * 14, ty - ny * 14)], fill=col)
        d.line([(hx, hy), (tx, ty)], fill=tuple(int(v * 0.8) for v in col[:3]) + (255,), width=3)

def prickly_pear(d, ox, oy, rnd):
    """Flat oval pads growing out of each other, with spine dots and a few yellow flowers."""
    pads = []
    def pad(x, y, a, L, depth):
        W = L * 0.72
        cx, cy = x + math.sin(a) * L / 2, y - math.cos(a) * L / 2
        pts = [(cx + math.cos(t) * W / 2 * math.cos(a) - math.sin(t) * L / 2 * math.sin(a), cy + math.cos(t) * W / 2 * math.sin(a) + math.sin(t) * L / 2 * math.cos(a)) for t in [i * 2 * math.pi / 30 for i in range(30)]]
        pads.append((pts, cx, cy, W, L))
        if depth < 3:
            for _ in range(rnd.randint(1, 2)):
                b = a + rnd.uniform(-0.8, 0.8)
                tx, ty = cx + math.sin(a + rnd.uniform(-0.7, 0.7)) * L * 0.4, cy - math.cos(a) * L * 0.4
                pad(tx, ty, b, L * rnd.uniform(0.75, 0.92), depth + 1)
    for a in (-0.5, 0.1, 0.6):
        pad(ox + C / 2 + a * 120, oy + C - 6, a, C * 0.3, 0)
    for pts, cx, cy, W, L in pads:
        if not all(ox < x < ox + C and oy < y < oy + C for x, y in pts): continue
        col = jit(rnd.choice([(92, 132, 70), (104, 142, 76), (84, 120, 64)]), 8, rnd)
        d.polygon(pts, fill=col)
        for _ in range(int(W * L / 1200)):
            x, y = cx + rnd.uniform(-0.35, 0.35) * W, cy + rnd.uniform(-0.38, 0.38) * L
            disc(d, x, y, 3, (220, 214, 180, 255))
        if rnd.random() < 0.3:
            top = min(pts, key=lambda p: p[1])
            for k in range(8): leaf(d, top[0], top[1], k * math.pi / 4, 28, 22, jit((240, 206, 60), 8, rnd), -0.3)

# ------------------------------------------------------------------ the atlas

CELLS = [
    lambda d, x, y, r: boxwood(d, x, y, r),
    lambda d, x, y, r: azalea(d, x, y, r, [(236, 110, 150), (226, 92, 138), (244, 140, 170)]),
    lambda d, x, y, r: rhododendron(d, x, y, r),
    lambda d, x, y, r: holly(d, x, y, r),
    lambda d, x, y, r: fern(d, x, y, r),
    lambda d, x, y, r: fountain_grass(d, x, y, r),
    lambda d, x, y, r: pampas(d, x, y, r),
    lambda d, x, y, r: pink_muhly(d, x, y, r),
    lambda d, x, y, r: flowers(d, x, y, r, [(214, 40, 40), (196, 26, 36), (232, 66, 56)]),
    lambda d, x, y, r: flowers(d, x, y, r, [(246, 196, 40), (240, 170, 30), (250, 214, 70)]),
    lambda d, x, y, r: flowers(d, x, y, r, [(110, 76, 190), (128, 92, 206), (96, 64, 170)], 'spike'),
    lambda d, x, y, r: flowers(d, x, y, r, [(246, 244, 236), (240, 170, 200), (250, 250, 244), (230, 140, 184)]),
    lambda d, x, y, r: sagebrush(d, x, y, r),
    lambda d, x, y, r: yucca(d, x, y, r),
    lambda d, x, y, r: palmetto(d, x, y, r),
    lambda d, x, y, r: prickly_pear(d, x, y, r),
]

img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
for i, draw in enumerate(CELLS):
    cell = Image.new('RGBA', (C, C), (0, 0, 0, 0))
    draw(ImageDraw.Draw(cell), 0, 0, random.Random(1000 + i))
    img.paste(cell, ((i % 4) * C, (i // 4) * C))
img.resize((S // 2, S // 2), Image.LANCZOS).save(f'{out}/plants.png', optimize=True)
print('wrote plants.png')
