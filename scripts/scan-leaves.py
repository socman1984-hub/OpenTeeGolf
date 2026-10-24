# Real leaves for the trees grown by public/js/grow.js: replaces the foliage quarters of
# public/textures/trees/<kind>_leaves.png (drawn by scripts/tree-textures.py, which sets out the
# four-quarter layout) with leafy twigs made from ambientCG's CC0 scans of single leaves, each
# species' leaves brought to its own shade of green. Quarters holding Spanish moss or flowers stay
# as drawn. Download each set's 1K-JPG zip, put its _Color.jpg and _Opacity.jpg in one folder:
#   python scripts/scan-leaves.py <folder> public/textures/trees [kind ...]
# Sets: live oak LeafSet001, magnolia LeafSet022, sycamore LeafSet025, Monterey and bald cypress
# LeafSet019 (sprays of scale leaves rather than leaves). Mesquite and palo verde keep their drawn
# leaves: no scan has their fine, feathery leaflets.
# The twigs are built as in scripts/leaf-twigs.py. Needs Python 3 with Pillow and NumPy.
import sys, os, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance

src, out = sys.argv[1], sys.argv[2]
only = set(sys.argv[3:])

# kind: leaf set, quarters to replace, mean colour of a leaf, leaves on a twig, leaf size (share
# of the twig's height), droop (0 held out .. 1 hanging), stem colour
KINDS = {
    'liveoak':     ('LeafSet001', ('bl', 'br'), (60, 78, 40), 90, 0.13, 0.0, (80, 70, 58)),
    'magnolia':    ('LeafSet022', ('bl', 'br'), (40, 72, 36), 18, 0.3, 0.1, (96, 84, 64)),
    'sycamore':    ('LeafSet025', ('bl', 'br', 'tl', 'tr'), (88, 128, 54), 14, 0.32, 0.1, (110, 96, 70)),
    'cypress':     ('LeafSet019', ('bl', 'br', 'tl', 'tr'), (40, 64, 38), 18, 0.42, 0.0, (80, 64, 50)),
    'baldcypress': ('LeafSet019', ('bl', 'br', 'tl', 'tr'), (104, 144, 64), 16, 0.4, 0.35, (110, 80, 60)),
}
QUARTERS = {'bl': (0, 512), 'br': (512, 512), 'tl': (0, 0), 'tr': (512, 0)}

def load_leaves(name):
    """Each leaf of a scan, cut out and stood up along its long axis, the wider end at the top."""
    col = Image.open(os.path.join(src, f'{name}_1K-JPG_Color.jpg')).convert('RGB')
    op = Image.open(os.path.join(src, f'{name}_1K-JPG_Opacity.jpg')).convert('L').resize(col.size)
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
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
                        yy, xx = cy + dy, cx + dx
                        if 0 <= yy < small.shape[0] and 0 <= xx < small.shape[1] and small[yy, xx] and not lab[yy, xx]:
                            lab[yy, xx] = n; stack.append((yy, xx))
    rgba = col.copy(); rgba.putalpha(op)
    leaves = []
    for k in range(1, n + 1):
        ys, xs = np.nonzero(lab == k)
        if len(ys) < 200: continue
        leaf = rgba.crop((xs.min() * s - 4, ys.min() * s - 4, xs.max() * s + 8, ys.max() * s + 8))
        m = np.asarray(leaf.getchannel('A')) > 40
        yy, xx = np.nonzero(m)
        cov = np.cov(np.stack([xx - xx.mean(), yy - yy.mean()]))
        ax = np.linalg.eigh(cov)[1][:, 1]
        leaf = leaf.rotate(math.degrees(math.atan2(ax[1], ax[0])) + 90, expand=True, resample=Image.BICUBIC)
        leaf = leaf.crop(leaf.getbbox())
        rows = (np.asarray(leaf.getchannel('A')) > 40).sum(1); half = len(rows) // 2
        if rows[:half].sum() < rows[half:].sum(): leaf = leaf.rotate(180)
        leaves.append(leaf)
    return leaves

def recolour(leaves, target):
    """Bring the leaves' average colour to the species' own, keeping each leaf's veins and shading."""
    px = np.concatenate([np.asarray(l)[np.asarray(l)[..., 3] > 128][:, :3] for l in leaves]).astype(np.float32)
    k = np.array(target, np.float32) / np.maximum(px.mean(0), 1)
    out = []
    for l in leaves:
        a = np.asarray(l).astype(np.float32)
        a[..., :3] = np.clip(a[..., :3] * k, 0, 255)
        out.append(Image.fromarray(a.astype(np.uint8), 'RGBA'))
    return out

