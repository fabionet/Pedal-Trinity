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
    ("A", (280, 25), (280, 72)), ("B", (500, 25), (500, 72)), ("C", (608, 25), (608, 72)),
    ("D", (826, 25), (826, 72)), ("E", (940, 25), (940, 72)), ("F", (1048, 25), (1048, 72)),
    ("G", (50, 250), (50, 190)), ("H", (1230, 250), (1230, 190)), ("I", (1240, 25), (1240, 72)),
    ("J", (1186, 25), (1186, 115)), ("K", (1139, 25), (1110, 115))])
save(ov, "guide_overview.jpg")

# ---- dettaglio di uno slot (primo slot, vista 3 a 1280x760)
slot = annotate(main, [
    ("1", (131, 76), (131, 76)), ("2", (300, 76), (300, 145)), ("3", (185, 106), (185, 160)),
    ("4", (288, 106), (250, 170)), ("5", (355, 106), (355, 160)), ("6", (424, 106), (424, 160)),
    ("7", (218, 272), (150, 190)), ("8", (289, 232), (390, 185)), ("9", (290, 520), (400, 560)),
    ("10", (452, 330), (452, 420))])
save(slot.crop((0, 56 * S, 470 * S, 752 * S)), "guide_slot.jpg")

# ---- viste e zoom (scala 1)
save(load("view6.png"), "guide_view6.jpg", 84)
save(load("view18.png"), "guide_view18.jpg", 84)
save(load("zoom.png"), "guide_zoom.jpg", 86)
save(load("nam.png"), "guide_nam.jpg", 86)
save(load("realmod.png"), "guide_realmod.jpg", 84)
save(load("dual.png"), "guide_dual.jpg", 84)
save(load("options.png").crop((276, 56, 1324, 844)), "guide_options.jpg", 86)
save(load("cables.png"), "guide_cables.jpg", 84)
save(load("learn.png"), "guide_midilearn.jpg", 86)
save(load("midi.png").crop((116, 6, 1164, 754)), "guide_midi.jpg", 86)
# mosaico dei 18 temi (6 x 3), con il nome sotto ciascuno
names = [("pro", "Pedana Pro"), ("tolex", "Tolex e cromo"), ("walnut", "Noce e ottone"),
         ("green", "British Green"), ("alu", "Alluminio spazzolato"), ("night", "Studio notte"),
         ("redtolex", "Tolex rosso"), ("tweed", "Tweed '59"), ("diamond", "Lamiera mandorlata"),
         ("carbon", "Fibra di carbonio"), ("maple", "Acero chiaro"), ("ebony", "Ebano e argento"),
         ("surf", "Surf blu"), ("purple", "Velluto viola"), ("slate", "Ardesia"),
         ("goldrails", "Guide oro"), ("white", "Tolex bianco"), ("neon", "Neon notte")]
cols = 6
tw, th = 480, 270
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGB", (tw * cols, (th + 34) * rows), (18, 18, 20))
d = ImageDraw.Draw(sheet)
SMALL = ImageFont.truetype(F.path, 20) if hasattr(F, "path") else F
for k, (tid, label) in enumerate(names):
    im = load("theme_%s.png" % tid).resize((tw, th), Image.LANCZOS)
    x, y = (k % cols) * tw, (k // cols) * (th + 34)
    sheet.paste(im, (x, y))
    d.text((x + tw // 2, y + th + 17), label, font=SMALL, fill=GOLD, anchor="mm")
save(sheet, "guide_themes.jpg", 84)
save(load("stereo.png"), "guide_stereo.jpg", 84)
info = load("info.png")
w, h = info.size
save(info.crop((int(w * 0.18), int(h * 0.03), int(w * 0.82), int(h * 0.97))), "guide_info.jpg", 88)
print("immagini della guida in", OUT)
