#!/usr/bin/env python3
"""
Illustrazioni della guida PDF dagli screenshot reali del plugin
(generati da tools/build_guide.sh con "Pedal Trinity" --screenshot).

Uso: python3 tools/art/guide_images.py <cartella screenshot>
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "docs", "images")
os.makedirs(OUT, exist_ok=True)
SRC = sys.argv[1]
S = 2   # gli screenshot principali sono a scala 2
F = ImageFont.truetype("/usr/share/fonts/truetype/lato/Lato-Black.ttf", 26)
GOLD = (217, 180, 100)


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGB")


def annotate(img, marks, scale=S):
    img = img.copy()
    d = ImageDraw.Draw(img)
    for label, (tx, ty), (mx, my) in marks:
        x, y, px, py = tx * scale, ty * scale, mx * scale, my * scale
        if (x, y) != (px, py):
            d.line([(x, y), (px, py)], fill=(0, 0, 0), width=7)
            d.line([(x, y), (px, py)], fill=GOLD, width=3)
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=GOLD, outline=(0, 0, 0), width=2)
        r = 21
        d.ellipse([px - r - 3, py - r - 3, px + r + 3, py + r + 3], fill=(0, 0, 0))
        d.ellipse([px - r, py - r, px + r, py + r], fill=GOLD, outline=(255, 255, 255), width=2)
        d.text((px, py + 1), label, font=F, fill=(0, 0, 0), anchor="mm")
    return img


def save(img, name, q=88):
    img.save(os.path.join(OUT, name), quality=q, optimize=True, progressive=True)


# ---- copertina e README
main = load("main.png")           # 1280x760 a scala 2, vista 3
save(main, "cover.jpg", 86)
main.resize((main.width // 2, main.height // 2), Image.LANCZOS).save(os.path.join(OUT, "screenshot.png"), optimize=True)

# ---- panoramica: tasti della barra (coordinate logiche a 1280x760)
ov = annotate(main, [
    ("A", (294, 25), (294, 72)), ("B", (432, 25), (432, 72)), ("C", (566, 25), (566, 72)),
    ("D", (818, 25), (818, 72)), ("E", (936, 25), (936, 72)), ("F", (1055, 25), (1055, 72)),
    ("G", (1167, 20), (1167, 72)), ("H", (1240, 25), (1240, 72))])
save(ov, "guide_overview.jpg")

# ---- dettaglio di uno slot (primo slot, vista 3 a 1280x760)
slot = annotate(main, [
    ("1", (20, 73), (20, 73)), ("2", (220, 73), (300, 140)), ("3", (54, 103), (54, 150)),
    ("4", (219, 103), (170, 160)), ("5", (302, 103), (302, 160)), ("6", (385, 103), (385, 160)),
    ("7", (140, 245), (60, 300)), ("8", (216, 211), (300, 230)), ("9", (216, 523), (330, 560))])
save(slot.crop((0, 56 * S, 430 * S, 752 * S)), "guide_slot.jpg")

# ---- viste e zoom (scala 1)
save(load("view6.png"), "guide_view6.jpg", 84)
save(load("view18.png"), "guide_view18.jpg", 84)
save(load("zoom.png"), "guide_zoom.jpg", 86)
info = load("info.png")
w, h = info.size
save(info.crop((int(w * 0.18), int(h * 0.03), int(w * 0.82), int(h * 0.97))), "guide_info.jpg", 88)
print("immagini della guida in", OUT)
