"""Check the turf's colours in the game: one photograph of each cut, compared (OTGPalette.h).

    python unreal/tests/turf_colours.py [--course Pebble_Beach_Golf_Course] [--hole 7] [--out <folder>] [--cmd "OTG.Palette.Red 0.5"]

For each surface - the green, the collar, the fairway, the rough and the deep rough - it starts the
game (the editor's -game, with the editor closed), puts the free view on a patch well inside it
(OTG.Look.Swatch: straight down from 4.5 m, so the mowing stripes and the sun's angle are the same
for every cut), calm, at noon, with the eye's adaptation off so one cut isn't
brightened to match another, and takes a screenshot. The middle of each is averaged and the cuts
are compared as a well-kept course has them (DZ Grasses' showcase, our palette):

  - one hue: every mown cut's red and blue, as shares of its green, within HUE_SPREAD of each other
  - lighter the shorter: green >= fairway >= collar >= rough, each step under STEP_MAX
  - the deep rough greyer than the rough (less saturated)

It prints a table and PASS or FAIL for each, and leaves the photographs in --out. The game's
GameUserSettings.ini (the -ResX/-ResY of the runs overwrite it) is put back afterwards.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", "OpenTeeGolf"))
EDITOR = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
SURFACES = ["green", "fringe", "fairway", "rough", "native"]
# (red / green and blue / green, between the mown cuts: DZ Grasses' showcase measures 0.16 between
# its green, fairway and collar - the light on a tall blade and a short one isn't the same, so no
# course reads as exactly one hue on screen)
HUE_SPREAD = 0.18
STEP_MAX = 0.25     # (one cut's lightness under the next shorter one's, as a share)


def linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def measure(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    # (the middle, where the patch is, looking down on it: a narrow strip, for a collar)
    box = (int(w * 0.45), int(h * 0.45), int(w * 0.55), int(h * 0.55))
    r, g, b = im.crop(box).resize((1, 1), Image.BOX).getpixel((0, 0))
    lr, lg, lb = linear(r), linear(g), linear(b)
    luma = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb
    mx, mn = max(lr, lg, lb), min(lr, lg, lb)
    return {"srgb": [r, g, b], "luma": luma, "red": lr / max(lg, 1e-6), "blue": lb / max(lg, 1e-6), "sat": (mx - mn) / max(mx, 1e-6)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--course", default="Pebble_Beach_Golf_Course")
    ap.add_argument("--hole", default="7")
    ap.add_argument("--out", default=os.path.join(PROJECT, "Saved", "TurfColours"))
    ap.add_argument("--cmd", default="", help="console commands run first (; between them), e.g. palette settings")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    shot = os.path.join(PROJECT, "Saved", "Screenshots", "otg_shot.png")
    gus = glob.glob(os.path.join(PROJECT, "Saved", "Config", "*", "GameUserSettings.ini"))
    for g in gus:
        shutil.copy2(g, g + ".turfbak")
    results = {}
    try:
        for s in SURFACES:
            if os.path.exists(shot):
                os.remove(shot)
            cmds = [c.strip() for c in a.cmd.split(";") if c.strip()] + ["ShowFlag.EyeAdaptation 0", "OTG.Round.Time 12", "OTG.Look.Swatch %s 20 450" % s]
            args = [EDITOR, os.path.join(PROJECT, "OpenTeeGolf.uproject"), "-game", "-windowed", "-resx=1600", "-resy=900",
                    "-course=" + a.course, "-hole=" + a.hole, "-weather=calm", "-shot=70", "-shotwait=10",
                    "-shotcmd=" + ";".join(c.replace(" ", "+") for c in cmds)]
            try:
                subprocess.run(args, timeout=300)
            except subprocess.TimeoutExpired:
                print("%s: the game didn't finish" % s)
            if not os.path.exists(shot):
                print("%s: no screenshot" % s)
                continue
            # (no patch of it near the hole - a course without collars, say: its photograph is of
            # something else, so it's left out)
            log = os.path.join(PROJECT, "Saved", "Logs", "OpenTeeGolf.log")
            if os.path.exists(log) and ("Swatch %s: none near here" % s) in open(log, encoding="utf-8", errors="ignore").read():
                print("%s: none on this hole, left out" % s)
                continue
            dst = os.path.join(a.out, s + ".png")
            shutil.copy2(shot, dst)
            results[s] = measure(dst)
    finally:
        for g in gus:
            shutil.copy2(g + ".turfbak", g)
            os.remove(g + ".turfbak")

    print("%-8s %-15s %7s %6s %6s %6s" % ("cut", "screen sRGB", "light", "red/g", "blue/g", "sat"))
    for s in SURFACES:
        if s in results:
            m = results[s]
            print("%-8s %-15s %7.4f %6.3f %6.3f %6.3f" % (s, ",".join(map(str, m["srgb"])), m["luma"], m["red"], m["blue"], m["sat"]))
    ok = True
    mown = [s for s in ("green", "fringe", "fairway", "rough") if s in results]
    if len(mown) >= 2:
        for k in ("red", "blue"):
            vals = [results[s][k] for s in mown]
            spread = max(vals) - min(vals)
            good = spread <= HUE_SPREAD
            ok &= good
            print("%s one hue (%s/green spread %.3f, at most %.2f)" % ("PASS" if good else "FAIL", k, spread, HUE_SPREAD))
        order = [s for s in ("green", "fairway", "fringe", "rough") if s in results]
        for x, y in zip(order, order[1:]):
            lx, ly = results[x]["luma"], results[y]["luma"]
            good = ly <= lx * 1.02 and ly >= lx * (1 - STEP_MAX)
            ok &= good
            print("%s %s (%.4f) a step darker than %s (%.4f)" % ("PASS" if good else "FAIL", y, ly, x, lx))
    if "native" in results and "rough" in results:
        good = results["native"]["sat"] < results["rough"]["sat"]
        ok &= good
        print("%s deep rough greyer than the rough (saturation %.3f against %.3f)" % ("PASS" if good else "FAIL", results["native"]["sat"], results["rough"]["sat"]))
    with open(os.path.join(a.out, "turf_colours.json"), "w") as f:
        json.dump(results, f, indent=1)
    print("ALL PASS" if ok else "SOME FAILED", "- photographs in", a.out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
