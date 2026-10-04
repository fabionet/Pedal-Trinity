#!/usr/bin/env python3
"""
Serigrafia del piano dei pedali a scatola (builder "stomp" di stomp_layout.py), per la modalita' normale
(build/cat/, nome e sigla Pedal Trinity + "FabioNET") e per le repliche REAL MOD (build/cat_real/, nome e
sigla reali del modello, senza il marchio del produttore):
  <id>_top.png   piano superiore (proiettato dall'alto su tutta la scocca: i bordi restano del colore di fondo)

Grafica descritta in look["gfx"] con primitive geometriche semplici (fasce, riquadri, diagonali, cerchi,
raggiere, righe regolari): colori e impostazione del pedale vero, MAI loghi, artwork, ritratti o firme.
  bg           colore di fondo del piano (default: colore del pedale)
  panels       [("band", y0%, y1%, colore), ("rect", x0%, y0%, x1%, y1%, colore[, bordo, raggio mm, spessore mm]),
                ("diag", colore, "tr"|"tl"|"br"|"bl"), ("poly", [(x%, y%), ...], colore), ("circle", x%, y%, r mm, colore[, bordo, spessore]),
                ("ellipse", x%, y%, rx mm, ry mm, colore, spessore mm), ("rays", x%, y%, colore, n),
                ("stripes", colore, larghezza mm, passo mm, angolo gradi[, (x0%, y0%, x1%, y1%)]), ("hline", y%, colore, spessore mm[, x0%, x1%]),
                ("text", "testo", x%, y%, mm, colore, font)]
  name / code / sub   dict(y=%, x=%, size=mm, font=..., col=..., upper=bool, w=% della larghezza, text=... (solo sub))
               senza y: la scritta va nella fascia libera piu' ampia sopra il footswitch
  label        dict(col=..., font=..., size=mm, pos="below"|"above")   scritte dei comandi
  ticks        True: scala attorno ai pomelli
  brand        dict(x=%, y=%) dove va "FabioNET" in modalita' normale (al posto del marchio del pedale vero)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_textures as MT  # noqa: E402
import make_catalog_textures as MC  # noqa: E402
import pedal_layout as PL  # noqa: E402

FD = "/usr/share/fonts/opentype/urw-base35/"
FONTS = {"bold": MT.F_BOLD, "boldit": MT.F_BOLDIT, "narrow": MT.F_NARROW, "black": MT.F_BLACK, "blackit": MT.F_BLACKIT,
         "gothic": FD + "URWGothic-Demi.otf", "gothicit": FD + "URWGothic-DemiOblique.otf", "bookman": FD + "URWBookman-Demi.otf",
         "bookmanit": FD + "URWBookman-DemiItalic.otf", "serif": FD + "P052-Bold.otf", "serifit": FD + "P052-BoldItalic.otf",
         "script": FD + "Z003-MediumItalic.otf", "roman": FD + "C059-Bold.otf", "mono": FD + "NimbusMonoPS-Bold.otf"}
DARK, LIGHT = (20, 20, 22), (240, 238, 228)


def tbox(x, y, s, size, fnt, anchor="mm", spacing=0.0):
    """Riquadro ESATTO (metri) dei glifi disegnati da MT.Canvas.text (stesso ancoraggio di PIL)."""
    from PIL import ImageFont
    f = ImageFont.truetype(fnt, max(6, int(size * MT.PPM)))
    bb = f.getbbox(s or " ", anchor=anchor)
    if spacing:
        a = MC.text_box(x, y, s, size, fnt, anchor, spacing)
        return (a[0], y - bb[3] / MT.PPM, a[2], y - bb[1] / MT.PPM)
    return (x + bb[0] / MT.PPM, y - bb[3] / MT.PPM, x + bb[2] / MT.PPM, y - bb[1] / MT.PPM)


def font(name, dflt="bold"):
    return FONTS.get(name or dflt, FONTS[dflt])


def lum(c):
    return MC.lum(c)


def contrast(bgc):
    return DARK if lum(bgc) > 128 else LIGHT


class Ctx:
    def __init__(self, r, lay):
        self.r, self.lay = r, lay
        b = lay["body"]
        self.W, self.D = b["W"], b["D"]
        self.gfx = r["look"].get("gfx") or {}

    def X(self, pct):
        return (float(pct) / 100.0 - 0.5) * self.W

    def Y(self, pct):
        return (0.5 - float(pct) / 100.0) * self.D


def draw_panels(c, cx, panels):
    W, D = cx.W, cx.D
    for p in panels:
        k = p[0]
        if k == "band":
            c.rect(-W / 2 - 0.001, cx.Y(p[2]), W / 2 + 0.001, cx.Y(p[1]), fill=tuple(p[3]))
        elif k == "rect":
            x0, y0, x1, y1 = cx.X(p[1]), cx.Y(p[4]), cx.X(p[3]), cx.Y(p[2])
            fill = tuple(p[5]) if p[5] else None
            outline = tuple(p[6]) if len(p) > 6 and p[6] else None
            rad = (p[7] if len(p) > 7 else 0.0) / 1000.0
            wid = (p[8] if len(p) > 8 else 0.6) / 1000.0
            c.rect(x0, y0, x1, y1, fill=fill, outline=outline, width_m=wid, radius_m=rad)
        elif k == "diag":
            col, corner = tuple(p[1]), p[2]
            pts = {"tr": [(0, 0), (100, 0), (100, 100)], "tl": [(0, 0), (100, 0), (0, 100)],
                   "br": [(100, 0), (100, 100), (0, 100)], "bl": [(0, 0), (100, 100), (0, 100)]}[corner]
            ext = [(cx.X(-1 if x == 0 else 101), cx.Y(-1 if y == 0 else 101)) for x, y in pts]
            c.d.polygon([c.px(*q) for q in ext], fill=col)
        elif k == "poly":
            c.d.polygon([c.px(cx.X(x), cx.Y(y)) for x, y in p[1]], fill=tuple(p[2]))
        elif k == "circle":
            outline = tuple(p[5]) if len(p) > 5 and p[5] else None
            c.circle(cx.X(p[1]), cx.Y(p[2]), p[3] / 1000.0, fill=tuple(p[4]) if p[4] else None, outline=outline,
                     width_m=(p[6] if len(p) > 6 else 0.6) / 1000.0)
        elif k == "ellipse":
            x, y, rx, ry = cx.X(p[1]), cx.Y(p[2]), p[3] / 1000.0, p[4] / 1000.0
            a, b = c.px(x - rx, y + ry), c.px(x + rx, y - ry)
            c.d.ellipse([a, b], outline=tuple(p[5]), width=max(1, int(p[6] / 1000.0 * MT.PPM)))
        elif k == "rays":
            x, y, col, n = cx.X(p[1]), cx.Y(p[2]), tuple(p[3]), int(p[4])
            R = math.hypot(W, D)
            for i in range(n):
                a0 = 2 * math.pi * i / n
                a1 = a0 + math.pi / n
                c.d.polygon([c.px(x, y), c.px(x + R * math.cos(a0), y + R * math.sin(a0)),
                             c.px(x + R * math.cos(a1), y + R * math.sin(a1))], fill=col)
        elif k == "stripes":
            col, wid, step, ang = tuple(p[1]), p[2] / 1000.0, p[3] / 1000.0, math.radians(p[4])
            if len(p) > 5:
                rx0, ry0, rx1, ry1 = cx.X(p[5][0]), cx.Y(p[5][3]), cx.X(p[5][2]), cx.Y(p[5][1])
            else:
                rx0, ry0, rx1, ry1 = -W / 2 - 0.002, -D / 2 - 0.002, W / 2 + 0.002, D / 2 + 0.002
            from PIL import Image, ImageDraw
            mask = Image.new("L", c.img.size, 0)
            md = ImageDraw.Draw(mask)
            md.rectangle([c.px(rx0, ry1), c.px(rx1, ry0)], fill=255)
            layer = Image.new("L", c.img.size, 0)
            ld = ImageDraw.Draw(layer)
            R = math.hypot(W, D)
            ux, uy = math.cos(ang), math.sin(ang)          # direzione delle strisce
            nx, ny = -uy, ux
            t = -R
            while t < R:
                pts = [(nx * t + ux * -R, ny * t + uy * -R), (nx * t + ux * R, ny * t + uy * R),
                       (nx * (t + wid) + ux * R, ny * (t + wid) + uy * R), (nx * (t + wid) + ux * -R, ny * (t + wid) + uy * -R)]
                ld.polygon([c.px(*q) for q in pts], fill=255)
                t += step
            from PIL import ImageChops
            m2 = ImageChops.multiply(mask, layer)
            solid = Image.new("RGB", c.img.size, col)
            c.img.paste(solid, (0, 0), m2)
            c.d = ImageDraw.Draw(c.img)
        elif k == "hline":
            x0 = cx.X(p[4]) if len(p) > 4 else -W / 2 - 0.001
            x1 = cx.X(p[5]) if len(p) > 5 else W / 2 + 0.001
            c.line(x0, cx.Y(p[1]), x1, cx.Y(p[1]), tuple(p[2]), p[3] / 1000.0)
        elif k == "text":
            pass                            # scritte decorative: dopo gli ostacoli (vedi deco_texts)


def bg_at(c, x, y):
    px, py = c.px(x, y)
    px = min(max(int(px), 0), c.img.width - 1)
    py = min(max(int(py), 0), c.img.height - 1)
    return c.img.getpixel((px, py))


def bg_ring(c, x, y, rad):
    """Colore di fondo tipico attorno a un comando (mediana della luminanza su un anello): non risente delle
    scritte gia' stampate."""
    pts = []
    for k in range(16):
        a = 2 * math.pi * k / 16
        pts.append(bg_at(c, x + math.cos(a) * rad, y + math.sin(a) * rad))
    pts.sort(key=lum)
    return pts[len(pts) // 2]


def placer(cx, lay):
    W, D = cx.W, cx.D
    edge = lay["base"]["edge"]
    m = max(0.0022, edge * 0.75)
    pl = MC.Placer((-W / 2 + m, -D / 2 + m, W / 2 - m, D / 2 - m))
    r = cx.r
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        st = p["strip"]
        if st in ("boss", "boss_small", "ts_big", "ts_small"):
            pl.knob(p["x"], p["y"], p["r"], p["h"])
        elif st == "boss_outer":
            pl.knob(p["x"], p["y"], p["r"], p["h"] + PL.KNOB_H["boss_inner"])
        elif st == "toggle":
            pl.capsule(p["x"], p["y"], p["y"] + 0.0060, 0.0034)
        elif st == "button":
            pl.circle(p["x"], p["y"], p["r"] * 1.30 + 0.0006)
        elif st == "slider":
            pl.rect((p["x"] - 0.0034, p["y"] - 0.0035, p["x"] + 0.0034, p["y1"] + 0.0050))
    for part in lay["parts"]:
        t = part["type"]
        if t == "footswitch":
            pl.capsule(part["x"], part["y"], part["y"] + 0.0050, 0.0082)
        elif t == "led":
            pl.capsule(part["x"], part["y"], part["y"] + 0.0012, 0.0034)
        elif t == "knob":
            pl.knob(part["x"], part["y"], part["r"], part.get("h", 0.014) + 0.001)
        elif t == "switch":
            pl.capsule(part["x"], part["y"], part["y"] + 0.0060, 0.0034)
        elif t == "button":
            pl.circle(part["x"], part["y"], part["r"] * 1.30 + 0.0006)
        elif t == "slider":
            pl.rect((part["x"] - 0.0034, part["y0"] - 0.0035, part["x"] + 0.0034, part["y1"] + 0.0050))
        elif t == "display":
            pl.rect((part["x0"] - 0.0013, part["y0"] - 0.0013, part["x1"] + 0.0013, part["y1"] + 0.0013))
        elif t == "tubes":
            pl.rect((part["x"] - part["w"] / 2 - 0.001, part["y"] - part["d"] / 2 - 0.001,
                     part["x"] + part["w"] / 2 + 0.001, part["y"] + part["d"] / 2 + part["h"] * PL.TILT_TAN + 0.001))
    return pl


def text_at(c, pl, x, y, s, size, fnt, col=None, anchor="mm", spacing=0.0, force=False):
    box = tbox(x, y, s, size, fnt, anchor, spacing)
    if not force and not pl.free(box):
        return None
    if col is None:
        col = contrast(bg_at(c, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2))
    c.text(x, y, s, size, fnt, col, anchor=anchor, spacing=spacing)
    pl.rect(box)
    return box


def label_control(c, pl, cx, x, y, rad, h, lab, lg, maxw, kind="knob"):
    """Scritta di un comando: sotto, sopra (oltre la sommita' proiettata), a destra o a sinistra; si rimpicciolisce
    se serve. Restituisce il riquadro o None."""
    fnt = font(lg.get("font"), "bold")
    base = lg.get("size", 2.2) / 1000.0
    col = tuple(lg["col"]) if lg.get("col") else None
    shadow = PL.knob_shadow(h)
    order = ("above", "below") if lg.get("pos") == "above" else ("below", "above")
    for scale in (1.0, 0.88, 0.76, 0.66):
        size = MC.fit_size(lab, base * scale, maxw, fnt)
        for where in order + ("right", "left"):
            if where == "below":
                pos, anc = (x, y - rad - 0.0016 - size * 0.6), "mm"
            elif where == "above":
                pos, anc = (x, y + rad + shadow + 0.0016 + size * 0.6), "mm"
            elif where == "right":
                pos, anc = (x + rad + 0.0020, y), "lm"
            else:
                pos, anc = (x - rad - 0.0020, y), "rm"
            box = text_at(c, pl, pos[0], pos[1], lab, size, fnt, col, anc)
            if box:
                return box
    return None


def block_place(c, pl, cx, lines, pref_y, x_pct, zone, dry=False):
    """Blocco di righe (nome, sigla, sottotitolo) nella fascia libera piu' vicina a pref_y, rimpicciolito se serve.
    lines = [(testo, size m, font, colore|None, spacing)]; zone = (y0, y1) dove cercare.
    dry=True: non disegna e non occupa, restituisce i riquadri delle righe (o None)."""
    W = cx.W
    x = cx.X(x_pct)
    span = W - 0.010
    best = None
    for scale in (1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.42, 0.35):
        sz = []
        for s, size, fnt, col, sp in lines:
            maxw = span * 0.92
            sz.append(MC.fit_size(s, size * scale, maxw, fnt))
        hs = [tbox(0, 0, s, z, f, "mm", sp)[3] - tbox(0, 0, s, z, f, "mm", sp)[1]
              for (s, _, f, _, sp), z in zip(lines, sz)]
        gap = 0.0012
        total = sum(hs) + gap * (len(hs) - 1)
        ys = []
        y0, y1 = zone
        cands = []
        yy = y0 + total / 2
        while yy <= y1 - total / 2 + 1e-9:
            cands.append(yy)
            yy += 0.0005
        if pref_y is not None:
            cands.sort(key=lambda v: abs(v - pref_y))
        else:
            mid = (y0 + y1) / 2
            cands.sort(key=lambda v: abs(v - mid))
        for yc in cands:
            top = yc + total / 2
            boxes = []
            ok = True
            for (s, _, f, _, sp), z, hh in zip(lines, sz, hs):
                b0 = tbox(x, 0.0, s, z, f, "mm", sp)
                ty = top - hh / 2 - (b0[1] + b0[3]) / 2
                b = tbox(x, ty, s, z, f, "mm", sp)
                if not pl.free(b):
                    ok = False
                    break
                boxes.append((ty, b))
                top -= hh + gap
            if ok:
                best = (sz, boxes)
                break
        if best:
            break
    if not best:
        return None if dry else False
    sz, boxes = best
    if dry:
        return [b for _, b in boxes]
    for (s, _, f, col, sp), z, (ty, b) in zip(lines, sz, boxes):
        cc = col or contrast(bg_at(c, (b[0] + b[2]) / 2, (b[1] + b[3]) / 2))
        c.text(x, ty, s, z, f, cc, anchor="mm", spacing=sp)
        pl.rect(b)
    return True


def draw_name_deco(c, cx, deco, boxes, pl):
    """deco = dict(style=band|box|frame|lines, col=colore, pad=mm): attorno ai riquadri del blocco del nome, senza
    coprire scritte e comandi gia' piazzati (la fascia diventa un riquadro, il margine si riduce)."""
    W, D = cx.W, cx.D
    st = deco.get("style")
    for pad in (deco.get("pad", 2.0) / 1000.0, 0.0012, 0.0006):
        x0 = max(min(b[0] for b in boxes) - pad * 1.6, -W / 2 + 0.003)
        x1 = min(max(b[2] for b in boxes) + pad * 1.6, W / 2 - 0.003)
        y0 = min(b[1] for b in boxes) - pad
        y1 = max(b[3] for b in boxes) + pad
        if st == "band" and pl.free((-W / 2 + 0.003, y0, W / 2 - 0.003, y1)):
            break
        if pl.free((x0, y0, x1, y1)):
            if st == "band":
                st = "box"
            break
    else:
        if st == "band":
            st = "box"
    col = tuple(deco["col"])
    if st == "band":
        c.rect(-W / 2 - 0.001, y0, W / 2 + 0.001, y1, fill=col)
    elif st == "box":
        c.rect(x0, y0, x1, y1, fill=col, radius_m=0.002)
    elif st == "frame":
        c.rect(x0, y0, x1, y1, outline=col, width_m=0.0006, radius_m=0.002)
    elif st == "lines":
        c.line(x0 + 0.001, y1, x1 - 0.001, y1, col, 0.0008)
        c.line(x0 + 0.001, y0, x1 - 0.001, y0, col, 0.0008)


def top_texture(r, lay, out):
    cx = Ctx(r, lay)
    W, D = cx.W, cx.D
    g = cx.gfx
    normal = bool(r.get("normal"))
    bgc = tuple(g.get("bg") or r["colour"])
    c = MT.Canvas(-W / 2, W / 2, -D / 2, D / 2, bgc)
    draw_panels(c, cx, g.get("panels") or [])
    c.speckle(2.5 if r["look"].get("finish") in ("paint", "matte") else 1.5, 0.6)
    pl = placer(cx, lay)
    lg = dict(g.get("label") or {})
    # slot dei cursori
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        if p["strip"] == "slider":
            c.rect(p["x"] - 0.0009, p["y"] - 0.0020, p["x"] + 0.0009, p["y1"] + 0.0020, fill=(4, 4, 4), radius_m=0.0006)
    for part in lay["parts"]:
        if part["type"] == "slider":
            c.rect(part["x"] - 0.0009, part["y0"] - 0.0020, part["x"] + 0.0009, part["y1"] + 0.0020, fill=(4, 4, 4), radius_m=0.0006)
        elif part["type"] == "display":
            c.rect(part["x0"] - 0.0012, part["y0"] - 0.0012, part["x1"] + 0.0012, part["y1"] + 0.0012, fill=(40, 42, 46),
                   radius_m=0.0012)
    rings = []
    # ---- scritte dei comandi
    for k, ctl in enumerate(r["controls"]):
        p = lay["controls"][k]
        st = p["strip"]
        lab = ctl["label"]
        if st in ("boss", "boss_small", "ts_big", "ts_small", "boss_outer"):
            h = p["h"] + (PL.KNOB_H["boss_inner"] if st == "boss_outer" else 0.0)
            if st == "boss_outer":
                inner = [j for j, q in enumerate(lay["controls"]) if q["strip"] == "boss_inner" and abs(q["x"] - p["x"]) < 1e-6
                         and abs(q["y"] - p["y"]) < 1e-6]
                if inner:
                    lab = lab + "/" + r["controls"][inner[0]]["label"]
            label_control(c, pl, cx, p["x"], p["y"], p["r"], h, lab, lg, 0.026)
            if ctl["kind"] == "selector":
                rings.append((p, h, ctl.get("short") or [str(x)[:4] for x in ctl["choices"]]))
            elif g.get("ticks"):
                col = contrast(bg_ring(c, p["x"], p["y"], p["r"] + 0.0030))
                c.arc_ticks(p["x"], p["y"], p["r"] + 0.0010, p["r"] + 0.0018, 11, col, 0.00026)
        elif st == "toggle":
            ch = ctl.get("choices") or ["S", "C"]
            for j, sgn in ((0, -1), (1, 1)):
                s = str(ch[j])[:5].upper()
                size = MC.fit_size(s, 0.0016, 0.0060, MT.F_BOLD)
                text_at(c, pl, p["x"] + sgn * MC.toggle_dx(s, size), p["y"] + 0.0010, s, size, MT.F_BOLD)
            label_control(c, pl, cx, p["x"], p["y"], 0.0034, 0.0, lab, dict(lg, size=min(lg.get("size", 2.2), 1.9)), 0.020)
        elif st == "button":
            col = contrast(bg_ring(c, p["x"], p["y"], p["r"] * 1.3 + 0.0020))
            c.circle(p["x"], p["y"], p["r"] * 1.30 + 0.0008, outline=col, width_m=0.0003)
            label_control(c, pl, cx, p["x"], p["y"], p["r"] * 1.3 + 0.0008, 0.0, lab, dict(lg, size=min(lg.get("size", 2.2), 1.9)),
                          0.022)
        elif st == "slider":
            size = MC.fit_size(lab, 0.0016, 0.0105, MT.F_NARROW)
            for yy in (p["y1"] + 0.0058, p["y"] - 0.0048):
                if text_at(c, pl, p["x"], yy, lab, size, MT.F_NARROW):
                    break
    # comandi fissi del pedale vero (decorativi): stessa serigrafia
    for part in lay["parts"]:
        t = part["type"]
        if t == "knob" and part.get("label"):
            label_control(c, pl, cx, part["x"], part["y"], part["r"], part.get("h", 0.014), part["label"][:12], lg, 0.024)
        elif t in ("switch", "button") and part.get("label"):
            label_control(c, pl, cx, part["x"], part["y"], 0.0045, 0.0, part["label"][:12], dict(lg, size=1.7), 0.020)
        elif t == "slider" and part.get("label"):
            s = part["label"][:12]
            size = MC.fit_size(s, 0.0015, 0.0105, MT.F_NARROW)
            for yy in (part["y1"] + 0.0058, part["y0"] - 0.0048):
                if text_at(c, pl, part["x"], yy, s, size, MT.F_NARROW):
                    break
    # ---- nome, sigla, sottotitolo
    foot_ys = [p["y"] for p in lay["parts"] if p["type"] == "footswitch"]
    y_lo = (max(foot_ys) + 0.0085) if foot_ys else -D / 2 + 0.006
    zone = (min(y_lo, D / 2 - 0.02), D / 2 - 0.004)
    nm = g.get("name") or {}
    cd = g.get("code")
    sb = g.get("sub")
    name = r["real_name"]          # modalita' normale (as_shaped): nome Pedal Trinity; replica: nome reale
    if not normal and r["look"].get("print"):
        name = r["look"]["print"]  # replica: nome del modello come stampato sul pedale vero (senza marchio)
    code = r["real_code"]
    if nm.get("upper"):
        name = name.upper()
    lines = [(name, nm.get("size", 7.0) / 1000.0, font(nm.get("font"), "black"), tuple(nm["col"]) if nm.get("col") else None,
              nm.get("spacing", 0.0) / 1000.0)]
    sub_line = None
    if sb:
        st = sb.get("text") or r.get("subtitle") or ""
        if st:
            sub_line = (st, sb.get("size", 2.4) / 1000.0, font(sb.get("font"), "bold"), tuple(sb["col"]) if sb.get("col") else None,
                        sb.get("spacing", 0.25) / 1000.0)
    code_line = None
    if cd is not False:
        cd = cd or {}
        code_line = (code, cd.get("size", 3.2) / 1000.0, font(cd.get("font"), "black"), tuple(cd["col"]) if cd.get("col") else None,
                     cd.get("spacing", 0.0) / 1000.0)
    block = list(lines)
    if sub_line and not sb.get("y"):
        block.append(sub_line)
    if code_line and not cd.get("y") and cd.get("with_name", True):
        if cd.get("above"):
            block.insert(0, code_line)
        else:
            block.append(code_line)
    pref = cx.Y(nm["y"]) if nm.get("y") is not None else None
    deco = g.get("name_deco")
    if deco:
        # decorazione attorno al blocco del nome (tappa 3B): fascia, riquadro, cornice o filetti che seguono la
        # posizione effettiva del blocco (prova a vuoto, poi disegno della decorazione e del testo)
        boxes = block_place(c, pl, cx, block, pref, nm.get("x", 50), zone, dry=True) or \
            block_place(c, pl, cx, block, pref, nm.get("x", 50), (-D / 2 + 0.004, D / 2 - 0.004), dry=True)
        if boxes:
            draw_name_deco(c, cx, deco, boxes, pl)
    if not block_place(c, pl, cx, block, pref, nm.get("x", 50), zone):
        block_place(c, pl, cx, block, pref, nm.get("x", 50), (-D / 2 + 0.004, D / 2 - 0.004))
    if sub_line and sb.get("y"):
        block_place(c, pl, cx, [sub_line], cx.Y(sb["y"]), sb.get("x", 50), (-D / 2 + 0.004, D / 2 - 0.004))
    if code_line and (cd.get("y") or not cd.get("with_name", True)):
        block_place(c, pl, cx, [code_line], cx.Y(cd["y"]) if cd.get("y") else None, cd.get("x", 50),
                    (-D / 2 + 0.004, D / 2 - 0.004) if cd.get("y") else zone)
    # nomi dei footswitch (solo sui pedali con piu' footswitch, come sui pedali veri)
    feet = [p for p in lay["parts"] if p["type"] == "footswitch"]
    if len(feet) > 1:
        for p in feet:
            s = p["label"][:12]
            for yy in (p["y"] - 0.0118, p["y"] + 0.0135):
                if text_at(c, pl, p["x"], yy, s, MC.fit_size(s, 0.0020, 0.024, MT.F_BOLD), MT.F_BOLD,
                           tuple(lg["col"]) if lg.get("col") else None):
                    break
    # scritte decorative generiche (nessun marchio)
    for p in g.get("panels") or []:
        if p[0] == "text":
            fnt = font(p[6] if len(p) > 6 else "bold")
            text_at(c, pl, cx.X(p[2]), cx.Y(p[3]), p[1], p[4] / 1000.0, fnt, tuple(p[5]) if p[5] else None)
    # marchio Pedal Trinity (solo modalita' normale)
    if normal:
        br = g.get("brand") or {}
        cands = []
        if br:
            cands.append((cx.X(br.get("x", 50)), cx.Y(br.get("y", 92))))
        fy = min(foot_ys) if foot_ys else -D / 2 + 0.012
        cands += [(0.0, -D / 2 + 0.0055), (-W / 4, fy), (W / 4, fy), (-W / 2 + 0.014, D / 2 - 0.006),
                  (W / 2 - 0.014, D / 2 - 0.006), (0.0, D / 2 - 0.006)]
        for x, y in cands:
            for size in (0.0026, 0.0022, 0.0019):
                if text_at(c, pl, x, y, "FabioNET", size, MT.F_BOLDIT, tuple(br["col"]) if br.get("col") else None):
                    break
            else:
                continue
            break
    # nomi delle posizioni dei selettori
    for p, h, names in rings:
        col = contrast(bg_ring(c, p["x"], p["y"], p["r"] + 0.0045))
        MC.ring_names(c, pl, p["x"], p["y"], p["r"], h, names, 0.0012, col, r_gap=0.0028)
    MT.OUT = out
    c.save("%s_top.png" % r["id"])
