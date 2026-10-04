from catalog import model, knob, selector, toggle

"""
Pedal Trinity - wah a induttore stile Dunlop Cry Baby (standard, Junior, Mini, multi-range, fissi e
signature). Nome e sigla sul pedale sono originali; il pedale di riferimento e' solo in 'inspired'.
Fonti: manuali ufficiali Dunlop (jimdunlop.com/content/manuals/<SIGLA>.pdf), Electrosmash (GCB95),
R.G. Keen "The Technology of Wah Pedals", schede Thomann/Sweetwater per le misure (dossier stage 2).

Forma: stile "treadle" (bilanciere trascinabile, shaped_layout.py) con la sagoma del pedale vero; i
comandi laterali sono nella loro posizione reale lungo la fiancata (pct dal tallone, lato visto dal
chitarrista). Nessun logo: niente scritte del produttore; sulle signature solo colori e motivi semplificati.

Ruoli dei comandi (modello circuitale del wah a induttore, InductorWahEffect, config "type=inductor"):
  freq bilanciere (0 tallone, 1 punta; strip 254) | q VARIABLE Q (Rl in serie all'induttore) |
  range selettori di gamma (lista di C) | voice voci / induttori / modi (liste senza @) |
  fine FINE TUNE (Rt tra pot e massa) | level VOLUME / livello del boost o dell'uscita |
  boost levetta del boost | gain guadagno della distorsione/fuzz | dist levetta della distorsione/fuzz.
Ruoli senza effetto nel modello attuale (comandi reali comunque regolabili): mode (ordine fuzz/wah del
JH1FW), fine2 (HALO FINE del GCB65), uklevel/ukq/fuzzind/delay (JCT95).
Config: ricette di stage2/wah_recipes.md dove esistono, altrimenti stime tarate con il banco wahbench
sugli sweep e sui guadagni dei manuali (vedi CFG).
"""

WAH_CAT = "Wah / Filtri"
PEDAL = ("PEDAL", "freq", 0.5)

# finiture tipiche
CRINKLE = (24, 24, 26)
BLACK = (20, 20, 22)

# ------------------------------------------------------------------ config del wah a induttore (type=inductor)
# Ricette di stage2/wah_recipes.md (modello InductorWahEffect, Holters & Zoelzer DAFx-11) dove esistono; per gli
# altri modelli valori tarati con il banco wahbench sugli sweep e i guadagni dei manuali (stime, vedi note).
GCB = "type=inductor L=500m Rl=35 C=10n Rq=33k Rf=1.5k Ri=68k Ci=10n Rc=22k Re=390 R4=470k R5=470k R8=82k hfe=1430 vcc=8.15 pot=100k taper=hot hp=16"
JH = "type=inductor L=500m Rl=39 C=24n Rq=33k Re=470 taper=hot"
Q95 = "type=inductor L=500m C=12n Rq=27k Re=470 Rl=2k~47@q:L taper=hot boost=0.1~15@level brail=3.8"
ZW = "type=inductor L=500m Rl=51 C=15n Rq=15k Re=220 taper=hot"
CFG = {
    # --- ricette (wah_recipes.md)
    "gcb95": GCB, "tbm95": GCB, "kh95x": GCB,
    "gcb95f": "type=inductor L=500m Rl=20 C=10n Rq=33k Re=390 taper=hot",
    "cb95q": Q95,
    "cb535q": "type=inductor L=500m C=10n|13n|15n|20n|27n|33n@range Rq=20k Re=470 Rl=2k~56@q:L taper=hot boost=0.1~17@level brail=3.8",
    "db01": "type=inductor L=500m C=10n|13n|15n|20n|27n|33n@range Rq=20k Re=470 Rl=2k~56@q:L Rt=5k~0@fine taper=hot boost=0.1~17@level brail=3.8",
    "cbm95": "type=inductor L=500m Rl=35 C=10n|15n|22n@voice Rq=33k Re=390 taper=hot",
    "cbm535q": "type=inductor L=500m C=30n|18n|15n|11n@range Rq=20k Re=470 Rl=2k~56@q:L taper=hot boost=0.1~17@level brail=3.8",
    "cbm535ar": "type=inductor L=500m C=30n|18n|15n|11n@range Rq=20k Re=470 Rl=2k~56@q:L taper=hot boost=0.1~17@level brail=3.8",
    "cbj95": "type=inductor L=500m Rl=35 C=11n|15n|22n@voice Rq=33k Re=390 taper=hot",
    "jh1d": JH, "jb95": JH,
    "sw95": "type=inductor L=500m Rl=39 C=18n Rq=47k Re=470 vcc=16.5 taper=hot dist=6~40@gain dlevel=-8 dtone=3500 out=-20~0@level",
    "kh95": "type=inductor L=500m Rl=62 C=20n Rq=180k Re=560 taper=hot",
    "zw45": ZW,
    "evh95": "type=inductor L=500m Rl=56 C=11n Rq=33k Re=240 taper=evh",
    "bg95": "type=inductor L=500m Rl=47 C=18n|24n@voice Rq=27k Re=470 taper=hot",
    "jc95": "type=inductor L=500m Rl=62 C=15n Rq=47k Re=390 Rt=5k~0@fine taper=hot",
    "cb105q": "type=inductor L=500m C=16n Rq=100k Rc=12k Re=1 Rl=2k~68@q:L taper=hot boost=0.1~20@level brail=3.8",
    # --- stessa ricetta di un modello con le stesse specifiche
    "ew95v": GCB, "im95k": GCB, "ec95g": GCB, "jhm9": JH, "jh1fw": JH, "ln95": JH, "wa45": ZW,
    # --- stime tarate con wahbench (sweep e guadagni dei manuali)
    "cbm105q": "type=inductor L=500m C=16n Rq=100k Rc=12k Re=1 Rl=2k~68@q:L taper=hot out=-21~0@level",
    "qz1": "type=inductor L=500m C=12n Rq=27k Re=470 Rl=2k~47@q:L taper=hot boost=0.1~20@level brail=3.8",
    "csp030": "type=inductor L=500m C=12.4n|15.7n|21.7n@range Rq=20k Re=470~150@expand Rl=2k~56@q:L taper=hot boost=0.1~16@level brail=3.8",
    "gcb65": "type=inductor L=500m Rl=5 C=31n|19n@voice Rq=100k|220k@voice Re=1500 Rt=4k~0@fine taper=hot",
    "mc404": "type=inductor L=500m Rl=35 C=12n|25n@voice Rq=33k Re=470|560@voice taper=hot boost=0.1~20@level brail=3.8",
    "bb535r": "type=inductor L=500m Rl=35 C=11.2n|14.5n|23.9n|34.9n|46.9n|71.8n@range Rq=33k Re=560 taper=hot boost=0.1~12@level brail=3.8",
    "cm95": "type=inductor L=500m Rl=35 C=10.3n Rq=33k Re=390 taper=hot hp=0",
    "dd95fw": "type=inductor L=500m Rl=35 C=11.2n Rq=33k Re=330 taper=hot dist=50 dlevel=-14 dtone=3000",
    "sc95": "type=inductor L=500m Rl=20 C=17n Rq=47k Re=470 taper=hot",
    "gzr95": "type=inductor L=500m Rl=47 C=12n Rq=27k Re=470 taper=hot",
    "bd95": "type=inductor L=500m C=12n|10n@voice Rq=27k Re=470 Rl=2k~47@q:L taper=hot boost=0.1~15@level brail=3.8",
    "gcj95": "type=inductor L=500m Rl=35 C=11.2n Rq=33k Re=560 taper=hot",
    "mr95": "type=inductor L=500m Rl=35 C=10.1n Rq=22k Re=680 taper=hot hp=0",
    "jp95": "type=inductor L=500m Rl=56 C=28n Rq=100k Re=270 taper=hot",
    "at95": "type=inductor L=500m Rl=35 C=10.7n Rq=33k Re=680 taper=hot dist=20~60@gain dlevel=-14 dtone=3500",
    "jct95": "type=inductor L=500m C=9n|13.5n@voice Rq=4.7k|33k@voice Rc=12k|22k@voice Re=1|330@voice Rl=2k~47@q:L "
             "taper=hot out=-12~6@level dist=20~59@gain dlevel=-20~0@fuzzlevel dtone=1500~6000@tone:L",
    "tm95": "type=inductor L=500m Rl=35 C=10n|15.7n@voice Rq=33k Re=390 taper=hot",
}


