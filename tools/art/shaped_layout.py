"""
Pedal Trinity - pedali "sagomati" (Python puro): bilancieri wah/volume con la forma del pedale reale e
scatole di misura reale. Usato sia in modalita' NORMALE (stili "treadle" e "box" del catalogo) sia
dalle repliche REAL MOD (serie "shaped").

Forme (look["shape"]):
  crybaby   wah classico a vasca pressofusa, bilanciere a tutta lunghezza (GCB95 e derivati)
  junior    come crybaby ma corto e piu' alto (CBJ95)
  mini      meta' misura, bilanciere corto (CBM95, Mini 535Q, JHM9, Volume X Mini)
  wide      carcassa larga: bilanciere a sinistra e pannello comandi a destra (JCT95)
  dvx       volume in alluminio a profilo basso, bordi molto arrotondati (Volume X, X8)
  dvp1      volume grande squadrato (DVP1, DVP1XL)
  eb        Ernie Ball VP: telaio in alluminio e pedana piatta incernierata con gomma antiscivolo
  morley    Morley: carcassa larga in acciaio, bilanciere con profili cromati
  dearmond  volume storico anni '50-'60: fusione arrotondata e cappuccio cromato in punta

Comandi: il primo e' sempre il bilanciere (strip "treadle", 254 nell'interfaccia: anchor = tallone,
top = punta). Gli altri vanno dove indica look["place"][etichetta]:
  ("L"|"R", pct[, tipo])   fiancata sinistra/destra vista dal chitarrista, pct = 0 tallone .. 100 punta
  ("P", xf, pct[, tipo])   pannello della carcassa larga, xf = 0 bordo sinistro .. 1 bordo destro
  ("H", xf[, tipo])        striscia del tallone davanti al bilanciere, xf = -1 .. 1
  tipo: "knob" (piccolo), "big" (pomello grande), "button" (pulsante/kickswitch), "lever" (levetta),
        "foot" (interruttore a pedale)
Le fiancate del pedale non si vedono dalla camera inclinata: i comandi laterali stanno su una
"aletta" della fusione che sporge dal fianco alla loro altezza reale lungo il pedale, con il perno
verticale (stessi pomelli 3D dell'interfaccia).
LED: look["leds"] = [(dove, pos, colore), ...] con dove "L"|"R"|"H"|"P" come sopra; il primo e' il
LED di stato acceso dall'interfaccia (se manca: piccolo LED rosso sulla striscia del tallone).
"""
import math

import pedal_layout as PL

TAN22 = math.tan(math.radians(22.0))
SMALL_R, SMALL_H = PL.KNOB_R["boss_inner"], PL.KNOB_H["boss_inner"]
BIG_R, BIG_H = PL.KNOB_R["boss"], PL.KNOB_H["boss"]
BUTTON_R, FOOT_R, LED_R = 0.0042, 0.0068, 0.0021
LABEL_SIZE = 0.0018            # altezza delle scritte sulle alette (m)
LABEL_GAP = 0.0062             # dal centro del comando alla scritta sotto: oltre il raggio
TREADLE_WORDS = ("PEDAL", "VOLUME", "EXP", "EXP1", "WAH")

