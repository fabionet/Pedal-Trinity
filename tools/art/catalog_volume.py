from catalog import model, knob, toggle

"""
Pedal Trinity - pedali di volume / espressione a bilanciere (Dunlop Volume X e DVP1, High Gain Volume,
Ernie Ball VP / VP Jr / MVP / VPJR Tuner, Morley ottici, DeArmond 1602 storico).
Fonti: manuali Dunlop DVP1XL/DVP3/DVP4/DVP5/GCB80, istruzioni ufficiali Ernie Ball e Morley, schede
Thomann/Sweetwater (misure), foto Commons per DeArmond (solo per la forma, nessuna immagine incorporata).

Famiglia Volume del motore (ruoli "volume" = bilanciere, "min" = volume minimo a tallone giu').
Comandi senza ruolo nel motore (restano visibili e regolabili, documentati): "gain" (MVP), "pan" (VOL/PAN
dello stereo 500K). Forma "treadle" con la sagoma del pedale vero (shaped_layout.py), senza loghi.
"""

CAT = "Utility / Routing"
SILVER = (192, 194, 198)
BLACK = (28, 28, 30)


def vol(label="VOLUME"):
    return knob(label, "volume", 1.0, "percent", 0, 100)


def look(shape, dims, place=None, leds=None, finish="anodized", tread=None, **extra):
    d = dict(shape=shape, dims=dims, place=place or {}, leds=leds or [], finish=finish, rocker_colour=None,
             rocker_finish=extra.pop("rocker_finish", finish), tread=tread or ("grip", (16, 16, 17), (16, 16, 17)),
             body_motif=None)
    d.update(extra)
    return d


def volume(id, code, name, ref, real_code, real_name, colour, controls, notes, lk, subtitle="VOLUME", stereo=False):
    return model(id, code, name, ref, CAT, "Volume", colour, controls, "", notes, subtitle=subtitle, stereo=stereo,
                 style="treadle", look=lk, real=dict(code=real_code, name=real_name))


