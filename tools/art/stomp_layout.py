"""
Pedal Trinity - pedali "a scatola" con la forma del pedale vero (Python puro): contenitori MXR standard e
grande, Electro-Harmonix Nano, XO, XO grande, Pico, scatole storiche in lamiera piegata a cuneo ("pie plate"),
scatole grandi da tavolo, serie a valvole. Usato sia in modalita' NORMALE (stile "box" con look["shape"] di
questo modulo) sia dalle repliche REAL MOD (serie "stomp").

look (dizionario del catalogo):
  shape    una di SHAPES (proporzioni, raggi, finitura e pomelli tipici della famiglia di contenitori)
  dims     (larghezza, profondita', altezza) in mm del contenitore vero; wedge=(h davanti, h dietro) per le
           scatole a cuneo
  place    [(etichetta, x%, y%, tipo), ...] posizione dei comandi del pedale vero sul piano visto dall'alto
           (x 0 = bordo sinistro .. 100 = destro, y 0 = bordo lontano .. 100 = vicino al chitarrista);
           tipo: knob (pomello tipico del contenitore), big, med, small, lever (levetta), button (pulsante),
           slider (cursore), encoder (pomello grande dei preset)
  feet     [(etichetta, x%, y%), ...] footswitch; il primo e' quello dell'effetto (zona "foot" dell'interfaccia)
  leds     [(x%, y%, colore), ...] il primo e' il LED di stato acceso dall'interfaccia
  sections True: tutti i footswitch sono di sezione (levette del modello, anche il primo); l'effetto si inserisce
           cliccando il LED di stato
  extra    elementi solo decorativi: ("tubes", x%, y%, n), ("display", x0%, y0%, x1%, y1%), ("ledbar", x%, y%, n, "v"|"h")
I comandi del modello si assegnano alle posizioni per etichetta (poi per tipo, nell'ordine); le posizioni
del pedale vero senza un comando del modello diventano comandi decorativi (fissi) e i comandi del modello
senza posizione vanno nello spazio libero. Le sovrapposizioni si risolvono spostando i comandi il meno possibile.
"""
import math

import pedal_layout as PL

TAN22 = math.tan(math.radians(22.0))
MARGIN = 0.0035                     # dal bordo del piano stampabile (oltre il raggio di raccordo)

# raggio e altezza (m) dei comandi per tipo e filmstrip dell'interfaccia
KNOBS = {"big": ("ts_big", 0.0096, 0.0160), "knob": ("ts_small", 0.0076, 0.0145), "med": ("boss", 0.0073, 0.0150),
         "small": ("boss_small", 0.0049, 0.0075), "encoder": ("ts_big", 0.0096, 0.0160)}
TOGGLE_R, TOGGLE_H = 0.0040, 0.0110
BUTTON_R = 0.0045
SLIDER_LEN = 0.024
FOOT_R = 0.0068                    # cappuccio del footswitch tondo (m)
FOOT_ZONE = 0.0105                 # meta' lato della zona cliccabile del footswitch
LED_R = 0.0021

# famiglie di contenitori: raggio in pianta, raccordo dei bordi superiori, finitura tipica, pomello tipico
SHAPES = {
    "mxr":      dict(corner=0.0060, edge=0.0045, knob="med", kind="die", label="below"),
    "mxr_big":  dict(corner=0.0070, edge=0.0050, knob="med", kind="die", label="below"),
    "nano":     dict(corner=0.0075, edge=0.0055, knob="knob", kind="die", label="below"),
    "pico":     dict(corner=0.0060, edge=0.0045, knob="small", kind="die", label="below"),
    "xo":       dict(corner=0.0085, edge=0.0060, knob="knob", kind="die", label="below"),
    "xo_wide":  dict(corner=0.0085, edge=0.0060, knob="knob", kind="die", label="below"),
    "pie":      dict(corner=0.0030, edge=0.0016, knob="big", kind="sheet", label="below"),
    "slab":     dict(corner=0.0030, edge=0.0016, knob="knob", kind="sheet", label="below"),
    "tank":     dict(corner=0.0050, edge=0.0025, knob="big", kind="tank", label="below"),
    "tube":     dict(corner=0.0040, edge=0.0020, knob="knob", kind="sheet", label="below"),
}
ROTARY = ("knob", "big", "med", "small", "encoder")
SWITCHES = ("lever", "button")