def pedal():
    return knob(*PEDAL)


def look(shape, dims, place=None, leds=None, finish="crinkle", rocker=None, rocker_finish=None, tread=None,
         body_motif=None, **extra):
    """dims = (larghezza, lunghezza, altezza) in mm; tread = (motivo, colore1, colore2) della gomma."""
    d = dict(shape=shape, dims=dims, place=place or {}, leds=leds or [], finish=finish, rocker_colour=rocker,
             rocker_finish=rocker_finish or finish, tread=tread or ("ribs", (14, 14, 15), (14, 14, 15)),
             body_motif=body_motif)
    d.update(extra)
    return d


CB = (102, 254, 64)          # carcassa Cry Baby standard (mm)
MINI = (75, 133, 65)
JUNIOR = (102, 203, 76)


def wah(id, code, name, ref, real_code, real_name, colour, controls, f, q, notes, lk, subtitle="WAH"):
    """f, q: sweep dichiarato (tallone, punta) e Q tipica, solo come riferimento; il suono e' CFG[id]."""
    cfg = CFG[id]
    return model(id, code, name, "Dunlop " + ref, WAH_CAT, "Wah", colour, controls, cfg, notes,
                 subtitle=subtitle, style="treadle", look=lk, real=dict(code=real_code, name=real_name))


def box(id, code, name, ref, real_code, real_name, colour, controls, cfg, notes, lk, subtitle="FIXED WAH"):
    cfg = CFG[id]
    return model(id, code, name, "Dunlop " + ref, WAH_CAT, "Wah", colour, controls, cfg, notes,
                 subtitle=subtitle, style="box", look=lk, real=dict(code=real_code, name=real_name))


RANGE6_535 = ["1: 440-2200", "2: 400-1900", "3: 375-1800", "4: 345-1600", "5: 295-1400", "6: 250-1200"]
RANGE6_DB = ["1: 440-2.2k", "2: 400-1.9k", "3: 375-1.8k", "4: 345-1.6k", "5: 295-1.4k", "6: 250-1.2k"]
RANGE6_BB = ["1: 444-2117", "2: 385-1888", "3: 300-1468", "4: 248-1217", "5: 214-1050", "6: 173-847"]
RANGE4_MINI = ["1: 270-1300", "2: 320-1650", "3: 360-1840", "4: 420-2100"]
SHORT6 = ["1", "2", "3", "4", "5", "6"]

