"""
Pedal Trinity - layout di amplificatori e casse (tappa 6). Python puro: usato da amp_textures.py, render_pedals.py
(Blender), pedal_geometry.py e dai controlli di qualita'.

L'ampli (o la cassa) sta in piedi sul piano: x a destra, y verso il retro, z in alto, origine al centro dell'impronta.
La faccia frontale e' a y = -D/2. Le misure reali (mm) della scheda sono ridotte di un fattore 'scale' scelto in modo che
i pomelli disegnati dall'interfaccia (filmstrip di misura fissa) abbiano la grandezza apparente dei pomelli veri.

Inquadratura: per i pannelli sul frontale la camera e' quasi orizzontale (tilt 80 gradi dalla verticale: si vede una
striscia del cielo con la maniglia); per i pannelli sul piano superiore (AC30, Bassman tweed, B-15) la camera guarda
piu' dall'alto (tilt 38) cosi' il pannello e' leggibile. Il retro si renderizza con l'ampli girato di 180 gradi.

Formato di look["amp"] (scritto da gen_catalog_amps.py, vedi CATALOG_SPEC.md):
  kind      combo | head | cab | rack | flip           dims (W, H, D) mm reali, depth_top (casse angolate)
  scale     riduzione                                     view  front | top (posizione del pannello)
  strip     filmstrip dei pomelli funzionali              knob  stile dei pomelli decorativi (skirt, goldcap, ...)
  panel     dict(rect=(x0, y0, x1, y1) % della faccia (o del cielo), col, text, finish, recess mm, ...)
  entries   posizioni del pannello vero: dict(label, key, ch, kind, x, y) in % del pannello
            kind: knob selector toggle mini button slider jack lamp led power standby rocker fuse
  inc       canali emulati (minuscolo) per scegliere tra etichette uguali di canali diversi
  face      zone della faccia: grille / diamond / slats / plain / blank (rettangoli in % della faccia)
  covering, logo, name, corners, handle, feet, speakers, back, console (casse)
"""
import math
import re

import layout as L
import pedal_layout as PL

SIN0 = math.sin(math.radians(L.TILT_DEG))      # inclinazione con cui sono renderizzate le filmstrip dei pomelli
TILT_FRONT, TILT_TOP, TILT_BACK = 80.0, 38.0, 76.0
ROT_KINDS = ("knob", "selector")
SW_KINDS = ("toggle", "mini", "button", "rocker", "power", "standby")
FIXED_KINDS = ("jack", "lamp", "led", "power", "standby", "rocker", "fuse")

SYN = {
    "gain": ("GAIN", "VOLUME", "VOL", "PREAMP", "PRE", "DRIVE", "LOUDNESS", "INPUT"),
    "volume": ("VOLUME", "VOL", "LOUDNESS", "GAIN"),
    "drive": ("DRIVE", "GAIN", "OVERDRIVE", "OD"),
    "treble": ("TREBLE", "HIGH", "HI", "TREB", "KHZ"),
    "bass": ("BASS", "LOW", "LO", "HZ"),
    "mid": ("MIDDLE", "MID", "MIDRANGE", "MIDS"),
    "presence": ("PRESENCE", "PRES"),
    "master": ("MASTER", "OUTPUT", "MST", "VOLUME"),
    "level": ("MASTER", "OUTPUT", "LEVEL", "VOLUME", "VOL"),
    "reverb": ("REVERB", "REV"),
    "rate": ("SPEED", "RATE"),
    "speed": ("SPEED", "RATE"),
    "depth": ("INTENSITY", "DEPTH"),
    "intensity": ("INTENSITY", "DEPTH"),
    "bright": ("BRIGHT", "BRT", "BRILLIANT"),
    "cut": ("CUT", "TONE"),
    "tone": ("TONE", "CUT"),
    "resonance": ("RESONANCE", "DEPTH", "DEEP", "PUNCH"),
}


