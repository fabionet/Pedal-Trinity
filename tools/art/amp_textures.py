"""
Pedal Trinity - serigrafie e tessuti di amplificatori e casse (tappa 6), per render_pedals.py:
  <id>_apanel.png     pannello dei comandi (scritte, scale, riquadri) nelle coordinate del pannello (amp_layout.Panel)
  <id>_agrille<k>.png tela della griglia della zona k della faccia (trama secondo lo stile, coni visti attraverso la tela)
  <id>_alogo.png      targhetta del logo (solo modalita' normale: scritta originale "Trinity"; REAL MOD: nessun logo)
  <id>_aback.png      piastra posteriore del telaio o delle prese (scritte degli elementi del retro)
  <id>_aconsole.png   piano della pedaliera dei comandi davanti alle casse
  <id>_adiamond.png   lamiera mandorlata (frontale a 'diamond plate')
Nomi, sigle e scritte: modalita' normale = nome e sigla Pedal Trinity; REAL MOD = nome e sigla reali senza marchio.
Mai loghi, artwork o marchi dei produttori.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import amp_layout as AL
import make_textures as MT

FD = "/usr/share/fonts/opentype/urw-base35/"
F_SCRIPT = FD + "Z003-MediumItalic.otf"
F_GOTHIC = FD + "URWGothic-Demi.otf"
F_ROUND = FD + "URWGothic-Book.otf"
F_BLOCK = MT.F_BLACK
F_LABEL = MT.F_BOLD
F_NARROW = MT.F_NARROW
LOGO_TEXT = {"script": "Trinity", "block": "TRINITY", "round": "trinity", "gothic": "TRINITY"}
LOGO_FONT = {"script": F_SCRIPT, "block": F_BLOCK, "round": F_ROUND, "gothic": F_GOTHIC}
GPPM = 3000.0           # px/m delle tele delle griglie
rng = np.random.default_rng(11)


def _save(c, out, name):
    MT.OUT = out
    c.save(name)


def _fit(text, size, maxw, font):
    f = ImageFont.truetype(font, max(6, int(size * MT.PPM)))
    w = f.getlength(text) / MT.PPM
    return size if w <= maxw else size * maxw / max(w, 1e-6)


def _vis(lay):
    """Scala verticale apparente del pannello nell'immagine (inclinazione della camera)."""
    t = math.radians(lay["tilt"])
    P = lay.get("panel")
    if P is not None and P.view == "top" and not lay["amp"].get("console"):
        return math.cos(t)
    return math.sin(t)


def _knob_clear(lay, strip):
    """Semialtezza (m, coordinate del pannello) della sagoma disegnata di un pomello."""
    import pedal_layout as PL
    R, H = PL.KNOB_R[strip], PL.KNOB_H[strip]
    return (R * math.cos(math.radians(22)) + 0.5 * H * math.sin(math.radians(22))) / max(0.3, _vis(lay)), R


