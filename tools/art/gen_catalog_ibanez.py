#!/usr/bin/env python3
"""
Pedal Trinity - generatore dei cataloghi Ibanez della tappa 4 (catalog_ibanez.py e catalog_ibanez_b.py) dalle schede
~/.config/PedalTrinity/stage4/stage4_specs.json (campo "models" e "real_only").

L'ASPETTO viene dalle schede: forma della serie (prima serie a cuneo, scatole piatte anni '70, serie 808 con il tasto
quadrato, serie 9 con il coperchio-pedale cromato a righe, Power/Master (serie 10 e L) con il coperchio in plastica o in
gomma, DCP programmabili, Soundtank metallici e in plastica, Tone-Lok con i pomelli a scomparsa, MINI, TS808/TS9 e
riedizioni, Tube King con la valvola, Effect Component, Pentatone, Echo Shifter...), misure, posizione dei comandi
per etichetta (x%, y% della scheda), LED, footswitch e colori (tabella COL piu' sotto: tinte stimate dalle descrizioni,
mai loghi o artwork). La PARTE FUNZIONALE (famiglia, comandi, config, note) viene dai JSON sound_stage4_*.json tramite
stomp_catalog.stomp/rocker; qui si scrive solo il segnaposto con le etichette e l'ordine dei comandi della scheda.

Uso: python3 tools/art/gen_catalog_ibanez.py [schede.json]   (riscrive catalog_ibanez.py e catalog_ibanez_b.py)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPECS = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.config/PedalTrinity/stage4/stage4_specs.json")

# ------------------------------------------------------------------ colori (stime dalle descrizioni delle schede)
# id: (corpo, accento facoltativo per fasce/etichette/scritte)
COL = {
    "ib7od850": ((232, 110, 30),), "ib7od855": ((118, 168, 62),), "ib759": ((30, 30, 32), (200, 202, 206)),
    "ib7cp830": ((236, 120, 34),), "ib7cp835": ((196, 34, 38),), "ib7pt999": ((84, 160, 214),),
    "ib7pt707": ((36, 80, 168),), "ib7pt909": ((32, 92, 182),), "ib7fp777": ((36, 84, 170), (236, 236, 228)),
    "ib7jl70": ((238, 122, 32), (24, 24, 26)), "ib7st800": ((240, 202, 44),), "ib7fl303": ((240, 206, 42),),
    "ib7fl305": ((236, 236, 228), (40, 140, 72)), "ib7fl301": ((240, 202, 34),), "ib7fl301dx": ((242, 206, 46),),
    "ib7cs505": ((150, 112, 190),), "ib7ad80": ((210, 62, 140),), "ib7af201": ((236, 118, 32),),
    "ib7ge601": ((92, 170, 220),), "ib7pq401": ((104, 182, 226),), "ib7ge300": ((150, 152, 156),),
    "ib9ts9": ((118, 192, 98),), "ib9sd9": ((190, 212, 54),), "ib9st9": ((44, 124, 82),), "ib9sm9": ((40, 122, 162),),
    "ib9fl9": ((240, 206, 44),), "ib9cs9": ((176, 162, 212),), "ib9pt9": ((238, 112, 32),), "ib9ad9": ((226, 72, 142),),
    "ib9bc9": ((150, 102, 152),), "ib9cp9": ((222, 72, 42),), "ib9af9": ((238, 228, 172),), "ib9ge9": ((82, 150, 210),),
    "ib9pq9": ((122, 182, 226),),
    "ib10ts10": ((122, 200, 172),), "ib10ms10": ((22, 92, 92),), "ib10ds10": ((222, 92, 42),), "ib10fc10": ((200, 42, 52),),
    "ib10mt10": ((62, 180, 72),), "ib10lm7": ((92, 96, 102),), "ib10ss10": ((62, 72, 92),), "ib10ss20": ((72, 62, 92),),
    "ib10sc10": ((112, 182, 222),), "ib10tc10": ((122, 142, 210),), "ib10pc10": ((92, 122, 200),),
    "ib10ph10": ((234, 92, 42),), "ib10cd10": ((62, 142, 200),), "ib10dl10": ((42, 92, 180),), "ib10dpl10": ((32, 52, 122),),
    "ib10dsc10": ((180, 162, 214),), "ib10ddl10": ((152, 172, 200),), "ib10dml10": ((32, 102, 122),),
    "ib10dcf10": ((32, 62, 140),), "ib10ddl20": ((190, 192, 196),), "ib10dml20": ((62, 170, 170),),
    "ib10cp10": ((240, 132, 42),), "ib10bp10": ((200, 42, 42),), "ib10bc10": ((102, 112, 200),), "ib10bs10": ((62, 62, 66),),
    "ib10be10": ((200, 200, 200),), "ib10ge10": ((238, 226, 162),), "ib10ot10": ((226, 226, 220),),
    "ib10nb10": ((42, 42, 46),), "ib10ls10": ((62, 66, 72),),
    "ib10wh10": ((128, 130, 134), (90, 170, 220)), "ib10wf10": ((128, 130, 134), (60, 62, 66)),
    "ib10vl10": ((30, 30, 32), (40, 140, 140)),
    "ib10stl": ((52, 54, 58), (62, 182, 172)), "ib10msl": ((36, 36, 38), (212, 52, 52)),
    "ib10afl": ((52, 54, 58), (240, 202, 42)), "ib10pql": ((52, 54, 58), (92, 172, 222)),
    "ib10csl": ((52, 54, 58), (152, 112, 202)), "ib10dcl": ((176, 186, 176), (92, 82, 182)),
    "ib10ddl": ((176, 186, 176), (62, 152, 202)), "ib10dfl": ((176, 186, 176), (230, 112, 92)),
    "ib10dml": ((176, 186, 176), (92, 82, 182)),
    "ib10pdm1": ((26, 26, 28), (72, 190, 170)), "ib10pdd1": ((26, 26, 28), (62, 142, 222)),
    "ib10pds1": ((26, 26, 28), (212, 52, 52)), "ib10pod1": ((26, 26, 28), (240, 142, 42)),
    "ib10ppe1": ((26, 26, 28), (230, 210, 62)),
    "ibstts5": ((20, 20, 21), (152, 210, 92)), "ibstpl5": ((20, 20, 21), (230, 82, 82)), "ibsttm5": ((20, 20, 21), (245, 226, 62)),
    "ibstcs5": ((20, 20, 21), (214, 152, 200)), "ibstfl5": ((20, 20, 21), (112, 210, 200)),
    "ibstph5": ((20, 20, 21), (142, 142, 220)), "ibstdl5": ((20, 20, 21), (112, 182, 235)),
    "ibstcp5": ((20, 20, 21), (245, 142, 82)), "ibstfz5": ((20, 20, 21), (92, 200, 190)), "ibstcm5": ((20, 20, 21), (245, 142, 42)),
    "ibstcr5": ((46, 46, 50), (192, 226, 72)), "ibstmf5": ((46, 46, 50), (102, 182, 230)),
    "ibstsp5": ((20, 20, 21), (240, 240, 236)), "ibstbn5": ((20, 20, 21), (240, 172, 122)),
    "ibstcd5": ((20, 20, 21), (220, 222, 225)), "ibstaw5": ((20, 20, 21), (132, 200, 240)),
    "ibsttl5": ((20, 20, 21), (245, 182, 200)), "ibstbp5": ((20, 20, 21), (235, 92, 112)),
    "ibstbc5": ((20, 20, 21), (222, 202, 235)), "ibstem5": ((20, 20, 21), (42, 92, 200)),
    "ibtlts7": ((82, 150, 92),), "ibtlds7": ((150, 152, 156),), "ibtlsm7": ((190, 192, 196),), "ibtlsh7": ((122, 150, 190),),
    "ibtlfz7": ((200, 112, 52),), "ibtlpd7": ((160, 122, 72),), "ibtllf7": ((142, 160, 122),), "ibtlaw7": ((150, 112, 160),),
    "ibtlsb7": ((92, 112, 150),), "ibtlph7": ((200, 122, 62),), "ibtlpm7": ((112, 150, 140),), "ibtlap7": ((190, 142, 82),),
    "ibtlcf7": ((122, 132, 190),), "ibtltc7": ((132, 170, 200),), "ibtlde7": ((150, 152, 156),),
    "ibtlwd7": ((196, 198, 202), (200, 36, 36)),
    "ibtsts9dx": ((112, 190, 100),), "ibtsts808hw": ((42, 112, 72),), "ibtsts808hwv2": ((62, 140, 82),),
    "ibtsts808dx": ((72, 160, 92),), "ibtstsmini": ((92, 172, 96),), "ibtsts9b": ((100, 172, 112),),
    "ibtsnts": ((238, 232, 212), (36, 96, 60)), "ibtstsv808": ((172, 142, 72), (40, 70, 50)),
    "ibtsjemini": ((62, 150, 220), (232, 72, 160)), "ibtstk999": ((172, 174, 178),), "ibtstk999us": ((60, 62, 66),),
    "ibtstk999od": ((150, 32, 32), (230, 200, 120)), "ibtstk999ht": ((32, 32, 34), (230, 200, 120)),
    "ibtstc999": ((190, 160, 82),), "ibtssd9m": ((226, 96, 42),), "ibtsbb9": ((236, 202, 52),), "ibtsjd9": ((40, 44, 62),),
    "ibtsfzmini": ((232, 112, 32),), "ibtssmmini": ((40, 112, 162),), "ibtsbtmini": ((180, 36, 40),),
    "ibtsptpre": ((32, 32, 36), (80, 140, 230)),
    "ibmwh10v3": ((28, 28, 30), (200, 202, 206)), "ibmadmini": ((112, 62, 152),), "ibmcsmini": ((112, 190, 230),),
    "ibmphmini": ((236, 112, 42),), "ibmflmini": ((236, 202, 42),), "ibmtrmini": ((62, 150, 92),),
    "ibmes2": ((182, 184, 188), (40, 40, 42)), "ibmes3": ((150, 152, 156), (36, 36, 38)),
    "ibmaf2": ((40, 40, 44), (200, 202, 206)), "ibmad99": ((24, 24, 26), (200, 40, 40)),
    "ibmfl99": ((180, 182, 186), (40, 150, 70)), "ibmph99": ((180, 182, 186), (40, 40, 44)),
    "ibmrc99": ((24, 24, 26), (210, 140, 200)), "ibmec40": ((24, 24, 26), (240, 200, 60)),
    "ibmwd7jr": ((196, 198, 202), (200, 36, 36)), "ibmtwp10": ((30, 30, 32), (200, 202, 206)),
    "ibmld303": ((42, 44, 48), (120, 220, 200)), "ibmpteq": ((32, 32, 36), (80, 140, 230)),
    "ibmptgate": ((32, 32, 36), (80, 140, 230)),
    "ibmno57": ((30, 30, 32), (210, 212, 216)), "ibmno58": ((30, 30, 32), (210, 212, 216)),
    "ibmno84": ((30, 30, 32), (210, 212, 216)), "ibmno93": ((30, 30, 32), (210, 212, 216)),
    "ibmpt1000": ((92, 102, 122), (210, 212, 216)), "ibmst810": ((230, 192, 42), (24, 24, 26)),
    "ibxec20": ((24, 24, 26), (240, 160, 60)), "ibxec30": ((24, 24, 26), (240, 200, 60)),
    "ibxec50": ((24, 24, 26), (150, 220, 110)),
    "ed9": ((50, 150, 78),),
}

CATEGORY = {"fuzz": "Fuzz", "overdrive": "Overdrive / Boost", "boost": "Overdrive / Boost", "distorsione": "Distorsione",
            "compressore": "Dinamica", "eq": "Equalizzatori", "filtro": "Wah / Filtri", "wah": "Wah / Filtri",
            "synth": "Synth", "octave": "Pitch / Ottave", "pitch": "Pitch / Ottave", "chorus": "Chorus / Dimension",
            "flanger": "Flanger", "phaser": "Phaser", "tremolo": "Tremolo / Pan / Slicer", "vibrato": "Vibrato / Rotary",
            "rotary": "Vibrato / Rotary", "delay": "Delay", "reverb": "Riverbero", "looper": "Tuner / Looper",
            "utility": "Utility / Routing", "volume": "Utility / Routing", "noise gate": "Dinamica", "gate": "Dinamica",
            "preamp": "Overdrive / Boost", "multi": "Chorus / Dimension", "simulatore": "Amp / IR", "cab": "Amp / IR"}

SUB = {"Overdrive / Boost": "OVERDRIVE", "Distorsione": "DISTORTION", "Fuzz": "FUZZ", "Dinamica": "COMPRESSOR",
       "Equalizzatori": "EQUALIZER", "Wah / Filtri": "FILTER", "Chorus / Dimension": "CHORUS", "Flanger": "FLANGER",
       "Phaser": "PHASER", "Tremolo / Pan / Slicer": "TREMOLO", "Vibrato / Rotary": "ROTARY", "Delay": "DELAY",
       "Pitch / Ottave": "OCTAVE", "Synth": "SYNTH", "Utility / Routing": "UTILITY", "Amp / IR": "CABINET",
       "Riverbero": "REVERB", "Metal": "METAL", "Basso": "BASS"}

TREADLES = {"ib10wh10", "ib10wf10", "ib10vl10", "ibtlwd7", "ibmwh10v3", "ibmwd7jr", "ibmtwp10", "ibmno57", "ibmno58",
            "ibmno84", "ibmno93", "ibmpt1000", "ibmst810"}


# ------------------------------------------------------------------ etichette
ABBR = [("FREQUENCY", "FREQ"), ("MIDDLE", "MID"), ("REGENERATION", "REGEN"), ("SENSITIVITY", "SENS"),
        ("DISTORTION", "DIST"), ("RESONANCE", "RES"), ("FEEDBACK", "FDBK"), ("THRESHOLD", "THRESH"), ("LEVEL", "LVL"),
        ("DELAY", "D"), ("TIME", "TIME"), ("VOLUME", "VOL"), ("OVERDRIVE", "DRIVE"), ("SUSTAIN", "SUS"),
        ("PRESENCE", "PRES"), ("COMPRESS", "COMP"), ("ATTACK", "ATK"), ("TREBLE", "TREB"), ("BALANCE", "BAL"),
        ("WAVEFORM", "WAVE"), ("WAVE FORM", "WAVE"), ("REPEAT", "RPT"), ("MANUAL", "MAN")]


def clean_label(s):
    """Etichetta della scheda -> etichetta stampata (maiuscola, senza parentesi, max 10 caratteri) e alias."""
    full = str(s).strip()
    base = re.sub(r"\s*\(.*?\)\s*", " ", full).strip()
    base = base.replace("▲/▼", "").replace("▲", "").replace("▼", "").strip()
    paren = re.findall(r"\((.*?)\)", full)
    up = base.upper()
    aliases = []
    if up != full.upper():
        aliases.append(full.upper())
    for p in paren:
        p2 = re.sub(r"[^A-Za-z0-9/ .\-']", " ", p).strip().upper()
        if p2 and len(p2) <= 14 and not re.search(r"SINISTR|DESTR|LIVELLO|SCRITTA|PEDALE|STIMA", p2):
            aliases.append(p2.strip("' "))
    ab = up
    for a, b in ABBR:
        ab = re.sub(r"\b%s\b" % a, b, ab)
    if ab != up:
        aliases.append(ab)
    toks = up.replace("/", " ").replace("-", " ").replace(".", " ").split()
    if len(toks) > 1:
        aliases.append(" ".join(toks))
        aliases.append(toks[0])
    lab = up if len(up) <= 10 else (ab if len(ab) <= 10 else up[:10])
    out = []
    for a in aliases:
        if a and a != lab and a not in out:
            out.append(a)
    return lab, out


def ctl_kind(t):
    t = t.lower()
    if "pedale d'espress" in t or "pedana" in t or "bilanciere" in t:
        return "treadle"
    if "concentr" in t:
        return "concentric"
    if "slider" in t or "cursore" in t or "cursori" in t:
        return "slider"
    if "selettore rotativo" in t or ("selettore" == t.strip()) or "selettore a 3 po" in t or "selettore a 4" in t:
        return "rotsel"
    if "slitta" in t or "levetta" in t or "interruttore" in t or "leva" in t or "selettore" in t:
        return "lever"
    if ("pulsante" in t or "pulsanti" in t) and "pomello" not in t:
        return "button"
    if "pomello grande" in t:
        return "big"
    if "pomellino" in t or "pomello piccolo" in t or "trimmer" in t:
        return "small"
    if "push-lok" in t:
        return "pushlok"
    if "pomell" in t or "pomello" in t:
        return "knob"
    return None


def ext_controls(m):
    """Comandi esterni della scheda con posizione: [(etichetta, alias, tipo, x, y, posizioni, etichetta originale)]."""
    out = []
    for c in m["controls"]:
        if c.get("external") is False:
            continue
        k = ctl_kind(c.get("type") or "")
        if k is None:
            continue
        lab, al = clean_label(c["label"])
        out.append(dict(label=lab, alias=al, kind=k, x=c.get("x_pct"), y=c.get("y_pct"), pos=c.get("positions"),
                        orig=c["label"], type=c.get("type") or ""))
    return out


# ------------------------------------------------------------------ segnaposto funzionale (finche' manca il JSON)
FAMILY_OF = {"Overdrive / Boost": "Circuit", "Distorsione": "Circuit", "Fuzz": "Circuit", "Dinamica": "Compressor",
             "Equalizzatori": "GraphicEQ", "Wah / Filtri": "Wah", "Chorus / Dimension": "BBDChorus", "Flanger": "BBDFlanger",
             "Phaser": "Phaser", "Tremolo / Pan / Slicer": "Tremolo", "Vibrato / Rotary": "BBDChorus",
             "Delay": "AnalogDelay", "Pitch / Ottave": "Pitch", "Synth": "Synth", "Utility / Routing": "Router",
             "Amp / IR": "Circuit", "Riverbero": "Reverb"}

ROLE_WORDS = [("DRIVE", "gain"), ("DIST", "gain"), ("GAIN", "gain"), ("SUSTAIN", "gain"), ("FUZZ", "gain"), ("OVERDRIVE", "gain"),
              ("TONE", "tone"), ("FILTER", "tone"), ("BASS", "tone"), ("TREB", "tone"), ("MID", "tone"), ("LOW", "tone"),
              ("HI", "tone"), ("HIGH", "tone"), ("LO", "tone"), ("EDGE", "tone"), ("PUNCH", "tone"), ("ATTACK", "attack"),
              ("LEVEL", "level"), ("VOL", "level"), ("BALANCE", "level"), ("OUTPUT", "level"), ("MASTER", "level"),
              ("SPEED", "rate"), ("RATE", "rate"), ("DEPTH", "depth"), ("WIDTH", "depth"), ("REGEN", "res"),
              ("FEEDBACK", "res"), ("F.BACK", "res"), ("REPEAT", "feedback"), ("TIME", "time"), ("MIX", "level"),
              ("BLEND", "level"), ("MANUAL", "manual"), ("SENS", "sens"), ("PEAK", "peak"), ("RANGE", "mode"),
              ("MODE", "mode"), ("THRESH", "threshold")]


def role_for(label, fam):
    u = label.upper()
    for w, r in ROLE_WORDS:
        if u.startswith(w) or (" " + w) in u:
            if fam == "Compressor" and r == "gain":
                return "sustain"
            if fam in ("BBDChorus", "BBDFlanger", "AnalogDelay", "Phaser", "Tremolo") and r in ("gain", "tone"):
                return "level" if r == "gain" else "tone"
            if fam == "AnalogDelay" and r == "res":
                return "feedback"
            if fam == "Wah" and r in ("gain", "tone"):
                return "level" if r == "gain" else "peak"
            return r
    return "level" if fam != "Circuit" else "tone"


CFG = {
    "Compressor": "type=sustainer topo=ff attack=2,60 releaseRange=80,600 release=250 maxgain=30 thr=-40,-10 ratio=2,10 knee=6",
    "GraphicEQ": None,
    "Wah": "type=auto f=250,2200 q=2,8 filt=bp attack=8 release=120",
    "BBDChorus": "type=chorus stages=1024 clk=40000,180000 rate=0.1,8 lfo=0 aa=8000 comp=1 wet=1",
    "BBDFlanger": "type=flanger stages=512 clk=60000,600000 rate=0.05,6 lfo=0 aa=9000 fbmax=0.9 wet=1",
    "Phaser": "stages=4 f=200,3000 rate=0.1,8 fbmax=0.7 lfo=0 depth=1",
    "Tremolo": "type=tremolo rate=0.5,12",
    "AnalogDelay": "type=delay stages=4096 clk=8000,100000 aa=4500 comp=1 fbmax=0.95 wet=1",
    "Pitch": "type=analog range=12 lp=3000 gain=1",
    "Synth": "type=analog f=80,3000",
    "Router": "type=ab2",
    "Reverb": "modes=Hall:hall decay=0.5,6 predelay=20 bw=7000",
}


def placeholder(m, cat):
    """(famiglia, comandi, config) provvisori dalle etichette della scheda."""
    fam = FAMILY_OF.get(cat, "Circuit")
    ctrls = []
    ec = [c for c in ext_controls(m) if c["kind"] not in ("treadle",) and "OPEN" not in c["label"]
          and "BATTERY" not in c["label"]]
    sliders = [c for c in ec if c["kind"] == "slider"]
    if fam == "GraphicEQ" and len(sliders) < 3:
        fam = "ParametricEQ" if sliders or any(c["kind"] != "slider" for c in ec) else "Circuit"
    seen = set()
    k_eq = 0
    for c in ec:
        lab = c["label"]
        if lab in seen or c["kind"] == "button" and m["id"] in ("ib10pdm1", "ib10pdd1", "ib10pds1", "ib10pod1", "ib10ppe1"):
            continue
        seen.add(lab)
        if c["kind"] == "concentric":
            ctrls.append(("knob", lab, c))
        elif c["kind"] == "slider":
            ctrls.append(("slider", lab, c))
        elif c["kind"] in ("lever", "rotsel", "button"):
            ctrls.append(("sel", lab, c))
        else:
            ctrls.append(("knob", lab, c))
    out = []
    has_slider_and_knob = any(t == "slider" for t, _, _ in ctrls) and any(t != "slider" for t, _, _ in ctrls)
    for t, lab, c in ctrls[:12]:
        if t == "slider":
            if fam == "GraphicEQ":
                if lab.startswith("LEVEL") or lab.startswith("VOL"):
                    out.append("slider(%r, 'level', 0.5, -12, 12, 'db')" % lab)
                else:
                    out.append("slider(%r, 'b%d', 0.5, -12, 12, 'db')" % (lab, k_eq))
                    k_eq += 1
            elif has_slider_and_knob:
                out.append("knob(%r, %r)" % (lab, role_for(lab, fam)))
            else:
                out.append("slider(%r, %r, 0.5, 0, 10, 'dial')" % (lab, role_for(lab, fam)))
        elif t == "sel":
            pos = [str(p) for p in (c.get("pos") or [])]
            pos = [re.sub(r"\s*\(.*?\)", "", p).strip().upper()[:8] or "?" for p in pos]
            if "OPEN" in pos or not pos:
                pos = ["OFF", "ON"]
            if len(pos) == 2:
                out.append("toggle(%r, 'mode', (%r, %r), 0)" % (lab, pos[0], pos[1]))
            else:
                out.append("selector(%r, 'mode', %r, 0, short=%r)" % (lab, pos[:8], [p[:4] for p in pos[:8]]))
        else:
            out.append("knob(%r, %r)" % (lab, role_for(lab, fam)))
    if not out:
        out = ["knob('LEVEL', 'level')"]
    if fam == "Circuit":
        gi = next((i for i, x in enumerate(out) if "'gain'" in x), None)
        ti = next((i for i, x in enumerate(out) if "'tone'" in x and x.startswith("knob")), None)
        li = next((i for i, x in enumerate(out) if "'level'" in x and x.startswith("knob")), None)
        st = ["hpf R=470k C=100n"]
        st.append("fbclip Rg=4.7k Cg=47n Rf=51k%s Cf=51p d=si up=1 dn=1 rail=4.2" % ("+pot(%d,500k,A)" % gi if gi is not None else ""))
        if ti is not None:
            st.append("tonebm R1=22k C1=10n R2=22k C2=4n t=taper(%d,B)" % ti)
        st.append("vol a=taper(%d,A)" % li if li is not None else "gain db=0")
        cfg = "; ".join(st)
    elif fam == "GraphicEQ":
        nb = max(1, k_eq)
        fr = [100, 200, 400, 800, 1600, 3200, 6400, 12800][:nb]
        cfg = "freqs=%s qs=%s qmin=1.0 range=12 lvl=15" % (",".join(map(str, fr)), ",".join(["1.8"] * nb))
    elif fam == "ParametricEQ":
        cfg = "types=ls,pk,hs f0=60,400 f1=200,5000 f2=2000,10000 q=1.0 range=15"
        out = ["knob('LOW', 'g0')", "knob('MID', 'g1')", "knob('FREQ', 'f1')", "knob('HIGH', 'g2')", "knob('LEVEL', 'level')"]
    elif fam == "Router":
        out = ["toggle('SELECT', 'select', ('A', 'B'), 0)", "toggle('DIR', 'direction', ('1>2', '2>1'), 0)"]
        cfg = CFG["Router"]
    else:
        cfg = CFG.get(fam, "")
    return fam, out, cfg


# ------------------------------------------------------------------ forme per serie
def series_key(m):
    i = m["id"]
    s = m["series"].lower()
    sh = (m["appearance"].get("housing_shape") or "").lower()
    if i in TREADLES:
        return "treadle"
    if i in ("ib7fp777", "ib7jl70"):
        return "ibflat"
    if i == "ib759":
        return "ib59"
    if i in ("ib7fl303", "ib7ge300", "ib7fl305"):
        return "ib70big"
    if i.startswith("ib7"):
        if "narrow box" in sh[:60] or "808 \"narrow" in sh[:40] or "narrow box\" (1979" in sh:
            return "ib808n"
        if "wide box" in sh[:70] or "serie 808" in sh[:20]:
            return "ib808w"
        return "ib70"
    if i.startswith("ib9"):
        return "ib9"
    if i in ("ib10pdm1", "ib10pdd1", "ib10pds1", "ib10pod1", "ib10ppe1"):
        return "ibdcp"
    if i in ("ib10stl", "ib10msl", "ib10afl", "ib10pql", "ib10csl"):
        return "ibL"
    if i in ("ib10dcl", "ib10ddl", "ib10dfl", "ib10dml"):
        return "ibLD"
    if i.startswith("ib10"):
        return "ib10"
    if i.startswith("ibst"):
        return "ibstm" if i in ("ibstcr5", "ibstmf5") else "ibst"
    if i.startswith("ibtl"):
        return "ibtl"
    if i in ("ibtsts9dx", "ibtsts9b", "ibtssd9m", "ibtsbb9", "ibtsjd9"):
        return "ib9"
    if i in ("ibtsts808hw", "ibtsts808hwv2"):
        return "ib808hw"
    if i in ("ibtsnts", "ibtstsv808", "ed9"):
        return "ib808w"
    if i == "ibtsts808dx":
        return "ibdx"
    if i == "ibtsjemini":
        return "ibjem"
    if i.endswith("mini"):
        return "ibmini"
    if i in ("ibtstk999", "ibtstk999us"):
        return "ibtk"
    if i in ("ibtstk999od", "ibtstk999ht"):
        return "ibtkd"
    if i == "ibtstc999":
        return "ibtc"
    if i in ("ibtsptpre", "ibmpteq", "ibmptgate"):
        return "ibpenta"
    if i in ("ibmes2", "ibmes3"):
        return "ibes"
    if i == "ibmaf2":
        return "ibaf2"
    if i in ("ibmad99", "ibmfl99", "ibmph99", "ibmrc99"):
        return "ibneo"
    if i in ("ibmec40", "ibxec20", "ibxec30", "ibxec50"):
        return "ibec"
    if i == "ibmld303":
        return "ibld"
    return "ibbox"


def dims_of(m, key):
    a = m["appearance"].get("dimensions") or {}
    w, l, h = a.get("width_mm"), a.get("length_mm"), a.get("height_mm")
    est = {"ib70": (90, 125, 56), "ibflat": (165, 110, 46), "ib59": (180, 120, 56), "ib70big": (120, 150, 60),
           "ib808n": (60, 111, 36), "ib808w": (74, 125, 56), "ibneo": (96, 128, 50), "ibtk": (150, 140, 66),
           "ibtc": (170, 130, 62), "treadle": (110, 250, 78)}
    small70 = {"ib7pt999": (76, 112, 52), "ib7st800": (80, 118, 54)}
    if m["id"] in small70 and not w:
        return small70[m["id"]]
    if m["id"] == "ib7fl305" and not w:
        return (160, 112, 46)
    if w and l and h:
        return (float(w), float(l), float(h))
    return est.get(key, (74, 125, 56))


def pct_clip(v, lo=6.0, hi=94.0):
    return max(lo, min(hi, float(v)))


def place_list(m, key, W, D):
    """Posizioni dei comandi della scheda (etichetta, x%, y%, tipo, alias) secondo la serie."""
    pl = []
    seen = set()
    knob_t = {"ib70": "big", "ib70big": "big", "ibflat": "knob", "ibtl": "med", "ibmini": "small", "ibdcp": "small",
              "ibec": "knob", "ibtkd": "knob"}.get(key, "knob")
    for c in ext_controls(m):
        if c["kind"] == "treadle" or c["x"] is None or c["y"] is None:
            continue
        if "OPEN" in c["label"] or "BATTERY" in c["label"]:
            continue
        lab = c["label"]
        if lab in seen:
            par = re.findall(r"\((.*?)\)", c["orig"])
            if not par:
                continue
            lab = (par[0].strip().upper() + " " + lab)[:10]
        seen.add(lab)
        t = {"slider": "slider", "lever": "lever", "rotsel": knob_t, "button": "button", "big": "big", "small": "small",
             "pushlok": "med", "knob": knob_t, "concentric": "med"}[c["kind"]]
        if key == "ibmini" and c["kind"] in ("knob",) and c["y"] > 30:
            t = "big"
        x, y = pct_clip(c["x"], 5, 95), pct_clip(c["y"], 5, 95)
        al = list(c["alias"])
        if c["kind"] == "concentric":
            continue
        al += ALIAS.get((m["id"], lab), [])
        pl.append((lab, round(x, 1), round(y, 1), t, al))
    # pomelli concentrici della scheda (coppie alla stessa x): una posizione sola (l'inner segue l'outer)
    cc = [c for c in ext_controls(m) if c["kind"] == "concentric"]
    for k in range(0, len(cc), 2):
        a = cc[k]
        b = cc[k + 1] if k + 1 < len(cc) else None
        al = list(a["alias"]) + ([b["label"]] + list(b["alias"]) if b else [])
        pl.append((a["label"], pct_clip(a["x"], 5, 95), pct_clip(a["y"], 5, 95), "med", al))
    # alias del primo termine dell'etichetta solo se non e' condiviso con un'altra posizione (FILTER RES / FILTER CUT)
    firsts = {}
    for e in pl:
        f0 = e[0].replace("/", " ").replace("-", " ").replace(".", " ").split()[0]
        firsts[f0] = firsts.get(f0, 0) + 1
    out = []
    for e in pl:
        f0 = e[0].replace("/", " ").replace("-", " ").replace(".", " ").split()[0]
        al = [a for a in e[4] if not (a == f0 and firsts.get(f0, 0) > 1)]
        out.append((e[0], e[1], e[2], e[3], al))
    return out


# ritocchi per modello: LED e posizioni (anche per i comandi in piu' della scheda funzionale)
TWEAKS = {
    "ib10pc10": dict(leds=[(88, 5, "red")], place=[("SPEED A", 22, 15, "knob"), ("SPEED B", 50, 15, "knob"), ("FLANGE", 78, 15, "knob"),
                                                    ("WIDTH A", 22, 36, "knob"), ("WIDTH B", 50, 36, "knob"),
                                                    ("DELAY TIME", 78, 36, "knob"), ("MODE", 64, 5, "lever")]),
    "ibtsptpre": dict(place_all=[
        ("GATE", 7, 22, "knob"), ("LEVEL", 26, 24, "knob"), ("BASS", 51, 22, "med", ["TREBLE", "BASS/TREBLE"]),
        ("GAIN", 76, 22, "knob"), ("PRE BOOST", 93, 24, "med", ["POST BOOST"]), ("BRIGHT", 26, 9, "lever"),
        ("NOISE GATE", 11, 9, "lever"), ("BOOST ON", 93, 9, "lever"), ("FREQ LO", 22, 41, "small"),
        ("FREQ MID", 40, 41, "med", ["MID"]), ("FREQ HMID", 60, 41, "med", ["HIGH MID"]), ("FREQ HIGH", 78, 41, "small"),
        ("EQ ON", 94, 41, "lever"), ("LO", 28, 58, "slider"), ("LM", 41, 58, "slider"), ("MD", 54, 58, "slider"),
        ("HM", 66, 58, "slider"), ("HI", 78, 58, "slider")]),
    "ibmpteq": dict(place_all=[
        ("Q LO", 24, 12, "small"), ("Q LM", 40, 12, "small"), ("Q MD", 56, 12, "small"), ("Q HM", 72, 12, "small"),
        ("Q HI", 88, 12, "small"), ("LOW", 24, 30, "med", ["LO FREQ"]), ("LOW MID", 40, 30, "med", ["LM FREQ"]),
        ("MID", 56, 30, "med", ["MID FREQ"]), ("HIGH MID", 72, 30, "med", ["HM FREQ"]), ("HIGH", 88, 30, "med", ["HI FREQ"]),
        ("LEVEL", 9, 46, "knob"), ("LO", 24, 56, "slider"), ("LM", 40, 56, "slider"), ("MD", 56, 56, "slider"),
        ("HM", 72, 56, "slider"), ("HI", 88, 56, "slider")]),
    "ibtsjemini": dict(place_all=[
        ("DRIVE 1", 14, 20, "knob"), ("TONE 1", 28, 20, "knob"), ("LEVEL 1", 42, 20, "knob"), ("LED", 52.5, 18, "lever"),
        ("DRIVE 2", 64, 20, "knob"), ("TONE 2", 78, 20, "knob"), ("LEVEL 2", 91, 20, "knob")], feet_alias={1: ["CHANNEL"]}),
    "ib10ls10": dict(place=[("LOOP", 50, 10, "lever")]),
    "ibmptgate": dict(place_all=[("THRESHOLD", 50, 25, "knob"), ("MODE", 20, 14, "lever")]),
    "ibtstk999": dict(place=[("NOISE SUP", 85, 44, "small")], extra=[("tubes", 50, 55, 1)], leds=[(82, 64, "red")]),
    "ibtstk999us": dict(extra=[("tubes", 50, 55, 1)], leds=[(82, 64, "red")]),
    "ibtstsv808": dict(place_all=[("OVERDRIVE", 24, 23, "knob"), ("LEVEL", 76, 23, "knob"), ("TONE", 50, 32, "knob"),
                                  ("SAT", 12, 7, "small"), ("BASS", 88, 7, "small"), ("DIODE", 34, 6, "lever")],
                       leds=[(64, 7, "red")], label_side=True),
    "ibmld303": dict(place=[("FILTER", 24, 36, "lever")]),
    "ibmphmini": dict(place=[("STAGE", 16, 49, "button")]),
    "ibmtrmini": dict(place=[("LEVEL", 16, 46, "small")]),
    "ib10ss20": dict(place=[("MODE", 14, 14, "lever"), ("D-TIME", 38, 13, "knob"), ("REPEAT", 62, 13, "small"),
                            ("D-LEVEL", 84, 13, "small")]),
}

ALIAS = {("ibmes2", "OSCILLATIO"): ["OSCILLATE"], ("ibmes3", "ANALOG/DIG"): ["ANA/DIG"], ("ibmph99", "EFFECT LVL"): ["EFF.LEVEL"],
         ("ibmld303", "VOLUMI 1ST"): ["1/16 VOL", "VOL"]}
FEET_ALIAS = {("ibmaf2", 1): ["MODE"]}
BYPASS_WORDS = ("BYPASS", "ON/OFF", "EFFECT", "FX", "START", "NORMAL")


def feet_list(m, key):
    out = []
    for f in m["appearance"].get("footswitches") or []:
        lab, al = clean_label(f["label"])
        x = f.get("x_pct") if f.get("x_pct") is not None else 50
        y = f.get("y_pct") if f.get("y_pct") is not None else 80
        out.append((lab, float(x), float(y), al))
    if not out:
        out = [("ON/OFF", 50.0, 80.0, [])]
    # il primo footswitch e' quello dell'effetto (bypass)
    k = next((j for j, f in enumerate(out) if any(w in f[0] for w in BYPASS_WORDS)), 0)
    if k:
        out = [out[k]] + out[:k] + out[k + 1:]
    out = [(f[0], f[1], f[2], list(f[3]) + FEET_ALIAS.get((m["id"], j), [])) for j, f in enumerate(out)]
    return out


LEDC = {"rosso": "red", "verde": "green", "blu": "blue", "giallo": "yellow", "bianco": "white", "arancio": "orange"}


def leds_list(m):
    out = []
    for l in m.get("leds") or []:
        if not isinstance(l, dict):
            continue
        col = str(l.get("colour") or l.get("color") or "rosso").lower()
        if "lcd" in col or "segmenti" in col:
            continue
        c = "red"
        for k, v in LEDC.items():
            if col.startswith(k):
                c = v
                break
        if l.get("x_pct") is None or l.get("y_pct") is None:
            continue
        out.append((pct_clip(l["x_pct"], 5, 95), pct_clip(l["y_pct"], 4, 96), c))
    return out


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def ink(c):
    return (24, 24, 26) if lum(c) > 140 else (236, 234, 226)


def darker(c, f=0.55):
    return tuple(int(v * f) for v in c)


def lighter(c, f=0.45):
    return tuple(int(v + (255 - v) * f) for v in c)


# ------------------------------------------------------------------ aspetto per serie
def build_look(m, key):
    i = m["id"]
    W, D, H = dims_of(m, key)
    col = COL.get(i, ((150, 150, 150),))
    body = col[0]
    acc = col[1] if len(col) > 1 else None
    tx = ink(body)
    place = place_list(m, key, W, D)
    feet = feet_list(m, key)
    leds = leds_list(m)
    a = m["appearance"]
    fin = "paint"
    if "metallizz" in (a.get("finish") or "") or "metallizz" in (a.get("housing_color") or ""):
        fin = "satin"
    opts = dict(shape=None, dims=(W, D, H), place=place, feet=feet, leds=leds, finish=fin, gfx={}, adapt=True)
    g = opts["gfx"]
    label = {"col": tx, "size": 2.0}
    g["label"] = label
    if key == "ib9":
        # serie 9: piano rialzato con i pomelli, fronte inclinato, coperchio-pedale con targhetta argentata e piastra
        # cromata a righe (cataloghi Ibanez 1982-84); riedizioni moderne con lo stesso contenitore
        opts["shape"] = "ib9"
        opts["profile"] = [(0, H - 3), (41, H - 9), (44, H - 19), (100, H - 23)]
        cv = dict(foot=0, x0=6.5, x1=93.5, y0=44.5, y1=98.0, h0=H - 7.5, h1=H - 16.5, kind="paint", corner=3.5, print=True,
                  insets=[dict(x0=13, x1=87, y0=48.5, y1=60.5, kind="metal", col=(206, 208, 212), print=True, h=0.5),
                          dict(x0=13, x1=87, y0=62.5, y1=94.5, kind="chrome", col=(176, 179, 184), ribs=2.0,
                               ribs_col=(112, 114, 118), h=0.6)])
        opts["covers"] = [cv]
        opts["feet"] = [(feet[0][0], 50, 72, feet[0][3])]
        g["name"] = dict(zone=(48.5, 60.5), font="boldit", col=(30, 52, 122), size=4.6, upper=False)
        g["code"] = dict(size=2.4, col=(30, 52, 122), font="bold")
        g["brand"] = dict(x=50, y=96.2, col=tx)
        label["col"] = (240, 240, 236) if lum(body) < 170 else (24, 24, 26)
    elif key == "ib10":
        # Power Series: coperchio in plastica nera zigrinata, fascia scura col nome, targhetta verticale della sigla
        opts["shape"] = "ib10"
        opts["profile"] = [(0, H - 4), (44, H - 10), (47, H - 22), (100, H - 25)]
        cv = dict(foot=0, x0=17, x1=96, y0=48, y1=98.0, h0=H - 9, h1=H - 17, kind="plastic", corner=3.0, col=(24, 24, 26),
                  print=True,
                  insets=[dict(x0=21, x1=92, y0=60, y1=95, kind="plastic", col=(18, 18, 20), ribs=2.6, ribs_col=(40, 40, 43),
                               h=0.6)])
        opts["covers"] = [cv]
        g["panels"] = [("rect", 3.5, 48.5, 14.5, 97, lighter(body, 0.25), (30, 30, 32), 1.5, 0.4)]
        g["name"] = dict(zone=(35, 47), font="black", col=(236, 234, 226), upper=True, size=4.4)
        g["name_deco"] = dict(style="band", col=(30, 30, 32), pad=1.6)
        g["code"] = dict(y=72, x=9, angle=90, size=3.2, col=ink(lighter(body, 0.25)), font="black", len=40)
        g["brand"] = dict(x=56, y=53.5, col=(200, 200, 196))
        g["panels"].append(("text", "OPEN", 9, 99.0, 1.4, ink(body), "bold"))
    elif key in ("ibL", "ibLD"):
        # serie Master / L: coperchio con tappetino in gomma e fascia colorata (verticale o obliqua) col nome
        opts["shape"] = "ib10"
        opts["profile"] = [(0, H - 4), (44, H - 10), (47, H - 22), (100, H - 25)]
        ac = acc or (90, 170, 220)
        if key == "ibL":
            ins = [dict(x0=8, x1=72, y0=52, y1=95, kind="rubber", col=(22, 22, 24), ribs=4.0, ribs_col=(44, 44, 46), h=0.8),
                   dict(x0=76, x1=94, y0=50, y1=96, kind="gloss", col=ac, print=True, h=0.4)]
            g["name"] = dict(x=85, y=73, angle=90, font="black", col=ink(ac), upper=True, size=4.0, len=40)
            g["code"] = dict(y=57, x=40, size=3.0, col=ink(body), font="black")
        else:
            ins = [dict(x0=6, x1=94, y0=49.5, y1=59, kind="gloss", col=ac, print=True, h=0.4),
                   dict(x0=6, x1=18, y0=61, y1=96, kind="gloss", col=ac, h=0.4),
                   dict(x0=21, x1=94, y0=61, y1=95.5, kind="rubber", col=(22, 22, 24), ribs=4.0, ribs_col=(44, 44, 46), h=0.8)]
            g["name"] = dict(zone=(49.5, 59), font="black", col=ink(ac), upper=True, size=3.4)
            g["code"] = dict(y=78, x=12, angle=90, size=2.8, col=ink(ac), font="black", len=30)
        cv = dict(foot=0, x0=4, x1=96, y0=47.5, y1=98.0, h0=H - 9, h1=H - 17, kind="paint", corner=3.0, insets=ins)
        opts["covers"] = [cv]
        g["brand"] = dict(x=50, y=44, col=tx)
    elif key == "ibdcp":
        # DCP: contenitore nero piatto, display LCD e LED a 7 segmenti, tasti, fascia colorata col nome, pedale a lamelle
        opts["shape"] = "ibdcp"
        opts["profile"] = [(0, H - 2), (49, H - 2), (51, H - 13), (100, H - 15)]
        ac = acc or (80, 180, 170)
        opts["extra"] = [("display", 26, 5, 74, 17), ("display", 7, 5, 19, 17)]
        cv = dict(foot=0, x0=4, x1=96, y0=51.5, y1=98.0, h0=H - 1, h1=H - 8, kind="plastic", corner=2.5, col=(22, 22, 24),
                  ribs=6.0, ribs_col=(40, 40, 43))
        opts["covers"] = [cv]
        opts["place"] = [("SOUND", 12, 40, "button"), ("BANK", 30, 40, "button"), ("PATCH", 48, 40, "button"),
                         ("MODE", 66, 40, "button"), ("STORE", 84, 40, "button")]
        opts["param_row"] = (26.5, 8, 92)
        g["name"] = dict(zone=(43.5, 51), font="black", col=ink(ac), upper=True, size=3.6)
        g["name_deco"] = dict(style="band", col=ac, pad=1.0)
        g["code"] = dict(size=2.2, col=ink(ac))
        g["brand"] = dict(x=80, y=20.5, col=(200, 200, 196))
        opts["leds"] = []
    elif key in ("ibst", "ibstm"):
        # Soundtank: guscio a carro armato (plastica nera, o metallo scuro nella prima serie), coperchio con la finestra
        # dell'etichetta colorata e 5 piedini in gomma
        opts["shape"] = "ibst"
        opts["profile"] = [(0, H - 6), (29, H - 8), (33, H - 20), (100, H - 26)]
        ac = acc or (150, 210, 90)
        kind = "plastic" if key == "ibst" else "paint"
        cv = dict(foot=0, x0=4, x1=96, y0=33.5, y1=98.0, h0=H - 10, h1=H - 17, kind=kind, corner=8.0,
                  col=body if key == "ibst" else body, print=False,
                  insets=[dict(x0=14, x1=86, y0=38, y1=69, kind="label", col=ac, print=True, h=0.4, corner=3.0,
                               frame=darker(ac, 0.5))],
                  bumps=[(28, 79, 2.6), (50, 79, 2.6), (72, 79, 2.6), (39, 89, 2.6), (61, 89, 2.6)])
        opts["covers"] = [cv]
        g["name"] = dict(zone=(38.5, 68.5), font="black" if key == "ibst" else "narrow", col=ink(ac), upper=True, size=4.4)
        g["sub"] = dict(size=2.0, col=ink(ac), font="bold", text=None)
        g["code"] = dict(size=2.4, col=ink(ac), font="black")
        g["name_deco"] = None
        g["brand"] = dict(x=50, y=96.5, col=(170, 170, 168))
        label["col"] = (226, 226, 222)
        opts["finish"] = "matte" if key == "ibst" else "paint"
    elif key == "ibtl":
        # Tone-Lok: pomelli Push-Lok a scomparsa (bassi), coperchio-pedale con piastra in gomma, riquadro col nome
        opts["shape"] = "ibtl"
        opts["profile"] = [(0, H - 6), (46, H - 7), (49, H - 20), (100, H - 22)]
        cv = dict(foot=0, x0=4.5, x1=95.5, y0=49.5, y1=98.0, h0=H - 11, h1=H - 15, kind="paint", corner=3.5,
                  insets=[dict(x0=9, x1=91, y0=53.5, y1=95, kind="rubber", col=(24, 24, 25), ribs=3.0, ribs_col=(38, 38, 40), h=0.9)])
        opts["covers"] = [cv]
        opts["finish"] = "satin"
        g["name"] = dict(zone=(37, 49), font="black", col=(236, 234, 226), upper=True, size=3.4)
        g["name_deco"] = dict(style="box", col=darker(body, 0.45), pad=1.0)
        g["code"] = dict(size=3.4, col=(236, 234, 226), font="black", above=True)
        g["brand"] = dict(x=50, y=4.0, col=tx)
        opts["knob"] = "med"
    elif key in ("ib808w", "ib808n", "ib808hw"):
        # serie 808 / TS808: scatola pressofusa, meta' vicina col tasto quadrato metallico (Q-1) e nome in diagonale;
        # le riedizioni cablate a mano hanno il footswitch tondo
        opts["shape"] = "ib808"
        narrow = key == "ib808n"
        opts["profile"] = [(0, H), (100, H - (1.5 if narrow else 2.5))]
        if key != "ib808hw":
            kx = 9 if not narrow else 8
            cv = dict(foot=0, x0=kx, x1=100 - kx, y0=45.5, y1=96.0, h0=H + 3.5, h1=H + 2.0, deck=H - 3, kind="paint",
                      corner=2.5, print=True)
            if i == "ibtstsv808":
                cv["kind"] = "metal"
            opts["covers"] = [cv]
            g["name"] = dict(x=50, y=67, angle=24, font="black", col=tx, upper=True, size=5.6 if not narrow else 4.6, len=70)
            g["sub"] = dict(size=2.2, col=tx, font="bold")
            g["code"] = dict(size=2.6, col=tx, font="black")
            g["brand"] = dict(x=50, y=92.5, col=tx)
        else:
            opts["feet"] = [(feet[0][0], 50, 80, feet[0][3])]
            g["name"] = dict(zone=(42, 70), font="black", col=tx, upper=True, size=5.4)
            g["name_deco"] = dict(style="frame", col=tx, pad=1.6)
            g["code"] = dict(size=2.6, col=tx)
            g["brand"] = dict(x=50, y=95, col=tx)
        if i == "ibtsnts":
            g["panels"] = [("rect", 30, 30, 70, 41, (24, 28, 26), (90, 96, 92), 1.5, 0.5)]
        if i == "ibtstsv808":
            opts["finish"] = "brushed"
    elif key == "ib70":
        # prima serie (1975-79): pressofuso a cuneo, parte posteriore rialzata coi pomelli, fronte inclinato col
        # footswitch meccanico tondo cromato; filetti bianchi
        opts["shape"] = "ib70"
        opts["profile"] = [(0, H), (34, H), (100, H * 0.52)]
        g["panels"] = [("hline", 35.5, (236, 234, 226) if lum(body) < 170 else (24, 24, 26), 0.6, 4, 96)]
        g["name"] = dict(zone=(38, 64), font="boldit", col=tx, upper=False, size=6.0)
        g["code"] = dict(size=2.6, col=tx)
        g["brand"] = dict(x=50, y=95.5, col=tx)
    elif key == "ib70big":
        opts["shape"] = "ib70"
        if i == "ib7fl305":
            opts["shape"] = "ibsheet"
            opts["profile"] = [(0, H), (100, H - 6)]
            ac = acc or (40, 140, 72)
            g["panels"] = [("rect", 52, 52, 96, 96, ac, None, 3.0)]
            g["name"] = dict(zone=(40, 60), x=28, font="boldit", col=ac, upper=False, size=7.0)
        else:
            opts["profile"] = [(0, H), (42, H), (100, H * 0.55)]
            g["panels"] = [("rect", 30, 56, 70, 92, None, ink(body), 3.0, 0.7)]
            g["name"] = dict(zone=(40, 56), font="black", col=ink(body), upper=False, size=7.0)
        g["code"] = dict(size=2.8, col=ink(body))
        g["brand"] = dict(x=12, y=95, col=ink(body))
    elif key == "ibflat":
        opts["shape"] = "ibsheet"
        opts["profile"] = [(0, H), (100, H - 8)]
        if i == "ib7fp777":
            ac = acc or (236, 236, 228)
            g["panels"] = [("rect", 4, 6, 96, 94, ac, None, 3.0), ("circle", 50, 54, 15.0, (36, 84, 170)),
                           ("circle", 50, 54, 10.5, (240, 200, 50))]
            g["name"] = dict(zone=(44, 64), font="boldit", col=(24, 40, 120), upper=False, size=5.4)
            g["code"] = dict(size=3.0, col=(24, 40, 120), font="black", y=88, x=50)
            label["col"] = (24, 24, 26)
        else:
            g["panels"] = [("stripes", (24, 24, 26), 9.0, 30.0, -32, (0, 34, 100, 66))]
            g["name"] = dict(zone=(38, 62), font="black", col=(240, 238, 228), upper=True, size=6.0)
            g["name_deco"] = dict(style="box", col=(24, 24, 26), pad=1.6)
            g["code"] = dict(size=2.8, col=(240, 238, 228))
        g["brand"] = dict(x=50, y=96, col=ink(body))
    elif key == "ib59":
        opts["shape"] = "ibsheet"
        opts["profile"] = [(0, H), (100, H - 10)]
        ac = acc or (200, 202, 206)
        g["panels"] = [("rect", 22, 14, 82, 64, ac, (120, 122, 126), 2.5, 0.6)]
        g["name"] = dict(zone=(62, 72), font="black", col=(236, 234, 226), upper=True, size=5.0)
        g["code"] = dict(size=2.6, col=(236, 234, 226))
        g["brand"] = dict(x=50, y=96, col=(200, 200, 196))
        label["col"] = (24, 24, 26)
    elif key == "ibmini":
        # serie MINI: scatolina pressofusa stretta, due pomellini, pomello grande al centro, targhetta sopra il footswitch
        opts["shape"] = "ibmini"
        opts["dims"] = (W, D, min(H, 42.0))
        opts["profile"] = [(0, min(H, 42.0)), (100, min(H, 42.0) - 2)]
        g["panels"] = [("rect", 9, 55.5, 91, 67.5, (206, 208, 212), (120, 122, 126), 1.5, 0.4)]
        g["name"] = dict(zone=(55.5, 67.5), font="black", col=(30, 30, 34), upper=True, size=3.4)
        g["code"] = dict(size=2.0, col=tx, y=96.5, x=50)
        g["keep"] = []
        g["brand"] = dict(x=50, y=91.0, col=tx)
        label["size"] = 1.7
    elif key == "ibdx":
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H), (100, H - 3)]
        g["name"] = dict(zone=(42, 64), font="black", col=tx, upper=True, size=6.0)
        g["name_deco"] = dict(style="frame", col=tx, pad=1.6)
        g["code"] = dict(size=2.6, col=tx)
        g["brand"] = dict(x=50, y=95, col=tx)
    elif key == "ibjem":
        # doppio pedale con due coperchi affiancati (canale sinistro e destro)
        opts["shape"] = "ib9"
        opts["profile"] = [(0, H - 4), (45, H - 8), (48, H - 18), (100, H - 22)]
        ac = acc or (232, 72, 160)
        opts["covers"] = [dict(foot=0, x0=4, x1=48.5, y0=48.5, y1=97.5, h0=H - 9, h1=H - 15, kind="paint", corner=3.5,
                               insets=[dict(x0=8, x1=44.5, y0=56, y1=93, kind="rubber", col=(24, 24, 26), ribs=3.0,
                                            ribs_col=(40, 40, 42), h=0.8)]),
                          dict(foot=1, x0=51.5, x1=96, y0=48.5, y1=97.5, h0=H - 9, h1=H - 15, kind="paint", corner=3.5,
                               insets=[dict(x0=55.5, x1=92, y0=56, y1=93, kind="rubber", col=(24, 24, 26), ribs=3.0,
                                            ribs_col=(40, 40, 42), h=0.8)])]
        g["panels"] = [("diag", ac, "tl"), ("stripes", lighter(ac, 0.3), 2.0, 9.0, 35, (0, 0, 100, 44))]
        g["name"] = dict(zone=(34, 47.5), font="blackit", col=(24, 24, 26), upper=True, size=4.4)
        g["name_deco"] = dict(style="box", col=(240, 238, 228), pad=1.0)
        g["code"] = dict(size=2.4, col=(24, 24, 26))
        g["brand"] = dict(x=50, y=99.0, col=tx)
    elif key == "ibtk":
        # Tube King: contenitore grande e inclinato con la valvola 12AX7 visibile in una gabbia
        opts["shape"] = "tube"
        opts["profile"] = [(0, H), (100, H - 18)]
        opts["extra"] = [("tubes", 50, 46, 1)]
        g["name"] = dict(zone=(58, 76), font="serif", col=tx, upper=True, size=6.4)
        g["code"] = dict(size=2.6, col=tx)
        g["brand"] = dict(x=50, y=95.5, col=tx)
    elif key == "ibtkd":
        # Tube King OD/HT: contenitore a rombo (quadrato ruotato di 45 gradi)
        opts["shape"] = "ibbox"
        opts["dims"] = (W, D, 44.0)
        opts["outline"] = [(50, 1), (99, 50), (50, 99), (1, 50)]
        ac = acc or (230, 200, 120)
        g["panels"] = [("ellipse", 50, 15, 16.0, 6.0, ac, 0.6)]
        g["name"] = dict(zone=(58, 69), font="serif", col=ac, upper=True, size=8.0)
        g["code"] = dict(size=2.8, col=ac)
        g["brand"] = dict(x=50, y=15, col=ac)
        opts["margin"] = (0, 0, 0, 0)
    elif key == "ibtc":
        opts["shape"] = "ibbox"
        opts["outline"] = [(12, 2), (88, 2), (98, 98), (2, 98)]
        opts["profile"] = None
        opts["finish"] = "sparkle"
        g["panels"] = [("stripes", darker(body, 0.6), 1.4, 4.0, 0, (36, 42, 64, 58))]
        g["name"] = dict(zone=(58, 70), font="serif", col=ink(body), upper=True, size=6.0)
        g["code"] = dict(size=2.6, col=ink(body))
        g["brand"] = dict(x=50, y=96, col=ink(body))
    elif key == "ibpenta":
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H - 8), (100, H - 12)]
        g["name"] = dict(zone=(62, 82) if i != "ibmptgate" else (55, 70), font="black", col=(236, 234, 226), upper=True, size=4.4)
        g["code"] = dict(size=2.4, col=(200, 200, 196))
        g["brand"] = dict(x=50, y=97.0, col=(200, 200, 196))
        if i in ("ibtsptpre", "ibmpteq"):
            g["panels"] = [("hline", 47.5, (80, 140, 230), 0.5, 20, 86)]
    elif key == "ibes":
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H - 12), (100, H - 16)]
        ac = acc or (40, 40, 42)
        opts["extra"] = [("rails", 9.0, 5.0, ac)]
        opts["finish"] = "satin"            # metallo satinato (lo spazzolato anisotropo fa un riflesso a cono)
        g["name"] = dict(zone=(4, 14), font="black", col=(30, 30, 32), upper=True, size=4.6)
        g["code"] = dict(size=2.4, col=(30, 30, 32), y=94, x=50)
        g["brand"] = dict(x=50, y=88, col=(30, 30, 32))
        label["col"] = (30, 30, 32)
    elif key == "ibaf2":
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H - 18), (100, H - 22)]
        ac = acc or (200, 202, 206)
        g["panels"] = [("rect", 4, 14, 72, 48, None, ac, 3.0, 0.6), ("rect", 76, 14, 96, 48, None, ac, 3.0, 0.6)]
        g["name"] = dict(zone=(52, 72), font="blackit", col=ac, upper=True, size=6.0)
        g["code"] = dict(size=2.6, col=ac)
        g["brand"] = dict(x=50, y=92, col=ac)
        label["col"] = ac
    elif key == "ibneo":
        # Neo Classic: scatola con piano inclinato e fronte smussato, nome in corsivo colorato
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H), (80, H - 8), (100, H - 22)]
        ac = acc or (200, 40, 40)
        g["name"] = dict(zone=(48, 66), font="script", col=ac, upper=False, size=8.0)
        g["sub"] = dict(size=2.0, col=ac, font="bold")
        g["code"] = dict(size=2.4, col=ac)
        g["brand"] = dict(x=50, y=97, col=ac)
        g["panels"] = [("ellipse", 50, 57, 30.0, 9.0, ac, 0.5)]
        label["col"] = ac if lum(body) < 120 else (30, 30, 32)
    elif key == "ibec":
        # Effect Component: cuneo nero largo e basso, fila di comandi sul pannello rialzato in fondo, grande piastra in gomma
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H), (30, H), (36, H - 12), (100, H - 24)]
        ac = acc or (240, 180, 60)
        cv = dict(foot=0, x0=3, x1=97, y0=40, y1=97.5, h0=H - 13, h1=H - 22, kind="rubber", corner=5.0, col=(22, 22, 23),
                  ribs=5.0, ribs_col=(36, 36, 38))
        opts["covers"] = [cv]
        g["name"] = dict(zone=(30.5, 39.5), font="black", col=ac, upper=True, size=3.6)
        g["code"] = dict(size=2.2, col=(200, 200, 196))
        g["brand"] = dict(x=88, y=34.5, col=(200, 200, 196))
        label["col"] = (220, 220, 216)
    elif key == "ibld":
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H - 14), (100, H - 18)]
        opts["extra"] = [("display", 36, 30, 64, 40)]
        g["name"] = dict(zone=(64, 76), font="black", col=(236, 234, 226), upper=True, size=4.6)
        g["code"] = dict(size=2.4, col=(200, 200, 196))
        g["brand"] = dict(x=50, y=97, col=(200, 200, 196))
    else:
        opts["shape"] = "ibbox"
        opts["profile"] = [(0, H), (100, H - 3)]
        g["name"] = dict(font="black", col=tx, upper=True, size=5.4)
        g["code"] = dict(size=2.6, col=tx)
    tw = TWEAKS.get(i) or {}
    if "leds" in tw:
        opts["leds"] = tw["leds"]
    if "place_all" in tw:
        opts["place"] = [(e[0], e[1], e[2], e[3], list(e[4]) if len(e) > 4 else []) for e in tw["place_all"]]
    if "extra" in tw:
        opts["extra"] = tw["extra"]
    if "label_side" in tw:
        opts["label_side"] = True
        label["pos"] = "side"
    if "feet_alias" in tw:
        opts["feet"] = [(f[0], f[1], f[2], list(f[3]) + tw["feet_alias"].get(j, [])) for j, f in enumerate(opts["feet"])]
    if "place" in tw:
        pl = {e[0]: e for e in opts["place"]}
        for e in tw["place"]:
            pl[e[0]] = (e[0], e[1], e[2], e[3], list(pl[e[0]][4]) if e[0] in pl else (list(e[4]) if len(e) > 4 else []))
        opts["place"] = list(pl.values())
    if key in ("ib9", "ib10", "ibL", "ibLD"):
        opts["label_side"] = True
        label["pos"] = "side"
    if g.get("name_deco") is None:
        g.pop("name_deco", None)
    if g.get("sub") is not None and g["sub"].get("text") is None:
        g["sub"].pop("text", None)
    if opts.get("profile") is None:
        opts.pop("profile", None)
    return opts


# ------------------------------------------------------------------ bilancieri
def treadle_entry(m):
    i = m["id"]
    W, D, H = dims_of(m, "treadle")
    col = COL.get(i, ((40, 40, 42),))
    body = col[0]
    acc = col[1] if len(col) > 1 else (200, 202, 206)
    if i in ("ib10wh10", "ib10wf10", "ib10vl10"):
        tl = dict(shape="dvp1", dims=(W, D, H), leds=[], finish="paint", tread=("diamonds", (22, 22, 24), (30, 30, 32)))
    elif i == "ibtlwd7":
        tl = dict(shape="wide", dims=(W, D, H), leds=[("P", (0.85, 94), "red")], finish="satin", tread=("ribs", (24, 24, 26), (24, 24, 26)))
    elif i in ("ibmwh10v3", "ibmtwp10"):
        tl = dict(shape="crybaby", dims=(W, D, H), leds=[("L", 20, "red")], finish="hammer", tread=("diamonds", (22, 22, 24), (30, 30, 32)))
    elif i == "ibmwd7jr":
        tl = dict(shape="junior", dims=(W, D, H), leds=[("H", 0.0, "red")], finish="satin", tread=("ribs", (24, 24, 26), (24, 24, 26)))
    elif i in ("ibmno57", "ibmno58"):
        tl = dict(shape="morley", dims=(W, D, H), leds=[], finish="hammer", rocker=(200, 202, 206), rocker_finish="chrome",
                  tread=("dots", (190, 192, 196), (40, 40, 42)))
    elif i in ("ibmno84", "ibmno93"):
        tl = dict(shape="dearmond", dims=(W, D, H), leds=[], finish="hammer", tread=("diamonds" if i == "ibmno84" else "ribs",
                                                                                    (26, 26, 28), (36, 36, 38)))
    elif i == "ibmpt1000":
        tl = dict(shape="dearmond", dims=(W, D, H), leds=[], finish="hammer", tread=("ribs", (26, 26, 28), (26, 26, 28)))
    else:
        tl = dict(shape="dvp1", dims=(W, D, H), leds=[], finish="paint", tread=("grip", (20, 20, 22), (20, 20, 22)))
    # posizioni dei comandi sulle fiancate (x 0 = sinistra, 100 = destra; y 0 = punta)
    ps = []
    pedal_label = None
    for c in ext_controls(m):
        if c["kind"] == "treadle":
            pedal_label = c["label"]
            continue
        if c["x"] is None or c["y"] is None:
            continue
        side = "L" if c["x"] < 50 else "R"
        t = {"lever": "lever", "button": "button", "small": "knob", "big": "big", "rotsel": "knob", "slider": "lever"}.get(c["kind"], "knob")
        pct = max(15.0, min(85.0, 100.0 - float(c["y"])))
        if i == "ibtlwd7" and c["x"] > 40:
            t = "button" if t == "lever" else t
            ps.append((c["label"], "P", round(max(0.12, min(0.88, (c["x"] - 50) / 50.0)), 2), pct, t, c["alias"]))
            continue
        ps.append((c["label"], side, pct, t, c["alias"]))
    if i == "ibtlwd7":
        # pannello comandi a destra del bilanciere: pomelli Push-Lok in due colonne (posizioni della scheda ravvicinate)
        pos = {"LEVEL": (0.30, 76), "Q": (0.72, 76), "LO": (0.30, 60), "RANGE": (0.72, 60)}
        ps = [(e[0], "P", pos[e[0]][0], pos[e[0]][1], e[4], e[5]) if e[1] == "P" and e[0] in pos else e for e in ps]
    return tl, ps, pedal_label or "PEDAL"


def treadle_placeholder(m, cat, pedal_label):
    i = m["id"]
    lab = clean_label(pedal_label)[0]
    if i in ("ib10vl10", "ibmst810"):
        fam, cfg = "Volume", ""
        first = "knob(%r, 'volume', 1.0, 'percent', 0, 100)" % lab
    elif i == "ibmpt1000":
        fam, cfg = "Phaser", CFG["Phaser"]
        first = "knob(%r, 'rate')" % lab
    else:
        fam, cfg = "Wah", "type=pedal f=350,2200 q=3,6 filt=bp"
        first = "knob(%r, 'freq', 0.5)" % lab
    rest = []
    for c in ext_controls(m):
        if c["kind"] == "treadle" or c["x"] is None:
            continue
        if c["kind"] in ("lever", "button", "rotsel"):
            pos = [re.sub(r"\s*\(.*?\)", "", str(p)).strip().upper()[:8] for p in (c.get("pos") or [])] or ["OFF", "ON"]
            if len(pos) == 2:
                rest.append("toggle(%r, 'mode', (%r, %r), 0)" % (c["label"], pos[0], pos[1]))
            else:
                rest.append("selector(%r, 'mode', %r, 0, short=%r)" % (c["label"], pos[:8], [p[:4] for p in pos[:8]]))
        else:
            r = "min" if fam == "Volume" else ("level" if fam == "Wah" else "depth")
            rest.append("knob(%r, %r)" % (c["label"], r if fam != "Wah" else ("q" if "Q" == c["label"] else "level")))
    if fam == "Wah":
        cfg = "type=pedal f=350,2200 q=3,6 filt=bp"
        rest = [x for x in rest]
    return fam, [first] + rest[:10], cfg


# ------------------------------------------------------------------ scrittura
def fmt(v):
    return repr(v)


def real_of(m):
    name = re.sub(r"^Ibanez\s+", "", m["real_name"]).strip()
    name = re.sub(r"\s*\(collaborazione con [^)]*\)", "", name)
    code = m["real_code"]
    name = re.sub(r"\s*\((?!Steve|Paul)[^)]*\)", "", name).strip()
    if name.upper().startswith("NO." + code.upper()):
        code = "No." + code
    rest = name[len(code):].strip() if name.upper().startswith(code.upper()) else name
    t = rest.split(None, 1)
    if len(t) == 2 and t[0].replace("-", "").upper() == code.replace("-", "").upper():
        rest = t[1]
    return code, name, rest


def notes_of(m):
    t = (m.get("type") or "").strip()
    n = "%s (%s). Parte funzionale provvisoria (segnaposto) finche' non arriva la scheda sonora." % (t[:1].upper() + t[1:], m.get("year") or "")
    return n.replace("..", ".")


def entry(m, real_only=False):
    i = m["id"]
    cat = CATEGORY.get((m.get("category") or "").lower(), "Overdrive / Boost")
    code, name, rest = real_of(m)
    col = COL.get(i, ((150, 150, 150),))
    key = series_key(m)
    lines = []
    lines.append("    # %s - %s (%s)" % (m["real_name"], m.get("year") or "", m["series"]))
    if key == "treadle":
        tl, ps, pedal_label = treadle_entry(m)
        fam, ctrls, cfg = treadle_placeholder(m, cat, pedal_label)
        lines.append("    rocker(%r, %r, %r, %r," % (i, m["code"], m["name"], m["real_name"]))
        lines.append("           (%r, %r), %r, %r," % (code, rest or name, cat, col[0]))
        lk = "treadle_look(%r, %r, leds=%r, finish=%r%s%s, tread=%r)" % (
            tl["shape"], tuple(tl["dims"]), tl["leds"], tl["finish"],
            (", rocker=%r" % (tl["rocker"],)) if tl.get("rocker") else "",
            (", rocker_finish=%r" % tl["rocker_finish"]) if tl.get("rocker_finish") else "", tl["tread"])
        lines.append("           %s," % lk)
        lines.append("           (%r, [%s],\n            %r)," % (fam, ",\n             ".join(ctrls), cfg))
        lines.append("           %r," % notes_of(m))
        lines.append("           place_spec=%r)," % (ps,))
        return "\n".join(lines)
    lk = build_look(m, key)
    fam, ctrls, cfg = placeholder(m, cat)
    sub = SUB.get(cat, cat.upper())
    lines.append("    stomp(%r, %r, %r, %r," % (i, m["code"], m["name"], m["real_name"]))
    lines.append("          (%r, %r), %r, %r," % (code, rest or name, cat, col[0]))
    args = []
    for k in ("leds", "extra", "finish", "gfx", "knob", "profile", "covers", "outline", "margin", "param_row", "print_labels",
              "adapt", "label_side"):
        if lk.get(k) not in (None, [], {}):
            args.append("%s=%r" % (k, lk[k]))
    pr = re.sub(r"\s*\(.*?\)", "", rest if rest and rest != code else name).strip()
    args.append("print=%r" % pr)
    lines.append("          look(%r, %r,\n               %r,\n               feet=%r,\n               %s)," % (
        lk["shape"], tuple(lk["dims"]), lk["place"], lk["feet"], ",\n               ".join(args)))
    lines.append("          (%r, [%s],\n           %r)," % (fam, ",\n            ".join(ctrls), cfg))
    lines.append("          %r, subtitle=%r)," % (notes_of(m), sub))
    return "\n".join(lines)


HEAD = '''"""
Pedal Trinity - Ibanez (tappa 4%s): pedali a scatola con la forma del pedale vero (stomp_layout.py: profilo del piano,
coperchio-pedale incernierato, contorni speciali) e pedali a bilanciere (shaped_layout.py), modalita' normale con
nome e sigla ORIGINALI Pedal Trinity (sigle ZV/ZN/ZT/ZS/ZL/ZD/ZM/ZX) e replica REAL MOD con nome e sigla reali del
modello (mai il marchio del produttore). Nessun logo, artwork, ritratto o firma: colori e motivi geometrici semplici.

