from catalog import model, knob, outer, inner, selector, toggle, slider

"""
Pedal Trinity - modelli di MODULAZIONE e FILTRI (chorus, dimension, flanger, phaser,
tremolo/pan/slicer, rotary, vibrato, wah/filtri).
Fonti: docs/research/circuits_modulation.json/.md (valori di clock, stadi, LFO, filtri),
docs/research/boss_catalog.json (comandi e colori).
Ritardo BBD = stages / (2 * clk). Le gamme di rate sono in Hz (mappa logaritmica).
"""

# colori ricorrenti
AQUA = (90, 180, 210)
CE_BLUE = (60, 140, 205)
BASS_BROWN = (72, 46, 32)
CREAM = (228, 206, 150)
PURPLE = (118, 58, 138)
GREEN = (52, 146, 78)
BLACK = (30, 30, 33)
LAVENDER = (178, 182, 214)

# ------------------------------------------------------------------ configurazioni condivise
CE2_CFG = "type=chorus stages=1024 clk=85000,150000 rate=0.32,3.5 lfo=0 aa=6800 comp=0 sat=1"
CH1_CFG = "type=chorus stages=1024 clk=85000,150000 rate=0.27,4.7 lfo=0 aa=20000 comp=0 stereo=1"
DC2_CFG = ("type=dimension stages=1024 clk=92000,113000 rate=0.3,0.84 lfo=0 aa=20000 comp=1 stereo=1 "
           "modes=0.3,0.6,0.3,1.0,0.62,0.61,0.84,0.63")
BF2_CFG = "type=flanger stages=1024 clk=40000,500000 rate=0.0625,10 lfo=0 aa=12800 comp=0 fbmax=0.9 sat=0.8"
HF2_CFG = "type=flanger stages=512 clk=40000,500000 rate=0.0625,10 lfo=0 aa=12800 comp=0 fbmax=0.9 sat=0.8"
VB2_CFG = "type=vibrato stages=1024 clk=80000,205000 rate=2,15 lfo=1 aa=8000 comp=0 rise=5000"


def rate_knob(lo, hi, default=0.4, label="RATE"):
    return knob(label, "rate", default, "hz", lo, hi)


