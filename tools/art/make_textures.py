#!/usr/bin/env python3
"""
Genera le texture "serigrafate" dei pedali (colore vernice + scritte) usate
da render_scene.py. Uscita: tools/art/build/tex_*.png

Uso: python3 tools/art/make_textures.py
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
os.makedirs(OUT, exist_ok=True)

FONTDIR = "/usr/share/fonts/opentype/urw-base35/"
F_BOLD = FONTDIR + "NimbusSans-Bold.otf"
F_BOLDIT = FONTDIR + "NimbusSans-BoldItalic.otf"
F_NARROW = FONTDIR + "NimbusSansNarrow-Bold.otf"
F_REG = FONTDIR + "NimbusSans-Regular.otf"
F_BLACK = "/usr/share/fonts/truetype/lato/Lato-Black.ttf"
F_BLACKIT = "/usr/share/fonts/truetype/lato/Lato-BlackItalic.ttf"

PPM = L.TEX_PPM
rng = np.random.default_rng(7)


class Canvas:
    """Area rettangolare in metri: x in [x0,x1], y in [y0,y1] (y verso l'alto)."""

    def __init__(self, x0, x1, y0, y1, color):
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.w = int(round((x1 - x0) * PPM))
        self.h = int(round((y1 - y0) * PPM))
        self.img = Image.new("RGB", (self.w, self.h), color)
        self.d = ImageDraw.Draw(self.img)

    def px(self, x, y):
        return ((x - self.x0) * PPM, (self.y1 - y) * PPM)

    def text(self, x, y, s, size_m, font=F_BOLD, fill=(240, 240, 232),
             anchor="mm", spacing=0.0):
        f = ImageFont.truetype(font, max(6, int(size_m * PPM)))
        if spacing:
            # lettere spaziate (tracking)
            widths = [f.getlength(c) for c in s]
            gap = spacing * PPM
            total = sum(widths) + gap * (len(s) - 1)
            cx, cy = self.px(x, y)
            if anchor[0] == "m":
                sx = cx - total / 2
            elif anchor[0] == "l":
                sx = cx
            else:
                sx = cx - total
            for c, wdt in zip(s, widths):
                self.d.text((sx, cy), c, font=f, fill=fill, anchor="l" + anchor[1])
                sx += wdt + gap
        else:
            self.d.text(self.px(x, y), s, font=f, fill=fill, anchor=anchor)

    def rect(self, x0, y0, x1, y1, fill=None, outline=None, width_m=0.0, radius_m=0.0):
        a = self.px(x0, y1)
        b = self.px(x1, y0)
        self.d.rounded_rectangle([a, b], radius=radius_m * PPM, fill=fill,
                                 outline=outline, width=max(1, int(width_m * PPM)))

    def line(self, x0, y0, x1, y1, fill, width_m):
        self.d.line([self.px(x0, y0), self.px(x1, y1)], fill=fill,
                    width=max(1, int(round(width_m * PPM))))

    def circle(self, x, y, r, fill=None, outline=None, width_m=0.0):
        cx, cy = self.px(x, y)
        rp = r * PPM
        self.d.ellipse([cx - rp, cy - rp, cx + rp, cy + rp], fill=fill,
                       outline=outline, width=max(1, int(width_m * PPM)))

    def arc_ticks(self, x, y, r0, r1, n, fill, width_m, a0=-150, a1=150):
        for i in range(n):
            a = math.radians(a0 + (a1 - a0) * i / (n - 1))
            # angolo 0 = ore 12, positivo in senso orario
            sx, sy = math.sin(a), math.cos(a)
            self.line(x + sx * r0, y + sy * r0, x + sx * r1, y + sy * r1, fill, width_m)

    def speckle(self, amount=4.0, blur=0.6):
        a = np.asarray(self.img).astype(np.float32)
        n = rng.normal(0, amount, a.shape[:2])[..., None]
        n = np.asarray(Image.fromarray(np.clip(n + 128, 0, 255).astype(np.uint8)[..., 0])
                       .filter(ImageFilter.GaussianBlur(blur))).astype(np.float32)[..., None] - 128
        self.img = Image.fromarray(np.clip(a + n, 0, 255).astype(np.uint8))
        self.d = ImageDraw.Draw(self.img)

    def save(self, name):
        self.img.save(os.path.join(OUT, name))
        print("scritto", name, self.img.size)


CREAM = (238, 236, 222)
ORANGE = (240, 120, 30)
GOLD = (214, 176, 92)


# --------------------------------------------------------------------------
# ED-9 (overdrive verde)
# --------------------------------------------------------------------------
def tex_ts():
    p = L.TS
    c = Canvas(-p["W"] / 2, p["W"] / 2, -p["D"] / 2, p["D"] / 2, p["paint"])
    c.speckle(5.0, 0.7)
    for k in p["knobs"]:
        r = L.KNOB_TYPES[k["type"]]["r"]
        if k["type"] == "ts_big":
            c.text(k["x"], k["y"] + r + 0.0094, k["label"], 0.0034, F_BOLD, CREAM, spacing=0.0004)
        else:
            c.text(k["x"], k["y"] - r - 0.0040, k["label"], 0.0032, F_BOLD, CREAM, spacing=0.0004)
        c.arc_ticks(k["x"], k["y"], r + 0.0012, r + 0.0022, 11, CREAM, 0.00035)
    led = p["led"]
    c.text(led["x"], led["y"] + 0.0052, "ON", 0.0025, F_BOLD, CREAM)
    # banda argentata con il nome
    y0, y1 = -0.0118, 0.0052
    c.rect(-0.0335, y0, 0.0335, y1, fill=(200, 202, 198), radius_m=0.0010)
    c.rect(-0.0335, y0, 0.0335, y1, outline=(120, 124, 120), width_m=0.00025, radius_m=0.0010)
    c.text(0.0, 0.0006, "EMERALD", 0.0070, F_BLACK, p["paint"], spacing=0.0006)
    c.text(0.0, -0.0071, "OVERDRIVE  ED-9", 0.0034, F_BOLD, (30, 70, 40), spacing=0.00045)
    c.line(-0.030, -0.0040, 0.030, -0.0040, (30, 90, 45), 0.0003)
    # piccolo marchio in basso
    c.text(0.0, -0.0600, "FabioNET  •  PEDAL TRINITY", 0.0022, F_BOLD, CREAM, spacing=0.0002)
    c.save("tex_ed9_top.png")


# --------------------------------------------------------------------------
# Pannelli Boss-style
# --------------------------------------------------------------------------
def boss_panel_canvas(p):
    return Canvas(-p["W"] / 2, p["W"] / 2, p["panel_y0"], p["D"] / 2, p["panel_paint"])


def tex_mt_panel():
    p = L.MT
    c = boss_panel_canvas(p)
    c.speckle(3.0, 0.6)
    for k in p["knobs"]:
        r = L.KNOB_TYPES[k["type"]]["r"]
        ty = k["y"] - r - 0.0037
        if "inner" in k:
            c.text(k["x"], ty + 0.0008, k["label"], 0.0024, F_BOLD, CREAM, spacing=0.0002)
            c.text(k["x"], ty - 0.0022, k["inner"]["label"], 0.0021, F_BOLD, ORANGE, spacing=0.0001)
        else:
            c.text(k["x"], ty, k["label"], 0.0026, F_BOLD, CREAM, spacing=0.0002)
        c.arc_ticks(k["x"], k["y"], r + 0.0010, r + 0.0019, 2, CREAM, 0.00035)
    led = p["led"]
    c.text(led["x"] + 0.0052, led["y"], "CHECK", 0.0021, F_BOLD, CREAM, anchor="lm")
    t = p["toggle"]
    c.text(t["x"] - 0.0060, t["y"] + 0.0012, "S", 0.0024, F_BOLD, CREAM)
    c.text(t["x"] - 0.0060, t["y"] - 0.0020, "STD", 0.0014, F_BOLD, CREAM)
    c.text(t["x"] + 0.0060, t["y"] + 0.0012, "C", 0.0024, F_BOLD, ORANGE)
    c.text(t["x"] + 0.0060, t["y"] - 0.0020, "CUSTOM", 0.0014, F_BOLD, ORANGE)
    c.save("tex_mc2_panel.png")


def tex_eq_panel():
    p = L.EQ
    c = boss_panel_canvas(p)
    c.speckle(2.0, 0.6)
    y0, y1 = p["slot_y0"], p["slot_y1"]
    ym = (y0 + y1) / 2
    # linee della griglia dB
    for i in range(7):
        yy = y0 + (y1 - y0) * i / 6
        major = i in (0, 3, 6)
        for s in range(len(p["sliders"]) - 1):
            x = L.slider_x(s)
            half = 0.0026 if major else 0.0016
            c.line(x - half, yy, x - 0.0010, yy, CREAM, 0.00030 if major else 0.00022)
            c.line(x + 0.0010, yy, x + half, yy, CREAM, 0.00030 if major else 0.00022)
    # scala dB a sinistra
    xl = L.slider_x(0) - 0.0048
    c.text(xl, y1, "+15", 0.0017, F_BOLD, CREAM)
    c.text(xl, ym, "0", 0.0017, F_BOLD, CREAM)
    c.text(xl, y0, "-15", 0.0017, F_BOLD, CREAM)
    c.text(xl, y1 + 0.0035, "dB", 0.0015, F_BOLD, CREAM)
    # separatore prima del LEVEL
    xs = (L.slider_x(6) + L.slider_x(7)) / 2
    c.line(xs, y0 - 0.002, xs, y1 + 0.002, (90, 90, 90), 0.00025)
    # fessure e scritte frequenze
    for i, s in enumerate(p["sliders"]):
        x = L.slider_x(i)
        c.rect(x - 0.00075, y0 - 0.0015, x + 0.00075, y1 + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
        col = ORANGE if s["id"] == "eq_level" else CREAM
        c.text(x, y1 + 0.0072, s["label"], 0.0019 if len(s["label"]) < 5 else 0.0015,
               F_BOLD, col)
    c.text(0.0, y0 - 0.0045, "GRAPHIC EQUALIZER  •  Hz", 0.0017, F_BOLD, (150, 150, 150),
           spacing=0.0002)
    c.save("tex_gq7_panel.png")


def treadle_canvas(p, paint):
    length = math.hypot(p["tread_y1"] - p["tread_y0"], p["tread_h_back"] - p["tread_h_front"])
    return Canvas(-p["W"] / 2, p["W"] / 2, -length / 2, length / 2, paint), length


def tread_local_y(p, y):
    """Converte y del pedale in y locale lungo il treadle."""
    length = math.hypot(p["tread_y1"] - p["tread_y0"], p["tread_h_back"] - p["tread_h_front"])
    t = (y - p["tread_y0"]) / (p["tread_y1"] - p["tread_y0"])
    return -length / 2 + t * length


def tex_mt_tread():
    p = L.MT
    c, length = treadle_canvas(p, p["paint"])
    c.speckle(3.5, 0.6)
    top = length / 2
    c.text(-0.0300, top - 0.0055, "FabioNET", 0.0030, F_BOLDIT, CREAM, anchor="lm")
    c.text(0.0300, top - 0.0055, "CUSTOM CRAFT", 0.0024, F_BOLD, GOLD, anchor="rm", spacing=0.0002)
    c.line(-0.0300, top - 0.0085, 0.0300, top - 0.0085, GOLD, 0.00030)
    c.text(0.0, top - 0.0165, "Metal Core", 0.0092, F_BLACKIT, ORANGE)
    c.text(-0.0300, top - 0.0270, "DISTORTION", 0.0026, F_BOLD, CREAM, anchor="lm", spacing=0.0003)
    c.text(0.0300, top - 0.0272, "MC-2W", 0.0052, F_BLACK, CREAM, anchor="rm")
    c.save("tex_mc2_tread.png")


def tex_eq_tread():
    p = L.EQ
    c, length = treadle_canvas(p, p["paint"])
    c.speckle(6.0, 0.5)
    top = length / 2
    dark = (28, 30, 34)
    c.text(-0.0300, top - 0.0055, "FabioNET", 0.0030, F_BOLDIT, dark, anchor="lm")
    ly = tread_local_y(p, p["led"]["y"])
    c.text(p["led"]["x"] - 0.0048, ly, "CHECK", 0.0021, F_BOLD, dark, anchor="rm")
    c.text(0.0300, top - 0.0165, "Graphic EQ", 0.0072, F_BLACKIT, (20, 60, 150), anchor="rm")
    c.line(-0.0300, top - 0.0232, 0.0300, top - 0.0232, dark, 0.00030)
    c.text(-0.0300, top - 0.0275, "7-BAND EQUALIZER", 0.0024, F_BOLD, dark, anchor="lm",
           spacing=0.0002)
    c.text(0.0300, top - 0.0277, "GQ-7", 0.0052, F_BLACK, dark, anchor="rm")
    c.save("tex_gq7_tread.png")


# --------------------------------------------------------------------------
# Targhetta in alluminio spazzolato
# --------------------------------------------------------------------------
def tex_plate():
    p = L.PLATE
    c = Canvas(-p["w"] / 2, p["w"] / 2, -p["d"] / 2, p["d"] / 2, (176, 178, 180))
    # spazzolatura orizzontale
    a = np.asarray(c.img).astype(np.float32)
    streak = rng.normal(0, 14, (a.shape[0], 1))
    streak = streak + rng.normal(0, 8, (a.shape[0], a.shape[1])) * 0.35
    k = np.ones(81) / 81
    streak = np.apply_along_axis(lambda r: np.convolve(r, k, mode="same"), 1, streak)
    a += streak[..., None]
    c.img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    c.d = ImageDraw.Draw(c.img)
    eng = (58, 60, 64)
    c.text(-p["w"] / 2 + 0.008, 0.0, "PEDAL TRINITY", 0.0078, F_BLACK, eng, anchor="lm",
           spacing=0.0009)
    c.text(0.0, 0.0022, "OVERDRIVE  •  DISTORTION  •  GRAPHIC EQ", 0.0030, F_BOLD, eng,
           spacing=0.00035)
    c.text(0.0, -0.0030, "by FabioNET   •   v1.0.0 beta   •   GNU GPL v3", 0.0026,
           F_BOLD, eng, spacing=0.0002)
    ib = L.INFO_BUTTON
    c.text(ib["x"] - ib["r"] - 0.0030, 0.0, "INFO", 0.0030, F_BOLD, eng, anchor="rm",
           spacing=0.0003)
    c.circle(ib["x"], 0.0, ib["r"] + 0.0010, outline=eng, width_m=0.0003)
    # viti agli angoli
    for sx in (-1, 1):
        x = sx * (p["w"] / 2 - 0.0035)
        c.circle(x, 0.0, 0.0018, fill=(120, 122, 125), outline=(70, 70, 72), width_m=0.00025)
        c.line(x - 0.0012, -0.0006, x + 0.0012, 0.0006, (60, 60, 62), 0.00035)
    c.save("tex_plate.png")


if __name__ == "__main__":
    tex_ts()
    tex_mt_panel()
    tex_eq_panel()
    tex_mt_tread()
    tex_eq_tread()
    tex_plate()