# proporzioni delle forme: base_h = altezza della vasca (frazione di H); heel/toe = spazio libero davanti
# e dietro al bilanciere; r_heel/r_toe = raggi degli angoli in pianta; rock_w = differenza di larghezza
# del bilanciere rispetto alla vasca; plate = targhetta del nome sulla punta del bilanciere
SHAPES = {
    "crybaby": dict(base_h=0.56, heel=0.011, toe=0.004, r_heel=0.010, r_toe=0.030, rock_w=-0.005, rock_t=0.011,
                    rocker="cast", rubber="ribs", rub_heel=0.010, rub_toe=0.036),
    "junior": dict(base_h=0.56, heel=0.011, toe=0.005, r_heel=0.010, r_toe=0.028, rock_w=-0.005, rock_t=0.012,
                   rocker="cast", rubber="ribs", rub_heel=0.010, rub_toe=0.034),
    "mini": dict(base_h=0.52, heel=0.009, toe=0.004, r_heel=0.008, r_toe=0.020, rock_w=-0.004, rock_t=0.010,
                 rocker="cast", rubber="ribs", rub_heel=0.008, rub_toe=0.030),
    "wide": dict(base_h=0.56, heel=0.011, toe=0.005, r_heel=0.010, r_toe=0.016, rock_w=0.0, rock_t=0.011,
                 rocker="cast", rubber="studs", rub_heel=0.010, rub_toe=0.036, rocker_w=0.094, panel_h=0.72),
    "dvx": dict(base_h=0.46, heel=0.010, toe=0.006, r_heel=0.020, r_toe=0.024, rock_w=-0.003, rock_t=0.010,
                rocker="cast", rubber="grip", rub_heel=0.008, rub_toe=0.034),
    "dvp1": dict(base_h=0.48, heel=0.012, toe=0.006, r_heel=0.008, r_toe=0.012, rock_w=-0.004, rock_t=0.011,
                 rocker="cast", rubber="ribs", rub_heel=0.010, rub_toe=0.036),
    "eb": dict(base_h=0.42, heel=0.006, toe=0.010, r_heel=0.012, r_toe=0.016, rock_w=0.002, rock_t=0.006,
               rocker="plate", rubber="grip", rub_heel=0.006, rub_toe=0.034),
    "morley": dict(base_h=0.50, heel=0.012, toe=0.006, r_heel=0.006, r_toe=0.006, rock_w=-0.006, rock_t=0.010,
                   rocker="cast", rubber="ribs", rub_heel=0.010, rub_toe=0.036, trim=True),
    "dearmond": dict(base_h=0.52, heel=0.014, toe=0.016, r_heel=0.018, r_toe=0.030, rock_w=-0.008, rock_t=0.010,
                     rocker="cast", rubber="ribs", rub_heel=0.010, rub_toe=0.036, toe_cap=True),
}


def label_w(s, size=LABEL_SIZE):
    """Larghezza stimata di una scritta in maiuscolo bold (m): il layout e' Python puro (anche in Blender)."""
    return 0.66 * size * len(str(s)) + 0.0004


def _kind_of(c, spec_kind):
    if spec_kind:
        return spec_kind
    if c["kind"] == "toggle":
        return "button"
    return "knob"


def _footprint(kind, c):
    """(raggio, altezza, estensione verso il tallone, verso la punta) del comando sulla sua aletta."""
    if kind == "big":
        r, h = BIG_R, BIG_H
    elif kind == "knob":
        r, h = SMALL_R, SMALL_H
    elif kind == "button":
        r, h = BUTTON_R, 0.004
    elif kind == "foot":
        r, h = FOOT_R, 0.009
    elif kind == "lever":
        r, h = 0.0040, 0.012
    else:                          # led
        r, h = LED_R, 0.002
    shadow = h * TAN22
    ring = 0.0040 if (c is not None and c["kind"] == "selector") else 0.0
    return r, h, r + ring, r + max(ring, shadow) + 0.0006


def _treadle_index(ctrls):
    return 0


def shape_params(r):
    lk = r["look"]
    P = dict(SHAPES[lk["shape"]])
    P.update(lk.get("shape_opts") or {})
    return P


