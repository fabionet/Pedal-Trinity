#!/usr/bin/env python3
"""
Serigrafie per ogni modello del catalogo:
  build/cat/<id>_panel.png  (pannello comandi, enclosure compatta)
  build/cat/<id>_tread.png  (pedale, enclosure compatta)
  build/cat/<id>_top.png    (piano superiore, enclosure tipo TS)

Uso: python3 tools/art/make_catalog_textures.py [id ...]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalog  # noqa: E402
import pedal_layout as PL  # noqa: E402
import make_textures as MT  # noqa: E402  (Canvas, font)

OUT = os.path.join(HERE, "build", "cat")
os.makedirs(OUT, exist_ok=True)
MT.OUT = OUT


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def colours(m):
    body = tuple(m["colour"])
    text = tuple(m["text"]) if m.get("text") else ((22, 22, 24) if lum(body) > 118 else (238, 236, 222))
    accent = tuple(m["accent"]) if m.get("accent") else text
    if m.get("style") != "ts" and abs(lum(accent) - lum(body)) < 55 and max(abs(a - b) for a, b in zip(accent, body)) < 110:
        accent = text   # accento illeggibile sul corpo: titolo nel colore del testo
    minor = (240, 120, 30) if lum(body) < 110 else ((190, 30, 30) if lum(body) > 170 else (250, 250, 245))
    return body, text, accent, minor


SUBTITLES = {"Overdrive / Boost": "OVERDRIVE", "Distorsione": "DISTORTION", "Fuzz": "FUZZ", "Metal": "METAL DISTORTION",
             "Basso": "BASS", "Dinamica": "COMPRESSOR", "Equalizzatori": "EQUALIZER", "Wah / Filtri": "FILTER",
             "Chorus / Dimension": "CHORUS", "Flanger": "FLANGER", "Phaser": "PHASER", "Tremolo / Pan / Slicer": "TREMOLO",
             "Vibrato / Rotary": "VIBRATO", "Delay": "DIGITAL DELAY", "Eco a nastro": "TAPE ECHO", "Riverbero": "REVERB",
             "Pitch / Ottave": "PITCH SHIFTER", "Synth": "SYNTHESIZER", "Acustica": "ACOUSTIC", "Amp / IR": "AMP & CABINET",
             "Utility / Routing": "LINE SELECTOR", "Tuner / Looper": "TUNER / LOOPER"}


def fit_size(label, base, maxw_m, font):
    """Riduce la dimensione del testo finche' sta nella larghezza disponibile."""
    from PIL import ImageFont
    size = base
    while size > 0.0012:
        f = ImageFont.truetype(font, max(6, int(size * MT.PPM)))
        if f.getlength(label) / MT.PPM <= maxw_m:
            return size
        size *= 0.92
    return size


