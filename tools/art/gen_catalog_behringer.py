#!/usr/bin/env python3
"""
Pedal Trinity - generatore dei cataloghi della tappa 5 (Behringer e Cuvave / M-VAVE): catalog_behringer.py (serie in
plastica a coperchio-pedale), catalog_behringer_b.py (metallo: serie Vintage, serie 9, V-Tone, AB200, serie moderne,
serie BM, pedali a bilanciere) e catalog_cuvave.py, dalle schede ~/.config/PedalTrinity/stage5/stage5_specs.json.

L'ASPETTO viene dalle schede (misure, posizione dei comandi x%/y% dai disegni numerati dei manuali, LED, footswitch) e
dagli stampi descritti qui sotto, uno per famiglia di contenitori:
  bhpl     serie in plastica (62 schede): base a tutta larghezza, parte posteriore piu' stretta con la piastra scura dei
           comandi, coperchio-pedale che copre tutta la meta' vicina con il riquadro a filetto del nome su due righe, la
           sigla in un riquadro pieno e la gomma col bordo lontano bombato (foto Commons OD100, UO300, CO600, UT100,
           CS400, EQ700, TO800, EM600, BSY600; documenti informativi Behringer con le foto a colori)
  vintage  serie Vintage in lamiera (VT911, VD1, VB1, VP1 strette; VT999, VM1 larghe) con la grande 'V' nera
  bh9      serie 9 pressofusa (DC9, IG9, PH9)
  vtone    V-Tone Driver/DI (BDI21, GDI21, ADI21): plastica argento, fronte a U, grande pulsante in un ovale
  ab200    AB200: due coperchi-pedale affiancati
  moderne  Zentara, Zentara Mini, Fuzz Bender (a bara), Chorus Symphony, Octavia, Bass Brassmaster, B-Tron III,
           Dual-Phase, Octave Divider, 69 Vibe, cloni Lovetone (Meatball, Ring Stinger, Flange With No Name)
  bm       serie BM (cloni Moogerfooger): scatola verticale alta col piano inclinato e le fiancate rialzate
  cvmini   mini Cuvave / M-VAVE 90 x 40 mm; cvbox: serie M-VAVE 112 x 68 mm e IR BOX; cvbar: barretta Cube Baby
  bilancieri  HB01, FCV100, FC600, WahWah, Parapedal, Pedal Flanger, B-Flanger (shaped_layout.py)
Colori: tabella COL (verificati sulle foto libere e sui documenti informativi dove indicato, altrimenti stimati).
Mai loghi, marchi o artwork: solo colori, fasce e geometrie semplici; in modalita' normale nome e sigla Pedal Trinity
(sigle NV/NR/NX/NB/NY/NK/NW), sulle repliche REAL MOD il nome del modello senza il marchio del produttore.
La PARTE FUNZIONALE viene dai JSON sound_stage5_*.json tramite stomp_catalog.stomp/rocker; qui si scrive solo il
segnaposto con le etichette e l'ordine dei comandi della scheda.

Uso: python3 tools/art/gen_catalog_behringer.py [schede.json]
"""
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_argv = sys.argv
sys.argv = sys.argv[:1]
import gen_catalog_ibanez as GI  # noqa: E402  (funzioni comuni: etichette, segnaposto)
sys.argv = _argv
SPECS = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.config/PedalTrinity/stage5/stage5_specs.json")

lum, ink, darker, lighter = GI.lum, GI.ink, GI.darker, GI.lighter
BLACK, WHITE = (26, 26, 28), (238, 238, 232)
SILVER = (198, 200, 203)

# ------------------------------------------------------------------ colori
# id: (corpo, accento (nome/riquadri) o None, piastra dei comandi o None)
#  V = verificato su foto libera / documento informativo Behringer, S = stima (descrizione, originale clonato)
COL = {
    # serie in plastica - overdrive/distorsioni
    "bhto800": ((172, 212, 88), None, None),        # V foto Commons (verde lime)
    "bhto100": ((214, 122, 44), None, None),        # S ambra
    "bhod300": ((168, 108, 190), None, None),       # V (OD100 lilla, stessa serie)
    "bhod400": ((236, 196, 44), None, None),        # S
    "bhbo300": ((64, 136, 206), None, None),        # S
    "bhpo300": ((206, 70, 40), None, None),         # S
    "bhud300": ((236, 118, 36), None, None),        # S
    "bhum300": ((52, 52, 56), (236, 132, 40), (20, 20, 22)),   # S
    "bhhm300": ((232, 130, 172), None, None),       # S ('pretty pink color')
    "bhhd300": ((196, 44, 40), None, None),         # S
    "bhsm400": ((176, 28, 30), None, (20, 20, 22)), # S ('red-hot')
    "bhxd300": ((92, 100, 116), None, None),        # S
    "bhwd300": ((98, 54, 142), None, None),         # S
    "bhdm100": ((182, 40, 52), None, None),         # S
    "bhsf300": ((232, 172, 36), None, None),        # S
    "bhuz400": ((62, 64, 72), (236, 196, 44), (20, 20, 22)),   # S
    "bhfd300": ((236, 108, 32), None, None),        # S
    "bhpb100": ((222, 220, 206), None, None),       # S
    # modulazioni
    "bhuc200": ((86, 168, 218), None, None),        # S
    "bhuf300": ((122, 82, 172), None, None),        # S
    "bhup300": ((110, 178, 70), None, None),        # S
    "bhut300": ((226, 128, 36), None, None),        # V documento informativo UT300 (arancio)
    "bhtp300": ((56, 140, 176), None, None),        # S
    "bhuv300": ((72, 92, 172), None, None),         # S
    "bhuo300": ((204, 206, 204), None, (108, 108, 110)),       # V foto Commons (grigio chiaro, piastra grigia)
    "bhcc300": ((198, 214, 226), None, None),       # S
    "bhco600": ((246, 172, 30), None, None),        # V foto Commons (giallo-arancio)
    "bhhf300": ((226, 226, 220), None, None),       # S
    "bhcd400": ((58, 108, 188), None, None),        # S
    "bhsf400": ((150, 152, 162), None, None),       # S
    "bhsp400": ((122, 188, 80), None, None),        # S
    "bhso400": ((104, 70, 152), None, None),        # S
    "bhus600": ((140, 150, 172), None, None),       # S
    "bhfl600": ((128, 78, 160), None, None),        # S
    "bhrm600": ((178, 40, 40), None, None),         # S
    # tempo, filtri, wah
    "bhdd400": (SILVER, (24, 24, 26), None),        # V documento informativo (argento, riquadro nero)
    "bhdd600": ((222, 222, 216), None, None),       # S
    "bhvd400": (SILVER, (24, 24, 26), None),        # V documento informativo
    "bhem600": ((56, 140, 68), None, None),         # V foto Commons e documento informativo (verde)
    "bhdr600": ((62, 72, 92), None, None),          # S
    "bhdr400": (SILVER, (140, 70, 176), None),      # V documento informativo (scritte viola)
    "bhrv600": ((70, 176, 220), None, None),        # V documento informativo (azzurro)
    "bhfx600": (SILVER, (214, 40, 60), None),       # V documento informativo (scritte rosse)
    "bhuw300": ((52, 168, 150), None, None),        # S
    "bhdw400": ((238, 222, 52), None, None),        # V documento informativo e foto Commons (giallo)
    "bhfm600": ((232, 132, 32), None, None),        # S
    # basso, emulatori
    "bhtm300": ((72, 74, 80), (236, 196, 60), (20, 20, 22)),   # S
    "bham300": ((176, 82, 36), (240, 214, 96), None),          # V foto Commons AM100 (legno-arancio, scritte crema)
    "bham400": ((150, 92, 52), (240, 214, 96), None),          # S
    "bhbeq700": ((236, 222, 160), None, None),      # V documento informativo (crema/oro)
    "bhbod400": ((140, 30, 34), None, None),        # S
    "bhble400": ((58, 92, 140), None, None),        # S
    "bhbuf300": ((112, 72, 150), None, None),       # S
    "bhbuc400": ((60, 140, 190), None, None),       # S
    "bhbsy600": ((150, 34, 44), None, None),        # V foto Commons (rosso scuro)
    # dinamica, utility
    "bhcs400": ((36, 160, 156), None, None),        # V foto Commons (verde acqua)
    "bhnr300": (SILVER, (24, 24, 26), None),        # V documento informativo
    "bheq700": ((232, 220, 80), None, None),        # V foto Commons (giallo)
    "bhsm200": ((150, 152, 156), None, None),       # S ('Grey')
    "bhse200": ((236, 214, 60), None, None),        # S ('Yellow')
    "bhcl9": ((60, 120, 196), None, None),          # S
    "bhab100": ((228, 228, 224), None, None),       # S ('White')
    # metallo
    "bhvt911": (SILVER, BLACK, None), "bhvt999": (SILVER, BLACK, None), "bhvd1": (SILVER, BLACK, None),
    "bhvm1": (SILVER, BLACK, None), "bhvb1": (SILVER, BLACK, None), "bhvp1": (SILVER, BLACK, None),   # V disegni del manuale
    "bhdc9": ((30, 30, 32), (236, 236, 230), None), "bhig9": ((40, 72, 120), (236, 236, 230), None),
    "bhph9": ((232, 112, 30), (24, 24, 26), None),
    "bhbdi21": ((200, 202, 204), (24, 24, 26), None),           # V foto Commons e documento informativo
    "bhgdi21": ((70, 110, 180), (236, 236, 230), None),         # S ('blue')
    "bhadi21": ((172, 132, 82), (24, 24, 26), None),            # S
    "bhab200": ((196, 198, 200), (24, 24, 26), None),
    "bhzentara": ((206, 170, 84), (24, 24, 26), None), "bhzentaramini": ((206, 170, 84), (24, 24, 26), None),
    "bhfuzzbender": ((132, 134, 138), (24, 24, 26), None),
    "bhchorussymphony": ((176, 182, 190), (24, 24, 26), (34, 36, 40)),
    "bhoctavia": ((112, 62, 150), (240, 230, 200), None),
    "bhbassbrassmaster": ((34, 34, 36), (226, 180, 80), None),
    "bhbtron3": ((196, 198, 200), (24, 24, 26), (30, 30, 32)),
    "bhdualphase": ((200, 202, 204), (24, 24, 26), None),
    "bhoctavedivider": ((200, 202, 204), (24, 24, 26), (24, 24, 26)),
    "bh69vibe": ((28, 28, 30), (236, 236, 230), None),
    "bhmeatball": ((52, 92, 176), (236, 236, 228), None),
    "bhringstinger": ((232, 96, 160), (24, 24, 26), None),
    "bhflangenoname": ((112, 150, 120), (24, 24, 26), None),
    "bhbm11m": ((198, 200, 202), (24, 24, 26), None), "bhbm12": ((198, 200, 202), (24, 24, 26), None),
    "bhbm13": ((198, 200, 202), (24, 24, 26), None), "bhbm14m": ((198, 200, 202), (24, 24, 26), None),
    "bhbm15m": ((198, 200, 202), (24, 24, 26), None), "bhbm17": ((198, 200, 202), (24, 24, 26), None),
    "bhbm18m": ((198, 200, 202), (24, 24, 26), None),
    # bilancieri
    "bhhb01": ((28, 28, 30), (200, 36, 40), None), "bhfcv100": ((26, 26, 28), None, None),
    "bhfc600": ((176, 178, 182), None, None), "bhwahwah": ((30, 30, 32), (200, 202, 206), None),
    "bhparapedal": ((176, 178, 182), (24, 24, 26), None), "bhpedalflanger": ((176, 178, 182), (24, 24, 26), None),
    "bhbflanger": ((40, 40, 44), (236, 236, 230), None),
    # Cuvave / M-VAVE (stime: nessuna foto libera; colori sul modello dell'originale)
    "cvcubebaby": ((32, 32, 34), (238, 120, 40), None), "cvcubebabybass": ((30, 44, 80), (90, 180, 230), None),
    "cvcubebabyac": ((140, 92, 52), (240, 220, 170), None),
    "cvminiamp": ((30, 30, 32), (230, 180, 70), None), "cvminiefx": ((40, 40, 44), (220, 60, 50), None),
    "cvoverdrivets": ((70, 160, 84), None, None), "cvoverdriveblues": ((52, 112, 196), None, None),
    "cvoverdrivedb": ((150, 110, 70), None, None), "cvoverdriveamp": ((226, 132, 40), None, None),
    "cvdistbritish": ((40, 40, 42), (220, 180, 90), None), "cvdistsc1": ((200, 56, 44), None, None),
    "cvdistmt1": ((34, 34, 36), (230, 110, 40), None), "cvfuzz": ((120, 70, 150), None, None),
    "cvbooster": ((224, 224, 218), None, None), "cvcompressor": ((50, 150, 176), None, None),
    "cvnoisegate": ((60, 60, 64), (236, 236, 230), None), "cvchorus": ((120, 196, 226), None, None),
    "cvphaser": ((236, 140, 40), None, None), "cvtremolo": ((60, 130, 90), None, None),
    "cvdelaypd18": ((200, 200, 196), None, None), "cvdigdelay": ((238, 238, 232), None, None),
    "cvdigpitch": ((92, 80, 170), None, None), "cvdigreverb": ((54, 66, 92), None, None),
    "cvlooper": ((30, 30, 32), (236, 236, 230), None), "cvlooperpro": ((30, 30, 32), (236, 236, 230), None),
    "cvloop2": ((236, 236, 232), (24, 24, 26), None), "cvminiuniverse": ((30, 40, 80), (200, 220, 255), None),
    "cvelemental": ((60, 62, 68), (240, 160, 60), None), "cvelementalpro": ((44, 46, 52), (240, 160, 60), None),
    "cvclassicdelay": ((180, 150, 100), None, None), "cvgalaxia": ((44, 76, 170), (230, 236, 255), None),  # galaxia: V manuale ('blue')
    "cvirbox": ((32, 32, 34), (236, 236, 230), None),
}

