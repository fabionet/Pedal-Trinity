"""
Pedal Trinity - disposizione dei comandi delle repliche REAL MOD (Python puro).

real_layout(r) restituisce lo stesso dizionario di pedal_layout.pedal_layout()
(posizioni in metri: x a destra, y verso il retro, z in alto) con in piu':
  builder   "compact" | "box" | "treadle"   (costruttore Blender)
  body      dict(W, D, H)                   misure del contenitore
  frame     (x0, x1, v0, v1)                inquadratura della camera
  jacks     (W, [(y, z), (y, z) | None])    prese sui fianchi
  parts     elementi non funzionali da modellare/serigrafare:
            footswitch, button, display, led, label
  display_z quota del display
Strisce nuove: "treadle" (pedale a bilanciere trascinabile) e "footbutton"
(pulsante del looper sul footswitch renderizzato).
"""
import math

import pedal_layout as PL

KR, KH = PL.KNOB_R["boss"], PL.KNOB_H["boss"]
MARGIN = 0.009


def _label_key(s):
    return "".join(ch for ch in str(s).upper() if ch.isalnum())


def _match_control(item, controls, used):
    k = _label_key(item)
    for i, c in enumerate(controls):
        if i not in used and _label_key(c["label"]) == k:
            return i
    return None


def _frame(W, D, H, extra_front=0.0):
    """Inquadratura con margine per l'ombra (stessa camera inclinata dei compatti)."""
    cos, sin = math.cos(math.radians(22.0)), math.sin(math.radians(22.0))
    half = W / 2 + max(0.018, W * 0.10)
    v_top = (D / 2) * cos + (H + 0.02) * sin + 0.012
    v_bot = (-D / 2 - extra_front) * cos - 0.020
    return (-half, half, v_bot, v_top)


def _compact_order(r):
    """Copia dei comandi nell'ordine reale da sinistra a destra, con i concentrici (outer, inner) accoppiati.
    Restituisce (comandi riordinati, indici originali)."""
    ctrls = r["controls"]
    order = r.get("real_knob_order") or []
    pairs = {(_label_key(o), _label_key(i)) for o, i in (r.get("real_concentric") or []) if o and i}
    by_key = {}
    for i, c in enumerate(ctrls):
        by_key.setdefault(_label_key(c["label"]), i)
    rot = [i for i, c in enumerate(ctrls) if c["kind"] in ("knob", "outer", "inner", "selector")]
    seq = []
    for lab in order:
        i = by_key.get(_label_key(lab))
        if i is None and "/" in str(lab):
            o, _, inn = str(lab).partition("/")
            if _label_key(o) in by_key and _label_key(inn) in by_key:
                pairs.add((_label_key(o), _label_key(inn)))
                i = by_key[_label_key(o)]
        if i is not None and i in rot and i not in seq:
            seq.append(i)
    seq += [i for i in rot if i not in seq]
    # concentrici: l'inner segue subito il suo outer
    outer_of = {}
    for o, inn in pairs:
        if o in by_key and inn in by_key:
            outer_of[by_key[inn]] = by_key[o]
    final = []
    for i in seq:
        if i in outer_of and outer_of[i] in seq:
            continue
        final.append(i)
        for inn, o in outer_of.items():
            if o == i:
                final.append(inn)
    new = []
    for i in final:
        c = dict(ctrls[i])
        if i in outer_of:
            c["kind"] = "inner"
        elif i in outer_of.values():
            c["kind"] = "outer"
        new.append(c)
    idx = list(final)
    for i, c in enumerate(ctrls):
        if i not in idx:
            new.append(dict(c)); idx.append(i)
    return new, idx


# compatti a 3 pomelli "a V" (verificato sulle foto dei pedali reali): sinistra, centro piu' basso, destra
V_TRIO = {}
for _ids, _order in (
        (("ds1", "ds1_4a", "ds1_b50a", "ds1_wh", "ds1_bk", "ds1_6m", "ds1w"), ("TONE", "LEVEL", "DIST")),
        (("sd1", "sd1_4a", "sd1_b50a", "sd1_be", "sd1w"), ("LEVEL", "TONE", "DRIVE")),
        (("bd2", "bd2_b50a", "bd2_10m", "bd2w"), ("LEVEL", "TONE", "GAIN")),
        (("od3",), ("LEVEL", "TONE", "DRIVE")),
        (("cs2",), ("LEVEL", "ATTACK", "SUSTAIN")),
        (("tr2",), ("RATE", "WAVE", "DEPTH")),
        (("fz5",), ("LEVEL", "MODE", "FUZZ"))):
    for _i in _ids:
        V_TRIO[_i] = _order