# ------------------------------------------------------------------ fondi dei pannelli
def _panel_bg(c, fin, col):
    a = np.asarray(c.img).astype(np.float32)
    h, w = a.shape[:2]
    if fin in ("brushed", "brushed_gold", "copper"):
        n = rng.normal(0, 9.0, (h, 1)) * np.ones((1, w))
        n += rng.normal(0, 3.0, (h, w))
        n = np.asarray(Image.fromarray(np.clip(n + 128, 0, 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(1))).astype(np.float32) - 128
        a += n[..., None] * 0.6
    elif fin == "plexi":
        g = np.linspace(1.06, 0.94, h)[:, None, None]
        a = a * g + rng.normal(0, 2.0, (h, w, 1))
    elif fin == "chrome":
        g = np.linspace(1.04, 0.90, h)[:, None, None]
        a = a * g + rng.normal(0, 1.5, (h, w, 1))
    else:
        a += rng.normal(0, 2.5, (h, w, 1))
    c.img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    c.d = ImageDraw.Draw(c.img)


def _ellipse_ticks(c, x, y, a, b, n, fill, wm, a0=-150, a1=150, l=0.0012):
    for i in range(n):
        t = math.radians(a0 + (a1 - a0) * i / (n - 1))
        sx, sy = math.sin(t), math.cos(t)
        c.line(x + sx * a, y + sy * b, x + sx * (a + l), y + sy * (b + l), fill, wm)


def _icon(c, kind, x, y, s, col):
    """Pittogrammi semplici (pannello 'pics only' delle testate Graphic): onda, chiavi, pugno, altoparlante."""
    w = max(1, int(0.00045 * MT.PPM))
    if kind == "wave":
        pts = [c.px(x - s + 2 * s * k / 30, y + 0.5 * s * math.sin(k / 30 * 4 * math.pi)) for k in range(31)]
        c.d.line(pts, fill=col, width=w)
    elif kind == "bassclef":
        c.d.arc([*c.px(x - 0.6 * s, y + 0.6 * s), *c.px(x + 0.4 * s, y - 0.4 * s)], 180, 80, fill=col, width=w)
        c.circle(x - 0.45 * s, y + 0.1 * s, 0.15 * s, fill=col)
        c.circle(x + 0.65 * s, y + 0.3 * s, 0.09 * s, fill=col)
        c.circle(x + 0.65 * s, y - 0.1 * s, 0.09 * s, fill=col)
    elif kind == "trebleclef":
        c.d.arc([*c.px(x - 0.4 * s, y + 0.1 * s), *c.px(x + 0.4 * s, y - 0.7 * s)], 0, 360, fill=col, width=w)
        c.line(x + 0.1 * s, y - 0.9 * s, x + 0.1 * s, y + 0.9 * s, col, 0.00045)
        c.d.arc([*c.px(x - 0.2 * s, y + 1.0 * s), *c.px(x + 0.4 * s, y + 0.4 * s)], 180, 360, fill=col, width=w)
    elif kind == "fist":
        c.rect(x - 0.5 * s, y - 0.5 * s, x + 0.5 * s, y + 0.3 * s, fill=col, radius_m=0.2 * s)
        for k in range(4):
            c.circle(x - 0.38 * s + k * 0.25 * s, y + 0.38 * s, 0.13 * s, fill=col)
    elif kind == "speaker":
        c.rect(x - 0.6 * s, y - 0.25 * s, x - 0.25 * s, y + 0.25 * s, fill=col)
        c.d.polygon([c.px(x - 0.25 * s, y - 0.25 * s), c.px(x + 0.2 * s, y - 0.6 * s), c.px(x + 0.2 * s, y + 0.6 * s),
                     c.px(x - 0.25 * s, y + 0.25 * s)], fill=col)
        for k in range(2):
            rr = (0.45 + 0.25 * k) * s
            c.d.arc([*c.px(x + 0.2 * s - rr, y + rr), *c.px(x + 0.2 * s + rr, y - rr)], -40, 40, fill=col, width=w)


def panel_texture(r, lay, out):
    A = lay["amp"]
    P = lay["panel"]
    pn = A["panel"]
    normal = bool(r.get("normal"))
    col = tuple(pn["col"])
    text = tuple(pn["text"])
    c = MT.Canvas(-P.w / 2, P.w / 2, -P.h / 2, P.h / 2, col)
    _panel_bg(c, pn.get("finish"), col)
    vis = _vis(lay)
    strip = A.get("strip") or "ts_small"
    clear, R = _knob_clear(lay, strip)
    fs = min(0.0034, max(0.0022, P.h * 0.085))       # carattere delle etichette
    console = A.get("console")
    # riquadro nero (pannelli con la scatola della serie a sinistra) e righe colorate attorno ai gruppi
    if pn.get("box"):
        bx0, by0, bx1, by1 = pn["box"]
        x0, y0 = P.local(bx0 / 100.0, by1 / 100.0)
        x1, y1 = P.local(bx1 / 100.0, by0 / 100.0)
        c.rect(x0, y0, x1, y1, fill=(14, 14, 16), radius_m=0.0008)
    if pn.get("lines"):
        lc = tuple(pn["lines"])
        groups = {}
        for e in A.get("entries") or []:
            if e["kind"] in ("knob", "selector", "toggle", "mini") and e.get("ch") and str(e["ch"]).lower() not in ("none",):
                groups.setdefault(e["ch"], []).append(P.local(e["x"] / 100.0, e["y"] / 100.0))
        for g, pts in groups.items():
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            c.rect(min(xs) - R - 0.004, max(-P.h / 2 + 0.0025, min(ys) - clear - 0.002), max(xs) + R + 0.004,
                   min(P.h / 2 - 0.0025, max(ys) + clear + fs + 0.003), outline=lc, width_m=0.0006, radius_m=0.0015)
    items = []
    relabel = lay.get("relabel") or {}
    strip_of = {}
    for p, cc in zip(lay["controls"], r["controls"]):
        if p.get("entry") is not None:
            strip_of[p["entry"]] = p["strip"]
    if not console:
        hidden = set(lay.get("hidden") or [])
        for j, e in enumerate(A.get("entries") or []):
            if j in hidden:
                continue
            e = dict(e)
            if j in relabel:
                e["label"] = relabel[j]
            st = strip_of.get(j)
            if st in ("ts_big", "ts_small", "boss", "boss_outer") and e["kind"] not in ("knob", "selector"):
                e["kind"] = "knob"            # selettore funzionale su un commutatore del pannello vero
            elif st == "toggle" and e["kind"] in ("knob", "selector"):
                e["kind"] = "toggle"
            items.append((e, e["x"] / 100.0, e["y"] / 100.0))
        for ex in lay.get("extra") or []:
            items.append((dict(label=ex["label"], kind="knob" if r["controls"][ex["ctrl"]]["kind"] in ("knob", "selector", "outer")
                               else "toggle", key=ex["label"], ch=None), ex["x"] / 100.0, ex["y"] / 100.0))
    icons = pn.get("icons") and not normal
    boxr = None
    if pn.get("box"):
        bx0, by0, bx1, by1 = pn["box"]
        boxr = (bx0 / 100.0, by0 / 100.0, bx1 / 100.0, by1 / 100.0)
    base_text = text
    import pedal_layout as PL
    # ---- ingombri disegnati (pomelli filmstrip o modellati, levette, prese, spie) nelle coordinate del pannello
    func = {}
    for p_, cc in zip(lay["controls"], r["controls"]):
        if p_.get("amp") and not p_.get("inner_of"):
            func[(round(p_["u"], 4), round(p_["v"], 4))] = p_
    obst = []

    def knob_ext(st):
        rr, hh = PL.KNOB_R.get(st, R), PL.KNOB_H.get(st, 0.0145)
        return rr + 0.0024, (rr * math.cos(math.radians(22)) + 0.5 * hh * math.sin(math.radians(22))) / max(0.3, vis) + 0.0005
    ext_of = []
    for e, u, v in items:
        x, y = P.local(u, v)
        p_ = func.get((round(u, 4), round(v, 4)))
        k = e["kind"]
        if p_ is not None and p_["strip"] in PL.KNOB_R:
            ex, ey = knob_ext(p_["strip"])
        elif p_ is not None and p_["strip"] == "slider":
            dv = abs(p_["c3"][2] - p_["t3"][2]) if P.view == "front" else abs(p_["c3"][1] - p_["t3"][1])
            ex, ey = 0.0020, dv / 2 + 0.0016
        elif k in ("knob", "selector"):
            ex, ey = knob_ext(strip)
        elif k == "slider":
            ex, ey = 0.0020, 0.30 * min(1.0, 0.040 / max(1e-6, P.h)) * P.h + 0.0016
        elif k in ("power", "standby", "rocker"):
            ex, ey = (0.0062, 0.0082) if A.get("rockers") else (0.0035, 0.0040)
        elif k in ("toggle", "mini"):
            ex, ey = 0.0035, 0.0040
        elif k == "jack":
            ex = ey = 0.0053
        elif k == "lamp":
            ex = ey = 0.0062
        elif k == "led":
            ex = ey = 0.0030
        elif k == "button":
            ex = ey = 0.0047
        else:
            ex = ey = 0.0046
        ext_of.append((ex, ey))
        obst.append((x - ex, y - ey, x + ex, y + ey))
    if lay.get("led"):
        lu, lv = lay["led_uv"]
        lx, ly = P.local(lu, lv)
        rr = lay["led"][3] * 1.6 + 0.001
        obst.append((lx - rr, ly - rr, lx + rr, ly + rr))
    if pn.get("plate"):
        px_, py_ = P.local(pn["plate"][0] / 100.0, pn["plate"][1] / 100.0)
        obst.append((px_ - P.w * 0.0325, py_ - P.h * 0.21, px_ + P.w * 0.0325, py_ + P.h * 0.21))
    texts = []
    M = 0.0026                              # margine dal bordo smussato
    lim = (-P.w / 2 + M, -P.h / 2 + M, P.w / 2 - M, P.h / 2 - M)

    def tbox(s_, size, font, x, y, anchor="mm"):
        f = ImageFont.truetype(font, max(6, int(size * MT.PPM)))
        w = f.getlength(s_) / MT.PPM
        bb = f.getbbox(s_, anchor="ls")
        hh = (bb[3] - bb[1]) / MT.PPM
        x0 = x - w / 2 if anchor[0] == "m" else (x if anchor[0] == "l" else x - w)
        return (x0, y - hh / 2, x0 + w, y + hh / 2)

    def free(bx, own=None):
        if bx[0] < lim[0] or bx[1] < lim[1] or bx[2] > lim[2] or bx[3] > lim[3]:
            return False
        for q in obst + texts:
            if q is own:
                continue
            if bx[0] < q[2] + 0.0003 and q[0] < bx[2] + 0.0003 and bx[1] < q[3] + 0.0003 and q[1] < bx[3] + 0.0003:
                return False
        return True

    def place(s_, size, font, x, y, ex, ey, maxw, prefer_below=False):
        """Etichetta sopra (o sotto, a destra, a sinistra) del comando, ridotta se serve; None se non c'e' posto."""
        for k_ in (1.0, 0.86, 0.74):
            sz = _fit(s_, size * k_, maxw, font)
            w0 = tbox(s_, sz, font, 0, 0)
            tw, th = w0[2] - w0[0], w0[3] - w0[1]
            cands = [(x, y + ey + 0.0008 + th / 2), (x, y - ey - 0.0008 - th / 2),
                     (x + ex + 0.0008 + tw / 2, y), (x - ex - 0.0008 - tw / 2, y)]
            if prefer_below:
                cands[0], cands[1] = cands[1], cands[0]
            for (cx_, cy_) in cands:
                bx = tbox(s_, sz, font, cx_, cy_)
                if free(bx):
                    texts.append(bx)
                    return cx_, cy_, sz
        return None
    for (e, u, v), (ex, ey) in zip(items, ext_of):
        x, y = P.local(u, v)
        k = e["kind"]
        lab = e.get("label") or ""
        text = (226, 200, 120) if boxr and boxr[0] <= u <= boxr[2] and boxr[1] <= v <= boxr[3] else base_text
        if k in ("knob", "selector"):
            _ellipse_ticks(c, x, y, R + 0.0010, clear + 0.0006, 11, text, 0.00028)
        elif k in ("toggle", "mini", "power", "standby"):
            c.circle(x, y, 0.0030, fill=None, outline=tuple(int(q * 0.6 + 50) for q in text), width_m=0.0003)
        elif k == "jack":
            c.circle(x, y, 0.0048, fill=None, outline=text, width_m=0.0003)
        elif k == "slider":
            dv = ey - 0.0016
            c.rect(x - 0.0007, y - dv, x + 0.0007, y + dv, fill=(8, 8, 8))
        if not lab or k in ("lamp", "led"):
            continue
        if icons and lab in ICONS_OF and k in ("knob", "selector"):
            q = place("XX", fs, F_LABEL, x, y, ex, ey, 0.02)
            if q:
                _icon(c, ICONS_OF[lab], q[0], q[1], fs * 0.9, text)
            continue
        size = fs if k in ("knob", "selector") else fs * (0.85 if k != "slider" else 0.75)
        maxw = 2 * R + 0.012 if k in ("knob", "selector") else 0.016
        q = place(lab, size, F_LABEL, x, y, ex, ey, maxw)
        if q:
            c.text(q[0], q[1], lab, q[2], F_LABEL, text)
    text = base_text

    def place_free(s_, size, font, x, y, maxw):
        """Testo libero (nome, sigla): la posizione indicata o la piu' vicina libera sul pannello."""
        for k_ in (1.0, 0.8, 0.64):
            sz = _fit(s_, size * k_, maxw, font)
            best = None
            for dy in [0.0] + [d * 0.0015 for d in range(1, 30)] + [-d * 0.0015 for d in range(1, 30)]:
                for dx in [0.0] + [d * 0.003 for d in range(1, 60)] + [-d * 0.003 for d in range(1, 60)]:
                    bx = tbox(s_, sz, font, x + dx, y + dy)
                    if free(bx):
                        dd = math.hypot(dx, 2 * dy)
                        if best is None or dd < best[0]:
                            best = (dd, x + dx, y + dy, bx)
                        break
            if best is not None and best[0] < max(0.05, P.w * 0.25):
                texts.append(best[3])
                return best[1], best[2], sz
        return None
    # nome del modello sul pannello (dove il pannello vero lo porta) e sigla
    name = r["real_name"]
    nm = pn.get("name")
    if console:
        q = place_free(name, min(0.010, P.h * 0.32), F_SCRIPT, 0.0, P.h * 0.12, P.w * 0.6)
        if q:
            c.text(q[0], q[1], name, q[2], F_SCRIPT, text)
        if normal:
            q = place_free(r["real_code"], fs, F_LABEL, 0.0, -P.h * 0.26, P.w * 0.3)
            if q:
                c.text(q[0], q[1], r["real_code"], q[2], F_LABEL, text)
    elif nm:
        nx, ny = P.local(nm[0] / 100.0, nm[1] / 100.0)
        fnt = {"script": F_SCRIPT, "block": F_BLOCK, "blockl": F_BLOCK, "gothic": F_GOTHIC, "small": F_LABEL}.get(nm[2], F_LABEL)
        size = min(P.h * (0.30 if nm[2] == "script" else 0.20), 0.0080)
        txt = name if nm[2] != "blockl" else name.upper()
        q = place_free(txt, size, fnt, nx, ny, P.w * 0.16)
        if q:
            c.text(q[0], q[1], txt, q[2], fnt, text)
            if normal and nm[2] != "small":
                q2 = place_free(r["real_code"], fs * 0.85, F_LABEL, q[0], q[1] - q[2] * 0.95, P.w * 0.16)
                if q2:
                    c.text(q2[0], q2[1], r["real_code"], q2[2], F_LABEL, text)
    if pn.get("box"):
        bx0, by0, bx1, by1 = pn["box"]
        cx, cy = P.local((bx0 + bx1) / 200.0, (by0 + by1) / 200.0)
        bw = (bx1 - bx0) / 100.0 * P.w
        bh = (by1 - by0) / 100.0 * P.h
        t1 = name.upper() if not normal else r["real_code"]
        old = lim
        lim = (cx - bw / 2 + 0.001, cy - bh / 2 + 0.001, cx + bw / 2 - 0.001, cy + bh / 2 - 0.001)
        q = place_free(t1, fs * 1.2, F_GOTHIC, cx + bw * 0.18, cy + bh * 0.28, bw * 0.55)
        lim = old
        if q:
            c.text(q[0], q[1], t1, q[2], F_GOTHIC, (226, 200, 120))
    if pn.get("plate"):
        px, py = P.local(pn["plate"][0] / 100.0, pn["plate"][1] / 100.0)
        pw, ph = P.w * 0.065, P.h * 0.42
        c.rect(px - pw / 2, py - ph / 2, px + pw / 2, py + ph / 2, fill=(226, 226, 220), radius_m=0.0006)
        t = name.upper()
        c.text(px, py, t, _fit(t, ph * 0.38, pw * 0.86, F_BLOCK), F_BLOCK, (20, 20, 22))
    _save(c, out, "%s_apanel.png" % r["id"])


ICONS_OF = {"F.A.C.": "wave", "BASS": "bassclef", "TREBLE": "trebleclef", "HF DRIVE": "fist", "GAIN": "speaker"}


# ------------------------------------------------------------------ tele delle griglie
def _weave(h, w, c1, c2, period=4.0, mix=0.35):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    a = (np.sin(xx * 2 * np.pi / period) * np.sin(yy * 2 * np.pi / period) > 0).astype(np.float32)
    n = rng.normal(0, 0.08, (h, w)).astype(np.float32)
    t = np.clip(a * mix + 0.5 * (1 - mix) + n, 0, 1)[..., None]
    return np.array(c1, np.float32) * (1 - t) + np.array(c2, np.float32) * t


def grille_image(style, w_m, h_m, ppm=GPPM):
    w, h = max(8, int(w_m * ppm)), max(8, int(h_m * ppm))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    mm = ppm / 1000.0                       # px per mm
    if style == "bf_silver":
        a = _weave(h, w, (160, 164, 162), (210, 212, 208), 2.6, 0.35)
        thread = ((np.floor(xx / 2.6) % 4 == 0) | (np.floor(yy / 2.6) % 4 == 0))[..., None]
        a = np.where(thread, a * np.array([0.80, 0.86, 0.90]), a)
    elif style == "oxblood":
        band = (np.floor(yy / (7.5 * mm)) % 2)[..., None]
        a = _weave(h, w, (92, 38, 30), (128, 62, 44), 3.0, 0.3) * (1 - band) + _weave(h, w, (176, 146, 100), (206, 178, 126), 3.0, 0.3) * band
        gold = (np.abs((yy % (15 * mm)) - 7.5 * mm) < 0.5 * mm)[..., None]
        a = np.where(gold, np.array([210, 175, 100], np.float32), a)
    elif style == "diamond":
        a = _weave(h, w, (72, 34, 30), (98, 48, 40), 3.0, 0.3)
        s = 38 * mm
        d1 = np.abs(((xx + yy) % s) - s / 2) > s / 2 - 1.3 * mm
        d2 = np.abs(((xx - yy) % s) - s / 2) > s / 2 - 1.3 * mm
        a = np.where((d1 | d2)[..., None], np.array([196, 168, 120], np.float32), a)
    elif style in ("black_silver", "black_fine", "fine_dark"):
        c2 = {"black_silver": (92, 92, 94), "black_fine": (52, 52, 54), "fine_dark": (46, 46, 50)}[style]
        a = _weave(h, w, (14, 14, 15), c2, 3.2, 0.5)
    elif style == "silver_black":
        a = _weave(h, w, (40, 40, 42), (150, 152, 154), 3.2, 0.55)
    elif style == "silver_grey":
        a = _weave(h, w, (150, 152, 154), (200, 202, 204), 3.2, 0.5)
    elif style == "light_fine":
        a = _weave(h, w, (170, 170, 168), (212, 212, 208), 2.8, 0.4)
    elif style in ("basket_beige", "basket_light"):
        c1, c2 = ((176, 150, 104), (214, 194, 150)) if style == "basket_beige" else ((168, 162, 140), (214, 208, 186))
        p = 6.0 * mm
        cell = ((np.floor(xx / p) + np.floor(yy / p)) % 2)[..., None]
        sx = (np.sin(xx * 2 * np.pi / (p / 3)) * 0.5 + 0.5)[..., None]
        sy = (np.sin(yy * 2 * np.pi / (p / 3)) * 0.5 + 0.5)[..., None]
        t = cell * sx + (1 - cell) * sy
        a = np.array(c1, np.float32) * (1 - t) + np.array(c2, np.float32) * t + rng.normal(0, 4, (h, w, 1))
    elif style == "saltpepper":
        a = _weave(h, w, (16, 16, 17), (40, 40, 42), 3.0, 0.4)
        fl = rng.random((h, w)) > 0.93
        a = np.where(fl[..., None], np.array([180, 180, 176], np.float32), a)
    elif style == "jute":
        a = _weave(h, w, (12, 12, 12), (40, 38, 34), 5.0, 0.6)
    elif style == "wicker":
        p = 9.0 * mm
        d1 = (np.abs(((xx + yy) % p) - p / 2) < p * 0.22)
        d2 = (np.abs(((xx - yy) % p) - p / 2) < p * 0.22)
        a = np.full((h, w, 3), 10.0, np.float32)
        a = np.where((d1 | d2)[..., None], np.array([46, 44, 42], np.float32), a)
        a += rng.normal(0, 3, (h, w, 1))
    elif style in ("mesh", "metal_black"):
        p = (4.0 if style == "mesh" else 5.5) * mm
        ox = np.where((np.floor(yy / p) % 2) == 1, p / 2, 0.0)
        hole = (((xx + ox) % p - p / 2) ** 2 + ((yy % p) - p / 2) ** 2) < (p * 0.32) ** 2
        a = np.where(hole[..., None], np.array([6, 6, 6], np.float32), np.array([48, 48, 50], np.float32))
        a += rng.normal(0, 2, (h, w, 1))
    else:
        a = _weave(h, w, (20, 20, 22), (60, 60, 62), 3.2, 0.4)
    return a


def grille_texture(r, lay, out, k, f):
    A = lay["amp"]
    X0, X1, Z0, Z1 = f["xz"]
    w, h = X1 - X0, Z1 - Z0
    a = grille_image(f.get("style"), w, h)
    H, W = a.shape[:2]
    mm = GPPM / 1000.0
    # coni dietro la tela: ombra morbida, bordo del cono e cupola centrale
    W0, Hf, z0 = lay["W"], lay["H"], lay["z0"]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    shade = np.ones((H, W), np.float32)
    for (sx, sy, sd) in A.get("speakers") or []:
        cx = ((sx / 100.0 - 0.5) * W0 - X0) * GPPM
        cz = (Z1 - (z0 + Hf * (1 - sy / 100.0))) * GPPM
        rad = sd / 100.0 * W0 / 2 * GPPM
        d = np.sqrt((xx - cx) ** 2 + (yy - cz) ** 2)
        shade *= np.where(d < rad, 0.93 + 0.03 * (d / rad) ** 2, 1.0)
        shade *= 1.0 - 0.05 * np.exp(-((d - rad * 0.97) / (1.6 * mm)) ** 2)
        shade *= np.where(d < rad * 0.24, 0.97, 1.0)
    if A.get("port"):
        px, pz, pw, ph = A["port"]
        cx, cz = ((px / 100.0 - 0.5) * W0 - X0) * GPPM, (Z1 - (z0 + Hf * (1 - pz / 100.0))) * GPPM
        m = (np.abs(xx - cx) < pw / 200.0 * W0 * GPPM) & (np.abs(yy - cz) < ph / 200.0 * Hf * GPPM)
        shade *= np.where(m, 0.55, 1.0)
    if A.get("horn"):
        cx, cz = ((0.5 - 0.5) * W0 - X0) * GPPM, (Z1 - (z0 + Hf * 0.5)) * GPPM
        d = np.sqrt((xx - cx) ** 2 + (yy - cz) ** 2)
        shade *= np.where(d < 0.045 * W0 * GPPM, 0.75, 1.0)
    a = a * shade[..., None]
    img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))
    img.save(os.path.join(out, "%s_agrille%d.png" % (r["id"], k)))