CATEGORY = dict(GI.CATEGORY)
CATEGORY.update({"preamp": "Amp / IR", "multi": "Chorus / Dimension", "looper": "Tuner / Looper",
                 "volume": "Utility / Routing", "utility": "Utility / Routing"})
CAT_ID = {"bhnr300": "Dinamica", "bhig9": "Dinamica", "cvnoisegate": "Dinamica", "cvminiamp": "Amp / IR",
          "cvminiefx": "Amp / IR", "cvirbox": "Amp / IR", "cvcubebabybass": "Amp / IR", "cvcubebabyac": "Amp / IR",
          "bhfx600": "Chorus / Dimension", "bham300": "Acustica", "bham400": "Acustica", "bhadi21": "Acustica"}
SUB = dict(GI.SUB, **{"Acustica": "ACOUSTIC", "Tuner / Looper": "LOOPER", "Amp / IR": "AMP MODELER"})

# ------------------------------------------------------------------ etichette dei comandi
# (id, etichetta della scheda) -> (etichetta, alias)
LAB = {
    ("bhso400", "RANGE / OCT 2 / DRIVE"): ("OCT 2", ["RANGE", "DRIVE", "RANGE/OCT2"]),
    ("bhus600", "D TIME/SPEED"): ("D.TIME", ["SPEED", "D TIME", "TIME"]),
    ("bhdr400", "TONE / F.BACK"): ("TONE", ["F.BACK", "FEEDBACK", "TONE/FBACK"]),
    ("bhdr400", "R.TIME / D.TIME"): ("TIME", ["R.TIME", "D.TIME", "R/D TIME"]),
    ("bhhb01", "BOOST (deviatore)"): ("BOOST SW", ["BOOST ON"]),
    ("bhfcv100", "CV / NORM"): ("CV/NORM", ["MODE", "CV"]),
    ("bhchorussymphony", "INPUT LEVEL HIGH/LOW"): ("INPUT", ["INPUT LEVEL", "INPUT LVL", "HIGH/LOW"]),
    ("bhchorussymphony", "CHORUS INTENSITY"): ("INTENSITY", ["CHORUS INT", "CHORUS"]),
    ("bhchorussymphony", "VIBRATO DEPTH"): ("VIB DEPTH", ["DEPTH", "VIBRATO DEPTH"]),
    ("bhchorussymphony", "VIBRATO RATE"): ("VIB RATE", ["RATE", "VIBRATO RATE"]),
    ("bhbassbrassmaster", "HARMONIC 1/2"): ("HARMONIC", ["HARM"]),
    ("bhbassbrassmaster", "BRASS 1/2"): ("BRASS", ["BRASS SW"]),
    ("bhdualphase", "RATE (SWEEP GENERATOR 1)"): ("RATE 1", ["RATE A", "SWEEP 1 RATE"]),
    ("bhdualphase", "MAN/CV (SWEEP GEN 1)"): ("MAN/CV", ["MANUAL", "CV"]),
    ("bhdualphase", "SHAPE (SWEEP GEN 1)"): ("SHAPE 1", ["SHAPE A"]),
    ("bhdualphase", "SHAPE (SWEEP GEN 2)"): ("SHAPE 2", ["SHAPE B"]),
    ("bhdualphase", "RATE (SWEEP GEN 2)"): ("RATE 2", ["RATE B", "SWEEP 2 RATE"]),
    ("bhdualphase", "DEPTH (PHASOR A)"): ("DEPTH A", ["DEPTH 1"]),
    ("bhdualphase", "FEEDBACK (PHASOR A)"): ("FDBK A", ["FEEDBACK A", "FEEDBACK 1"]),
    ("bhdualphase", "SWEEP GEN 1/CV (PHASOR A)"): ("SWEEP A", ["SOURCE A"]),
    ("bhdualphase", "SWEEP SYNC NORM/REV (PHASOR B)"): ("SYNC", ["SWEEP SYNC", "NORM/REV"]),
    ("bhdualphase", "PHASOR B INPUT"): ("INPUT B", ["B INPUT", "PHASOR B"]),
    ("bhdualphase", "SWEEP (PHASOR B)"): ("SWEEP B", ["SOURCE B"]),
    ("bhdualphase", "DEPTH (PHASOR B)"): ("DEPTH B", ["DEPTH 2"]),
    ("bhdualphase", "FEEDBACK (PHASOR B)"): ("FDBK B", ["FEEDBACK B", "FEEDBACK 2"]),
    ("bhbm11m", "FILTER 2-POLE/4-POLE"): ("FILTER", ["POLES", "2/4 POLE", "SLOPE"]),
    ("bhbm11m", "ENVELOPE SMOOTH/FAST"): ("ENVELOPE", ["ENV", "SMOOTH/FAST"]),
    ("bhbm12", "MODULATOR LO/HI"): ("MODULATOR", ["LO/HI", "RANGE"]),
    ("bhbm12", "LFO SQUARE/SINE"): ("LFO", ["WAVE", "LFO WAVE"]),
    ("bhbm13", "6/12 STAGES"): ("STAGES", ["6/12"]),
    ("bhbm14m", "SHORT/LONG"): ("RANGE", ["SHORT/LONG", "TIME RANGE"]),
    ("bhbm15m", "LFO OFF/ON"): ("LFO", ["ANIMATION"]),
    ("bhbm15m", "FREQ BASS/MIDS"): ("FREQ", ["BASS/MIDS", "RANGE"]),
    ("bhbm17", "VCO SYNC OFF/ON"): ("SYNC", ["VCO SYNC"]),
    ("bhbm18m", "RANGE CHORUS/FLANGER"): ("RANGE", ["CHORUS/FLANGER", "MODE"]),
    ("bhmeatball", "FILTER MODE"): ("MODE", ["FILTER MODE", "FILTER"]),
    ("bhmeatball", "FILTER RANGE"): ("RANGE", ["FILTER RANGE"]),
    ("bhmeatball", "FILTER SWEEP"): ("SWEEP", ["FILTER SWEEP", "UP/DOWN"]),
    ("bhringstinger", "DRIVE(I)"): ("DRIVE", ["DRIVE(I)", "DRIVE I"]),
    ("bhflangenoname", "REACTION (YIN/YANG)"): ("REACTION", ["REACT"]),
    ("bhflangenoname", "ACTION (YIN/YANG)"): ("ACTION", []),
    ("bhoctavedivider", "POWER"): ("POWER", []),
    ("cvloop2", "memoria (1-3)"): ("MEMORY", ["MEM", "SLOT"]),
    ("cvloop2", "velocità"): ("SPEED", ["HALF", "VELOCITA"]),
    ("cvloop2", "READY-RECORD"): ("REC", ["READY", "RECORD"]),
    ("cvlooperpro", "UP / DOWN"): ("UP/DOWN", ["UP", "DOWN"]),
    ("cvirbox", "< (preset precedente)"): ("PREV", ["<", "DOWN"]),
    ("cvirbox", "> (preset successivo)"): ("NEXT", [">", "UP"]),
    ("cvcubebaby", "POWER (interruttore)"): ("POWER", []),
}