V_TOP_Y, V_MID_Y, V_X = 0.039, 0.0275, 0.022


def compact_layout(r):
    if r["id"] in V_TRIO:
        r = dict(r, real_knob_order=list(V_TRIO[r["id"]]), real_concentric=[])
    ctrls, idx = _compact_order(r)
    lay = PL.pedal_layout(dict(r, style="boss", controls=ctrls))
    pos = [None] * len(ctrls)
    kinds = [None] * len(ctrls)
    for j, i in enumerate(idx):
        pos[i] = lay["controls"][j]
        kinds[i] = ctrls[j]["kind"]
    lay["controls"] = pos
    lay["kinds"] = kinds                  # tipi visivi (pomelli concentrici del pedale reale)
    if r["id"] in V_TRIO:
        keys = {_label_key(c["label"]): i for i, c in enumerate(r["controls"])}
        for j, lab in enumerate(V_TRIO[r["id"]]):
            i = keys[_label_key(lab)]
            p = dict(pos[i])
            p["x"] = (-V_X, 0.0, V_X)[j]
            p["y"] = V_MID_Y if j == 1 else V_TOP_Y
            if j == 1:
                p["label_above"] = True     # come sui pedali reali: l'etichetta del pomello centrale sta sopra
            pos[i] = p
    lay["builder"] = "compact"
    lay["body"] = dict(W=PL.W, D=PL.D, H=PL.PANEL_H)
    lay["frame"] = None
    lay["jacks"] = None
    lay["parts"] = []
    return lay


def _auto_rows(r):
    """Disposizione di ripiego: comandi in file da 4-5, display per le serie digitali."""
    ctrls = [c for c in r["controls"] if c["kind"] not in ("button",)]
    per = 5 if r["series"] in ("floor", "series500", "twin") else 4
    rows = []
    if r["series"] in ("series200", "series500", "floor", "twin") and r["family"] not in ("Volume", "Router"):
        rows.append(["DISPLAY"])
    sliders = [c["label"] for c in ctrls if c["kind"] == "slider"]
    others = [c["label"] for c in ctrls if c["kind"] != "slider"]
    for i in range(0, len(others), per):
        rows.append(others[i:i + per])
    if sliders:
        rows.append(sliders)
    return rows


def _default_footswitches(r):
    n = {"twin": 2, "series200": 2, "series500": 3, "floor": 3, "vintage_box": 1, "tabletop": 0, "other": 1}.get(r["series"], 1)
    buttons = [c["label"] for c in r["controls"] if c["kind"] == "button"]
    if buttons:
        return buttons[:max(n, len(buttons))]
    return ["ON/OFF"] if n == 1 else ["MEMORY"] * (n - 1) + ["ON/OFF"]


MAIN_KEYS = ("ONOFF", "EFFECTONOFF", "EFFECT", "NORMALEFFECT", "EFFECTNORMAL", "BYPASS", "ON")


def _classify(item, ctrls, used, in_foot_row):
    """Classifica un elemento della scheda: (tipo, valore)."""
    s = str(item).strip()
    u = s.upper()
    if u.startswith("FOOTSWITCH"):
        return ("foot", s.split(":", 1)[1].strip() if ":" in s else "")
    if u in ("PEDAL", "TREADLE"):
        return (None, None)
    if u in ("LED", "CHECK LED", "CHECK"):
        return ("led", None)
    if u == "DISPLAY":
        return ("display", None)
    if u.startswith("BUTTON"):
        return ("button", s.split(":", 1)[1].strip() if ":" in s else "")
    if u.startswith("KNOB") and ":" not in s and _match_control(s, ctrls, used) is not None:
        pass                                # comando vero che si chiama "KNOB 1" (SY-200): non decorativo
    elif u.startswith("KNOB"):
        return ("deco_knob", s.split(":", 1)[1].strip() if ":" in s else "")
    if u.startswith("SWITCH"):
        lab = s.split(":", 1)[1].strip() if ":" in s else ""
        ci = _match_control(lab, ctrls, used)
        if ci is not None and ctrls[ci]["kind"] in ("toggle", "selector"):
            return ("ctrl", ci)
        return ("deco_switch", lab)
    if u.startswith("SLIDERS"):
        try:
            return ("deco_sliders", max(1, min(12, int(u.split(":", 1)[1]))))
        except Exception:
            return (None, None)
    ci = _match_control(s, ctrls, used)
    if ci is None and "/" in s:
        o, _, inn = s.partition("/")
        co, cn = _match_control(o, ctrls, used), _match_control(inn, ctrls, used)
        if co is not None and cn is not None and co != cn:
            return ("concentric", (co, cn))
    if ci is None:
        return (None, None)
    if in_foot_row and ctrls[ci]["kind"] in ("toggle", "button"):
        return ("foot_ctrl", ci)
    return ("ctrl", ci)


