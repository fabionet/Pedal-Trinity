#!/usr/bin/env python3
"""
Prepara le illustrazioni della guida PDF a partire dagli screenshot reali
del plugin (generati con: "Pedal Trinity" --screenshot ... --scale 2).

Uso: python3 tools/art/guide_images.py shot2x.png shot_info2x.png
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "docs", "images")
os.makedirs(OUT, exist_ok=True)
pos = json.load(open(os.path.join(HERE, "build", "positions.json")))
S = 2  # gli screenshot sono a scala 2
F = ImageFont.truetype("/usr/share/fonts/truetype/lato/Lato-Black.ttf", 26)
GOLD = (217, 180, 100)

shot = Image.open(sys.argv[1]).convert("RGB")
info = Image.open(sys.argv[2]).convert("RGB")


def knob(pid):
    k = next(k for k in pos["knobs"] if k["id"] == pid)
    return ((k["anchor"][0] + k["top"][0]) / 2, (k["anchor"][1] + k["top"][1]) / 2)


def slider(i):
    s = pos["sliders"][i]
    return (s["a_min"][0], (s["a_min"][1] + s["a_max"][1]) / 2)


def led(pid):
    l = next(l for l in pos["leds"] if l["id"] == pid)
    return tuple(l["c"])


def foot(pid):
    q = next(q for q in pos["footswitches"] if q["id"] == pid)["quad"]
    return (sum(p[0] for p in q) / 4, sum(p[1] for p in q) / 4)


def annotate(img, marks, origin=(0, 0)):
    img = img.copy()
    d = ImageDraw.Draw(img)
    for label, (tx, ty), (dx, dy) in marks:
        x, y = (tx - origin[0]) * S, (ty - origin[1]) * S
        mx, my = x + dx * S, y + dy * S
        if dx or dy:
            d.line([(x, y), (mx, my)], fill=(0, 0, 0), width=7)
            d.line([(x, y), (mx, my)], fill=GOLD, width=3)
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=GOLD, outline=(0, 0, 0), width=2)
        r = 21 if len(label) < 2 else 24
        d.ellipse([mx - r - 3, my - r - 3, mx + r + 3, my + r + 3], fill=(0, 0, 0))
        d.ellipse([mx - r, my - r, mx + r, my + r], fill=GOLD, outline=(255, 255, 255), width=2)
        d.text((mx, my + 1), label, font=F, fill=(0, 0, 0), anchor="mm")
    return img


def crop(img, box):
    return img.crop(tuple(v * S for v in box))


# copertina e screenshot del README
shot.save(os.path.join(OUT, "cover.jpg"), quality=88)
shot.resize((shot.width // 2, shot.height // 2), Image.LANCZOS).save(os.path.join(OUT, "screenshot.png"))

# panoramica
pl = pos["info"]["c"]
ov = annotate(shot, [("A", (175, 72), (0, 30)), ("B", tuple(pl), (-60, 0)),
                     ("C", (1188, 613), (-28, -24))])
ov.save(os.path.join(OUT, "guide_overview.jpg"), quality=88)

# ED-9
box = (70, 92, 390, 600)
ed = annotate(shot, [
    ("1", knob("od_drive"), (-52, -46)), ("2", knob("od_tone"), (-62, 30)), ("3", knob("od_level"), (52, -46)),
    ("4", led("ed9"), (40, -18)), ("5", foot("ed9"), (0, 0))])
crop(ed, box).save(os.path.join(OUT, "guide_ed9.jpg"), quality=90)

# MC-2W
box = (440, 92, 760, 600)
mc = annotate(shot, [
    ("1", knob("dist_level"), (-20, -60)), ("2", knob("dist_low"), (-22, 62)), ("3", knob("dist_high"), (8, -62)),
    ("4", knob("dist_mid"), (22, 62)), ("5", knob("dist_midfreq"), (30, -58)), ("6", knob("dist_gain"), (20, -60)),
    ("7", (pos["toggles"][0]["anchor"][0], pos["toggles"][0]["anchor"][1] - 8), (54, -16)),
    ("8", led("mc2"), (-50, -10)), ("9", foot("mc2"), (0, 40))])
crop(mc, box).save(os.path.join(OUT, "guide_mc2.jpg"), quality=90)

# GQ-7
box = (812, 92, 1132, 600)
marks = []
for i in range(7):
    marks.append((str(i + 1), slider(i), (0, -62 - (i % 2) * 34)))
marks.append(("8", slider(7), (32, -60)))
marks.append(("9", led("gq7"), (-10, 40)))
marks.append(("10", foot("gq7"), (0, 40)))
crop(annotate(shot, marks), box).save(os.path.join(OUT, "guide_gq7.jpg"), quality=90)

# pannello info
info.crop((200 * S, 28 * S, 1000 * S, 597 * S)).save(os.path.join(OUT, "guide_info.jpg"), quality=90)
print("immagini della guida in", OUT)
