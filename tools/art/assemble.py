#!/usr/bin/env python3
"""
Assembla i render di Blender nelle risorse del plugin:
  Resources/background.jpg        foto fissa dei pedali (2x)
  Resources/strip_<tipo>.png      filmstrip dei pomelli / cursore / levetta
  Source/gui/UILayout.h           coordinate generate per l'interfaccia C++
  tools/art/build/composite.png   anteprima con i pomelli ai valori di default

Uso: python3 tools/art/assemble.py
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.path.join(HERE, "build")
RES = os.path.join(ROOT, "Resources")
HDR = os.path.join(ROOT, "Source", "gui", "UILayout.h")

pos = json.load(open(os.path.join(BUILD, "positions.json")))
RS = pos["render_scale"]
N = pos["sheet"]["frames"]


# ---------------------------------------------------------------- sfondo
bg = Image.open(os.path.join(BUILD, "background.png")).convert("RGB")
w, h = bg.size
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
vig = np.clip(1.0 - 0.28 * np.clip(d - 0.55, 0, None) ** 1.6, 0.6, 1.0)
a = np.asarray(bg).astype(np.float32) * vig[..., None]
a += np.random.default_rng(3).normal(0, 1.2, a.shape[:2])[..., None]  # grana leggera
bg = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
bg.save(os.path.join(RES, "background.jpg"), quality=90, optimize=True, progressive=True)
print("background.jpg", bg.size, os.path.getsize(os.path.join(RES, "background.jpg")) // 1024, "KB")

# ---------------------------------------------------------------- filmstrip
frames = {}


AVAILABLE = sorted(int(f[2:5]) for f in os.listdir(os.path.join(BUILD, "sheet")) if f.startswith("f_"))
if len(AVAILABLE) < N:
    print("ATTENZIONE: solo %d/%d fotogrammi disponibili (anteprima parziale)" % (len(AVAILABLE), N))


def frame(i):
    i = min(AVAILABLE, key=lambda a: abs(a - i))   # fotogramma piu' vicino se mancante
    if i not in frames:
        frames[i] = Image.open(os.path.join(BUILD, "sheet", "f_%03d.png" % i)).convert("RGBA")
    return frames[i]


strips = {}
for kind in pos["sheet"]["items"]:
    cell = pos["sheet"]["cells"][kind]
    idx = [0] if kind == "slider" else ([0, N - 1] if kind == "toggle" else list(range(N)))
    x0, x1 = cell["cell_x0"], cell["cell_x1"]
    crops = [frame(i).crop((x0, 0, x1, frame(i).size[1])) for i in idx]
    alpha = np.zeros(crops[0].size[::-1], np.uint8)
    for c in crops:
        alpha = np.maximum(alpha, np.asarray(c)[..., 3])
    # ritaglio sull'oggetto + ombra "piena"; le code d'ombra deboli vengono sfumate
    FEATHER = 14
    ys, xs = np.where(alpha > 28)
    bx0, bx1 = max(0, xs.min() - FEATHER), min(alpha.shape[1], xs.max() + FEATHER + 1)
    by0, by1 = max(0, ys.min() - FEATHER), min(alpha.shape[0], ys.max() + FEATHER + 1)
    fw, fh = bx1 - bx0, by1 - by0
    wy = np.clip(np.minimum(np.arange(fh), fh - 1 - np.arange(fh)) / FEATHER, 0, 1)
    wx = np.clip(np.minimum(np.arange(fw), fw - 1 - np.arange(fw)) / FEATHER, 0, 1)
    win = np.outer(wy * wy * (3 - 2 * wy), wx * wx * (3 - 2 * wx))

    def feathered(img):
        arr = np.asarray(img).astype(np.float32).copy()
        solid = arr[..., 3] > 200          # il pomello resta intatto
        arr[..., 3] = np.where(solid, arr[..., 3], arr[..., 3] * win)
        return Image.fromarray(arr.astype(np.uint8))
    cols = min(8, len(crops))
    rows = math.ceil(len(crops) / cols)
    sheet = Image.new("RGBA", (fw * cols, fh * rows), (0, 0, 0, 0))
    for k, c in enumerate(crops):
        sheet.paste(feathered(c.crop((bx0, by0, bx1, by1))), ((k % cols) * fw, (k // cols) * fh))
    name = "strip_%s.png" % kind
    sheet.save(os.path.join(RES, name), optimize=True)
    ax = cell["anchor"][0] - x0 - bx0
    ay = cell["anchor"][1] - by0
    strips[kind] = dict(res=name.replace(".", "_"), fw=fw, fh=fh, cols=cols, frames=len(crops),
                        ax=ax, ay=ay, img=sheet)
    print(name, sheet.size, os.path.getsize(os.path.join(RES, name)) // 1024, "KB")

# ---------------------------------------------------------------- header C++
ORDER = ["ts_big", "ts_small", "boss", "boss_outer", "boss_inner", "slider", "toggle"]
ENUM = {"ts_big": "tsBig", "ts_small": "tsSmall", "boss": "boss", "boss_outer": "bossOuter",
        "boss_inner": "bossInner", "slider": "sliderCap", "toggle": "toggle"}
ppm = pos["ppm_1x"]


def f(v):
    return "%.3ff" % v


L = []
L.append("// File GENERATO da tools/art/assemble.py - non modificare a mano.")
L.append("// Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3")
L.append("#pragma once\n")
L.append("namespace pt::ui\n{")
L.append("    inline constexpr float uiWidth = %s, uiHeight = %s, renderScale = %s;" %
         (f(pos["ui_w"]), f(pos["ui_h"]), f(RS)))
L.append("")
L.append("    enum StripId { %s, numStrips };" % ", ".join(ENUM[k] for k in ORDER))
L.append("")
L.append("    /** Filmstrip: dimensioni del fotogramma e punto d'ancoraggio in pixel del render (2x). */")
L.append("    struct Strip { const char* resource; int frameW, frameH, cols, frames; float anchorX, anchorY; };")
L.append("    inline constexpr Strip strips[numStrips] =\n    {")
for k in ORDER:
    s = strips[k]
    L.append('        { "%s", %d, %d, %d, %d, %s, %s },' % (s["res"], s["fw"], s["fh"], s["cols"], s["frames"],
                                                          f(s["ax"]), f(s["ay"])))
L.append("    };\n")
L.append("    /** Pomello: ancoraggio (base), centro della sommita', raggio di presa (px @1x). */")
L.append("    struct Knob { const char* paramId; StripId strip; float ax, ay, tx, ty, radius; };")
L.append("    inline constexpr Knob knobs[] =\n    {")
for k in pos["knobs"]:
    L.append('        { "%s", %s, %s, %s, %s, %s, %s },' % (k["id"], ENUM[k["type"]], f(k["anchor"][0]),
                                                            f(k["anchor"][1]), f(k["top"][0]), f(k["top"][1]),
                                                            f(k["r"] * ppm)))
L.append("    };\n")
L.append("    /** Cursore del GQ-7: ancoraggio del cappuccio al minimo e al massimo. */")
L.append("    struct Fader { const char* paramId; float x, yMin, yMax; };")
L.append("    inline constexpr Fader faders[] =\n    {")
for s in pos["sliders"]:
    L.append('        { "%s", %s, %s, %s },' % (s["id"], f(s["a_min"][0]), f(s["a_min"][1]), f(s["a_max"][1])))
L.append("    };\n")
L.append("    struct Led { const char* pedal; float x, y, radius; };")
L.append("    inline constexpr Led leds[] =\n    {")
for l in pos["leds"]:
    L.append('        { "%s", %s, %s, %s },' % (l["id"], f(l["c"][0]), f(l["c"][1]), f(l["r"] * ppm)))
L.append("    };\n")
L.append("    struct Footswitch { const char* pedal; float x[4], y[4]; };")
L.append("    inline constexpr Footswitch footswitches[] =\n    {")
for q in pos["footswitches"]:
    L.append('        { "%s", { %s }, { %s } },' % (q["id"], ", ".join(f(p[0]) for p in q["quad"]),
                                                   ", ".join(f(p[1]) for p in q["quad"])))
L.append("    };\n")
t = pos["toggles"][0]
L.append("    inline constexpr float modeToggleX = %s, modeToggleY = %s;" % (f(t["anchor"][0]), f(t["anchor"][1])))
i = pos["info"]
L.append("    inline constexpr float infoX = %s, infoY = %s, infoRadius = %s;" % (f(i["c"][0]), f(i["c"][1]),
                                                                              f(i["r"] * ppm)))
L.append("}")
open(HDR, "w").write("\n".join(L) + "\n")
print("scritto", HDR)

# ---------------------------------------------------------------- anteprima composita
comp = bg.copy().convert("RGBA")


def paste(kind, idx, ax, ay):
    s = strips[kind]
    c, r = idx % s["cols"], idx // s["cols"]
    fr = s["img"].crop((c * s["fw"], r * s["fh"], (c + 1) * s["fw"], (r + 1) * s["fh"]))
    comp.alpha_composite(fr, (int(round(ax * RS - s["ax"])), int(round(ay * RS - s["ay"]))))


defaults = {"od_drive": .5, "od_level": .5, "od_tone": .5, "dist_level": .5, "dist_gain": .6,
            "dist_low": .5, "dist_high": .5, "dist_mid": .5, "dist_midfreq": .5}
for k in pos["knobs"]:
    paste(k["type"], int(round(defaults.get(k["id"], .5) * (N - 1))), *k["anchor"])
for s in pos["sliders"]:
    paste("slider", 0, s["a_min"][0], (s["a_min"][1] + s["a_max"][1]) / 2)
paste("toggle", 0, *pos["toggles"][0]["anchor"])
comp.convert("RGB").save(os.path.join(BUILD, "composite.png"))
print("composite.png")

# ---------------------------------------------------------------- icona 512x512
crop = comp.crop((300, 150, 2100, 1249)).convert("RGBA")
crop.thumbnail((470, 470), Image.LANCZOS)
icon = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
from PIL import ImageDraw
mask = Image.new("L", (512, 512), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, 511, 511], radius=96, fill=255)
bgc = Image.new("RGBA", (512, 512), (22, 22, 25, 255))
icon.paste(bgc, (0, 0), mask)
icon.alpha_composite(crop, ((512 - crop.width) // 2, (512 - crop.height) // 2))
icon.save(os.path.join(RES, "icon.png"))
print("icon.png")