ABBREV = {"VOL": "VOLUME", "MID": "MIDDLE", "MIDRANGE": "MIDDLE", "TREB": "TREBLE", "PRES": "PRESENCE", "RHY": "RHYTHM",
          "LD": "LEAD", "NRM": "NORMAL", "NOR": "NORMAL", "OD": "OVERDRIVE", "CLN": "CLEAN", "DRV": "DRIVE", "MST": "MASTER",
          "BRT": "BRIGHT", "LVL": "LEVEL", "FREQ": "FREQ", "DPTH": "DEPTH", "SPD": "SPEED", "CR": "CRUNCH"}


def tokens(s):
    s = str(s or "").upper().replace("Ã", "A")
    s = re.sub(r"[^A-Z0-9]+", " ", s)
    out = []
    for t in s.split():
        out.append(ABBREV.get(t, t))
    return out


# comandi funzionali che riassumono commutatori del pannello vero (canale, modo, effetto...): sinonimi per trovarne il posto
FUNC_SYN = {
    "CHANNEL": {"CHANNEL", "CH", "RHYTHM", "LEAD", "OVERDRIVE", "CLEAN", "CLN", "BE", "HBE", "SELECT", "CH1", "CH2", "CH3", "CH4"},
    "MODE": {"MODE", "CLEAN", "VINTAGE", "MODERN", "ROCK", "JAZZ", "LEAD", "PULL"},
    "EFFECT": {"CHORUS", "VIBRATO", "CHO", "VIB", "EFFECT"},
    "VOICING": {"CONTOUR", "VOICING"},
    "POWER": {"SPONGY", "BOLD", "POWER"},
    "TYPE": {"MODE", "CHANNEL", "SELECT"},
}
CH_WORDS = ("I", "II", "III", "A", "B", "LEAD", "NORMAL", "CLEAN", "CRUNCH", "RHYTHM", "OVERDRIVE")


def _score(c, e, inc):
    """Somiglianza tra un comando funzionale e una posizione del pannello vero. Restituisce (punteggio, per_nome):
    per_nome = l'etichetta del comando coincide (almeno in parte) con quella stampata sul pannello vero."""
    ct = tokens(c["label"])
    if not ct:
        return 0.0, False
    et = tokens(e["label"])
    kt = tokens(e.get("key")) + tokens(e.get("ch"))
    sc = 0.0
    if " ".join(ct) == " ".join(et):
        sc += 10
    common = set(ct) & set(et + kt)
    sc += 3.0 * len(common)
    by_name = bool(set(ct) & set(et + tokens(e.get("key"))))
    # sinonimi del ruolo
    for w in SYN.get(str(c.get("role") or "").lower(), ()):
        if w in et:
            sc += 1.5
            break
    syn = False
    for w, alts in FUNC_SYN.items():
        if w in ct and not common and alts & set(et):
            sc += 4.0 + (3.0 if e["kind"] in SW_KINDS else 0.0)
            syn = True
            break
    # numeri/canale (es. "VOL 2", "CH2 GAIN", "LOUDNESS I")
    cn = {t for t in ct if t.isdigit() or t in CH_WORDS}
    en = {t for t in et + kt if t.isdigit() or t in CH_WORDS}
    roman = {"1": "I", "2": "II", "3": "III"}
    cn |= {roman.get(t, t) for t in cn}
    en |= {roman.get(t, t) for t in en}
    if cn and en and not syn:
        sc += 2.5 if cn & en else -2.5
    cht = set(tokens(e.get("ch")))
    inct = set(t for i in inc for t in tokens(i))
    if cht and inct and not syn and not cht <= {"COMUNE", "TUTTI", "FINALE", "UNICO", "NONE", "SELEZIONE"}:
        sc += 1.5 if cht & inct else -1.0
    k = c["kind"]
    ek = e["kind"]
    if k in ("knob", "selector", "outer") and ek in ROT_KINDS:
        sc += 1.0
    elif k == "toggle" and ek in SW_KINDS:
        sc += 1.0
    elif k == "slider" and ek == "slider":
        sc += 1.0
    elif k == "button" and ek in ("button", "mini", "toggle"):
        sc += 1.0
    elif (k == "selector" and ek in SW_KINDS) or (k == "toggle" and ek in ROT_KINDS) or (k == "knob" and ek == "slider"):
        sc -= 1.0
    else:
        sc -= 3.0
    return sc, by_name


