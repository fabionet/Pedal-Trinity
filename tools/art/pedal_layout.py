"""
Pedal Trinity - layout dei singoli pedali del catalogo (Python puro).

Dato un modello del catalogo calcola, in metri nelle coordinate locali del
pedale (x a destra, y verso il retro, z in alto), la posizione di ogni
comando, del LED, della levetta, dell'eventuale display e del pedale.
Usato da make_catalog_textures.py (serigrafie) e render_pedals.py (Blender).
"""

import math

# enclosure compatta tipo BOSS (stesse misure del render originale)
W, D = 0.073, 0.129
PANEL_Y0, PANEL_Y1, PANEL_H = 0.018, D / 2, 0.050
TREAD_Y0, TREAD_Y1 = -0.0645, 0.0160
TREAD_H_FRONT, TREAD_H_BACK = 0.036, 0.047

# enclosure tipo Tube Screamer
TS_W, TS_D, TS_H = 0.074, 0.125, 0.046

KNOB_R = {"ts_big": 0.0096, "ts_small": 0.0076, "boss": 0.0073, "boss_outer": 0.0079, "boss_inner": 0.0049}
KNOB_H = {"ts_big": 0.0160, "ts_small": 0.0145, "boss": 0.0150, "boss_outer": 0.0100, "boss_inner": 0.0075}
STRIP_ID = {"ts_big": 0, "ts_small": 1, "boss": 2, "boss_outer": 3, "boss_inner": 4, "slider": 5, "toggle": 6, "button": 255}

ROW_X = {1: [0.0], 2: [-0.0135, 0.0135], 3: [-0.022, 0.0, 0.022], 4: [-0.0255, -0.0085, 0.0085, 0.0255]}


# variante "pannello profondo" (pedali con due file di comandi, come le serie 200/500)
TALL_PANEL_Y0, TALL_TREAD_Y1 = -0.010, -0.012


def tread_top_z(y, y1=TREAD_Y1):
    t = (y - TREAD_Y0) / (y1 - TREAD_Y0)
    return TREAD_H_FRONT + t * (TREAD_H_BACK - TREAD_H_FRONT)


def group_positions(controls):
    """Raggruppa i comandi rotativi in posizioni fisiche: un pomello concentrico
    (outer seguito da inner) occupa una sola posizione."""
    groups, i = [], 0
    rotary = [c for c in controls if c["kind"] in ("knob", "outer", "inner", "selector")]
    while i < len(rotary):
        c = rotary[i]
        if c["kind"] == "outer" and i + 1 < len(rotary) and rotary[i + 1]["kind"] == "inner":
            groups.append([c, rotary[i + 1]]); i += 2
        else:
            groups.append([c]); i += 1
    return groups


