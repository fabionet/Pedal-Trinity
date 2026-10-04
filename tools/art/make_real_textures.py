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
    """Colori delle serigrafie; i colori della scheda con poco contrasto sul corpo (scritte illeggibili,
    es. testo uguale al corpo) passano al testo chiaro/scuro standard."""
    body, text, accent, minor = MC.colours(r)
    if abs(MC.lum(text) - MC.lum(body)) < 60:
        text = (22, 22, 24) if MC.lum(body) > 118 else (238, 236, 222)
    d = MC.lum(accent) - MC.lum(body)
    # nome chiaro su corpo chiaro, scuro su corpo scuro, o quasi uguale al corpo: illeggibile nel render
    if abs(d) < 15 or (0 < d < 70 and MC.lum(body) > 110) or (MC.lum(body) < 60 and abs(d) < 35):
        accent = text
    return body, text, accent, minor


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
    # pedale corto (pannello a due file): la gomma parte piu' in alto, blocco compattato (vedi render_pedals.build_boss)
    short = ty1 < PL.TREAD_Y1 - 1e-6
    yn, sn, yl, yc, sc = (0.0115, 0.0064, 0.0162, 0.0200, 0.0044) if short else (0.0165, 0.0078, 0.0225, 0.0272, 0.0056)
    size = MC.fit_size(name, sn, 0.058, MT.F_BLACKIT)
    c.text(-0.0290, top - yn, name, size, MT.F_BLACKIT, accent, anchor="lm")
    c.line(-0.0300, top - yl, 0.0300, top - yl, text, 0.00030)
    code = r["real_code"]
    c.text(0.0300, top - yc, code, MC.fit_size(code, sc, 0.030, MT.F_BLACK), MT.F_BLACK, text, anchor="rm")
    if r.get("stereo"):
        c.text(-0.0300, top - yc, "STEREO", 0.0020, MT.F_BOLD, text, anchor="lm", spacing=0.0002)
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


def knob_label(c, x, y, rad, label, text, size=0.0022, below=True, maxw=0.022):
    ty = y - rad - 0.0050 if below else y + rad + 0.0045
    if rad < 0.006:
        ty = y - rad - 0.0042
    c.text(x, ty, label, MC.fit_size(label, size, maxw, MT.F_BOLD), MT.F_BOLD, text)
    c.arc_ticks(x, y, rad + 0.0010, rad + 0.0018, 11, text, 0.00026)
    return MC.text_box(x, ty, label, MC.fit_size(label, size, maxw, MT.F_BOLD), MT.F_BOLD)


FOOT_LABEL_DY = 0.0050       # scritta sopra il footswitch: oltre la sua sommita' proiettata
NAME_DY = 0.0110             # nome del modello sopra i footswitch


def box_placer(r, lay, W, D):
    """Ostacoli del piano superiore (vedi make_catalog_textures.Placer)."""
    pl = MC.Placer((-W / 2 + 0.0022, -D / 2 + 0.0022, W / 2 - 0.0022, D / 2 - 0.0022))
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        st = p["strip"]
        if st in ("boss", "boss_small"):
            pl.knob(p["x"], p["y"], p["r"], p["h"])
        elif st == "boss_outer":
            pl.knob(p["x"], p["y"], p["r"], p["h"] + PL.KNOB_H["boss_inner"])
        elif st == "toggle":
            pl.capsule(p["x"], p["y"], p["y"] + 0.0060, 0.0032)
        elif st == "button":
            pl.circle(p["x"], p["y"], p["r"] * 1.25 + 0.0006)
        elif st == "slider":
            pl.rect((p["x"] - 0.0030, p["y"] - 0.0035, p["x"] + 0.0030, p["y1"] + 0.0035))
    for part in lay["parts"]:
        t = part["type"]
        if t == "display":
            pl.rect((part["x0"] - 0.0013, part["y0"] - 0.0013, part["x1"] + 0.0013, part["y1"] + 0.0013))
        elif t == "button":
            pl.capsule(part["x"], part["y"], part["y"] + 0.0012, part["r"] * 1.25 + 0.0006)
        elif t == "led":
            pl.capsule(part["x"], part["y"], part["y"] + 0.0012, 0.0031)
        elif t == "knob":
            pl.knob(part["x"], part["y"], part["r"], part.get("h", 0.014) + 0.001)
        elif t == "switch":
            pl.rect((part["x"] - 0.0047, part["y"] - 0.0018, part["x"] + 0.0047, part["y"] + 0.0030))
        elif t == "slider":
            pl.rect((part["x"] - 0.0032, part["y0"] - 0.0020, part["x"] + 0.0032, part["y1"] + 0.0040))
        elif t == "footswitch":
            shadow = 0.0045 if part.get("kind") == "treadle_pad" else 0.0037
            pl.rect((part["x"] - part["w"] / 2 - 0.0015, part["y"] - part["d"] / 2 - 0.0015,
                     part["x"] + part["w"] / 2 + 0.0015, part["y"] + part["d"] / 2 + shadow))
    lx, ly = lay["led"][0], lay["led"][1]
    pl.capsule(lx, ly, ly + 0.0012, 0.0031)
    return pl