def wah_layout(r):
    """Layout del pedale sagomato r (dizionario del catalogo con 'look' e 'dims' in metri)."""
    lk = r["look"]
    P = shape_params(r)
    W, D, H = r["dims"]
    ctrls = r["controls"]
    place = {str(k).upper(): v for k, v in (lk.get("place") or {}).items()}
    base_h = P["base_h"] * H
    has_heel = any(v[0] == "H" for v in place.values()) or any(l[0] == "H" for l in (lk.get("leds") or []))
    heel = max(P["heel"], 0.020 if has_heel else 0.0)
    toe = P["toe"] + (lk.get("toe_strip") or 0.0)
    # bilanciere
    if lk["shape"] == "wide":
        rw = P["rocker_w"]
        xc = -W / 2 + 0.004 + rw / 2
    else:
        rw = W + P["rock_w"]
        xc = 0.0
    y0, y1 = -D / 2 + heel, D / 2 - toe
    z0 = base_h + (0.004 if P["rocker"] == "plate" else 0.006)
    z1 = max(z0 + 0.010, H - 0.001)
    length = math.hypot(y1 - y0, z1 - z0)
    ang = math.atan2(z1 - z0, y1 - y0)

    def zr(y):
        """Quota della superficie superiore del bilanciere in y."""
        return z0 + (y - y0) * (z1 - z0) / (y1 - y0)

    out = dict(style=r.get("style"), builder="wah", shape=lk["shape"], controls=[None] * len(ctrls), led=None,
               toggle=None, display=None, sliders=None, buttons=[], panel_y0=-D / 2, tread_y1=-D / 2, parts=[],
               display_z=base_h, kinds=[None] * len(ctrls))
    out["base"] = dict(W=W, D=D, h=base_h, r_heel=P["r_heel"], r_toe=P["r_toe"])
    out["rocker"] = dict(xc=xc, y0=y0, y1=y1, z0=z0, z1=z1, w=rw, t=P["rock_t"], length=length, angle=ang,
                         kind=P["rocker"], rubber=P["rubber"], rub_heel=P["rub_heel"], rub_toe=P["rub_toe"],
                         trim=bool(P.get("trim")), toe_cap=bool(P.get("toe_cap")))
    # targhetta (coordinate locali del bilanciere: y lungo la pedana dal centro)
    pd = min(0.024, P["rub_toe"] - 0.008)
    out["plate"] = dict(y=length / 2 - 0.004 - pd / 2, d=pd, w=rw - 0.026)
    # bilanciere trascinabile: dal tallone a poco prima della targhetta
    # (zona di presa larga al massimo 9 cm e ancoraggio del tallone abbastanza lontano dal bordo: la sagoma di
    # controllo, un cerchio attorno al tallone, resta sul pedale anche sui modelli larghi)
    tr_r = min((rw - 0.020) / 2, 0.045)
    ty0, ty1 = y0 + max(0.030 * min(1.0, D / 0.2), 0.9 * tr_r - heel + 0.005), y1 - 0.026
    out["controls"][0] = dict(x=xc, y=ty0, z=zr(ty0), y1=ty1, z1=zr(ty1), strip="treadle", r=tr_r)
    out["kinds"][0] = "knob"
    # zona dell'interruttore a punta (on/off): in cima al bilanciere
    fy0, fy1 = y1 - 0.024, y1 - 0.004
    fx = rw / 2 - 0.008
    out["foot"] = [(xc - fx, fy0, zr(fy0)), (xc + fx, fy0, zr(fy0)), (xc + fx, fy1, zr(fy1)), (xc - fx, fy1, zr(fy1))]
    # pannello della carcassa larga
    if lk["shape"] == "wide":
        px0, px1 = xc + rw / 2 + 0.004, W / 2 - 0.004
        out["panel"] = dict(x0=px0, x1=px1, y0=-D / 2 + 0.006, y1=D / 2 - 0.006, z=P["panel_h"] * H)
    # ---- comandi non a bilanciere
    sides = {"L": [], "R": []}
    warnings = []
    for i, c in enumerate(ctrls):
        if i == 0:
            continue
        spec = place.get(c["label"].upper())
        if spec is None:
            spec = ("R", 50)
            warnings.append("%s: posizione di %s non indicata (fiancata destra)" % (r["id"], c["label"]))
        where = spec[0]
        if where in ("L", "R"):
            kind = _kind_of(c, spec[2] if len(spec) > 2 else None)
            sides[where].append(dict(i=i, kind=kind, y=-D / 2 + float(spec[1]) / 100.0 * D, label=c["label"], ctl=c))
        elif where == "P":
            kind = _kind_of(c, spec[3] if len(spec) > 3 else None)
            pn = out["panel"]
            x = pn["x0"] + float(spec[1]) * (pn["x1"] - pn["x0"])
            y = -D / 2 + float(spec[2]) / 100.0 * D
            out["controls"][i] = _ctrl_pos(kind, c, x, y, pn["z"])
            out["kinds"][i] = c["kind"]
        elif where == "H":
            kind = _kind_of(c, spec[2] if len(spec) > 2 else None)
            x = float(spec[1]) * (W / 2 - 0.012)
            y = -D / 2 + heel / 2
            out["controls"][i] = _ctrl_pos(kind, c, x, y, base_h)
            out["kinds"][i] = c["kind"]
    leds = list(lk.get("leds") or [])
    if not leds:
        leds = [("H", 0.78, "red")]
    for k, (where, pos, col) in enumerate(leds):
        if where in ("L", "R"):
            sides[where].append(dict(i=None, kind="led", y=-D / 2 + float(pos) / 100.0 * D, label="", ctl=None, main=k == 0,
                                     colour=col))
        elif where == "P":
            pn = out["panel"]
            x = pn["x0"] + float(pos[0]) * (pn["x1"] - pn["x0"])
            y = -D / 2 + float(pos[1]) / 100.0 * D
            _add_led(out, x, y, pn["z"], col, k == 0)
        else:
            x = float(pos) * (W / 2 - 0.012)
            _add_led(out, x, -D / 2 + heel / 2, base_h, col, k == 0)
    # ---- alette laterali
    out["pods"] = []
    ymin, ymax = -D / 2 + 0.010, D / 2 - 0.010
    pod_z = base_h * 0.86
    for side, items in sides.items():
        if not items:
            continue
        sgn = -1 if side == "L" else 1
        items.sort(key=lambda it: it["y"])
        for it in items:
            r_, h_, eb, ea = _footprint(it["kind"], it["ctl"])
            it.update(r=r_, h=h_, eb=eb, ea=ea)
        # scritte sotto (verso il tallone) se c'e' posto, altrimenti di lato (verso l'esterno)
        mode = "below"
        for a, b in zip(items, items[1:]):
            need = a["ea"] + b["eb"] + (LABEL_GAP if b["label"] else 0.0) + 0.0010
            if b["y"] - a["y"] < need:
                mode = "side"
        for it in items:
            lab = it["label"] and it["kind"] not in ("led",)
            it["eb2"] = it["eb"] + (0.0068 if (mode == "below" and lab) else 0.0010)
        # sovrapposizioni residue: si allargano le distanze (nell'ordine reale), poi si rientra nei limiti
        for a, b in zip(items, items[1:]):
            need = a["ea"] + b["eb2"] + 0.0008
            if b["y"] - a["y"] < need:
                warnings.append("%s: %s spostato di %.1f mm per non toccare %s" % (r["id"], b["label"] or "LED",
                                                                                     (need - (b["y"] - a["y"])) * 1000, a["label"] or "LED"))
                b["y"] = a["y"] + need
        over = items[-1]["y"] + items[-1]["ea"] - ymax
        if over > 0:
            for it in items:
                it["y"] -= over
        under = ymin - (items[0]["y"] - items[0]["eb2"])
        if under > 0:
            for it in items:
                it["y"] += under
        # larghezza dell'aletta e posizione dei comandi
        widths = []
        for it in items:
            if it["kind"] == "lever":
                ch = (it["ctl"].get("choices") or ["", ""]) if it["ctl"] else ["", ""]
                w_in = 2 * (0.0044 + max(label_w(str(ch[0])[:5], 0.0015), label_w(str(ch[1])[:5], 0.0015))) + 0.002
                widths.append(max(w_in, label_w(it["label"]) + 0.002 if mode == "below" else 0.0))
            elif mode == "below" or not it["label"]:
                ring = 2 * (it["r"] + (0.0045 if it["ctl"] is not None and it["ctl"]["kind"] == "selector" else 0.0012))
                widths.append(max(ring + 0.002, label_w(it["label"]) + 0.002 if it["label"] else 0.0))
            else:
                widths.append(0.0025 + 2 * it["r"] + 0.0024 + label_w(it["label"]) + 0.0015)
        pw = max(0.0125, max(widths) + 0.0020)
        x_in = W / 2 - 0.002                   # l'aletta entra di 2 mm nella vasca
        for it, w_ in zip(items, widths):
            if mode == "below" or it["kind"] in ("lever",) or not it["label"]:
                cx = x_in + 0.002 + pw / 2 - 0.001
                it["label_pos"] = "below"
            else:
                cx = x_in + 0.0025 + 0.0010 + it["r"]
                it["label_pos"] = "side"
            it["x"] = sgn * cx
        # una sola aletta per gruppo di comandi vicini (distanza < 30 mm), altrimenti piu' alette
        groups, cur = [], [items[0]]
        for a, b in zip(items, items[1:]):
            if (b["y"] - b["eb2"]) - (a["y"] + a["ea"]) > 0.030:
                groups.append(cur); cur = [b]
            else:
                cur.append(b)
        groups.append(cur)
        for g in groups:
            gy0 = min(it["y"] - it["eb2"] for it in g) - 0.0025
            gy1 = max(it["y"] + it["ea"] for it in g) + 0.0025
            pod = dict(side=side, x0=min(sgn * x_in, sgn * (x_in + pw)), x1=max(sgn * x_in, sgn * (x_in + pw)),
                       y0=gy0, y1=gy1, z=pod_z, items=[])
            for it in g:
                pod["items"].append(dict(kind=it["kind"], x=it["x"], y=it["y"], r=it["r"], h=it["h"], label=it["label"],
                                         label_pos=it["label_pos"], i=it["i"], colour=it.get("colour")))
                if it["kind"] == "led":
                    _add_led(out, it["x"], it["y"], pod_z, it["colour"], it.get("main", False))
                else:
                    out["controls"][it["i"]] = _ctrl_pos(it["kind"], it["ctl"], it["x"], it["y"], pod_z)
                    out["kinds"][it["i"]] = it["ctl"]["kind"]
            out["pods"].append(pod)
    for i, c in enumerate(out["controls"]):
        if c is None:
            raise ValueError("comando senza posizione: %s / %s" % (r["id"], ctrls[i]["label"]))
    if out["led"] is None:
        _add_led(out, W / 2 - 0.012, -D / 2 + heel / 2, base_h, "red", True)
    # display (accordatore a sfioramento sulla striscia della punta)
    if lk.get("display"):
        dd = lk.get("toe_strip") or 0.020
        x0, x1 = -W / 2 + 0.014, W / 2 - 0.014
        yy0, yy1 = D / 2 - dd + 0.003, D / 2 - 0.004
        out["parts"].append(dict(type="display", x0=x0, y0=yy0, x1=x1, y1=yy1))
        out["display"] = (x0, yy0, x1, yy1)
    # misure d'ingombro (con le alette), inquadratura, prese
    half = W / 2
    for pod in out["pods"]:
        half = max(half, abs(pod["x0"]), abs(pod["x1"]))
    out["body"] = dict(W=2 * half, D=D, H=H)
    import real_layout
    out["frame"] = real_layout._frame(2 * half, D, H)
    busy = [(p["y0"] - 0.008, p["y1"] + 0.008) for p in out["pods"]]
    jp = float(lk.get("jack_pct", 55.0))
    jy = None
    for k in range(0, 40):
        for sgn in (1, -1):
            pct = jp + sgn * 2.5 * k
            if not 12 <= pct <= 92:
                continue
            y = -D / 2 + pct / 100.0 * D
            if all(not (a <= y <= b) for a, b in busy) and (not r.get("stereo") or all(not (a <= y - 0.022 <= b) for a, b in busy)):
                jy = y
                break
        if jy is not None:
            break
    if jy is None:
        jy = -D / 2 + jp / 100.0 * D
    jz = min(base_h * 0.5, 0.020)
    out["jacks"] = (W, [(jy, jz), (jy - 0.022, jz) if r.get("stereo") else None])
    out["warnings"] = warnings
    return out