Fonti dell'aspetto: schede della tappa 4 (~/.config/PedalTrinity/stage4/stage4_specs.json: cataloghi e manuali Ibanez,
misure dei contenitori, posizioni dei comandi x%%, y%% sul piano visto dall'alto, in parte stimate +-5 punti); colori
stimati dalle descrizioni. La parte funzionale viene da sound_stage4_*.json (stomp_catalog.stomp/rocker): i comandi
qui sotto sono il segnaposto usato finche' l'id manca in quei file.
File generato da gen_catalog_ibanez.py: per cambiare l'aspetto modificare il generatore e rigenerare.
"""
from catalog import knob, outer, inner, selector, toggle, slider, button
from stomp_catalog import stomp, look, rocker, treadle_look

MODELS = [
'''


def main():
    d = json.load(open(SPECS, encoding="utf-8"))
    ms = d["models"]
    a_ids = [m for m in ms if m["id"][:3] in ("ib7", "ib9") or m["id"].startswith("ib10")]
    b_ids = [m for m in ms if m not in a_ids]
    for fn, group, tag in (("catalog_ibanez.py", a_ids, ": anni '70, serie 808, 9, 10/Power, Master, DCP"),
                           ("catalog_ibanez_b.py", b_ids, ": Soundtank, Tone-Lok, Tube Screamer moderni, MINI, Tube King, "
                                                          "Effect Component, Pentatone, wah")):
        out = [HEAD % tag]
        for m in group:
            out.append(entry(m) + "\n")
        out.append("]\n")
        if fn == "catalog_ibanez_b.py":
            ro = [x for x in d.get("real_only") or [] if x["id"] == "ed9"]
            if ro:
                m = ro[0]
                lk = build_look(m, "ib808w")
                lk["print_labels"] = {"DRIVE": "OVERDRIVE"}
                code, name, rest = real_of(m)
                args = []
                for k in ("leds", "finish", "gfx", "profile", "covers", "print_labels"):
                    if lk.get(k) not in (None, [], {}):
                        args.append("%s=%r" % (k, lk[k]))
                lk["gfx"]["sub"] = dict(lk["gfx"].get("sub") or {}, text="OVERDRIVE PRO")
                args = [a for a in args if not a.startswith("gfx=")] + ["gfx=%r" % lk["gfx"]]
                args.append("print=%r" % "Tube Screamer")
                place = [(p[0], p[1], p[2], p[3], list(p[4]) + (["DRIVE"] if p[0] == "OVERDRIVE" else [])) for p in lk["place"]]
                out.append("\n# Replica REAL MOD del modello esistente ed9 (Emerald Drive, stile 'ts' invariato in modalita' normale):\n"
                           "# TS-808 Tube Screamer originale (1979-82), serie 808 wide box, tasto quadrato metallico Q-1.\n")
                out.append("REAL_ONLY = {\n    'ed9': dict(real=dict(code=%r, name=%r),\n                look=look(%r, %r,\n                          %r,\n"
                           "                          feet=%r,\n                          %s)),\n}\n" % (
                               code, "Tube Screamer Overdrive Pro", lk["shape"], tuple(lk["dims"]), place, lk["feet"],
                               ",\n                          ".join(args)))
        with open(os.path.join(HERE, fn), "w", encoding="utf-8") as f:
            f.write("".join(out))
        print(fn, len(group))


if __name__ == "__main__":
    main()
