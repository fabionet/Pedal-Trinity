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


# ---------------------------------------------------------------- scritte senza sforamenti
# Le scritte si stampano solo dove restano visibili nell'immagine: fuori dalla sagoma dei pomelli
# disegnati dal programma (che nella vista inclinata coprono anche il pannello dietro di loro),
# del LED, delle levette, dei display e dentro il pannello (lontano dal bordo smussato).
def text_box(x, y, s, size, font, anchor="mm", spacing=0.0):
    """Riquadro (x0, y0, x1, y1) in metri di una scritta di MT.Canvas.text."""
    from PIL import ImageFont
    f = ImageFont.truetype(font, max(6, int(size * MT.PPM)))
    w = (sum(f.getlength(ch) for ch in s) + spacing * MT.PPM * (len(s) - 1)) if spacing else f.getlength(s)
    bb = f.getbbox(s or " ", anchor="ls")
    w /= MT.PPM
    hh = (bb[3] - bb[1]) / MT.PPM
    x0 = x - w / 2 if anchor[0] == "m" else (x if anchor[0] == "l" else x - w)
    y0 = y - hh / 2 if anchor[1] == "m" else y
    return (x0, y0, x0 + w, y0 + hh)


class Placer:
    """Ostacoli in coordinate del pannello: capsule verticali (pomelli), cerchi, rettangoli (testi, display)."""

    def __init__(self, bounds, margin=0.0003):
        self.bounds = bounds          # (x0, y0, x1, y1) zona stampabile
        self.obs = []
        self.margin = margin

    def capsule(self, x, y0, y1, r):
        self.obs.append(("cap", x, y0, y1, r))

    def circle(self, x, y, r):
        self.obs.append(("cap", x, y, y, r))

    def rect(self, box):
        self.obs.append(("rect",) + tuple(box))

    def knob(self, x, y, r, h):
        """Sagoma del pomello nell'immagine riportata sul pannello: dalla base alla sommita' proiettata."""
        self.capsule(x, y, y + PL.knob_shadow(h), r + 0.0004)

    def free(self, box, skip=None):
        x0, y0, x1, y1 = box
        bx0, by0, bx1, by1 = self.bounds
        if x0 < bx0 or x1 > bx1 or y0 < by0 or y1 > by1:
            return False
        for o in self.obs:
            if o is skip:
                continue
            if o[0] == "rect":
                if x0 < o[3] + self.margin and o[1] - self.margin < x1 and y0 < o[4] + self.margin and o[2] - self.margin < y1:
                    return False
            else:
                _, cx, cy0, cy1, r = o
                dx = max(0.0, x0 - cx, cx - x1)
                dy = max(0.0, y0 - cy1, cy0 - y1)
                if math.hypot(dx, dy) < r + self.margin:
                    return False
        return True


def ring_names(c, pl, x, y, r, h, names, size, colour, font=None, r_gap=0.0030):
    """Nomi delle posizioni di un selettore attorno al pomello, agli stessi angoli dell'indice.
    Il cerchio dei nomi segue la sagoma visibile del pomello (dietro si allunga della sommita' proiettata);
    un nome che finirebbe coperto o fuori dal pannello diventa una tacca, se visibile."""
    font = font or MT.F_BOLD
    n = len(names)
    s = PL.knob_shadow(h)
    tick_col = colour
    for j, nm in enumerate(names):
        a = math.radians(-150 + 300 * (0.08 + 0.84 * j / max(1, n - 1)))
        sa, ca = math.sin(a), math.cos(a)
        done = False
        # prima alla distanza normale, poi un po' piu' fuori, infine piu' vicino e in corpo minore
        for extra, sz in ((0.0, size), (0.0008, size), (0.0016, size), (-0.0006, size * 0.85), (0.0004, size * 0.85)):
            rr = r + r_gap + extra
            px, py = x + sa * rr, y + ca * rr + s * max(0.0, ca)
            box = text_box(px, py, nm, sz, font)
            if pl.free(box):
                c.text(px, py, nm, sz, font, colour)
                pl.rect(box)
                done = True
                break
        if not done:
            r0, r1 = r + 0.0010, r + 0.0019
            q0 = (x + sa * r0, y + ca * r0 + s * max(0.0, ca))
            q1 = (x + sa * r1, y + ca * r1 + s * max(0.0, ca))
            tb = (min(q0[0], q1[0]) - 0.0002, min(q0[1], q1[1]) - 0.0002, max(q0[0], q1[0]) + 0.0002, max(q0[1], q1[1]) + 0.0002)
            if pl.free(tb):
                c.line(q0[0], q0[1], q1[0], q1[1], tick_col, 0.00030)