def pedal_layout(m):
    """Restituisce il layout completo del modello m (dizionario del catalogo)."""
    style = m.get("style", "boss")
    ctrls = m["controls"]
    out = dict(style=style, controls=[None] * len(ctrls), led=None, toggle=None, display=None,
               sliders=None, buttons=[], panel_y0=PANEL_Y0, tread_y1=TREAD_Y1)
    idx = {id(c): k for k, c in enumerate(ctrls)}

    if style == "ts":
        # 2 pomelli grandi in alto + 1 piccolo al centro (come il TS808)
        pos = [(-0.019, 0.036, "ts_big"), (0.0, 0.020, "ts_small"), (0.019, 0.036, "ts_big")]
        rot = [c for c in ctrls if c["kind"] in ("knob", "selector")]
        for c, (x, y, t) in zip(rot, pos):
            out["controls"][idx[id(c)]] = dict(x=x, y=y, z=TS_H, strip=t, r=KNOB_R[t], h=KNOB_H[t])
        out["led"] = (0.0, 0.052, TS_H, 0.0026)
        fs = dict(x=0.0, y=-0.035, w=0.050, d=0.038, h=0.009)
        out["foot"] = [(fs["x"] + sx * fs["w"] / 2, fs["y"] + sy * fs["d"] / 2, TS_H + fs["h"])
                       for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        return out

    # ---- enclosure compatta
    top = PANEL_H
    sliders = [c for c in ctrls if c["kind"] == "slider"]
    buttons = [c for c in ctrls if c["kind"] == "button"]
    toggles = [c for c in ctrls if c["kind"] == "toggle"]
    groups = group_positions(ctrls)
    family = m.get("family", "")

    if sliders:
        n = len(sliders)
        x0, x1 = -0.0273, 0.0273
        pitch = (x1 - x0) / max(1, n - 1)
        y0, y1 = 0.0250, 0.0530
        for k, c in enumerate(sliders):
            x = x0 + k * pitch
            out["controls"][idx[id(c)]] = dict(x=x, y=y0, z=top, y1=y1, strip="slider", r=0.004)
        out["sliders"] = dict(x0=x0, pitch=pitch, y0=y0, y1=y1, n=n)
        # eventuali pomelli oltre ai cursori: non previsti in questo stile
    else:
        n = len(groups)
        if family == "Looper":
            rows = [(0.028, groups)]
            out["display"] = (-0.030, 0.042, 0.030, 0.060)
        elif family == "Tuner":
            rows = [(0.028, groups)]           # comandi in basso, display in alto
            out["display"] = (-0.030, 0.040, 0.030, 0.060)
        elif n <= 4:
            rows = [(0.036, groups)]
        elif n <= 6:
            rows = [(0.041, groups[:3]), (0.0135, groups[3:])]
            out["panel_y0"], out["tread_y1"] = TALL_PANEL_Y0, TALL_TREAD_Y1
        else:
            rows = [(0.041, groups[:4]), (0.0135, groups[4:8])]
            out["panel_y0"], out["tread_y1"] = TALL_PANEL_Y0, TALL_TREAD_Y1
        for y, g in rows:
            xs = ROW_X.get(len(g), ROW_X[4])
            if family == "Looper":
                xs = [0.024, 0.024 - 0.0165][:len(g)]
            for x, grp in zip(xs, g):
                if len(grp) == 2:
                    o, inn = grp
                    out["controls"][idx[id(o)]] = dict(x=x, y=y, z=top, strip="boss_outer", r=KNOB_R["boss_outer"], h=KNOB_H["boss_outer"])
                    out["controls"][idx[id(inn)]] = dict(x=x, y=y, z=top + KNOB_H["boss_outer"], strip="boss_inner",
                                                          r=KNOB_R["boss_inner"], h=KNOB_H["boss_inner"])
                else:
                    c = grp[0]
                    out["controls"][idx[id(c)]] = dict(x=x, y=y, z=top, strip="boss", r=KNOB_R["boss"], h=KNOB_H["boss"])
        two_rows = len(rows) > 1
        # pulsanti (looper): fila in alto a sinistra del display
        for k, c in enumerate(buttons):
            bx = -0.027 + k * 0.0115
            out["controls"][idx[id(c)]] = dict(x=bx, y=0.028, z=top, strip="button", r=0.0042)

    # LED e levetta
    if sliders:
        out["led"] = (0.0265, 0.0103, tread_top_z(0.0103), 0.0024)   # LED sul pedale (come il GE-7)
        out["led_on_tread"] = True
    elif out.get("display"):
        out["led"] = (0.029, 0.0625, top, 0.0020)
    elif not sliders and len(groups) > 4:
        out["led"] = (-0.017 if toggles else 0.0, 0.0585, top, 0.0023)
    else:
        out["led"] = (-0.017 if toggles else 0.0, 0.0575, top, 0.0024)
    for c in toggles:
        if len(groups) > 4:
            out["controls"][idx[id(c)]] = dict(x=0.017, y=0.0585, z=top, strip="toggle")
        else:
            out["controls"][idx[id(c)]] = dict(x=0.017, y=0.0575, z=top, strip="toggle")

    ty1 = out["tread_y1"]
    out["foot"] = [(sx * W / 2, yy, tread_top_z(yy, ty1))
                   for sx, yy in ((-1, TREAD_Y0), (1, TREAD_Y0), (1, ty1), (-1, ty1))]
    for k, c in enumerate(out["controls"]):
        if c is None:
            raise ValueError("comando senza posizione: %s / %s" % (m["id"], ctrls[k]["label"]))
    return out