MODELS = [
    # ================================================================== CRY BABY STANDARD E DERIVATI
    wah("gcb95", "CW-95", "Weeping Sweep", "Cry Baby GCB95 Standard", "GCB95", "Cry Baby Standard", CRINKLE,
        [pedal()], (400, 2000), (4, 6.25),
        "Wah a induttore classico: filtro passa-banda risonante (L 500 mH Fasel rosso, C 10 nF, Rq 33k, pot 100k "
        "Hot Potz) con picco di ~18-19 dB da 350-450 Hz al tallone a 1.5-2.5 kHz in punta; buffer d'ingresso "
        "MPSA13 (manuale Dunlop GCB95, BOM Electrosmash).",
        look("crybaby", CB)),

    wah("gcb95f", "CW-95V", "Weeping Sweep Vintage", "Cry Baby GCB95F Classic", "GCB95F", "Cry Baby Classic", (26, 26, 28),
        [pedal()], (365, 1800), (3.2, 4.5),
        "Cry Baby 'Classic' con induttore Fasel e true bypass: centro un po' piu' basso e picco piu' morbido del "
        "GCB95 (sweep non pubblicato, stimato 330-400 Hz -> 1.7-1.9 kHz; manuale Dunlop GCB95F).",
        look("crybaby", CB)),

    wah("cbj95", "CW-J95", "Little Weeper", "Cry Baby Junior CBJ95", "CBJ95", "Cry Baby Junior", CRINKLE,
        [pedal(), selector("VOICING", "voice", ["H: 380-2100", "M: 330-1800", "L: 270-1500"], 0, short=["H", "M", "L"])],
        (380, 2100), (4, 6.25),
        "Cry Baby da 8 pollici (prese in punta) con slitta VOICING a 3 posizioni sulla fiancata destra: H "
        "380-2100 Hz (suono GCB95), M 330-1800 Hz vintage, L 270-1500 Hz; LED bianco, true hardwire (manuale CBJ95).",
        look("junior", JUNIOR, {"VOICING": ("R", 20)}, [("R", 34, "white")], jack_pct=80)),

    wah("cbm95", "CW-M95", "Pocket Weeper", "Cry Baby Mini CBM95", "CBM95", "Cry Baby Mini", CRINKLE,
        [pedal(), selector("VOICING", "voice", ["H: GCB95", "M: 330-1800", "L: 270-1500"], 0, short=["H", "M", "L"])],
        (400, 2000), (4, 6.25),
        "Cry Baby a meta' misura: voce H = GCB95, M vintage e L bassa con il deviatore VOICING, che sul pedale vero "
        "e' interno (qui sulla fiancata destra verso il tallone, come sul Junior) (manuale CBM95).",
        look("mini", MINI, {"VOICING": ("R", 22)})),

    wah("cbm535q", "MW-M535", "Pocket Multi Sweep", "Cry Baby Mini 535Q CBM535Q", "CBM535Q", "Cry Baby Mini 535Q", CRINKLE,
        [pedal(), toggle("BOOST", "boost", ("OFF", "ON"), 0),
         selector("RANGE", "range", RANGE4_MINI, 3, short=["1", "2", "3", "4"]),
         knob("VOLUME", "level", 0.5, "db", 0, 17), knob("Q", "q", 0.5)],
        (420, 2100), (2, 10),
        "Mini 535Q: sulla fiancata destra BOOST (fino a +17 dB), slitta RANGE a 4 posizioni (270-1300, 320-1650, "
        "360-1840, 420-2100 Hz), VOLUME e Q; filtro 15 dB, true hardwire (manuale CBM535Q).",
        look("mini", MINI, {"BOOST": ("R", 25), "RANGE": ("R", 55), "VOLUME": ("R", 70), "Q": ("R", 82)},
             [("R", 40, "red")])),

    wah("cbm535ar", "MW-M535A", "Pocket Multi Return", "Cry Baby Mini 535Q Auto-Return CBM535AR", "CBM535AR",
        "Cry Baby Mini 535Q Auto-Return", CRINKLE,
        [pedal(), toggle("BOOST", "boost", ("OFF", "ON"), 0),
         selector("RANGE", "range", RANGE4_MINI, 3, short=["1", "2", "3", "4"]),
         knob("VOLUME", "level", 0.5, "db", 0, 17), knob("Q", "q", 0.5)],
        (420, 2100), (2, 10),
        "Come il Mini 535Q ma con interruttore automatico: l'effetto si inserisce quando il piede preme il "
        "bilanciere e si spegne dopo un ritardo regolabile internamente (manuale CBM535AR).",
        look("mini", MINI, {"BOOST": ("R", 25), "RANGE": ("R", 55), "VOLUME": ("R", 70), "Q": ("R", 82)},
             [("R", 40, "red")])),

    wah("cb535q", "MW-535", "Multi Sweep Six", "Cry Baby 535Q Multi-Wah", "535Q", "Cry Baby 535Q Multi-Wah", CRINKLE,
        [pedal(), knob("VOLUME", "level", 0.5, "db", 0, 16), knob("VARIABLE Q", "q", 0.5),
         toggle("BOOST", "boost", ("OFF", "ON"), 0), selector("RANGE", "range", RANGE6_535, 0, short=SHORT6)],
        (440, 2200), (2, 10),
        "Multi-wah: VOLUME (boost fino a +16 dB) e Q sulla fiancata sinistra, pulsante BOOST e selettore RANGE a 6 "
        "posizioni (440-2200 ... 250-1200 Hz) sulla destra; induttore ~593 mH (manuale Dunlop 535Q).",
        look("crybaby", CB, {"VOLUME": ("L", 75), "VARIABLE Q": ("L", 82), "BOOST": ("R", 15), "RANGE": ("R", 85)})),

    wah("cb95q", "QW-95", "Q Sweep Return", "Cry Baby 95Q", "95Q", "Cry Baby 95Q", CRINKLE,
        [pedal(), knob("VOLUME", "level", 0.5, "db", 0, 15), knob("VARIABLE Q", "q", 0.5),
         toggle("BOOST", "boost", ("OFF", "ON"), 0)],
        (390, 2000), (2, 10),
        "Wah bufferizzato con spegnimento automatico (si inserisce premendo il bilanciere), VOLUME (boost +15 dB) e "
        "Q a sinistra, pulsante BOOST con LED rosso a destra; 390-2000 Hz, picco 17 dB (manuale 95Q).",
        look("crybaby", CB, {"VOLUME": ("L", 75), "VARIABLE Q": ("L", 82), "BOOST": ("R", 15)}, [("R", 24, "red")])),

    wah("cb105q", "BW-105", "Low End Sweep", "Cry Baby 105Q Bass Wah", "105Q", "Cry Baby Bass Wah", (226, 226, 222),
        [pedal(), knob("VOLUME", "level", 0.5, "db", 0, 20), knob("VARIABLE Q", "q", 0.5),
         toggle("BOOST", "boost", ("OFF", "ON"), 0)],
        (180, 1800), (4, 6.25),
        "Wah per basso a spegnimento automatico: sweep 180-1800 Hz con picco +25/+32 dB, VOLUME (+20 dB) e Q a "
        "sinistra, BOOST a destra (posizione stimata); carcassa bianca (manuale 105Q).",
        look("crybaby", CB, {"VOLUME": ("L", 75), "VARIABLE Q": ("L", 82), "BOOST": ("R", 15)}, finish="gloss")),

    wah("cbm105q", "BW-M105", "Pocket Low Sweep", "Cry Baby Mini Bass Wah CBM105Q", "CBM105Q", "Cry Baby Mini Bass Wah",
        (208, 210, 212),
        [pedal(), knob("VOL", "level", 0.5, "db", -21, 0), knob("Q", "q", 0.5)],
        (180, 1800), (4, 6.25),
        "Mini wah per basso con ritorno automatico: 180-1800 Hz, VOL (0/-21 dB) e Q sulla fiancata (lato stimato), "
        "ritardo di spegnimento interno 35-550 ms (manuale CBM105Q).",
        look("mini", MINI, {"VOL": ("R", 70), "Q": ("R", 82)}, finish="paint")),

    box("qz1", "FW-Q1", "Parked Sweep", "Cry Baby Q Zone QZ1", "QZ1", "Cry Baby Q Zone", (118, 120, 122),
        [knob("PEAK", "freq", 0.5, "hz", 380, 2000), knob("Q ZONE", "q", 0.5), knob("VOLUME", "level", 0.4, "db", 0, 20)],
        "type=pedal f=380,2000 q=2,10 filt=bp",
        "Wah 'parcheggiato' a pedalina: il pomello PEAK fissa la frequenza del picco (380-2000 Hz) al posto del "
        "bilanciere, Q ZONE la larghezza (picco +16 dB), VOLUME 0..+20 dB; LED rosso (manuale QZ1).",
        dict(dims=(95, 120, 55), rows=[["Q ZONE"], ["VOLUME", "LED", "PEAK"]], finish="hammer")),

    box("csp030", "FW-Q30", "Parked Sweep Custom", "Custom Shop Q Zone CSP030", "CSP030", "Q Zone Fixed Wah",
        (46, 52, 62),
        [knob("BANDPASS", "freq", 0.5), knob("BOOST", "level", 0.3, "db", 0, 16),
         selector("RANGE", "range", ["H", "M", "L"], 1), knob("Q ADJ", "q", 0.5), knob("EXPAND", "expand", 0.0)],
        "type=pedal f=300,2000 q=2,10 filt=bp",
        "Wah fisso Custom Shop con induttore Fasel rosso: BANDPASS (centro), levetta RANGE H/M/L (resa con un "
        "selettore), Q ADJ, EXPAND (estende il basso e il guadagno) e BOOST fino a +16 dB; LED bianco (manuale "
        "CSP030; gamme non pubblicate, colore da verificare).",
        dict(dims=(95, 120, 55), rows=[["BANDPASS", "Q ADJ"], ["BOOST", "RANGE", "EXPAND"]], finish="hammer")),

    wah("gcb65", "DW-65", "Twin Coil Sweep", "Cry Baby Custom Badass Dual-Inductor GCB65", "GCB65",
        "Cry Baby Dual-Inductor", (24, 24, 26),
        [pedal(), toggle("HALO/FASEL", "voice", ("HALO", "FASEL"), 1), knob("FASEL FINE", "fine", 0.5),
         knob("HALO FINE", "fine2", 0.5)],
        (550, 1500), (4, 6.25),
        "Due induttori commutabili con il kickswitch sulla fiancata destra: Fasel (550 Hz -> 1.2-1.8 kHz) e Halo "
        "(380 Hz -> 1.1-1.5 kHz), ciascuno con la sua regolazione fine della frequenza in punta; +18 dB, true "
        "hardwire (manuale GCB65).",
        look("crybaby", CB, {"HALO/FASEL": ("R", 15), "FASEL FINE": ("R", 75), "HALO FINE": ("R", 85)},
             [("H", 0.0, "white")], rocker=(176, 178, 182), rocker_finish="brushed",
             tread=("twin", (14, 14, 15), (176, 178, 182)))),

    wah("mc404", "DW-404", "Studio Twin Coil", "MC404 CAE Wah", "MC404", "Dual Inductor Wah", BLACK,
        [pedal(), knob("BOOST VOL", "level", 0.5, "db", 0, 20), toggle("INDUCTOR", "voice", ("YEL", "RED"), 0),
         toggle("BOOST", "boost", ("OFF", "ON"), 0)],
        (400, 2050), (4, 6.25),
        "Wah da studio a due induttori Fasel: giallo (400 Hz -> 1.9-2.2 kHz) e rosso (255-355 Hz -> 1.3-1.5 kHz) "
        "con kickswitch INDUCTOR a sinistra verso il tallone, BOOST a destra e pomello BOOST VOLUME (fino a +20 dB) "
        "sulla fiancata destra verso la punta come sul pedale vero; Q interne (manuale MC404, foto Commons).",
        look("crybaby", CB, {"BOOST VOL": ("R", 85), "INDUCTOR": ("L", 15), "BOOST": ("R", 15)},
             [("H", 0.6, "green"), ("L", 25, "red")], finish="paint")),

    wah("bb535r", "MW-535R", "Multi Sweep Reissue", "Cry Baby BB535 Reissue", "BB535R", "Cry Baby BB535 Reissue", CRINKLE,
        [pedal(), toggle("BOOST SEL", "boost", ("OFF", "ON"), 0), knob("BOOST", "level", 0.5, "db", 0, 12),
         selector("RANGE", "range", RANGE6_BB, 0, short=SHORT6)],
        (444, 2117), (4, 6.25),
        "Riedizione del BB535 con induttore da 535 mH: pulsante e pomellino BOOST (fino a +12 dB) e selettore "
        "RANGE a 6 posizioni (444-2117 ... 173-847 Hz) sulla fiancata destra; bufferizzato (manuale BB535R).",
        look("crybaby", CB, {"BOOST SEL": ("R", 15), "BOOST": ("R", 30), "RANGE": ("R", 85)},
             [("R", 68, "green"), ("R", 6, "red")])),

    wah("cm95", "TW-95", "Muted Trumpet Sweep", "Clyde McCoy Cry Baby CM95", "CM95", "Clyde McCoy Cry Baby", (30, 30, 32),
        [pedal()], (410, 2200), (4, 6.25),
        "Riedizione del wah 'Clyde McCoy' anni '60 con induttore Halo e senza buffer (impedenza d'ingresso ~70 "
        "kohm): 410-2200 Hz, picco 18 dB (manuale CM95; finitura stimata).",
        look("crybaby", CB, finish="paint", rocker=(214, 216, 220), rocker_finish="chrome")),

    wah("ew95v", "VW-95", "Sweep & Swell", "Mister Cry Baby Super EW95V", "EW95V", "Mister Cry Baby Super", CRINKLE,
        [pedal()], (400, 2000), (4, 6.25),
        "Wah e volume nello stesso pedale: l'interruttore in punta alterna il wah e un volume attivo con boost fino "
        "a +16 dB (nel plugin l'interruttore inserisce il wah; sweep non pubblicato, valori tipici).",
        look("crybaby", CB)),

    wah("dd95fw", "FZW-95", "Daring Fuzz Sweep", "Cry Baby Daredevil Fuzz Wah DD95FW", "DD95FW", "Cry Baby Fuzz Wah",
        (204, 206, 210),
        [pedal(), toggle("WAH/FUZZ", "dist", ("WAH", "FUZZ"), 0)],
        (375, 2100), (4, 6.25),
        "Fuzz-wah: levetta WAH / FUZZ WAH sulla fiancata destra, fuzz ad alto guadagno (+55/+63 dB) prima del wah "
        "375-2100 Hz; uscita e guadagno del fuzz interni (manuale DD95FW).",
        look("crybaby", CB, {"WAH/FUZZ": ("R", 70, "lever")}, finish="chrome", rocker=CRINKLE, rocker_finish="crinkle",
             tread=("frame", (14, 14, 15), (40, 190, 180)))),

    # ================================================================== SIGNATURE
    wah("jh1d", "SS-67", "Sixty-Seven Sweep", "Jimi Hendrix Cry Baby JH1D", "JH1D", "Jimi Hendrix Cry Baby", BLACK,
        [pedal()], (300, 1450), (3.5, 5),
        "Wah in stile fine anni '60 su carcassa in alluminio con finitura crinkle: sweep basso 290-310 Hz -> "
        "1400-1510 Hz, picco 16.5 dB (manuale JH1D).",
        look("crybaby", CB)),

    wah("jhm9", "SS-67M", "Sixty-Seven Pocket", "Jimi Hendrix Cry Baby Mini JHM9", "JHM9", "Jimi Hendrix Cry Baby Mini",
        BLACK, [pedal()], (280, 1420), (3.5, 5),
        "Versione mini del wah Hendrix: 248-310 Hz -> 1250-1600 Hz, frizione interna (manuale JHM9).",
        look("mini", MINI)),

    wah("jh1fw", "SS-67F", "Sixty-Seven Fuzz Sweep", "Jimi Hendrix Fuzz Wah JH1FW", "JH1FW", "Jimi Hendrix Fuzz Wah",
        (22, 22, 24),
        [pedal(), selector("FUZZ/WAH", "mode", ["WAH", "FUZZ", "WAH>FUZZ", "FUZZ>WAH"], 0,
                           short=["WAH", "FUZZ", "W>F", "F>W"])],
        (300, 1450), (3.5, 5),
        "Wah Hendrix con fuzz tipo Fuzz Face integrato e selettore dell'ordine (solo wah, solo fuzz, wah -> fuzz, "
        "fuzz -> wah); dati scarsi, posizione del selettore stimata (fonti secondarie).",
        look("crybaby", (102, 254, 51), {"FUZZ/WAH": ("R", 50)}, finish="paint")),

    wah("sw95", "HW-95", "Top Hat Howl", "Slash Cry Baby SW95", "SW95", "Slash Cry Baby", (18, 18, 20),
        [pedal(), toggle("DISTORTION", "dist", ("OFF", "ON"), 0), knob("VOLUME", "level", 0.5),
         knob("GAIN", "gain", 0.5)],
        (320, 1700), (4, 6.25),
        "Wah con distorsione ad alto guadagno prima del filtro (Fasel modificato, 270-370 Hz -> 1.5-1.9 kHz): "
        "pulsante DISTORTION con LED rosso, VOLUME e GAIN sulla fiancata destra; 18 V da due batterie (manuale SW95).",
        look("crybaby", CB, {"DISTORTION": ("R", 15), "VOLUME": ("R", 75), "GAIN": ("R", 85)},
             [("H", 0.6, "blue"), ("R", 24, "red")], finish="gloss")),

    wah("sc95", "HW-95C", "Top Hat Classic", "Slash Cry Baby Classic SC95", "SC95", "Slash Cry Baby Classic", (18, 18, 20),
        [pedal()], (340, 1700), (5.5, 8),
        "Wah signature con toroide high-Q da 560 mH avvolto a basso rumore: 340-1700 Hz, LED blu su entrambe le "
        "fiancate, hardwire DPDT (manuale SC95).",
        look("crybaby", CB, leds=[("R", 50, "blue"), ("L", 50, "blue")], finish="gloss")),

    wah("kh95", "RW-95", "Spirit Board Sweep", "Kirk Hammett Cry Baby KH95", "KH95", "Kirk Hammett Cry Baby", (18, 18, 20),
        [pedal()], (340, 1600), (4.5, 7),
        "Wah signature con sweep 300-380 Hz -> 1.4-1.8 kHz e picco +17/+21 dB; nero con grafica verde, qui resa con "
        "un semplice bordo verde sulla gomma (manuale KH95; varianti KH95X viola e KH95Y gialla).",
        look("crybaby", CB, finish="paint", tread=("frame", (14, 14, 15), (60, 190, 90)))),

    wah("kh95x", "RW-95X", "Spirit Board Violet", "Kirk Hammett Cry Baby KH95X", "KH95X", "Kirk Hammett Cry Baby KH95X",
        (92, 44, 132),
        [pedal()], (400, 2000), (4, 6.25),
        "Edizione KH95X del wah signature (finitura viola della serie Kirk Hammett Collection). Dunlop la presenta "
        "come variante di finitura: qui ha il circuito e i comandi del Cry Baby originale GCB95 (L 500 mH, C 10 nF, "
        "Rq 33k, pot 100k Hot Potz, ~350-450 Hz -> 1.5-2.5 kHz), con la finitura viola resa senza artwork.",
        look("crybaby", CB, finish="paint", tread=("frame", (14, 14, 15), (150, 90, 200)))),

    wah("db01", "HD-01", "Hellfire Six Sweep", "Dimebag Cry Baby From Hell DB01", "DB01", "Dimebag Cry Baby From Hell",
        (44, 46, 44),
        [pedal(), knob("VOLUME", "level", 0.5, "db", 0, 16), knob("VARIABLE Q", "q", 0.5), knob("FINE TUNE", "fine", 0.5),
         toggle("BOOST", "boost", ("OFF", "ON"), 0), selector("RANGE", "range", RANGE6_DB, 0, short=SHORT6)],
        (440, 1850), (2, 10),
        "Multi-wah signature bufferizzato: VOLUME (+16 dB), Q e FINE TUNE (frequenza in punta) a sinistra, BOOST con "
        "LED e RANGE a 6 posizioni (440 Hz/1.5-2.2 kHz ... 250 Hz/1.2 kHz) a destra (manuale DB01/DB01B, finitura "
        "mimetica nera del DB01B semplificata).",
        look("crybaby", CB, {"VOLUME": ("L", 70), "VARIABLE Q": ("L", 78), "FINE TUNE": ("L", 86), "BOOST": ("R", 15),
                             "RANGE": ("R", 85)}, [("H", 0.6, "green"), ("R", 24, "red")], finish="paint",
             body_motif="camo", tread=("camo", (20, 21, 20), (70, 72, 68)))),

    wah("zw45", "XW-45", "Bullseye Sweep", "Zakk Wylde Cry Baby ZW45", "ZW45", "Zakk Wylde Cry Baby", (150, 150, 148),
        [pedal()], (300, 2000), (4.5, 7),
        "Wah signature su fusione in metallo grezzo: 250-350 Hz al tallone, fino a ~2.4 kHz in punta, +17 dB "
        "(manuale ZW45); motivo a cerchi concentrici semplificato sulla gomma.",
        look("crybaby", CB, finish="raw", tread=("rings", (16, 16, 17), (210, 210, 205)))),

    wah("wa45", "XW-45A", "Bullseye Sweep II", "Wylde Audio Cry Baby WA45", "WA45", "Wylde Cry Baby", (24, 24, 26),
        [pedal()], (300, 2000), (4.5, 7),
        "Seconda versione del wah di Zakk Wylde con lo stesso circuito dello ZW45 (manuale WA45; finitura da "
        "verificare, qui nera con cerchi semplificati).",
        look("crybaby", CB, finish="paint", tread=("rings", (16, 16, 17), (150, 150, 146)))),

    wah("bg95", "PD-95", "Polka Dot Sweep", "Buddy Guy Cry Baby BG95", "BG95", "Buddy Guy Cry Baby", (16, 16, 18),
        [pedal(), toggle("BG/DEEP", "voice", ("BG", "DEEP"), 0)],
        (340, 1700), (4, 6.25),
        "Wah a due voci con kickswitch rosso sulla fiancata destra: BG 290-390 Hz -> 1.5-1.9 kHz, DEEP 250-330 Hz "
        "-> 1.3-1.6 kHz; induttore Fasel, LED blu sulle fiancate; nero a pois bianchi (manuale BG95).",
        look("crybaby", CB, {"BG/DEEP": ("R", 15)}, [("R", 50, "blue"), ("L", 50, "blue")], finish="gloss",
             body_motif="dots", tread=("dots", (14, 14, 15), (235, 235, 230)))),

    wah("jb95", "HL-95", "Halo Blues Sweep", "Joe Bonamassa Cry Baby JB95", "JB95", "Joe Bonamassa Cry Baby", CRINKLE,
        [pedal()], (300, 1450), (3.5, 5),
        "Wah signature con induttore Halo in stile vintage: 290-310 Hz -> 1400-1510 Hz, 16.5 dB; true hardwire o "
        "buffer con deviatore interno (manuale JB95; finitura da verificare).",
        look("crybaby", CB, tread=("frame", (14, 14, 15), (170, 40, 36)))),

    wah("evh95", "SG-95", "Striped Grail Sweep", "Cry Baby EVH95", "EVH95", "Eddie Van Halen Cry Baby", (16, 16, 18),
        [pedal()], (340, 2100), (5.5, 8),
        "Wah con induttore high-Q selezionato a mano e potenziometro a curva custom: 300-380 Hz -> 1.9-2.3 kHz, "
        "+20/+21 dB, true hardwire, due LED blu (manuale EVH95); strisce gialle e nere semplificate.",
        look("crybaby", CB, leds=[("R", 80, "blue"), ("L", 80, "blue")], finish="gloss", body_motif="stripes",
             tread=("stripes", (16, 16, 17), (232, 196, 30)))),

    wah("jc95", "OR-95", "Rust Grunge Sweep", "Jerry Cantrell Cry Baby JC95", "JC95", "Jerry Cantrell Cry Baby",
        (150, 86, 52),
        [pedal(), knob("FINE TUNE", "fine", 0.5, "hz", 1050, 2070)],
        (355, 1560), (4.5, 7),
        "Wah signature con grande pomello zigrinato FINE TUNE sulla fiancata destra che sposta la frequenza in "
        "punta (1050-2070 Hz); 320-390 Hz al tallone, +18/+20 dB; finitura rame invecchiato (manuale JC95).",
        look("crybaby", CB, {"FINE TUNE": ("R", 80, "big")}, finish="rust")),

    wah("tbm95", "RV-95", "Red Stencil Sweep", "Tom Morello Cry Baby TBM95", "TBM95", "Tom Morello Cry Baby", (186, 30, 30),
        [pedal()], (400, 2000), (4, 6.25),
        "Circuito identico al GCB95 (Fasel rosso, 350-450 Hz -> 1.5-2.5 kHz) in carcassa rossa; gli slogan a "
        "stencil del pedale vero non sono riprodotti (manuale TBM95).",
        look("crybaby", CB, finish="paint")),

    wah("jp95", "SX-95", "Smoked Six Sweep", "John Petrucci Cry Baby JP95", "JP95", "John Petrucci Cry Baby", (72, 74, 78),
        [pedal()], (220, 1350), (4.5, 7),
        "Wah con EQ grafico interno a 6 bande (100 Hz-3.2 kHz, +-18 dB) preimpostato da Petrucci, Q e volume interni: "
        "200-240 Hz -> 1.2-1.5 kHz, fino a +33 dB; cromo fume' (manuale JP95).",
        look("crybaby", CB, finish="smoked")),

    wah("gzr95", "BW-Z95", "Doom Bass Sweep", "Geezer Butler Cry Baby GZR95", "GZR95", "Geezer Butler Cry Baby", CRINKLE,
        [pedal()], (400, 2000), (4, 6.25),
        "Wah per basso a ritorno automatico che miscela il segnale pulito: 400-2000 Hz, Q e ritardo di spegnimento "
        "interni (manuale GZR95; finitura da verificare).",
        look("crybaby", CB, tread=("frame", (14, 14, 15), (110, 60, 150)))),

    wah("bd95", "WD-95", "White Twin Sweep", "Billy Duffy Cry Baby BD95", "BD95", "Billy Duffy Cry Baby", (232, 232, 228),
        [pedal(), toggle("MODE", "voice", ("95Q", "VINT"), 0), knob("VOLUME", "level", 0.5, "db", 0, 15),
         knob("Q", "q", 0.5)],
        (410, 2000), (2, 10),
        "Wah a ritorno automatico con due modi sul kickswitch MODE: 95Q (410-2000 Hz, VOLUME +15 dB e Q attivi, LED "
        "blu) e Vintage (450-2200 Hz, LED rossi); bianco lucido e cromo (manuale BD95).",
        look("crybaby", CB, {"MODE": ("R", 12), "VOLUME": ("R", 78), "Q": ("R", 88)}, [("L", 50, "blue"), ("L", 57, "red")],
             finish="gloss", rocker=(214, 216, 220), rocker_finish="chrome")),

    wah("gcj95", "CU-95", "Copper Blues Sweep", "Gary Clark Jr. Cry Baby GCJ95", "GCJ95", "Gary Clark Jr. Cry Baby",
        (184, 115, 75),
        [pedal()], (450, 2100), (4, 6.25),
        "Wah signature in rame spazzolato che si ossida col tempo: manuale 400-500 Hz -> 2.0-2.2 kHz (i rivenditori "
        "riportano valori piu' bassi), +17 dB (manuale GCJ95).",
        look("crybaby", CB, finish="copper")),

    wah("ln95", "MG-95", "Carnival Sweep", "Leo Nocentelli Cry Baby LN95", "LN95", "Leo Nocentelli Cry Baby", (88, 40, 120),
        [pedal()], (300, 1450), (3.5, 5),
        "Wah signature 'Mardi Gras': 290-310 Hz -> 1.4-1.5 kHz, 16.5 dB, hardwire; viola glitterato con accenti verdi "
        "e oro semplificati (manuale LN95).",
        look("crybaby", CB, finish="sparkle", tread=("diamonds", (14, 14, 15), (212, 176, 60)))),

    wah("mr95", "SP-95", "Stardust Filter Sweep", "Mick Ronson Cry Baby MR95LTD", "MR95LTD", "Mick Ronson Cry Baby",
        (226, 214, 180),
        [pedal()], (500, 2250), (2.8, 4),
        "Edizione limitata con induttore custom, transistor a basso guadagno e senza buffer (Zin 78 kohm): 500-2250 "
        "Hz con picco piu' morbido, pensato anche come filtro fisso (manuale MR95LTD; finitura da verificare).",
        look("crybaby", CB, finish="gloss", rocker=(210, 172, 86), rocker_finish="gold")),

    wah("im95k", "KL-95", "Nightmare Sweep", "Iron Maiden Killers Cry Baby IM95K", "IM95K", "Iron Maiden Killers Cry Baby",
        (30, 30, 34),
        [pedal()], (400, 2000), (4, 6.25),
        "Edizione da collezione con il circuito del GCB95 (Fasel rosso); l'artwork dell'album sulla gomma non e' "
        "riprodotto (manuale IM95K; colori da verificare).",
        look("crybaby", CB, finish="paint", tread=("frame", (14, 14, 15), (226, 190, 40)))),

    wah("ec95g", "GD-95", "Golden Sixty Sweep", "Eric Clapton Cry Baby EC95G", "EC95G", "Eric Clapton Cry Baby",
        (212, 170, 80),
        [pedal()], (400, 2000), (4, 6.25),
        "Edizione limitata placcata oro con il circuito del GCB95 (350-450 Hz -> 1.5-2.5 kHz); stesso aspetto del "
        "GCB95G '50th Anniversary' (manuale EC95G).",
        look("crybaby", CB, finish="gold")),

    wah("at95", "AF-95", "Blue Thunder Fuzz", "Cry Baby Akira Takasaki Fuzz Wah AT95", "AT95", "Akira Takasaki Fuzz Wah",
        (30, 70, 170),
        [pedal(), toggle("FUZZ", "dist", ("OFF", "ON"), 0), knob("FUZZ SENS", "gain", 0.5)],
        (480, 2200), (4.5, 7),
        "Fuzz-wah a ritorno automatico: pulsante FUZZ ON/OFF e FUZZ SENSITIVITY sulla fiancata destra, LED verde "
        "(fuzz) e blu (wah) sul tallone; 480-2200 Hz, fuzz fino a +60 dB (manuale AT95).",
        look("crybaby", CB, {"FUZZ": ("R", 20), "FUZZ SENS": ("R", 70)}, [("H", 0.45, "blue"), ("H", -0.45, "green")],
             finish="paint")),

    wah("jct95", "UK-95", "Spiral Filter Fuzz", "Justin Chancellor Cry Baby JCT95", "JCT95", "Justin Chancellor Cry Baby",
        (40, 80, 170),
        [pedal(), toggle("WAH SELECT", "voice", ("UK", "WAH"), 1), knob("WAH VOL", "level", 0.5), knob("WAH Q", "q", 0.5),
         knob("UK VOL", "uklevel", 0.5), knob("UK Q", "ukq", 0.5), knob("FUZZ", "gain", 0.5), knob("TONE", "tone", 0.5),
         knob("VOL", "fuzzlevel", 0.5), toggle("FUZZ SEL", "dist", ("OFF", "ON"), 0),
         toggle("FUZZ IND", "fuzzind", ("OFF", "ON"), 0), knob("BYP DELAY", "delay", 0.5)],
        (340, 1920), (4, 6.25),
        "Carcassa larga: bilanciere a ritorno automatico a sinistra e pannello a destra con interruttori WAH SELECT "
        "(wah Fasel 340-1920 Hz / filtro 'UK' a stato solido 232-2700 Hz) e FUZZ SELECT, volume e Q dei due filtri, "
        "fuzz con TONE e VOL, kickswitch FUZZ IND e BYPASS DELAY (manuale JCT95).",
        look("wide", (170, 260, 70),
             {"WAH SELECT": ("P", 0.5, 91, "foot"), "WAH VOL": ("P", 0.27, 82), "WAH Q": ("P", 0.73, 82),
              "UK VOL": ("P", 0.27, 68), "UK Q": ("P", 0.73, 68), "FUZZ": ("P", 0.5, 54), "TONE": ("P", 0.27, 40),
              "VOL": ("P", 0.73, 40), "FUZZ SEL": ("P", 0.5, 16, "foot"), "FUZZ IND": ("R", 12),
              "BYP DELAY": ("P", 0.84, 8)},
             [("P", (0.14, 91), "yellow")], finish="anodized", tread=("studs", (14, 14, 15), (14, 14, 15)))),

    wah("tm95", "BZ-95", "Twin Mode Sweep", "Tak Cry Baby TM95", "TM95", "Tak Matsumoto Cry Baby", (230, 230, 228),
        [pedal(), toggle("MODE", "voice", ("1", "2"), 0)],
        (400, 2000), (4, 6.25),
        "Wah a due modi per il mercato giapponese: MODE 1 enfatizza gli acuti (ritmica), MODE 2 ha gamma piu' ampia "
        "per gli assoli; sul pedale vero si commuta con un footswitch esterno o un deviatore interno, qui con un "
        "tasto sul tallone accanto al LED (frequenze non pubblicate).",
        look("crybaby", CB, {"MODE": ("H", -0.45)}, [("H", 0.45, "red")], finish="gloss",
             tread=("frame", (14, 14, 15), (120, 60, 160)))),
]