def diamond_texture(r, lay, out, k, f):
    X0, X1, Z0, Z1 = f["xz"]
    ppm = 3000.0
    w, h = int((X1 - X0) * ppm), int((Z1 - Z0) * ppm)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    mm = ppm / 1000.0
    p = 9.0 * mm
    a = np.full((h, w), 200.0, np.float32)
    # rombi in rilievo a coppie incrociate (lamiera mandorlata)
    cx = (xx % p) - p / 2
    cy = (yy % p) - p / 2
    flip = ((np.floor(xx / p) + np.floor(yy / p)) % 2) == 1
    u = np.where(flip, cx + cy, cx - cy) / p
    v = np.where(flip, cx - cy, cx + cy) / p
    bar = (np.abs(u) < 0.48) & (np.abs(v) < 0.10)
    a = np.where(bar, 236.0, a) + rng.normal(0, 5, (h, w))
    a -= np.where(np.roll(bar, 2, axis=0) & ~bar, 46.0, 0.0)
    img = np.stack([a, a, a * 1.01], -1)
    sl = f.get("slots")
    if sl:
        fx0, fy0, fx1, fy1 = [v / 100.0 for v in sl]
        n = 18
        gw = (fx1 - fx0) * w / n
        for i in range(n):
            x0 = int(fx0 * w + i * gw + gw * 0.25)
            img[int(fy0 * h):int(fy1 * h), x0:int(x0 + gw * 0.5)] = (16, 16, 18)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(os.path.join(out, "%s_agrille%d.png" % (r["id"], k)))