def box_top(r, lay, out=None):
    body, text, accent, minor = colours(r)
    b = lay["body"]
    W, D = b["W"], b["D"]
    c = MT.Canvas(-W / 2, W / 2, -D / 2, D / 2, body)
    c.speckle(2.5, 0.6)
    pl = box_placer(r, lay, W, D)

    def T(x, y, s, size, font, col, anchor="mm", spacing=0.0):
        c.text(x, y, s, size, font, col, anchor=anchor, spacing=spacing)
        pl.rect(MC.text_box(x, y, s, size, font, anchor, spacing))

    ctrls = r["controls"]
    rings = []
    for k, ctl in enumerate(ctrls):
        p = lay["controls"][k]
        maxw = max(0.008, p.get("w", 0.022) - 0.0010)
        if p["strip"] == "boss_outer":
            inner = [j for j, q in enumerate(lay["controls"]) if q["strip"] == "boss_inner" and abs(q["x"] - p["x"]) < 1e-6
                     and abs(q["y"] - p["y"]) < 1e-6]
            T(p["x"], p["y"] - p["r"] - 0.0044, ctl["label"], MC.fit_size(ctl["label"], 0.0020, maxw, MT.F_BOLD), MT.F_BOLD, text)
            if inner:
                lab = ctrls[inner[0]]["label"]
                T(p["x"], p["y"] - p["r"] - 0.0070, lab, MC.fit_size(lab, 0.0017, maxw, MT.F_BOLD), MT.F_BOLD, minor)
            c.arc_ticks(p["x"], p["y"], p["r"] + 0.0010, p["r"] + 0.0018, 11, text, 0.00026)
        elif p["strip"] in ("boss", "boss_small"):
            pl.rect(knob_label(c, p["x"], p["y"], p["r"], ctl["label"], text, maxw=maxw))
            if ctl["kind"] == "selector":
                rings.append((p, ctl.get("short") or [str(x)[:4] for x in ctl["choices"]]))
        elif p["strip"] == "slider":
            c.rect(p["x"] - 0.00075, p["y"] - 0.0015, p["x"] + 0.00075, p["y1"] + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            T(p["x"], p["y1"] + 0.0055, ctl["label"], MC.fit_size(ctl["label"], 0.0017, min(0.0095, maxw + 0.0005), MT.F_BOLD), MT.F_BOLD, text)
        elif p["strip"] == "toggle":
            ch = ctl.get("choices") or ["S", "C"]
            T(p["x"] - MC.toggle_dx(str(ch[0])[:5], MC.fit_size(str(ch[0])[:5], 0.0017, 0.0060, MT.F_BOLD)), p["y"] + 0.0010, str(ch[0])[:5], MC.fit_size(str(ch[0])[:5], 0.0017, 0.0060, MT.F_BOLD), MT.F_BOLD, text)
            T(p["x"] + MC.toggle_dx(str(ch[1])[:5], MC.fit_size(str(ch[1])[:5], 0.0017, 0.0060, MT.F_BOLD)), p["y"] + 0.0010, str(ch[1])[:5], MC.fit_size(str(ch[1])[:5], 0.0017, 0.0060, MT.F_BOLD), MT.F_BOLD, minor)
            T(p["x"], p["y"] - 0.0042, ctl["label"], MC.fit_size(ctl["label"], 0.0014, maxw, MT.F_BOLD), MT.F_BOLD, text)
        elif p["strip"] == "button":
            T(p["x"], p["y"] - p["r"] - 0.0030, ctl["label"], MC.fit_size(ctl["label"], 0.0016, min(0.014, maxw), MT.F_BOLD), MT.F_BOLD, text)
    for part in lay["parts"]:
        t = part["type"]
        pw = max(0.008, part.get("w", 0.016) - 0.0010)
        if t == "display":
            c.rect(part["x0"] - 0.0012, part["y0"] - 0.0012, part["x1"] + 0.0012, part["y1"] + 0.0012, fill=(40, 42, 46), radius_m=0.0014)
            c.rect(part["x0"], part["y0"], part["x1"], part["y1"], fill=(8, 10, 12), radius_m=0.0010)
        elif t == "button" and part.get("label"):
            T(part["x"], part["y"] - part["r"] - 0.0030, part["label"], MC.fit_size(part["label"], 0.0015, pw, MT.F_BOLD), MT.F_BOLD, text)
        elif t == "footswitch" and part.get("label"):
            dy = FOOT_LABEL_DY + (0.0005 if part.get("kind") == "treadle_pad" else 0.0)
            T(part["x"], part["y"] + part["d"] / 2 + dy, part["label"].upper(),
              MC.fit_size(part["label"].upper(), 0.0019, part["w"] * 1.1, MT.F_BOLD), MT.F_BOLD, text)
        elif t == "led":
            c.circle(part["x"], part["y"], 0.0030, outline=text, width_m=0.0002)
        elif t == "knob":
            pl.rect(knob_label(c, part["x"], part["y"], part["r"], part.get("label") or "", text, maxw=pw))
        elif t == "switch":
            c.rect(part["x"] - 0.0045, part["y"] - 0.0016, part["x"] + 0.0045, part["y"] + 0.0016, fill=(12, 12, 14), radius_m=0.0008)
            if part.get("label"):
                T(part["x"], part["y"] - 0.0045, part["label"], MC.fit_size(part["label"], 0.0014, pw, MT.F_BOLD), MT.F_BOLD, text)
        elif t == "slider":
            c.rect(part["x"] - 0.00075, part["y0"] - 0.0015, part["x"] + 0.00075, part["y1"] + 0.0015, fill=(2, 2, 2), radius_m=0.0005)
            for q in range(5):
                yy = part["y0"] + (part["y1"] - part["y0"]) * q / 4
                c.line(part["x"] - 0.0024, yy, part["x"] - 0.0012, yy, text, 0.00022)
    # nome e sigla del modello: fascia sopra i footswitch (o accanto al pulsante EFFECT sulle unita' da tavolo)
    feet = [p for p in lay["parts"] if p["type"] == "footswitch"]
    if feet:
        yb = max(p["y"] + p["d"] / 2 for p in feet) + NAME_DY
    else:
        yb = lay.get("name_y", -D / 2 + 0.010)
    x1 = lay.get("name_x1", W / 2 - 0.008)
    name = r["real_name"]
    code = r["real_code"]
    from PIL import ImageFont
    span = x1 - (-W / 2 + 0.008)
    csize = MC.fit_size(code, 0.0062, span * 0.40, MT.F_BLACK)
    code_w = ImageFont.truetype(MT.F_BLACK, max(6, int(csize * MT.PPM))).getlength(code) / MT.PPM
    room = span - code_w - 0.005
    if room < span * 0.45:
        # contenitore stretto: sigla sopra, nome sotto, entrambi a tutta larghezza
        T(x1, yb + csize * 1.2, code, csize, MT.F_BLACK, text, anchor="rm")
        T(-W / 2 + 0.008, yb, name, MC.fit_size(name, 0.0056, span, MT.F_BLACKIT), MT.F_BLACKIT, accent, anchor="lm")
    else:
        T(-W / 2 + 0.008, yb, name, MC.fit_size(name, 0.0060, room, MT.F_BLACKIT), MT.F_BLACKIT, accent, anchor="lm")
        T(x1, yb, code, csize, MT.F_BLACK, text, anchor="rm")
    # nomi delle posizioni dei selettori, poi la scritta CHECK del LED dove resta posto
    for p, names in rings:
        MC.ring_names(c, pl, p["x"], p["y"], p["r"], p["h"], names, 0.0011, text, r_gap=0.0030)
    lx, ly = lay["led"][0], lay["led"][1]
    for x, y, anchor in ((lx - 0.0045, ly, "rm"), (lx + 0.0045, ly, "lm"), (lx, ly - 0.0050, "mm"), (lx, ly + 0.0050, "mm")):
        box = MC.text_box(x, y, "CHECK", 0.0015, MT.F_BOLD, anchor)
        if pl.free(box):
            T(x, y, "CHECK", 0.0015, MT.F_BOLD, text, anchor=anchor)
            break
    MT.OUT = out or OUT
    c.save("%s_top.png" % r["id"])


def treadle_textures(r, lay, out=None):
    body, text, accent, minor = colours(r)
    b = lay["body"]
    W = b["W"]
    blk = lay["back_block"]
    c = MT.Canvas(-W / 2, W / 2, blk["y0"], blk["y1"], body)
    c.speckle(2.5, 0.6)
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        maxw = min(0.020, p.get("w", 0.020) - 0.002)
        if p["strip"] == "boss":
            # sotto il pomello (a destra finiva sul pomello vicino o oltre il bordo del blocco)
            c.text(p["x"], p["y"] - p["r"] - 0.0045, ctl["label"], MC.fit_size(ctl["label"], 0.0019, maxw, MT.F_BOLD), MT.F_BOLD, text)
            c.arc_ticks(p["x"], p["y"], p["r"] + 0.0010, p["r"] + 0.0018, 11, text, 0.00026)
        elif p["strip"] == "toggle":
            ch = ctl.get("choices") or ["S", "C"]
            c.text(p["x"] - MC.toggle_dx(str(ch[0])[:5], MC.fit_size(str(ch[0])[:5], 0.0016, 0.0060, MT.F_BOLD)), p["y"] + 0.0010, str(ch[0])[:5], MC.fit_size(str(ch[0])[:5], 0.0016, 0.0060, MT.F_BOLD), MT.F_BOLD, text)
            c.text(p["x"] + MC.toggle_dx(str(ch[1])[:5], MC.fit_size(str(ch[1])[:5], 0.0016, 0.0060, MT.F_BOLD)), p["y"] + 0.0010, str(ch[1])[:5], MC.fit_size(str(ch[1])[:5], 0.0016, 0.0060, MT.F_BOLD), MT.F_BOLD, minor)
            c.text(p["x"], p["y"] - 0.0045, ctl["label"], MC.fit_size(ctl["label"], 0.0016, maxw, MT.F_BOLD), MT.F_BOLD, text)
    MT.OUT = out or OUT
    c.save("%s_back.png" % r["id"])
    # targhetta sulla punta del bilanciere: sigla e nome
    pw, pd = W - 0.020, 0.028
    c = MT.Canvas(-pw / 2, pw / 2, -pd / 2, pd / 2, (18, 18, 20))
    c.speckle(2.0, 0.5)
    if r.get("normal"):
        # modalita' normale: nome Pedal Trinity in evidenza, sigla e marchio FabioNET
        c.text(0.0, 0.0040, r["real_name"], MC.fit_size(r["real_name"], 0.0062, pw * 0.86, MT.F_BLACKIT), MT.F_BLACKIT, (225, 225, 220))
        c.text(-pw / 2 + 0.004, -0.0068, "FabioNET", 0.0026, MT.F_BOLDIT, (200, 200, 196), anchor="lm")
        c.text(pw / 2 - 0.004, -0.0068, r["real_code"], MC.fit_size(r["real_code"], 0.0040, pw * 0.4, MT.F_BLACK), MT.F_BLACK,
               (200, 200, 196), anchor="rm")
    else:
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
        elif lay["builder"] == "wah":
            import shaped_textures
            shaped_textures.wah_textures(r, lay, OUT)
        elif lay["builder"] == "stomp":
            import stomp_textures
            stomp_textures.top_texture(r, lay, OUT)
        else:
            box_top(r, lay)
        n += 1
    print("serigrafie REAL MOD: %d" % n)


if __name__ == "__main__":
    main(set(sys.argv[1:]))