def key(s):
    return "".join(ch for ch in str(s).upper() if ch.isalnum())


def shape_params(lk):
    P = dict(SHAPES[lk["shape"]])
    P.update(lk.get("shape_opts") or {})
    return P


class Geo:
    """Conversioni tra percentuali del pedale vero e metri, quota del piano (cuneo)."""

    def __init__(self, W, D, zf, zb):
        self.W, self.D, self.zf, self.zb = W, D, zf, zb

    def x(self, pct):
        return (float(pct) / 100.0 - 0.5) * self.W

    def y(self, pct):
        return (0.5 - float(pct) / 100.0) * self.D

    def z(self, y):
        t = (y + self.D / 2) / self.D
        return self.zf + t * (self.zb - self.zf)


def _ctrl_class(c):
    k = c["kind"]
    if k in ("knob", "outer", "selector"):
        return "rot"
    if k == "inner":
        return "inner"
    if k == "slider":
        return "slider"
    if k == "button":
        return "button"
    return "switch"              # toggle


def _spec_class(kind):
    if kind in ROTARY:
        return "rot"
    if kind == "slider":
        return "slider"
    return "switch"


GENERIC = {"LEVEL", "LVL", "MODE", "KNOB", "MINI", "THE", "AND"}


def _tokens(s):
    t = [w for w in "".join(ch if ch.isalnum() else " " for ch in str(s).upper()).split()]
    return t


def _score(label, e):
    """Somiglianza tra un'etichetta del modello e una posizione del pedale vero (etichetta e alias)."""
    k = key(label)
    names = [e[0]] + list(e[4] if len(e) > 4 and e[4] else [])
    best = 0
    tl = _tokens(label)
    for n in names:
        ek = key(n)
        if ek == k:
            sc = 4
        elif len(k) >= 3 and len(ek) >= 3 and (ek.startswith(k) or k.startswith(ek)):
            sc = 3
        else:
            te = _tokens(n)
            common = [w for w in tl if w in te and (len(w) >= 2 or w.isdigit()) and w not in GENERIC]
            sc = 2 if common else 0
        best = max(best, sc)
    return best


def _match_all(ctrls, entries):
    """Assegnazione globale per etichetta: prima le coppie piu' simili, a parita' quelle dello stesso tipo."""
    pairs = []
    for i, c in enumerate(ctrls):
        if c["kind"] == "inner":
            continue
        cc = _ctrl_class(c)
        for j, e in enumerate(entries):
            sc = _score(c["label"], e)
            if sc <= 0:
                continue
            ec = _spec_class(e[3])
            if (cc == "slider") != (ec == "slider"):
                continue
            same = (cc == ec) or (cc == "button" and ec == "switch")
            pairs.append((sc + (0.5 if same else 0.0), -abs(i - j) * 0.001, i, j))
    pairs.sort(reverse=True)
    out, used_i, used_j = {}, set(), set()
    for sc, _, i, j in pairs:
        if i in used_i or j in used_j:
            continue
        out[i] = j
        used_i.add(i)
        used_j.add(j)
    return out


def footprint(item, other=None):
    """Ingombro sul piano: capsula verticale (x, y0, y1, r) che comprende la sommita' proiettata e la scritta.
    Il footswitch occupa la sua zona cliccabile per i comandi, solo il dado per i LED."""
    t = item["t"]
    x, y = item["x"], item["y"]
    if t == "rot":
        r, h = item["r"], item["h"]
        lab = 0.0058 if item.get("label") and other is not None and other["t"] != "led" else 0.0
        return (x, y - lab, y + h * TAN22, r + 0.0010)
    if t == "toggle":
        return (x, y - 0.0045, y + TOGGLE_H * TAN22 * 0.6, 0.0050)
    if t == "button":
        return (x, y - 0.0045, y + 0.0010, BUTTON_R + 0.0012)
    if t == "slider":
        return (x, item["y0"] - 0.0020, item["y1"] + 0.0060, 0.0040)
    if t == "led":
        return (x, y, y + 0.0010, 0.0028 if other is not None and other["t"] == "foot" else 0.0030)
    if t == "foot":
        if other is not None and other["t"] == "led":
            return (x, y, y, 0.0074)
        if other is not None and other["t"] == "foot":
            return (x, y, y + 0.0045, 0.0080)
        return (x, y, y + 0.0040, FOOT_ZONE + 0.0006)
    if t == "zone":
        return (x, y, y, item["r"] + 0.0005)
    if t == "tube1":
        return (x, y - 0.006, y + 0.006 + item["h"] * TAN22, 0.0130)
    if t == "tubes":
        return (x, y, y, 0.0)
    return (x, y, y, 0.004)