def ctl_kind(t):
    tl = (t or "").lower()
    if "interruttore a bil" in tl:
        return "lever"                  # interruttore a bilanciere (rocker switch), non un pedale
    if "dip" in tl or "vite" in tl or "trimmer" in tl:
        return None
    if "deviatore" in tl:
        return "lever"
    if "cursori" in tl or "cursore" in tl:
        return "slider"
    if "pulsante" in tl and "pomello" not in tl:
        return "button"
    if "pomello a punta" in tl:
        return "knob"
    return GI.ctl_kind(t)


def ext_controls(m):
    """Comandi esterni della scheda: come gen_catalog_ibanez.ext_controls, con i tipi e le etichette della tappa 5."""
    out = []
    for c in m["controls"]:
        if c.get("external") is False:
            continue
        k = ctl_kind(c.get("type") or "")
        if k is None:
            continue
        if (m["id"], c["label"]) in LAB:
            lab, al = LAB[(m["id"], c["label"])]
            al = list(al) + [c["label"].upper()]
        else:
            lab, al = GI.clean_label(c["label"])
        if m["id"] == "bhbm15m" and k == "slider":
            # 8 cursori FILTERS della fascia alta: una posizione per cursore
            for q in range(8):
                out.append(dict(label="FILTER %d" % (q + 1), alias=["F%d" % (q + 1), "BAND %d" % (q + 1)], kind="slider",
                                x=24.0 + q * 7.4, y=21.0, pos=None, orig=c["label"], type=c.get("type") or ""))
            continue
        out.append(dict(label=lab, alias=al, kind=k, x=c.get("x_pct"), y=c.get("y_pct"), pos=c.get("positions"),
                        orig=c["label"], type=c.get("type") or ""))
    return out


GI.ext_controls = ext_controls        # il segnaposto di gen_catalog_ibanez usa le etichette di questa tappa


def placeholder(m, cat):
    i = m["id"]
    if cat == "Tuner / Looper":
        return ("Looper", ["knob('LEVEL', 'level', 0.7)", "button('REC', 'rec')", "button('STOP', 'stop')",
                           "button('CLEAR', 'clear')"], "maxs=60")
    if i == "bhab100":
        return ("Router", ["toggle('MODE', 'mode', ('A/B', 'A+B'), 0)", "knob('A LEVEL', 'levelA', 0.7)",
                           "knob('B LEVEL', 'levelB', 0.7)"], "type=ls2")
    if i == "bhab200":
        return ("Router", ["toggle('SWITCH 2', 'select', ('A', 'B'), 0)", "toggle('LATCH', 'direction', ('LATCH', 'MOM'), 0)"],
                "type=ab2")
    if i == "bhsm200":
        return ("SlowGear", ["knob('SENS', 'sens')", "knob('ATTACK', 'attack')"], "attack=50,2000")
    fam, ctrls, cfg = GI.placeholder(m, cat)
    if cat == "Dinamica" and fam == "Compressor" and ("GATE" in m["real_name"].upper() or "NOISE" in m["real_name"].upper()
                                                     or i == "bhig9"):
        ks = [c for c in ext_controls(m) if c["kind"] != "lever"][:2]
        ctrls = ["knob(%r, 'threshold')" % (ks[0]["label"] if ks else "THRESH"), "knob('DECAY', 'decay')"]
        if i == "bhnr300":
            ctrls = ["toggle('MODE', 'mode', ('MUTE', 'RED.'), 1)", "knob('THRESH', 'threshold')", "knob('DECAY', 'decay')"]
        if i == "bhig9":
            ctrls = ["knob('THRESHOLD', 'threshold')", "selector('FREQUENCY', 'mode', ['LOW', 'MID', 'HIGH'], 1, short=['LO', 'MID', 'HI'])"]
        fam, cfg = "NoiseGate", "thr=-80,-20 decay=20,800"
    if cat == "Amp / IR" and fam == "Circuit":
        pass
    return fam, ctrls, cfg


# ------------------------------------------------------------------ posizioni
def place_of(m, xmap=None, kinds=None, default="knob", skip=()):
    """[(etichetta, x%, y%, tipo, alias)] dalle posizioni della scheda. xmap = funzione x% -> x% (es. parte posteriore piu'
    stretta della serie in plastica); kinds = {etichetta: tipo} forzati."""
    out, seen = [], set()
    for c in ext_controls(m):
        if c["kind"] == "treadle" or c["x"] is None or c["y"] is None or c["label"] in skip:
            continue
        lab = c["label"]
        if lab in seen:
            continue
        seen.add(lab)
        t = {"slider": "slider", "lever": "lever", "rotsel": default, "button": "button", "big": "big", "small": "small",
             "pushlok": "med", "knob": default, "concentric": "med"}[c["kind"]]
        if kinds and lab in kinds:
            t = kinds[lab]
        x = float(c["x"])
        if xmap:
            x = xmap(x)
        out.append((lab, round(GI.pct_clip(x, 4, 96), 1), round(GI.pct_clip(c["y"], 3, 97), 1), t, list(c["alias"])))
    return out


def feet_of(m, default=(50.0, 80.0)):
    out = []
    for f in m["appearance"].get("footswitches") or []:
        lab = re.sub(r"\s*\(.*?\)\s*", " ", f["label"]).strip().upper()
        lab = lab.replace(" + FEEDBACK", "").replace(" E TAP", "").replace("FOOTSWITCH ", "").replace("FOOTSWITCH", "ON/OFF")
        lab = re.sub(r"\s*;.*$", "", lab).strip()
        al = [f["label"].upper()]
        x = f.get("x_pct") if f.get("x_pct") is not None else default[0]
        y = f.get("y_pct") if f.get("y_pct") is not None else default[1]
        out.append((lab[:16] or "ON/OFF", float(x), float(y), al))
    if not out:
        out = [("ON/OFF", default[0], default[1], [])]
    k = next((j for j, f in enumerate(out) if any(w in f[0] for w in GI.BYPASS_WORDS + ("ON/OFF", "A/B"))), 0)
    if k:
        out = [out[k]] + out[:k] + out[k + 1:]
    return out


def leds_of(m, ymin=4.0):
    out = []
    for l in m.get("leds") or []:
        if not isinstance(l, dict) or l.get("x_pct") is None or l.get("y_pct") is None:
            continue
        col = str(l.get("colour") or l.get("color") or "").lower()
        if "display" in col or "segmenti" in col:
            continue
        c = "red"
        for k, v in (("ambra", "orange"), ("blu", "blue"), ("verde", "green"), ("giallo", "yellow"), ("rosso", "red"),
                     ("bianco", "white"), ("arancio", "orange")):
            if col.startswith(k):
                c = v
                break
        else:
            if "blu" in col:
                c = "blue"
        out.append((GI.pct_clip(l["x_pct"], 5, 95), GI.pct_clip(l["y_pct"], ymin, 96), c))
    return out


def arc_poly(x0, x1, y_mid, sag, y_bottom, n=24):
    """Regione sotto una linea ad arco (y% al centro piu' bassa di 'sag' rispetto ai bordi) fino a y_bottom."""
    pts = []
    for k in range(n + 1):
        t = k / n
        x = x0 + (x1 - x0) * t
        y = y_mid - sag * (1 - (2 * t - 1) ** 2) + sag
        pts.append((round(x, 2), round(y, 2)))
    return pts + [(x1, y_bottom), (x0, y_bottom)]


# ------------------------------------------------------------------ stampi
def body_ink(body, acc):
    return acc if acc else ink(body)


def look_bhpl(m, col):
    """Serie in plastica a coperchio-pedale: misure 70 x 123 x 54 mm (manuali)."""
    i = m["id"]
    W, D, H = 70.0, 123.0, 54.0
    body, acc, panel = col
    panel = panel or (28, 28, 30)
    ptx = (236, 236, 232) if lum(panel) < 128 else (24, 24, 26)
    nink = body_ink(body, acc)
    xmap = lambda x: 50 + (x - 50) * 0.92            # posizioni del disegno sulla parte posteriore piu' stretta
    place = place_of(m, xmap, default="med")
    rows2 = any(p[2] > 23.0 and p[3] in ("med", "knob") for p in place)
    if rows2:
        # seconda fila (pomelli piu' piccoli, come sui pedali veri a 5-7 comandi) e prima fila un po' piu' in alto; la
        # seconda fila resta lontana dal bordo rialzato del coperchio-pedale, che nella vista inclinata la coprirebbe
        place = [(p[0], p[1], min(p[2], 28.0) if p[2] > 23.0 else min(p[2], 13.5),
                  "small" if p[2] > 23.0 and p[3] in ("med", "knob") else p[3], p[4]) for p in place]
    if any(p[3] == "slider" for p in place):
        place = [(p[0], round(50 + (p[1] - 50) * 0.98, 1), max(p[2], 20.0), p[3], p[4]) for p in place]
    else:
        # prima fila sotto il LED ON/BATT (come sulle foto: LED in alto al centro, pomelli in fila sotto)
        place = [(p[0], p[1], (max(p[2], 16.8) if not rows2 else p[2]) if p[3] in ("med", "knob") else p[2], p[3], p[4])
                 for p in place]
    feet = [(f[0], 50.0, 72.0, f[3]) for f in feet_of(m)[:1]]
    leds = [(round(xmap(l[0]), 1), 4.6, l[2]) for l in leds_of(m)] or [(50.0, 4.6, "blue")]
    if rows2 and len(leds) == 1:
        # due file di comandi: il LED ON/BATT va accanto alla seconda fila (in alto spingerebbe giu' la prima fila
        # fino al bordo del coperchio-pedale)
        xs2 = [p[1] for p in place if p[2] > 23.0]
        leds = [(88.0 if not any(x > 70 for x in xs2) else 12.0, 27.0, leds[0][2])]
    lx, ly = leds[0][0], leds[0][1]
    g = {}
    g["panels"] = [("rect", 7.4, 1.8, 92.6, 35.4, panel, None, 3.2),
                   ("rect", 5.5, 43.4, 94.5, 56.0, None, nink, 1.2, 0.45),
                   ("text", "OUT", 9.5, 40.4, 2.1, nink, "gothic"), ("text", "IN", 91.5, 40.4, 2.1, nink, "gothic")]
    g["panels"].append(("text", "ON/BATT", lx, round(ly - 4.0, 1), 1.45, ptx, "bold") if rows2 and len(leds) == 1 else
                       ("text", "ON/BATT", round(lx + (11.0 if lx < 70 else -11.0), 1), ly, 1.45, ptx, "bold"))
    g["label"] = dict(col=ptx, size=1.85, pos="above", font="bold")
    g["name"] = dict(zone=(44.0, 55.4), font="gothic", col=nink, upper=True, size=4.7, lines=2)
    box = acc if acc else ((24, 24, 26) if lum(body) > 90 else (236, 236, 232))
    g["code"] = dict(y=59.4, x=50, size=3.0, font="gothic", col=body if not acc else (240, 240, 236), box=dict(col=box, padx=1.6, pady=0.7, radius=0.9))
    g["brand"] = dict(x=50, y=41.0, col=nink)
    cover = dict(foot=0, x0=0.6, x1=99.4, y0=37.2, y1=99.6, h0=H - 0.5, h1=33.0, deck=19.0, kind="paint", corner=6.0,
                 print=True, insets=[dict(x0=10.5, x1=89.5, y0=64.5, y1=96.5, kind="rubber", col=(36, 36, 38), corner=7.0,
                                          h=1.3, arch=2.2)])
    lk = dict(shape="bhpl", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="anodized" if body == SILVER else "paint",
              gfx=g, knob="med", profile=[(0, 41.0), (36.2, 44.5), (37.6, 17.0), (100, 17.0)], covers=[cover],
              plan=dict(rear_w=63.0, step=37.0, base_h=18.0, corner=7.0), jack_at=(57.0, 9.0),
              margin=(0.6, 0.6, 0.6, 0.0), knob_pad=0.1, nolab=True, adapt=True)
    if any(p[3] == "slider" for p in place):
        lk["margin"] = (0.0, 0.0, 0.6, 0.0)        # cursori fitti come sugli EQ veri (la sagoma resta sul corpo)
    return lk


