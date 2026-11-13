"""The stream bed's textures: rounded pebbles and gravel over silt, seamless, made from scratch
(no outside images). Writes textures/creekbed_color.png and textures/creekbed_normal.png here in
unreal/, which build_course.py lays under the course's streams and ponds.

    python make_bed_texture.py

Each stone is a rounded, slightly flattened oval of its own size, turn and colour - washed greys,
browns and tans, as a creek's stones are - dropped onto the square (wrapping round its edges, so
the picture tiles) with the higher one on top where two overlap; silt and coarse sand between.
"""
import math
import os

import numpy as np
from PIL import Image

SIZE = 1024          # pixels; build_course.py lays one copy over BED_TILE_M metres
SEED = 11
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures")

# stone colours (sRGB 0..1): washed greys, blue-grey, browns, tans, near-black, a rusty one
STONES = np.array([
    (0.52, 0.51, 0.48), (0.44, 0.45, 0.45), (0.47, 0.41, 0.34), (0.58, 0.53, 0.44),
    (0.31, 0.30, 0.28), (0.50, 0.40, 0.31), (0.62, 0.60, 0.55), (0.38, 0.35, 0.31),
])
SILT = np.array((0.37, 0.32, 0.25))
# (how many stones of each size: radius range in pixels)
SIZES = ((70, 25, 70), (520, 10, 26), (5500, 3, 9))


def main():
    rng = np.random.default_rng(SEED)
    os.makedirs(OUT, exist_ok=True)
    height = np.zeros((SIZE, SIZE), np.float32)
    col = np.zeros((SIZE, SIZE, 3), np.float32)
    # the ground between: silt with coarse sand
    grain = rng.random((SIZE, SIZE), np.float32)
    col[:] = SILT * (0.8 + 0.4 * grain[..., None])
    height[:] = 0.04 * grain
    for count, rmin, rmax in SIZES:
        for _ in range(count):
            r = rng.uniform(rmin, rmax)
            aspect = rng.uniform(0.55, 1.0)
            turn = rng.uniform(0, math.pi)
            cx, cy = rng.uniform(0, SIZE, 2)
            tall = r * rng.uniform(0.45, 0.7)     # (pebbles lie flat-ish)
            base = rng.uniform(-0.15, 0.05) * r   # (half sunk in the silt)
            c = STONES[rng.integers(len(STONES))] * rng.uniform(0.85, 1.12)
            # its box, wrapped round the square
            x0, x1 = int(cx - r) - 1, int(cx + r) + 2
            y0, y1 = int(cy - r) - 1, int(cy + r) + 2
            xs = np.arange(x0, x1)
            ys = np.arange(y0, y1)
            X, Y = np.meshgrid(xs - cx, ys - cy)
            u = (X * math.cos(turn) + Y * math.sin(turn)) / r
            v = (-X * math.sin(turn) + Y * math.cos(turn)) / (r * aspect)
            d2 = u * u + v * v
            inside = d2 < 1
            if not inside.any():
                continue
            # a rounded top, a little flatter in the middle (superellipse profile)
            h = base + tall * np.sqrt(np.clip(1 - d2 ** 1.4, 0, 1))
            yi = np.mod(ys, SIZE)[:, None].repeat(len(xs), 1)
            xi = np.mod(xs, SIZE)[None, :].repeat(len(ys), 0)
            cur = height[yi, xi]
            top = inside & (h > cur)
            height[yi[top], xi[top]] = h[top]
            # lit from above: the stone's middle a touch lighter than its edges, a few speckles
            shade = (0.78 + 0.22 * np.sqrt(np.clip(1 - d2, 0, 1)))[top] * (0.94 + 0.12 * rng.random(top.sum()))
            col[yi[top], xi[top]] = c * shade[:, None]
    # (a soft shadow of silt round each stone where it meets the bed)
    low = np.clip(1 - height / 6.0, 0, 1)
    col *= (1 - 0.25 * low ** 3)[..., None]
    Image.fromarray((np.clip(col, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, "creekbed_color.png"))
    # normal map from the height (pixels, wrapped), DirectX style for Unreal (green down)
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
    n = np.stack([-dx, dy, np.ones_like(dx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8)).save(os.path.join(OUT, "creekbed_normal.png"))
    print("wrote", os.path.join(OUT, "creekbed_color.png"), "and creekbed_normal.png")


if __name__ == "__main__":
    main()
