#!/usr/bin/env python3
"""
Serigrafie dei pedali sagomati (builder "wah" di shaped_layout.py), per la modalita' normale (build/cat/)
e per le repliche REAL MOD (build/cat_real/):
  <id>_base.png    piano della vasca (colore, motivo semplificato, scritte dei comandi sul tallone)
  <id>_rocker.png  bilanciere (colore e motivo)
  <id>_tread.png   gomma del bilanciere (colore e motivo semplificato: righe, pois, strisce, mimetico...)
  <id>_plate.png   targhetta in punta: nome e sigla (normale: Pedal Trinity + FabioNET; REAL: sigla e nome reali)
  <id>_pod<k>.png  alette laterali con le scritte dei comandi
  <id>_panel.png   pannello della carcassa larga
Nessun logo del produttore e nessun artwork copiato: solo colori e motivi geometrici semplici.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_textures as MT  # noqa: E402
import make_catalog_textures as MC  # noqa: E402
import shaped_layout as SL  # noqa: E402

PLATE_BG = (18, 18, 20)
PLATE_TEXT = (226, 226, 220)
PLATE_MINOR = (196, 196, 190)


def colours(r):
    import make_real_textures as MR
    return MR.colours(r)


def motif(c, kind, col2, x0, x1, y0, y1, seed=1, scale=1.0):
    """Motivi geometrici semplificati (nessun artwork reale)."""
    rnd = random.Random(seed)
    if kind == "dots":
        step, rad = 0.0125 * scale, 0.0032 * scale
        j = 0
        y = y0 + step / 2
        while y < y1 + step:
            x = x0 + (step / 2 if j % 2 else 0.0)
            while x < x1 + step:
                c.circle(x, y, rad, fill=col2)
                x += step
            y += step * 0.87
            j += 1
    elif kind == "stripes":
        # bande diagonali di larghezza variabile
        span = (x1 - x0) + (y1 - y0)
        t = -span
        while t < span:
            w = rnd.uniform(0.003, 0.009) * scale
            pts = [(x0 + t, y0), (x0 + t + w, y0), (x0 + t + w + (y1 - y0) * 0.55, y1), (x0 + t + (y1 - y0) * 0.55, y1)]
            c.d.polygon([c.px(*p) for p in pts], fill=col2)
            t += rnd.uniform(0.010, 0.022) * scale
    elif kind == "camo":
        greys = [col2, tuple(int(v * 0.7) for v in col2), tuple(min(255, int(v * 1.25)) for v in col2)]
        area = (x1 - x0) * (y1 - y0)
        for _ in range(int(area / 0.00012) + 6):
            cx, cy = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
            rx, ry = rnd.uniform(0.004, 0.011) * scale, rnd.uniform(0.003, 0.008) * scale
            a, b = c.px(cx - rx, cy + ry), c.px(cx + rx, cy - ry)
            c.d.ellipse([a, b], fill=rnd.choice(greys))
    elif kind == "rings":
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        rmax = math.hypot(x1 - x0, y1 - y0) / 2
        k = int(rmax / 0.006) + 1
        for i in range(k, 0, -1):
            rr = i * 0.006 * scale
            c.circle(cx, cy, rr, fill=col2 if i % 2 else c.img.getpixel((2, 2)))
    elif kind == "frame":
        ins = 0.0028
        c.rect(x0 + ins, y0 + ins, x1 - ins, y1 - ins, outline=col2, width_m=0.0011, radius_m=0.003)
        c.rect(x0 + ins + 0.0022, y0 + ins + 0.0022, x1 - ins - 0.0022, y1 - ins - 0.0022, outline=col2, width_m=0.0003,
               radius_m=0.002)
    elif kind == "diamonds":
        step = 0.012 * scale
        j = 0
        y = y0 + step / 2
        while y < y1:
            x = x0 + step / 2 + (step / 2 if j % 2 else 0.0)
            while x < x1:
                s = 0.0022 * scale
                c.d.polygon([c.px(x, y + s * 1.4), c.px(x + s, y), c.px(x, y - s * 1.4), c.px(x - s, y)], fill=col2)
                x += step
            y += step * 0.8
            j += 1
    elif kind == "twin":
        # due strisce di gomma longitudinali su fondo metallico (GCB65)
        w = (x1 - x0)
        c.rect(x0, y0, x1, y1, fill=col2)
        for xs in (x0 + w * 0.06, x0 + w * 0.56):
            c.rect(xs, y0 + 0.002, xs + w * 0.38, y1 - 0.002, fill=(14, 14, 15), radius_m=0.003)


def _save(c, out, name):
    MT.OUT = out
    c.save(name)


def wah_textures(r, lay, out):
    lk = r["look"]
    body, text, accent, minor = colours(r)
    b, rk = lay["base"], lay["rocker"]
    W, D = b["W"], b["D"]
    seed = sum(ord(ch) for ch in r["id"])
    bm = lk.get("body_motif")
    mot_col = (lk.get("tread") or ("ribs", (14, 14, 15), (14, 14, 15)))[2]
    # ---- vasca
    c = MT.Canvas(-W / 2, W / 2, -D / 2, D / 2, body)
    if bm:
        motif(c, bm, mot_col, -W / 2, W / 2, -D / 2, D / 2, seed)
    c.speckle(3.0, 0.6)
    pl = MC.Placer((-W / 2 + 0.002, -D / 2 + 0.001, W / 2 - 0.002, D / 2 - 0.002))
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        if k == 0 or abs(p["y"] - (-D / 2 + (rk["y0"] + D / 2) / 2)) > 0.002:
            continue
        # comando sulla striscia del tallone: scritta accanto (a destra)
        lab = ctl["label"]
        size = MC.fit_size(lab, 0.0019, 0.020, MT.F_BOLD)
        c.text(p["x"] + p.get("r", 0.004) + 0.0020, p["y"], lab, size, MT.F_BOLD, text, anchor="lm")
    if lk.get("display"):
        x0, y0, x1, y1 = lay["display"]
        c.rect(x0 - 0.0012, y0 - 0.0012, x1 + 0.0012, y1 + 0.0012, fill=(40, 42, 46), radius_m=0.0014)
        c.rect(x0, y0, x1, y1, fill=(8, 10, 12), radius_m=0.0010)
    _save(c, out, "%s_base.png" % r["id"])
    # ---- bilanciere
    rc = tuple(lk.get("rocker_colour") or body)
    L, rw = rk["length"], rk["w"]
    c = MT.Canvas(-rw / 2, rw / 2, -L / 2, L / 2, rc)
    if bm:
        motif(c, bm, mot_col, -rw / 2, rw / 2, -L / 2, L / 2, seed + 1)
    c.speckle(3.0, 0.6)
    _save(c, out, "%s_rocker.png" % r["id"])
    # ---- gomma
    tw = rw - 2 * rubber_inset(rk)
    ry0, ry1 = rubber_span(rk)
    kind, col1, col2 = lk.get("tread") or ("ribs", (14, 14, 15), (14, 14, 15))
    c = MT.Canvas(-tw / 2, tw / 2, ry0, ry1, tuple(col1))
    if kind not in ("ribs", "grip", "studs"):
        motif(c, kind, tuple(col2), -tw / 2, tw / 2, ry0, ry1, seed + 2)
    c.speckle(2.0, 0.6)
    _save(c, out, "%s_tread.png" % r["id"])
    # ---- targhetta
    pt = lay["plate"]
    pw, pd = pt["w"], pt["d"]
    c = MT.Canvas(-pw / 2, pw / 2, -pd / 2, pd / 2, tuple(lk.get("plate_colour") or PLATE_BG))
    c.speckle(2.0, 0.5)
    c.rect(-pw / 2 + 0.0012, -pd / 2 + 0.0012, pw / 2 - 0.0012, pd / 2 - 0.0012, outline=(70, 70, 72), width_m=0.0003,
           radius_m=0.0015)
    if r.get("normal"):
        name = r["real_name"]
        c.text(0.0, pd * 0.16, name, MC.fit_size(name, min(0.0060, pd * 0.30), pw * 0.86, MT.F_BLACKIT), MT.F_BLACKIT,
               PLATE_TEXT)
        sy = -pd * 0.28
        c.text(-pw / 2 + 0.0035, sy, "FabioNET", MC.fit_size("FabioNET", min(0.0026, pd * 0.13), pw * 0.36, MT.F_BOLDIT),
               MT.F_BOLDIT, PLATE_MINOR, anchor="lm")
        c.text(pw / 2 - 0.0035, sy, r["real_code"], MC.fit_size(r["real_code"], min(0.0036, pd * 0.17), pw * 0.40, MT.F_BLACK),
               MT.F_BLACK, PLATE_MINOR, anchor="rm")
    else:
        code = r["real_code"]
        c.text(0.0, pd * 0.14, code, MC.fit_size(code, min(0.0066, pd * 0.32), pw * 0.80, MT.F_BLACK), MT.F_BLACK, PLATE_TEXT)
        nm = r["real_name"].upper()
        c.text(0.0, -pd * 0.27, nm, MC.fit_size(nm, min(0.0026, pd * 0.13), pw * 0.88, MT.F_BOLD), MT.F_BOLD, PLATE_MINOR,
               spacing=0.0001)
    _save(c, out, "%s_plate.png" % r["id"])
    # ---- alette laterali
    for k, pod in enumerate(lay["pods"]):
        pod_texture(r, lay, pod, k, out, body, text, minor)
    # ---- pannello della carcassa larga
    if lay.get("panel"):
        panel_texture(r, lay, out, body, text, minor)


rubber_inset = SL.rubber_inset
rubber_span = SL.rubber_span


def _label(c, pl, it, lab, text, local, maxw):
    """Scritta di un comando: sotto (verso il tallone) o di lato (verso l'esterno)."""
    x, y = local(it["x"], it["y"])
    r = it["r"]
    size = MC.fit_size(lab, SL.LABEL_SIZE, maxw, MT.F_BOLD)
    if it["label_pos"] == "side":
        outward = 1 if it["x"] > 0 else -1
        tx = x + outward * (r + 0.0024)
        anchor = "lm" if outward > 0 else "rm"
        c.text(tx, y, lab, size, MT.F_BOLD, text, anchor=anchor)
        pl.rect(MC.text_box(tx, y, lab, size, MT.F_BOLD, anchor))
    else:
        ty = y - r - 0.0040
        c.text(x, ty, lab, size, MT.F_BOLD, text)
        pl.rect(MC.text_box(x, ty, lab, size, MT.F_BOLD))


def pod_texture(r, lay, pod, k, out, body, text, minor):
    cx, cy = (pod["x0"] + pod["x1"]) / 2, (pod["y0"] + pod["y1"]) / 2
    w, d = pod["x1"] - pod["x0"], pod["y1"] - pod["y0"]
    c = MT.Canvas(-w / 2, w / 2, -d / 2, d / 2, body)
    c.speckle(3.0, 0.6)

    def local(x, y):
        return x - cx, y - cy

    pl = MC.Placer((-w / 2 + 0.0008, -d / 2 + 0.0008, w / 2 - 0.0008, d / 2 - 0.0008), margin=0.0002)
    for it in pod["items"]:
        x, y = local(it["x"], it["y"])
        if it["kind"] in ("knob", "big"):
            pl.knob(x, y, it["r"], it["h"])
        elif it["kind"] in ("button", "foot"):
            pl.circle(x, y, it["r"] * 1.35 + 0.0005)
        elif it["kind"] == "lever":
            pl.capsule(x, y, y + 0.0060, 0.0032)
        else:
            pl.capsule(x, y, y + 0.0010, 0.0030)
    rings = []
    maxw = w - 0.0020
    for it in pod["items"]:
        x, y = local(it["x"], it["y"])
        if it["kind"] == "led":
            c.circle(x, y, 0.0029, outline=text, width_m=0.0002)
            continue
        ctl = r["controls"][it["i"]]
        lab = ctl["label"]
        if it["kind"] == "lever":
            ch = ctl.get("choices") or ["", ""]
            for j, sgn in ((0, -1), (1, 1)):
                s = str(ch[j])[:5]
                size = MC.fit_size(s, 0.0015, 0.0050, MT.F_BOLD)
                tx = x + sgn * MC.toggle_dx(s, size)
                c.text(tx, y + 0.0010, s, size, MT.F_BOLD, text if j == 0 else minor)
                pl.rect(MC.text_box(tx, y + 0.0010, s, size, MT.F_BOLD))
            ty = y - 0.0042
            size = MC.fit_size(lab, SL.LABEL_SIZE, maxw, MT.F_BOLD)
            c.text(x, ty, lab, size, MT.F_BOLD, text)
            pl.rect(MC.text_box(x, ty, lab, size, MT.F_BOLD))
            continue
        if it["label_pos"] == "side":
            outward = 1 if it["x"] > 0 else -1
            lw = (w / 2 - x * outward) - it["r"] - 0.0024 - 0.0008      # dal bordo del comando al bordo esterno
        else:
            lw = maxw
        _label(c, pl, it, lab, text, local, max(0.004, lw))
        if it["kind"] in ("knob", "big"):
            if ctl["kind"] == "selector":
                rings.append((x, y, it, ctl.get("short") or [str(v)[:4] for v in ctl["choices"]]))
            else:
                c.arc_ticks(x, y, it["r"] + 0.0009, it["r"] + 0.0016, 11, text, 0.00024)
        elif it["kind"] in ("button", "foot"):
            c.circle(x, y, it["r"] * 1.30 + 0.0006, outline=text, width_m=0.00025)
    for x, y, it, names in rings:
        MC.ring_names(c, pl, x, y, it["r"], it["h"], names, 0.0012, text, r_gap=0.0026)
    _save(c, out, "%s_pod%d.png" % (r["id"], k))


def panel_texture(r, lay, out, body, text, minor):
    pn = lay["panel"]
    x0, x1, y0, y1 = pn["x0"], pn["x1"], pn["y0"], pn["y1"]
    c = MT.Canvas(x0, x1, y0, y1, body)
    c.speckle(3.0, 0.6)
    pl = MC.Placer((x0 + 0.002, y0 + 0.002, x1 - 0.002, y1 - 0.002))
    items = []
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        if k == 0 or not (x0 - 1e-6 <= p["x"] <= x1 + 1e-6):
            continue
        items.append((k, ctl, p))
        if p["strip"] in ("boss", "boss_small"):
            pl.knob(p["x"], p["y"], p["r"], p["h"])
        elif p["strip"] == "button":
            pl.circle(p["x"], p["y"], p["r"] * 1.35 + 0.0005)
    lx, ly = lay["led"][0], lay["led"][1]
    if x0 <= lx <= x1:
        pl.capsule(lx, ly, ly + 0.0012, 0.0031)
    for k, ctl, p in items:
        lab = ctl["label"]
        if p["strip"] == "button":
            ty = p["y"] - p["r"] * 1.3 - 0.0040          # sotto l'interruttore (sopra coprirebbe il bordo in punta)
            size = MC.fit_size(lab, 0.0020, 0.030, MT.F_BOLD)
            c.text(p["x"], ty, lab, size, MT.F_BOLD, text)
            pl.rect(MC.text_box(p["x"], ty, lab, size, MT.F_BOLD))
            c.circle(p["x"], p["y"], p["r"] * 1.30 + 0.0008, outline=text, width_m=0.0003)
        else:
            ty = p["y"] - p["r"] - 0.0042
            size = MC.fit_size(lab, 0.0019, 0.024, MT.F_BOLD)
            c.text(p["x"], ty, lab, size, MT.F_BOLD, text)
            pl.rect(MC.text_box(p["x"], ty, lab, size, MT.F_BOLD))
            c.arc_ticks(p["x"], p["y"], p["r"] + 0.0009, p["r"] + 0.0016, 11, text, 0.00024)
    _save(c, out, "%s_panel.png" % r["id"])


def normal(m, lay, out):
    """Serigrafie di un modello del catalogo normale di stile treadle/box (nome e sigla Pedal Trinity)."""
    import make_real_textures as MR
    r = SL.as_shaped(m)
    if lay["builder"] == "wah":
        wah_textures(r, lay, out)
    elif lay["builder"] == "treadle":
        MR.treadle_textures(r, lay, out=out)
    elif lay["builder"] == "stomp":
        import stomp_textures
        stomp_textures.top_texture(r, lay, out)
    else:
        MR.box_top(r, lay, out=out)