MODELS = [
    # ================================================================== DUNLOP
    volume("dvp3", "SV-3", "Smooth Swell X", "Dunlop Volume (X) DVP3", "DVP3", "Volume (X)", BLACK, [vol()],
           "Volume passivo a profilo basso con trasmissione a nastro (niente corde): pot 250 kohm audio per il volume "
           "e 10 kohm per l'uscita d'espressione, TUNER out, minimo regolabile solo sull'espressione (manuale DVP3).",
           look("dvx", (98, 254, 65))),

    volume("dvp4", "SV-4M", "Pocket Swell", "Dunlop Volume (X) Mini DVP4", "DVP4", "Volume (X) Mini", BLACK, [vol()],
           "Volume passivo mini (formato Cry Baby Mini): 250 kohm audio + 10 kohm d'espressione, presa AUX "
           "tuner/espressione commutabile internamente (manuale DVP4).",
           look("dvx", (75, 152, 64), shape_opts=dict(r_heel=0.014, r_toe=0.016))),

    volume("dvp5", "SV-8", "Board Swell Eight", "Dunlop Volume (X)8 DVP5", "DVP5", "Volume (X) 8", BLACK, [vol()],
           "Volume passivo da 8 pollici progettato per le pedaliere: 250 kohm audio + 10 kohm d'espressione, TUNER e "
           "FX out (manuale DVP5).",
           look("dvx", (95, 203, 64))),

    volume("dvp1xl", "SV-1XL", "Wide Swell XL", "Dunlop Volume (XL) DVP1XL", "DVP1XL", "Volume (XL)", (26, 26, 28), [vol()],
           "Volume passivo full size in alluminio (stessa carcassa del DVP1): 250 kohm audio + 10 kohm d'espressione, "
           "TUNER e FX out, trasmissione a nastro (manuale DVP1XL).",
           look("dvp1", (114, 292, 76), tread=("ribs", (16, 16, 17), (16, 16, 17)))),

    volume("dvp1", "SV-1", "Band Swell Classic", "Dunlop Volume Pedal DVP1", "DVP1", "Volume Pedal", (26, 26, 28), [vol()],
           "Volume passivo full size (2010-2016) con trasmissione a nastro d'acciaio, pot 250 kohm audio e TUNER out "
           "(manuale DVP1).",
           look("dvp1", (114, 292, 76), tread=("ribs", (16, 16, 17), (16, 16, 17)))),

    volume("gcb80", "SV-80", "Iron Swell", "Dunlop High Gain Volume GCB80", "GCB80", "High Gain Volume", (24, 24, 26),
           [vol()],
           "Volume passivo nella carcassa del Cry Baby: pot 250 kohm audio da un milione di cicli, nessun minimo "
           "(muto a tallone giu') (manuale GCB80).",
           look("crybaby", (102, 254, 64), finish="crinkle", tread=("ribs", (14, 14, 15), (14, 14, 15)))),

    # ================================================================== ERNIE BALL
    volume("vpjr250", "EV-250J", "Junior Swell 250", "Ernie Ball VP Jr 250K 6180", "P06180", "VP Jr 250K", SILVER, [vol()],
           "Volume passivo compatto in alluminio: pot 250 kohm audio azionato da una corda in Vectran, micro-interruttore "
           "interno per due curve di swell, TUNER out e prese in punta (istruzioni Ernie Ball).",
           look("eb", (89, 254, 61), jack_pct=86, finish="brushed")),

    volume("vpjr25", "EV-25J", "Junior Swell 25", "Ernie Ball VP Jr 25K 6181", "P06181", "VP Jr 25K", SILVER, [vol()],
           "Versione da 25 kohm del VP Jr per sorgenti attive o a bassa impedenza (tastiere, pickup attivi); "
           "utilizzabile anche come espressione (istruzioni Ernie Ball).",
           look("eb", (89, 254, 61), jack_pct=86, finish="brushed")),

    volume("ebvp250", "EV-250", "Classic Swell 250", "Ernie Ball 250K Mono Volume Pedal 6166", "P06166",
           "250K Mono Volume Pedal", SILVER, [vol()],
           "Volume passivo full size: pot 250 kohm audio con corda su puleggia, interruttore interno della curva e "
           "TUNER out (istruzioni Ernie Ball; varianti 40th Anniversary e con cambio canale non separate).",
           look("eb", (102, 279, 70), jack_pct=86, finish="brushed")),

    volume("ebvp500s", "EV-500S", "Stereo Pan Swell", "Ernie Ball Stereo Volume/Pan 500K 6165", "P06165",
           "Stereo Volume/Pan 500K", SILVER, [vol(), toggle("VOL/PAN", "pan", ("VOL", "PAN"), 0)],
           "Volume stereo passivo con pot 500 kohm lineare e interruttore laterale a pedale VOLUME/PAN; nel plugin il "
           "modo PAN non e' riprodotto (la famiglia Volume regola solo il livello).",
           look("eb", (102, 279, 70), {"VOL/PAN": ("R", 26, "foot")}, jack_pct=86, finish="brushed"), stereo=True),

    volume("ebmvp", "EV-GB", "Boost Swell Pro", "Ernie Ball MVP Most Valuable Pedal 6182", "P06182",
           "MVP Most Valuable Pedal", SILVER,
           [vol(), knob("MIN", "min", 0.0, "percent", 0, 50), knob("GAIN", "gain", 0.0, "db", 0, 20)],
           "Volume attivo bufferizzato: MIN fissa il volume a tallone giu' (0..~50%), GAIN il guadagno in punta "
           "(unity..+20 dB, nel plugin solo indicativo); TUNER out (istruzioni Ernie Ball; lato dei pomelli stimato).",
           look("eb", (89, 254, 60), {"MIN": ("R", 70), "GAIN": ("R", 84)}, jack_pct=40, finish="brushed")),

    volume("vpjrtuner", "EV-TJ", "Tuner Swell Junior", "Ernie Ball VPJR Tuner P06203", "P06203", "VPJR Tuner",
           (30, 30, 32), [vol()],
           "Volume attivo con accordatore a sfioramento in punta (doppio tocco: volume, tuner o entrambi; diapason "
           "432-447 Hz); nel plugin il display e' decorativo (istruzioni Ernie Ball).",
           look("eb", (89, 254, 61), jack_pct=60, display=True, toe_strip=0.022)),

    # ================================================================== MORLEY
    volume("morleypla", "OV-LA", "Optic Gator Swell", "Morley Little Alligator PLA", "PLA", "Little Alligator",
           (22, 22, 24), [vol(), knob("MIN VOL", "min", 0.0, "percent", 0, 100)],
           "Volume elettro-ottico attivo senza potenziometro (LED, fotoresistenza e otturatore mosso dal pedale), "
           "curva lineare, MIN VOL per il livello a tallone giu' con LED sopra (istruzioni Morley; lato stimato).",
           look("morley", (149, 232, 70), {"MIN VOL": ("R", 76)}, [("R", 92, "red")], finish="paint")),

    volume("morleypvo", "OV-PV", "Optic Pro Swell", "Morley Pro Series Volume PVO", "PVO", "Pro Series Volume",
           (40, 40, 44), [vol(), knob("MIN VOL", "min", 0.0, "percent", 0, 100)],
           "Volume elettro-ottico attivo della serie Pro con curva audio e MIN VOL (istruzioni Morley; misure non "
           "pubblicate, stimate come il Little Alligator piu' lungo).",
           look("morley", (152, 280, 76), {"MIN VOL": ("R", 78)}, [("R", 92, "red")], finish="paint")),

    # ================================================================== STORICO
    volume("dearmond1602", "HV-16", "Fifties Gear Swell", "DeArmond Model 1602", "1602", "Model 1602 Volume Pedal",
           (112, 114, 118), [vol()],
           "Volume passivo storico (anni '50-'70) con potenziometro azionato a ingranaggio dal bilanciere; carcassa "
           "martellata nero/argento con targhetta blu; valore del pot non documentato (stima 250-500 kohm), misure "
           "stimate dalle foto Commons.",
           look("dearmond", (115, 265, 80), finish="hammer", tread=("ribs", (16, 16, 17), (16, 16, 17)),
                plate_colour=(28, 62, 150))),
]