def toggle_dx(label, size):
    """Distanza dal centro della levetta delle scritte delle due posizioni: le scritte lunghe (THRU, MUTE...)
    si allontanano quanto basta per non finire sotto il dado della levetta."""
    w = text_box(0.0, 0.0, label, size, MT.F_BOLD)[2] * 2
    return max(0.0058, 0.0038 + w / 2)


def panel_placer(m, lay, x0, x1, y0, y1, bevel=0.0025):
    """Ostacoli del pannello dei compatti: pomelli (con la sommita' proiettata), LED, levette, pulsanti, display."""
    pl = Placer((x0 + bevel, y0 + bevel, x1 - bevel, y1 - bevel))
    for k, ctl in enumerate(m["controls"]):
        p = lay["controls"][k]
        st = p["strip"]
        if st in ("boss", "ts_big", "ts_small", "boss_small"):
            pl.knob(p["x"], p["y"], p["r"], p["h"])
        elif st == "boss_outer":
            pl.knob(p["x"], p["y"], p["r"], p["h"] + PL.KNOB_H["boss_inner"])
        elif st == "toggle":
            pl.capsule(p["x"], p["y"], p["y"] + 0.0060, 0.0032)
        elif st == "button":
            pl.circle(p["x"], p["y"], p["r"] + 0.0012)
        elif st == "slider":
            pl.rect((p["x"] - 0.0030, p["y"] - 0.0035, p["x"] + 0.0030, p.get("y1", p["y"]) + 0.0035))
    if lay.get("led"):
        lx, ly, _, lr = lay["led"]
        if not lay.get("led_on_tread"):
            pl.capsule(lx, ly, ly + 0.0015, lr * 1.45 + 0.0004)
    if lay.get("display"):
        dx0, dy0, dx1, dy1 = lay["display"]
        pl.rect((dx0 - 0.0010, dy0 - 0.0010, dx1 + 0.0010, dy1 + 0.0010))
    return pl