class Panel:
    """Pannello dei comandi: mappa (u, v) in [0, 1] (u a destra, v in basso) sul piano del pannello."""

    def __init__(self, A, W, H, D, z0=0.0, rect=None):
        p = A["panel"]
        x0, y0, x1, y1 = [v / 100.0 for v in (rect or p["rect"])]
        self.view = A.get("view", "front")
        rec = p.get("recess", 6.0) * A["scale"] / 1000.0
        self.rec = rec
        self.X0, self.X1 = (x0 - 0.5) * W, (x1 - 0.5) * W
        if self.view == "front":
            z_top = z0 + H
            self.Z1, self.Z0 = z_top - y0 * H, z_top - y1 * H
            self.yp = -D / 2 + rec
            self.w, self.h = self.X1 - self.X0, self.Z1 - self.Z0
            self.centre = ((self.X0 + self.X1) / 2, self.yp, (self.Z0 + self.Z1) / 2)
        else:
            self.Y1, self.Y0 = D / 2 - y0 * D, D / 2 - y1 * D
            self.zp = z0 + H - rec
            self.w, self.h = self.X1 - self.X0, self.Y1 - self.Y0
            self.centre = ((self.X0 + self.X1) / 2, (self.Y0 + self.Y1) / 2, self.zp)

    def p3(self, u, v, out=0.0):
        """Punto 3D del pannello (u, v frazioni del pannello, v = 0 in alto); out = sporgenza dal piano (m)."""
        x = self.X0 + u * self.w
        if self.view == "front":
            return (x, self.yp - out, self.Z1 - v * self.h)
        return (x, self.Y1 - v * self.h, self.zp + out)

    def local(self, u, v):
        """Coordinate (m) nella texture del pannello: origine al centro, y verso l'alto del pannello."""
        return (u * self.w - self.w / 2, self.h / 2 - v * self.h)


FEET_MM = {"glides": 5.0, "rubber": 10.0, "casters": 60.0, "skids": 16.0, "none": 0.0}


def face_rect(A, rect, W, H, z0):
    """Rettangolo in % della faccia frontale -> (X0, X1, Z0, Z1) in metri."""
    x0, y0, x1, y1 = [v / 100.0 for v in rect]
    return ((x0 - 0.5) * W, (x1 - 0.5) * W, z0 + H * (1 - y1), z0 + H * (1 - y0))


def back_layout(A, W, H, D, z0):
    """Retro: piastra del telaio (o delle prese) e posizioni degli elementi in coordinate del retro (m), con
    l'origine al centro della faccia posteriore, x a destra vista da dietro, y in alto."""
    b = A.get("back") or {}
    kind = b.get("kind", "closed")
    x0, y0, x1, y1 = [v / 100.0 for v in (b.get("rect") or (4, 6, 96, 26))]
    if kind in ("vent", "open", "cage") and y1 - y0 > 0.24:
        y1 = y0 + 0.20                 # piastra del telaio; sotto si vedono le valvole
    if kind == "rack":
        x0, y0, x1, y1 = 0.04, 0.12, 0.96, 0.88
    px0, px1 = (x0 - 0.5) * W, (x1 - 0.5) * W
    py1, py0 = H / 2 - y0 * H, H / 2 - y1 * H
    pw, ph = px1 - px0, py1 - py0
    els = []
    for lab, k, ex, ey in b.get("elements") or []:
        els.append(dict(label=lab, kind=k, x=px0 + ex / 100.0 * pw, y=py1 - ey / 100.0 * ph))
    tubes = []
    for cls, nm, tx in b.get("tubes") or []:
        tubes.append(dict(cls=cls, name=nm, x=(0.04 + 0.92 * tx / 100.0 - 0.5) * W))
    return dict(kind=kind, plate=(px0, px1, py0, py1), elements=els, tubes=tubes, tube_dir=b.get("tube_dir"),
                z0=z0, H=H)