# ingombro dei comandi sul piano dei contenitori a scatola: (larghezza, altezza della fila, spostamento del centro)
# in metri. Altezza = dalla scritta sotto il comando alla sommita' che, nella vista inclinata, copre il piano dietro.
SMALL_R, SMALL_H = PL.KNOB_R["boss_inner"], PL.KNOB_H["boss_inner"]
ITEM = {"knob": (0.0190, 0.0270, 0.0), "small": (0.0140, 0.0205, 0.0012), "concentric": (0.0200, 0.0315, 0.0),
        "toggle": (0.0170, 0.0150, 0.0), "button": (0.0130, 0.0140, 0.0015), "led": (0.0070, 0.0070, 0.0),
        "switch": (0.0130, 0.0110, 0.0), "slider": (0.0080, 0.0260, 0.0), "display": (0.0460, 0.0140, 0.0)}
SLIDER_MIN_W = 0.0058
ROW_GAP = 0.0015


def _item_kind(typ, val, ctrls, small):
    if typ in ("ctrl", "foot_ctrl"):
        k = ctrls[val]["kind"]
        if k == "slider":
            return "slider"
        if k == "toggle":
            return "toggle"
        if k == "button":
            return "button"
        return "small" if small else "knob"
    return {"display": "display", "led": "led", "button": "button", "deco_knob": "small" if small else "knob",
            "concentric": "concentric", "deco_switch": "switch", "deco_sliders": "slider"}[typ]


def _item_width(typ, val, kind, W):
    if kind == "display":
        return 0.046 if W < 0.12 else 0.066
    if typ == "deco_sliders":
        return ITEM["slider"][0] * val
    return ITEM[kind][0]


DISPLAY_MIN_W = 0.030


def _row_widths(row, ctrls, W, avail_w, small):
    """Larghezze degli elementi di una fila; il display si restringe (fino a DISPLAY_MIN_W) per far posto ai comandi."""
    kinds = [_item_kind(t, v, ctrls, small) for t, v in row]
    widths = [_item_width(t, v, k, W) for (t, v), k in zip(row, kinds)]
    over = sum(widths) + ROW_GAP * len(row) - avail_w
    if over > 0:
        for i, k in enumerate(kinds):
            if k == "display":
                cut = min(over, widths[i] - DISPLAY_MIN_W)
                widths[i] -= cut
                over -= cut
    return kinds, widths


def _fits(row, ctrls, W, avail_w, small):
    kinds, widths = _row_widths(row, ctrls, W, avail_w, small)
    if sum(widths) + ROW_GAP * len(row) <= avail_w:
        return True
    n_sl = sum(v if t == "deco_sliders" else 1 for t, v in row)
    return all(k == "slider" for k in kinds) and n_sl * SLIDER_MIN_W <= avail_w


def _wrap_rows(rows, ctrls, W, avail_w, small=False):
    """Divide le file troppo larghe (i cursori si stringono fino al passo minimo prima di andare a capo)."""
    out = []
    for row in rows:
        if _fits(row, ctrls, W, avail_w, small):
            out.append(row); continue
        k = 2
        while True:
            per = int(math.ceil(len(row) / k))
            chunks = [row[i:i + per] for i in range(0, len(row), per)]
            if all(_fits(ch, ctrls, W, avail_w, small) for ch in chunks) or per == 1:
                out.extend(chunks); break
            k += 1
    return out