def panel_texture(m, lay):
    body, text, accent, minor = colours(m)
    c = MT.Canvas(-PL.W / 2, PL.W / 2, lay["panel_y0"], PL.PANEL_Y1, body)
    c.speckle(3.0, 0.6)
    ctrls = m["controls"]
    pl = panel_placer(m, lay, -PL.W / 2, PL.W / 2, lay["panel_y0"], PL.PANEL_Y1)

    def T(x, y, s, size, font, col, anchor="mm", spacing=0.0):
        c.text(x, y, s, size, font, col, anchor=anchor, spacing=spacing)
        pl.rect(text_box(x, y, s, size, font, anchor, spacing))

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
        T(xl, y1, "+15", 0.0016, MT.F_BOLD, text)
        T(xl, (y0 + y1) / 2, "0", 0.0016, MT.F_BOLD, text)
        T(xl, y0, "-15", 0.0016, MT.F_BOLD, text)
        for k, ctl in enumerate([x for x in ctrls if x["kind"] == "slider"]):
            x = s["x0"] + k * s["pitch"]
            c.rect(x - 0.00075, y0 - 0.0015, x + 0.00075, y1 + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            col = minor if ctl["role"] == "level" else text
            size = fit_size(ctl["label"], 0.0019, s["pitch"] * 1.05, MT.F_BOLD)
            T(x, y1 + 0.0068, ctl["label"], size, MT.F_BOLD, col)
        T(0.0, y0 - 0.0045, (m.get("subtitle") or "EQUALIZER") + "  -  Hz", 0.0016, MT.F_BOLD, text, spacing=0.0002)
    # ---- pomelli: etichette (i nomi delle posizioni dei selettori dopo, negli spazi rimasti liberi)
    rings = []
    for k, ctl in enumerate(ctrls):
        p = lay["controls"][k]
        if ctl["kind"] in ("knob", "selector", "outer"):
            r = p["r"]
            if p.get("label_above"):
                # sopra il pomello, oltre la sommita' che nell'immagine copre il pannello dietro di lui
                ty = p["y"] + r + PL.knob_shadow(p["h"]) + 0.0034
            else:
                ty = p["y"] - r - 0.0052
            if ctl["kind"] == "outer":
                inn = ctrls[k + 1]
                T(p["x"], ty + 0.0006, ctl["label"], fit_size(ctl["label"], 0.0022, 0.0165, MT.F_BOLD), MT.F_BOLD, text)
                T(p["x"], ty - 0.0020, inn["label"], fit_size(inn["label"], 0.0019, 0.0165, MT.F_BOLD), MT.F_BOLD, minor)
            else:
                T(p["x"], ty, ctl["label"], fit_size(ctl["label"], 0.0024, 0.0165, MT.F_BOLD), MT.F_BOLD, text)
            if ctl["kind"] == "selector":
                names = ctl.get("short") or [str(x)[:4] for x in ctl["choices"]]
                rings.append((p, names))
            else:
                c.arc_ticks(p["x"], p["y"], r + 0.0010, r + 0.0018, 2, text, 0.00032)
        elif ctl["kind"] == "toggle":
            ch = ctl["choices"] or ["S", "C"]
            T(p["x"] - toggle_dx(str(ch[0])[:5], 0.0019), p["y"] + 0.0010, str(ch[0])[:5], 0.0019, MT.F_BOLD, text)
            T(p["x"] + toggle_dx(str(ch[1])[:5], 0.0019), p["y"] + 0.0010, str(ch[1])[:5], 0.0019, MT.F_BOLD, minor)
            T(p["x"], p["y"] - 0.0040, ctl["label"], 0.0014, MT.F_BOLD, text)
        elif ctl["kind"] == "button":
            c.circle(p["x"], p["y"], p["r"] + 0.0009, fill=(40, 40, 42), outline=(120, 120, 124), width_m=0.0003)
            T(p["x"], p["y"] - p["r"] - 0.0028, ctl["label"], fit_size(ctl["label"], 0.0017, 0.011, MT.F_BOLD), MT.F_BOLD, text)
    # ---- display
    if lay.get("display"):
        x0, y0, x1, y1 = lay["display"]
        c.rect(x0 - 0.0008, y0 - 0.0008, x1 + 0.0008, y1 + 0.0008, fill=(60, 62, 66), radius_m=0.0012)
        c.rect(x0, y0, x1, y1, fill=(6, 7, 8), radius_m=0.0009)
    # ---- LED
    if lay["led"] and not lay.get("led_on_tread"):
        lx, ly, _, lr = lay["led"]
        if lay.get("led_label_below"):
            T(lx, ly - lr * 1.45 - 0.0017, "CHECK", 0.0016, MT.F_BOLD, text)
        elif lx < 0:
            T(lx + lr + 0.0025, ly, "CHECK", 0.0019, MT.F_BOLD, text, anchor="lm")
        else:
            T(lx - lr - 0.0025, ly, "CHECK", 0.0019, MT.F_BOLD, text, anchor="rm")
    # ---- nomi delle posizioni dei selettori
    for p, names in rings:
        ring_names(c, pl, p["x"], p["y"], p["r"], p["h"], names, 0.0012, text, r_gap=0.0030)
    c.save("%s_panel.png" % m["id"])


# posizioni (dalla cima del pedale) del blocco nome / riga / sottotitolo e sigla.
# Sui pedali corti dei pannelli a due file la gomma parte circa 24 mm sotto la cima (render_pedals.build_boss)
TREAD_TEXT_STD = dict(name=0.0160, name_size=0.0086, line=0.0228, sub=0.0272, code_size=0.0052)
TREAD_TEXT_SHORT = dict(name=0.0112, name_size=0.0066, line=0.0158, sub=0.0192, code_size=0.0042)


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
    # pedale corto (pannello profondo a due file): la gomma lascia meno spazio, blocco del nome compattato
    k = TREAD_TEXT_SHORT if ty1 < PL.TREAD_Y1 - 1e-6 else TREAD_TEXT_STD
    name_size = fit_size(m["name"], k["name_size"], 0.060, MT.F_BLACKIT)
    c.text(0.0, top - k["name"], m["name"], name_size, MT.F_BLACKIT, accent)
    c.line(-0.0300, top - k["line"], 0.0300, top - k["line"], text, 0.00030)
    sub = (m.get("subtitle") or SUBTITLES.get(m["category"], "EFFECT")).upper().strip()
    c.text(-0.0300, top - k["sub"], sub, fit_size(sub, 0.0024, 0.034, MT.F_BOLD), MT.F_BOLD, text, anchor="lm", spacing=0.0002)
    c.text(0.0300, top - k["sub"] - 0.0002, m["code"], fit_size(m["code"], k["code_size"], 0.026, MT.F_BLACK), MT.F_BLACK, text, anchor="rm")
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
        if m["style"] in ("treadle", "box"):
            import shaped_textures         # bilancieri e scatole con la forma del pedale vero
            shaped_textures.normal(m, lay, OUT)
        elif m["style"] == "ts":
            ts_texture(m, lay)
        elif m["style"] == "nam":
            nam_texture(m, lay)
        else:
            panel_texture(m, lay)
            tread_texture(m, lay)


if __name__ == "__main__":
    main(set(sys.argv[1:]))
