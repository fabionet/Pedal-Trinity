from catalog import model, knob, outer, inner, selector, toggle, slider
"""
Pedal Trinity - dinamica, EQ, acustici, ampli/IR, utility, accordatori e volumi.
Fonti: docs/research/circuits_dynamics_eq_utility.json/.md (service notes CS-1/2/3, LM-2, gyrator GE-7/GE-7B,
routing AB-2/LS-2, TU-2/TU-3), docs/research/digital_delay_reverb_amp.json (FBM-1, FDR-1, IR-2, IR-200:
stack Bassman 5F6-A e Deluxe AB763), docs/research/boss_catalog.json (comandi e colori).

Nota di progetto: i comandi che la famiglia DSP non puo' riprodurre (riverbero/chorus/vibrato dei
processori acustici e degli ampli digitali) sono omessi dal pannello e segnalati nelle note: nel
plugin si ottengono mettendo in catena i modelli Reverb / BBDChorus / Tremolo dedicati.
"""


def sw(i, vals):
    """sw(i,v0,v1,...) per la netlist: valori per posizione del selettore i."""
    return "sw(%d,%s)" % (i, ",".join(str(v) for v in vals))


def _fmv(i, stacks, t, m, l):
    """Stack FMV con valori per tipo di ampli (stacks = lista di tuple R1 R2 R3 R4 C1 C2 C3)."""
    names = ("R1", "R2", "R3", "R4", "C1", "C2", "C3")
    return "fmv " + " ".join("%s=%s" % (n, sw(i, [s[k] for s in stacks])) for k, n in enumerate(names)) + \
        " t=%s m=%s l=%s" % (t, m, l)


def _cab(i, cabs):
    """Filtri di cabinet: hpf2 (risposta del mobile), risonanza del cono, break-up, lpf2 (limite del cono)."""
    return ("hpf2 f=%s Q=0.8; peak f=%s Q=1.4 g=4; peak f=%s Q=2 g=%s; lpf2 f=%s Q=0.7"
            % (sw(i, [c[0] for c in cabs]), sw(i, [c[1] for c in cabs]), sw(i, [c[2] for c in cabs]),
               sw(i, [c[3] for c in cabs]), sw(i, [c[4] for c in cabs])))


# stack passivi (R1 treble, R2 bass, R3 mid, R4 slope, C1 treble, C2 bass, C3 mid)
BLACKFACE = ("250k", "250k", "10k", "100k", "250p", "100n", "47n")      # AB763 (Twin, mid 10k)
BASSMAN = ("250k", "1M", "25k", "56k", "250p", "20n", "20n")            # 5F6-A
CLASSA = ("1M", "1M", "10k", "100k", "50p", "22n", "22n")               # top boost (approssimato a FMV)
BRIT = ("220k", "1M", "22k", "33k", "470p", "22n", "22n")               # plexi / JCM
SLO = ("250k", "1M", "25k", "47k", "470p", "22n", "22n")                # lead americano a 3 stadi
RECT = ("250k", "250k", "25k", "47k", "500p", "22n", "22n")             # moderno a raddrizzatore

# preamplificatore a 3 triodi comune a IR-2 / IR-200 (i = indice del selettore AMP, g = indice del GAIN)
def _preamp(i, g, hpc, g1, g2, rk3, lpc):
    return ("hpf R=470k C=%s; triode Rp=100k Rk=1.5k B=300 byp=1; gain a=taper(%d,A)*%s; "
            "triode Rp=100k Rk=1.5k B=300 byp=1; hpf R=470k C=22n; lpf R=100k C=%s; gain a=%s; "
            "triode Rp=100k Rk=%s B=300 byp=1"
            % (sw(i, hpc), g, sw(i, g1), sw(i, lpc), sw(i, g2), sw(i, rk3)))


# ------------------------------------------------------------------ IR-2: 11 coppie ampli/cabinet
IR2_TYPES = ["CLEAN", "AMERICAN", "TWEED", "CLASS A", "CRUNCH", "BRIT", "HI-GAIN", "SUPER LEAD", "BROWN",
             "MODDED", "MODERN"]
IR2_SHORT = ["CLN", "AMER", "TWD", "CLSA", "CRN", "BRIT", "HIGN", "SLD", "BRWN", "MOD", "MDRN"]
IR2_HPC = ["47n", "47n", "22n", "22n", "22n", "10n", "4.7n", "4.7n", "6.8n", "3.3n", "3.3n"]
IR2_G1 = [0.08, 0.06, 0.3, 0.25, 0.25, 0.35, 1, 1, 1, 1, 1]
IR2_G2 = [0.02, 0.02, 0.03, 0.04, 0.03, 0.04, 0.12, 0.15, 0.1, 0.2, 0.2]
IR2_RK3 = ["1.5k", "1.5k", "1.5k", "1.5k", "1.5k", "1.5k", "2.7k", "10k", "2.7k", "10k", "10k"]
IR2_LPC = ["47p", "47p", "100p", "100p", "220p", "220p", "470p", "470p", "470p", "1n", "470p"]
IR2_STACK = [BLACKFACE, BLACKFACE, BASSMAN, CLASSA, BRIT, BRIT, SLO, SLO, BRIT, BRIT, RECT]
IR2_TRIM = [0.36, 0.47, 0.063, 0.056, 0.075, 0.04, 0.035, 0.028, 0.035, 0.028, 0.028]
IR2_SAG = [0.1, 0.15, 0.35, 0.3, 0.2, 0.25, 0.15, 0.15, 0.2, 0.15, 0.25]
#          hp  res  bu    bug lp
IR2_CAB = [(75, 95, 2500, 3, 5500),    # 1x12 chiuso
           (70, 95, 2200, 4, 5000),    # 2x12 aperto
           (60, 80, 2000, 4, 4800),    # 4x10 (G10 Gold, 80 Hz)
           (75, 85, 2600, 6, 5500),    # 2x12 aperto (cono "blue")
           (70, 85, 2400, 5, 5200),    # 2x12 aperto G12-65
           (60, 75, 2000, 5, 4800),    # 4x12 chiuso G12M
           (60, 75, 2200, 5, 5000),    # 4x12 chiuso Creamback
           (60, 75, 2600, 6, 5000),    # 4x12 chiuso V30
           (60, 75, 2000, 5, 4800),    # 4x12 chiuso G12M
           (60, 80, 2400, 4, 5500),    # 4x12 chiuso G12K-100
           (60, 75, 2600, 6, 5000)]    # 4x12 chiuso V30

IR2_NET = "; ".join([
    _preamp(0, 1, IR2_HPC, IR2_G1, IR2_G2, IR2_RK3, IR2_LPC),
    _fmv(0, IR2_STACK, "taper(2,B)", "taper(4,B)", "taper(5,A)"),
    "gain a=" + sw(0, IR2_TRIM),
    "power V=1 g=1 sag=" + sw(0, IR2_SAG),
    _cab(0, IR2_CAB),
    "vol a=taper(3,A)*3"])

# ------------------------------------------------------------------ IR-200: 11 ampli x 10 cabinet
IR200_TYPES = ["NATURAL", "SS CLEAN", "BLACKFACE", "CLASS A", "TWEED", "X-HI GAIN", "BRIT STACK", "UB METAL",
               "NAT BASS", "XDRV BASS", "CONCERT"]