# ------------------------------------------------------------------ logo, retro, pedaliera
def logo_texture(r, lay, out):
    A = lay["amp"]
    lg = A.get("logo")
    box = AL.logo_box(A, lay["W"], lay["H"], lay["z0"])
    if not lg or box is None:
        return
    _, _, wm, hm = box
    ppm = 6000.0
    W, H = int(wm * ppm), int(hm * ppm)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    font = lg.get("font") or "script"
    col = tuple(lg.get("col") or (230, 230, 230))
    txt = LOGO_TEXT.get(font, "Trinity")
    if lg.get("plate"):
        d.rounded_rectangle([0, 0, W - 1, H - 1], radius=int(H * 0.18), fill=tuple(lg.get("platecol") or (30, 30, 30)) + (255,),
                            outline=col + (255,), width=max(2, int(H * 0.05)))
        maxw, maxh = W * 0.80, H * 0.62
    else:
        maxw, maxh = W * 0.96, H * 0.90
    size = int(maxh * (1.15 if font == "script" else 0.95))
    f = ImageFont.truetype(LOGO_FONT.get(font, F_SCRIPT), size)
    while f.getlength(txt) > maxw and size > 8:
        size = int(size * 0.94)
        f = ImageFont.truetype(LOGO_FONT.get(font, F_SCRIPT), size)
    d.text((W / 2, H / 2), txt, font=f, fill=col + (255,), anchor="mm")
    img.save(os.path.join(out, "%s_alogo.png" % r["id"]))