def look_vintage(m, col):
    """Serie Vintage in lamiera piegata con la grande 'V' nera (disegni dei manuali VT911, VD1, VT999, VM1, VB1)."""
    i = m["id"]
    a = (m["appearance"].get("dimensions") or {})
    W = float(a.get("width_mm") or 86)
    D = float(a.get("length_mm") or 143)
    H = float(a.get("height_mm") or 70)
    body, acc, _ = col
    blk = BLACK
    place = place_of(m, default="knob")
    feet = feet_of(m)
    leds = leds_of(m)
    g = {"label": dict(col=WHITE, size=2.0, font="bold"), "ticks": True}
    screws = [("circle", 4.5, 3.5, 1.5, (158, 160, 164), (96, 98, 102), 0.3), ("circle", 95.5, 3.5, 1.5, (158, 160, 164), (96, 98, 102), 0.3),
              ("circle", 4.5, 96.5, 1.5, (158, 160, 164), (96, 98, 102), 0.3), ("circle", 95.5, 96.5, 1.5, (158, 160, 164), (96, 98, 102), 0.3)]
    extra = []
    if i in ("bhvt999", "bhvm1"):
        # larghe: braccio sinistro diagonale e colonna destra dei pomelli (la 'V')
        g["panels"] = [("poly", [(4, 9), (30, 9), (60, 97), (40, 97)], blk), ("poly", [(52, 9), (97, 9), (97, 97), (52, 97)], blk),
                       ("hline", 7.0, blk, 0.5, 3, 97)] + screws
        if i == "bhvt999":
            g["panels"] += [("rect", 33.0, 62.0 + k * 4.0, 41.0, 63.6 + k * 4.0, (6, 6, 6), (92, 94, 98), 0.4, 0.3) for k in range(5)]
            g["panels"] += [("rect", 34.5, 66.4, 39.5, 67.0, (220, 96, 40))]
        g["panels"].insert(2, ("rect", 2.5, 41.0, 56.0, 57.5, blk, None, 1.5))
        g["name"] = dict(x=29, y=46.5, size=6.0, font="black", col=(240, 240, 236), upper=True, angle=0, len=50)
        g["sub"] = dict(y=54, x=29, size=2.0, col=(240, 240, 236), font="bold", spacing=0.4)
        g["code"] = dict(y=91, x=28, size=2.8, col=blk, font="bold")
        g["brand"] = dict(x=22, y=14, col=blk)
        feet = [(feet[0][0], feet[0][1], feet[0][2], feet[0][3])]
        prof = [(0, H - 1), (100, H - 4)]
    elif i in ("bhvt911",):
        g["panels"] = [("poly", [(5, 6), (95, 6), (95, 34), (66, 96), (34, 96), (5, 34)], blk)] + screws
        g["panels"] += [("rect", 29.0, 70.0 + k * 3.4, 37.0, 71.4 + k * 3.4, (6, 6, 6), (92, 94, 98), 0.4, 0.3) for k in range(4)]
        g["panels"] += [("rect", 30.5, 73.6, 35.5, 74.2, (220, 96, 40))]
        g["name"] = dict(zone=(40, 58), size=7.5, font="black", col=(240, 240, 236), upper=True, lines=2)
        g["sub"] = dict(size=2.3, col=(240, 240, 236), font="bold", spacing=0.4)
        g["code"] = dict(y=90.5, x=50, size=2.6, col=(240, 240, 236), font="bold")
        g["brand"] = dict(x=50, y=97.5, col=blk)
        prof = [(0, H - 2), (100, H - 5)]
    else:
        # VD1, VB1, VP1: fascia nera dei comandi in alto, nome sul metallo, imbuto nero col footswitch in basso
        top = 34.0 if i == "bhvd1" else 30.0
        g["panels"] = [("poly", [(4, 2.5), (96, 2.5), (92, top), (8, top)], blk),
                       ("poly", [(16, 61), (84, 61), (78, 95.5), (22, 95.5)], blk)] + screws
        g["name"] = dict(zone=(top + 2, 59.5), size=8.0, font="black", col=blk, upper=True, lines=2)
        g["sub"] = dict(size=2.2, col=blk, font="bold", spacing=0.4)
        g["code"] = dict(y=86, x=50, size=2.6, col=(240, 240, 236), font="bold")
        g["brand"] = dict(x=50, y=92, col=(240, 240, 236))
        feet = [(feet[0][0], 50.0, 73.0, feet[0][3])]
        prof = [(0, H - 2), (100, H - 5)]
        if i == "bhvp1":
            place = [("RATE", 50, 15, "big", []), ("TONE", 84, 9, "lever", [])]
            leds = [(16, 9, "blue")]
    return dict(shape="bhsheet", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="brushed" if False else "satin",
                gfx=g, profile=prof, extra=extra, adapt=True)


def look_bh9(m, col):
    W, D, H = 69.0, 113.0, 56.0
    body, acc, _ = col
    tx = acc or ink(body)
    place = place_of(m, default="knob")
    feet = [(f[0], 50.0, 80.0, f[3]) for f in feet_of(m)[:1]]
    leds = leds_of(m) or [(50, 40, "blue")]
    g = {"label": dict(col=tx, size=2.0, font="bold"),
         "panels": [("hline", 50.0, tx, 0.5, 8, 92), ("hline", 63.0, tx, 0.5, 8, 92)],
         "name": dict(zone=(50.5, 62.5), font="black", col=tx, upper=True, size=4.6),
         "sub": dict(size=1.9, col=tx, font="bold", spacing=0.3),
         "code": dict(y=95.5, x=50, size=2.4, col=tx, font="black"), "brand": dict(x=50, y=4, col=tx)}
    if m["id"] == "bhph9":
        place = [("RATE", 50, 26, "big", []), ("SWIRL", 22, 12, "lever", [])]
    return dict(shape="bh9", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="paint", gfx=g,
                profile=[(0, H), (100, H - 2.5)], adapt=True)


def look_vtone(m, col):
    """V-Tone Driver/DI (foto Commons BDI21 e documento informativo): plastica, fronte a U, grande pulsante in un ovale."""
    i = m["id"]
    W, D, H = 100.0, 127.0, 50.0
    body, acc, _ = col
    tx = acc or ink(body)
    place = [(p[0], p[1], round(p[2] + 4.5, 1), p[3], p[4]) for p in place_of(m, default="knob")]
    leds = [(l[0], l[1] + 4.5, l[2]) for l in leds_of(m)] or [(50, 28, "red")]
    outline = [(3, 0), (97, 0), (100, 3), (100, 60), (98, 76), (93, 87), (85, 94.5), (72, 98.8), (50, 100), (28, 98.8),
               (15, 94.5), (7, 87), (2, 76), (0, 60), (0, 3)]
    lower = lighter(body, 0.10) if lum(body) < 200 else darker(body, 0.93)
    g = {"label": dict(col=tx, size=2.0, font="bold"),
         "panels": [("poly", arc_poly(-2, 102, 46.0, 4.0, 102), lower), ("hline", 46.2, darker(body, 0.75), 0.0, 0, 0),
                    ("ellipse", 50, 80, 25.0, 11.0, (120, 122, 126), 1.6), ("ellipse", 50, 80, 23.2, 9.6, (236, 238, 240), 0.7)],
         "name": dict(x=70, y=5.0, font="black", col=tx, upper=True, size=4.4),
         "sub": dict(text="AMP MODELER - DRIVER - DI", y=10.2, x=50, size=1.7, col=tx, font="bold", spacing=0.6),
         "code": dict(y=5.0, x=17, size=3.6, font="blackit", col=tx), "real_code": False,
         "real_texts": [("text", "V-TONE", 17, 5.0, 3.6, tx, "blackit")],
         "brand": dict(x=50, y=57, col=tx)}
    g["panels"] = [p for p in g["panels"] if not (p[0] == "hline" and p[3] == 0.0)]
    return dict(shape="bhvtone", dims=(W, D, H), place=place, feet=[("ON/OFF", 50.0, 80.0, ["ON/OFF (TONE FUNCTIONS)"])],
                leds=leds, finish="anodized" if lum(body) > 150 else "paint", gfx=g,
                profile=[(0, H - 1), (45, H - 6), (100, H - 16)], outline=outline, outline_profile=True, foot_kind="big",
                jack_at=(22.0, 18.0), adapt=True)


