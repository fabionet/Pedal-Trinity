from catalog import model, knob, outer, inner, selector, toggle, slider

"""
Pedal Trinity - pitch / ottave, synth e multi-modello.
Fonti: docs/research/circuits_dynamics_eq_utility.json/.md (OC-2 catena completa, OC-3, OC-5, PS-2..PS-6, HR-2,
SY-1, SY-200, SYB-3, SYB-5), circuits_modulation.json e digital_delay_reverb_amp.json (MO-2),
circuits_overdrive.md + catalog_core.py (OD-1/SD-1/DS-1 per il multi-modello), boss_catalog.json (comandi, colori).
Motore: Source/engine/FxPitch.cpp (famiglie Pitch e Synth).
"""

KEYS = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
# intervalli diatonici del motore harmonist (FxPitch.cpp): -12,-6,-5,-4,-3,-2,+2,+3,+4,+5,+6,+12
HARMONY = ["-1 OCT", "-6TH", "-5TH", "-4TH", "-3RD", "-2ND", "+2ND", "+3RD", "+4TH", "+5TH", "+6TH", "+1 OCT"]
HARMONY_S = ["-8", "-6", "-5", "-4", "-3", "-2", "+2", "+3", "+4", "+5", "+6", "+8"]
# HR-2: 11 posizioni reali; con il motore (indice*11/10 arrotondato) cadono esattamente sugli intervalli
# sopra, tranne DT che diventa +2nd.
HR2_VOICE = ["-OCT", "-6", "-5", "-4", "-3", "DT", "+3", "+4", "+5", "+6", "+OCT"]