def back_texture(r, lay, out):
    A = lay["amp"]
    b = lay["back"]
    px0, px1, py0, py1 = b["plate"]
    w, h = px1 - px0, py1 - py0
    fender = A.get("knob") in ("skirt", "pointer") and A["kind"] in ("combo",)
    col, text = ((196, 198, 200), (20, 20, 22)) if fender else ((26, 26, 28), (224, 224, 220))
    if A["kind"] == "rack":
        col, text = (36, 36, 38), (224, 224, 220)
    c = MT.Canvas(-w / 2, w / 2, -h / 2, h / 2, col)
    _panel_bg(c, "brushed" if fender else "black", col)
    fs = min(0.0030, max(0.0018, h * 0.10))
    cx, cy = (px0 + px1) / 2, (py0 + py1) / 2
    for e in b["elements"]:
        x, y = e["x"] - cx, e["y"] - cy
        if e["kind"] == "label":
            c.rect(x - 0.012, y - 0.007, x + 0.012, y + 0.007, fill=(200, 200, 196), radius_m=0.0006)
            c.text(x, y + 0.002, "SERIAL", fs * 0.7, F_LABEL, (30, 30, 30))
            continue
        ty = y + 0.0085 + fs / 2
        if ty + fs / 2 > h / 2 - 0.0015:
            ty = y - 0.0085 - fs / 2
        c.text(x, ty, e["label"], _fit(e["label"], fs * 0.8, 0.020, F_NARROW), F_NARROW, text)
    name = "%s  %s" % (r["real_name"], r["real_code"]) if r["real_code"] else r["real_name"]
    c.text(-w / 2 + 0.004, -h / 2 + fs * 0.9, name.upper(), _fit(name.upper(), fs * 0.9, w * 0.45, F_LABEL), F_LABEL, text, anchor="lm")
    c.text(w / 2 - 0.004, -h / 2 + fs * 0.9, "FabioNET  •  PEDAL TRINITY", fs * 0.75, F_LABEL, text, anchor="rm")
    _save(c, out, "%s_aback.png" % r["id"])