def _seg_dist(a, b):
    """Distanza tra due capsule verticali (x, y0, y1, r) e direzione di separazione."""
    dx = b[0] - a[0]
    if b[1] > a[2]:
        dy = b[1] - a[2]
    elif a[1] > b[2]:
        dy = b[2] - a[1]
    else:
        dy = 0.0
    return math.hypot(dx, dy), dx, dy


def _bounds(item, W, D):
    """Limiti del centro dell'elemento: tutta la sagoma (sommita' proiettata compresa) dentro il piano."""
    x, y0, y1, r = footprint(item)
    if item["t"] == "rot":
        r = item["r"] + 0.0004
        y0 = item["y"]
    return (-W / 2 + MARGIN + r, W / 2 - MARGIN - r, -D / 2 + MARGIN + r + (item["y"] - y0),
            D / 2 - MARGIN - r - (y1 - item["y"]))


def resolve(items, W, D, warnings, rid):
    """Allontana i comandi che si toccano (i fissi non si muovono), poi li riporta dentro il piano."""
    gap = 0.0006
    for it in items:
        it["x0"], it["y0_"] = it["x"], it["y"]
    for _ in range(400):
        moved = False
        for it in items:
            if it.get("fixed"):
                continue
            bx0, bx1, by0, by1 = _bounds(it, W, D)
            nx = min(max(it["x"], bx0), bx1) if bx0 <= bx1 else 0.0
            ny = min(max(it["y"], by0), by1) if by0 <= by1 else (by0 + by1) / 2
            if abs(nx - it["x"]) > 1e-7 or abs(ny - it["y"]) > 1e-7:
                _shift(it, nx - it["x"], ny - it["y"])
                moved = True
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                a, b = items[i], items[j]
                if (a.get("fixed") and b.get("fixed")) or a.get("skip") or b.get("skip"):
                    continue
                if (a["t"] == "zone" or b["t"] == "zone") and "led" not in (a["t"], b["t"]):
                    continue
                if a.get("pair") is not None and a.get("pair") == b.get("pair"):
                    continue
                fa, fb = footprint(a, b), footprint(b, a)
                d, dx, dy = _seg_dist(fa, fb)
                need = fa[3] + fb[3] + gap
                if d >= need:
                    continue
                push = need - d + 1e-5
                if d < 1e-6:
                    dx, dy, d = (1.0 if b["x"] >= a["x"] else -1.0), 0.0, 1.0
                ux, uy = dx / d, dy / d
                # nelle file (stessa altezza) si scorre in orizzontale
                if abs(uy) < 0.35:
                    ux, uy = (1.0 if ux >= 0 else -1.0), 0.0
                wa = 0.0 if a.get("fixed") else (0.5 if not b.get("fixed") else 1.0)
                wb = 0.0 if b.get("fixed") else (0.5 if not a.get("fixed") else 1.0)
                _shift(a, -ux * push * wa, -uy * push * wa)
                _shift(b, ux * push * wb, uy * push * wb)
                moved = True
        if not moved:
            break
    for it in items:
        if it.get("fixed"):
            continue
        dd = math.hypot(it["x"] - it["x0"], it["y"] - it["y0_"])
        if dd > 0.0015:
            warnings.append("%s: %s spostato di %.1f mm" % (rid, it.get("label") or it["t"], dd * 1000))
    # controllo finale delle sovrapposizioni rimaste
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, b = items[i], items[j]
            if a.get("skip") or b.get("skip") or (a.get("fixed") and b.get("fixed")):
                continue
            if (a["t"] == "zone" or b["t"] == "zone") and "led" not in (a["t"], b["t"]):
                continue
            fa, fb = footprint(a, b), footprint(b, a)
            d, _, _ = _seg_dist(fa, fb)
            if d < fa[3] + fb[3] - 0.0008:
                warnings.append("%s: %s e %s ancora vicini (%.1f mm)" % (rid, a.get("label") or a["t"], b.get("label") or b["t"],
                                                                       (fa[3] + fb[3] - d) * 1000))