def look_ab200(m, col):
    W, D, H = 134.0, 89.0, 46.0
    body, acc, _ = col
    tx = acc or ink(body)
    covers = []
    for j, (x0, x1) in enumerate(((5.0, 41.0), (59.0, 95.0))):
        covers.append(dict(foot=j, x0=x0, x1=x1, y0=24.0, y1=97.0, h0=H + 2.0, h1=H - 6.0, kind="plastic", corner=4.0,
                           col=(28, 28, 30), print=False, insets=[dict(x0=x0 + 4, x1=x1 - 4, y0=36.0, y1=93.0, kind="rubber",
                                                                       col=(20, 20, 22), ribs=3.0, ribs_col=(40, 40, 43), h=0.6)]))
    leds = [(10, 12, "green"), (35, 12, "red"), (65, 12, "green"), (90, 12, "red")]
    g = {"label": dict(col=tx, size=2.0, font="bold"),
         "panels": [("text", "1", 23, 30.0, 5.0, (236, 236, 230), "black"), ("text", "2", 77, 30.0, 5.0, (236, 236, 230), "black"),
                    ("text", "A", 10, 5.5, 2.0, tx, "bold"), ("text", "B", 35, 5.5, 2.0, tx, "bold"),
                    ("text", "A", 65, 5.5, 2.0, tx, "bold"), ("text", "B", 90, 5.5, 2.0, tx, "bold"),
                    ("hline", 57.0, tx, 0.5, 45, 55)],
         "name": dict(x=50, y=80, angle=90, font="black", col=tx, upper=True, size=3.4, len=36),
         "code": dict(y=61, x=50, size=2.6, col=tx, font="black"), "brand": dict(x=50, y=20, col=tx)}
    feet = [("SWITCH 1", 23.0, 62.0, ["1"]), ("SWITCH 2", 77.0, 62.0, ["2"])]
    return dict(shape="bhbox", dims=(W, D, H), place=[("LATCH", 50, 8, "lever", ["LATCH 1", "LATCH/NON LATCH"])], feet=feet,
                leds=leds, finish="paint", gfx=g, profile=[(0, H), (100, H - 4)], covers=covers, adapt=True)


def look_bm(m, col):
    """Serie BM (QSG Behringer): scatola verticale 147.6 x 196.9 x 85.9 mm, piano inclinato, fiancate rialzate, sezioni
    separate da filetti, pomelli grandi con scala 0-10."""
    i = m["id"]
    W, D, H = 147.6, 196.9, 85.9
    body, acc, _ = col
    tx = acc or BLACK
    place = place_of(m, default="big")
    feet = feet_of(m)
    feet = [(f[0], f[1], f[2], f[3]) for f in feet]
    leds = leds_of(m, 4)
    secs = {"bhbm11m": (30.5, 51.0), "bhbm12": (30.5, 51.0), "bhbm13": (30.5, 51.0), "bhbm14m": (33.0, 51.5),
            "bhbm15m": (32.0, 52.0), "bhbm17": (33.0, 53.5), "bhbm18m": (33.0, 51.5)}[i]
    g = {"label": dict(col=tx, size=2.4, font="bold"), "ticks": True,
         "panels": [("hline", secs[0], tx, 0.5, 8, 92), ("hline", secs[1], tx, 0.5, 8, 92), ("hline", 68.0, tx, 0.4, 8, 92)],
         "name": dict(x=74, y=8.5, font="black", col=tx, upper=True, size=3.6),
         "code": dict(y=4.5, x=74, size=4.4, font="black", col=tx), "brand": dict(x=22, y=6.0, col=tx)}
    if i == "bhbm15m":
        g["panels"].append(("rect", 20.0, 12.0, 81.0, 30.0, None, tx, 1.0, 0.5))
    g["panels"] += [("rect", -1, -1, 2.2, 101, (60, 60, 64)), ("rect", 97.8, -1, 101, 101, (60, 60, 64))]
    return dict(shape="bhbm", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="satin", gfx=g,
                profile=[(0, H - 2), (100, 52.0)], margin=(3.0, 3.0, 0.0, 0.0), adapt=True)


def look_modern(m, col):
    i = m["id"]
    a = m["appearance"].get("dimensions") or {}
    est = {"bhzentaramini": (50, 100, 48), "bhfuzzbender": (78, 142, 52), "bhoctavia": (90, 118, 54)}
    W, D, H = [float(v) for v in (est.get(i) or (a.get("width_mm"), a.get("length_mm"), a.get("height_mm")))]
    body, acc, panel = col
    tx = acc or ink(body)
    place = place_of(m, default="knob")
    feet = feet_of(m)
    leds = leds_of(m)
    g = {"label": dict(col=tx, size=2.2, font="bold")}
    lk = dict(shape="bhbox", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="paint", gfx=g,
              profile=[(0, H), (100, H - 2)], adapt=True)
    if i in ("bhzentara", "bhzentaramini"):
        mini = i == "bhzentaramini"
        lk["finish"] = "sparkle"
        lk["knob"] = "small" if mini else "big"
        if mini:
            lk["place"] = [("GAIN", 26, 22, "small", []), ("TREBLE", 50, 34, "small", []), ("OUTPUT", 74, 22, "small", [])]
            lk["feet"] = [(feet[0][0], 50.0, 82.0, feet[0][3])]
            lk["leds"] = [(50, 52, "red")]
        else:
            lk["place"] = [(p[0], p[1], 25.0, "big", p[4]) for p in place]
            lk["leds"] = [(9, 47, "red")]
            lk["feet"] = [(feet[0][0], 76.0, 75.0, feet[0][3])]
        g["label"] = dict(col=tx, size=2.4, font="serif")
        g["name"] = dict(x=74 if not mini else 50, y=60 if not mini else 63, font="serif", col=tx, upper=True, size=5.6 if not mini else 3.8)
        g["sub"] = dict(size=2.2 if not mini else 1.7, col=tx, font="serif", spacing=0.5)
        g["code"] = dict(y=94 if not mini else 95.5, x=50, size=2.4, col=tx, font="serif")
        g["brand"] = dict(x=50, y=5 if not mini else 4.5, col=tx)
        if not mini:
            g["panels"] = [("circle", 26, 70, 13.0, None, tx, 0.5), ("circle", 26, 70, 10.5, None, tx, 0.3)]
    elif i == "bhfuzzbender":
        lk["outline"] = [(24, 0), (76, 0), (100, 24), (90, 100), (10, 100), (0, 24)]
        lk["outline_profile"] = True
        lk["finish"] = "hammer"
        lk["place"] = [("LEVEL", 32, 32, "knob", []), ("ATTACK", 68, 32, "knob", []), ("ORIG/MOD", 50, 39, "lever", ["MOD", "ORIG"])]
        lk["leds"] = [(50, 24, "red")]
        lk["feet"] = [(feet[0][0], 50.0, 70.0, feet[0][3])]
        g["name"] = dict(zone=(3.0, 21.0), font="bookman", col=tx, upper=True, size=5.0, lines=2)
        g["code"] = dict(y=92, x=50, size=2.4, col=tx, font="bold")
        g["brand"] = dict(x=50, y=86, col=tx)
        lk["jack_at"] = (30.0, 22.0)
    elif i == "bhchorussymphony":
        pnl = panel or (34, 36, 40)
        g["panels"] = [("rect", 3, 4, 97, 48, pnl, None, 2.0), ("hline", 62.0, (40, 40, 44), 0.5, 4, 96)]
        g["label"] = dict(col=WHITE, size=2.2, font="bold")
        g["name"] = dict(x=50, y=55, font="gothic", col=tx, upper=True, size=5.0)
        g["sub"] = dict(size=2.0, col=tx, font="bold", spacing=0.5)
        g["code"] = dict(y=94, x=50, size=2.6, col=tx, font="bold")
        g["brand"] = dict(x=88, y=8, col=WHITE)
        g["foot_col"] = tx
        lk["leds"] = [(25, 18, "red"), (25, 66, "green"), (75, 66, "red")]
        lk["feet"] = [(f[0], f[1], 84.0, f[3]) for f in feet]
    elif i == "bhoctavia":
        g["name"] = dict(x=50, y=52, font="bookmanit", col=tx, upper=False, size=7.0)
        g["code"] = dict(y=94, x=50, size=2.4, col=tx, font="bold")
        g["brand"] = dict(x=50, y=8, col=tx)
        g["panels"] = [("ellipse", 50, 52, 30.0, 9.0, tx, 0.6)]
        lk["leds"] = [(50, 64, "red")]
        lk["feet"] = [(feet[0][0], 50.0, 80.0, feet[0][3])]
    elif i == "bhbassbrassmaster":
        g["name"] = dict(x=50, y=62, font="serif", col=tx, upper=True, size=4.6)
        g["code"] = dict(y=95, x=50, size=2.4, col=tx, font="bold")
        g["brand"] = dict(x=18, y=94, col=tx)
        g["panels"] = [("rect", 4, 54, 96, 70, None, tx, 2.0, 0.6)]
        lk["leds"] = [(50, 74, "red")]
        lk["feet"] = [(feet[0][0], 50.0, 85.0, feet[0][3])]
        lk["jack_at"] = (8.0, 30.0)
    elif i == "bhbtron3":
        g["panels"] = [("rect", 4, 3, 96, 62, panel or (30, 30, 32), None, 2.0)]
        g["label"] = dict(col=WHITE, size=2.2, font="bold")
        g["foot_col"] = tx
        g["name"] = dict(x=50, y=73, font="blackit", col=tx, upper=True, size=6.0)
        g["code"] = dict(y=95, x=50, size=2.4, col=tx, font="bold")
        g["brand"] = dict(x=50, y=6, col=WHITE)
        lk["leds"] = [(50, 66, "red")]
    elif i == "bhdualphase":
        g["panels"] = [("rect", 3, 4, 63, 33, None, tx, 1.5, 0.5), ("rect", 66, 4, 97, 33, None, tx, 1.5, 0.5),
                       ("rect", 3, 36, 37, 74, None, tx, 1.5, 0.5), ("rect", 40, 36, 97, 74, None, tx, 1.5, 0.5),
                       ("text", "SWEEP GENERATOR 1", 18, 6.5, 1.8, tx, "bold"), ("text", "SWEEP GEN 2", 81, 6.5, 1.8, tx, "bold"),
                       ("text", "PHASOR A", 14, 38.5, 1.8, tx, "bold"), ("text", "PHASOR B", 86, 38.5, 1.8, tx, "bold")]
        g["name"] = dict(x=50, y=86, font="gothic", col=tx, upper=True, size=4.2, spacing=0.8)
        g["code"] = dict(y=96, x=50, size=2.2, col=tx, font="bold")
        g["brand"] = dict(x=52, y=45, col=tx)
        lk["leds"] = [(10, 41, "red"), (90, 41, "red")]
        lk["feet"] = [(f[0], f[1], f[2], f[3]) for f in feet]
    elif i == "bhoctavedivider":
        pnl = panel or (24, 24, 26)
        g["panels"] = [("rect", 7, 6, 49, 50, None, pnl, 3.0, 0.8), ("rect", 7, 52, 49, 72, None, pnl, 2.0, 0.6),
                       ("rect", 52, 52, 93, 72, None, pnl, 2.0, 0.6)]
        g["name"] = dict(x=73, y=20, font="gothic", col=tx, upper=False, size=4.0, spacing=0.4)
        g["code"] = dict(y=96, x=50, size=2.2, col=tx, font="bold")
        g["brand"] = dict(x=73, y=10, col=tx)
        lk["leds"] = [(86, 42, "red")]
        lk["feet"] = [(f[0], f[1], f[2], f[3]) for f in feet]
    elif i == "bh69vibe":
        g["name"] = dict(x=50, y=12, font="script", col=tx, upper=False, size=8.0)
        g["code"] = dict(y=95, x=50, size=2.4, col=tx, font="bold")
        g["brand"] = dict(x=50, y=80, col=tx)
        g["ticks"] = True
        lk["profile"] = [(0, H - 4), (6, H), (100, H - 2)]
        lk["leds"] = [(12, 52, "red"), (27, 52, "red"), (82, 52, "red")]
        lk["feet"] = [(feet[1][0] if len(feet) > 1 else "EFFECT", 82.0, 74.0, []), (feet[0][0], 20.0, 74.0, feet[0][3])] \
            if feet[0][0] != "EFFECT" else [(f[0], f[1], 74.0, f[3]) for f in feet]
    elif i in ("bhmeatball", "bhringstinger", "bhflangenoname"):
        # cloni Lovetone (QSG Behringer): fascia scura delle prese in alto, piramide chiara al centro
        hdr = (30, 30, 32)
        pyr = lighter(body, 0.35)
        g["panels"] = [("rect", 2.5, 2.5, 97.5, 10.5, hdr, None, 1.0), ("poly", [(50, 44), (78, 97), (22, 97)], pyr)]
        g["label"] = dict(col=tx, size=2.2, font="gothic")
        g["name"] = dict(x=50, y=86 if i == "bhmeatball" else 70, font="gothicit", col=tx, upper=False, size=7.0)
        g["code"] = dict(y=95.5, x=88, size=2.2, col=tx, font="bold")
        g["brand"] = dict(x=12, y=95.5, col=tx)
        jl = {"bhmeatball": ["DC In", "Output", "Pedal1", "Pedal2", "FX Loop", "In"],
              "bhringstinger": ["DC In", "Output", "LFO Depth", "VCO CV", "Carrier", "In"],
              "bhflangenoname": ["DC In", "Space", "Time Out", "CV In", "Trig", "In"]}[i]
        for k, s in enumerate(jl):
            g["panels"].append(("text", s, 10 + k * 16, 6.5, 1.8, WHITE, "gothic"))
        lk["feet"] = [(f[0], f[1], f[2], f[3]) for f in feet]
        lk["profile"] = [(0, H), (100, H - 1)]
    return lk


