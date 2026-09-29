#!/usr/bin/env python3
"""
Serigrafie delle repliche REAL MOD (build/cat_real/):
  <id>_panel.png, <id>_tread.png   compatti (come i pedali originali, con sigla e nome reali)
  <id>_top.png                     contenitori a scatola (Twin, serie 200/500, pavimento, da tavolo)
  <id>_back.png, <id>_plate.png    pedali a bilanciere (blocco comandi e targhetta)
Nessun logo del produttore: solo nome e sigla del modello, come riferimento.

Uso: python3 tools/art/make_real_textures.py [id ...]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_textures as MT  # noqa: E402
import make_catalog_textures as MC  # noqa: E402
import pedal_layout as PL  # noqa: E402
import real_catalog  # noqa: E402
import real_layout  # noqa: E402

OUT = os.path.join(HERE, "build", "cat_real")
os.makedirs(OUT, exist_ok=True)


def colours(r):
    return MC.colours(r)


def compact_tread(r, lay):
    """Pedale del compatto: nome grande e sigla, come sui pedali reali (senza logo)."""
    body, text, accent, minor = colours(r)
    ty1 = lay["tread_y1"]
    length = math.hypot(ty1 - PL.TREAD_Y0, PL.TREAD_H_BACK - PL.TREAD_H_FRONT)
    c = MT.Canvas(-PL.W / 2, PL.W / 2, -length / 2, length / 2, body)
    c.speckle(3.5, 0.6)
    top = length / 2
    if lay.get("led_on_tread"):
        lx, ly = lay["led"][0], lay["led"][1]
        t = (ly - PL.TREAD_Y0) / (ty1 - PL.TREAD_Y0)
        c.text(lx - 0.0048, -length / 2 + t * length, "CHECK", 0.0021, MT.F_BOLD, text, anchor="rm")
    name = r["real_name"] or r["real_code"]
    size = MC.fit_size(name, 0.0078, 0.058, MT.F_BLACKIT)
    c.text(-0.0290, top - 0.0165, name, size, MT.F_BLACKIT, accent, anchor="lm")
    c.line(-0.0300, top - 0.0225, 0.0300, top - 0.0225, text, 0.00030)
    code = r["real_code"]
    c.text(0.0300, top - 0.0272, code, MC.fit_size(code, 0.0056, 0.030, MT.F_BLACK), MT.F_BLACK, text, anchor="rm")
    if r.get("stereo"):
        c.text(-0.0300, top - 0.0272, "STEREO", 0.0020, MT.F_BOLD, text, anchor="lm", spacing=0.0002)
    MT.OUT = OUT
    c.save("%s_tread.png" % r["id"])


def compact(r, lay):
    MT.OUT = OUT
    # comandi nell'ordine reale (l'anello interno di un concentrico segue il suo esterno)
    order = sorted(range(len(r["controls"])), key=lambda i: (lay["controls"][i]["x"], lay["controls"][i]["strip"] == "boss_inner"))
    ctrls = []
    for i in order:
        c = dict(r["controls"][i]); c["kind"] = lay["kinds"][i]; ctrls.append(c)
    MC.panel_texture(dict(r, controls=ctrls), dict(lay, controls=[lay["controls"][i] for i in order]))
    compact_tread(r, lay)


def knob_label(c, x, y, rad, label, text, size=0.0022, below=True):
    ty = y - rad - 0.0050 if below else y + rad + 0.0045
    c.text(x, ty, label, MC.fit_size(label, size, 0.022, MT.F_BOLD), MT.F_BOLD, text)
    c.arc_ticks(x, y, rad + 0.0010, rad + 0.0018, 11, text, 0.00026)


def box_top(r, lay):
    body, text, accent, minor = colours(r)
    b = lay["body"]
    W, D = b["W"], b["D"]
    c = MT.Canvas(-W / 2, W / 2, -D / 2, D / 2, body)
    c.speckle(2.5, 0.6)
    ctrls = r["controls"]
    for k, ctl in enumerate(ctrls):
        p = lay["controls"][k]
        if p["strip"] == "boss_outer":
            inner = [j for j, q in enumerate(lay["controls"]) if q["strip"] == "boss_inner" and abs(q["x"] - p["x"]) < 1e-6
                     and abs(q["y"] - p["y"]) < 1e-6]
            c.text(p["x"], p["y"] - p["r"] - 0.0044, ctl["label"], MC.fit_size(ctl["label"], 0.0020, 0.022, MT.F_BOLD), MT.F_BOLD, text)
            if inner:
                lab = ctrls[inner[0]]["label"]
                c.text(p["x"], p["y"] - p["r"] - 0.0070, lab, MC.fit_size(lab, 0.0017, 0.022, MT.F_BOLD), MT.F_BOLD, minor)
            c.arc_ticks(p["x"], p["y"], p["r"] + 0.0010, p["r"] + 0.0018, 11, text, 0.00026)
        elif p["strip"] == "boss":
            knob_label(c, p["x"], p["y"], p["r"], ctl["label"], text)
            if ctl["kind"] == "selector":
                names = ctl.get("short") or [str(x)[:4] for x in ctl["choices"]]
                n = len(names)
                for j, nm in enumerate(names):
                    a = math.radians(-150 + 300 * (0.08 + 0.84 * j / max(1, n - 1)))
                    rr = p["r"] + 0.0032
                    c.text(p["x"] + math.sin(a) * rr, p["y"] + math.cos(a) * rr, nm, 0.0011, MT.F_BOLD, text)
        elif p["strip"] == "slider":
            c.rect(p["x"] - 0.00075, p["y"] - 0.0015, p["x"] + 0.00075, p["y1"] + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            c.text(p["x"], p["y1"] + 0.0055, ctl["label"], MC.fit_size(ctl["label"], 0.0017, 0.0095, MT.F_BOLD), MT.F_BOLD, text)
        elif p["strip"] == "toggle":
            ch = ctl.get("choices") or ["S", "C"]
            c.text(p["x"] - 0.0058, p["y"] + 0.0010, str(ch[0])[:5], 0.0017, MT.F_BOLD, text)
            c.text(p["x"] + 0.0058, p["y"] + 0.0010, str(ch[1])[:5], 0.0017, MT.F_BOLD, minor)
            c.text(p["x"], p["y"] - 0.0042, ctl["label"], 0.0014, MT.F_BOLD, text)
        elif p["strip"] == "button":
            c.text(p["x"], p["y"] - p["r"] - 0.0030, ctl["label"], MC.fit_size(ctl["label"], 0.0016, 0.014, MT.F_BOLD), MT.F_BOLD, text)
    for part in lay["parts"]:
        t = part["type"]
        if t == "display":
            c.rect(part["x0"] - 0.0012, part["y0"] - 0.0012, part["x1"] + 0.0012, part["y1"] + 0.0012, fill=(40, 42, 46), radius_m=0.0014)
            c.rect(part["x0"], part["y0"], part["x1"], part["y1"], fill=(8, 10, 12), radius_m=0.0010)
        elif t == "button" and part.get("label"):
            c.text(part["x"], part["y"] - part["r"] - 0.0030, part["label"], MC.fit_size(part["label"], 0.0015, 0.016, MT.F_BOLD),
                   MT.F_BOLD, text)
        elif t == "footswitch" and part.get("label"):
            c.text(part["x"], part["y"] + part["d"] / 2 + 0.0036, part["label"].upper(),
                   MC.fit_size(part["label"].upper(), 0.0019, part["w"] * 1.1, MT.F_BOLD), MT.F_BOLD, text)
        elif t == "led":
            c.circle(part["x"], part["y"], 0.0030, outline=text, width_m=0.0002)
        elif t == "knob":
            knob_label(c, part["x"], part["y"], part["r"], part.get("label") or "", text)
        elif t == "switch":
            c.rect(part["x"] - 0.0045, part["y"] - 0.0016, part["x"] + 0.0045, part["y"] + 0.0016, fill=(12, 12, 14), radius_m=0.0008)
            if part.get("label"):
                c.text(part["x"], part["y"] - 0.0045, part["label"], MC.fit_size(part["label"], 0.0014, 0.018, MT.F_BOLD), MT.F_BOLD, text)
        elif t == "slider":
            c.rect(part["x"] - 0.00075, part["y0"] - 0.0015, part["x"] + 0.00075, part["y1"] + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            for q in range(5):
                yy = part["y0"] + (part["y1"] - part["y0"]) * q / 4
                c.line(part["x"] - 0.0024, yy, part["x"] - 0.0012, yy, text, 0.00022)
    # nome e sigla del modello: fascia sopra i footswitch (o in basso sulle unita' da tavolo)
    feet = [p for p in lay["parts"] if p["type"] == "footswitch"]
    yb = (max(p["y"] + p["d"] / 2 for p in feet) + 0.0090) if feet else (-D / 2 + 0.010)
    name = r["real_name"]
    code = r["real_code"]
    from PIL import ImageFont
    csize = MC.fit_size(code, 0.0062, W * 0.35, MT.F_BLACK)
    code_w = ImageFont.truetype(MT.F_BLACK, max(6, int(csize * MT.PPM))).getlength(code) / MT.PPM
    room = W - 0.016 - code_w - 0.005
    if room < W * 0.40:
        # contenitore stretto: sigla sopra, nome sotto, entrambi a tutta larghezza
        c.text(W / 2 - 0.008, yb + csize * 1.2, code, csize, MT.F_BLACK, text, anchor="rm")
        c.text(-W / 2 + 0.008, yb, name, MC.fit_size(name, 0.0056, W - 0.016, MT.F_BLACKIT), MT.F_BLACKIT, accent, anchor="lm")
    else:
        c.text(-W / 2 + 0.008, yb, name, MC.fit_size(name, 0.0060, room, MT.F_BLACKIT), MT.F_BLACKIT, accent, anchor="lm")
        c.text(W / 2 - 0.008, yb, code, csize, MT.F_BLACK, text, anchor="rm")
    lx, ly = lay["led"][0], lay["led"][1]
    c.text(lx - 0.0045, ly, "CHECK", 0.0015, MT.F_BOLD, text, anchor="rm")
    MT.OUT = OUT
    c.save("%s_top.png" % r["id"])


def treadle_textures(r, lay):
    body, text, accent, minor = colours(r)
    b = lay["body"]
    W = b["W"]
    blk = lay["back_block"]
    c = MT.Canvas(-W / 2, W / 2, blk["y0"], blk["y1"], body)
    c.speckle(2.5, 0.6)
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        if p["strip"] == "boss":
            c.text(p["x"] + p["r"] + 0.0018, p["y"], ctl["label"], MC.fit_size(ctl["label"], 0.0019, 0.020, MT.F_BOLD), MT.F_BOLD, text,
                   anchor="lm")
    MT.OUT = OUT
    c.save("%s_back.png" % r["id"])
    # targhetta sulla punta del bilanciere: sigla e nome
    pw, pd = W - 0.020, 0.028
    c = MT.Canvas(-pw / 2, pw / 2, -pd / 2, pd / 2, (18, 18, 20))
    c.speckle(2.0, 0.5)
    c.text(0.0, 0.0045, r["real_code"], MC.fit_size(r["real_code"], 0.0070, pw * 0.8, MT.F_BLACK), MT.F_BLACK, (225, 225, 220))
    c.text(0.0, -0.0060, r["real_name"].upper(), MC.fit_size(r["real_name"].upper(), 0.0030, pw * 0.85, MT.F_BOLD), MT.F_BOLD,
           (200, 200, 196), spacing=0.0002)
    c.save("%s_plate.png" % r["id"])


def main(only):
    rs = real_catalog.real_models()
    n = 0
    for r in rs:
        if only and r["id"] not in only:
            continue
        lay = real_layout.real_layout(r)
        if lay["builder"] == "compact":
            compact(r, lay)
        elif lay["builder"] == "treadle":
            treadle_textures(r, lay)
        else:
            box_top(r, lay)
        n += 1
    print("serigrafie REAL MOD: %d" % n)


if __name__ == "__main__":
    main(set(sys.argv[1:]))