def rubber_inset(rk):
    """Margine laterale della gomma sul bilanciere (m)."""
    return 0.0045 if rk["kind"] == "plate" else 0.0055


def rubber_span(rk):
    """Estensione della gomma lungo il bilanciere (coordinate locali, 0 = centro)."""
    L = rk["length"]
    return -L / 2 + rk["rub_heel"], L / 2 - rk["rub_toe"]


def _ctrl_pos(kind, c, x, y, z):
    if kind == "big":
        return dict(x=x, y=y, z=z, strip="boss", r=BIG_R, h=BIG_H)
    if kind == "knob":
        return dict(x=x, y=y, z=z, strip="boss_small", r=SMALL_R, h=SMALL_H)
    if kind == "lever":
        return dict(x=x, y=y, z=z, strip="toggle")
    if kind == "foot":
        return dict(x=x, y=y, z=z + 0.009, strip="button", r=FOOT_R, part="foot")
    return dict(x=x, y=y, z=z + 0.004, strip="button", r=BUTTON_R, part="button")


def _add_led(out, x, y, z, colour, main):
    out["parts"].append(dict(type="led", x=x, y=y, z=z, colour=colour))
    if main or out["led"] is None:
        out["led"] = (x, y, z, 0.0020)


# ------------------------------------------------------------------ modalita' normale
def as_shaped(m):
    """Dizionario 'r' (come quelli delle repliche REAL MOD) per un modello del catalogo di stile
    treadle/box, con nome e sigla Pedal Trinity."""
    lk = m["look"]
    r = dict(m)
    r["real_code"], r["real_name"] = m["code"], m["name"]
    r["normal"] = True
    if lk.get("boss"):
        # pedali a bilanciere BOSS: forma e misure della replica (real_specs.json), scritte Pedal Trinity
        import real_catalog
        sp = real_catalog.load_specs().get(m["id"], {})
        series = sp.get("series") if sp.get("series") in ("treadle_volume", "treadle_wah", "rocker") else "treadle_wah"
        dims = sp.get("dims_mm") or real_catalog.DEFAULT_DIMS[series]
        r["series"] = series
        r["dims"] = tuple(float(v) / 1000.0 for v in dims)
        return r
    r["dims"] = tuple(float(v) / 1000.0 for v in lk["dims"])
    if m["style"] == "box" and lk.get("stomp"):
        r["series"] = "stomp"           # MXR, Electro-Harmonix...: forma del pedale vero (stomp_layout.py)
    elif m["style"] == "box":
        r["series"] = "other"
        r["real_layout"] = lk.get("rows")
        r["real_footswitches"] = None
    else:
        r["series"] = "shaped"
    return r


def layout_of(r):
    """Layout di un pedale sagomato (normale o REAL MOD) secondo la sua serie."""
    import real_layout
    if r["series"] == "shaped":
        return wah_layout(r)
    if r["series"] == "stomp":
        import stomp_layout
        return stomp_layout.stomp_layout(r)
    if r["series"] in ("treadle_volume", "treadle_wah", "rocker"):
        return real_layout.treadle_layout(r)
    return real_layout.box_layout(r)


def normal_layout(m):
    return layout_of(as_shaped(m))
