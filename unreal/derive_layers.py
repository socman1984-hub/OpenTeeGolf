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
    if os.path.exists(os.path.join(export, 'water.glb')):
        stream = stream_layer(export, size)
        boulders(export, meta, size, stream)


# ------------------------------------------------------------------ streams and boulders

STREAM_SLOPE = 0.008    # a water surface falling more than this (per metre) is a stream, not a pond
# Boulders along the creeks (and now and then at a pond): how far apart the groups are along the
# shore (metres, at least), how likely a spot is to get one, the main stone's size (metres across),
# and how many smaller stones lie round it
BOULDER_SPACING_M = {'stream': 7.0, 'pond': 28.0}
BOULDER_CHANCE = {'stream': 0.5, 'pond': 0.35}
BOULDER_SIZE_M = (0.45, 1.5)
BOULDER_SMALL = (0, 4)
BOULDER_SEED = 5


def read_glb(path):
    """Positions (n x 3: metres east, up, south) and triangles (m x 3) of a glTF binary's first mesh."""
    import struct
    b = open(path, 'rb').read()
    jl = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + jl])
    bin_ = b[20 + jl + 8:]

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a['componentType']]
        o = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        v = np.frombuffer(bin_, dt, a['count'] * n, o)
        return v.reshape(-1, n) if n > 1 else v

    p = j['meshes'][0]['primitives'][0]
    return acc(p['attributes']['POSITION']).astype(np.float64), acc(p['indices']).reshape(-1, 3).astype(np.int64)


def stream_layer(export, size):
    """layer_stream.png: where the water runs (its surface slopes) - build_course.py lays pebbles in
    the bottom of a stream, silt and mud in a pond. A soft field like the other layers."""
    v, t = read_glb(os.path.join(export, 'water.glb'))
    a, b, c = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    n = np.cross(b - a, c - a)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    slope = np.hypot(n[:, 0], n[:, 2]) / np.maximum(np.abs(n[:, 1]), 1e-6)
    running = slope > STREAM_SLOPE
    mpp = size / SIZE
    mask = np.zeros((SIZE, SIZE), np.float32)
    for pts in (a[running], b[running], c[running], (a[running] + b[running] + c[running]) / 3):
        i = np.clip((pts[:, 0] / mpp).astype(int), 0, SIZE - 1)
        j = np.clip((pts[:, 2] / mpp).astype(int), 0, SIZE - 1)
        mask[j, i] = 1
    # (the triangles are about a metre: fill between their corners, then soften)
    mask = (box_blur(mask, 2) > 0.02).astype(np.float32)
    field = mask
    for _ in range(3):
        field = box_blur(field, max(1, int(round(2.0 / mpp))))
    Image.fromarray(np.clip(field * 255 + 0.5, 0, 255).astype(np.uint8), 'L').save(os.path.join(export, 'layer_stream.png'))
    print('layer_stream.png: %.2f%% of the landscape, %d of %d water triangles run' % (100 * mask.mean(), running.sum(), len(t)))
    return field


def boulders(export, meta, size, stream):
    """boulders.json: big stones along the creeks' banks and in their beds (a few by the ponds),
    in groups - one large stone with smaller ones round it. Each [x, y, z (cm: east, south, up from
    the heightmap's base), size (cm across), yaw, how flat (z scale), which stone]."""
    wpath = os.path.join(export, 'layer_water.png')
    if not os.path.exists(wpath):
        return
    w = np.asarray(Image.open(wpath).convert('L'), np.float32) / 255
    if w.shape != (SIZE, SIZE):
        w = np.asarray(Image.fromarray((w * 255).astype(np.uint8)).resize((SIZE, SIZE), Image.BILINEAR), np.float32) / 255
    hm = np.asarray(Image.open(os.path.join(export, 'heightmap.png')), np.float64) / 65535 * meta['heightRangeMetres'] * 100
    hn = hm.shape[0]
    mpp = size / SIZE
    rng = np.random.default_rng(BOULDER_SEED)

    def height(xm, zm):
        fx, fz = xm / size * (hn - 1), zm / size * (hn - 1)
        i, j = min(int(fx), hn - 2), min(int(fz), hn - 2)
        u, v = fx - i, fz - j
        return (hm[j, i] * (1 - u) * (1 - v) + hm[j, i + 1] * u * (1 - v) + hm[j + 1, i] * (1 - u) * v + hm[j + 1, i + 1] * u * v)

    # the shore: the water's field near its outline (0.5), a little either side
    jj, ii = np.nonzero((w > 0.42) & (w < 0.6))
    order = rng.permutation(len(jj))
    taken = {}
    out = []
    cell = 4.0
    for k in order:
        j, i = jj[k], ii[k]
        kind = 'stream' if stream[j, i] > 0.3 else 'pond'
        if rng.random() > BOULDER_CHANCE[kind] * 0.02:
            continue
        xm, zm = (i + 0.5) * mpp, (j + 0.5) * mpp
        gx, gz = int(xm / cell), int(zm / cell)
        sp = BOULDER_SPACING_M[kind]
        r = int(np.ceil(sp / cell))
        near = False
        for dx in range(-r, r + 1):
            for dz in range(-r, r + 1):
                for (px, pz) in taken.get((gx + dx, gz + dz), ()):
                    if (px - xm) ** 2 + (pz - zm) ** 2 < sp * sp:
                        near = True
        if near:
            continue
        taken.setdefault((gx, gz), []).append((xm, zm))
        big = rng.uniform(*BOULDER_SIZE_M) * (1.0 if kind == 'stream' else 0.8)
        group = [(xm, zm, big)]
        for _ in range(rng.integers(BOULDER_SMALL[0], BOULDER_SMALL[1] + 1)):
            ang, d = rng.uniform(0, 2 * np.pi), rng.uniform(0.5, 1.4) * big + 0.3
            sx, sz = xm + np.cos(ang) * d, zm + np.sin(ang) * d
            pi_, pj = int(sx / mpp), int(sz / mpp)
            if 0 <= pi_ < SIZE and 0 <= pj < SIZE and 0.36 < w[pj, pi_] < 0.75:
                group.append((sx, sz, big * rng.uniform(0.25, 0.6)))
        for sx, sz, d in group:
            # (the pivot is the stone's middle: half of it or a little more below the ground)
            z = height(sx, sz) - d * 100 * rng.uniform(0.0, 0.12)
            out.append([round(sx * 100, 1), round(sz * 100, 1), round(z, 1), round(d * 100, 1),
                        round(rng.uniform(0, 360), 1), round(rng.uniform(0.5, 0.85), 2), int(rng.integers(0, 6))])
    json.dump(out, open(os.path.join(export, 'boulders.json'), 'w'))
    print('boulders.json: %d stones in %d groups' % (len(out), sum(len(v) for v in taken.values())))


if __name__ == '__main__':
    main(sys.argv[1])
