#!/usr/bin/env python3
"""
Pedal Trinity - generatore dei cataloghi di amplificatori e casse (tappa 6).

Legge le schede ~/.config/PedalTrinity/stage6/stage6_amps.json e stage6_cabs.json (misure, comandi del frontale con
posizioni in %, retro, colori) e scrive tools/art/catalog_amps.py e tools/art/catalog_cabs.py (modelli amp_catalog.amp
e amp_catalog.cab, stile "box" con look["amp"]: vedi amp_layout.py e CATALOG_SPEC.md).
Per cambiare l'aspetto si modifica questo generatore (tabelle STYLE / CAB_STYLE) e si rigenera:
    python3 tools/art/gen_catalog_amps.py
Le foto Commons delle schede sono servite solo come riferimento visivo (mai copiate): nessun logo, nessun artwork.
"""
import json
import math
import os
import pprint
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
S = os.path.expanduser("~/.config/PedalTrinity/stage6")

# ------------------------------------------------------------------ colori e materiali
BLACK = (24, 24, 26)
TOLEX = dict(col=BLACK, kind="levant")
WHITE_PIPE = (226, 222, 205)
GOLD_PIPE = (200, 165, 85)
SILVER_PIPE = (190, 192, 194)
CHROME = (205, 206, 208)