IR200_SHORT = ["NAT", "SS", "BLK", "CLSA", "TWD", "XHG", "BRIT", "MTL", "NB", "XB", "CONC"]
IR200_HPC = ["47n", "47n", "47n", "22n", "22n", "3.3n", "10n", "3.3n", "100n", "22n", "100n"]
IR200_G1 = [0.08, 0.03, 0.06, 0.25, 0.3, 1, 0.35, 1, 0.08, 0.5, 0.1]
IR200_G2 = [0.02, 0.01, 0.02, 0.04, 0.03, 0.2, 0.04, 0.2, 0.02, 0.05, 0.02]
IR200_RK3 = ["1.5k", "1.5k", "1.5k", "1.5k", "1.5k", "10k", "1.5k", "10k", "1.5k", "1.5k", "1.5k"]
IR200_LPC = ["47p", "47p", "47p", "100p", "100p", "470p", "220p", "1n", "47p", "220p", "47p"]
IR200_STACK = [BLACKFACE, BASSMAN, BLACKFACE, CLASSA, BASSMAN, SLO, BRIT, RECT, BASSMAN, BRIT, BASSMAN]
IR200_TRIM = [0.36, 1.9, 0.47, 0.056, 0.063, 0.028, 0.04, 0.028, 0.36, 0.025, 0.33]
IR200_SAG = [0.1, 0.0, 0.15, 0.3, 0.35, 0.15, 0.25, 0.2, 0.1, 0.2, 0.1]
IR200_CABS = ["1x12 OPEN", "2x12 SS", "2x12 C12K", "2x12 G12M", "4x10 P10R", "4x12 G12M", "4x12 T75",
              "1x18 BASS", "2x15 BASS", "8x10 BASS"]
IR200_CSHORT = ["112", "SS", "C12K", "G12M", "410", "412M", "T75", "118", "215", "810"]
IR200_CAB = [(75, 100, 2400, 4, 5200), (70, 95, 2600, 3, 6000), (70, 95, 2200, 5, 5000), (65, 85, 2000, 5, 4800),
             (60, 90, 2000, 4, 4800), (60, 75, 2000, 5, 4800), (60, 80, 2800, 6, 5200), (35, 45, 1500, 2, 3500),
             (40, 55, 1600, 2, 3800), (45, 70, 1800, 3, 4500)]

IR200_NET = "; ".join([
    _preamp(0, 2, IR200_HPC, IR200_G1, IR200_G2, IR200_RK3, IR200_LPC),
    _fmv(0, IR200_STACK, "taper(5,B)", "taper(4,B)", "taper(3,A)"),
    "gain a=" + sw(0, IR200_TRIM),
    "power V=1 g=1 sag=" + sw(0, IR200_SAG),
    _cab(1, IR200_CAB),
    "vol a=taper(6,A)*3"])

# ------------------------------------------------------------------ volumi (famiglia Volume)
# colori come il pedale di riferimento (stessa forma della replica, vedi shaped_layout.as_shaped)
VOL_COLOUR = {"pv1": (185, 187, 189), "fv50h": (24, 24, 26), "fv50l": (24, 24, 26), "fv300h": (42, 42, 44),
              "fv300l": (42, 42, 44), "fv500h": (154, 156, 159), "fv500l": (154, 156, 159), "fv30h": (106, 108, 112),
              "fv30l": (106, 108, 112), "fv60": (24, 24, 26)}


def _vol(id, code, name, inspired, notes, minimum=True, stereo=False, exp=False):
    ctl = [knob("EXP" if exp else "VOLUME", "volume", 1.0, "percent", 0, 100)]
    if minimum:
        ctl.append(knob("MIN VOL", "min", 0.0, "percent", 0, 100))
    return model(id, code, name, inspired, "Utility / Routing", "Volume", VOL_COLOUR.get(id, (30, 30, 33)), ctl, "", notes,
                 accent=(200, 200, 200), subtitle="EXPRESSION" if exp else "FOOT VOLUME", stereo=stereo,
                 style="treadle", look=dict(boss=True))


# ------------------------------------------------------------------ accordatori (famiglia Tuner)
def _tuner(id, code, name, inspired, colour, notes, mute=1, subtitle="CHROMATIC TUNER"):
    return model(id, code, name, inspired, "Tuner / Looper", "Tuner", colour,
                 [toggle("OUTPUT", "mode", ("THRU", "MUTE"), mute),
                  knob("PITCH", "ref", 0.444, "hz", 436, 445)],
                 "", notes, subtitle=subtitle)


EQ_BASS_BROWN = (72, 52, 40)
EQ_BASS_GOLD = (226, 200, 140)