def box_layout(r):
    W, D, H = r["dims"]
    ctrls = r["controls"]
    out = dict(style=r["style"], controls=[None] * len(ctrls), led=None, toggle=None, display=None,
               sliders=None, buttons=[], panel_y0=-D / 2, tread_y1=-D / 2, builder="box",
               body=dict(W=W, D=D, H=H), parts=[], display_z=H)
    rows = [row if isinstance(row, list) else [row] for row in (r.get("real_layout") or _auto_rows(r))]
    used = set()
    foot_rows, panel_rows = [], []
    for row in rows:
        is_foot = any(str(x).upper().startswith("FOOTSWITCH") for x in row)
        items = []
        for it in row:
            typ, val = _classify(it, ctrls, used, is_foot)
            if typ is None:
                continue
            if typ in ("ctrl", "foot_ctrl"):
                used.add(val)
            elif typ == "concentric":
                used.update(val)
            if is_foot and typ in ("led",):
                continue
            items.append((typ, val))
        if not items:
            continue
        if is_foot:
            feet = [x for x in items if x[0] in ("foot", "foot_ctrl")]
            rest = [x for x in items if x[0] not in ("foot", "foot_ctrl")]
            foot_rows.append(feet)
            if rest:
                panel_rows.append(rest)
        else:
            panel_rows.append(items)
    if not foot_rows and r["series"] != "tabletop":
        spec = r.get("real_footswitches")
        labels = None
        if isinstance(spec, dict):
            labels = spec.get("labels") or None
            if not labels and isinstance(spec.get("count"), int):
                labels = ["ON/OFF"] * spec["count"]
        if labels is None:
            labels = _default_footswitches(r)
        feet = []
        for lab in labels[:8]:
            ci = _match_control(lab, ctrls, used)
            if ci is not None and ctrls[ci]["kind"] in ("button", "toggle"):
                used.add(ci); feet.append(("foot_ctrl", ci))
            else:
                feet.append(("foot", str(lab)))
        if feet:
            foot_rows.append(feet)
    # pulsanti del looper non citati: sui footswitch se ce ne sono, altrimenti sul pannello
    for i, c in enumerate(ctrls):
        if c["kind"] == "button" and i not in used:
            used.add(i)
            if foot_rows:
                foot_rows[-1].append(("foot_ctrl", i))
            else:
                panel_rows.append([("ctrl", i)])
    # comandi non citati nella scheda: una fila in fondo al pannello
    left = [i for i, c in enumerate(ctrls) if i not in used]
    for i in left:
        for row in panel_rows:
            if _fits(row + [("ctrl", i)], ctrls, W, W - 2 * MARGIN, False):
                row.append(("ctrl", i)); break
        else:
            panel_rows.append([("ctrl", i)])
    # LED non citato: in fondo alla prima fila (mai sopra un comando)
    if not any(t == "led" for row in panel_rows for t, _ in row):
        if panel_rows:
            panel_rows[0].append(("led", None))
        else:
            panel_rows.append([("led", None)])
    avail_w = W - 2 * MARGIN
    base_rows = panel_rows
    foot_rows = foot_rows[-2:]

    # --- spazio verticale: footswitch davanti, fascia del nome, file del pannello dietro.
    # Se le file non ci stanno: zona dei footswitch piu' bassa, poi pomelli piccoli, poi file accorpate.
    def heights(rows, small):
        return [max(ITEM[_item_kind(t, v, ctrls, small)][1] for t, v in row) for row in rows]

    y_back = D / 2 - MARGIN - 0.002
    nrow = len(foot_rows)
    if foot_rows:
        fz = min(0.058 if nrow == 1 else 0.042, D * (0.40 if nrow == 1 else 0.26))
        fz_min = min(fz, 0.030 if nrow == 1 else 0.028)
    else:
        fz = fz_min = 0.0

    def front(fz):
        if not foot_rows:
            return -D / 2 + MARGIN + 0.022            # fascia del nome con il pulsante EFFECT
        return -D / 2 + MARGIN + fz * nrow + 0.004 * (nrow - 1) + 0.0155   # + scritte e nome sopra i footswitch

    small = False
    panel_rows = _wrap_rows(base_rows, ctrls, W, avail_w, False)
    while sum(heights(panel_rows, small)) > y_back - front(fz) and fz > fz_min + 1e-9:
        fz = max(fz_min, fz - 0.002)
    if sum(heights(panel_rows, small)) > y_back - front(fz):
        small = True
        panel_rows = _wrap_rows(base_rows, ctrls, W, avail_w, True)
        while sum(heights(panel_rows, small)) > y_back - front(fz):
            best = None
            hs = heights(panel_rows, small)
            for i in range(len(panel_rows) - 1):
                merged = panel_rows[i] + panel_rows[i + 1]
                if _fits(merged, ctrls, W, avail_w, True):
                    gain = min(hs[i], hs[i + 1])
                    if best is None or gain > best[0]:
                        best = (gain, i, merged)
            if best is None:
                break
            _, i, merged = best
            panel_rows[i:i + 2] = [merged]
    hs = heights(panel_rows, small)
    avail_h = y_back - front(fz)
    if sum(hs) > avail_h + 1e-9:
        print("ATTENZIONE: %s - comandi troppo fitti (%.1f mm su %.1f)" % (r["id"], sum(hs) * 1000, avail_h * 1000))
        hs = [h * avail_h / sum(hs) for h in hs]
    extra = (avail_h - sum(hs)) / max(1, len(hs))
    out["small_knobs"] = small

    # --- footswitch
    twin = r["series"] == "twin"
    main = None
    fz_total = fz * nrow + 0.004 * max(0, nrow - 1)
    for ri, frow in enumerate(foot_rows):
        fy = -D / 2 + MARGIN + fz_total - fz / 2 - ri * (fz + 0.004)
        nf = len(frow)
        pitch = (W - 2 * MARGIN) / nf
        fw = min(pitch * (0.86 if twin else 0.62), 0.075 if twin else 0.040)
        fd = fz * (0.92 if twin else 0.64)
        for i, (typ, val) in enumerate(frow):
            x = -W / 2 + MARGIN + pitch * (i + 0.5)
            label = ctrls[val]["label"] if typ == "foot_ctrl" else val
            part = dict(type="footswitch", kind="treadle_pad" if twin else "pad", x=x, y=fy, w=fw, d=fd, label=label)
            out["parts"].append(part)
            if typ == "foot_ctrl":
                c = ctrls[val]
                if c["kind"] == "button":
                    out["controls"][val] = dict(x=x, y=fy, z=H + 0.007, strip="footbutton", r=min(fw, fd) * 0.5)
                else:
                    out["controls"][val] = dict(x=x, y=fy, z=H + 0.007, strip="footswitch", r=min(fw, fd) * 0.5,
                                                led=(x, fy + fd / 2 + 0.0045, H))
                    out["parts"].append(dict(type="led", x=x, y=fy + fd / 2 + 0.0045))
                part["control"] = True
                continue
            key = _label_key(label)
            if main is None and (key in MAIN_KEYS or key.endswith("ONOFF") or "EFFECT" in key or "BYPASS" in key):
                main = part
    if foot_rows:
        free = [p for p in out["parts"] if p["type"] == "footswitch" and not p.get("control")]
        if r["family"] == "Looper":
            rec = [p for p in out["parts"] if p["type"] == "footswitch" and "REC" in _label_key(p["label"])]
            main = rec[0] if rec else main
        main = main or (free[-1] if free else [p for p in out["parts"] if p["type"] == "footswitch"][-1])
        x, y, w, d = main["x"], main["y"], main["w"], main["d"]
        out["foot"] = [(x - w / 2, y - d / 2, H + 0.007), (x + w / 2, y - d / 2, H + 0.007),
                       (x + w / 2, y + d / 2, H + 0.007), (x - w / 2, y + d / 2, H + 0.007)]
        out["name_y"] = max(p["y"] + p["d"] / 2 for p in out["parts"] if p["type"] == "footswitch") + 0.0110
        out["name_x1"] = W / 2 - 0.008
    else:
        # nessun footswitch (unita' da tavolo): interruttore EFFECT nella fascia del nome, a destra
        x, y = W / 2 - MARGIN - 0.008, -D / 2 + MARGIN + 0.0095
        out["parts"].append(dict(type="button", x=x, y=y, r=0.0060, label="EFFECT"))
        out["foot"] = [(x - 0.008, y - 0.008, H), (x + 0.008, y - 0.008, H), (x + 0.008, y + 0.008, H), (x - 0.008, y + 0.008, H)]
        out["name_y"] = y
        out["name_x1"] = x - 0.0060 - 0.0050

    # --- pannello: file dal retro verso il davanti
    ytop = y_back
    for row, h in zip(panel_rows, hs):
        row_h = h + extra
        yc = ytop - row_h / 2
        ytop -= row_h
        kinds, widths = _row_widths(row, ctrls, W, avail_w, small)
        total = sum(widths)
        if total + ROW_GAP * len(row) > avail_w:
            # solo cursori (EQ): passo ridotto
            scale = (avail_w - ROW_GAP * len(row)) / total
            widths = [w * scale for w in widths]
            total = sum(widths)
        gap = max(ROW_GAP, (avail_w - total) / max(1, len(row)))
        x = -W / 2 + MARGIN + gap / 2
        y_lo, y_hi = yc - row_h / 2, yc + row_h / 2
        for (typ, val), w, kind in zip(row, widths, kinds):
            cx = x + w / 2
            x += w + gap
            yk = yc + ITEM[kind][2]
            if typ == "display":
                dh = max(0.010, min(row_h - 0.004, 0.030))
                part = dict(type="display", x0=cx - w / 2, y0=yc - dh / 2, x1=cx + w / 2, y1=yc + dh / 2)
                out["parts"].append(part)
                if out["display"] is None:
                    out["display"] = (part["x0"], part["y0"], part["x1"], part["y1"])
            elif typ == "led":
                out["parts"].append(dict(type="led", x=cx, y=yc))
                if out["led"] is None:
                    out["led"] = (cx, yc, H, 0.0020)
            elif typ == "button":
                out["parts"].append(dict(type="button", x=cx, y=yk, r=0.0042, label=val, w=w))
            elif typ == "deco_knob":
                rr = SMALL_R if small else KR * 0.95
                out["parts"].append(dict(type="knob", x=cx, y=yk, r=rr, h=SMALL_H if small else 0.014, label=val, w=w))
            elif typ == "deco_switch":
                out["parts"].append(dict(type="switch", x=cx, y=yk, label=val, w=w))
            elif typ == "concentric":
                co, cn = val
                out["controls"][co] = dict(x=cx, y=yk, z=H, strip="boss_outer", r=PL.KNOB_R["boss_outer"], h=PL.KNOB_H["boss_outer"], w=w)
                out["controls"][cn] = dict(x=cx, y=yk, z=H + PL.KNOB_H["boss_outer"], strip="boss_inner",
                                           r=PL.KNOB_R["boss_inner"], h=PL.KNOB_H["boss_inner"])
                out.setdefault("kinds_override", {})[co] = "outer"
                out["kinds_override"][cn] = "inner"
            elif typ == "deco_sliders":
                for j in range(val):
                    sx = cx - w / 2 + (j + 0.5) * w / val
                    out["parts"].append(dict(type="slider", x=sx, y0=y_lo + 0.0045, y1=y_hi - 0.0080))
            else:
                c = ctrls[val]
                if c["kind"] == "slider":
                    out["controls"][val] = dict(x=cx, y=y_lo + 0.0045, z=H, y1=y_hi - 0.0080, strip="slider", r=0.004, w=w)
                elif c["kind"] == "toggle":
                    out["controls"][val] = dict(x=cx, y=yk, z=H, strip="toggle", w=w)
                elif c["kind"] == "button":
                    out["controls"][val] = dict(x=cx, y=yk, z=H, strip="button", r=0.0045, w=w)
                elif small:
                    out["controls"][val] = dict(x=cx, y=yk, z=H, strip="boss_small", r=SMALL_R, h=SMALL_H, w=w)
                else:
                    out["controls"][val] = dict(x=cx, y=yk, z=H, strip="boss", r=KR, h=KH, w=w)
    if out["led"] is None:
        out["led"] = (W / 2 - MARGIN - 0.004, D / 2 - MARGIN - 0.002, H, 0.0020)
    for i, c in enumerate(out["controls"]):
        if c is None:
            raise ValueError("comando senza posizione: %s / %s" % (r["id"], ctrls[i]["label"]))
    out["frame"] = _frame(W, D, H)
    jy = D / 2 - min(0.035, D * 0.25)
    out["jacks"] = (W, [(jy, H * 0.5), (jy - 0.022, H * 0.5) if r.get("stereo") else None])
    return out


