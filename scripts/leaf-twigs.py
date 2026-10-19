# Leafy-twig pictures for the trees (public/textures/trees/*_leaves.png), in the same form as
# ez-tree's own leaf pictures: one twig on a transparent background, its base at the bottom middle.
# Made from ambientCG's CC0 scans of single leaves (download the 1K-JPG zip of each and pass its
# _Color.jpg and _Opacity.jpg):
#   maple:  LeafSet010  python scripts/leaf-twigs.py C.jpg O.jpg maple_leaves.png 5 34 0.15 0 0.6
#   willow: LeafSet013  python scripts/leaf-twigs.py C.jpg O.jpg willow_leaves.png 7 60 0.15 0.55 0.3
#   birch:  LeafSet014  python scripts/leaf-twigs.py C.jpg O.jpg birch_leaves.png 9 45 0.1 0.15 0.45
# Needs Python 3 with Pillow and NumPy.
#
import sys, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance

color, opacity, out = sys.argv[1], sys.argv[2], sys.argv[3]
seed, count, size = int(sys.argv[4]), int(sys.argv[5]), float(sys.argv[6])
droop = float(sys.argv[7]) if len(sys.argv) > 7 else 0.0   # leaves hanging down (willow)
green = float(sys.argv[8]) if len(sys.argv) > 8 else 0.0   # push yellow-green scans to summer green
random.seed(seed)

col = Image.open(color).convert('RGB')
op = Image.open(opacity).convert('L').resize(col.size)
a = np.array(op) > 40
s = 4
small = a[::s, ::s]
lab = np.zeros(small.shape, int); n = 0
for y in range(small.shape[0]):
    for x in range(small.shape[1]):
        if small[y, x] and not lab[y, x]:
            n += 1; stack = [(y, x)]; lab[y, x] = n
            while stack:
                cy, cx = stack.pop()
                for dy, dx in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1),(1,-1),(-1,1)):
                    yy, xx = cy+dy, cx+dx
                    if 0 <= yy < small.shape[0] and 0 <= xx < small.shape[1] and small[yy, xx] and not lab[yy, xx]:
                        lab[yy, xx] = n; stack.append((yy, xx))
rgba = col.copy(); rgba.putalpha(op)
leaves = []
for k in range(1, n + 1):
    ys, xs = np.nonzero(lab == k)
    if len(ys) < 200: continue
    leaf = rgba.crop((xs.min()*s - 4, ys.min()*s - 4, xs.max()*s + 8, ys.max()*s + 8))
    # Stand each leaf up along its long axis (scans can lie at any angle), the wider end at the top
    m = np.asarray(leaf.getchannel('A')) > 40
    yy, xx = np.nonzero(m)
    cx, cy = xx.mean(), yy.mean()
    cov = np.cov(np.stack([xx - cx, yy - cy]))
    w_, v_ = np.linalg.eigh(cov); ax = v_[:, 1]
    turn = math.degrees(math.atan2(ax[1], ax[0])) + 90
    leaf = leaf.rotate(turn, expand=True, resample=Image.BICUBIC)
    leaf = leaf.crop(leaf.getbbox())
    m = np.asarray(leaf.getchannel('A')) > 40
    rows = m.sum(1); half = len(rows) // 2
    if rows[:half].sum() < rows[half:].sum(): leaf = leaf.rotate(180)
    leaves.append(leaf)
print('leaves found:', len(leaves))

if green:
    def greener(im):
        arr = np.asarray(im).astype(np.float32)
        rgb = arr[..., :3]
        l = rgb @ np.array([0.3, 0.59, 0.11])
        target = np.stack([l*0.62, l*0.92, l*0.38], -1)
        arr[..., :3] = rgb * (1 - green) + target * green
        return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGBA')
    leaves = [greener(l) for l in leaves]

S = 1024
card = Image.new('RGBA', (S, S), (0, 0, 0, 0))
twig = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(twig)
BARK = (70, 56, 44, 255)

def curve(x0, y0, ang, length, bend, steps=24):
    pts = [(x0, y0)]
    for i in range(steps):
        ang += bend / steps
        x0 += math.cos(ang) * length / steps; y0 += math.sin(ang) * length / steps
        pts.append((x0, y0))
    return pts

def draw_stem(pts, w0, w1):
    for i in range(1, len(pts)):
        d.line([pts[i-1], pts[i]], fill=BARK, width=max(1, int(w0 + (w1 - w0) * i / len(pts))))

main = curve(S*0.5, S*0.99, -math.pi/2 + random.uniform(-0.2, 0.2), S*0.8, random.uniform(-0.4, 0.4))
draw_stem(main, 8, 3)
stems = [main]
for t in (0.25, 0.38, 0.5, 0.62, 0.74):
    i = int(t * (len(main) - 1)); x, y = main[i]
    side = 1 if len(stems) % 2 else -1
    ang = math.atan2(main[i+1][1]-y, main[i+1][0]-x) + side * random.uniform(0.6, 0.95)
    sh = curve(x, y, ang, S*random.uniform(0.22, 0.32), -side*random.uniform(0.2, 0.5), 12)
    draw_stem(sh, 4, 2); stems.append(sh)

spots = []
for st in stems:
    for i in range(2, len(st), 2):
        spots.append((st, i))
random.shuffle(spots)
placed = 0
for st, i in spots:
    if placed >= count: break
    x, y = st[i]; nx, ny = st[min(i+1, len(st)-1)]
    ang = math.atan2(ny - y, nx - x) + random.choice((-1, 1)) * random.uniform(0.4, 1.0)
    if droop: ang = ang * (1 - droop) + (math.pi / 2) * droop   # hanging down
    leaf = random.choice(leaves)
    sc = S * size * random.uniform(0.75, 1.1) / max(leaf.size)
    w, h = max(1, int(leaf.width*sc*random.uniform(0.55, 1.0))), max(1, int(leaf.height*sc))  # tilted away: narrower
    lf = leaf.resize((w, h), Image.LANCZOS)
    if random.random() < 0.5: lf = lf.transpose(Image.FLIP_LEFT_RIGHT)
    shade = random.uniform(0.62, 1.0)
    rgb = ImageEnhance.Brightness(lf.convert('RGB')).enhance(shade); rgb.putalpha(lf.getchannel('A')); lf = rgb
    R = int(math.hypot(lf.width, lf.height)) + 4  # (room for the leaf turned any way about its stalk)
    big = Image.new('RGBA', (2*R, 2*R), (0,0,0,0))
    big.paste(lf, (R - lf.width//2, R - lf.height), lf)
    big = big.rotate(-math.degrees(ang + math.pi/2), resample=Image.BICUBIC)
    bb = big.getbbox()
    if not bb: continue
    ox, oy = int(x - big.width/2), int(y - big.height/2)
    # (whole leaves only: none cut off by the edge of the card)
    if ox + bb[0] < 2 or oy + bb[1] < 2 or ox + bb[2] > S - 2 or oy + bb[3] > S - 2: continue
    card.alpha_composite(big, (ox, oy)); placed += 1
print('placed', placed)
Image.alpha_composite(twig, card).save(out, optimize=True)