def _frame(pts, tilt, margin_x, mb=0.035, mt=0.025):
    c, s = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
    vs = [y * c + z * s for (x, y, z) in pts]
    xs = [x for (x, y, z) in pts]
    half = max(abs(min(xs)), abs(max(xs))) + margin_x
    return (-half, half, min(vs) - mb, max(vs) + mt)


def box_pts(W, D, H, z0=0.0):
    return [(sx * W / 2, sy * D / 2, z) for sx in (-1, 1) for sy in (-1, 1) for z in (z0, H)]


def console_geo(A, W, D, n):
    """Pedaliera dei comandi davanti alla cassa (casse: microfono, distanza, livello...): scatola a leggio con il
    piano inclinato verso la camera. Restituisce dict con le misure (m)."""
    pitch = 0.0255
    rows = 1 if n * pitch + 0.075 <= max(0.9 * W, 0.22) else 2
    per = int(math.ceil(n / float(rows)))
    cw = max(0.110, per * pitch + 0.075)
    # leggio ripido (circa 62 gradi): i pomelli si vedono quasi di fronte come quelli del pannello
    cd, zf, zb = (0.034, 0.014, 0.078) if rows == 1 else (0.050, 0.014, 0.110)
    yc = -D / 2 - 0.006 - cd / 2
    return dict(x0=-cw / 2, x1=cw / 2, y0=yc - cd / 2, y1=yc + cd / 2, zf=zf, zb=zb, pitch=pitch, rows=rows, per=per)


def console_p3(cg, u, v, out=0.0):
    """Punto della faccia inclinata della pedaliera: u da sinistra (0..1), v dall'alto (0 = bordo alto/lontano)."""
    x = cg["x0"] + u * (cg["x1"] - cg["x0"])
    y = cg["y1"] - v * (cg["y1"] - cg["y0"])
    z = cg["zb"] - v * (cg["zb"] - cg["zf"])
    ang = math.atan2(cg["zb"] - cg["zf"], cg["y1"] - cg["y0"])
    return (x, y - out * math.sin(ang), z + out * math.cos(ang))


def assign(r, A):
    """Assegna i comandi funzionali alle posizioni del pannello vero. Restituisce (assegnazioni, extra, relabel)
    con assegnazioni = {indice comando: indice entry}; relabel = {indice entry: etichetta funzionale} per le posizioni
    prese per sinonimo (es. MODE sul commutatore del canale): il pannello stampa allora l'etichetta del comando."""
    ctrls = r["controls"]
    entries = A.get("entries") or []
    inc = [str(i).lower() for i in (A.get("inc") or [])]
    pairs = []
    for i, c in enumerate(ctrls):
        if c["kind"] == "inner":
            continue
        for j, e in enumerate(entries):
            if e["kind"] in ("jack", "lamp", "led", "fuse"):
                continue
            sc, by_name = _score(c, e, inc)
            if e["kind"] in ("power", "standby") and sc < 10:
                continue                  # interruttori di rete: solo se il comando ha proprio quel nome
            if sc >= 3.0:
                pairs.append((sc, -abs(i - j) * 0.001, i, j, by_name))
    pairs.sort(reverse=True)
    out, used, relabel = {}, set(), {}
    for sc, _, i, j, by_name in pairs:
        if i in out or j in used:
            continue
        out[i] = j
        used.add(j)
        if not by_name:
            relabel[j] = ctrls[i]["label"]
    extra = [i for i, c in enumerate(ctrls) if c["kind"] != "inner" and i not in out]
    return out, extra, relabel