MODELS = [
    # ================================================================== CHORUS / DIMENSION
    model("ce1", "EN-1", "Ensemble One", "BOSS CE-1 Chorus Ensemble", "Chorus / Dimension", "BBDChorus",
          (150, 152, 156),
          [toggle("MODE", "mode", ("CHO", "VIB"), 0),
           knob("LEVEL", "level", 0.7),
           knob("INTENSITY", "depth", 0.5),
           knob("V.DEPTH", "vdepth", 0.5),
           knob("V.RATE", "vrate", 0.4, "hz", 1, 10)],
          "type=chorus stages=512 clk=40000,70000 rate=0.5,0.6 lfo=0 aa=6800 comp=0 stereo=1",
          "Chorus/vibrato BBD da pavimento derivato dal JC-120: in modo chorus rate fisso e INTENSITY sulla "
          "profondita', uscite A/B stereo. Dati di clock e rate stimati (confidenza bassa, boss_catalog/effectsdatabase).",
          accent=(20, 20, 20), subtitle="CHORUS ENSEMBLE", stereo=True),

    model("ce2", "AQ-2", "Aqua Chorus", "BOSS CE-2 Chorus", "Chorus / Dimension", "BBDChorus",
          AQUA,
          [rate_knob(0.32, 3.5), knob("DEPTH", "depth", 0.5)],
          CE2_CFG,
          "MN3007 (1024 stadi) + MN3101, clock ~85-150 kHz (4-6 ms), LFO a triangolo 0.32-3.5 Hz, "
          "filtri anti-alias/ricostruzione a 3 poli (-3 dB 6.8 kHz) con pre/de-enfasi 15 dB, mix fisso 50/50 "
          "(clone Aion Azure, ElectroSmash).",
          accent=(20, 40, 90), subtitle="CHORUS"),

    model("ce3", "TI-3", "Stereo Tide", "BOSS CE-3 Chorus", "Chorus / Dimension", "BBDChorus",
          (70, 140, 200),
          [rate_knob(0.33, 3.7), knob("DEPTH", "depth", 0.5), toggle("MODE", "mode", ("I", "II"), 0)],
          CE2_CFG.replace("rate=0.32,3.5", "rate=0.33,3.7") + " stereo=1",
          "Come il CE-2 (MN3207+MN3102, filtri 3n3/8n2/470p a 6.8 kHz) con LFO 0.33-3.7 Hz (service note) e "
          "uscite stereo: modo I A=dry+wet / B=dry-wet in controfase, modo II A=wet / B=dry. Clock assunto come CE-2.",
          accent=(20, 30, 70), subtitle="CHORUS", stereo=True),

    model("dc2", "WS-2", "Wide Space", "BOSS DC-2 Dimension C", "Chorus / Dimension", "BBDChorus",
          LAVENDER,
          [selector("MODE", "mode", ["1", "2", "3", "4"], 0)],
          DC2_CFG,
          "Due MN3207 in controfase con compander NE570 e filtri 470p/1n8/220p (-3 dB ~22 kHz); un solo LFO a "
          "triangolo, i quattro tasti scelgono rate/profondita' (0.3 Hz x0.86, 0.3 Hz x1.42, 0.62 Hz, 0.84 Hz). "
          "Fonti: clone Aion Blueshift, schematicheaven.",
          accent=(40, 40, 90), subtitle="DIMENSION", stereo=True),

    model("ce2b", "LT-2B", "Low Tide Chorus", "BOSS CE-2B Bass Chorus", "Basso", "BBDChorus",
          BASS_BROWN,
          [rate_knob(0.32, 3.5), knob("DEPTH", "depth", 0.5), knob("E.LEVEL", "level", 0.7)],
          CE2_CFG + " wet=1",
          "Circuito del CE-2 (MN3007, clock 100-128 kHz) con condensatore d'uscita del wet da 12n: passa-alto a "
          "~282 Hz, i bassi restano non modulati; aggiunto E.LEVEL 250kC (service note CE-2/CE-2B).",
          text=(230, 210, 150), accent=(230, 210, 150), subtitle="BASS CHORUS"),

    model("dc3", "PSD-3", "Pink Space", "BOSS DC-3 Digital Dimension", "Chorus / Dimension", "BBDChorus",
          (206, 150, 196),
          [knob("E.LEVEL", "level", 0.7), knob("EQ", "tone", 0.5), rate_knob(0.1, 6), knob("DEPTH", "depth", 0.5)],
          "type=dimension stages=1024 clk=85000,130000 rate=0.1,6 lfo=0 aa=20000 comp=0 stereo=1",
          "Dimension digitale (DSP a 50 kHz, 20 Hz-20 kHz) con due linee in controfase, RATE/DEPTH continui ed EQ "
          "sul wet; approssimato con due BBD senza rumore (manuale DC-3).",
          accent=(80, 30, 80), subtitle="DIGITAL SPACE", stereo=True),

    model("ch1", "HY-1", "Hyper Chorus", "BOSS CH-1 Super Chorus", "Chorus / Dimension", "BBDChorus",
          (70, 150, 215),
          [knob("E.LEVEL", "level", 0.7), knob("EQ", "tone", 0.5), rate_knob(0.27, 4.7), knob("DEPTH", "depth", 0.5)],
          CH1_CFG,
          "MN3007+MN3101 con filtri 470p/1n8/220p (-3 dB ~22 kHz, piu' brillante del CE-2), EQ passa-basso "
          "1k+250kC su 1n2 (530 Hz-133 kHz), LFO a triangolo 0.27-4.7 Hz; uscita A mix, B dry (service manual CH-1).",
          accent=(20, 30, 80), subtitle="SUPER CHORUS", stereo=True),

    model("ce5", "BE-5", "Blue Ensemble", "BOSS CE-5 Chorus Ensemble", "Chorus / Dimension", "BBDChorus",
          (80, 150, 210),
          [knob("E.LEVEL", "level", 0.7), rate_knob(0.27, 7.1), knob("DEPTH", "depth", 0.5),
           outer("HIGH", "tone", 0.8, "hz", 1500, 12000), inner("LOW", "low", 0.2, "hz", 88, 1026)],
          "type=chorus stages=1024 clk=85000,150000 rate=0.27,7.1 lfo=0 aa=20000 comp=0 stereo=1",
          "MN3007+MN3101, filtri 470p/1n8/220p (-3 dB ~22 kHz), LFO 0.27-7.1 Hz; filtri sul wet: LOW passa-alto SVF "
          "88-1026 Hz e HIGH passa-basso del 1° ordine (service manual CE-5). Uscite A mix / B wet.",
          accent=(20, 30, 80), subtitle="CHORUS ENSEMBLE", stereo=True),

    model("ceb3", "DQ-3B", "Deep Chorus", "BOSS CEB-3 Bass Chorus", "Basso", "BBDChorus",
          (70, 145, 205),
          [knob("E.LEVEL", "level", 0.7), knob("LOW FILTER", "low", 0.4, "hz", 80, 800),
           rate_knob(0.27, 7.1), knob("DEPTH", "depth", 0.5)],
          "type=chorus stages=1024 clk=85000,150000 rate=0.27,7.1 lfo=0 aa=10000 comp=1 stereo=1",
          "Linea a IC d'eco ES56028S (sostituto del BBD con compander interno e filtri on-chip), LFO a triangolo; "
          "LOW FILTER toglie i bassi solo dal wet. Rate e clock stimati come CE-5 (schema CEB-3A, confidenza bassa).",
          accent=(20, 30, 80), subtitle="BASS CHORUS", stereo=True),

    model("ce2w", "AQ-2C", "Aqua Chorus Custom", "BOSS CE-2W Chorus Waza Craft", "Chorus / Dimension", "BBDChorus",
          AQUA,
          [rate_knob(0.32, 3.5), knob("DEPTH", "depth", 0.5),
           selector("MODE", "mode", ["STANDARD", "VINT CHO", "VINT VIB"], 0, short=["S", "CHO", "VIB"])],
          CE2_CFG + " stereo=1",
          "Riedizione analogica: modo S = CE-2 (MN3007, 85-150 kHz, LFO 0.32-3.5 Hz), modi CE-1 chorus (con "
          "profondita' variabile) e CE-1 vibrato (solo wet). Uscite A mix / B dry (manuale CE-2W).",
          accent=(20, 40, 90), subtitle="CHORUS", stereo=True),

    model("dc2w", "WS-2C", "Wide Space Custom", "BOSS DC-2W Dimension C Waza Craft", "Chorus / Dimension",
          "BBDChorus", LAVENDER,
          [selector("MODE", "mode", ["1", "2", "3", "4", "1+4", "2+4", "3+4"], 0),
           toggle("VOICE", "voice", ("S", "SDD"), 0)],
          DC2_CFG + ",0.84,0.8,0.84,1.0,0.84,0.9",
          "Come il DC-2 (due BBD in controfase, compander, LFO 0.3-0.84 Hz) con le combinazioni di tasti "
          "dell'SDD-320 (1+4, 2+4, 3+4, valori approssimati) e voce S / SDD-320 piu' brillante (manuale DC-2W).",
          accent=(40, 40, 90), subtitle="DIMENSION", stereo=True),

    model("ch1be", "HY-1T", "Hyper Chorus Target", "BOSS CH-1-BE Super Chorus (bull's eye)", "Chorus / Dimension",
          "BBDChorus", (196, 36, 44),
          [knob("E.LEVEL", "level", 0.7), knob("EQ", "tone", 0.5), rate_knob(0.27, 4.7), knob("DEPTH", "depth", 0.5)],
          CH1_CFG,
          "Edizione speciale a bersaglio del CH-1: stesso circuito MN3007+MN3101, filtri a ~22 kHz, EQ sul wet "
          "530 Hz-133 kHz, LFO 0.27-4.7 Hz (service manual CH-1).",
          text=(240, 240, 235), accent=(240, 240, 235), subtitle="SUPER CHORUS", stereo=True),

    model("md200", "ML-200", "Mod Lab", "BOSS MD-200 Modulation", "Chorus / Dimension", "BBDChorus",
          (40, 70, 165),
          [selector("MODE", "voice",
                    ["CHORUS", "VINT CHO", "FLANGER", "PHASER", "V.PHASER", "C.VIBE", "VIBRATO", "TREMOLO",
                     "ROTARY", "A.WAH", "SLICER", "OVERTONE"], 0,
                    short=["CHO", "VCH", "FLG", "PHS", "VPH", "CVB", "VIB", "TRM", "ROT", "AWH", "SLC", "OVT"]),
           rate_knob(0.1, 10), knob("DEPTH", "depth", 0.5), knob("E.LEVEL", "level", 0.7),
           knob("PARAM 1", "tone", 0.5), knob("PARAM 2", "res", 0.3), knob("PARAM 3", "manual", 0.5)],
          "type=chorus stages=1024 clk=60000,200000 rate=0.1,10 lfo=1 aa=20000 comp=0 fbmax=0.9 stereo=1",
          "Multimodulazione digitale a 12 modi con I/O stereo; qui il nucleo e' un chorus a linea modulata senza "
          "rumore, PARAM 1-3 su tono, risonanza e centro della modulazione (boss_catalog, manuale MD-200).",
          accent=(230, 230, 240), subtitle="MODULATION", stereo=True),

    model("md500", "ML-500", "Mod Lab Pro", "BOSS MD-500 Modulation", "Chorus / Dimension", "BBDChorus",
          (55, 55, 140),
          [selector("MODE", "voice",
                    ["CHORUS", "FLANGER", "PHASER", "C.VIBE", "VIBRATO", "TREMOLO", "DIMENSION", "RING MOD",
                     "ROTARY", "FILTER", "SLICER", "OVERTONE"], 0,
                    short=["CHO", "FLG", "PHS", "CVB", "VIB", "TRM", "DIM", "RING", "ROT", "FLT", "SLC", "OVT"]),
           rate_knob(0.1, 10), knob("DEPTH", "depth", 0.5), knob("E.LEVEL", "level", 0.7),
           knob("PARAM 1", "tone", 0.5), knob("PARAM 2", "res", 0.3)],
          "type=chorus stages=1024 clk=60000,200000 rate=0.1,10 lfo=1 aa=20000 comp=0 fbmax=0.9 stereo=1",
          "Multimodulazione digitale (12 modi, 28 algoritmi) con I/O stereo; approssimata con una linea modulata "
          "senza rumore, PARAM 1-2 su tono e risonanza (boss_catalog, manuale MD-500).",
          accent=(230, 230, 240), subtitle="MODULATION", stereo=True),

    # ================================================================== FLANGER
    model("bf1", "JT-1", "Jet Stream One", "BOSS BF-1 Flanger", "Flanger", "BBDFlanger",
          (180, 182, 186),
          [knob("MANUAL", "manual", 0.5), knob("DEPTH", "depth", 0.5), rate_knob(0.05, 10),
           knob("RESONANCE", "res", 0.4)],
          "type=flanger stages=1024 clk=32000,1000000 rate=0.05,10 lfo=0 aa=10000 comp=0 fbmax=0.9 sat=0.8",
          "Flanger da pavimento con SAD1024 e clock CMOS: ritardo 0.5-16 ms, sweep 100 ms-20 s (specifiche); "
          "filtri non documentati, stimati ~10 kHz (confidenza bassa).",
          accent=(20, 20, 20), subtitle="FLANGER"),

    model("bf2", "VJ-2", "Violet Jet", "BOSS BF-2 Flanger", "Flanger", "BBDFlanger",
          PURPLE,
          [knob("MANUAL", "manual", 0.5), knob("DEPTH", "depth", 0.5), rate_knob(0.0625, 10),
           knob("RES", "res", 0.4)],
          BF2_CFG,
          "MN3207 (1024 stadi) + MN3102 a clock controllato in corrente 40-500 kHz (1-12.8 ms), LFO a triangolo "
          "0.074-12 Hz, soft-clip a diodi 1N914 prima del BBD e ricostruzione del 5° ordine a ~12.8 kHz; "
          "risonanza tarata sotto l'auto-oscillazione (clone Aion Aerolith, service manual).",
          accent=(240, 230, 240), subtitle="FLANGER"),

    model("hf2", "HJ-2", "High Jet", "BOSS HF-2 Hi Band Flanger", "Flanger", "BBDFlanger",
          (200, 160, 200),
          [knob("MANUAL", "manual", 0.5), knob("DEPTH", "depth", 0.5), rate_knob(0.0625, 10),
           knob("RES", "res", 0.4)],
          HF2_CFG,
          "Scheda tipo BF-2 con MN3204 (512 stadi): ritardo 0.5-6.5 ms, LFO 100 ms-16 s; la 'banda alta' nasce "
          "dal BBD piu' corto, senza controllo di taglio (hobby-hour, specifiche).",
          accent=(70, 20, 70), subtitle="HI BAND FLANGER"),

    model("bf2b", "LJ-2B", "Low Jet", "BOSS BF-2B Bass Flanger", "Basso", "BBDFlanger",
          BASS_BROWN,
          [knob("MANUAL", "manual", 0.5), knob("DEPTH", "depth", 0.5), rate_knob(0.0625, 10),
           knob("RES", "res", 0.3)],
          HF2_CFG.replace("fbmax=0.9", "fbmax=0.75"),
          "Scheda HF-2 (MN3204, 0.5-6.5 ms) con wet filtrato passa-alto a ~194 Hz (C6 10n) e ritorno della "
          "risonanza a ~174-272 Hz, trimmer di risonanza tarato basso (specifiche, bossareaforum).",
          text=(230, 210, 150), accent=(230, 210, 150), subtitle="BASS FLANGER"),

    model("bf3", "VJ-3", "Violet Jet Digital", "BOSS BF-3 Flanger", "Flanger", "BBDFlanger",
          (100, 60, 140),
          [outer("MANUAL", "manual", 0.5), inner("RES", "res", 0.4),
           knob("DEPTH", "depth", 0.5), rate_knob(0.056, 10),
           selector("MODE", "mode", ["STANDARD", "ULTRA", "GATE/PAN", "MOMENTARY"], 0,
                    short=["STD", "ULT", "GATE", "MOM"])],
          "type=flanger stages=1024 clk=35500,1700000 rate=0.056,10 lfo=0 aa=20000 comp=0 fbmax=0.95 stereo=1",
          "Flanger digitale: 0.3-14.4 ms (ingresso chitarra), LFO 100 ms-18 s, modi Standard/Ultra/Gate-Pan/"
          "Momentary; con DEPTH al massimo MANUAL non agisce (manuale BF-3).",
          accent=(240, 230, 240), subtitle="FLANGER", stereo=True),

    # ================================================================== PHASER
    model("ph1", "GS-1", "Green Swirl", "BOSS PH-1 Phaser", "Phaser", "Phaser",
          GREEN,
          [rate_knob(0.0625, 10), knob("DEPTH", "depth", 0.7)],
          "stages=4 f=159,8000 rate=0.0625,10 fbmax=0 lfo=0 depth=1 resfix=0",
          "Quattro all-pass a JFET 2SK30A (10k/10n, 100k su D-S: 159 Hz-8 kHz), 720°, LFO 16 s-100 ms, senza "
          "risonanza (specifiche PH-1, nucleo del PH-1R).",
          accent=(20, 40, 20), subtitle="PHASER"),

    model("ph1r", "GS-1R", "Green Swirl Res", "BOSS PH-1R Phaser", "Phaser", "Phaser",
          (48, 138, 74),
          [rate_knob(0.067, 14.3), knob("DEPTH", "depth", 0.7), knob("RESONANCE", "res", 0.3)],
          "stages=4 f=159,8000 rate=0.067,14.3 fbmax=0.8 lfo=0 depth=1",
          "Quattro all-pass a JFET accoppiati (159 Hz-8 kHz), LFO di rilassamento 0.067-14 Hz a triangolo "
          "esponenziale, risonanza passa-alto ~82 Hz con guadagno d'anello max ~0.8 (clone Aion Emerald).",
          accent=(20, 40, 20), subtitle="PHASER"),

    model("ph2", "GS-2", "Super Swirl", "BOSS PH-2 Super Phaser", "Phaser", "Phaser",
          (40, 140, 70),
          [rate_knob(0.071, 10), knob("DEPTH", "depth", 0.7), knob("RES", "res", 0.3),
           toggle("MODE", "mode", ("I", "II"), 0)],
          "stages=12,12 f=100,6000 rate=0.071,10 fbmax=0.7 lfo=0 depth=1",
          "12 stadi (2160°): 8 all-pass OTA IR3109 a controllo antilog + 4 fissi, compander NE571, LFO a triangolo "
          "14 s-100 ms; modo I sweep ampio, modo II phasing marcato (manuale PH-2, superphasermods).",
          accent=(20, 40, 20), subtitle="SUPER PHASER"),

    model("ph3", "GS-3", "Swirl Shifter", "BOSS PH-3 Phase Shifter", "Phaser", "Phaser",
          (60, 160, 90),
          [rate_knob(0.0625, 5), knob("DEPTH", "depth", 0.7), knob("RES", "res", 0.3),
           selector("STAGE", "mode", ["4", "8", "10", "12", "FALL", "RISE", "STEP"], 0,
                    short=["4", "8", "10", "12", "FALL", "RISE", "STEP"])],
          "stages=4,8,10,12,12,12,12 f=100,6000 rate=0.0625,5 fbmax=0.8 lfo=0 depth=1",
          "Phaser digitale a 4/8/10/12 stadi (12 = 2x6) piu' modi FALL/RISE (barber-pole) e STEP; con DEPTH al "
          "minimo lo sweep si ferma, tap 0.2-16 s (manuale PH-3).",
          accent=(20, 40, 20), subtitle="PHASE SHIFTER"),

    # ================================================================== TREMOLO / PAN / SLICER
    model("pn2", "SWG-2", "Stereo Swing", "BOSS PN-2 Tremolo/Pan", "Tremolo / Pan / Slicer", "Tremolo",
          (40, 170, 170),
          [rate_knob(0.07, 7.7, 0.5), knob("DEPTH", "depth", 0.7),
           selector("MODE", "mode", ["PAN TRI", "PAN SQR", "TREM TRI", "TREM SQR"], 0,
                    short=["P~", "P#", "T~", "T#"])],
          "type=pan rate=0.07,7.7",
          "Doppio VCA (probabilmente M5207 come il TR-2) pilotato in controfase: pan o tremolo con onda "
          "triangolare o quadra, periodo 130 ms-14 s (specifiche PN-2, confidenza bassa sul circuito).",
          accent=(10, 50, 50), subtitle="TREMOLO/PAN", stereo=True),

    model("tr2", "PU-2", "Pulse Wave", "BOSS TR-2 Tremolo", "Tremolo / Pan / Slicer", "Tremolo",
          (22, 92, 92),
          [knob("WAVE", "wave", 0.3), rate_knob(1.1, 11.1, 0.5), knob("DEPTH", "depth", 0.6)],
          "type=tremolo rate=1.1,11.1",
          "VCA M5207 a uscita in corrente con LFO triangolo->quadra (WAVE miscela), periodo 90-900 ms "
          "(1.1-11.1 Hz), CV livellato a ~16 Hz che arrotonda i fronti; uscita 100% wet (schema, specifiche TR-2).",
          accent=(230, 240, 235), subtitle="TREMOLO"),

    model("sl2", "BCT-2", "Beat Cutter", "BOSS SL-2 Slicer", "Tremolo / Pan / Slicer", "Tremolo",
          (30, 110, 110),
          [selector("TYPE", "mode", ["SINGLE 1", "SINGLE 2", "DUAL 1", "DUAL 2", "TREMOLO", "HARMONIC",
                                     "SFX 1", "SFX 2"], 0,
                    short=["S1", "S2", "D1", "D2", "TRM", "HRM", "FX1", "FX2"]),
           knob("VARIATION", "variation", 0.0, "dial", 1, 11),
           outer("ATTACK", "attack", 0.3), inner("DUTY", "wave", 0.5),
           outer("BALANCE", "depth", 0.8), inner("TEMPO", "rate", 0.4, "dial", 40, 300)],
          "type=slicer rate=0.667,5",
          "Slicer ritmico digitale a pattern (16 passi per battuta), tempo 40-300 BPM con tap, DUTY sulla "
          "lunghezza delle fette e BALANCE dry/wet (manuale SL-2).",
          accent=(230, 240, 235), subtitle="SLICER", stereo=True),

    model("sl20", "BCT-20", "Beat Cutter Station", "BOSS SL-20 Slicer", "Tremolo / Pan / Slicer", "Tremolo",
          BLACK,
          [knob("E.LEVEL", "level", 0.7), knob("D.LEVEL", "direct", 0.0),
           selector("BANK", "bank", ["1", "2", "3", "4", "5"], 0),
           selector("PATTERN", "mode", [str(i) for i in range(1, 13)], 0),
           knob("ATTACK", "attack", 0.3), knob("DUTY", "wave", 0.5),
           knob("TEMPO", "rate", 0.4, "dial", 30, 250)],
          "type=slicer rate=0.5,4.17",
          "Processore di pattern audio: 50 pattern da una battuta (qui i 12 principali), tempo 30-250 BPM e "
          "MIDI clock, livelli effetto/diretto separati, modi d'uscita stereo (manuale SL-20).",
          accent=(40, 200, 200), subtitle="SLICER", stereo=True),

    # ================================================================== VIBRATO / ROTARY
    model("rt2", "SPN-2", "Spin Cabinet", "BOSS RT-2 Rotary Ensemble", "Vibrato / Rotary", "Tremolo",
          (222, 216, 200),
          [outer("FAST", "fast", 0.5), inner("SLOW", "slow", 0.5),
           outer("LEVEL", "level", 0.5), inner("DRIVE", "drive", 0.3),
           selector("MODE", "voice", ["I", "II", "III"], 0),
           toggle("SPEED", "mode", ("SLOW", "FAST"), 0)],
          "type=rotary rate=0.7,6.7",
          "Simulatore digitale di altoparlante rotante: corno e tamburo con accelerazioni diverse "
          "(~0.8/6.7 Hz), Doppler e crossover, drive e tre voci (boss_catalog, manuale RT-2).",
          accent=(120, 30, 30), subtitle="ROTARY ENSEMBLE", stereo=True),

    model("rt20", "SPN-20", "Spin Station", "BOSS RT-20 Rotary Sound Processor", "Vibrato / Rotary", "Tremolo",
          (122, 26, 36),
          [selector("MODE", "voice", ["I", "II", "III", "IV"], 0),
           knob("RISE TIME", "rise", 0.5), knob("E.LEVEL", "level", 0.7), knob("D.LEVEL", "direct", 0.0),
           knob("BALANCE", "balance", 0.5), knob("OVERDRIVE", "drive", 0.2),
           knob("SLOW", "slow", 0.5), knob("FAST", "fast", 0.5),
           toggle("SPEED", "mode", ("SLOW", "FAST"), 0)],
          "type=rotary rate=0.7,6.7",
          "Rotary COSM (DSP a 44.1 kHz): modi I Leslie 122 + OD, II microfoni vicini, III 122 + stack "
          "britannico, IV vibe; BALANCE corno/rotore, RISE TIME del cambio velocita' (manuale RT-20).",
          accent=(240, 220, 200), subtitle="ROTARY", stereo=True),

    model("vb2", "WB-2", "Pitch Wobble", "BOSS VB-2 Vibrato", "Vibrato / Rotary", "BBDChorus",
          (40, 180, 200),
          [rate_knob(2, 15, 0.3), knob("DEPTH", "depth", 0.5), knob("RISE TIME", "rise", 0.3, "ms", 150, 5000),
           selector("MODE", "latch", ["UNLATCH", "BYPASS", "LATCH"], 1, short=["UNL", "BYP", "LAT"])],
          VB2_CFG,
          "MN3207 + MN3102 attorno a 4 ms (clock centrale ~128 kHz), uscita 100% wet, LFO sinusoidale a T "
          "ponte 2-15 Hz, RISE TIME 150 ms-5 s con OTA (specifiche, schema stompboxelectronics).",
          accent=(10, 40, 60), subtitle="VIBRATO"),

    model("vb2w", "WB-2C", "Pitch Wobble Custom", "BOSS VB-2W Vibrato Waza Craft", "Vibrato / Rotary", "BBDChorus",
          (40, 180, 200),
          [rate_knob(2, 15, 0.3), knob("DEPTH", "depth", 0.5), knob("RISE TIME", "rise", 0.3, "ms", 150, 5000),
           selector("MODE", "latch", ["UNLATCH", "BYPASS", "LATCH"], 1, short=["UNL", "BYP", "LAT"]),
           toggle("VOICE", "voice", ("S", "C"), 0)],
          VB2_CFG,
          "Riedizione a BBD del VB-2: S = originale (4 ms, LFO sinusoidale 2-15 Hz, rise 150 ms-5 s), C = "
          "modulazione piu' marcata e risposta diversa; ingresso DEPTH per pedale (manuale VB-2W).",
          accent=(10, 40, 60), subtitle="VIBRATO"),

    # ================================================================== WAH / FILTRI
    model("pw1", "RKW-1", "Rock Sweep", "BOSS PW-1 Rocker Wah", "Wah / Filtri", "Wah",
          (35, 35, 37),
          [knob("PEDAL", "freq", 0.5)],
          "type=pedal f=400,2200 q=4,6 filt=bp",
          "Wah a pedale analogico a filtro risonante passa-banda con escursione ~400 Hz-2.2 kHz; interruttore "
          "sotto il pedale (boss_catalog; valori tipici dei wah a induttore, stimati).",
          accent=(200, 200, 205), subtitle="WAH", style="treadle", look=dict(boss=True)),

    model("fw3", "FSW-3", "Foot Sweep", "BOSS FW-3 Foot Wah", "Wah / Filtri", "Wah",
          (28, 28, 30),
          [knob("PEDAL", "freq", 0.5), knob("PEAK", "peak", 0.5)],
          "type=pedal f=380,2400 q=2,10 filt=bp",
          "Wah a pedale compatto con on/off a pressione in punta e PEAK sulla risonanza del passa-banda; "
          "escursione ~380 Hz-2.4 kHz (boss_catalog, valori stimati).",
          accent=(200, 200, 205), subtitle="WAH", style="treadle", look=dict(boss=True)),

    model("tw1", "TQ-1", "Touch Quack", "BOSS TW-1 Touch Wah", "Wah / Filtri", "Wah",
          (234, 220, 150),
          [knob("SENS", "sens", 0.5), knob("PEAK", "peak", 0.5), toggle("DRIVE", "mode", ("UP", "DOWN"), 0)],
          "type=touch f=300,2500 q=2,10 filt=bp attack=1 release=27",
          "Passa-banda risonante accordato da fotoaccoppiatore LED/CdS (picco tarato 1.3-1.65 kHz), inviluppo a "
          "semionda con attacco ~1 ms e rilascio ~27 ms, DRIVE su/giu' (schema TW-1).",
          accent=(40, 30, 10), subtitle="TOUCH WAH"),

    model("ft2", "DYF-2", "Dyna Filter", "BOSS FT-2 Dynamic Filter", "Wah / Filtri", "Wah",
          (230, 214, 160),
          [knob("SENS", "sens", 0.5), knob("CUTOFF", "freq", 0.3), knob("Q", "peak", 0.5),
           toggle("MODE", "mode", ("UP", "DOWN"), 0)],
          "type=touch f=150,4000 q=0.7,8 filt=bp attack=2 release=22",
          "Filtro a variabili di stato con VCA M5207 e integratori da 1n5, inviluppo a doppia semionda "
          "(tau ~22 ms) seguito da un Sallen-Key a 10.7 Hz, direzione UP/DOWN (schema FT-2).",
          accent=(40, 30, 10), subtitle="DYNAMIC FILTER"),

    model("aw2", "AQK-2", "Auto Quack", "BOSS AW-2 Auto Wah", "Wah / Filtri", "Wah",
          (232, 218, 165),
          [rate_knob(0.2, 9, 0.3), knob("DEPTH", "depth", 0.6), knob("MANUAL", "freq", 0.3),
           knob("SENS", "sens", 0.4)],
          "type=lfo f=300,2500 q=2,8 filt=bp attack=1 release=68 rate=0.2,9",
          "Passa-banda a fotoaccoppiatore come il TW-1, pilotato da LFO triangolo->seno 0.2-9 Hz sommato "
          "all'inviluppo (attacco 1 ms, rilascio 68 ms) e al MANUAL (schema AW-2).",
          accent=(40, 30, 10), subtitle="AUTO WAH"),

    model("aw3", "DQK-3", "Dyna Quack", "BOSS AW-3 Dynamic Wah", "Wah / Filtri", "Wah",
          (216, 210, 190),
          [knob("DECAY", "decay", 0.5), knob("MANUAL", "freq", 0.3), knob("SENS", "sens", 0.5),
           selector("MODE", "mode", ["SHARP", "DOWN", "UP", "HUMANIZER", "TEMPO"], 2,
                    short=["SHP", "DN", "UP", "HUM", "TMP"])],
          "type=touch f=250,3000 q=2,12 filt=bp attack=2 release=150 rate=0.25,5",
          "Wah digitale a inviluppo UP/DOWN/SHARP, humanizer vocale e modo TEMPO (tap 200 ms-4 s), ingressi "
          "chitarra/basso ed EXP per uso a pedale (manuale AW-3).",
          accent=(40, 30, 10), subtitle="DYNAMIC WAH"),

    model("pw10", "MSW-10", "Model Sweep", "BOSS PW-10 V-Wah", "Wah / Filtri", "Wah",
          (30, 30, 32),
          [knob("PEDAL", "freq", 0.5),
           selector("TYPE", "voice", ["CUSTOM", "ADVANCED", "BASS MIX", "CLASSIC", "BRIT", "OPTO",
                                      "VOICE", "VIBE"], 3,
                    short=["CUS", "ADV", "BASS", "CLS", "BRIT", "OPTO", "VOIC", "VIBE"]),
           knob("RANGE", "range", 0.5), knob("DRIVE", "drive", 0.2)],
          "type=pedal f=300,2500 q=3,8 filt=bp",
          "Wah COSM multimodello (wah classici, voce, vibe) con WAH RANGE e DRIVE, toe switch e memorie "
          "(boss_catalog, manuale PW-10); approssimato con un passa-banda risonante.",
          accent=(200, 200, 205), subtitle="V-WAH", style="treadle", look=dict(boss=True)),

    model("pw3", "CSW-3", "Classic Sweep", "BOSS PW-3 Wah", "Wah / Filtri", "Wah",
          (28, 28, 30),
          [knob("PEDAL", "freq", 0.5), toggle("TONE", "peak", ("RICH", "VINT"), 0)],
          "type=pedal f=350,2300 q=3.5,7 filt=bp",
          "Wah analogico a pedale con due timbriche: RICH (picco piu' largo e corposo) e VINTAGE (picco stretto "
          "tipo wah classico); escursione ~350 Hz-2.3 kHz (boss_catalog, valori stimati).",
          accent=(200, 200, 205), subtitle="WAH", style="treadle", look=dict(boss=True)),
]
