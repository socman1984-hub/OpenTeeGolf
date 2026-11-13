# Extra surface masks for build_course.py that the web export doesn't write, derived from its play
# data: run by make_pack.ps1 (system Python with numpy and Pillow) before Unreal builds the course.
#
#   python unreal/derive_layers.py exports/<course id>
#
#   layer_native.png   deep rough and native grass (the web game's "deeprough" under the trees and
#                      "native" out of play), and the ground under every tree's crown (but never on
#                      a fairway, tee, green, fringe, first cut, path, bunker or water): where
#                      build_course.py grows tall wild grass. On the
#                      landscape's square like the export's own layer_*.png, and like them a soft
#                      field: 0.5 on the outline, 0 and 1 a couple of metres either side.

import json
import os
import sys

import numpy as np
from PIL import Image

CODES = ['', 'water', 'green', 'fringe', 'fairway', 'tee', 'firstcut', 'rough', 'deeprough', 'native', 'path', 'sand', 'oob']
NATIVE = ('deeprough', 'native')
KEEP_CLEAR = ('water', 'green', 'fringe', 'fairway', 'tee', 'firstcut', 'path', 'sand')
UNDER_CROWN = 0.85   # (how much of each crown's spread)
SIZE = 4096        # (as the export's masks)
SOFT_M = 1.2       # how soft the outline is


def box_blur(a, r):
    """Box blur, radius r pixels, both ways (three of them: near enough a gaussian)"""
    if r < 1:
        return a
    k = 2 * r + 1
    for axis in (0, 1):
        p = np.pad(a, [(r + 1, r) if ax == axis else (0, 0) for ax in (0, 1)], mode='edge')
        c = np.cumsum(p, axis=axis, dtype=np.float64)
        if axis == 0:
            a = (c[k:, :] - c[:-k, :]) / k
        else:
            a = (c[:, k:] - c[:, :-k]) / k
    return a


def main(export):
    meta = json.load(open(os.path.join(export, 'course.json'), encoding='utf-8'))
    play = json.load(open(os.path.join(export, 'play.json'), encoding='utf-8'))
    s = play['surfaces']
    codes = s.get('codes') or CODES
    surf = np.array(Image.open(os.path.join(export, s['file'])))
    if surf.ndim == 3:
        surf = surf[..., 0]
    want = np.zeros(256, bool)
    for name in NATIVE:
        if name in codes:
            want[codes.index(name)] = True
    # the landscape's square: its north-west corner and size, course metres
    x0, z1 = meta['courseOrigin']['x'], meta['courseOrigin']['z']
    size = meta['sizeMetres']
    px = (np.arange(SIZE) + 0.5) * size / SIZE
    xs, zs = x0 + px, z1 - px
    i = np.floor((xs - s['minX']) / s['res']).astype(int)
    j = np.floor((s['maxZ'] - zs) / s['res']).astype(int)
    inside_i = (i >= 0) & (i < s['w'])
    inside_j = (j >= 0) & (j < s['h'])
    sub = surf[np.clip(j, 0, s['h'] - 1)][:, np.clip(i, 0, s['w'] - 1)]
    inside = inside_j[:, None] & inside_i[None, :]
    mask = want[sub] & inside
    # under the trees' crowns (the play data's trees: [x, z, trunk top, trunk r, kind, cone, a, b, c];
    # the spread is b for a round crown, c for a cone)
    mpp = size / SIZE
    for t in play.get('trees', []):
        x, z, cone, b, c = t[0], t[1], t[5], t[7], t[8]
        r = (c if cone else b) * UNDER_CROWN
        if r <= 0:
            continue
        ci, cj, rp = (x - x0) / mpp, (z1 - z) / mpp, r / mpp
        i0, i1 = max(0, int(ci - rp)), min(SIZE - 1, int(ci + rp) + 1)
        j0, j1 = max(0, int(cj - rp)), min(SIZE - 1, int(cj + rp) + 1)
        if i0 > i1 or j0 > j1:
            continue
        ii, jj = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1))
        mask[j0:j1 + 1, i0:i1 + 1] |= (ii + 0.5 - ci) ** 2 + (jj + 0.5 - cj) ** 2 <= rp * rp
    clear = np.zeros(256, bool)
    for name in KEEP_CLEAR:
        if name in codes:
            clear[codes.index(name)] = True
    mask &= ~(clear[sub] & inside)
    field = mask.astype(np.float32)
    r = max(1, int(round(SOFT_M / (size / SIZE))))
    for _ in range(3):
        field = box_blur(field, r)
    out = os.path.join(export, 'layer_native.png')
    Image.fromarray(np.clip(field * 255 + 0.5, 0, 255).astype(np.uint8), 'L').save(out)
    print('layer_native.png: %.1f%% of the landscape' % (100 * mask.mean()))


if __name__ == '__main__':
    main(sys.argv[1])
