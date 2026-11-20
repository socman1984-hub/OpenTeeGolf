# The ground's rough texture made from the game's own grass: OTG.Look.Bake photographs a patch of
# the rough from straight above (Saved/TurfBake/rough_color.png - alpha: a blade or not - its
# normals and its heights); this fills the gaps between the blades with the shade under them, makes
# it tile and writes textures/rough_baked_color.jpg and rough_baked_normal.jpg, and the colour's
# mean (linear) to textures/rough_baked.json. build_course.py's ground takes them in place of the web
# version's rough texture, so the ground under the rough's blades, and on past where they're drawn,
# is the same grass seen from a little way off.
#
#   python unreal/bake_turf.py        (after a run with OTG.Look.Bake rough 300 rough)

import json
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
BAKE = os.path.join(HERE, "OpenTeeGolf", "Saved", "TurfBake")
OUT = os.path.join(HERE, "textures")
SIZE = 1024


def tileable(a):
    """The picture blended with itself shifted half a tile: its own seams at the middle are covered by
    the original, the original's at the edges by the shifted copy"""
    h, w = a.shape[:2]
    shifted = np.roll(np.roll(a, h // 2, 0), w // 2, 1)
    y = np.abs(np.linspace(-1, 1, h))[:, None]
    x = np.abs(np.linspace(-1, 1, w))[None, :]
    edge = np.clip((np.maximum(x, y) - 0.55) / 0.4, 0, 1)
    edge = edge * edge * (3 - 2 * edge)
    if a.ndim == 3:
        edge = edge[..., None]
    return a * (1 - edge) + shifted * edge


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def bake(name):
    col = Image.open(os.path.join(BAKE, name + "_color.png")).convert("RGBA")
    nrm = Image.open(os.path.join(BAKE, name + "_normal.png")).convert("RGB")
    a = np.asarray(col).astype(np.float32) / 255.0
    rgb, alpha = a[..., :3], a[..., 3:]
    # the ground between the blades: the blades' own colour, blurred, in their shade
    blur = np.asarray(col.convert("RGB").filter(ImageFilter.GaussianBlur(24))).astype(np.float32) / 255.0
    cover = np.asarray(Image.fromarray((alpha[..., 0] * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(24))).astype(np.float32)[..., None] / 255.0
    under = np.where(cover > 0.02, blur / np.maximum(cover, 0.02), rgb.mean(axis=(0, 1)))
    under = under * 0.5
    rgb = rgb * alpha + under * (1 - alpha)
    n = np.asarray(nrm).astype(np.float32) / 255.0 * 2 - 1
    n = n * alpha + np.array([0, 0, 1], np.float32) * (1 - alpha)
    # (the capture looked straight down, image up along the world's X: the ground's tangent space)
    tn = np.stack([n[..., 1], n[..., 0], np.maximum(n[..., 2], 0.2)], -1)
    tn /= np.linalg.norm(tn, axis=-1, keepdims=True)
    rgb = tileable(rgb)
    tn = tileable(tn)
    tn /= np.linalg.norm(tn, axis=-1, keepdims=True)
    c8 = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).resize((SIZE, SIZE), Image.LANCZOS)
    n8 = Image.fromarray((np.clip(tn * 0.5 + 0.5, 0, 1) * 255 + 0.5).astype(np.uint8)).resize((SIZE, SIZE), Image.LANCZOS)
    c8.save(os.path.join(OUT, name + "_baked_color.jpg"), quality=92)
    n8.save(os.path.join(OUT, name + "_baked_normal.jpg"), quality=95)
    mean = srgb_to_linear(np.asarray(c8).astype(np.float64) / 255.0).reshape(-1, 3).mean(0)
    json.dump({"mean": [round(float(v), 4) for v in mean]}, open(os.path.join(OUT, name + "_baked.json"), "w"))
    print("%s: written, mean %s" % (name, mean.round(4)))


if __name__ == "__main__":
    for n in sys.argv[1:] or ["rough"]:
        bake(n)