def _shift(it, dx, dy):
    it["x"] += dx
    it["y"] += dy
    if it["t"] == "slider":
        it["y0"] += dy
        it["y1"] += dy


def stomp_layout(r):
    lk = r["look"]
    P = shape_params(lk)
    W, D, H = r["dims"]
    wedge = lk.get("wedge")
    zf, zb = ((wedge[0] / 1000.0, wedge[1] / 1000.0) if wedge else (H, H))
    G = Geo(W, D, zf, zb)
    ctrls = r["controls"]
    rid = r["id"]
    warnings = []
    entries = [tuple(e) for e in (lk.get("place") or [])]
    feet = [tuple(f) for f in (lk.get("feet") or [("ON/OFF", 50, 80)])]
    default_knob = lk.get("knob") or P["knob"]

    # ---- assegnazione dei comandi alle posizioni del pedale vero
    assign = [None] * len(ctrls)            # indice di entries, ("foot", j) o None
    used, used_feet = set(), set()
    # pulsanti / levette che sono footswitch del pedale vero (prima delle posizioni del pannello)
    for i, c in enumerate(ctrls):
        if c["kind"] not in ("toggle", "button"):
            continue
        for j, f in enumerate(feet):
            if (j == 0 and r.get("family") != "Looper" and not lk.get("sections")) or j in used_feet:
                continue
            ck = key(c["label"])
            names = [key(f[0])] + [key(a) for a in (f[3] if len(f) > 3 and f[3] else [])]
            if ck in names or (len(ck) >= 2 and names[0].startswith(ck)):
                assign[i] = ("foot", j)
                used_feet.add(j)
                break
    rest = [c if assign[i] is None else dict(c, kind="inner") for i, c in enumerate(ctrls)]
    for i, j in _match_all(rest, entries).items():
        assign[i] = j
        used.add(j)
    # poi per tipo, nell'ordine
    for i, c in enumerate(ctrls):
        if assign[i] is not None or c["kind"] == "inner":
            continue
        cls = _ctrl_class(c)
        want = {"rot": ("rot", "switch"), "switch": ("switch", "rot"), "button": ("switch",), "slider": ("slider",)}[cls]
        for w in want:
            for j, e in enumerate(entries):
                if j not in used and _spec_class(e[3]) == w:
                    assign[i] = j
                    used.add(j)
                    break
            if assign[i] is not None:
                break
    # bottoni del looper senza posizione: sui footswitch liberi
    for i, c in enumerate(ctrls):
        if assign[i] is None and c["kind"] in ("button", "toggle"):
            for j, f in enumerate(feet):
                if j > 0 and j not in used_feet:
                    assign[i] = ("foot", j)
                    used_feet.add(j)
                    break

    # ---- elementi del piano
    items = []
    out = dict(style=r.get("style"), builder="stomp", shape=lk["shape"], controls=[None] * len(ctrls), led=None,
               toggle=None, display=None, sliders=None, buttons=[], panel_y0=-D / 2, tread_y1=-D / 2, parts=[],
               kinds=[None] * len(ctrls), display_z=zb)
    pair_id = 0
    for i, c in enumerate(ctrls):
        a = assign[i]
        if c["kind"] == "inner" or (isinstance(a, tuple)):
            continue
        if a is None:
            items.append(dict(t=_item_type(c, None), i=i, label=c["label"], x=0.0, y=0.0, auto=True, ctl=c))
            continue
        e = entries[a]
        x, y = G.x(e[1]), G.y(e[2])
        it = dict(i=i, label=c["label"], x=x, y=y, ctl=c, spec=e[3])
        it["t"] = _item_type(c, e[3])
        items.append(it)
    # concentrici: l'inner segue il suo outer
    for it in list(items):
        i = it.get("i")
        if i is not None and ctrls[i]["kind"] == "outer" and i + 1 < len(ctrls) and ctrls[i + 1]["kind"] == "inner":
            it["inner"] = i + 1
    # posizioni del pedale vero senza comando: decorative
    for j, e in enumerate(entries):
        if j in used:
            continue
        t = {"slider": "slider", "lever": "toggle", "button": "button"}.get(e[3], "rot")
        items.append(dict(t=t, i=None, label=e[0], x=G.x(e[1]), y=G.y(e[2]), deco=True, spec=e[3]))
    for it in items:
        _size(it, default_knob)
    # footswitch
    for j, f in enumerate(feet):
        items.append(dict(t="foot", i=None, label=f[0], x=G.x(f[1]), y=G.y(f[2]), fixed=True, foot=j))
    # LED
    leds = [tuple(l) for l in (lk.get("leds") or [])]
    for k, l in enumerate(leds):
        items.append(dict(t="led", i=None, label="", x=G.x(l[0]), y=G.y(l[1]), colour=l[2] if len(l) > 2 else "red",
                          main=(k == 0), fixed=bool(lk.get("fixed_leds"))))
    # elementi decorativi grandi
    for ex in lk.get("extra") or []:
        if ex[0] == "tubes":
            # valvole con la gabbia: un ingombro per valvola (le capsule non descrivono bene un rettangolo largo)
            n = ex[3] if len(ex) > 3 else 2
            x0, y0 = G.x(ex[1]), G.y(ex[2])
            out_t = dict(type="tubes", x=x0, y=y0, n=n, w=0.024 * n + 0.010, d=0.034, h=0.030)
            items.append(dict(t="tubes", i=None, label="", x=x0, y=y0, part=out_t, fixed=True, skip=True))
            for q in range(n):
                items.append(dict(t="tube1", i=None, label="", x=x0 + (q - (n - 1) / 2) * 0.024, y=y0, h=0.030, fixed=True))
    # zona del nome stampato (se la grafica ne fissa l'altezza): allontana solo i LED
    g = lk.get("gfx") or {}
    for key_, dflt in (("name", 7.0),):
        e = g.get(key_)
        if isinstance(e, dict) and e.get("y") is not None:
            hz = e.get("size", dflt) / 1000.0 * 0.5
            xc = G.x(e.get("x", 50))
            n = 7
            for q in range(n):
                xx = xc + (q - (n - 1) / 2) / (n - 1) * W * 0.70
                if abs(xx) < W / 2 - 0.004:
                    items.append(dict(t="zone", i=None, label="", x=xx, y=G.y(e["y"]), r=hz, fixed=True))
    # comandi senza posizione: nella zona libera (dall'alto, da sinistra)
    for it in items:
        if it.get("auto"):
            best = None
            for yy in [D / 2 - 0.016 - 0.006 * k for k in range(int((D - 0.04) / 0.006))]:
                for xx in [-W / 2 + 0.012 + 0.006 * k for k in range(int((W - 0.024) / 0.006) + 1)]:
                    _place(it, xx, yy)
                    ok = all(_seg_dist(footprint(it, o), footprint(o, it))[0] >= footprint(it, o)[3] + footprint(o, it)[3] + 0.001
                             for o in items if o is not it and not o.get("auto") and not o.get("skip") and o["t"] != "zone")
                    bx0, bx1, by0, by1 = _bounds(it, W, D)
                    ok = ok and bx0 <= xx <= bx1 and by0 <= yy <= by1
                    if ok:
                        best = (xx, yy)
                        break
                if best:
                    break
            _place(it, *(best or (0.0, 0.0)))
            it.pop("auto")
            warnings.append("%s: %s senza posizione nella scheda (messo nello spazio libero)" % (rid, it["label"]))
    resolve(items, W, D, warnings, rid)

    # ---- uscita
    def z_at(y):
        return G.z(y)

    for it in items:
        t, x, y = it["t"], it["x"], it["y"]
        z = z_at(y)
        i = it.get("i")
        if t == "foot":
            continue
        if t == "led":
            out["parts"].append(dict(type="led", x=x, y=y, z=z, colour=it["colour"]))
            if it["main"] or out["led"] is None:
                out["led"] = (x, y, z, 0.0020)
            continue
        if t == "tubes":
            out["parts"].append(dict(it["part"], z=z))
            continue
        if t in ("tube1", "zone"):
            continue
        if it.get("deco"):
            if t == "rot":
                out["parts"].append(dict(type="knob", x=x, y=y, z=z, r=it["r"], h=it["h"], label=it["label"], spec=it["spec"]))
            elif t == "toggle":
                out["parts"].append(dict(type="switch", x=x, y=y, z=z, label=it["label"]))
            elif t == "button":
                out["parts"].append(dict(type="button", x=x, y=y, z=z, r=BUTTON_R, label=it["label"]))
            elif t == "slider":
                out["parts"].append(dict(type="slider", x=x, y0=it["y0"], y1=it["y1"], z=z, label=it["label"]))
            continue
        c = ctrls[i]
        if t == "rot":
            if it.get("inner") is not None:
                out["controls"][i] = dict(x=x, y=y, z=z, strip="boss_outer", r=PL.KNOB_R["boss_outer"], h=PL.KNOB_H["boss_outer"])
                j = it["inner"]
                out["controls"][j] = dict(x=x, y=y, z=z + PL.KNOB_H["boss_outer"], strip="boss_inner",
                                          r=PL.KNOB_R["boss_inner"], h=PL.KNOB_H["boss_inner"])
                out["kinds"][j] = "inner"
            else:
                out["controls"][i] = dict(x=x, y=y, z=z, strip=it["strip"], r=it["r"], h=it["h"])
        elif t == "toggle":
            out["controls"][i] = dict(x=x, y=y, z=z, strip="toggle")
        elif t == "button":
            out["controls"][i] = dict(x=x, y=y, z=z + 0.003, strip="button", r=BUTTON_R, part="button")
        elif t == "slider":
            out["controls"][i] = dict(x=x, y=it["y0"], z=z_at(it["y0"]), y1=it["y1"], strip="slider", r=0.004)
        out["kinds"][i] = c["kind"]
    # footswitch: il primo e' quello dell'effetto; gli altri sono comandi (levette/pulsanti del modello) o decorativi
    for it in items:
        if it["t"] != "foot":
            continue
        x, y = it["x"], it["y"]
        z = z_at(y)
        part = dict(type="footswitch", kind=lk.get("foot_kind", "round"), x=x, y=y, z=z, r=FOOT_R, label=it["label"])
        out["parts"].append(part)
        j = it["foot"]
        if j == 0:
            zf_ = z + 0.012
            q = FOOT_ZONE
            out["foot"] = [(x - q, y - q, zf_), (x + q, y - q, zf_), (x + q, y + q, zf_), (x - q, y + q, zf_)]
            out["name_y"] = y
        for i, a in enumerate(assign):
            if a == ("foot", j):
                c = ctrls[i]
                part["control"] = True
                if c["kind"] == "button":
                    out["controls"][i] = dict(x=x, y=y, z=z + 0.012, strip="footbutton", r=FOOT_ZONE * 0.9)
                else:
                    # LED del footswitch: quello della scheda piu' vicino sopra il footswitch, se c'e'
                    lp = [p for p in out["parts"] if p["type"] == "led" and abs(p["x"] - x) < 0.012 and 0 < p["y"] - y < 0.030]
                    lp.sort(key=lambda p: p["y"] - y)
                    if lp:
                        led = (lp[0]["x"], lp[0]["y"], lp[0]["z"])
                    else:
                        led = (x, y + FOOT_ZONE + 0.004, z)
                        out["parts"].append(dict(type="led", x=led[0], y=led[1], z=led[2], colour="red"))
                    out["controls"][i] = dict(x=x, y=y, z=z + 0.012, strip="footswitch", r=FOOT_ZONE * 0.9, led=led)
                out["kinds"][i] = c["kind"]
    if lk.get("sections") and any(a == ("foot", 0) for a in assign) and out["led"] is not None:
        # pedali con soli footswitch di sezione (look["sections"], tappa 3B): i footswitch sono le levette delle sezioni,
        # l'effetto si inserisce dal LED di stato (il primo di look["leds"], piccola zona cliccabile attorno)
        lx, ly, lz, _ = out["led"]
        q = 0.0045
        out["foot"] = [(lx - q, ly - q, lz), (lx + q, ly - q, lz), (lx + q, ly + q, lz), (lx - q, ly + q, lz)]
    if out["led"] is None:
        # pedali storici senza LED: l'interfaccia mostra comunque lo stato con un piccolo LED (vicino al footswitch)
        f0 = [it for it in items if it["t"] == "foot" and it["foot"] == 0][0]
        x, y = f0["x"] + FOOT_ZONE + 0.0045, f0["y"] + FOOT_ZONE
        out["parts"].append(dict(type="led", x=x, y=y, z=z_at(y), colour="red", tiny=True))
        out["led"] = (x, y, z_at(y), 0.0018)
    for i, c in enumerate(out["controls"]):
        if c is None:
            raise ValueError("comando senza posizione: %s / %s" % (rid, ctrls[i]["label"]))
    for ex in lk.get("extra") or []:
        if ex[0] == "display":
            x0, x1 = G.x(ex[1]), G.x(ex[3])
            y1, y0 = G.y(ex[2]), G.y(ex[4])
            out["parts"].append(dict(type="display", x0=x0, y0=y0, x1=x1, y1=y1, z=z_at((y0 + y1) / 2)))
            if out["display"] is None and lk.get("display_ui"):
                out["display"] = (x0, y0, x1, y1)
                out["display_z"] = z_at((y0 + y1) / 2)
        elif ex[0] == "ledbar":
            n, vert = ex[3], (ex[4] if len(ex) > 4 else "v") == "v"
            for k in range(n):
                off = (k - (n - 1) / 2) * 0.0055
                x = G.x(ex[1]) + (0 if vert else off)
                y = G.y(ex[2]) - (off if vert else 0)
                out["parts"].append(dict(type="led", x=x, y=y, z=z_at(y), colour=ex[5] if len(ex) > 5 else "yellow", small=True))
    out["body"] = dict(W=W, D=D, H=max(zf, zb))
    out["top"] = (zf, zb)
    out["base"] = dict(W=W, D=D, corner=P["corner"], edge=P["edge"], kind=P["kind"])
    import real_layout
    tall = max([p.get("h", 0.0) for p in out["parts"] if p["type"] == "tubes"] + [0.0])
    out["frame"] = real_layout._frame(W, D, max(zf, zb) + max(0.0, tall - 0.010))
    jz = min(max(zf, zb) * 0.45, 0.024)
    jy = D / 2 - min(0.032, D * 0.28)
    out["jacks"] = (W, [(jy, jz), (jy - 0.020, jz) if r.get("stereo") else None])
    out["warnings"] = warnings
    out["items"] = items
    return out


def _item_type(c, spec_kind):
    k = c["kind"]
    if k == "slider":
        return "slider"
    if k == "button":
        return "button"
    if k == "toggle":
        return "button" if spec_kind == "button" else "toggle"
    return "rot"


def _size(it, default_knob):
    if it["t"] == "rot":
        sk = it.get("spec") or "knob"
        if sk not in KNOBS:
            sk = "small" if sk in ("lever", "button") else "knob"
        if sk == "knob":
            sk = default_knob
        if it.get("ctl") is not None and it["ctl"]["kind"] == "outer" and it.get("inner") is not None:
            sk = "med"
        strip, r, h = KNOBS[sk]
        if it.get("inner") is not None:
            r, h = PL.KNOB_R["boss_outer"], PL.KNOB_H["boss_outer"] + PL.KNOB_H["boss_inner"]
        it.update(strip=strip, r=r, h=h)
    elif it["t"] == "slider":
        it["y0"], it["y1"] = it["y"] - SLIDER_LEN / 2, it["y"] + SLIDER_LEN / 2


def _place(it, x, y):
    if it["t"] == "slider":
        it["y0"], it["y1"] = y - SLIDER_LEN / 2, y + SLIDER_LEN / 2
    it["x"], it["y"] = x, y