def amp_layout(r):
    lk = r["look"]
    A = lk["amp"]
    s = A["scale"]
    Wr, Hr, Dr = A["dims"]
    W, H, D = Wr * s / 1000.0, Hr * s / 1000.0, Dr * s / 1000.0
    z0 = FEET_MM.get(A.get("feet") or "none", 8.0) * s / 1000.0
    ctrls = r["controls"]
    n = len(ctrls)
    out = dict(builder="amp", style=r.get("style"), amp=A, s=s, W=W, H=H, D=D, z0=z0, controls=[None] * n,
               kinds=[c["kind"] for c in ctrls], deco=[], extra=[], led=None, foot=None, display=None, parts=[],
               warnings=[])
    strip = A.get("strip") or "ts_small"
    R, Hk = PL.KNOB_R[strip], PL.KNOB_H[strip]
    if A.get("panel"):
        out["panel"] = Panel(A, W, H, D, z0)
    if A.get("console"):
        out["tilt"] = TILT_FRONT
        cg = console_geo(A, W, D, max(1, len([c for c in ctrls if c["kind"] != "inner"])))
        out["console"] = cg
        _console_controls(r, out, cg)
        pts = box_pts(W, D, z0 + H) + [(cg["x0"], cg["y0"], 0.0), (cg["x1"], cg["y0"], 0.0)]
    else:
        P = out["panel"]
        out["tilt"] = TILT_FRONT if P.view == "front" else TILT_TOP
        _panel_controls(r, out, P, strip, R, Hk)
        pts = box_pts(W, D, z0 + H)
    top_extra = 0.030 * s / 0.62 if A.get("handle") in ("strap", "leather", "towel") else 0.0
    pts += [(0.0, 0.0, z0 + H + top_extra)]
    out["frame"] = _frame(pts, out["tilt"], max(0.03, W * 0.07))
    out["back_tilt"] = TILT_BACK
    out["back_frame"] = _frame(box_pts(W, D, z0 + H) + [(0.0, 0.0, z0 + H + top_extra)], TILT_BACK, max(0.03, W * 0.07))
    out["back"] = back_layout(A, W, H, D, z0)
    out["faces"] = [dict(f, xz=face_rect(A, f["rect"], W, H, z0)) for f in (A.get("face") or [])]
    out["body"] = dict(W=W, D=D, H=z0 + H)
    P = out.get("panel")
    zj = (P.Z0 + P.Z1) / 2 if (P is not None and P.view == "front" and not A.get("console")) else z0 + H * 0.6
    out["jacks"] = (W, [(-D / 2 + 0.012, zj), None])
    return out