def console_texture(r, lay, out):
    cg = lay["console"]
    span = cg["x1"] - cg["x0"]
    slant = math.hypot(cg["y1"] - cg["y0"], cg["zb"] - cg["zf"])
    c = MT.Canvas(-span / 2, span / 2, -slant / 2, slant / 2, (34, 34, 37))
    _panel_bg(c, "black", (34, 34, 37))
    text = (226, 226, 222)
    fs = 0.0026
    import pedal_layout as PL
    ang = math.atan2(cg["zb"] - cg["zf"], cg["y1"] - cg["y0"])
    vis = math.sin(ang + math.radians(90.0 - lay["tilt"]))
    for i, p in enumerate(lay["controls"]):
        cc = r["controls"][i]
        if cc["kind"] == "inner":
            continue
        x = p["u"] * span - span / 2
        y = slant / 2 - p["v"] * slant
        lab = cc["label"]
        if p["strip"] in PL.KNOB_R:
            rr, hh = PL.KNOB_R[p["strip"]], PL.KNOB_H[p["strip"]]
            ext = (rr * math.cos(math.radians(22)) + 0.5 * hh * math.sin(math.radians(22))) / max(0.3, vis)
            if i + 1 < len(r["controls"]) and r["controls"][i + 1]["kind"] == "inner":
                ext += 0.0020
        else:
            ext = 0.0045
        c.text(x, y + ext + 0.0010 + fs / 2, lab, _fit(lab, fs, cg["pitch"] - 0.002, F_LABEL), F_LABEL, text)
    uf, vf = lay["foot_uv"]
    fx = uf * span - span / 2
    c.rect(fx - 0.0105, slant / 2 - 0.95 * slant, fx + 0.0105, slant / 2 - 0.15 * slant, fill=(18, 18, 20), radius_m=0.002)
    name = r["real_name"]
    c.text(-span / 2 + 0.006, -slant / 2 + 0.0055, name.upper(), _fit(name.upper(), fs, span * 0.55, F_LABEL), F_LABEL, (238, 200, 120), anchor="lm")
    c.text(-span / 2 + 0.006, slant / 2 - 0.0035, "CAB  •  MIC  •  PEDAL TRINITY", fs * 0.75, F_LABEL, text, anchor="lm")
    _save(c, out, "%s_aconsole.png" % r["id"])


def textures(r, lay, out):
    """Tutte le serigrafie di un ampli o di una cassa (normale o REAL MOD)."""
    os.makedirs(out, exist_ok=True)
    A = lay["amp"]
    if lay.get("panel") is not None:
        panel_texture(r, lay, out)
    for k, f in enumerate(lay["faces"]):
        if f["kind"] == "grille":
            grille_texture(r, lay, out, k, f)
        elif f["kind"] == "diamond":
            diamond_texture(r, lay, out, k, f)
    if r.get("normal"):
        logo_texture(r, lay, out)
    back_texture(r, lay, out)
    if lay.get("console"):
        console_texture(r, lay, out)