def look_cvmini(m, col):
    i = m["id"]
    pro = i == "cvlooperpro"
    W, D, H = 40.0, 90.0, 49.5 if pro else 34.5
    body, acc, _ = col
    tx = acc or ink(body)
    place = place_of(m, default="small")
    leds = leds_of(m) or [(50, 52, "red")]
    g = {"label": dict(col=tx, size=1.5, font="bold"),
         "name": dict(zone=(56.0, 68.0), font="black", col=tx, upper=True, size=3.0),
         "code": dict(y=95.5, x=50, size=1.9, col=tx, font="bold"), "brand": dict(x=50, y=4.0, col=tx)}
    lk = dict(shape="cvmini", dims=(W, D, H), place=place, feet=[("ON/OFF", 50.0, 81.0, [])], leds=leds, finish="paint",
              gfx=g, knob="small", profile=[(0, H), (100, H - 1)], foot_kind="flat", adapt=True)
    if len(place) == 1:
        lk["place"] = [(place[0][0], 50, 22, "knob", place[0][4])]
        lk["knob"] = "knob"
    if pro:
        lk["place"] = [("LOOP LEVEL", 50, 13, "small", ["LOOP", "LEVEL"]), ("UP", 30, 50, "button", ["UP/DOWN"]),
                       ("DOWN", 70, 50, "button", [])]
        lk["extra"] = [("display", 18, 22, 82, 42)]
        lk["leds"] = [(50, 57, "red")]
        g["name"] = dict(zone=(59.0, 69.0), font="black", col=tx, upper=True, size=2.8)
    return lk


def look_cvbox(m, col):
    i = m["id"]
    W, D, H = (50.0, 90.0, 45.0) if i == "cvirbox" else (68.0, 112.0, 40.0)
    body, acc, _ = col
    tx = acc or ink(body)
    place = place_of(m, default="knob")
    leds = leds_of(m) or [(60, 55, "red")]
    feet = feet_of(m, (50.0, 82.0))
    g = {"label": dict(col=tx, size=1.9, font="bold"),
         "name": dict(zone=(58.0, 70.0), font="black", col=tx, upper=True, size=3.8),
         "code": dict(y=96, x=50, size=2.1, col=tx, font="bold"), "brand": dict(x=50, y=4.0, col=tx)}
    lk = dict(shape="cvbox", dims=(W, D, H), place=place, feet=[(f[0][:12], f[1], f[2], f[3]) for f in feet], leds=leds,
              finish="paint", gfx=g, knob="knob", profile=[(0, H), (100, H - 1)], foot_kind="flat", adapt=True)
    if i == "cvirbox":
        lk["place"] = [("LOW CUT", 22, 13, "small", ["LOWCUT"]), ("VOL", 50, 25, "small", ["VOLUME"]),
                       ("HI CUT", 78, 13, "small", ["HICUT"]), ("PREV", 28, 63, "button", ["<"]), ("NEXT", 72, 63, "button", [">"])]
        lk["extra"] = [("display", 34, 36, 66, 52)]
        lk["feet"] = [("BYPASS", 50.0, 84.0, [])]
        lk["leds"] = [(50, 72, "red")]
        g["name"] = dict(zone=(3.0, 9.0), font="black", col=tx, upper=True, size=3.0)
        g["code"] = dict(y=96.5, x=50, size=1.8, col=tx, font="bold")
        g["brand"] = dict(x=50, y=92, col=tx)
    elif i == "cvloop2":
        lk["place"] = [("LOOP", 50, 16, "big", ["LEVEL"]), ("MEMORY", 22, 34, "button", ["MEM"]),
                       ("SPEED", 78, 34, "button", []), ("REC", 50, 44, "button", ["READY"])]
        lk["leds"] = [(50, 34, "blue"), (50, 58, "red")]
    elif i in ("cvelementalpro", "cvgalaxia"):
        lk["leds"] = [(50, 60, "green")]
        g["name"] = dict(zone=(52.0, 66.0), font="black", col=tx, upper=True, size=3.6)
    if i == "cvgalaxia":
        g["panels"] = [("circle", 18, 66, 1.0, (240, 240, 255)), ("circle", 84, 52, 0.8, (240, 240, 255)),
                       ("circle", 30, 92, 0.7, (240, 240, 255)), ("circle", 72, 94, 1.1, (240, 240, 255)),
                       ("circle", 10, 50, 0.6, (240, 240, 255)), ("circle", 92, 70, 0.9, (240, 240, 255))]
    if i == "cvminiuniverse":
        g["panels"] = [("ellipse", 50, 64, 22.0, 6.0, lighter(body, 0.4), 0.5)]
        lk["leds"] = [(80, 55, "red")]
    return lk


def look_cvbar(m, col):
    """Barretta Cube Baby 161 x 57 x 17.5 mm (manuale): 10 pomellini in fila, 3 footswitch A/B/C, LED."""
    W, D, H = 161.0, 57.0, 17.5
    body, acc, _ = col
    tx = acc or ink(body)
    place = place_of(m, default="small", skip=("POWER",))
    feet = [("A", 12.0, 74.0, []), ("B", 50.0, 74.0, []), ("C", 88.0, 74.0, [])]
    leds = [(66, 9, "blue"), (12, 52, "green"), (50, 52, "green"), (88, 52, "green")]
    g = {"label": dict(col=tx, size=1.5, font="bold"),
         "panels": [("hline", 44.0, tx, 0.3, 4, 96)],
         "name": dict(x=31, y=60, font="black", col=tx, upper=True, size=3.0),
         "code": dict(y=60, x=69, size=2.2, col=tx, font="bold"), "brand": dict(x=50, y=94, col=tx)}
    lk = dict(shape="cvbar", dims=(W, D, H), place=place, feet=feet, leds=leds, finish="paint", gfx=g, knob="small",
              profile=[(0, H), (100, H - 0.5)], foot_kind="flat", knob_pad=0.2, nolab=True, adapt=True)
    return lk