MODELS = [
    # ================================================================== OTTAVE
    model("oc2", "UO-2", "Deep Divider", "BOSS OC-2 Octave", "Pitch / Ottave", "Pitch",
          (158, 96, 52),
          [knob("OCT 1", "oct1", 0.6), knob("OCT 2", "oct2", 0.3), knob("DIRECT", "direct", 0.7)],
          "type=analog lp=700 gain=4.7 range=12",
          "Octaver analogico monofonico: preamp non invertente 1+10k/2.7k = 4.7x, filtro d'analisi RC 22k/10n (723 Hz) "
          "+ Sallen-Key 330k/2n2/220p (693 Hz, Q 1.58), comparatori con isteresi dal rivelatore di picco e flip-flop "
          "4013/BA634 per -1 e -2 ottave moltiplicati per +-0.5 (schema clone PedalPCB Ocelot).",
          accent=(30, 20, 15), subtitle="OCTAVE"),

    model("oc3", "UO-3", "Super Divider", "BOSS OC-3 Super Octave", "Pitch / Ottave", "Pitch",
          (112, 44, 40),
          [knob("DIRECT", "direct", 0.7), knob("OCT1", "oct1", 0.6), knob("CONTROL", "oct2", 0.3),
           selector("MODE", "mode", ["POLY", "OCT2", "DRIVE"], 0, short=["POLY", "OCT2", "DRV"])],
          "type=poly range=12 modes=poly,oct2,drive",
          "Octaver digitale polifonico: OCT1 = -1 ottava polifonica, CONTROL = livello -2 ottave (modo OCT2 compatibile "
          "OC-2); nel modo DRIVE la distorsione dell'ottava non e' riprodotta e CONTROL resta inattivo (manuale OC-3).",
          accent=(235, 225, 210), subtitle="SUPER OCTAVE"),

    model("oc5", "UO-5", "Octave Divider", "BOSS OC-5 Octave", "Pitch / Ottave", "Pitch",
          (96, 58, 42),
          [knob("DIRECT", "direct", 0.7), knob("+1 OCT", "up", 0.0), knob("-1 OCT", "oct1", 0.6),
           knob("-2 OCT", "oct2", 0.3),
           selector("MODE", "mode", ["VINTAGE", "POLY", "POLY +1"], 1, short=["VINT", "POLY", "+1"])],
          "type=poly range=12 modes=oct1,oct2,up",
          "Octaver polifonico con -1/-2/+1 ottava e modo VINTAGE che simula l'OC-2 (manuale OC-5). Il motore miscela due "
          "voci trasposte: nei modi VINTAGE/POLY -1 e -2 ottave, nella terza posizione -1 e +1 ottava (la levetta "
          "reale VINTAGE/POLY e lo switch GUITAR/BASS sono raggruppati in questo selettore).",
          accent=(235, 225, 210), subtitle="OCTAVE"),

    # ================================================================== PITCH SHIFTER / HARMONIST
    model("ps2", "PV-2", "Pitch Voyager", "BOSS PS-2 Digital Pitch Shifter/Delay", "Pitch / Ottave", "Pitch",
          (45, 88, 170),
          [knob("BALANCE", "balance", 0.5), knob("F.BACK", "feedback", 0.3),
           knob("MANUAL", "shift", 0.5, "semitone", -12, 12),
           selector("MODE", "mode", ["DLY 125", "DLY 500", "DLY 2S", "MANUAL", "+1 OCT", "-1 OCT"], 3,
                    short=["125", "500", "2S", "MAN", "+1", "-1"])],
          "type=shifter range=12 lp=6000",
          "Pitch shifter digitale +-1 ottava (banda dell'effetto 80 Hz-6 kHz da specifiche Roland); MANUAL regola la "
          "trasposizione a semitoni. I modi delay e F.BACK non sono gestiti dalla famiglia Pitch (vedi DigitalDelay).",
          accent=(240, 240, 235), subtitle="PITCH SHIFTER/DELAY"),

    model("ps3", "PV-3", "Pitch Voyager II", "BOSS PS-3 Digital Pitch Shifter/Delay", "Pitch / Ottave", "Pitch",
          (34, 58, 138),
          [knob("BALANCE", "balance", 0.5), knob("PITCH A", "shift", 0.75, "semitone", -24, 24),
           knob("PITCH B", "fine", 0.5, "semitone", -0.5, 0.5),
           selector("MODE", "mode", ["DLY S", "DLY M", "DLY L", "DETUNE", "FAST", "SLOW", "INVERSE", "DT&DT",
                                     "DT&PITCH", "P&P", "EXP"], 4,
                    short=["D1", "D2", "D3", "DT", "FAST", "SLOW", "INV", "DTDT", "DTP", "PP", "EXP"])],
          "type=shifter range=24",
          "Pitch shifter a 16 bit / 32 kHz con 26 passi (+-12 semitoni e +-2 ottave) e 11 modi (specifiche Roland); "
          "PITCH A = trasposizione, PITCH B = detune fine +-50 cent sulla stessa voce (il motore ha una sola voce); "
          "i modi delay non sono riprodotti.",
          accent=(240, 240, 235), subtitle="PITCH SHIFTER/DELAY", stereo=True),

    model("ps5", "PV-5", "Super Transposer", "BOSS PS-5 Super Shifter", "Pitch / Ottave", "Pitch",
          (30, 122, 140),
          [outer("BALANCE", "balance", 0.5), inner("D.TIME", "time", 0.3),
           selector("H.R KEY", "key", KEYS, 0),
           knob("PITCH", "shift", 0.75, "semitone", -24, 24),
           selector("MODE", "mode", ["PITCH", "HARMONIST", "DETUNE", "T.ARM", "FLUTTER"], 0,
                    short=["PIT", "HR", "DT", "TARM", "FLT"])],
          "type=shifter range=24",
          "Super shifter polifonico +-2 ottave (valori +-1,2,5,7,12,24 semitoni sul pomello reale, qui a semitoni); "
          "BALANCE con parita' al centro, D.TIME/SPEED concentrico. Modi HARMONIST/T.ARM/FLUTTER non distinti dal "
          "motore shifter (manuale PS-5).",
          accent=(240, 240, 235), subtitle="SUPER SHIFTER", stereo=True),

    model("ps6", "HV-6", "Harmony Weaver", "BOSS PS-6 Harmonist", "Pitch / Ottave", "Pitch",
          (24, 100, 124),
          [knob("BALANCE", "level", 0.5), selector("SHIFT", "harmony", HARMONY, 7, short=HARMONY_S),
           selector("KEY", "key", KEYS, 0),
           selector("MODE", "mode", ["HARM MAJ", "HARM MIN", "PITCH", "DETUNE", "S-BEND"], 0,
                    short=["MAJ", "MIN", "PIT", "DT", "BEND"])],
          "type=harmonist range=24",
          "Harmonist diatonico: riconosce la nota (YIN) e sceglie l'intervallo nella tonalita' KEY; SHIFT elenca gli "
          "intervalli del motore (-1 ott ... +1 ott) invece delle combinazioni a 2 voci del PS-6; BALANCE = livello "
          "dell'armonia (specifiche/manuale PS-6).",
          accent=(240, 240, 235), subtitle="HARMONIST", stereo=True),

    model("hr2", "HV-2", "Twin Voices", "BOSS HR-2 Harmonist", "Pitch / Ottave", "Pitch",
          (50, 92, 172),
          [outer("E.LEVEL A", "level", 0.6), inner("E.LEVEL B", "level2", 0.0),
           selector("VOICE A", "harmony", HR2_VOICE, 6, short=HR2_VOICE),
           selector("VOICE B", "harmony2", HR2_VOICE, 5, short=HR2_VOICE),
           selector("KEY", "key", KEYS, 0)],
          "type=harmonist range=12",
          "Armonizzatore intelligente a 2 voci con 12 tonalita' e intervalli -1 ott ... +1 ott (effectsdatabase, "
          "manuale HR-2); il motore rende la voce A (DT diventa +2nd), la voce B e il suo livello sono solo di pannello.",
          accent=(240, 240, 235), subtitle="HARMONIST", stereo=True),

    model("mo2", "MV-2", "Overtone Mist", "BOSS MO-2 Multi Overtone", "Pitch / Ottave", "Pitch",
          (82, 110, 142),
          [knob("BALANCE", "balance", 0.5), knob("TONE", "tone", 0.5),
           knob("DETUNE", "fine", 0.6, "semitone", -0.5, 0.5),
           selector("MODE", "shift", ["-1 OCT", "+-1 OCT", "+1 OCT"], 2, short=["3", "2", "1"])],
          "type=shifter range=12",
          "Generatore di armonici MDP (confidenza bassa): MODE 1 +1 ottava, MODE 3 -1 ottava, MODE 2 approssimato con "
          "voce all'unisono desintonizzata (effetto rotante); DETUNE = +-50 cent. Specifiche boss.info e manuale MO-2.",
          accent=(240, 240, 235), subtitle="MULTI OVERTONE", stereo=True),

    model("xs1", "XP-1", "Poly Transposer", "BOSS XS-1 Poly Shifter", "Pitch / Ottave", "Pitch",
          (104, 172, 214),
          [knob("BALANCE", "balance", 1.0), knob("SHIFT", "shift", 0.25, "semitone", -24, 24)],
          "type=shifter range=24",
          "Pitch shifter polifonico per drop tuning, capo e ottave (boss.info); SHIFT bipolare a semitoni ingloba la "
          "levetta SHIFT DIRECTION, la levetta PEDAL MODE (uso con pedale) non e' riprodotta.",
          accent=(20, 40, 70), subtitle="POLY SHIFTER"),

    model("xs100", "XP-100", "Poly Transposer Pro", "BOSS XS-100 Poly Shifter", "Pitch / Ottave", "Pitch",
          (31, 156, 222),
          [knob("PEDAL", "shift", 0.5, "semitone", -24, 24), knob("BALANCE", "balance", 1.0),
           toggle("TUNE DOWN", "tunedown", ("OFF", "ON"), 0)],
          "type=shifter range=24",
          "Versione da pavimento del poly shifter con pedale d'espressione integrato (stile whammy): PEDAL = "
          "trasposizione continua +-2 ottave, BALANCE dal menu; TUNE DOWN solo di pannello (boss.info).",
          accent=(104, 172, 214), subtitle="POLY SHIFTER", style="treadle", look=dict(boss=True)),

    # ================================================================== SYNTH
    model("syb3", "BZ-3", "Low Synth", "BOSS SYB-3 Bass Synthesizer", "Basso", "Synth",
          (176, 170, 196),
          [outer("EFFECT", "level", 0.6), inner("DIRECT", "direct", 0.3),
           outer("FREQ", "freq", 0.4, "hz", 60, 4000), inner("RES", "res", 0.5),
           outer("SENS", "sens", 0.5), inner("DECAY", "decay", 0.5),
           selector("MODE", "wave", ["SAW", "SQUARE", "PWM", "SAW+NOISE", "SAW-1OCT", "PWM+NOISE", "SN-1OCT",
                                     "W.SHAPE N", "W.SHAPE R", "T.WAH N", "T.WAH R"], 0,
                    short=["SAW", "SQR", "PWM", "SWN", "SW-1", "PWN", "SN-1", "WS", "WSR", "TW", "TWR"])],
          "type=digital f=60,4000",
          "Synth per basso digitale: oscillatore interno che segue la nota (saw/quadra/impulso) nel filtro risonante "
          "con inviluppo FREQ/RES/DECAY; i modi W.SHAPE e T.WAH sono approssimati dall'onda a impulso "
          "(synthmania, vintagesynth).",
          accent=(40, 30, 90), subtitle="BASS SYNTHESIZER", stereo=True),

    model("syb5", "BZ-5", "Low Synth II", "BOSS SYB-5 Bass Synthesizer", "Basso", "Synth",
          (162, 156, 186),
          [outer("FREQ", "freq", 0.4, "hz", 60, 4000), inner("RES", "res", 0.5),
           outer("DECAY", "decay", 0.5), inner("RATE", "rate", 0.5),
           outer("EFFECT", "level", 0.6), inner("DIRECT", "direct", 0.0),
           selector("MODE", "wave", ["SAW", "SAW+SUB", "SAW LFO", "SQUARE", "SQR+SUB", "SQR LFO", "PULSE",
                                     "PULSE+PH", "PWM", "W.SHAPE UP", "W.SHAPE DN"], 0,
                    short=["SAW", "S-1", "SLFO", "SQR", "Q-1", "QLFO", "PLS", "PHS", "PWM", "WSU", "WSD"])],
          "type=digital f=60,4000",
          "Synth per basso a 11 modi (manuale SYB-5): 9 a oscillatore interno su note singole, 2 WAVE SHAPE; "
          "DECAY = sweep del filtro, RATE = LFO nei modi 3 e 6 (non gestito dal motore). Onde approssimate con "
          "dente di sega, quadra e impulso.",
          accent=(40, 30, 90), subtitle="BASS SYNTHESIZER", stereo=True),

    model("vo1", "VX-1", "Robot Choir", "BOSS VO-1 Vocoder", "Synth", "Synth",
          (212, 72, 142),
          [outer("LEVEL", "level", 0.6), inner("BLEND", "direct", 0.3),
           knob("TONE", "freq", 0.5, "hz", 250, 3500), knob("COLOR", "res", 0.6),
           selector("MODE", "wave", ["TALK BOX", "ADVANCED", "VINTAGE", "CHOIR"], 0,
                    short=["TALK", "ADV", "VINT", "CHOR"]),
           toggle("MIC SENS", "sens", ("LO", "HI"), 1)],
          "type=digital f=250,3500",
          "Vocoder con microfono XLR: senza modulatore vocale e' reso con la famiglia Synth (portante che segue la "
          "nota in un filtro risonante 250-3500 Hz aperto dall'inviluppo, zona delle formanti), piu' vicina del Wah "
          "perche' il VO-1 sintetizza la portante; MIC SENS = sensibilita' dell'inviluppo (boss.info).",
          accent=(250, 235, 245), subtitle="VOCODER"),

    model("sy1", "SN-1", "Synth Garden", "BOSS SY-1 Synthesizer", "Synth", "Synth",
          (98, 112, 200),
          [selector("TYPE", "mode", ["LEAD1", "LEAD2", "PAD", "BASS", "STR", "ORGN", "BELL", "SFX1", "SFX2",
                                     "SEQ1", "SEQ2"], 0,
                    short=["LD1", "LD2", "PAD", "BASS", "STR", "ORGN", "BELL", "SFX1", "SFX2", "SEQ1", "SEQ2"]),
           selector("VARIATION", "wave", [str(k) for k in range(1, 12)], 0),
           outer("TONE", "freq", 0.5, "hz", 120, 8000), inner("DEPTH", "sens", 0.5),
           outer("EFFECT", "level", 0.6), inner("DIRECT", "direct", 0.5),
           toggle("GTR/BASS", "range", ("GTR", "BASS"), 0)],
          "type=digital f=120,8000",
          "Synth polifonico per chitarra/basso con 11 tipi x 11 variazioni (manuale SY-1); qui oscillatore che segue "
          "la nota (monofonico) con onda scelta da VARIATION e filtro TONE/DEPTH con inviluppo.",
          accent=(245, 245, 250), subtitle="SYNTHESIZER"),

    model("sy200", "SN-200", "Synth Garden 200", "BOSS SY-200 Synthesizer", "Synth", "Synth",
          (40, 40, 45),
          [selector("TYPE", "mode", ["LEAD", "PAD", "STRING", "BELL", "ORGAN", "BASS", "DUAL", "SWEEP", "NOISE",
                                     "SFX", "SEQ", "ARPEGGIO"], 0,
                    short=["LEAD", "PAD", "STR", "BELL", "ORGN", "BASS", "DUAL", "SWP", "NOIS", "SFX", "SEQ", "ARP"]),
           selector("VARIATION", "wave", ["1", "2", "3"], 0),
           knob("KNOB 1", "freq", 0.5, "hz", 100, 9000), knob("KNOB 2", "res", 0.5), knob("KNOB 3", "decay", 0.5),
           knob("E.LEVEL", "level", 0.6), knob("D.LEVEL", "direct", 0.5)],
          "type=digital f=100,9000",
          "Evoluzione dell'SY-1 con 12 tipi e 128 memorie (44.1 kHz, DSP 32 bit, specifiche boss.info); i tre "
          "pomelli assegnabili sono fissati su cutoff, risonanza e decay dell'inviluppo del filtro.",
          accent=(120, 200, 255), subtitle="SYNTHESIZER"),

    model("sy300", "SN-300", "Synth Engine", "BOSS SY-300 Guitar Synthesizer", "Synth", "Synth",
          (190, 192, 198),
          [selector("WAVE", "wave", ["SAW", "SQR", "PWM"], 0),
           knob("CUTOFF", "freq", 0.5, "hz", 80, 10000), knob("RESO", "res", 0.5),
           knob("ENV DEPTH", "sens", 0.5), knob("DECAY", "decay", 0.5),
           knob("SYNTH", "level", 0.6), knob("DIRECT", "direct", 0.4)],
          "type=digital f=80,10000",
          "Synth polifonico senza pickup esafonico a 3 oscillatori (SIN/SAW/TRI/SQR/PWM/NOISE, boss.info): qui "
          "un oscillatore che segue la nota con filtro risonante e inviluppo; comandi di pannello al posto del display.",
          accent=(30, 30, 33), subtitle="GUITAR SYNTHESIZER", stereo=True),

    # ================================================================== MULTI-MODELLO
    # MODEL (indice 3): 0 = OD-1, 1 = SD-1, 2 = DS-1. Gli stadi non usati da un modello diventano trasparenti
    # tramite sw() (condensatori da 1p, guadagno unitario, diodi a -30 dB).
    model("px1", "MX-1", "Model Carrier", "BOSS PX-1 Plugout FX", "Overdrive / Boost", "Circuit",
          (206, 208, 211),
          [knob("LEFT", "level", 0.5), knob("CENTER", "tone", 0.5), knob("RIGHT", "gain", 0.5),
           selector("MODEL", "mode", ["OVERDRIVE", "SUPER OD", "DISTORTION"], 1, short=["OD", "SOD", "DIST"])],
          """
          hpf R=470k C=47n;
          hpf R=sw(3,100k,100k,10k) C=sw(3,4.7n,18n,26n);
          bjt g=sw(3,1,1,60) vp=4.8 vn=3.6 soft=2.2;
          lpf R=10k C=sw(3,1p,1p,1n);
          opni Rg=4.7k Cg=1u Rf=pot(2,100k,B)*sw(3,0,0,1)+10 Cf=100p rail=3.8;
          fbclip Rg=4.7k Cg=47n Rf=pot(2,1M,A)*sw(3,1,1,0)+sw(3,33k,33k,10) Cf=sw(3,47p,10p,1p) d=si up=1 dn=2 rail=4.2;
          lpf R=10k C=sw(3,18n,1p,1p);
          tonets R7=10k C4=sw(3,1p,18n,1p) P=20k t=taper(1,W) Rw=470 Cw=sw(3,1p,27n,1p) Rf=10k Cf=sw(3,1p,10n,1p) rail=4.2;
          gain db=sw(3,-30,-30,0);
          dclip R=2.2k C=sw(3,0,0,10n) d=si;
          gain db=sw(3,30,30,0);
          tonebm R1=sw(3,10,10,6.8k) C1=sw(3,1p,1p,100n) R2=sw(3,10,10,6.8k) C2=sw(3,1p,1p,22n) t=taper(1,B)*sw(3,0,0,1);
          vol a=taper(0,B)
          """,
          "Piattaforma multi-modello: resa con la famiglia Circuit e un selettore MODEL fra tre dei 16 modelli "
          "inclusi, i drive (OD-1: HPF 339 Hz, 33k+1M, diodi 1:2, LPF 884 Hz; SD-1: HPF 88 Hz e tono attivo 20k W; "
          "DS-1: booster + op-amp + diodi a massa, dai dossier/service note). LEFT/CENTER/RIGHT = level/tone/drive.",
          accent=(30, 30, 33), subtitle="MULTI MODEL", stereo=True),
]