TREADLE_WORDS = ("VOLUME", "PEDAL", "EXP", "EXP1")


def treadle_layout(r):
    W, D, H = r["dims"]
    ctrls = r["controls"]
    out = dict(style=r["style"], controls=[None] * len(ctrls), led=None, toggle=None, display=None,
               sliders=None, buttons=[], panel_y0=-D / 2, tread_y1=-D / 2, builder="treadle",
               body=dict(W=W, D=D, H=H), parts=[], display_z=H)
    base_h = H * 0.42
    # comandi oltre al bilanciere: blocco sul retro piu' profondo (pomello + scritta sotto)
    n_extra = len(ctrls) - 1
    # bilanciere: dal tallone (davanti) alla punta (dietro), inclinato di ~9 gradi
    t0, t1 = -D / 2 + 0.012, D / 2 - (0.034 if n_extra > 0 else 0.030)
    ang = math.radians(9.0)
    z_heel = base_h + 0.012
    z_toe = z_heel + (t1 - t0) * math.tan(ang)
    out["treadle"] = dict(y0=t0, y1=t1, z0=z_heel, z1=z_toe, w=W - 0.012, base_h=base_h)
    tr = None
    for i, c in enumerate(ctrls):
        if _label_key(c["label"]) in TREADLE_WORDS or (tr is None and c["label"].upper().startswith(("VOLUME", "PEDAL", "EXP"))):
            tr = i
            break
    if tr is None:
        tr = 0
    ymid = (t0 + t1) / 2
    zmid = (z_heel + z_toe) / 2
    out["controls"][tr] = dict(x=0.0, y=t0 + 0.035, z=z_heel + 0.035 * math.tan(ang), y1=t1 - 0.026,
                               z1=z_toe - 0.026 * math.tan(ang), strip="treadle", r=(W - 0.02) / 2)
    others = [i for i in range(len(ctrls)) if out["controls"][i] is None]
    # blocco comandi sul retro, dietro la punta del bilanciere (MIN VOL, TYPE, DRIVE...): piu' alto della punta,
    # altrimenti nella vista inclinata la punta del bilanciere lo nasconderebbe e i pomelli finirebbero sopra di essa
    n = len(others)
    bz = base_h + 0.016
    if n:
        bz = max(bz, z_toe + 0.004)
    out["back_block"] = dict(y0=t1 + 0.002, y1=D / 2, z=bz)
    # pomelli verso il retro (la sommita' proiettata resta sul blocco), scritte sotto; LED a sinistra
    by = D / 2 - 0.016 if n else (t1 + D / 2) / 2
    x0, x1 = -W / 2 + 0.016, W / 2 - 0.006
    slot = (x1 - x0) / max(1, n)
    for j, i in enumerate(others):
        c = ctrls[i]
        x = x0 + slot * (j + 0.5) if n > 1 else W / 4
        if c["kind"] == "toggle":
            out["controls"][i] = dict(x=x, y=by, z=bz, strip="toggle", w=slot)
        else:
            out["controls"][i] = dict(x=x, y=by, z=bz, strip="boss", r=KR, h=KH, w=slot)
    # interruttore a punta (on/off) per wah e rocker, zona piccola in cima al bilanciere
    tip = t1 - 0.010
    out["foot"] = [(-W / 2 + 0.01, tip - 0.012, z_toe), (W / 2 - 0.01, tip - 0.012, z_toe),
                   (W / 2 - 0.01, tip + 0.008, z_toe), (-W / 2 + 0.01, tip + 0.008, z_toe)]
    out["led"] = (-W / 2 + 0.008, by + (0.004 if n else 0.0), bz, 0.0020)
    # inquadratura come con il bilanciere lungo (dimensioni dell'immagine e piano delle foto personali invariati)
    z_toe_frame = z_heel + (D / 2 - 0.030 - t0) * math.tan(ang)
    out["frame"] = _frame(W, D, max(z_toe, z_toe_frame) + 0.01)
    out["jacks"] = (W, [(D / 2 - 0.04, base_h * 0.55), (D / 2 - 0.062, base_h * 0.55) if r.get("stereo") else None])
    return out


def real_layout(r):
    s = r["series"]
    if s == "shaped":
        import shaped_layout          # wah e volume con la forma del pedale reale (Cry Baby, Ernie Ball, ...)
        return shaped_layout.wah_layout(r)
    if s == "stomp":
        import stomp_layout           # MXR, Electro-Harmonix...: contenitore e comandi del pedale vero
        return stomp_layout.stomp_layout(r)
    if s == "compact":
        return compact_layout(r)
    if s in ("treadle_volume", "treadle_wah", "rocker"):
        return treadle_layout(r)
    return box_layout(r)