def _console_controls(r, out, cg):
    ctrls = r["controls"]
    rot = [i for i, c in enumerate(ctrls) if c["kind"] != "inner"]
    n = len(rot)
    span = cg["x1"] - cg["x0"]
    # comandi da sinistra, footswitch (zona foot) e LED a destra
    for k, i in enumerate(rot):
        c = ctrls[i]
        row, col = (0, k) if cg["rows"] == 1 else (k // cg["per"], k % cg["per"])
        u = (0.030 + cg["pitch"] * (col + 0.5)) / span
        v = 0.60 if cg["rows"] == 1 else (0.36 if row == 0 else 0.80)
        c3 = console_p3(cg, u, v)
        if c["kind"] == "toggle":
            out["controls"][i] = dict(strip="toggle", x=c3[0], y=c3[1], z=c3[2], c3=c3, amp=True, u=u, v=v)
        elif c["kind"] == "slider":
            out["controls"][i] = dict(strip="slider", x=c3[0], y=c3[1], z=c3[2], c3=console_p3(cg, u, 0.85),
                                      t3=console_p3(cg, u, 0.25), r=0.004, amp=True, u=u, v=v)
        elif c["kind"] == "button":
            out["controls"][i] = dict(strip="button", x=c3[0], y=c3[1], z=c3[2], c3=c3, r=0.004, amp=True, u=u, v=v)
        else:
            st = "boss_outer" if c["kind"] == "outer" else "boss"
            out["controls"][i] = dict(strip=st, x=c3[0], y=c3[1], z=c3[2], c3=c3, r=PL.KNOB_R[st], h=PL.KNOB_H[st],
                                      amp=True, u=u, v=v)
            if c["kind"] == "outer" and i + 1 < len(ctrls) and ctrls[i + 1]["kind"] == "inner":
                out["controls"][i + 1] = dict(strip="boss_inner", x=c3[0], y=c3[1], z=c3[2], c3=c3,
                                              r=PL.KNOB_R["boss_inner"], h=PL.KNOB_H["boss_inner"], amp=True,
                                              inner_of=PL.KNOB_H["boss_outer"], u=u, v=v)
    uf = 1.0 - 0.022 / span
    q = 0.0105
    out["foot_uv"] = (uf, 0.55)
    out["foot"] = [console_p3(cg, uf - q / span, 0.15), console_p3(cg, uf + q / span, 0.15),
        console_p3(cg, uf + q / span, 0.95), console_p3(cg, uf - q / span, 0.95)]
    lu = 1.0 - 0.022 / span
    l3 = console_p3(cg, lu, 0.06, out=-0.0022)
    out["led"] = (l3[0], l3[1], l3[2], 0.0018)
    out["led_uv"] = (lu, 0.06)


def _panel_controls(r, out, P, strip, R, Hk):
    A = out["amp"]
    ctrls = r["controls"]
    entries = A.get("entries") or []
    asg, extra, relabel = assign(r, A)
    out["relabel"] = relabel
    used = set(asg.values())
    # posizioni occupate (per i comandi senza corrispondenza)
    occ = [(e["x"] / 100.0, e["y"] / 100.0, e["kind"]) for e in entries]
    clear = (R * math.cos(math.radians(22)) + 0.5 * Hk * SIN0) / max(0.3, math.sin(math.radians(TILT_FRONT if P.view == "front" else 90 - TILT_TOP)))
    for i in extra:
        kd = ctrls[i]["kind"]
        kd = "knob" if kd in ("knob", "selector", "outer") else ("slider" if kd == "slider" else "toggle")
        spot = _free_spot(P, occ, entries, 0.0, kd, R, clear)
        if spot is None:
            # nessuno spazio libero: prende il posto di un comando fisso (senza funzione) che abbia spazio intorno
            j = _takeover(P, entries, used, kd, R, clear)
            if j is not None:
                asg[i] = j
                used.add(j)
                out["relabel"][j] = ctrls[i]["label"]
                continue
            # selettore dell'ingresso (canale I/II/ponticellati): al posto di una coppia di prese d'ingresso
            pair = _jack_pair(P, entries, used, kd, R, clear) if "INPUT" in tokens(ctrls[i]["label"]) else None
            if pair is not None:
                (u, v), js = pair
                for j in js:
                    used.add(j)
                    out.setdefault("hidden", []).append(j)
                out["extra"].append(dict(ctrl=i, x=u * 100, y=v * 100, label=ctrls[i]["label"]))
                occ.append((u, v, kd))
                continue
            out["warnings"].append("%s: nessuna posizione libera per %s" % (r["id"], ctrls[i]["label"]))
            spot = (0.5, 0.5)
        occ.append((spot[0], spot[1], kd))
        out["extra"].append(dict(ctrl=i, x=spot[0] * 100, y=spot[1] * 100, label=ctrls[i]["label"]))
    pos = {}
    for i, j in asg.items():
        pos[i] = (entries[j]["x"] / 100.0, entries[j]["y"] / 100.0)
    for ex in out["extra"]:
        pos[ex["ctrl"]] = (ex["x"] / 100.0, ex["y"] / 100.0)
    for i, j in asg.items():
        pos[i] = (entries[j]["x"] / 100.0, entries[j]["y"] / 100.0)
    for i, c in enumerate(ctrls):
        if c["kind"] == "inner":
            continue
        u, v = pos[i]
        c3 = P.p3(u, v)
        base = dict(x=c3[0], y=c3[1], z=c3[2], c3=c3, amp=True, u=u, v=v, entry=asg.get(i))
        if c["kind"] == "toggle":
            out["controls"][i] = dict(base, strip="toggle")
        elif c["kind"] == "button":
            out["controls"][i] = dict(base, strip="button", r=0.0042)
        elif c["kind"] == "slider":
            dv = 0.30 * min(1.0, 0.040 / max(1e-6, P.h))
            out["controls"][i] = dict(base, strip="slider", c3=P.p3(u, min(0.92, v + dv)), t3=P.p3(u, max(0.08, v - dv)), r=0.004)
        elif c["kind"] == "outer":
            out["controls"][i] = dict(base, strip="boss_outer", r=PL.KNOB_R["boss_outer"], h=PL.KNOB_H["boss_outer"])
            if i + 1 < len(ctrls) and ctrls[i + 1]["kind"] == "inner":
                out["controls"][i + 1] = dict(base, strip="boss_inner", r=PL.KNOB_R["boss_inner"],
                                              h=PL.KNOB_H["boss_inner"], inner_of=PL.KNOB_H["boss_outer"])
        else:
            out["controls"][i] = dict(base, strip=strip, r=R, h=Hk)
    for i, c in enumerate(ctrls):
        if out["controls"][i] is None:          # inner orfano
            out["controls"][i] = dict(out["controls"][max(0, i - 1)])
    # posizioni del pannello senza comando funzionale: comandi fissi (modellati nel render)
    for j, e in enumerate(entries):
        if j in used:
            continue
        u, v = e["x"] / 100.0, e["y"] / 100.0
        out["deco"].append(dict(e, u=u, v=v, c3=P.p3(u, v)))
    # LED (spia) e zona del footswitch (interruttore di standby o di accensione)
    lamp = next((e for e in entries if e["kind"] == "lamp"), None) or next((e for e in entries if e["kind"] == "led"), None)
    if lamp is not None:
        u, v = lamp["x"] / 100.0, lamp["y"] / 100.0
        rr = 0.0042 if lamp["kind"] == "lamp" else 0.0020
    else:
        u, v = _free_spot(P, occ, entries, 0.0, "led", R, clear) or (0.97, 0.5)
        rr = 0.0020
        out["deco"].append(dict(label="LED", key="LED", ch=None, kind="led", x=u * 100, y=v * 100, u=u, v=v, c3=P.p3(u, v)))
    l3 = P.p3(u, v, out=0.004 if rr > 0.003 else 0.0015)
    # proj aggiunge 2.2 mm in z al LED: compensato per i pannelli frontali
    if P.view == "front":
        l3 = (l3[0], l3[1], l3[2] - 0.0022)
    out["led"] = (l3[0], l3[1], l3[2], rr * 0.85)
    out["led_uv"] = (u, v)
    sw = (next((e for e in entries if e["kind"] == "standby"), None) or next((e for e in entries if e["kind"] == "power"), None)
          or next((e for e in entries if e["kind"] == "rocker"), None))
    if sw is not None:
        u, v = sw["x"] / 100.0, sw["y"] / 100.0
    else:
        u, v = out["led_uv"]
    q = 0.0085
    sx_, sy_ = P.local(u, v)
    for p_ in out["controls"]:
        if p_ is None or "u" not in p_:
            continue
        cx_, cy_ = P.local(p_["u"], p_["v"])
        rr_ = (p_.get("r") or 0.0035) + 0.0035
        if abs(cx_ - sx_) < q + rr_ and abs(cy_ - sy_) < q + rr_ * 1.6:
            q = max(0.0030, min(q, abs(cx_ - sx_) - rr_))
    du, dv = q / P.w, q / P.h
    out["foot"] = [P.p3(u - du, v - dv), P.p3(u + du, v - dv), P.p3(u + du, v + dv), P.p3(u - du, v + dv)]
    out["foot_uv"] = (u, v)


def footprint(P, kind, u, v, R, clear, fs=0.0034):
    """Ingombro (m, coordinate del pannello: x a destra, y in alto) di un comando con la sua etichetta sopra."""
    x, y = P.local(u, v)
    if kind in ("knob", "selector", "outer"):
        return (x - R - 0.002, y - clear - 0.001, x + R + 0.002, y + clear + fs + 0.003)
    if kind == "slider":
        return (x - 0.004, y - 0.014, x + 0.004, y + 0.018)
    if kind in ("lamp", "led"):
        return (x - 0.005, y - 0.005, x + 0.005, y + 0.005)
    return (x - 0.0065, y - 0.0055, x + 0.0065, y + 0.006 + fs + 0.002)


def _takeover(P, entries, used, kind, R, clear):
    """Posizione decorativa (commutatore o pomello senza funzione) dove il comando sta senza toccare gli altri."""
    pref = ("toggle", "mini", "button", "selector", "knob") if kind == "toggle" else ("selector", "knob", "toggle", "mini", "button")
    for k in pref:
        for j, e in enumerate(entries):
            if j in used or e["kind"] != k:
                continue
            f = footprint(P, kind, e["x"] / 100.0, e["y"] / 100.0, R, clear)
            others = [footprint(P, o["kind"], o["x"] / 100.0, o["y"] / 100.0, R, clear) for jj, o in enumerate(entries) if jj != j]
            if not any(f[0] < b[2] and b[0] < f[2] and f[1] < b[3] and b[1] < f[3] for b in others):
                return j
    return None


def _jack_pair(P, entries, used, kind, R, clear):
    """Coppia di prese d'ingresso vicine (es. I High / I Low) al cui centro sta il comando senza toccare il resto."""
    jacks = [j for j, e in enumerate(entries) if e["kind"] == "jack" and j not in used]
    for a in jacks:
        for b in jacks:
            if b <= a or abs(entries[a]["y"] - entries[b]["y"]) > 1 or abs(entries[a]["x"] - entries[b]["x"]) > 5:
                continue
            u = (entries[a]["x"] + entries[b]["x"]) / 200.0
            v = entries[a]["y"] / 100.0
            f = footprint(P, kind, u, v, R, clear)
            others = [footprint(P, o["kind"], o["x"] / 100.0, o["y"] / 100.0, R, clear) for jj, o in enumerate(entries) if jj not in (a, b)]
            inside = f[0] >= -P.w / 2 and f[2] <= P.w / 2 and f[1] >= -P.h / 2 and f[3] <= P.h / 2
            if inside and not any(f[0] < q[2] and q[0] < f[2] and f[1] < q[3] and q[1] < f[3] for q in others):
                return (u, v), (a, b)
    return None


def _free_spot(P, occ, entries, gap, kind="knob", R=0.0076, clear=0.0098):
    """Posizione libera sul pannello per un comando senza corrispondenza nel pannello vero: nessuna sovrapposizione
    con gli ingombri (comando + etichetta) delle posizioni esistenti, dentro il pannello; la piu' vicina al centro
    della fila dei pomelli (poi delle altre file e della meta' altezza)."""
    boxes = [footprint(P, k, a, b, R, clear) for (a, b, k) in occ]
    rows = []
    for e in entries:
        if e["kind"] in ROT_KINDS and e["y"] / 100.0 not in rows:
            rows.append(e["y"] / 100.0)
    rows = (rows or [0.5]) + [0.5, 0.3, 0.7]
    best = None
    for ri, vr in enumerate(rows):
        for k in range(3, 98):
            u = k / 100.0
            f = footprint(P, kind, u, vr, R, clear)
            if f[0] < -P.w / 2 + 0.002 or f[2] > P.w / 2 - 0.002 or f[1] < -P.h / 2 + 0.0015 or f[3] > P.h / 2 - 0.0015:
                continue
            if any(f[0] < b[2] and b[0] < f[2] and f[1] < b[3] and b[1] < f[3] for b in boxes):
                continue
            cand = (abs(u - 0.55) + 0.3 * ri, (u, vr))
            if best is None or cand[0] < best[0]:
                best = cand
        if best is not None:
            return best[1]
    return None


LOGO_FONTS = ("script", "block", "round", "gothic")


def logo_box(A, W, H, z0):
    """Targhetta del logo sulla faccia (solo modalita' normale): centro (x, z) e misure (w, h) in metri."""
    lg = A.get("logo")
    if not lg:
        return None
    big = lg.get("big")
    w = W * (0.36 if big else (0.22 if lg.get("font") == "script" else 0.20))
    if lg.get("plate"):
        w = min(w, W * 0.24)
    h = w * (0.30 if lg.get("font") == "script" else 0.24)
    if lg.get("plate"):
        h = w * 0.26
    x = (lg["x"] / 100.0 - 0.5) * W
    z = z0 + H * (1 - lg["y"] / 100.0)
    x = max(-W / 2 + w / 2 + 0.006, min(W / 2 - w / 2 - 0.006, x))
    return (x, z, w, h)