def panel_texture(m, lay):
    body, text, accent, minor = colours(m)
    c = MT.Canvas(-PL.W / 2, PL.W / 2, lay["panel_y0"], PL.PANEL_Y1, body)
    c.speckle(3.0, 0.6)
    ctrls = m["controls"]
    # ---- cursori (EQ grafico)
    if lay["sliders"]:
        s = lay["sliders"]
        y0, y1 = s["y0"], s["y1"]
        for i in range(7):
            yy = y0 + (y1 - y0) * i / 6
            major = i in (0, 3, 6)
            for k in range(s["n"]):
                x = s["x0"] + k * s["pitch"]
                half = min(0.0026, s["pitch"] * 0.36) if major else min(0.0016, s["pitch"] * 0.24)
                c.line(x - half, yy, x - 0.0010, yy, text, 0.00030 if major else 0.00022)
                c.line(x + 0.0010, yy, x + half, yy, text, 0.00030 if major else 0.00022)
        xl = s["x0"] - 0.0048
        c.text(xl, y1, "+15", 0.0016, MT.F_BOLD, text)
        c.text(xl, (y0 + y1) / 2, "0", 0.0016, MT.F_BOLD, text)
        c.text(xl, y0, "-15", 0.0016, MT.F_BOLD, text)
        for k, ctl in enumerate([x for x in ctrls if x["kind"] == "slider"]):
            x = s["x0"] + k * s["pitch"]
            c.rect(x - 0.00075, y0 - 0.0015, x + 0.00075, y1 + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            col = minor if ctl["role"] == "level" else text
            size = fit_size(ctl["label"], 0.0019, s["pitch"] * 1.05, MT.F_BOLD)
            c.text(x, y1 + 0.0068, ctl["label"], size, MT.F_BOLD, col)
        c.text(0.0, y0 - 0.0045, (m.get("subtitle") or "EQUALIZER") + "  -  Hz", 0.0016, MT.F_BOLD, text, spacing=0.0002)
    # ---- pomelli
    for k, ctl in enumerate(ctrls):
        p = lay["controls"][k]
        if ctl["kind"] in ("knob", "selector", "outer"):
            r = p["r"]
            ty = p["y"] + r + 0.0046 if p.get("label_above") else p["y"] - r - 0.0052
            if ctl["kind"] == "outer":
                inn = ctrls[k + 1]
                c.text(p["x"], ty + 0.0006, ctl["label"], fit_size(ctl["label"], 0.0022, 0.0165, MT.F_BOLD), MT.F_BOLD, text)
                c.text(p["x"], ty - 0.0020, inn["label"], fit_size(inn["label"], 0.0019, 0.0165, MT.F_BOLD), MT.F_BOLD, minor)
            else:
                c.text(p["x"], ty, ctl["label"], fit_size(ctl["label"], 0.0024, 0.0165, MT.F_BOLD), MT.F_BOLD, text)
            if ctl["kind"] == "selector":
                names = ctl.get("short") or [str(x)[:4] for x in ctl["choices"]]
                n = len(names)
                for j, nm in enumerate(names):
                    prop = 0.08 + 0.84 * j / max(1, n - 1)
                    a = math.radians(-150 + 300 * prop)
                    rr = r + 0.0030
                    c.text(p["x"] + math.sin(a) * rr, p["y"] + math.cos(a) * rr, nm, 0.0012, MT.F_BOLD, text)
            else:
                c.arc_ticks(p["x"], p["y"], r + 0.0010, r + 0.0018, 2, text, 0.00032)
        elif ctl["kind"] == "toggle":
            ch = ctl["choices"] or ["S", "C"]
            c.text(p["x"] - 0.0058, p["y"] + 0.0010, str(ch[0])[:5], 0.0019, MT.F_BOLD, text)
            c.text(p["x"] + 0.0058, p["y"] + 0.0010, str(ch[1])[:5], 0.0019, MT.F_BOLD, minor)
            c.text(p["x"], p["y"] - 0.0040, ctl["label"], 0.0014, MT.F_BOLD, text)
        elif ctl["kind"] == "button":
            c.circle(p["x"], p["y"], p["r"] + 0.0009, fill=(40, 40, 42), outline=(120, 120, 124), width_m=0.0003)
            c.text(p["x"], p["y"] - p["r"] - 0.0028, ctl["label"], fit_size(ctl["label"], 0.0017, 0.011, MT.F_BOLD), MT.F_BOLD, text)
    # ---- display
    if lay.get("display"):
        x0, y0, x1, y1 = lay["display"]
        c.rect(x0 - 0.0008, y0 - 0.0008, x1 + 0.0008, y1 + 0.0008, fill=(60, 62, 66), radius_m=0.0012)
        c.rect(x0, y0, x1, y1, fill=(6, 7, 8), radius_m=0.0009)
    # ---- LED
    if lay["led"] and not lay.get("led_on_tread"):
        lx, ly, _, lr = lay["led"]
        if lx < 0:
            c.text(lx + lr + 0.0025, ly, "CHECK", 0.0019, MT.F_BOLD, text, anchor="lm")
        else:
            c.text(lx - lr - 0.0025, ly, "CHECK", 0.0019, MT.F_BOLD, text, anchor="rm")
    c.save("%s_panel.png" % m["id"])


def tread_texture(m, lay):
    body, text, accent, minor = colours(m)
    ty1 = lay["tread_y1"]
    length = math.hypot(ty1 - PL.TREAD_Y0, PL.TREAD_H_BACK - PL.TREAD_H_FRONT)
    c = MT.Canvas(-PL.W / 2, PL.W / 2, -length / 2, length / 2, body)
    c.speckle(3.5, 0.6)
    top = length / 2
    c.text(-0.0300, top - 0.0055, "FabioNET", 0.0030, MT.F_BOLDIT, text, anchor="lm")
    if lay.get("led_on_tread"):
        lx, ly = lay["led"][0], lay["led"][1]
        t = (ly - PL.TREAD_Y0) / (ty1 - PL.TREAD_Y0)
        c.text(lx - 0.0048, -length / 2 + t * length, "CHECK", 0.0021, MT.F_BOLD, text, anchor="rm")
    elif m.get("stereo"):
        c.text(0.0300, top - 0.0055, "STEREO", 0.0022, MT.F_BOLD, text, anchor="rm", spacing=0.0002)
    name_size = fit_size(m["name"], 0.0086, 0.060, MT.F_BLACKIT)
    c.text(0.0, top - 0.0160, m["name"], name_size, MT.F_BLACKIT, accent)
    c.line(-0.0300, top - 0.0228, 0.0300, top - 0.0228, text, 0.00030)
    sub = (m.get("subtitle") or SUBTITLES.get(m["category"], "EFFECT")).upper().strip()
    c.text(-0.0300, top - 0.0272, sub, fit_size(sub, 0.0024, 0.034, MT.F_BOLD), MT.F_BOLD, text, anchor="lm", spacing=0.0002)
    c.text(0.0300, top - 0.0274, m["code"], fit_size(m["code"], 0.0052, 0.026, MT.F_BLACK), MT.F_BLACK, text, anchor="rm")
    c.save("%s_tread.png" % m["id"])


def ts_texture(m, lay):
    body, text, accent, minor = colours(m)
    c = MT.Canvas(-PL.TS_W / 2, PL.TS_W / 2, -PL.TS_D / 2, PL.TS_D / 2, body)
    c.speckle(5.0, 0.7)
    for k, ctl in enumerate(m["controls"]):
        p = lay["controls"][k]
        if p["strip"] == "ts_big":
            c.text(p["x"], p["y"] + p["r"] + 0.0094, ctl["label"], 0.0034, MT.F_BOLD, (238, 236, 222), spacing=0.0004)
        else:
            c.text(p["x"], p["y"] - p["r"] - 0.0040, ctl["label"], 0.0032, MT.F_BOLD, (238, 236, 222), spacing=0.0004)
        c.arc_ticks(p["x"], p["y"], p["r"] + 0.0012, p["r"] + 0.0022, 11, (238, 236, 222), 0.00035)
    lx, ly = lay["led"][0], lay["led"][1]
    c.text(lx, ly + 0.0052, "ON", 0.0025, MT.F_BOLD, (238, 236, 222))
    y0, y1 = -0.0118, 0.0052
    c.rect(-0.0335, y0, 0.0335, y1, fill=(200, 202, 198), radius_m=0.0010)
    c.rect(-0.0335, y0, 0.0335, y1, outline=(120, 124, 120), width_m=0.00025, radius_m=0.0010)
    word = m["name"].split()[0].upper()
    c.text(0.0, 0.0006, word, fit_size(word, 0.0070, 0.060, MT.F_BLACK), MT.F_BLACK, accent, spacing=0.0006)
    sub = ((m.get("subtitle") or "OVERDRIVE") + "  " + m["code"]).upper()
    c.text(0.0, -0.0071, sub, fit_size(sub, 0.0034, 0.058, MT.F_BOLD), MT.F_BOLD, (30, 70, 40), spacing=0.00045)
    c.line(-0.030, -0.0040, 0.030, -0.0040, (30, 90, 45), 0.0003)
    c.text(0.0, -0.0600, "FabioNET  •  PEDAL TRINITY", 0.0022, MT.F_BOLD, (238, 236, 222), spacing=0.0002)
    c.save("%s_top.png" % m["id"])


def nam_texture(m, lay):
    """Piano superiore del NAM-A1A2 (contenitore grande): display, pomelli, gruppi A/B, footswitch."""
    body, text, accent, minor = colours(m)
    c = MT.Canvas(-PL.NAM_W / 2, PL.NAM_W / 2, -PL.NAM_D / 2, PL.NAM_D / 2, body)
    c.speckle(3.0, 0.6)
    ctrls = m["controls"]
    gold = (226, 190, 110)
    # display dei meter con cornice
    x0, y0, x1, y1 = lay["display"]
    c.rect(x0 - 0.0012, y0 - 0.0012, x1 + 0.0012, y1 + 0.0012, fill=(40, 12, 14), radius_m=0.0016)
    c.rect(x0, y0, x1, y1, fill=(6, 7, 8), radius_m=0.0012)
    c.text(-0.0400, y1 + 0.0036, "FabioNET", 0.0023, MT.F_BLACKIT, text, anchor="lm")
    c.text(0.0400, y1 + 0.0036, "STEREO  A / B", 0.0019, MT.F_BOLD, text, anchor="rm")
    # pomelli
    for k, ctl in enumerate(ctrls):
        pp = lay["controls"][k]
        if ctl["kind"] != "knob":
            continue
        r = pp["r"]
        c.text(pp["x"], pp["y"] - r - 0.0050, ctl["label"], fit_size(ctl["label"], 0.0022, 0.0150, MT.F_BOLD), MT.F_BOLD, text)
        c.arc_ticks(pp["x"], pp["y"], r + 0.0008, r + 0.0014, 11, text, 0.00026)
    # gruppi dei canali sotto la seconda fila
    for x, label in ((-0.02275, "CANALE A"), (0.02275, "CANALE B")):
        yb = PL.NAM_ROW2 - 0.0073 - 0.0092
        c.line(x - 0.0125, yb + 0.0018, x + 0.0125, yb + 0.0018, gold, 0.00030)
        c.text(x, yb - 0.0006, label, 0.0021, MT.F_BOLD, gold, spacing=0.0003)
    # nome del modello
    c.text(0.0, -0.0228, m["name"], fit_size(m["name"], 0.0068, 0.078, MT.F_BLACKIT), MT.F_BLACKIT, accent)
    c.line(-0.038, -0.0268, 0.038, -0.0268, text, 0.00035)
    c.text(-0.038, -0.0292, (m.get("subtitle") or "").upper(), 0.0019, MT.F_BOLD, text, anchor="lm")
    c.text(0.038, -0.0292, m["code"], 0.0032, MT.F_BLACK, text, anchor="rm")
    # LED e footswitch A / B
    for k, ctl in enumerate(ctrls):
        pp = lay["controls"][k]
        if ctl["kind"] != "toggle":
            continue
        lx, ly, _ = pp["led"]
        c.text(lx + 0.0060, ly, ctl["label"], 0.0036, MT.F_BLACK, text, anchor="lm")
        c.circle(pp["x"], pp["y"], pp["r"] + 0.0024, outline=gold, width_m=0.00030)
    c.text(0.0, -0.0705, "FabioNET  \u2022  PEDAL TRINITY  \u2022  NAM CORE (MIT)", 0.0017, MT.F_BOLD, text, spacing=0.0002)
    c.save("%s_top.png" % m["id"])


def main(ids):
    models, errors = catalog.load_all(strict=False)
    if errors:
        print("ATTENZIONE, catalogo con errori (modelli comunque generati):\n  " + "\n  ".join(errors[:20]))
    for m in models:
        if ids and m["id"] not in ids:
            continue
        lay = PL.pedal_layout(m)
        if m["style"] == "ts":
            ts_texture(m, lay)
        elif m["style"] == "nam":
            nam_texture(m, lay)
        else:
            panel_texture(m, lay)
            tread_texture(m, lay)


if __name__ == "__main__":
    main(set(sys.argv[1:]))
