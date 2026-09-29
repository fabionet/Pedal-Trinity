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
# inquadratura verticale per stile (la larghezza e' la stessa per tutti: stessa scala in pixel)
FRAMES = {"boss": (-0.068, 0.088), "ts": (-0.068, 0.088), "nam": (-0.082, 0.101)}
V0, V1 = FRAMES["boss"]
RES_X = int(round((X1 - X0) * PPM))
RES_Y = int(round((V1 - V0) * PPM))
VC = (V0 + V1) / 2


def use_frame(style, box=None):
    """Imposta l'inquadratura dello stile (usata da proj e dal render).
    box = (x0, x1, v0, v1) per le repliche REAL MOD di misura diversa dal compatto."""
    global X0, X1, V0, V1, RES_X, RES_Y, VC
    if box is not None:
        X0, X1, V0, V1 = box
    else:
        X0, X1 = -0.047, 0.047
        V0, V1 = FRAMES.get(style, FRAMES["boss"])
    RES_X = int(round((X1 - X0) * PPM))
    RES_Y = int(round((V1 - V0) * PPM))
    VC = (V0 + V1) / 2


def proj(x, y, z):
    """Punto locale del pedale -> pixel logici (@1x) dell'immagine."""
    v = y * COS + z * SIN - VC
    return [round((RES_X / 2 + x * PPM) / RS, 3), round((RES_Y / 2 - v * PPM) / RS, 3)]


def geometry(m, lay=None):
    lay = lay or PL.pedal_layout(m)
    use_frame(m.get("style", "boss"), lay.get("frame"))
    data = dict(id=m["id"], imageW=RES_X / RS, imageH=RES_Y / RS, controls=[])
    for k, c in enumerate(m["controls"]):
        p = lay["controls"][k]
        e = dict(strip=PL.STRIP_ID[p["strip"]])
        if p["strip"] == "slider":
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = proj(p["x"], p["y1"], p["z"]); e["radius"] = 10.0
        elif p["strip"] == "toggle":
            e["anchor"] = proj(p["x"], p["y"], p.get("z", PL.PANEL_H) + 0.0048); e["top"] = e["anchor"]; e["radius"] = 14.0
        elif p["strip"] == "treadle":
            # bilanciere: anchor = tallone, top = punta, radius = meta' larghezza
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = proj(p["x"], p["y1"], p["z1"]); e["radius"] = p["r"] * PPM / RS
        elif p["strip"] == "footbutton":
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = e["anchor"]; e["radius"] = p["r"] * PPM / RS
        elif p["strip"] == "footswitch":
            e["anchor"] = proj(p["x"], p["y"], p["z"]); e["top"] = proj(*p["led"]); e["radius"] = p["r"] * PPM / RS
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
        dz = lay.get("display_z", PL.PANEL_H)
        a, b = proj(x0, y1, dz), proj(x1, y0, dz)
        data["display"] = [a[0], a[1], b[0] - a[0], b[1] - a[1]]
    data["jacks"] = jacks(m, lay)
    data["body"] = body_rect(lay)
    return data


def body_rect(lay):
    """Piano superiore del pedale nell'immagine [x, y, w, h] e altezza del fronte (px logici):
    dove le foto personali si appoggiano in modalita' REAL MOD."""
    b = lay.get("body") or dict(W=PL.W, D=PL.D, H=PL.PANEL_H)
    W, D = b["W"], b["D"]
    if lay.get("builder") == "treadle":
        t = lay["treadle"]
        z_back, z_front = lay["back_block"]["z"], t["z0"]
    elif lay.get("builder") == "box":
        z_back = z_front = b["H"]
    else:
        z_back, z_front = PL.PANEL_H, PL.TREAD_H_FRONT
    a = proj(-W / 2, D / 2, z_back)
    c = proj(W / 2, -D / 2, z_front)
    base = proj(W / 2, -D / 2, 0.0)
    return [a[0], a[1], c[0] - a[0], c[1] - a[1], base[1] - c[1]]


# prese jack sui fianchi (come in render_pedals.add_jacks): A sempre, B solo sui pedali stereo
JACK_Y_A, JACK_Y_B, JACK_Z = 0.042, 0.020, 0.022
TS_JACK_Y, TS_JACK_Z = 0.028, 0.024


def jack_layout(m):
    """(larghezza, [(y, z) presa A, (y, z) presa B o None]) in coordinate del pedale."""
    if m.get("style") == "ts":
        return PL.TS_W, [(TS_JACK_Y, TS_JACK_Z), None]
    if m.get("style") == "nam":
        return PL.NAM_W, [(0.040, 0.020), (0.016, 0.020)]
    return PL.W, [(JACK_Y_A, JACK_Z), (JACK_Y_B, JACK_Z) if m.get("stereo") else None]


def jacks(m, lay=None):
    """Bocche delle prese in pixel logici: [x ingresso (sinistra), x uscita (destra), y A, y B (= y A se mono)]."""
    w, pos = lay["jacks"] if lay is not None and lay.get("jacks") else jack_layout(m)
    face = w / 2 + 0.0016
    ya = proj(-face, *pos[0])[1]
    yb = proj(-face, *pos[1])[1] if pos[1] else ya
    return [proj(-face, *pos[0])[0], proj(face, *pos[0])[0], ya, yb]
