"""
Pedal Trinity - proiezione delle coordinate dei comandi nell'immagine del pedale
(stessa camera ortografica inclinata del render). Python puro: usato da
render_pedals.py (Blender) e da assemble_catalog.py.
"""
import math

import layout as L
import pedal_layout as PL

TILT = math.radians(L.TILT_DEG)
COS, SIN = math.cos(TILT), math.sin(TILT)
PPM = L.UI_WIDTH * L.RENDER_SCALE / (L.VIEW_X_MAX - L.VIEW_X_MIN)   # px/m del render (2x)
RS = L.RENDER_SCALE
X0, X1 = -0.047, 0.047
V0, V1 = -0.068, 0.088
RES_X = int(round((X1 - X0) * PPM))
RES_Y = int(round((V1 - V0) * PPM))
VC = (V0 + V1) / 2


def proj(x, y, z):
    """Punto locale del pedale -> pixel logici (@1x) dell'immagine."""
    v = y * COS + z * SIN - VC
    return [round((RES_X / 2 + x * PPM) / RS, 3), round((RES_Y / 2 - v * PPM) / RS, 3)]


def geometry(m, lay=None):
    lay = lay or PL.pedal_layout(m)
    data = dict(id=m["id"], imageW=RES_X / RS, imageH=RES_Y / RS, controls=[])
    for k, c in enumerate(m["controls"]):
        p = lay["controls"][k]
        e = dict(strip=PL.STRIP_ID[p["strip"]])
        if p["strip"] == "slider":
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = proj(p["x"], p["y1"], p["z"]); e["radius"] = 10.0
        elif p["strip"] == "toggle":
            e["anchor"] = proj(p["x"], p["y"], PL.PANEL_H + 0.0048); e["top"] = e["anchor"]; e["radius"] = 14.0
        elif p["strip"] == "button":
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = e["anchor"]; e["radius"] = p["r"] * PPM / RS
        else:
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = proj(p["x"], p["y"], p["z"] + p["h"])
            e["radius"] = p["r"] * PPM / RS
        data["controls"].append(e)
    lx, ly, lz, lr = lay["led"]
    if lay.get("led_on_tread"):
        lz = PL.tread_top_z(ly, lay["tread_y1"])
    data["led"] = proj(lx, ly, lz + 0.0022) + [lr * PPM / RS]
    data["foot"] = [proj(*q) for q in lay["foot"]]
    if lay.get("display"):
        x0, y0, x1, y1 = lay["display"]
        a, b = proj(x0, y1, PL.PANEL_H), proj(x1, y0, PL.PANEL_H)
        data["display"] = [a[0], a[1], b[0] - a[0], b[1] - a[1]]
    data["jacks"] = jacks(m)
    return data


# prese jack sui fianchi (come in render_pedals.add_jacks): A sempre, B solo sui pedali stereo
JACK_Y_A, JACK_Y_B, JACK_Z = 0.042, 0.020, 0.022
TS_JACK_Y, TS_JACK_Z = 0.028, 0.024


def jack_layout(m):
    """(larghezza, [(y, z) presa A, (y, z) presa B o None]) in coordinate del pedale."""
    if m.get("style") == "ts":
        return PL.TS_W, [(TS_JACK_Y, TS_JACK_Z), None]
    return PL.W, [(JACK_Y_A, JACK_Z), (JACK_Y_B, JACK_Z) if m.get("stereo") else None]


def jacks(m):
    """Bocche delle prese in pixel logici: [x ingresso (sinistra), x uscita (destra), y A, y B (= y A se mono)]."""
    w, pos = jack_layout(m)
    face = w / 2 + 0.0016
    ya = proj(-face, *pos[0])[1]
    yb = proj(-face, *pos[1])[1] if pos[1] else ya
    return [proj(-face, *pos[0])[0], proj(face, *pos[0])[0], ya, yb]