# ritocchi per modello (comandi della scheda funzionale senza posizione nella scheda dell'aspetto, footswitch di sezione)
TWEAKS = {
    "bhus600": dict(margin=(1.4, 1.4, 0.6, 0.0)),
    "bhrm600": dict(place_add=[("SPEED", 50, 27, "lever", ["FAST/SLOW"])]),
    "bhab100": dict(sections=True, feet=[("A/B", 50.0, 72.0, ["SELECT"])], leds=[(36.0, 4.6, "red"), (61.0, 4.6, "green"), (84.0, 4.6, "red")],
                    place_all=[("MODE", 16.0, 20.0, "lever", ["SELECTOR"]), ("A LEVEL", 60.0, 21.0, "med", ["A LVL"]),
                               ("B LEVEL", 83.5, 21.0, "med", ["B LVL"])]),
    "bhab200": dict(sections=True, feet=[("A/B 1", 23.0, 62.0, ["SWITCH 1", "1"]), ("A/B 2", 77.0, 62.0, ["SWITCH 2", "2"])],
                    place_all=[("LATCH 1", 50, 31, "lever", []), ("LATCH 2", 50, 47, "lever", [])]),
    "bhchorussymphony": dict(feet_alias={"VIBRATO/CHORUS": ["MODE"]}),
    "bh69vibe": dict(feet_alias={"CHORUS/VIBRATO": ["MODE"]}),
    "bhflangenoname": dict(place_add=[("MODE", 86, 50, "knob", ["SPACE", "TIME/SPACE"])]),
    "cvlooper": dict(feet=[("REC/PLAY", 50.0, 81.0, ["ON/OFF"])],
                     place_all=[("LOOP", 50, 21, "knob", ["LEVEL"]), ("STOP", 27, 40, "button", []), ("CLEAR", 73, 40, "button", []),
                                ("UNDO", 50, 51, "button", [])], leds=[(50, 63, "red")], name_zone=(3.5, 10.5)),
    "cvlooperpro": dict(feet=[("REC/PLAY", 50.0, 81.0, ["ON/OFF"])],
                        place_all=[("LOOP LEVEL", 50, 11, "small", ["LOOP", "LEVEL"]), ("STOP", 27, 45, "button", ["UP"]),
                                   ("CLEAR", 73, 45, "button", ["DOWN"]), ("UNDO", 50, 55, "button", [])],
                        leds=[(50, 65, "red")], extra=[("display", 16, 19, 84, 37)], name_zone=(37.0, 41.0)),
    "cvloop2": dict(feet=[("REC/PLAY", 50.0, 82.0, ["ON/OFF"])],
                    place_all=[("LOOP", 50, 16, "big", ["LEVEL"]), ("STOP", 22, 34, "button", ["MEMORY"]),
                               ("CLEAR", 78, 34, "button", ["SPEED"]), ("UNDO", 50, 46, "button", ["READY", "REC"])],
                    leds=[(50, 34, "blue"), (50, 58, "red")]),
    "cvirbox": dict(place_all=[("LOW CUT", 22, 13, "small", ["LOWCUT"]), ("VOL", 50, 24, "small", ["VOLUME"]),
                               ("HI CUT", 78, 13, "small", ["HICUT"]), ("PRESET", 50, 64, "small", ["PREV", "NEXT"]),
                               ("<", 22, 64, "button", ["PREV"]), (">", 78, 64, "button", ["NEXT"])]),
}


def apply_tweaks(m, lk):
    tw = TWEAKS.get(m["id"]) or {}
    if "place_all" in tw:
        lk["place"] = [tuple(e) for e in tw["place_all"]]
    if "place_add" in tw:
        lk["place"] = list(lk["place"]) + [tuple(e) for e in tw["place_add"]]
    if "feet" in tw:
        lk["feet"] = [tuple(f) for f in tw["feet"]]
    if "feet_alias" in tw:
        lk["feet"] = [(f[0], f[1], f[2], list(f[3]) + tw["feet_alias"].get(f[0], [])) for f in lk["feet"]]
    for k in ("sections", "leds", "extra", "margin"):
        if k in tw:
            lk[k] = tw[k]
    if "name_zone" in tw:
        lk["gfx"]["name"] = dict(lk["gfx"].get("name") or {}, zone=tw["name_zone"])
        lk["gfx"]["name"].pop("y", None)
    lk["inner_skip"] = True
    return lk


def mold_of(m):
    i = m["id"]
    if i in TREADLES:
        return "treadle"
    if i.startswith("cv"):
        if i.startswith("cvcubebaby"):
            return "cvbar"
        if i in ("cvminiamp", "cvminiefx", "cvloop2", "cvminiuniverse", "cvelemental", "cvelementalpro", "cvclassicdelay",
                 "cvgalaxia", "cvirbox"):
            return "cvbox"
        return "cvmini"
    if i in ("bhvt911", "bhvt999", "bhvd1", "bhvm1", "bhvb1", "bhvp1"):
        return "vintage"
    if i in ("bhdc9", "bhig9", "bhph9"):
        return "bh9"
    if i in ("bhbdi21", "bhgdi21", "bhadi21"):
        return "vtone"
    if i == "bhab200":
        return "ab200"
    if i.startswith("bhbm"):
        return "bm"
    if "plastica" in m["series"]:
        return "bhpl"
    return "modern"


TREADLES = {"bhhb01", "bhfcv100", "bhfc600", "bhwahwah", "bhparapedal", "bhpedalflanger", "bhbflanger"}
BUILD = {"bhpl": look_bhpl, "vintage": look_vintage, "bh9": look_bh9, "vtone": look_vtone, "ab200": look_ab200,
         "bm": look_bm, "modern": look_modern, "cvmini": look_cvmini, "cvbox": look_cvbox, "cvbar": look_cvbar}


# ------------------------------------------------------------------ bilancieri
def treadle_entry(m):
    i = m["id"]
    a = m["appearance"].get("dimensions") or {}
    W, D, H = float(a.get("width_mm") or 110), float(a.get("length_mm") or 250), float(a.get("height_mm") or 80)
    body, acc, _ = COL[i]
    if i == "bhhb01":
        tl = dict(shape="crybaby", dims=(W, D, H), leds=[("H", -0.4, "red"), ("H", 0.4, "yellow")], finish="crinkle",
                  tread=("ribs", (20, 20, 22), (20, 20, 22)))
        ps = [("RANGE", "R", 66, "big", []), ("BOOST SW", "R", 40, "lever", ["BOOST ON"]), ("BOOST", "L", 78, "knob", []),
              ("Q", "L", 62, "knob", []), ("FINE", "L", 46, "knob", [])]
        pedal = "PEDAL"
    elif i == "bhfcv100":
        tl = dict(shape="dvp1", dims=(W, D, min(H, 80.0)), leds=[], finish="paint", tread=("diamonds", (22, 22, 24), (34, 34, 36)))
        ps = [("CV/NORM", "L", 86, "lever", ["MODE"]), ("MIN VOL", "R", 86, "knob", ["MIN"])]
        pedal = "PEDAL"
    elif i == "bhfc600":
        tl = dict(shape="dvp1", dims=(W, D, min(H, 90.0)), leds=[], finish="hammer", rocker=(176, 178, 182), rocker_finish="hammer",
                  tread=("grip", (24, 24, 26), (24, 24, 26)))
        ps = [("MIN VOL", "R", 18, "knob", ["MIN"])]
        pedal = "PEDAL"
    elif i == "bhwahwah":
        tl = dict(shape="crybaby", dims=(W, D, H), leds=[("R", 82, "red")], finish="crinkle", rocker=(200, 202, 206),
                  rocker_finish="chrome", tread=("ribs", (20, 20, 22), (20, 20, 22)))
        ps = []
        pedal = "PEDAL"
    elif i in ("bhparapedal", "bhpedalflanger"):
        tl = dict(shape="junior", dims=(W, D, H), leds=[("H", 0.0, "red")], finish="hammer", rocker=(176, 178, 182),
                  rocker_finish="hammer", tread=("ribs", (60, 62, 66), (176, 178, 182)))
        ps = [("SPREAD", "L", 86, "knob", []), ("AUTO/MAN", "R", 72, "lever", ["AUTO/MANUAL", "MODE"]),
              ("INTENSITY", "R", 86, "knob", [])] if i == "bhpedalflanger" else []
        pedal = "PEDAL"
    else:   # bhbflanger
        tl = dict(shape="wide", dims=(W, D, min(H, 90.0)), leds=[("P", (0.5, 60), "red")], finish="crinkle",
                  tread=("studs", (24, 24, 26), (40, 40, 44)))
        ps = [("STOP", "P", 0.5, 88, "knob", []), ("START", "P", 0.5, 72, "knob", []), ("RATE", "P", 0.5, 56, "knob", ["OSC", "RATE (OSC)"]),
              ("FEEDBACK", "P", 0.5, 40, "knob", ["FDBK"]), ("PEDAL", "P", 0.5, 16, "button", ["MODE", "AUTO"])]
        pedal = "SWEEP"
    return tl, ps, pedal


def treadle_placeholder(m, pedal):
    i = m["id"]
    if i in ("bhfcv100", "bhfc600"):
        rest = ["knob('MIN VOL', 'min', 0.0, 'percent', 0, 100)"]
        if i == "bhfcv100":
            rest.insert(0, "toggle('CV/NORM', 'mode', ('NORM', 'CV'), 0)")
        return "Volume", ["knob(%r, 'volume', 1.0, 'percent', 0, 100)" % pedal] + rest, ""
    if i == "bhhb01":
        return "Wah", ["knob('PEDAL', 'freq', 0.5)", "knob('RANGE', 'freq', 0.5)", "toggle('BOOST SW', 'mode', ('OFF', 'ON'), 0)",
                       "knob('BOOST', 'level')", "knob('Q', 'q')", "knob('FINE', 'peak')"], "type=pedal f=350,2200 q=3,8 filt=bp"
    if i == "bhpedalflanger":
        return "BBDFlanger", ["knob('PEDAL', 'manual', 0.5)", "knob('SPREAD', 'depth')", "toggle('AUTO/MAN', 'mode', ('MAN', 'AUTO'), 0)",
                              "knob('INTENSITY', 'res')"], GI.CFG["BBDFlanger"]
    if i == "bhbflanger":
        return "BBDFlanger", ["knob('SWEEP', 'manual', 0.5)", "knob('STOP', 'depth')", "knob('START', 'manual')",
                              "knob('RATE', 'rate')", "knob('FEEDBACK', 'res')"], GI.CFG["BBDFlanger"]
    return "Wah", ["knob('PEDAL', 'freq', 0.5)"], "type=pedal f=350,2200 q=3,6 filt=bp"