MODELS = [
    # ================================================================== EQUALIZZATORI
    model("ge10", "GQ-10", "Studio Graphic 10", "BOSS GE-10", "Equalizzatori", "GraphicEQ", (190, 192, 196),
          [slider("31", "b0", 0.5, -12, 12), slider("62", "b1", 0.5, -12, 12), slider("125", "b2", 0.5, -12, 12),
           slider("250", "b3", 0.5, -12, 12), slider("500", "b4", 0.5, -12, 12), slider("1k", "b5", 0.5, -12, 12),
           slider("2k", "b6", 0.5, -12, 12), slider("4k", "b7", 0.5, -12, 12), slider("8k", "b8", 0.5, -12, 12),
           slider("16k", "b9", 0.5, -12, 12), slider("LEVEL", "level")],
          "freqs=31.25,62.5,125,250,500,1000,2000,4000,8000,16000 qs=1.8,1.8,1.8,1.8,1.8,1.8,1.8,1.8,1.8,1.8 "
          "qmin=1.0 range=12 lvl=15",
          "EQ grafico da pavimento a 10 bande d'ottava 31.25 Hz-16 kHz, +-12 dB per banda (anteprima service notes), "
          "uscita fino a +15 dB. Q d'ottava (~1.4) che si stringe con il guadagno come nei grafici analogici.",
          accent=(20, 60, 150), subtitle="10-BAND EQUALIZER"),

    model("sp1", "SQ-1", "Spectrum Sweep", "BOSS SP-1 Spectrum", "Equalizzatori", "Circuit", (200, 50, 45),
          [knob("SPECTRUM", "freq", 0.5, "hz", 500, 5000), knob("BALANCE", "mix", 0.5)],
          """
          hpf R=220k C=47n;
          tap;
          bpf f=log(0,500,5k) Q=3;
          gain db=12;
          mix wet=taper(1,B) dry=lin(1,1,0.5)
          """,
          "Passa-banda sweepabile 500 Hz-5 kHz (pot doppio, Q ~3) sommato al segnale diretto: BALANCE dosa il "
          "filtrato rispetto al dry, senza pomello di livello. Circuito 'Spectrum' derivato dagli ampli Roland "
          "(bosszone; Q stimato).",
          accent=(245, 240, 230), subtitle="SPECTRUM"),

    model("ge6", "GQ-6", "Six Band EQ", "BOSS GE-6", "Equalizzatori", "GraphicEQ", (205, 207, 210),
          [slider("100", "b0"), slider("200", "b1"), slider("400", "b2"), slider("800", "b3"),
           slider("1.6k", "b4"), slider("3.2k", "b5")],
          "freqs=95.6,204,394,790,1549,3303 qs=3.36,3.47,3.71,4.07,3.11,3.74 qmin=0.9 range=15",
          "Predecessore del GE-7: sei bande d'ottava a gyrator 100 Hz-3.2 kHz +-15 dB, senza cursore LEVEL ne' "
          "mensola 6.4 kHz. Schema non reperito: rami gyrator del GE-7 (f0 e Q calcolati) come da dossier.",
          accent=(20, 60, 150), subtitle="GRAPHIC EQUALIZER"),

    model("ge7b", "GQ-7B", "Low End EQ", "BOSS GE-7B Bass Equalizer", "Basso", "GraphicEQ", EQ_BASS_BROWN,
          [slider("62", "b0"), slider("125", "b1"), slider("250", "b2"), slider("500", "b3"),
           slider("1k", "b4"), slider("2k", "b5"), slider("4k", "b6"), slider("LEVEL", "level")],
          "freqs=65,115,272,539,1265,2042,4180 qs=3.36,2.79,3.77,4.07,2.54,3.47,0.9 qmin=0.9 range=15 lvl=15",
          "Stessa topologia del GE-7 con rami gyrator per basso (2.2u/82n/100k -> 65 Hz ... 68n/3.3n/82k -> 2 kHz, "
          "R2 330 ohm) e mensola RC 560 ohm + 68n a 4.2 kHz. Valori dal build doc Prismatic EQ.",
          accent=EQ_BASS_GOLD, text=EQ_BASS_GOLD, subtitle="BASS EQUALIZER"),

    model("eh2", "XH-2", "Harmonic Exciter", "BOSS EH-2 Enhancer", "Equalizzatori", "Circuit", (120, 200, 190),
          [knob("MIX", "mix", 0.4), knob("FREQ", "freq", 0.5, "hz", 1000, 8000), knob("SENS", "sens", 0.5)],
          """
          hpf R=1M C=47n;
          tap;
          hpf2 f=log(1,1k,8k) Q=0.7;
          gain a=log(2,2,40);
          bjt g=1 vp=0.25 vn=0.25 soft=2 inv=0;
          hpf2 f=log(1,1k,8k) Q=0.7;
          mix wet=lin(0,0,1) dry=1
          """,
          "Exciter: la banda alta (passa-alto FREQ 1-8 kHz) viene saturata (SENS) per generare armoniche, filtrata "
          "di nuovo e miscelata al segnale pulito con MIX. Nessuno schema pubblico: modello di principio.",
          accent=(20, 70, 80), subtitle="ENHANCER"),

    model("pq4", "PMQ-4", "Para Quad EQ", "BOSS PQ-4 Parametric Equalizer", "Equalizzatori", "ParametricEQ",
          (225, 223, 215),
          [outer("PRESENCE", "g3", 0.5, "db", -18, 18), inner("LOW", "g0", 0.5, "db", -18, 18),
           outer("MIDDLE", "g1", 0.5, "db", -18, 18), inner("MID FREQ", "f1", 0.5, "hz", 100, 1600),
           outer("HIGH", "g2", 0.5, "db", -18, 18), inner("HIGH FREQ", "f2", 0.5, "hz", 500, 8000),
           knob("LEVEL", "level", 0.5, "db", -18, 18)],
          "types=ls,pk,pk,hs f0=100 f1=100,1600 f2=500,8000 f3=8000 q=0.7,1.0,1.0,0.7 range=18",
          "Quattro filtri: mensola LOW <100 Hz, MIDDLE 100 Hz-1.6 kHz, HIGH 500 Hz-8 kHz, mensola PRESENCE >8 kHz, "
          "tutti +-18 dB a Q fisso, LEVEL +-18 dB (manuale PQ-4).",
          accent=(20, 60, 150), subtitle="PARAMETRIC EQUALIZER"),

    model("pq3b", "PMQ-3B", "Bass Para EQ", "BOSS PQ-3B Bass Parametric Equalizer", "Basso", "ParametricEQ",
          EQ_BASS_BROWN,
          [outer("LOW", "g0", 0.5, "db", -15, 15), inner("LOW FREQ", "f0", 0.5, "hz", 25, 400),
           outer("MIDDLE", "g1", 0.5, "db", -15, 15), inner("MID FREQ", "f1", 0.5, "hz", 160, 2500),
           outer("HIGH", "g2", 0.5, "db", -15, 15), inner("HIGH FREQ", "f2", 0.5, "hz", 1000, 16000),
           knob("LEVEL", "level", 0.5, "db", -15, 15)],
          "types=pk,pk,pk f0=25,400 f1=160,2500 f2=1000,16000 q=0.9,0.9,0.9 range=15",
          "EQ parametrico per basso a tre bande semi-parametriche (LOW 25-400 Hz, MIDDLE 160 Hz-2.5 kHz, "
          "HIGH 1-16 kHz) con pomelli concentrici livello/frequenza e LEVEL generale (catalogo BOSS).",
          accent=EQ_BASS_GOLD, text=EQ_BASS_GOLD, subtitle="BASS PARAMETRIC EQ"),

    model("geb7", "GQB-7", "Bass Graphic 7", "BOSS GEB-7 Bass Equalizer", "Basso", "GraphicEQ", (225, 225, 220),
          [slider("50", "b0"), slider("120", "b1"), slider("400", "b2"), slider("500", "b3"),
           slider("800", "b4"), slider("4.5k", "b5"), slider("10k", "b6"), slider("LEVEL", "level")],
          "freqs=50,120,400,500,800,4500,10000 qs=3.5,3.5,3.5,3.5,3.5,3.5,0.9 qmin=0.9 range=15 lvl=15",
          "Sette bande per basso 50 Hz-10 kHz +-15 dB e Level +-15 dB. Rami gyrator ricavati con Q_ramo ~3.5 "
          "(4.7u/68n/100k -> 49 Hz, 1.5u/33n/100k -> 125 Hz); 10 kHz come mensola RC (dossier, valori L).",
          accent=(20, 60, 150), subtitle="BASS EQUALIZER"),

    model("eq200", "GQ-210", "Twin Ten EQ", "BOSS EQ-200 Graphic Equalizer", "Equalizzatori", "GraphicEQ",
          (190, 192, 196),
          [slider("30", "b0"), slider("60", "b1"), slider("120", "b2"), slider("200", "b3"), slider("400", "b4"),
           slider("800", "b5"), slider("1.6k", "b6"), slider("3.2k", "b7"), slider("6.4k", "b8"),
           slider("12.8k", "b9"), slider("LEVEL", "level")],
          "freqs=30,60,120,200,400,800,1600,3200,6400,12800 qs=1.41,1.41,1.41,1.41,1.41,1.41,1.41,1.41,1.41,1.41 "
          "qmin=1.41 range=15 lvl=15",
          "EQ digitale 10 bande +-15 dB (tipo 30/800/12.8k) a Q costante, 96 kHz. Nel plugin e' un canale "
          "stereo linkato (PARA+LINK); i canali A/B separati, memorie e MIDI non sono emulati.",
          accent=(20, 60, 150), subtitle="GRAPHIC EQUALIZER", stereo=True),

    # ================================================================== ACUSTICI
    model("ac2", "AK-2", "Wooden Voice", "BOSS AC-2 Acoustic Simulator", "Acustica", "Acoustic", (205, 145, 60),
          [knob("LEVEL", "level"), knob("BODY", "body"), knob("TOP", "top"),
           toggle("MODE", "mode", ("STD", "ENH"), 0)],
          "modes=110:6:1.2|220:3:1.5|650:-3:1.0;115:5:1.3|230:3:1.5|3500:4:0.9 top=5000 cut=600",
          "Simulatore acustico per chitarra elettrica: risonanza del corpo 100-250 Hz con scavo 400-800 Hz (BODY), "
          "brillantezza 3-8 kHz (TOP), modo ENHANCE piu' brillante. Circuito non pubblico: filtri stimati (dossier L).",
          accent=(60, 30, 10), subtitle="ACOUSTIC SIMULATOR"),

    model("ac3", "AK-3", "Spruce Top", "BOSS AC-3 Acoustic Simulator", "Acustica", "Acoustic", (190, 150, 90),
          [selector("MODE", "mode", ["STANDARD", "JUMBO", "ENHANCE", "PIEZO"], 0, short=["STD", "JMB", "ENH", "PZ"]),
           knob("TOP", "top"), knob("BODY", "body"),
           outer("REVERB", "reverb", 0.3), inner("LEVEL", "level")],
          "modes=110:6:1.2|220:3:1.5|650:-3:1.0;95:7:1.1|190:4:1.4|700:-4:1.0;115:5:1.3|230:3:1.5|3500:4:0.9;"
          "120:4:1.2|250:2:1.5|1500:-3:1.2 top=5000 cut=600",
          "COSM derivato dall'AD-8: quattro modelli di corpo (Standard, Jumbo con bassi piu' profondi, Enhance "
          "brillante, Piezo), TOP e BODY, REVERB/LEVEL concentrici (specifiche BOSS).",
          accent=(60, 30, 10), subtitle="ACOUSTIC SIMULATOR"),

    model("ad2", "APR-2", "Piezo Preamp", "BOSS AD-2 Acoustic Preamp", "Acustica", "Circuit", (180, 140, 95),
          [knob("RESONANCE", "body", 0.5), knob("NOTCH", "freq", 0.0, "hz", 50, 500)],
          """
          hpf R=10M C=10n;
          hpf2 f=40 Q=0.7;
          peak f=1200 Q=0.8 g=-3;
          peak f=110 Q=1.1 g=lin(0,0,7);
          peak f=220 Q=1.6 g=lin(0,0,3);
          peak f=600 Q=1 g=lin(0,0,-3);
          hshelf f=6000 Q=0.7 g=lin(0,0,3);
          peak f=log(1,50,500) Q=6 g=-12
          """,
          "Preamp per piezo (Zin 10 Mohm): ACOUSTIC RESONANCE aggiunge corpo e calore togliendo la durezza del "
          "piezo, NOTCH anti-feedback 50-500 Hz (a fondo corsa sotto la gamma dello strumento). AMBIENCE omesso: "
          "nel plugin usare un Reverb in catena.",
          accent=(60, 30, 10), subtitle="ACOUSTIC PREAMP"),

    model("ad3", "APR-3", "Stage Acoustic", "BOSS AD-3 Acoustic Instrument Processor", "Acustica", "Circuit",
          (30, 30, 33),
          [knob("ANTI-FB", "freq", 0.0, "hz", 70, 400), knob("BOTTOM", "low", 0.5, "db", -12, 12),
           knob("TOP", "high", 0.5, "db", -12, 12)],
          """
          hpf R=1M C=100n;
          peak f=200 Q=1.2 g=3;
          lshelf f=120 Q=0.7 g=lin(1,-12,12);
          hshelf f=5000 Q=0.7 g=lin(2,-12,12);
          peak f=log(0,70,400) Q=8 g=-15
          """,
          "Processore acustico da pavimento: filtro anti-feedback a banda stretta 70-400 Hz, BOTTOM e TOP a mensola "
          "+-12 dB, leggera risonanza del corpo. 2x2 CHORUS e REVERB omessi (usare i modelli dedicati in catena).",
          accent=(210, 170, 110), subtitle="ACOUSTIC PROCESSOR", stereo=True),

    model("ad5", "APR-5", "Acoustic Desk", "BOSS AD-5 Acoustic Instrument Processor", "Acustica", "Circuit",
          (30, 30, 33),
          [knob("PREAMP", "level", 0.5),
           outer("BASS", "low", 0.5, "db", -12, 12), inner("MIDDLE", "mid", 0.5, "db", -12, 12),
           outer("TREBLE", "high", 0.5, "db", -12, 12), inner("PRESENCE", "presence", 0.5, "db", -12, 12),
           outer("AFB DEPTH", "depth", 0.0, "db", 0, -18), inner("AFB FREQ", "freq", 0.3, "hz", 50, 1000),
           knob("BODY", "body", 0.5), knob("MIC DIST", "dist", 0.3)],
          """
          hpf R=1M C=100n;
          lshelf f=100 Q=0.7 g=lin(1,-12,12);
          peak f=800 Q=0.8 g=lin(2,-12,12);
          hshelf f=3500 Q=0.7 g=lin(3,-12,12);
          hshelf f=9000 Q=0.7 g=lin(4,-12,12);
          peak f=log(6,50,1000) Q=8 g=lin(5,0,-18);
          peak f=110 Q=1.2 g=lin(7,0,8);
          peak f=220 Q=1.5 g=lin(7,0,4);
          lshelf f=200 Q=0.7 g=lin(8,2,-4);
          lpf2 f=log(8,18k,6k) Q=0.7;
          vol a=taper(0,A)*3
          """,
          "Processore da tavolo per piezo/magnetico: EQ 4 bande +-12 dB, anti-feedback con profondita' e frequenza "
          "50 Hz-1 kHz, ACOUSTIC BODY (risonanze 110/220 Hz) e MIC DISTANCE (meno prossimita' e meno alte). "
          "Chorus e riverbero omessi (modelli dedicati in catena).",
          accent=(210, 170, 110), subtitle="ACOUSTIC PROCESSOR", stereo=True),

    model("ad8", "APR-8", "Acoustic Station", "BOSS AD-8 Acoustic Guitar Processor", "Acustica", "Circuit",
          (160, 162, 166),
          [selector("BODY TYPE", "mode", ["STANDARD", "JUMBO", "SMALL", "NYLON"], 0, short=["STD", "JMB", "SML", "NYL"]),
           knob("BODY", "body", 0.5), knob("ENHANCE", "enhance", 0.3),
           knob("BASS", "low", 0.5, "db", -12, 12),
           outer("MIDDLE", "mid", 0.5, "db", -12, 12), inner("MID FREQ", "freq", 0.5, "hz", 200, 5000),
           outer("TREBLE", "high", 0.5, "db", -12, 12), inner("PRESENCE", "presence", 0.5, "db", -12, 12),
           knob("LEVEL", "level", 0.5)],
          """
          hpf R=1M C=100n;
          peak f=sw(0,110,95,140,120) Q=1.2 g=lin(1,0,8);
          peak f=sw(0,220,190,260,240) Q=1.6 g=lin(1,0,4);
          peak f=sw(0,650,700,600,900) Q=1 g=sw(0,-3,-4,-2,-4);
          peak f=4500 Q=0.8 g=lin(2,0,9);
          lshelf f=100 Q=0.7 g=lin(3,-12,12);
          peak f=log(5,200,5k) Q=1 g=lin(4,-12,12);
          hshelf f=4000 Q=0.7 g=lin(6,-12,12);
          hshelf f=9000 Q=0.7 g=lin(7,-12,12);
          vol a=taper(8,A)*3
          """,
          "COSM che trasforma il suono piezo in quello di una chitarra microfonata: tipo di corpo, BODY, STRING "
          "ENHANCE, EQ con medio parametrico 200 Hz-5 kHz e PRESENCE. Tipi di corpo approssimati; riverbero, "
          "memorie e anti-feedback automatico non emulati.",
          accent=(30, 30, 33), subtitle="ACOUSTIC PROCESSOR", stereo=True),

    model("ad10", "APR-10", "Dual Acoustic DI", "BOSS AD-10 Acoustic Preamp", "Acustica", "Circuit", (30, 30, 33),
          [knob("RESONANCE", "body", 0.5), knob("BASS", "low", 0.5, "db", -12, 12),
           outer("MIDDLE", "mid", 0.5, "db", -12, 12), inner("MID FREQ", "freq", 0.5, "hz", 200, 5000),
           knob("TREBLE", "high", 0.5, "db", -12, 12), knob("PRESENCE", "presence", 0.5, "db", -12, 12),
           knob("NOTCH", "notch", 0.0, "hz", 50, 500), knob("LEVEL", "level", 0.5)],
          """
          hpf R=10M C=10n;
          hpf2 f=40 Q=0.7;
          peak f=110 Q=1.1 g=lin(0,0,7);
          peak f=220 Q=1.6 g=lin(0,0,3);
          peak f=1200 Q=0.8 g=lin(0,0,-3);
          lshelf f=100 Q=0.7 g=lin(1,-12,12);
          peak f=log(3,200,5k) Q=1 g=lin(2,-12,12);
          hshelf f=4000 Q=0.7 g=lin(4,-12,12);
          hshelf f=9000 Q=0.7 g=lin(5,-12,12);
          peak f=log(6,50,500) Q=6 g=-12;
          vol a=taper(7,A)*3
          """,
          "Preamp/DI a due canali: ACOUSTIC RESONANCE, EQ 4 bande con medio parametrico, notch anti-feedback e "
          "livello, per un canale. Compressore, ambience, chorus, delay, tuner e looper del pedale reale sono "
          "affidati agli altri modelli del plugin.",
          accent=(210, 170, 110), subtitle="ACOUSTIC PREAMP", stereo=True),

    # ================================================================== AMPLI / IR
    model("fbm1", "TB-59", "Tweed Bass 59", "BOSS FBM-1 Fender '59 Bassman", "Amp / IR", "AmpSim", (200, 175, 120),
          [knob("LEVEL", "level", 0.5), knob("GAIN", "gain", 0.5), knob("TREBLE", "treble", 0.5),
           knob("BASS", "bass", 0.5), knob("MIDDLE", "mid", 0.5), knob("PRESENCE", "presence", 0.3),
           toggle("INPUT", "bright", ("NORM", "BRT"), 0)],
          """
          triode Rp=100k Rk=820 B=330 byp=1;
          hpf R=1M C=20n;
          hshelf f=1.5k Q=0.6 g=sw(6,0,1)*lin(1,12,0);
          vol a=taper(1,A);
          triode Rp=100k Rk=820 B=330 byp=0;
          bjt g=1 vp=45 vn=150 soft=3 inv=0;
          fmv R1=250k R2=1M R3=25k R4=56k C1=250p C2=20n C3=20n t=taper(2,B) m=taper(4,B) l=taper(3,A);
          gain db=-26;
          hshelf f=3.5k Q=0.7 g=lin(5,0,9);
          power V=1 g=1 sag=0.35;
          hpf2 f=70 Q=0.8; peak f=100 Q=1.4 g=3; peak f=2400 Q=2 g=3; lpf2 f=5500 Q=0.7;
          vol a=taper(0,A)*2
          """,
          "5F6-A: triodo 12AX7 (100k/820 ohm), volume 1M con condensatore bright (ingresso BRT), secondo triodo e "
          "cathode follower che pilota lo stack FMV reale (250k/1M/25k/56k, 250p/20n/20n), presence nella "
          "retroazione, finale con sag da raddrizzatore a valvola e voicing 4x10 (ampbooks, Yeh DAFx 2006).",
          accent=(120, 30, 30), subtitle="AMP SIMULATOR"),

    model("fdr1", "BF-65", "Blackface 65", "BOSS FDR-1 Fender '65 Deluxe Reverb", "Amp / IR", "AmpSim", (30, 30, 33),
          [knob("LEVEL", "level", 0.5), knob("GAIN", "gain", 0.4), knob("TREBLE", "treble", 0.5),
           knob("BASS", "bass", 0.5)],
          """
          triode Rp=100k Rk=1.5k B=280 byp=1;
          fmv R1=250k R2=250k R3=6.8k R4=100k C1=250p C2=100n C3=47n t=taper(2,B) m=1 l=taper(3,A);
          vol a=taper(1,A);
          triode Rp=100k Rk=1.5k B=280 byp=1;
          hpf R=1M C=47n;
          gain db=-20;
          triode Rp=100k Rk=820 B=280 byp=0;
          gain db=-34;
          power V=1 g=1 sag=0.3;
          hpf2 f=85 Q=0.8; peak f=110 Q=1.5 g=4; peak f=2500 Q=2 g=4; lpf2 f=5000 Q=0.7;
          vol a=taper(0,A)*2
          """,
          "AB763: triodo 12AX7 (100k/1.5k) che pilota dalla placca lo stack a due pomelli (250k/250k, medio fisso "
          "6.8k, slope 100k, 250p/100n/47n), volume 1M, secondo triodo, rete di miscelazione del riverbero, terzo "
          "triodo, 6V6 con sag e 1x12. REVERB e VIBRATO omessi: usare Reverb a molla e Tremolo in catena.",
          accent=(225, 225, 220), subtitle="AMP SIMULATOR"),

    model("ir2", "AIR-2", "Amp & IR Compact", "BOSS IR-2 Amp & Cabinet", "Amp / IR", "AmpSim", (45, 45, 48),
          [selector("TYPE", "type", IR2_TYPES, 0, short=IR2_SHORT),
           outer("GAIN", "gain", 0.5), inner("TREBLE", "treble", 0.5),
           outer("LEVEL", "level", 0.5), inner("MIDDLE", "mid", 0.5),
           knob("BASS", "bass", 0.5)],
          IR2_NET,
          "Undici coppie ampli/cabinet: preampli a tre triodi 12AX7 con guadagni, bias e taglio dei bassi per tipo, "
          "stack FMV reali (blackface, 5F6-A, plexi, lead a 3 stadi, moderno), finale con sag e cabinet a filtri "
          "(hp 60-75 Hz, risonanza 75-95 Hz, lp 4.8-5.5 kHz). AMBIENCE omesso; IR utente con il modello IR Loader.",
          accent=(230, 120, 40), subtitle="AMP & CABINET", stereo=True),

    model("ir200", "AIR-200", "Amp & IR Station", "BOSS IR-200 Amp & IR Cabinet", "Amp / IR", "AmpSim", (45, 45, 48),
          [selector("AMP", "type", IR200_TYPES, 0, short=IR200_SHORT),
           selector("CABINET", "cab", IR200_CABS, 0, short=IR200_CSHORT),
           knob("GAIN", "gain", 0.5), knob("BASS", "bass", 0.5), knob("MIDDLE", "mid", 0.5),
           knob("TREBLE", "treble", 0.5), knob("LEVEL", "level", 0.5)],
          IR200_NET,
          "Otto ampli per chitarra e tre per basso (preampli a tre triodi con stack FMV per tipo, finale con sag) "
          "e dieci cabinet indipendenti a filtri, da 1x12 aperto a 8x10 basso. Memorie, AMBIENCE e IR Celestion non "
          "emulati: per un IR vero caricare un .wav nel modello IR Loader.",
          accent=(230, 120, 40), subtitle="AMP & IR CABINET", stereo=True),

    model("irl1", "IRL-1", "IR Loader", "originale Pedal Trinity (IR loader)", "Amp / IR", "CabIR", (60, 62, 66),
          [selector("CAB", "cab", ["1x12 OPEN", "2x12 OPEN", "4x10 OPEN", "4x12 CLOSED", "2x12 BLUE", "1x12 CLOSED"],
                    0, short=["112", "212", "410", "412", "BLUE", "112C"]),
           knob("MIC", "mic", 0.3), knob("DIST", "dist", 0.2), knob("MIX", "mix", 1.0), knob("LEVEL", "level", 0.5)],
          "cabs=1x12open:105:1.4:5200:75:2400:4|2x12open:95:1.3:5000:70:2200:5|4x10open:80:1.2:4800:60:2000:4|"
          "4x12closed:80:1.6:4700:60:2000:6|2x12blue:85:1.4:5500:75:2600:6|1x12closed:95:1.6:5500:75:2500:3",
          "Modello originale: convolve il segnale con un IR di cabinet. Gli IR interni sono generati dal modello "
          "fisico del cono (risonanza, break-up, filtro a pettine della posizione del microfono); trascinando un "
          ".wav mono/stereo si carica l'IR dell'utente (normalizzato). MIX miscela dry/IR.",
          accent=(120, 200, 230), subtitle="IR LOADER"),

    model("irl2", "IRL-2B", "Bass IR Loader", "originale Pedal Trinity (IR loader per basso)", "Basso", "CabIR",
          (60, 62, 66),
          [selector("CAB", "cab", ["1x15", "2x10", "4x10", "8x10", "1x18"], 2, short=["115", "210", "410", "810", "118"]),
           knob("MIC", "mic", 0.3), knob("DIST", "dist", 0.2), knob("MIX", "mix", 1.0), knob("LEVEL", "level", 0.5)],
          "cabs=1x15:55:1.3:3800:40:1500:2|2x10:80:1.2:4500:55:1900:3|4x10:70:1.2:4000:45:1800:3|"
          "8x10:65:1.4:3800:45:1700:4|1x18:45:1.3:3200:32:1400:2",
          "Modello originale per basso: IR interni di cabinet da 1x15 a 8x10 e 1x18 (risonanze 45-80 Hz, "
          "limite del cono 3.2-4.5 kHz) oppure un file .wav dell'utente; MIX permette di tenere il DI pulito.",
          accent=(120, 200, 230), subtitle="BASS IR LOADER"),

    # ================================================================== DINAMICA
    model("cs1", "CX-1", "Blue Sustain", "BOSS CS-1 Compression Sustainer", "Dinamica", "Compressor", (90, 170, 220),
          [knob("LEVEL", "level", 0.5), knob("SUSTAIN", "sustain", 0.5),
           toggle("MODE", "tone", ("NORM", "TREB"), 0)],
          "type=sustainer topo=fb attack=5,20 release=400 maxgain=27 thr=-50,-15 ratio=2,10 knee=10 tone=1900",
          "Compressore ottico: fotoaccoppiatore LED/CdS nella retroazione dell'op-amp (G max ~22, 26.8 dB), "
          "attacco elettrico 0.22 ms + ritardo della CdS, rilascio ~330 ms; MODE TREBLE (0.15u+560 ohm, 1.89 kHz) "
          "contro NORMAL. Service note BOSS CS-1: 50 dB in ingresso -> 6 dB in uscita.",
          accent=(20, 40, 90), subtitle="COMPRESSION SUSTAINER"),

    model("cs2", "CX-2", "Deep Sustain", "BOSS CS-2 Compression Sustainer", "Dinamica", "Compressor", (40, 90, 190),
          [knob("LEVEL", "level", 0.5), knob("ATTACK", "release", 0.5), knob("SUSTAIN", "sustain", 0.5)],
          "type=sustainer topo=ff attack=0.5,3 releaseRange=1600,100 maxgain=38 thr=-45,-12 ratio=3,20 knee=6",
          "OTA BA662A con rivelatore 'transistor pump' a doppia semionda: SUSTAIN 1MC in serie a 27k (38:1 di "
          "corrente, ~38 dB), ATTACK 150KC + 10k su 10u = recupero 100 ms-1.6 s (CW preserva l'attacco). "
          "Service note BOSS CS-2.",
          accent=(230, 230, 240), subtitle="COMPRESSION SUSTAINER"),

    model("cs3", "CX-3", "Studio Sustain", "BOSS CS-3 Compression Sustainer", "Dinamica", "Compressor", (40, 110, 200),
          [knob("LEVEL", "level", 0.5), knob("TONE", "tone", 0.5), knob("ATTACK", "release", 0.5),
           knob("SUSTAIN", "sustain", 0.5)],
          "type=sustainer topo=fb attack=0.5,2 releaseRange=1280,103 maxgain=30 thr=-45,-10 ratio=3,12 knee=6 tone=1250",
          "VCA uPC1252H2 in retroazione con rivelatore di picco (passa-alto side-chain ~340 Hz): ATTACK 250KC + 22k "
          "su 4.7u = recupero 103 ms-1.28 s, SUSTAIN limita il guadagno massimo, TONE a mensola ~1.25 kHz +-10 dB. "
          "Service note BOSS CS-3.",
          accent=(230, 230, 240), subtitle="COMPRESSION SUSTAINER"),

    model("bc1x", "BCX-1", "Bass Squeeze", "BOSS BC-1X Bass Comp", "Basso", "Compressor", (20, 110, 120),
          [knob("LEVEL", "level", 0.6), knob("RATIO", "ratio", 0.4), knob("RELEASE", "release", 0.4),
           knob("THRESHOLD", "threshold", 0.5)],
          "type=limiter topo=ff attack=2,2 releaseRange=30,1500 thr=0,-40 ratio=1.5,20 knee=8",
          "Compressore digitale MDP per basso (18 V interni): THRESHOLD, RATIO e RELEASE continui, LEVEL di "
          "compensazione. L'analisi multibanda MDP non e' pubblicata: qui a banda unica con ginocchio morbido.",
          accent=(230, 230, 230), subtitle="BASS COMP"),

    model("cp1x", "MCX-1", "Multi Comp X", "BOSS CP-1X Compressor", "Dinamica", "Compressor", (40, 100, 190),
          [knob("LEVEL", "level", 0.6), knob("ATTACK", "attack", 0.5, "ms", 0.5, 50), knob("RATIO", "ratio", 0.4),
           knob("COMP", "threshold", 0.5)],
          "type=limiter topo=ff attack=0.5,50 release=150 thr=0,-40 ratio=1.5,20 knee=8",
          "Compressore digitale MDP a 18 V: COMP abbassa la soglia, RATIO e ATTACK continui, LEVEL di uscita. "
          "Analisi multibanda non documentata (specifiche BOSS): modello a banda unica a ginocchio morbido.",
          accent=(230, 230, 240), subtitle="COMPRESSOR"),

    model("lm2", "LX-2", "Peak Limiter", "BOSS LM-2 Limiter", "Dinamica", "Compressor", (40, 170, 180),
          [knob("LEVEL", "level", 0.5), knob("TONE", "tone", 0.5), knob("RELEASE", "release", 0.4),
           knob("THRESHOLD", "threshold", 0.5)],
          "type=limiter topo=ff attack=0.5,1 releaseRange=22,1020 thr=-10,-40 ratio=10,20 knee=3 tone=6000",
          "Limiter VCA uPC1252H2 feed-forward: rivelatore log-amp + raddrizzatore di precisione + peak hold su 1u "
          "(rilascio 22k + RELEASE 1MA = 22 ms-1.02 s), soglia con azione solo sopra THRESHOLD, ratio >=10:1, "
          "TONE ~6 kHz. Service note BOSS LM-2.",
          accent=(20, 50, 60), subtitle="LIMITER"),

    model("lm2b", "LXB-2", "Bass Limiter", "BOSS LM-2B Bass Limiter", "Basso", "Compressor", (110, 95, 55),
          [knob("ENHANCE", "enhance", 0.3), knob("LEVEL", "level", 0.5), knob("THRESHOLD", "threshold", 0.5)],
          "type=limiter topo=ff attack=0.5,1 release=200 thr=-10,-40 ratio=10,10 knee=3 enhance=2000",
          "Versione per basso del limiter VCA LM-2: soglia regolabile, rapporto fisso alto, rilascio medio e "
          "ENHANCE che esalta la presenza delle corde (campana ~2 kHz, stimata).",
          accent=EQ_BASS_GOLD, text=EQ_BASS_GOLD, subtitle="BASS LIMITER"),

    model("lmb3", "LXB-3", "Bass Limit Enhance", "BOSS LMB-3 Bass Limiter Enhancer", "Basso", "Compressor",
          (80, 170, 210),
          [knob("LEVEL", "level", 0.5), knob("ENHANCE", "enhance", 0.3), knob("RATIO", "ratio", 0.5),
           knob("THRESHOLD", "threshold", 0.5)],
          "type=limiter topo=ff attack=1,1 release=150 thr=-5,-40 ratio=1,20 knee=6 enhance=2000",
          "Limiter per basso con RATIO continuo da 1:1 a limitazione (manuale: 2:1 -> +6 dB sopra soglia diventano "
          "+3 dB), THRESHOLD, ENHANCE armonico e LEVEL. Nessuno schema pubblico: costanti di tempo stimate.",
          accent=(20, 50, 80), subtitle="BASS LIMITER ENHANCER"),

    model("nf1", "NG-1", "Hush Gate", "BOSS NF-1 Noise Gate", "Dinamica", "NoiseGate", (200, 202, 205),
          [knob("SENS", "threshold", 0.5), knob("DECAY", "decay", 0.4)],
          "thr=-40,-85 decay=5,1000",
          "Noise gate analogico: SENS abbassa la soglia di apertura (piu' sensibile = si apre con segnali piu' "
          "deboli), DECAY regola la chiusura. Il suo rivelatore e' la base dello SG-1.",
          accent=(30, 30, 33), subtitle="NOISE GATE"),

    model("ns2", "HG-2", "Hush Suppressor", "BOSS NS-2 Noise Suppressor", "Dinamica", "NoiseGate", (228, 228, 224),
          [knob("THRESHOLD", "threshold", 0.4), knob("DECAY", "decay", 0.4),
           selector("MODE", "mode", ["REDUCTION", "MUTE"], 0, short=["RED", "MUTE"])],
          "thr=-80,-20 decay=30,2000",
          "Soppressore a VCA con rivelatore d'inviluppo veloce (attacco ~1 ms, decadimento 30 ms-2 s); MODE MUTE "
          "silenzia l'uscita per accordare. Il loop SEND/RETURN non e' emulato: il gate agisce sul segnale che "
          "attraversa il modello (manuale NS-2).",
          accent=(30, 30, 33), subtitle="NOISE SUPPRESSOR"),

    model("ns1x", "HG-1X", "Hush Suppressor X", "BOSS NS-1X Noise Suppressor", "Dinamica", "NoiseGate",
          (190, 192, 196),
          [knob("THRESHOLD", "threshold", 0.4), knob("DECAY", "decay", 0.4),
           selector("MODE", "mode", ["REDUCTION", "MUTE"], 0, short=["RED", "MUTE"])],
          "thr=-85,-25 decay=10,1500",
          "Soppressore digitale MDP (48 kHz, 32 bit float) con THRESHOLD, DECAY e modo MUTE. DAMP (profondita' "
          "dell'attenuazione) e il modo GATE sono riassunti nel gate del plugin, che chiude completamente.",
          accent=(30, 30, 33), subtitle="NOISE SUPPRESSOR"),

    model("sg1", "SWL-1", "Violin Swell", "BOSS SG-1 Slow Gear", "Dinamica", "SlowGear", (30, 30, 33),
          [knob("SENS", "sens", 0.5), knob("ATTACK", "attack", 0.5)],
          "attack=300,2000",
          "Attack-delay a JFET 2SK30A in serie al segnale: un rivelatore da +68 dB fa ripartire da zero la rampa a "
          "ogni nota (10u + 47n ricaricati via 10k + ATTACK 20kB, tau 100-400 ms, fade-in 0.3-2 s). "
          "Service note SG-1 e replica GGG.",
          accent=(230, 230, 230), subtitle="SLOW GEAR"),

    # ================================================================== ACCORDATORI
    _tuner("tu2", "TN-2", "Stage Tuner", "BOSS TU-2 Chromatic Tuner", (215, 215, 210),
           "Accordatore cromatico C0-C8 (+-3 cent), riferimento 438-445 Hz, OUTPUT mutata ad accordatore acceso e "
           "BYPASS sempre passante: nel plugin la levetta sceglie mute o passante, PITCH il La di riferimento."),
    _tuner("tu3", "TN-3", "Chromatic Tuner", "BOSS TU-3 Chromatic Tuner", (30, 30, 33),
           "Rilevamento C0-C8 con precisione +-1 cent, riferimento 436-445 Hz (default 440), meter a 21 LED "
           "cent/stream. OUTPUT mutata a tuner acceso; con la levetta THRU il segnale passa come dal jack BYPASS."),
    _tuner("tu3s", "TN-3S", "Tuner Switchless", "BOSS TU-3S Chromatic Tuner", (30, 30, 33),
           "Versione senza interruttore per pedaliere con switcher: stesso rilevamento del TU-3, uscita sempre "
           "passante (levetta su THRU di fabbrica).", mute=0),
    _tuner("tu3w", "TN-3W", "Tuner Craft", "BOSS TU-3W Chromatic Tuner (Waza Craft)", (30, 30, 33),
           "TU-3 costruito a mano con switch buffer/true bypass (irrilevante nel plugin, dove il segnale e' "
           "digitale): rilevamento C0-C8, riferimento 436-445 Hz, uscita mutata in accordatura."),
    _tuner("tu1000", "TN-1000", "Big Stage Tuner", "BOSS TU-1000 Stage Tuner", (30, 30, 33),
           "Accordatore da palco con meter grande e uscita parallela: nel plugin mute o passante con la levetta e "
           "riferimento regolabile; flat tuning e modi di visualizzazione non emulati.",
           subtitle="STAGE TUNER"),

    # ================================================================== UTILITY / ROUTING
    model("psm5", "PMS-5", "Master Loop", "BOSS PSM-5 Power Supply & Master Switch", "Utility / Routing", "Router",
          (190, 45, 40),
          [toggle("LOOP", "select", ("OFF", "ON"), 0)],
          "type=ls2",
          "Un loop commutato: spento GUITAR->AMP diretto, acceso GUITAR->SEND->RETURN->AMP. Nel plugin (Router "
          "ls2, modo A<->BYPASS) il 'loop' e' il canale sinistro della coppia stereo; l'alimentazione DC non "
          "ha equivalente.",
          accent=(240, 240, 235), subtitle="MASTER SWITCH", stereo=True),

    model("ls2", "LSX-2", "Line Switcher", "BOSS LS-2 Line Selector", "Utility / Routing", "Router", (220, 220, 215),
          [selector("MODE", "mode", ["A<>BYPASS", "B<>BYPASS", "A<>B", "A+B<>BYP", "A+B MIX", "OUT SELECT"], 0,
                    short=["A", "B", "A-B", "A+B", "MIX", "OUT"]),
           knob("LEVEL A", "levelA", 0.5), knob("LEVEL B", "levelB", 0.5),
           toggle("PEDAL", "select", ("OFF", "ON"), 0)],
          "type=ls2",
          "Selettore/mixer bufferizzato di due linee: LEVEL A/B da muto a +20 dB (centro 0 dB) sul ritorno, "
          "modo MIX che somma le due linee e OUTPUT SELECT. Nel plugin la linea A e' il canale sinistro, la B il "
          "destro (manuale LS-2).",
          accent=(30, 30, 33), subtitle="LINE SELECTOR", stereo=True),

    model("ab2", "ABX-2", "Two Way", "BOSS AB-2 2-Way Selector", "Utility / Routing", "Router", (30, 30, 33),
          [selector("MODE", "direction", ["A/B>OUT", "IN>A/B"], 0, short=["SEL", "SPLT"]),
           toggle("A/B", "select", ("A", "B"), 0)],
          "type=ab2",
          "Selettore passivo bidirezionale a guadagno unitario, mai A e B insieme: MODE SEL sceglie l'ingresso "
          "A (sinistro) o B (destro) verso l'uscita, SPLT invia l'ingresso all'uscita A oppure B (commutazione "
          "silenziosa con dissolvenza di 10 ms).",
          accent=(230, 60, 50), subtitle="2-WAY SELECTOR", stereo=True),

    model("es5", "LR-5", "Loop Router 5", "BOSS ES-5 Effects Switching System", "Utility / Routing", "Router",
          (30, 30, 33),
          [selector("ROUTING", "mode", ["A<>BYPASS", "B<>BYPASS", "A<>B", "A+B<>BYP", "A+B MIX", "OUT SELECT"], 2,
                    short=["A", "B", "A-B", "A+B", "MIX", "OUT"]),
           knob("LEVEL A", "levelA", 0.5), knob("LEVEL B", "levelB", 0.5),
           toggle("PATCH", "select", ("1", "2"), 0)],
          "type=ls2",
          "Switcher programmabile a 5 loop: nel plugin non esistono loop esterni, quindi si comporta come un "
          "selettore di due linee (canali L/R) con livello per linea e ramo parallelo; PATCH alterna i due stati. "
          "Memorie, CTL/EXP e MIDI si ottengono con l'automazione dell'host.",
          accent=(230, 230, 230), subtitle="SWITCHING SYSTEM", stereo=True),

    model("es8", "LR-8", "Loop Router 8", "BOSS ES-8 Effects Switching System", "Utility / Routing", "Router",
          (30, 30, 33),
          [selector("ROUTING", "mode", ["A<>BYPASS", "B<>BYPASS", "A<>B", "A+B<>BYP", "A+B MIX", "OUT SELECT"], 2,
                    short=["A", "B", "A-B", "A+B", "MIX", "OUT"]),
           knob("LEVEL A", "levelA", 0.5), knob("LEVEL B", "levelB", 0.5),
           toggle("PATCH", "select", ("1", "2"), 0)],
          "type=ls2",
          "Switcher a 8 loop (7-8 stereo) con loop volume e 800 patch: nel plugin selettore/mixer di due linee "
          "L/R con livelli e ramo parallelo, PATCH alterna i due stati. Riordino dei loop, CTL/EXP e MIDI "
          "demandati all'host.",
          accent=(230, 230, 230), subtitle="SWITCHING SYSTEM", stereo=True),

    # ================================================================== VOLUME / ESPRESSIONE
    _vol("pv1", "VL-1", "Rocker Volume", "BOSS PV-1 Rocker Volume",
         "Pedale volume passivo a bilanciere della serie Rocker: potenziometro a curva logaritmica, nessun "
         "minimo regolabile.", minimum=False),
    _vol("fv50h", "VL-50H", "Foot Volume 50H", "BOSS FV-50H Foot Volume",
         "Volume passivo ad alta impedenza per chitarra (2 in / 2 out) con MINIMUM VOLUME a tallone giu' "
         "(a zero = mute). Curva audio.", stereo=True),
    _vol("fv50l", "VL-50L", "Foot Volume 50L", "BOSS FV-50L Foot Volume",
         "Versione a bassa impedenza stereo per tastiere e linea, con MINIMUM VOLUME e uscita EXP.", stereo=True),
    _vol("fv300h", "VL-300H", "Foot Volume 300H", "BOSS FV-300H Foot Volume",
         "Volume passivo ad alta impedenza con TUNER OUT e MINIMUM VOLUME a tallone giu'."),
    _vol("fv300l", "VL-300L", "Foot Volume 300L", "BOSS FV-300L Foot Volume/Expression",
         "Volume/espressione stereo a bassa impedenza (2 in / 2 out) con TUNER OUT e MINIMUM VOLUME.", stereo=True),
    _vol("fv500h", "VL-500H", "Foot Volume 500H", "BOSS FV-500H Foot Volume",
         "Pot volume 250 kohm ad alta impedenza, TUNER OUT, MINIMUM VOLUME che non cambia il massimo "
         "(a zero = mute per accordare). Taper audio (dossier)."),
    _vol("fv500l", "VL-500L", "Foot Volume 500L", "BOSS FV-500L Foot Volume/Expression",
         "Pot volume 25 kohm per sorgenti <600 ohm, stereo IN/OUT 1-2, TUNER OUT e MINIMUM VOLUME.", stereo=True),
    _vol("fv30h", "VL-30H", "Foot Volume 30H", "BOSS FV-30H Foot Volume",
         "Volume compatto ad alta impedenza con TUNER OUT; MINIMUM VOLUME per il livello a tallone giu'."),
    _vol("fv30l", "VL-30L", "Foot Volume 30L", "BOSS FV-30L Foot Volume",
         "Volume compatto stereo a bassa impedenza con uscita EXP e MINIMUM VOLUME.", stereo=True),
    _vol("fv60", "VL-60", "Stereo Volume 60", "BOSS FV-60 Stereo Volume Pedal",
         "Volume stereo passivo anni '80 senza minimo regolabile.", minimum=False, stereo=True),
    _vol("fv100", "VL-100", "Guitar Volume 100", "BOSS FV-100 Guitar Volume",
         "Volume passivo per chitarra anni '80, pot a curva audio senza minimo regolabile.", minimum=False),
    _vol("fv200", "VL-200", "Keys Volume 200", "BOSS FV-200 Keyboard Volume",
         "Volume passivo per tastiere anni '80 (bassa impedenza), senza minimo regolabile.", minimum=False),
    model("ev30", "EXP-30", "Dual Expression", "BOSS EV-30 Dual Expression Pedal", "Utility / Routing", "Volume",
          (30, 30, 33),
          [knob("EXP 1", "volume", 1.0, "percent", 0, 100), knob("MIN EXP 1", "min", 0.0, "percent", 0, 100)],
          "",
          "Pedale d'espressione senza audio (due uscite TRS). Nel plugin passa il segnale e il pedale agisce da "
          "volume con minimo regolabile; per comandare un altro parametro si mappa EXP 1 con l'automazione dell'host.",
          accent=(200, 200, 200), subtitle="DUAL EXPRESSION", style="treadle", look=dict(boss=True)),
    model("ev1wl", "EXP-1W", "Wireless Expression", "BOSS EV-1-WL Wireless MIDI Expression Pedal",
          "Utility / Routing", "Volume", (30, 30, 33),
          [knob("EXP", "volume", 1.0, "percent", 0, 100)],
          "",
          "Espressione MIDI via Bluetooth/USB senza percorso audio: nel plugin funziona da pedale volume sul "
          "segnale che lo attraversa; il controllo di altri parametri avviene con l'automazione/MIDI learn dell'host.",
          accent=(200, 200, 200), subtitle="MIDI EXPRESSION", style="treadle", look=dict(boss=True)),
]