# stili dei pomelli decorativi (posizioni senza comando funzionale)
#   skirt   nero a gonna (Fender, Ampeg)      goldcap  nero con calotta dorata (Marshall)
#   chicken a puntatore crema (Vox, Matchless) pointer  a puntatore nero (tweed)
#   black   nero zigrinato                    silver   nero con inserto d'alluminio (Roland, Hiwatt)
STYLE = {
    # --------------------------------------------------------------- Fender blackface e tweed
    "amtwinreverb": dict(kind="combo", strip="ts_small", knob="skirt", dk=25,
                         panel=dict(col=(16, 16, 18), text=(236, 236, 230), finish="black", recess=8, name=(86, 50, "script")),
                         face=[dict(kind="grille", rect=(3.5, 19, 96.5, 95), style="bf_silver", piping=WHITE_PIPE)],
                         covering=TOLEX, logo=dict(x=18, y=27, font="script", col=CHROME), corners="chrome",
                         handle="strap", feet="glides", back=dict(kind="open", tube_dir="down", power_side="left")),
    "amdeluxereverb": dict(kind="combo", strip="ts_small", knob="skirt", dk=25,
                           panel=dict(col=(16, 16, 18), text=(236, 236, 230), finish="black", recess=8, name=(86, 50, "script")),
                           face=[dict(kind="grille", rect=(3.5, 20, 96.5, 95), style="bf_silver", piping=WHITE_PIPE)],
                           covering=TOLEX, logo=dict(x=20, y=29, font="script", col=CHROME), corners=None,
                           handle="strap", feet="glides", back=dict(kind="open", tube_dir="down", power_side="left")),
    "amprincetonreverb": dict(kind="combo", strip="ts_small", knob="skirt", dk=25,
                              panel=dict(col=(16, 16, 18), text=(236, 236, 230), finish="black", recess=8, name=(80, 50, "script")),
                              face=[dict(kind="grille", rect=(4, 21, 96, 94.5), style="bf_silver", piping=WHITE_PIPE)],
                              covering=TOLEX, logo=dict(x=22, y=31, font="script", col=CHROME), corners=None,
                              handle="strap", feet="glides", back=dict(kind="open", tube_dir="down", power_side="left")),
    "ambassman5f6a": dict(kind="combo", view="top", strip="ts_small", knob="pointer", dk=26,
                          panel=dict(col=(200, 202, 204), text=(20, 20, 22), finish="chrome", recess=10, name=None),
                          face=[dict(kind="grille", rect=(3, 9, 97, 91), style="oxblood", piping=None)],
                          covering=dict(col=(196, 160, 96), kind="tweed"), logo=dict(x=18, y=18, font="script", col=(236, 230, 210), plate=True),
                          corners=None, handle="leather", feet="glides", back=dict(kind="open", tube_dir="down", power_side="left")),
    # --------------------------------------------------------------- Vox, Hiwatt, Matchless, Orange
    "amac30tb": dict(kind="combo", view="top", strip="ts_small", knob="chicken", dk=28,
                     panel=dict(col=(176, 108, 70), text=(24, 18, 14), finish="copper", recess=14, name=None),
                     face=[dict(kind="grille", rect=(3, 17, 97, 96), style="diamond", piping=GOLD_PIPE, band=11)],
                     covering=dict(col=(26, 26, 30), kind="vinyl"), logo=dict(x=50, y=25, font="block", col=(222, 200, 140)),
                     corners="brass", handle="leather", feet="glides", vents=True, back=dict(kind="open", tube_dir="down", power_side="right")),
    "amac15": dict(kind="combo", view="top", strip="ts_small", knob="chicken", dk=28,
                   panel=dict(col=(176, 108, 70), text=(24, 18, 14), finish="copper", recess=14, name=None),
                   face=[dict(kind="grille", rect=(3, 17, 97, 96), style="diamond", piping=GOLD_PIPE, band=11)],
                   covering=dict(col=(26, 26, 30), kind="vinyl"), logo=dict(x=50, y=26, font="block", col=(222, 200, 140)),
                   corners="brass", handle="leather", feet="glides", vents=True, back=dict(kind="open", tube_dir="down", power_side="right")),
    "amdr103": dict(kind="head", strip="ts_small", knob="silver", dk=24,
                    panel=dict(col=(44, 46, 50), text=(232, 232, 228), finish="black", recess=10, name=(50, 14, "block")),
                    face=[dict(kind="grille", rect=(4, 7, 96, 52), style="fine_dark", piping=(150, 150, 150))],
                    covering=TOLEX, logo=dict(x=50, y=28, font="block", col=(235, 235, 235), plate=True, platecol=(40, 70, 120)),
                    corners="black", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="left")),
    "amdc30": dict(kind="combo", strip="ts_small", knob="chicken", dk=26,
                   panel=dict(col=(20, 20, 22), text=(230, 230, 225), finish="black", recess=10, name=None),
                   face=[dict(kind="grille", rect=(4, 24, 96, 95), style="silver_black", piping=SILVER_PIPE)],
                   covering=TOLEX, logo=dict(x=50, y=32, font="block", col=(240, 200, 120), plate=True, platecol=(30, 24, 20)),
                   corners="chrome", handle="strap", feet="rubber", back=dict(kind="open", tube_dir="down", power_side="left")),
    "amor120": dict(kind="head", strip="ts_small", knob="black", dk=26, dims=(610, 290, 280),
                    panel=dict(col=(232, 108, 28), text=(22, 18, 14), finish="orange", recess=8, name=None, icons=True),
                    face=[dict(kind="grille", rect=(5, 8, 95, 48), style="basket_beige", piping=GOLD_PIPE, frame=3)],
                    covering=dict(col=(226, 98, 22), kind="vinyl"), logo=dict(x=50, y=27, font="round", col=(232, 108, 28), plate=True, platecol=(24, 24, 24)),
                    corners="black", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    # --------------------------------------------------------------- Marshall (pannello in basso, logo sulla tela in alto)
    "amjtm45": dict(kind="head", strip="ts_small", knob="goldcap", dk=23, panel_rect=(3.5, 70, 96.5, 90),
                    panel=dict(col=(210, 172, 82), text=(20, 16, 10), finish="plexi", recess=10, name=(3, 50, "blockl")),
                    face=[dict(kind="grille", rect=(4, 8, 96, 64), style="black_silver", piping=WHITE_PIPE)],
                    covering=TOLEX, logo=dict(x=50, y=34, font="block", col=(240, 240, 236), plate=True, platecol=(30, 30, 30)),
                    corners="chrome", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    "am1959slp": dict(kind="head", strip="ts_small", knob="goldcap", dk=23, panel_rect=(3.5, 70, 96.5, 90),
                      panel=dict(col=(212, 176, 86), text=(20, 16, 10), finish="plexi", recess=10, name=(3, 50, "blockl")),
                      face=[dict(kind="grille", rect=(4, 8, 96, 64), style="black_silver", piping=WHITE_PIPE)],
                      covering=TOLEX, logo=dict(x=50, y=34, font="script", col=(242, 242, 238)),
                      corners="chrome", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    "amjcm800": dict(kind="head", strip="ts_small", knob="goldcap", dk=23, panel_rect=(3.5, 68, 96.5, 88),
                     panel=dict(col=(206, 170, 88), text=(20, 16, 10), finish="brushed_gold", recess=10, name=None,
                                box=(1.5, 12, 22.5, 88)),
                     face=[dict(kind="grille", rect=(4, 8, 96, 62), style="black_fine", piping=WHITE_PIPE)],
                     covering=TOLEX, logo=dict(x=50, y=33, font="script", col=(244, 244, 240), big=True),
                     corners="chrome", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    "amjcm900": dict(kind="head", strip="ts_small", knob="goldcap", dk=23, panel_rect=(3.5, 68, 96.5, 88),
                     panel=dict(col=(206, 170, 88), text=(20, 16, 10), finish="brushed_gold", recess=10, name=None,
                                box=(12.5, 12, 18.5, 88)),
                     face=[dict(kind="grille", rect=(4, 8, 96, 62), style="black_fine", piping=WHITE_PIPE)],
                     covering=TOLEX, logo=dict(x=50, y=33, font="script", col=(244, 244, 240), big=True),
                     corners="chrome", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    "amjubilee": dict(kind="head", strip="ts_small", knob="goldcap", dk=23, panel_rect=(3.5, 68, 96.5, 88),
                      panel=dict(col=(196, 198, 202), text=(20, 20, 22), finish="chrome", recess=10, name=(30, 50, "blockl")),
                      face=[dict(kind="grille", rect=(4, 8, 96, 62), style="black_fine", piping=(200, 200, 204))],
                      covering=dict(col=(176, 176, 180), kind="silver"), logo=dict(x=50, y=33, font="script", col=(244, 244, 240), big=True),
                      corners="black", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    # --------------------------------------------------------------- Mesa, Dumble
    "ammark2cplus": dict(kind="head", strip="boss", knob="black", dk=21, panel_rect=(4, 9, 96, 41),
                         panel=dict(col=(18, 18, 20), text=(232, 232, 226), finish="black", recess=10, name=None),
                         face=[dict(kind="grille", rect=(4, 47, 96, 94), style="wicker", piping=None)],
                         covering=dict(col=(22, 22, 24), kind="bronco"), logo=dict(x=14, y=58, font="block", col=(236, 236, 230), plate=True, platecol=(20, 20, 20)),
                         corners="chrome", handle="leather", feet="rubber", back=dict(kind="closed", tube_dir="up", power_side="right")),
    "amdualrecto": dict(kind="head", strip="ts_small", knob="black", dk=23, panel_rect=(2.5, 70, 97.5, 93),
                        panel=dict(col=(14, 14, 16), text=(226, 226, 222), finish="black", recess=6, name=None),
                        face=[dict(kind="diamond", rect=(0, 0, 100, 67), slots=(24, 30, 76, 48))],
                        covering=dict(col=(22, 22, 24), kind="bronco"), logo=dict(x=50, y=17, font="block", col=(210, 210, 214), plate=True, platecol=(22, 22, 24)),
                        corners="leather", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="left")),
    "amdumbleods": dict(kind="head", strip="ts_small", knob="silver", dk=23, dims=(660, 280, 260), panel_rect=(3, 72, 97, 92),
                        panel=dict(col=(18, 18, 20), text=(226, 226, 222), finish="black", recess=8, name=(86, 18, "small")),
                        face=[dict(kind="grille", rect=(4, 8, 96, 66), style="black_fine", piping=(120, 120, 124))],
                        covering=TOLEX, logo=dict(x=50, y=36, font="script", col=(226, 226, 222)),
                        corners=None, handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="left")),
    # --------------------------------------------------------------- Peavey, Soldano, Friedman
    "am5150": dict(kind="head", strip="ts_small", knob="black", dk=23, panel_rect=(3, 68, 97, 92),
                   panel=dict(col=(20, 20, 22), text=(222, 222, 218), finish="black", recess=8, name=(88, 22, "blockl")),
                   face=[dict(kind="grille", rect=(4, 7, 96, 62), style="mesh", piping=None)],
                   covering=TOLEX, logo=dict(x=50, y=33, font="block", col=(222, 222, 226), plate=True, platecol=(40, 40, 44)),
                   corners="black", handle="strap", feet="rubber", back=dict(kind="closed", tube_dir="down", power_side="right")),
    "amslo100": dict(kind="head", strip="ts_small", knob="black", dk=23, panel_rect=(4, 70, 96, 92),
                     panel=dict(col=(200, 202, 206), text=(20, 20, 22), finish="chrome", recess=8, name=None),
                     face=[dict(kind="grille", rect=(4, 8, 96, 64), style="black_fine", piping=None)],
                     covering=TOLEX, logo=dict(x=28, y=24, font="script", col=(214, 216, 220), plate=True, platecol=(30, 30, 30)),
                     corners="chrome", handle="strap", feet="rubber", back=dict(kind="closed", tube_dir="down", power_side="right")),
    "ambe100": dict(kind="head", strip="ts_small", knob="black", dk=23,
                    panel=dict(col=(196, 160, 88), text=(18, 14, 8), finish="brushed_gold", recess=6, name=None),
                    face=[dict(kind="plain", rect=(2, 4, 98, 96))],
                    covering=TOLEX, logo=dict(x=15, y=50, font="script", col=(206, 172, 96)), piping_all=GOLD_PIPE,
                    corners="black", handle="strap", feet="rubber", back=dict(kind="vent", tube_dir="down", power_side="right")),
    # --------------------------------------------------------------- Bogner, ENGL, Diezel
    "amecstasy": dict(kind="head", strip="ts_small", knob="black", dk=22, panel_rect=(2.5, 58, 97.5, 93),
                      panel=dict(col=(198, 200, 204), text=(18, 18, 20), finish="chrome", recess=8, name=None),
                      face=[dict(kind="grille", rect=(4, 8, 96, 52), style="saltpepper", piping=SILVER_PIPE)],
                      covering=TOLEX, logo=dict(x=50, y=30, font="script", col=(232, 232, 236)),
                      corners="chrome", handle="strap", feet="rubber", back=dict(kind="closed", tube_dir="down", power_side="right")),
    "ampowerball": dict(kind="head", strip="ts_small", knob="black", dk=21, panel_rect=(2.5, 44, 97.5, 95),
                        panel=dict(col=(30, 30, 33), text=(214, 214, 210), finish="black", recess=8, name=(84, 30, "blockl")),
                        face=[dict(kind="grille", rect=(4, 6, 96, 38), style="black_fine", piping=None)],
                        covering=TOLEX, logo=dict(x=50, y=22, font="block", col=(226, 226, 230), plate=True, platecol=(36, 36, 40)),
                        corners="black", handle="strap", feet="rubber", back=dict(kind="closed", tube_dir="down", power_side="right")),
    "amvh4": dict(kind="head", strip="ts_small", knob="black", dk=21, panel_rect=(2.5, 44, 97.5, 95),
                  panel=dict(col=(178, 180, 184), text=(20, 20, 22), finish="brushed", recess=8, name=None),
                  face=[dict(kind="grille", rect=(4, 6, 96, 38), style="mesh", piping=None)],
                  covering=TOLEX, logo=dict(x=50, y=22, font="gothic", col=(230, 230, 232), plate=True, platecol=(26, 26, 28)),
                  corners="black", handle="strap", feet="rubber", back=dict(kind="closed", tube_dir="down", power_side="right")),
    # --------------------------------------------------------------- Roland, Ampeg, Gallien-Krueger
    "amjc120": dict(kind="combo", strip="ts_small", knob="silver", dk=24,
                    panel=dict(col=(176, 178, 182), text=(18, 18, 20), finish="brushed", recess=6, name=None, plate=(88.5, 50)),
                    face=[dict(kind="grille", rect=(3, 18, 97, 95.5), style="black_silver", piping=None)],
                    covering=dict(col=(26, 26, 28), kind="vinyl"), logo=dict(x=14, y=25, font="block", col=(220, 220, 224), plate=True, platecol=(30, 30, 32)),
                    corners="chrome", handle="side", feet="casters", back=dict(kind="closed", tube_dir=None, power_side="left")),
    "amsvt": dict(kind="head", strip="ts_small", knob="skirt", dk=25,
                  panel=dict(col=(186, 188, 192), text=(20, 40, 110), finish="brushed", recess=8, name=None, lines=(40, 80, 170)),
                  face=[dict(kind="grille", rect=(4, 38, 96, 95), style="silver_black", piping=None)],
                  covering=TOLEX, logo=dict(x=22, y=62, font="script", col=(232, 232, 236), plate=True, platecol=(24, 24, 26)),
                  corners="chrome", handle="side", feet="rubber", back=dict(kind="cage", tube_dir="up", power_side="center")),
    "amb15n": dict(kind="flip", view="top", strip="ts_small", knob="skirt", dk=25, head_h=300,
                   panel=dict(col=(26, 30, 40), text=(220, 226, 236), finish="lucite", recess=6, name=None),
                   face=[dict(kind="grille", rect=(8, 44, 92, 96), style="silver_grey", piping=WHITE_PIPE, double=True)],
                   covering=dict(col=(62, 98, 150), kind="check"), logo=dict(x=26, y=55, font="script", col=(236, 236, 240)),
                   corners=None, handle="side", feet="casters", back=dict(kind="flip", tube_dir=None, power_side="left")),
    "amgk800rb": dict(kind="rack", strip="boss", knob="black", dk=20, panel_rect=(0, 0, 100, 100),
                      panel=dict(col=(20, 20, 22), text=(232, 232, 228), finish="black", recess=0, name=(9, 82, "blockl")),
                      face=[], covering=dict(col=(30, 30, 32), kind="metal"), logo=None,
                      corners=None, handle="rack", feet="rubber", back=dict(kind="rack", tube_dir=None, power_side="right")),
}

# posizioni stimate per gli ampli senza posizioni nelle schede (nessuna foto libera): dal manuale e dall'ordine dei
# comandi descritto nelle schede (vedi le note delle schede); x/y in % del pannello
MANUAL_ENTRIES = {
    "amecstasy": (
        [("STANDBY", "standby", 3, 62), ("POWER", "power", 7, 62),
         ("PRESENCE A", "knob", 12, 66, "PRESENCE A", "finale"), ("PRESENCE B", "knob", 17, 66, "PRESENCE B", "finale"),
         ("EXCURSION", "mini", 12, 26, "EXCURSION A / EXCURSION B", "finale"), ("PRES B SEL", "mini", 17, 26, "PRESENCE B SELECT", "finale"),
         ("PRE EQ 1", "mini", 24, 26, "PRE EQ 1", "Ch1"), ("BOOST 1", "mini", 28, 26, "GAIN BOOST CH.1", "Ch1"),
         ("GAIN 1", "knob", 24, 66, "GAIN 1", "Ch1"), ("BASS", "knob", 29, 66, "BASS 1", "Ch1"), ("MIDDLE", "knob", 34, 66, "MIDDLE 1", "Ch1"),
         ("TREBLE", "knob", 39, 66, "TREBLE 1", "Ch1"), ("VOL. 1", "knob", 44, 66, "VOL. 1", "Ch1"),
         ("BOOST", "mini", 50, 26, "GAIN BOOST CH. 2 & 3", "Ch2/Ch3"), ("STRUCTURE", "mini", 54.5, 26, "STRUCTURE", "Ch2/Ch3"),
         ("PLEXI", "mini", 59, 26, "PLEXI", "Ch2/Ch3"), ("PRE EQ 2", "mini", 64, 26, "PRE EQ 2", "Ch2"), ("PRE EQ 3", "mini", 68.5, 26, "PRE EQ 3", "Ch3"),
         ("GAIN 2", "knob", 50, 66, "GAIN 2", "Ch2"), ("GAIN 3", "knob", 55, 66, "GAIN 3", "Ch3"), ("BASS", "knob", 60, 66, "BASS", "Ch2/Ch3"),
         ("MIDDLE", "knob", 65, 66, "MIDDLE", "Ch2/Ch3"), ("TREBLE", "knob", 70, 66, "TREBLE", "Ch2/Ch3"),
         ("VOL. 2", "knob", 75, 66, "VOL. 2", "Ch2"), ("VOL. 3", "knob", 80, 66, "VOL. 3", "Ch3"),
         ("CH1", "led", 21, 44), ("CH2", "led", 47.5, 44), ("CH3", "led", 85, 44),
         ("INPUT H", "jack", 90, 66), ("INPUT L", "jack", 95, 66)]),
    "amvh4": (
        [("IN", "jack", 3, 34), ("THRU", "jack", 3, 74),
         ("GAIN", "knob", 9, 28, "GAIN (Ch1)", "Ch1"), ("BASS", "knob", 14, 28, "BASS (Ch1)", "Ch1"), ("MIDDLE", "knob", 19, 28, "MIDDLE (Ch1)", "Ch1"),
         ("TREBLE", "knob", 24, 28, "TREBLE (Ch1)", "Ch1"), ("VOLUME", "knob", 29, 28, "VOLUME (Ch1)", "Ch1"),
         ("GAIN", "knob", 37, 28, "GAIN (Ch2)", "Ch2"), ("BASS", "knob", 42, 28, "BASS (Ch2)", "Ch2"), ("MIDDLE", "knob", 47, 28, "MIDDLE (Ch2)", "Ch2"),
         ("TREBLE", "knob", 52, 28, "TREBLE (Ch2)", "Ch2"), ("VOLUME", "knob", 57, 28, "VOLUME (Ch2)", "Ch2"),
         ("GAIN", "knob", 9, 74, "GAIN (Ch3)", "Ch3"), ("BASS", "knob", 14, 74, "BASS (Ch3)", "Ch3"), ("MIDDLE", "knob", 19, 74, "MIDDLE (Ch3)", "Ch3"),
         ("TREBLE", "knob", 24, 74, "TREBLE (Ch3)", "Ch3"), ("VOLUME", "knob", 29, 74, "VOLUME (Ch3)", "Ch3"),
         ("GAIN", "knob", 37, 74, "GAIN (Ch4)", "Ch4"), ("BASS", "knob", 42, 74, "BASS (Ch4)", "Ch4"), ("MIDDLE", "knob", 47, 74, "MIDDLE (Ch4)", "Ch4"),
         ("TREBLE", "knob", 52, 74, "TREBLE (Ch4)", "Ch4"), ("VOLUME", "knob", 57, 74, "VOLUME (Ch4)", "Ch4"),
         ("CH1", "button", 64, 22, "CH1", None), ("CH2", "button", 68, 22, "CH2", None),
         ("CH3", "button", 64, 50, "CH3", None), ("CH4", "button", 68, 50, "CH4", None),
         ("PRESENCE", "knob", 75, 28, "PRESENCE", "finale"), ("DEEP", "knob", 80.5, 28, "DEEP", "finale"),
         ("MASTER", "knob", 75, 74, "MASTER", "finale"), ("MIX", "knob", 80.5, 74, "MIX", "loop"),
         ("BRIGHT", "mini", 33, 50, "BRIGHT (Ch1/Ch2)", "Ch1/Ch2"),
         ("INSERT", "button", 64, 78, "INSERT", None), ("MUTE", "button", 68, 78, "MUTE", None),
         ("POWER", "power", 90, 50), ("STANDBY", "standby", 95, 50), ("LED", "led", 86, 30)]),
}

# ritocchi di singole posizioni (etichette lunghe accorciate per la serigrafia, comandi spostati)
PRINT = {"VIB-TREM SPEED": "SPEED", "VIB-TREM (selettore)": "VIB-TREM", "VIB-TREM VOLUME": "VOLUME",
         "NORMAL VOLUME": "VOLUME", "BRILLIANT VOLUME": "VOLUME", "TONE (cut)": "CUT", "MASTER VOLUME": "MASTER",
         "PRE-AMP VOLUME": "PRE-AMP", "LEAD GAIN (PREAMP CH. B)": "LEAD GAIN", "GAIN (PREAMP CH. A)": "GAIN",
         "VOLUME (MASTER CH. B)": "VOLUME", "VOLUME (MASTER CH. A)": "VOLUME", "REVERB (CH. B)": "REVERB",
         "REVERB (CH. A)": "REVERB", "CH. B PUSH ON": "CH. B", "OUTPUT POWER": "OUTPUT", "OUTPUT MASTER": "OUTPUT MASTER",
         "LEAD MASTER": "LEAD MASTER", "INPUT GAIN": "INPUT GAIN", "VOLUME 1 / PULL BRIGHT": "VOLUME 1",
         "TREBLE / PULL SHIFT": "TREBLE", "BASS / PULL SHIFT": "BASS", "MASTER / PULL DEEP": "MASTER",
         "LEAD DRIVE / PULL BRIGHT": "LEAD DRIVE", "LEAD MASTER / PULL LEAD": "LEAD MASTER", "EQ (OUT / IN / AUTO)": "EQ",
         "CLEAN / VINTAGE": "CLN/VIN", "CHANNEL STYLE (VINTAGE / MODERN)": "VIN/MOD", "OUTPUT (LOOP ACTIVE MASTER)": "OUTPUT",
         "POWER (SPONGY / OFF / BOLD)": "POWER", "ROCK / JAZZ": "ROCK/JAZZ", "MANUAL / PEDAL": "MAN/PED",
         "PRE GAIN (Rhythm)": "PRE GAIN", "POST GAIN (Rhythm)": "POST GAIN", "PRE GAIN (Lead)": "PRE GAIN",
         "POST GAIN (Lead)": "POST GAIN", "RHYTHM / LEAD": "RHY/LEAD", "NORMAL (preamp)": "NORMAL",
         "OVERDRIVE (preamp)": "OVERDRIVE", "NORMAL / OVERDRIVE": "NOR/OD", "NORMAL (master)": "NORMAL",
         "OVERDRIVE (master)": "OVERDRIVE", "CLEAN VOLUME": "CLEAN VOL", "TREBLE (Clean)": "TREBLE", "BASS (Clean)": "BASS",
         "CLN / BE / HBE": "CLN/BE/HBE", "F.A.C. (icona: onda sonora)": "F.A.C.", "Hz / bass (icona: chiave di basso)": "BASS",
         "KHz / treble (icona: chiave di violino)": "TREBLE", "HF DRIVE (icona: pugno)": "HF DRIVE",
         "GAIN (icona: altoparlante che spinge aria)": "GAIN", "CH1 VOLUME": "VOLUME", "CH1 BASS": "BASS",
         "CH1 TREBLE": "TREBLE", "CH2 VOLUME": "VOLUME", "CH2 TONE": "TONE", "MASTER (push/pull)": "MASTER",
         "VOLUME (Normal)": "VOLUME", "VOLUME (Bright)": "VOLUME", "LOUDNESS I": "LOUDNESS I", "LOUDNESS II": "LOUDNESS II",
         "LOUDNESS 1": "LOUDNESS 1", "LOUDNESS 2": "LOUDNESS 2", "MID FREQ 1-2-3": "1-2-3", "BASS CUT / OFF / ULTRA LO": "ULTRA LO",
         "CHORUS/OFF/VIBRATO": "CHO/VIB", "VIBRATO SPEED": "SPEED", "VIBRATO DEPTH": "DEPTH", "POWER/POLARITY": "POWER",
         "BOOST (footswitch)": "FOOTSW", "FREQ (crossover)": "X-OVER", "HIGH (master 100 W)": "HIGH", "LOW (master 300 W)": "LOW",
         "Ch I Gain": "GAIN", "Bright": "BRIGHT", "Bottom": "BOTTOM", "Bass": "BASS", "Middle": "MIDDLE",
         "Treble-Clean": "TREBLE CLN", "Treble-Crunch": "TREBLE CR", "Clean Volume": "CLEAN VOL", "Crunch Volume": "CRUNCH VOL",
         "Presence": "PRESENCE", "Depth Punch": "DEPTH", "Ch II Gain": "GAIN", "Lo Lead Bottom": "BOTTOM", "Hi Lead Bottom": "BOTTOM",
         "Mid-open": "MID OPEN", "Mid-focused": "MID FOCUS", "Treble": "TREBLE", "Lo Lead Vol.": "LO LEAD", "Hi Lead Vol.": "HI LEAD",
         "open/focused": "OPEN/FOC", "Ch I/II": "CH I/II", "Gain Lo/Hi": "LO/HI", "Master A": "MASTER A", "Master B": "MASTER B",
         "ON (AC SWITCH)": "ON", "GROUND SWITCH": "GROUND", "INPUT BRIGHT / NORMAL (Ch.1)": "INPUTS", "INPUT BRIGHT / NORMAL (Ch.2)": "INPUTS",
         "INPUT 1 / 2": "INPUT", "INPUT 3": "INPUT", "INPUT (NOR)": "NOR", "INPUT (FET)": "FET"}
# etichette stampate in REAL MOD quando il pannello vero usa icone (OR120: solo pittogrammi)
ICONS = {"F.A.C.": "wave", "BASS": "bassclef", "TREBLE": "trebleclef", "HF DRIVE": "fist", "GAIN": "speaker"}

POWER_LABELS = ("POWER", "MAINS", "ON", "AC SWITCH", "MAINS ON")
STANDBY_LABELS = ("STANDBY", "STAND-BY", "STAND BY", "H.T.", "STANDBY ON", "STAND-BY ON")


def kind_of(t, label):
    t = (t or "").lower()
    base = re.sub(r"\(.*?\)", "", label.upper()).strip()
    if "retro" in t:
        return None
    if any(base == p or base.startswith(p + " ") for p in STANDBY_LABELS) or base.startswith("STANDBY"):
        return "standby"
    if base in POWER_LABELS or (base.startswith("POWER") and "SELECT" not in base) or base.startswith("MAINS"):
        return "knob" if "pomello" in t else "power"
    if "cursore" in t:
        return "slider"
    if "presa" in t or "jack" in t:
        return "jack"
    if "selettore" in t or "a scatti" in t:
        return "selector"
    if "mini" in t:
        return "mini"
    if "pulsant" in t:
        return "button"
    if "levett" in t or "interruttore" in t:
        return "toggle"
    return "knob"


def print_label(label):
    if label in PRINT:
        return PRINT[label]
    s = re.sub(r"\(.*?\)", "", label).strip()
    s = s.split(" / ")[0].strip()
    return s.upper()[:12]


def amp_entries(a, st):
    """Posizioni del pannello vero (comandi, levette, spie, ingressi) in % del pannello."""
    if a["id"] in MANUAL_ENTRIES:
        out = []
        for e in MANUAL_ENTRIES[a["id"]]:
            lab, k, x, y = e[:4]
            key = e[4] if len(e) > 4 else lab
            ch = e[5] if len(e) > 5 else None
            out.append(dict(label=lab, key=key, ch=ch, kind=k, x=float(x), y=float(y)))
        return out
    f = a["appearance"]["front"]
    out = []
    for c in a["controls"]:
        if c.get("x_pct") is None or c.get("y_pct") is None:
            continue
        k = kind_of(c["type"], c["label"])
        if k is None:
            continue
        out.append(dict(label=print_label(c["label"]), key=c["label"], ch=c.get("channel"), kind=k,
                        x=float(c["x_pct"]), y=float(c["y_pct"])))

    def near(x, y, d=2.5):
        return any(abs(e["x"] - x) < d and abs(e["y"] - y) < 12 for e in out)
    for s in f.get("switches") or []:
        if s.get("x_pct") is None or near(s["x_pct"], s["y_pct"] or 50):
            continue
        lab = s["label"]
        k = kind_of(s.get("type") or "levetta", lab)
        if "FUSE" in lab.upper():
            k = "fuse"
        out.append(dict(label=print_label(lab), key=lab, ch=None, kind=k or "toggle", x=float(s["x_pct"]), y=float(s["y_pct"] or 50)))
    for l in f.get("lamps") or []:
        if l.get("x_pct") is None:
            continue
        lab = l["label"].lower()
        k = "led" if ("led" in lab or "spie" in lab or "canale" in lab) else "lamp"
        out.append(dict(label="", key=l["label"], ch=None, kind=k, x=float(l["x_pct"]), y=float(l["y_pct"] or 50)))
    for j in f.get("inputs") or []:
        if j.get("x_pct") is None or near(j["x_pct"], j["y_pct"] or 50, 1.5):
            continue
        lab = j["label"]
        n = 2 if re.search(r"1\s*/\s*2|HIGH\s*/\s*LO|Hi/Lo|H\s*/\s*L|High\s*/\s*Low|\(x2|/", lab, re.I) else 1
        for q in range(n):
            out.append(dict(label="INPUT" if q == 0 else "", key=lab, ch=None, kind="jack",
                            x=float(j["x_pct"]) + (q - (n - 1) / 2) * 3.2, y=float(j["y_pct"] or 50)))
    return out


# ------------------------------------------------------------------ valvole e retro
POWER_T = ("EL34", "6L6GC", "6L6", "5881", "KT66", "KT88", "6550", "6550A", "EL84", "6V6GT", "6V6", "6CA7")
RECT_T = ("GZ34", "5AR4", "5U4GB", "5U4", "EZ81", "5Y3", "5V4")
PRE_T = ("12AX7", "ECC83", "12AT7", "ECC81", "12AU7", "ECC82", "7025", "EF86", "12AY7", "6SL7", "12DW7", "12BH7", "6C4")


def parse_tubes(text, side):
    t = (text or "").upper().replace("×", "X")
    found = []
    for name in sorted(POWER_T + RECT_T + PRE_T, key=len, reverse=True):
        for m in re.finditer(r"(?:(\d+)\s*X\s*)?" + re.escape(name) + r"(?:\s*X\s*(\d+))?", t):
            n = int(m.group(1) or m.group(2) or 1)
            found.append((m.start(), name, min(n, 6)))
        t = t.replace(name, " " * len(name))
    found.sort()
    power = sum(n for _, nm, n in found if nm in POWER_T)
    pre = sum(n for _, nm, n in found if nm in PRE_T)
    rect = sum(n for _, nm, n in found if nm in RECT_T)
    if power == 0 and pre == 0:
        return []
    pname = next((nm for _, nm, n in found if nm in POWER_T), "6L6")
    pre = max(pre, 2)
    out = []
    # potenza da una parte, preampli dall'altra (vista da dietro)
    pw = [("power", pname)] * max(power, 2) + [("rect", "GZ34")] * min(rect, 2)
    pr = [("pre", "12AX7")] * min(pre, 7)
    if side == "right":
        seq = pr[::-1] + pw[::-1]
    elif side == "center":
        seq = pr[:len(pr) // 2] + pw + pr[len(pr) // 2:]
    else:
        seq = pw + pr
    n = len(seq)
    for k, (cls, nm) in enumerate(seq):
        out.append((cls, nm, round(6 + 88 * (k + 0.5) / n, 1)))
    return out


def back_elems(b):
    out = []
    els = b.get("elements") or []
    nn = [e for e in els if e.get("x_pct") is None]
    for k, e in enumerate(els):
        t = (e.get("type") or "").lower()
        lab = e["label"]
        if "xlr" in t:
            kind = "xlr"
        elif "speakon" in lab.lower() or "din" in t or "sub-d" in t:
            kind = "round"
        elif "fusib" in t:
            kind = "fuse"
        elif "cavo" in t:
            kind = "cord"
        elif "iec" in t or ("presa" in t and ("MAINS" in lab.upper() or "AC SOCKET" in lab.upper() or "rete" in lab.lower())):
            kind = "iec"
        elif "selettor" in t or "pomello" in t or "manopola" in t or "trimmer" in t:
            kind = "selector"
        elif "levett" in t or "interrutt" in t or "deviator" in t:
            kind = "toggle"
        elif "etichett" in t or "targhett" in t:
            kind = "label"
        elif "punti" in t:
            kind = "fuse"
        else:
            kind = "jack2" if ("prese" in t or "x2" in t or "(2" in lab) else "jack"
        x, y = e.get("x_pct"), e.get("y_pct")
        if x is None:
            i = nn.index(e)
            x, y = 8 + 84 * (i + 0.5) / len(nn), 50
        short = re.sub(r"\(.*?\)", "", lab).strip().upper()
        short = short.replace("SPEAKERS", "SPEAKER").replace("LOUDSPEAKER", "SPEAKER")
        out.append((short[:16], kind, float(x), float(y if y is not None else 50)))
    return out


# ------------------------------------------------------------------ comandi segnaposto (finche' manca il JSON funzionale)
def short(s):
    import stomp_catalog
    return stomp_catalog.short_label(s)


ROLE_OF = [("PRESENCE", "presence"), ("RESONANCE", "resonance"), ("DEEP", "resonance"), ("DEPTH", "depth"), ("INTENSITY", "depth"),
           ("SPEED", "rate"), ("REVERB", "reverb"), ("TREBLE", "treble"), ("HIGH", "treble"), ("MIDDLE", "mid"), ("MID", "mid"),
           ("BASS", "bass"), ("LOW", "bass"), ("MASTER", "master"), ("OUTPUT", "master"), ("GAIN", "gain"), ("DRIVE", "gain"),
           ("PRE", "gain"), ("VOLUME", "gain"), ("LOUDNESS", "gain"), ("NORMAL", "gain"), ("OVERDRIVE", "gain"), ("CUT", "cut"),
           ("TONE", "tone"), ("BRIGHT", "bright")]


def role_of(label):
    L = label.upper()
    for k, r in ROLE_OF:
        if k in L:
            return r
    return "level"


def placeholder_amp(a, entries, inc):
    from catalog import knob, toggle, selector
    rot, sw = [], []
    for e in entries:
        if e["kind"] in ("knob", "selector"):
            ch = str(e.get("ch") or "").lower()
            ok = not ch or not inc or any(i in ch or ch in i for i in inc) or ch in ("comune", "tutti", "finale", "none", "unico", "selezione")
            if ok:
                rot.append(e)
        elif e["kind"] in ("toggle", "mini"):
            sw.append(e)
    seen = set()
    ctrls = []
    for e in rot:
        lab = short(e["label"]) or "KNOB"
        if lab in seen and len(rot) > 6:
            continue
        seen.add(lab)
        ctrls.append(knob(lab, role_of(lab), 0.5))
        if len(ctrls) >= 10:
            break
    for e in sw[:2]:
        if len(ctrls) >= 12:
            break
        ctrls.append(toggle(short(e["label"]) or "SW", "bright" if "BRIGHT" in e["label"].upper() else "mode", ("OFF", "ON"), 0))
    if not ctrls:
        ctrls = [knob("VOLUME", "gain", 0.5), knob("TONE", "tone", 0.5)]
    roles = [c["role"] for c in ctrls]

    def idx(*rs):
        for r in rs:
            if r in roles:
                return roles.index(r)
        return None
    g, t, m, b, lv = idx("gain"), idx("treble", "tone"), idx("mid"), idx("bass"), idx("master")
    T = "taper(%d,B)" % t if t is not None else "0.5"
    M = "taper(%d,B)" % m if m is not None else "0.5"
    B = "taper(%d,A)" % b if b is not None else "0.5"
    G = "taper(%d,A)" % g if g is not None else "0.5"
    LV = "taper(%d,A)*2" % lv if lv is not None else "1"
    cfg = ("triode Rp=100k Rk=1.5k B=280 byp=1; fmv R1=250k R2=250k R3=10k R4=100k C1=250p C2=100n C3=47n t=%s m=%s l=%s; "
           "vol a=%s; triode Rp=100k Rk=1.5k B=280 byp=1; gain db=-20; triode Rp=100k Rk=820 B=280 byp=0; gain db=-34; "
           "power V=1 g=1 sag=0.3; hpf2 f=85 Q=0.8; peak f=110 Q=1.5 g=4; peak f=2500 Q=2 g=4; lpf2 f=5000 Q=0.7; vol a=%s"
           % (T, M, B, G, LV))
    return ("AmpSim", ctrls, cfg)


def placeholder_cab(c):
    from catalog import knob
    h = c.get("engine_hints") or {}
    fres, q, lp, hp = h.get("fres") or 90, h.get("qres") or 1.4, h.get("lp") or 5000, h.get("hp") or 70
    cfg = "cabs=%s:%d:%.2f:%d:%d:2400:4" % (c["id"], int(fres), float(q), int(lp), int(hp))
    return ("CabIR", [knob("MIC", "mic", 0.3), knob("DIST", "dist", 0.2), knob("MIX", "mix", 1.0), knob("LEVEL", "level", 0.5)], cfg)


# ------------------------------------------------------------------ casse
CAB_STYLE = {
    # combo: aspetto della scheda dell'ampli (stessa faccia senza comandi: pannello cieco con il nome)
    "cbprinceton110": dict(amp="amprincetonreverb"), "cbdeluxe112": dict(amp="amdeluxereverb"),
    "cbtwin212": dict(amp="amtwinreverb"), "cbbassman410": dict(amp="ambassman5f6a"), "cbac30212": dict(amp="amac30tb"),
    "cbac15112": dict(amp="amac15"), "cbjc120212": dict(amp="amjc120"), "cbb15115": dict(amp="amb15n", cube=True),
    "cbv212blue": dict(grille="diamond", gcol=None, covering=dict(col=(26, 26, 30), kind="vinyl"), piping=GOLD_PIPE, band=11,
                       logo=dict(x=20, y=9, font="block", col=(222, 200, 140)), corners="brass", handle="leather"),
    "cbm1960a": dict(grille="black_silver", covering=TOLEX, piping=WHITE_PIPE, logo=dict(x=50, y=17, font="script", col=(242, 242, 238)),
                     corners="black", handle="side", feet="casters", inset=3.5),
    "cbm1960b": dict(grille="black_silver", covering=TOLEX, piping=WHITE_PIPE, logo=dict(x=50, y=12, font="script", col=(242, 242, 238)),
                     corners="black", handle="side", feet="casters", inset=3.5),
    "cbm1960bw": dict(grille="basket_light", covering=TOLEX, piping=WHITE_PIPE, logo=dict(x=50, y=12, font="script", col=(30, 30, 30)),
                      corners="black", handle="side", feet="rubber", inset=3.5),
    "cbm1936": dict(grille="black_silver", covering=TOLEX, piping=WHITE_PIPE, logo=dict(x=50, y=14, font="script", col=(242, 242, 238)),
                    corners="black", handle="top_recessed", feet="rubber", inset=3.5),
    "cbhiwatt412": dict(grille="light_fine", covering=TOLEX, piping=(200, 200, 196), logo=dict(x=50, y=12, font="block", col=(235, 235, 235), plate=True, platecol=(40, 70, 120)),
                        corners="black", handle="side", feet="rubber", inset=4, round=True),
    "cbppc412": dict(grille="basket_beige", covering=dict(col=(226, 98, 22), kind="vinyl"), piping=None, frame=(222, 92, 20),
                     logo=dict(x=50, y=12, font="round", col=(232, 108, 28), plate=True, platecol=(24, 24, 24)), corners="black", handle="side", feet="skids", inset=7),
    "cbppc212": dict(grille="basket_beige", covering=dict(col=(226, 98, 22), kind="vinyl"), piping=None, frame=(222, 92, 20),
                     logo=dict(x=50, y=16, font="round", col=(232, 108, 28), plate=True, platecol=(24, 24, 24)), corners="black", handle="side", feet="skids", inset=7),
    "cbmatchless212": dict(grille="silver_black", covering=TOLEX, piping=SILVER_PIPE, logo=dict(x=50, y=14, font="block", col=(240, 200, 120), plate=True, platecol=(30, 24, 20)),
                           corners="chrome", handle="strap", feet="rubber", inset=4),
    "cbrecto412": dict(grille="jute", covering=dict(col=(22, 22, 24), kind="bronco"), piping=None, logo=dict(x=50, y=10, font="block", col=(210, 210, 214), plate=True, platecol=(22, 22, 24)),
                       corners="chrome", handle="side", feet="casters", inset=5),
    "cbrecto412tr": dict(grille="jute", covering=dict(col=(22, 22, 24), kind="bronco"), piping=None, logo=dict(x=50, y=10, font="block", col=(210, 210, 214), plate=True, platecol=(22, 22, 24)),
                         corners="black", handle="side", feet="casters", inset=5),
    "cbmesa112": dict(grille="wicker", covering=dict(col=(22, 22, 24), kind="bronco"), piping=None, logo=dict(x=20, y=14, font="block", col=(236, 236, 230), plate=True, platecol=(20, 20, 20)),
                      corners="chrome", handle="leather", feet="rubber", inset=5, port=(50, 86, 34, 5)),
    "cbmesa212": dict(grille="jute", covering=dict(col=(22, 22, 24), kind="bronco"), piping=None, logo=dict(x=50, y=12, font="block", col=(210, 210, 214), plate=True, platecol=(22, 22, 24)),
                      corners="black", handle="side", feet="casters", inset=5),
    "cb5150412": dict(grille="black_fine", covering=TOLEX, piping=None, logo=dict(x=50, y=10, font="block", col=(222, 222, 226), plate=True, platecol=(40, 40, 44)),
                      corners="chrome", handle="side", feet="casters", inset=4),
    "cbsoldano412": dict(grille="black_fine", covering=TOLEX, piping=None, logo=dict(x=24, y=10, font="script", col=(214, 216, 220), plate=True, platecol=(30, 30, 30)),
                         corners="chrome", handle="side", feet="casters", inset=4),
    "cbengl412": dict(grille="black_fine", covering=TOLEX, piping=None, logo=dict(x=50, y=12, font="block", col=(226, 226, 230), plate=True, platecol=(36, 36, 40)),
                      corners="black", handle="side", feet="rubber", inset=4, slant=True),
    "cbdiezel412": dict(grille="mesh", covering=TOLEX, piping=None, logo=dict(x=50, y=10, font="gothic", col=(230, 230, 232), plate=True, platecol=(26, 26, 28)),
                        corners="black", handle="side", feet="casters", inset=4),
    "cbbogner412": dict(grille="black_fine", covering=TOLEX, piping=SILVER_PIPE, logo=dict(x=50, y=12, font="script", col=(232, 232, 236)),
                        corners="chrome", handle="side", feet="casters", inset=4),
    "cbfriedman412": dict(grille="black_fine", covering=TOLEX, piping=GOLD_PIPE, logo=dict(x=50, y=11, font="script", col=(206, 172, 96)),
                          corners="black", handle="side", feet="rubber", inset=4),
    "cbfriedman212": dict(grille="black_fine", covering=TOLEX, piping=GOLD_PIPE, logo=dict(x=50, y=14, font="script", col=(206, 172, 96)),
                          corners="black", handle="side", feet="rubber", inset=4),
    "cbsvt810": dict(grille="silver_black", covering=TOLEX, piping=None, logo=dict(x=50, y=5, font="script", col=(232, 232, 236), plate=True, platecol=(24, 24, 26)),
                     corners="chrome", handle="towel", feet="casters", inset=3),
    "cbsvt410": dict(grille="silver_black", covering=TOLEX, piping=None, logo=dict(x=50, y=8, font="script", col=(232, 232, 236), plate=True, platecol=(24, 24, 26)),
                     corners="chrome", handle="side", feet="casters", inset=3, horn=True),
    "cbgk410": dict(grille="metal_black", covering=dict(col=(28, 28, 30), kind="carpet"), piping=None, logo=dict(x=50, y=7, font="block", col=(230, 230, 230), plate=True, platecol=(160, 30, 30)),
                    corners="black", handle="side", feet="casters", inset=3, horn=True, ports=True),
}


def cab_spec(c, amps):
    cs = CAB_STYLE.get(c["id"], {})
    de = c.get("dimensions_ext") or {}
    W, H, D = de.get("width_mm") or 600, de.get("height_mm") or 600, de.get("depth_mm") or 300
    ap = c.get("appearance") or {}
    spk = [(float(s.get("x_pct") or 50), float(s.get("y_pct") or 50), float(s.get("diameter_pct_of_width") or 40))
           for s in c.get("speaker_layout") or []]
    back_open = 1 if (c.get("engine_hints") or {}).get("open") else 0
    belems = back_elems((ap.get("back") or {}))
    if cs.get("amp"):
        a = amps[cs["amp"]]
        st = STYLE[a["id"]]
        A = amp_spec(a, st, cab_mode=True)
        A["console"] = True
        A["speakers"] = spk
        A["entries"] = []
        if A["view"] == "top":
            A["panel"] = None          # pannello sul cielo (AC30, Bassman, B-15): la cassa non lo ha sul frontale
        A["view"] = "front"
        if cs.get("cube"):
            # B-15: solo il cubo del mobile (la testata non fa parte della cassa)
            A["kind"] = "cab"
            A["dims"] = (W, H, D)
            A["face"] = [dict(kind="grille", rect=(7, 7, 93, 93), style="silver_grey", piping=WHITE_PIPE, double=True)]
            A["logo"] = dict(x=26, y=18, font="script", col=(236, 236, 240))
        A["back"] = dict(kind="cabopen" if back_open else "cabclosed", elements=belems, rect=(30, 70, 70, 90), tube_dir=None)
        return A
    gstyle = cs.get("grille") or "black_fine"
    inset = cs.get("inset", 4)
    face = [dict(kind="grille", rect=(inset, inset, 100 - inset, 100 - inset), style=gstyle, piping=cs.get("piping"),
                 frame=cs.get("frame"), band=cs.get("band"))]
    A = dict(kind="cab", dims=(W, H, D), depth_top=de.get("depth_top_mm"), view="front", console=True,
             strip="boss", knob="black", panel=None, entries=[], inc=[], face=face,
             covering=cs.get("covering") or TOLEX, logo=cs.get("logo"), name=None, corners=cs.get("corners"),
             handle=cs.get("handle", "side"), feet=cs.get("feet", "rubber"), speakers=spk, slant=bool(c.get("dimensions_ext", {}).get("depth_top_mm")) or cs.get("slant", False),
             round=cs.get("round", False), port=cs.get("port"), horn=cs.get("horn", False), ports=cs.get("ports", False),
             back=dict(kind="cabopen" if back_open else "cabclosed", elements=belems, rect=(30, 66, 70, 88), tube_dir=None))
    A["scale"] = 0.62
    return A


# ------------------------------------------------------------------ ampli
def amp_spec(a, st, cab_mode=False):
    ap = a["appearance"]
    dm = ap["dimensions"]
    W, H, D = st.get("dims") or (dm.get("width_mm"), dm.get("height_mm"), dm.get("depth_mm"))
    f = ap["front"]
    view = st.get("view") or f.get("panel_view") or "front"
    pr = st.get("panel_rect") or (f.get("panel_rect_pct") and tuple(f["panel_rect_pct"][k] for k in ("x0", "y0", "x1", "y1"))) or (3, 60, 97, 92)
    inc = [c["name"].lower() for c in a["channels"] if c.get("include")]
    entries = amp_entries(a, st)
    panel = dict(st["panel"], rect=tuple(float(v) for v in pr))
    b = ap["back"]
    brect = b.get("panel_rect_pct")
    brect = tuple(brect[k] for k in ("x0", "y0", "x1", "y1")) if brect else None
    bst = dict(st.get("back") or {})
    back = dict(kind=bst.get("kind", "vent"), rect=bst.get("rect") or brect or (4, 6, 96, 26), elements=back_elems(b),
                tubes=parse_tubes(b.get("tubes_layout"), bst.get("power_side", "left")), tube_dir=bst.get("tube_dir", "down"))
    # scala: pomelli (filmstrip) della grandezza apparente dei pomelli veri e distanza minima tra i comandi
    strip = st.get("strip", "ts_small")
    import pedal_layout as PL
    sd = 2 * PL.KNOB_R[strip] * 1000.0
    s = sd / float(st.get("dk", 24))
    pw = (pr[2] - pr[0]) / 100.0 * (W if view else W)
    ph = (pr[3] - pr[1]) / 100.0 * (H if view == "front" else D)
    rots = [e for e in entries if e["kind"] in ("knob", "selector")]
    for i in range(len(rots)):
        for j in range(i + 1, len(rots)):
            dx = abs(rots[i]["x"] - rots[j]["x"]) / 100.0 * pw
            dy = abs(rots[i]["y"] - rots[j]["y"]) / 100.0 * ph
            dd = math.hypot(dx, dy * 1.6)
            if dd > 1:
                s = max(s, (sd + 1.5) / dd)
    s = round(min(1.0, s), 3)
    A = dict(kind=st["kind"], dims=(W, H, D), view=view, strip=strip, knob=st.get("knob", "black"), scale=s,
             panel=panel, entries=entries, inc=inc, face=st.get("face") or [], covering=st.get("covering") or TOLEX,
             logo=st.get("logo"), corners=st.get("corners"), handle=st.get("handle", "strap"), feet=st.get("feet", "rubber"),
             speakers=[], back=back, vents=st.get("vents", False), piping_all=st.get("piping_all"),
             head_h=st.get("head_h"), console=False)
    return A


def notes_of(a):
    rv = a.get("reference_version") or {}
    return ("Segnaposto (in attesa della parte funzionale): %s. %s" % (rv.get("model") or a["real_name"],
            (a.get("why_iconic") or "")[:160])).strip()


def main():
    amps = {a["id"]: a for a in json.load(open(os.path.join(S, "stage6_amps.json")))["amps"]}
    cabs = json.load(open(os.path.join(S, "stage6_cabs.json")))["cabs"]
    ids_a = [l.strip() for l in open(os.path.join(S, "ids_amps.txt")) if l.strip()]
    ids_c = [l.strip() for l in open(os.path.join(S, "ids_cabs.txt")) if l.strip().startswith("cb")]
    cab_by_amp = {c["combo_of"]: c for c in cabs if c.get("combo_of")}
    rows = []
    for aid in ids_a:
        a = amps[aid]
        st = STYLE[aid]
        A = amp_spec(a, st)
        if aid in cab_by_amp:
            c = cab_by_amp[aid]
            A["speakers"] = [(float(s.get("x_pct") or 50), float(s.get("y_pct") or 50), float(s.get("diameter_pct_of_width") or 40))
                             for s in c.get("speaker_layout") or []]
        fam, ctrls, cfg = placeholder_amp(a, A["entries"], A["inc"])
        colour = tuple(A["covering"]["col"])
        inspired = "%s %s (%s)" % (a["brand"], a["real_name"], a.get("real_code") or "")
        look = dict(amp=A, dims=(A["dims"][0], A["dims"][2], A["dims"][1]))
        rows.append(("amp", aid, a["code"], a["name"], inspired, (a.get("real_code") or "", a["real_name"]), colour, look,
                     (fam, ctrls, cfg), notes_of(a)))
    crow = []
    for cid in ids_c:
        c = next(x for x in cabs if x["id"] == cid)
        A = cab_spec(c, amps)
        fam, ctrls, cfg = placeholder_cab(c)
        colour = tuple(A["covering"]["col"])
        inspired = "%s %s" % (c["brand"], c["real_name"])
        look = dict(amp=A, dims=(A["dims"][0], A["dims"][2], A["dims"][1]))
        notes = "Segnaposto (in attesa della parte funzionale): cassa %s %s, %s." % (c["format"], c.get("back") or "", c.get("character", "")[:140])
        crow.append(("cab", cid, c["code"], c["name"], inspired, (c.get("real_code") or "", c["real_name"]), colour, look,
                     (fam, ctrls, cfg), notes))
    # laboratorio di cassa (modello originale Pedal Trinity, senza replica REAL MOD): formato del mobile, cono, microfono
    # e posizione scelti con i comandi; aspetto di una cassa da studio 2x12 chiusa
    lab = dict(kind="cab", dims=(700, 520, 330), depth_top=None, view="front", console=True, strip="boss", knob="black",
               panel=None, entries=[], inc=[], face=[dict(kind="grille", rect=(4, 4, 96, 96), style="silver_black", piping=SILVER_PIPE)],
               covering=dict(col=(40, 42, 46), kind="bronco"), logo=dict(x=50, y=11, font="block", col=(230, 200, 120), plate=True,
                                                                          platecol=(26, 26, 28)),
               name=None, corners="chrome", handle="side", feet="rubber", speakers=[(27, 52, 40), (73, 52, 40)], slant=False,
               round=False, port=None, horn=False, ports=False, scale=0.62,
               back=dict(kind="cabclosed", elements=[("INPUT", "jack", 40, 50), ("PARALLEL", "jack", 60, 50)], rect=(30, 66, 70, 88), tube_dir=None))
    from catalog import knob
    crow.append(("cab", "cblab", "KX-LAB", "Cab Lab", "originale Pedal Trinity (laboratorio di cassa)", None, (40, 42, 46),
                 dict(amp=lab, dims=(700, 330, 520)),
                 ("CabIR", [knob("MIC", "mic", 0.3), knob("DIST", "dist", 0.2), knob("MIX", "mix", 1.0), knob("LEVEL", "level", 0.5)],
                  "cabs=cblab:90:1.40:5000:70:2400:4"),
                 "Laboratorio di cassa: formato del mobile, altoparlante, microfono e posizione a scelta (segnaposto)."))
    write("catalog_amps.py", "Amplificatori (tappa 6): 27 ampli", rows)
    write("catalog_cabs.py", "Casse (tappa 6): 31 casse e il laboratorio di cassa", crow)
    print("ampli %d, casse %d" % (len(rows), len(crow)))


def write(fn, title, rows):
    lines = ['"""', "Pedal Trinity - catalogo GENERATO da gen_catalog_amps.py: %s." % title,
             "Non modificare a mano: cambiare il generatore e rigenerare. Parte funzionale da sound_stage6_*.json", '"""',
             "from amp_catalog import amp, cab", "", "MODELS = ["]
    for kind, mid, code, name, inspired, real, colour, look, ph, notes in rows:
        fam, ctrls, cfg = ph
        lines.append("    %s(%r, %r, %r, %r, %r, %r," % (kind, mid, code, name, inspired, real, colour))
        lines.append("        look=" + pprint.pformat(look, width=120, compact=True).replace("\n", "\n             ") + ",")
        lines.append("        placeholder=(%r, %s, %r)," % (fam, pprint.pformat(ctrls, width=110, compact=True).replace("\n", "\n                     "), cfg))
        lines.append("        notes=%r)," % notes)
    lines.append("]")
    open(os.path.join(HERE, fn), "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