# ------------------------------------------------------------------ nomi reali
REAL = {
    "bhzentara": ("Zentara", "Zentara Overdrive", "Zentara"), "bhzentaramini": ("Zentara Mini", "Zentara Mini Overdrive", "Zentara"),
    "bhfuzzbender": ("Fuzz Bender", "Fuzz Bender", "Fuzz Bender"),
    "bhchorussymphony": ("Chorus Symphony", "Chorus Symphony", "Chorus Symphony"),
    "bhoctavia": ("Octavia", "Octavia Octave Fuzz", "Octavia"), "bhbassbrassmaster": ("Bass Brassmaster", "Bass Brassmaster Fuzz", "Bass Brassmaster"),
    "bhbtron3": ("B-Tron III", "B-Tron III Envelope Filter", "B-Tron III"), "bhdualphase": ("Dual-Phase", "Dual-Phase", "Dual-Phase"),
    "bhoctavedivider": ("Octave Divider", "Octave Divider", "octave divider"),
    "bhbflanger": ("B-Flanger", "B-Flanger", "B-Flanger"), "bhparapedal": ("Parapedal", "Parapedal Wah Filter", "Parapedal"),
    "bhpedalflanger": ("Pedalflanger", "Pedalflanger", "Pedalflanger"), "bh69vibe": ("69 Vibe", "69 Vibe", "69 Vibe"),
    "bhwahwah": ("WahWah", "WahWah", "WahWah"), "bhmeatball": ("Meatball", "Meatball", "Meatball"),
    "bhringstinger": ("Ring Stinger", "Ring Stinger", "Ring Stinger"),
    "bhflangenoname": ("Flange No Name", "The Flange With No Name", "The Flange with No Name"),
    "bhbdi21": ("BDI21", "V-Tone Bass Driver DI", "BASS BDI 21"), "bhgdi21": ("GDI21", "V-Tone Guitar Driver DI", "GUITAR GDI 21"),
    "bhadi21": ("ADI21", "V-Tone Acoustic Driver DI", "ACOUSTIC ADI 21"),
    "bhhb01": ("HB01", "Hellbabe", "Hell Babe"),
    "cvcubebaby": ("Cube Baby", "Cube Baby", "CUBE BABY"), "cvcubebabybass": ("Cube Baby Bass", "Cube Baby Bass", "CUBE BABY BASS"),
    "cvcubebabyac": ("Cube Baby AC", "Cube Baby AC", "CUBE BABY AC"), "cvminiamp": ("PD33", "Mini-Amp", "MINI-AMP"),
    "cvminiefx": ("PD32", "Mini-EFX", "MINI-EFX"), "cvoverdrivets": ("Overdrive-TS", "Overdrive-TS", "Overdrive-TS"),
    "cvoverdriveblues": ("Overdrive-Blues", "Overdrive-Blues", "Overdrive-Blues"),
    "cvoverdrivedb": ("Overdrive-DB", "Overdrive-DB", "Overdrive-DB"), "cvoverdriveamp": ("Overdrive-AMP", "Overdrive-AMP", "Overdrive-AMP"),
    "cvdistbritish": ("Dist-British", "Distortion-British", "DIST-British"), "cvdistsc1": ("Dist-SC1", "Distortion-SC1", "Distortion-SC1"),
    "cvdistmt1": ("Dist-MT1", "Distortion-MT1", "DIST-MT1"), "cvfuzz": ("Fuzz", "Fuzz", "FUZZ"), "cvbooster": ("Booster", "Booster", "BOOSTER"),
    "cvcompressor": ("Compressor", "Compressor", "COMPRESSOR"), "cvnoisegate": ("Noise Gate", "Noise Gate", "NOISE GATE"),
    "cvchorus": ("Chorus", "Chorus", "CHORUS"), "cvphaser": ("Phaser", "Phaser", "PHASER"), "cvtremolo": ("Tremolo", "Classic Tremolo", "TREMOLO"),
    "cvdelaypd18": ("PD18", "Classic Delay", "CLASSIC DELAY"), "cvdigdelay": ("DIG Delay", "Digital Delay", "DIG DELAY"),
    "cvdigpitch": ("DIG Pitch", "Digital Pitch", "DIG PITCH"), "cvdigreverb": ("DIG Reverb", "Digital Reverb", "DIG REVERB"),
    "cvlooper": ("Looper", "Mini Looper", "LOOPER"), "cvlooperpro": ("Looper Pro", "Looper Pro", "LOOPER PRO"),
    "cvloop2": ("PD44", "Loop II", "LOOP II"), "cvminiuniverse": ("PD38", "Mini-Universe", "MINI-UNIVERSE"),
    "cvelemental": ("PD42", "Elemental", "ELEMENTAL"), "cvelementalpro": ("Elemental Pro", "Elemental Pro", "ELEMENTAL PRO"),
    "cvclassicdelay": ("PD45", "Classic Delay", "CLASSIC DELAY"), "cvgalaxia": ("Galaxia", "Galaxia", "GALAXIA"),
    "cvirbox": ("SK14", "IR Box", "IR BOX"),
}


def real_of(m):
    """(sigla, nome, scritta stampata) reali senza il marchio del produttore."""
    i = m["id"]
    if i in REAL:
        return REAL[i]
    code = m["real_code"]
    name = re.sub(r"^(Behringer|Cuvave|M-VAVE)\s+", "", m["real_name"]).strip()
    name = re.sub(r"\s*\(.*?\)", "", name).strip()
    rest = name[len(code):].strip() if name.upper().startswith(code.upper()) else name
    pr = rest.replace("/", " /")
    return code, rest, pr


def notes_of(m):
    t = (m.get("type") or "").strip()
    n = "%s (%s). Parte funzionale provvisoria (segnaposto) finche' non arriva la scheda sonora." % (t[:1].upper() + t[1:], m.get("year") or "")
    return n.replace("..", ".")


def subtitle_of(m, cat):
    if m["id"] in ("bhig9", "bhnr300", "cvnoisegate"):
        return "NOISE GATE"
    return SUB.get(cat, cat.upper())


def entry(m):
    i = m["id"]
    cat = CAT_ID.get(i) or CATEGORY.get((m.get("category") or "").lower(), "Overdrive / Boost")
    code, name, pr = real_of(m)
    col = COL.get(i, ((150, 150, 150), None, None))
    key = mold_of(m)
    lines = ["    # %s - %s (%s)" % (m["real_name"], m.get("year") or "", m["series"][:70])]
    if key == "treadle":
        tl, ps, pedal = treadle_entry(m)
        fam, ctrls, cfg = treadle_placeholder(m, pedal)
        lines.append("    rocker(%r, %r, %r, %r," % (i, m["code"], m["name"], m["real_name"]))
        lines.append("           (%r, %r), %r, %r," % (code, name, cat, col[0]))
        lk = "treadle_look(%r, %r, leds=%r, finish=%r%s%s, tread=%r)" % (
            tl["shape"], tuple(tl["dims"]), tl["leds"], tl["finish"],
            (", rocker=%r" % (tl["rocker"],)) if tl.get("rocker") else "",
            (", rocker_finish=%r" % tl["rocker_finish"]) if tl.get("rocker_finish") else "", tl["tread"])
        lines.append("           %s," % lk)
        lines.append("           (%r, [%s],\n            %r)," % (fam, ",\n             ".join(ctrls), cfg))
        lines.append("           %r," % notes_of(m))
        lines.append("           place_spec=%r)," % (ps,))
        return "\n".join(lines)
    lk = apply_tweaks(m, BUILD[key](m, col))
    fam, ctrls, cfg = placeholder(m, cat)
    lines.append("    stomp(%r, %r, %r, %r," % (i, m["code"], m["name"], m["real_name"]))
    lines.append("          (%r, %r), %r, %r," % (code, name, cat, col[0]))
    args = []
    for k in ("leds", "extra", "finish", "gfx", "knob", "profile", "covers", "outline", "margin", "plan", "jack_at", "knob_pad", "nolab", "sections", "inner_skip", "outline_profile",
              "foot_kind", "print_labels", "foot_leds", "adapt", "label_side"):
        if lk.get(k) not in (None, [], {}):
            args.append("%s=%r" % (k, lk[k]))
    args.append("print=%r" % pr)
    lines.append("          look(%r, %r,\n               %r,\n               feet=%r,\n               %s)," % (
        lk["shape"], tuple(lk["dims"]), lk["place"], lk["feet"], ",\n               ".join(args)))
    lines.append("          (%r, [%s],\n           %r)," % (fam, ",\n            ".join(ctrls), cfg))
    lines.append("          %r, subtitle=%r)," % (notes_of(m), subtitle_of(m, cat)))
    return "\n".join(lines)


HEAD = '''"""
Pedal Trinity - %s (tappa 5): pedali a scatola con la forma del pedale vero (stomp_layout.py: stampi della tappa 5,
profilo del piano, coperchio-pedale, contorni speciali) e pedali a bilanciere (shaped_layout.py), modalita' normale con
nome e sigla ORIGINALI Pedal Trinity (sigle NV/NR/NX/NB/NY/NK/NW) e replica REAL MOD con nome e sigla reali del modello
(mai il marchio del produttore). Nessun logo, artwork, ritratto o firma: colori e motivi geometrici semplici.

Fonti dell'aspetto: schede della tappa 5 (~/.config/PedalTrinity/stage5/stage5_specs.json: manuali e guide rapide
Behringer e M-VAVE coi disegni numerati, misure dei contenitori, posizioni dei comandi x%%, y%% sul piano visto
dall'alto); colori verificati sulle foto libere di Wikimedia Commons e sui documenti informativi Behringer dove
possibile, altrimenti stimati (vedi la tabella COL del generatore). La parte funzionale viene da sound_stage5_*.json
(stomp_catalog.stomp/rocker): i comandi qui sotto sono il segnaposto usato finche' l'id manca in quei file.
File generato da gen_catalog_behringer.py: per cambiare l'aspetto modificare il generatore e rigenerare.
"""
from catalog import knob, outer, inner, selector, toggle, slider, button
from stomp_catalog import stomp, look, rocker, treadle_look

MODELS = [
'''


def main():
    d = json.load(open(SPECS, encoding="utf-8"))
    ms = d["models"]
    groups = (("catalog_behringer.py", "Behringer, serie in plastica a coperchio-pedale",
               [m for m in ms if mold_of(m) == "bhpl"]),
              ("catalog_behringer_b.py", "Behringer in metallo (Vintage, serie 9, V-Tone, AB200, serie moderne, BM) e bilancieri",
               [m for m in ms if m["id"].startswith("bh") and mold_of(m) != "bhpl"]),
              ("catalog_cuvave.py", "Cuvave / M-VAVE (Cube Baby, mini pedali, serie 112 x 68 mm, IR BOX)",
               [m for m in ms if m["id"].startswith("cv")]))
    for fn, tag, group in groups:
        out = [HEAD % tag]
        for m in group:
            out.append(entry(m) + "\n")
        out.append("]\n")
        with open(os.path.join(HERE, fn), "w", encoding="utf-8") as f:
            f.write("".join(out))
        print(fn, len(group))


if __name__ == "__main__":
    main()
