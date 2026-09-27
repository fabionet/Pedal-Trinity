"""
Pedal Trinity - layout geometrico condiviso.

Usato sia da make_textures.py (Python di sistema + PIL) sia da render_scene.py
(Python interno di Blender). Solo Python puro: nessuna dipendenza.

Unita': metri. Coordinate locali di ogni pedale: origine al centro della base,
x verso destra, y verso il retro (in alto nello schermo), z verso l'alto.
"""

import math

# --------------------------------------------------------------------------
# Camera / immagine
# --------------------------------------------------------------------------
TILT_DEG = 22.0            # inclinazione della camera rispetto alla verticale
UI_WIDTH = 1200            # larghezza logica dell'interfaccia (px @ 1x)
RENDER_SCALE = 2           # il render e' fatto a 2x per schermi HiDPI
VIEW_X_MIN, VIEW_X_MAX = -0.170, 0.170
VIEW_V_MIN, VIEW_V_MAX = -0.071, 0.106    # coordinata "v" = y*cos + z*sin

TEX_PPM = 10000            # pixel per metro delle texture stampate

# --------------------------------------------------------------------------
# Pedali
# --------------------------------------------------------------------------
PEDAL_PITCH = 0.105

TS = dict(  # Overdrive stile Tube Screamer
    id="ed9", cx=-PEDAL_PITCH, W=0.074, D=0.125, H=0.046,
    corner=0.011, edge=0.004,
    paint=(38, 128, 62),
    knobs=[
        dict(id="od_drive", label="DRIVE", x=-0.019, y=0.036, type="ts_big"),
        dict(id="od_level", label="LEVEL", x=0.019, y=0.036, type="ts_big"),
        dict(id="od_tone", label="TONE", x=0.0, y=0.020, type="ts_small"),
    ],
    led=dict(x=0.0, y=0.052, r=0.0026),
    footswitch=dict(x=0.0, y=-0.035, w=0.050, d=0.038, h=0.009),
)

BOSS_COMMON = dict(W=0.073, D=0.129, corner=0.007, edge=0.003,
                   base_h=0.030, panel_y0=0.018, panel_h=0.050,
                   tread_y0=-0.0645, tread_y1=0.0160,
                   tread_h_front=0.036, tread_h_back=0.047,
                   rubber_y0=-0.0615, rubber_y1=-0.0215)

MT = dict(  # Distorsore stile MT-2W Waza Craft
    id="mc2", cx=0.0, **BOSS_COMMON,
    paint=(30, 30, 33), panel_paint=(18, 18, 20),
    knobs=[
        dict(id="dist_level", label="LEVEL", x=-0.0255, y=0.036, type="boss"),
        dict(id="dist_low", label="LOW", x=-0.0085, y=0.036, type="boss_outer",
             inner=dict(id="dist_high", label="HIGH", type="boss_inner")),
        dict(id="dist_mid", label="MIDDLE", x=0.0085, y=0.036, type="boss_outer",
             inner=dict(id="dist_midfreq", label="MID FREQ", type="boss_inner")),
        dict(id="dist_gain", label="DIST", x=0.0255, y=0.036, type="boss"),
    ],
    led=dict(x=-0.017, y=0.0575, r=0.0024),
    toggle=dict(id="dist_mode", x=0.017, y=0.0575),
)

EQ = dict(  # Equalizzatore grafico stile GE-7
    id="gq7", cx=PEDAL_PITCH, **BOSS_COMMON,
    paint=(196, 199, 204), panel_paint=(20, 20, 22),
    sliders=[dict(id="eq_%d" % i, label=l) for i, l in enumerate(
        ["100", "200", "400", "800", "1.6k", "3.2k", "6.4k"])] +
            [dict(id="eq_level", label="LEVEL")],
    slider_x0=-0.0273, slider_pitch=0.0078,
    slot_y0=0.0250, slot_y1=0.0530,   # corsa del cursore (centro cappuccio)
    led=dict(x=0.0265, y=0.0103, r=0.0024),
)

PEDALS = [TS, MT, EQ]

# Knob: raggio (m) e altezza (m)
KNOB_TYPES = {
    "ts_big":     dict(r=0.0096, h=0.0160),
    "ts_small":   dict(r=0.0076, h=0.0145),
    "boss":       dict(r=0.0073, h=0.0150),
    "boss_outer": dict(r=0.0079, h=0.0100),
    "boss_inner": dict(r=0.0049, h=0.0075),   # appoggiato sopra "boss_outer"
}
SLIDER_CAP = dict(w=0.0056, d=0.0088, h=0.0055)

# Targhetta in alto con il nome del prodotto e il tasto INFO
PLATE = dict(x=0.0, y=0.0955, w=0.318, d=0.0150, h=0.0022)
INFO_BUTTON = dict(x=0.143, y=0.0955, r=0.0048)


def slider_x(i):
    return EQ["slider_x0"] + i * EQ["slider_pitch"]


def boss_panel_top():
    return BOSS_COMMON["panel_h"]


def tread_top_z(y):
    """Quota della superficie superiore del pedale (treadle) Boss in y."""
    c = BOSS_COMMON
    t = (y - c["tread_y0"]) / (c["tread_y1"] - c["tread_y0"])
    return c["tread_h_front"] + t * (c["tread_h_back"] - c["tread_h_front"])


def tread_angle():
    c = BOSS_COMMON
    return math.atan2(c["tread_h_back"] - c["tread_h_front"],
                      c["tread_y1"] - c["tread_y0"])
