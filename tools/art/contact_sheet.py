#!/usr/bin/env python3
"""Foglio provini di controllo: tutti i render dei pedali con sigla (build/contact_<n>.jpg)."""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalog  # noqa: E402

PEDALS = os.path.join(HERE, "build", "pedals")
models, _ = catalog.load_all(strict=False)
thumbs = []
font = ImageFont.truetype("/usr/share/fonts/truetype/lato/Lato-Black.ttf", 16)
for m in models:
    p = os.path.join(PEDALS, m["id"] + ".png")
    if not os.path.exists(p):
        continue
    im = Image.open(p).convert("RGB")
    im.thumbnail((180, 300))
    canvas = Image.new("RGB", (190, 330), (23, 24, 27))
    canvas.paste(im, ((190 - im.width) // 2, 0))
    ImageDraw.Draw(canvas).text((95, 318), "%s %s" % (m["code"], m["id"]), font=font, fill=(217, 180, 100), anchor="mm")
    thumbs.append(canvas)
per_sheet, cols = 60, 12
for s in range(0, len(thumbs), per_sheet):
    part = thumbs[s:s + per_sheet]
    rows = (len(part) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 190, rows * 330), (10, 10, 12))
    for k, t in enumerate(part):
        sheet.paste(t, ((k % cols) * 190, (k // cols) * 330))
    out = os.path.join(HERE, "build", "contact_%d.jpg" % (s // per_sheet + 1))
    sheet.save(out, quality=82)
    print(out, len(part))