def twig(leaves, seed, count, size, droop, stem):
    """One leafy twig on a 1024 x 1024 transparent card, its base at the bottom middle."""
    random.seed(seed)
    S = 1024
    card = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    tw = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(tw)
    def curve(x0, y0, ang, length, bend, steps=24):
        pts = [(x0, y0)]
        for _ in range(steps):
            ang += bend / steps
            x0 += math.cos(ang) * length / steps; y0 += math.sin(ang) * length / steps
            pts.append((x0, y0))
        return pts
    def draw_stem(pts, w0, w1):
        for i in range(1, len(pts)):
            d.line([pts[i - 1], pts[i]], fill=stem + (255,), width=max(1, int(w0 + (w1 - w0) * i / len(pts))))
    main = curve(S * 0.5, S * 0.99, -math.pi / 2 + random.uniform(-0.2, 0.2), S * 0.8, random.uniform(-0.4, 0.4))
    draw_stem(main, 8, 3)
    stems = [main]
    for t in (0.25, 0.38, 0.5, 0.62, 0.74):
        i = int(t * (len(main) - 1)); x, y = main[i]
        side = 1 if len(stems) % 2 else -1
        ang = math.atan2(main[i + 1][1] - y, main[i + 1][0] - x) + side * random.uniform(0.6, 0.95)
        sh = curve(x, y, ang, S * random.uniform(0.22, 0.32), -side * random.uniform(0.2, 0.5), 12)
        draw_stem(sh, 4, 2); stems.append(sh)
    spots = [(st, i) for st in stems for i in range(2, len(st), 2)]
    random.shuffle(spots)
    placed = 0
    for st, i in spots * 6:
        if placed >= count: break
        x, y = st[i]; nx, ny = st[min(i + 1, len(st) - 1)]
        ang = math.atan2(ny - y, nx - x) + random.choice((-1, 1)) * random.uniform(0.4, 1.0)
        if droop: ang = ang * (1 - droop) + (math.pi / 2) * droop
        leaf = random.choice(leaves)
        sc = S * size * random.uniform(0.75, 1.1) / max(leaf.size)
        w, h = max(1, int(leaf.width * sc * random.uniform(0.55, 1.0))), max(1, int(leaf.height * sc))
        lf = leaf.resize((w, h), Image.LANCZOS)
        if random.random() < 0.5: lf = lf.transpose(Image.FLIP_LEFT_RIGHT)
        rgb = ImageEnhance.Brightness(lf.convert('RGB')).enhance(random.uniform(0.68, 1.05)); rgb.putalpha(lf.getchannel('A')); lf = rgb
        R = int(math.hypot(lf.width, lf.height)) + 4
        big = Image.new('RGBA', (2 * R, 2 * R), (0, 0, 0, 0))
        big.paste(lf, (R - lf.width // 2, R - lf.height), lf)
        big = big.rotate(-math.degrees(ang + math.pi / 2), resample=Image.BICUBIC)
        bb = big.getbbox()
        if not bb: continue
        ox, oy = int(x - big.width / 2), int(y - big.height / 2)
        if ox + bb[0] < 2 or oy + bb[1] < 2 or ox + bb[2] > S - 2 or oy + bb[3] > S - 2: continue
        card.alpha_composite(big, (ox, oy)); placed += 1
    return Image.alpha_composite(tw, card)

cache = {}
for kind, (name, quarters, colour, count, size, droop, stem) in KINDS.items():
    if only and kind not in only: continue
    if name not in cache: cache[name] = load_leaves(name)
    leaves = recolour(cache[name], colour)
    path = os.path.join(out, f'{kind}_leaves.png')
    img = Image.open(path).convert('RGBA')
    for i, q in enumerate(quarters):
        x, y = QUARTERS[q]
        img.paste((0, 0, 0, 0), (x, y, x + 512, y + 512))
        img.alpha_composite(twig(leaves, sum(map(ord, kind)) + i, count, size, droop, stem).resize((512, 512), Image.LANCZOS), (x, y))
    img.save(path, optimize=True)
    print('wrote', kind, f'({len(leaves)} leaves from {name})')
