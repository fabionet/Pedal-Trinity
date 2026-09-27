"""
Pedal Trinity - modelli di riferimento (scritti a mano, fanno da esempio per gli altri gruppi).
Fonti: docs/research/circuits_overdrive.json, circuits_distortion.json, circuits_dynamics_eq_utility.json
"""
from catalog import model, knob, outer, inner, selector, toggle, slider

MODELS = [
    # ------------------------------------------------------------------ Tube Screamer TS808
    model("ed9", "ED-9", "Emerald Drive", "Ibanez TS808 Tube Screamer", "Overdrive / Boost", "Circuit",
          (38, 128, 62),
          [knob("DRIVE", "drive"), knob("TONE", "tone"), knob("LEVEL", "level")],
          """
          hpf R=510k C=20n;
          fbclip Rg=4.7k Cg=47n Rf=51k+pot(0,500k,A) Cf=51p d=si up=1 dn=1 rail=4.2;
          tonets R7=1k C4=220n P=20k t=taper(1,B) Rw=220 Cw=220n Rf=1k Cf=0 rail=4.2;
          hpf R=100k C=1u;
          vol a=taper(2,A)
          """,
          "Overdrive con clipping a diodi nella retroazione dell'op-amp (1N914 simmetrici), guadagno 12-118x "
          "con gobba sulle medie (passa-alto 720 Hz sul ramo che distorce) e tono attivo TS808 (1k/0.22u, pot 20k).",
          style="ts", text=(238, 236, 222), accent=(38, 128, 62), subtitle="OVERDRIVE"),

    # ------------------------------------------------------------------ BOSS DS-1
    model("ds1", "DX-1", "Orange Crunch", "BOSS DS-1", "Distorsione", "Circuit",
          (236, 110, 30),
          [knob("TONE", "tone"), knob("LEVEL", "level"), knob("DIST", "gain")],
          """
          hpf R=470k C=47n;
          hpf R=10k C=26n;
          bjt g=60 vp=4.8 vn=3.6 soft=2.2;
          lpf R=10k C=1n;
          opni Rg=4.7k Cg=1u Rf=pot(2,100k,B)+10 Cf=100p rail=3.8;
          dclip R=2.2k C=10n d=si;
          tonebm R1=6.8k C1=100n R2=6.8k C2=22n t=taper(0,B);
          vol a=taper(1,B)
          """,
          "Booster a transistor 2SC2240 (~36 dB, passa-alto ~600 Hz), op-amp con DIST in retroazione (1-22x, clipping "
          "ai binari), diodi 1N4148 verso massa dopo 2.2k con 10n, tono passivo LPF 234 Hz / HPF 1 kHz (service note BOSS).",
          accent=(20, 20, 20)),

    # ------------------------------------------------------------------ BOSS MT-2W (Waza)
    model("mc2w", "MC-2W", "Metal Forge", "BOSS MT-2W Metal Zone Waza Craft", "Metal", "Circuit",
          (30, 30, 33),
          [knob("LEVEL", "level"),
           outer("LOW", "low", 0.5, "db", -15, 15), inner("HIGH", "high", 0.5, "db", -15, 15),
           outer("MIDDLE", "mid", 0.5, "db", -15, 15), inner("MID FREQ", "midfreq", 0.5, "hz", 200, 5000),
           knob("DIST", "gain", 0.6),
           toggle("MODE", "mode", ("S", "C"), 0)],
          """
          hpf R=1M C=47n;
          hpf R=100k C=sw(6,15n,10n);
          peak f=952 Q=2.8 g=sw(6,38,34);
          lpf R=220k C=100p rail=3.8;
          gain db=-6; lpf R=5k C=47n;
          hpf R=100k C=33n;
          opni Rg=1k Cg=10u Rf=pot(5,250k,A)*sw(6,1,1.4)+1k Cf=47p rail=3.8;
          dclip R=2.2k C=0 d=si;
          shelf1 R=4.7k Cs=15n Cp=32n;
          peak f=4894 Q=2.2 g=sw(6,12.7,10);
          peak f=105 Q=4 g=sw(6,18,14);
          peak f=105 Q=1.4 g=lin(1,-15,15);
          hshelf f=5000 Q=0.7 g=lin(2,-15,15);
          peak f=log(4,227,5386) Q=1.1 g=lin(3,-15,15);
          vol a=taper(0,A)*0.5
          """,
          "Pre-EQ a gyrator a 952 Hz (+38 dB), secondo op-amp con DIST 250kA (2-252x), diodi 1SS133 a massa, "
          "gyrator fissi a 4.9 kHz e 105 Hz, EQ attivo: LOW 105 Hz, HIGH shelf, MIDDLE semi-parametrico "
          "a ponte di Wien 227-5386 Hz (service note MT-2). Modo C: piu' guadagno, bassi piu' stretti.",
          accent=(240, 120, 30), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS GE-7
    model("gq7", "GQ-7", "Graphic EQ", "BOSS GE-7", "Equalizzatori", "GraphicEQ",
          (196, 199, 204),
          [slider("100", "b0"), slider("200", "b1"), slider("400", "b2"), slider("800", "b3"),
           slider("1.6k", "b4"), slider("3.2k", "b5"), slider("6.4k", "b6"), slider("LEVEL", "level")],
          "freqs=95.6,204,394,790,1549,3303,6400 qs=3.36,3.47,3.71,4.07,3.11,3.74,0.9 qmin=0.9 range=15 lvl=15",
          "Sette bande a gyrator RLC serie (f0 e Q calcolati dai componenti: 100 Hz Q 3.4 ... 3.2 kHz Q 3.7), "
          "la banda 6.4 kHz e' una mensola RC; Q proporzionale al guadagno come con i cursori reali. Level +-15 dB.",
          accent=(20, 60, 150), subtitle="7-BAND EQUALIZER"),

    # ------------------------------------------------------------------ splitter di Pedal Trinity
    model("split", "SPL-3", "Signal Splitter", "", "Splitter", "Splitter",
          (38, 40, 46),
          [selector("MODE", "mode", ["MONO", "DUAL", "STEREO"], 0, short=["MONO", "DUAL", "ST"]),
           knob("BALANCE", "balance", 0.5, units="percent", lo=-100, hi=100),
           knob("LEVEL A", "levelA", 2.0 / 3.0, units="db", lo=-24, hi=12),
           knob("LEVEL B", "levelB", 2.0 / 3.0, units="db", lo=-24, hi=12)],
          "",
          "Modulo di instradamento (non e' un pedale del catalogo BOSS): divide il segnale mono in un punto "
          "qualsiasi della catena. MONO lascia passare il segnale, DUAL crea due catene mono indipendenti "
          "(A = uscita sinistra, B = destra), STEREO una catena stereo in cui i pedali a doppio jack elaborano "
          "A e B. BALANCE e LEVEL A/B regolano le due mandate; il bilanciamento d'uscita e' sul meter OUTPUT.",
          accent=(217, 180, 100), subtitle="A/B SPLITTER", stereo=True),

    # ------------------------------------------------------------------ Neural Amp Modeler (A1 / A2)
    model("nama1a2", "NAM-A1A2", "NAM-A1A2 Model", "Neural Amp Modeler di Steven Atkinson (core NAM, MIT)", "Amp / IR", "Nam",
          (92, 4, 7),
          [knob("INPUT", "input", 0.5, units="db", lo=-20, hi=20),
           knob("BASS", "bass", 0.5), knob("MIDDLE", "middle", 0.5), knob("TREBLE", "treble", 0.5),
           knob("OUTPUT", "output", 0.5, units="db", lo=-40, hi=40),
           knob("NAM A", "namA", 30.0 / 42.0, units="db", lo=-30, hi=12),
           knob("IR A", "irA", 30.0 / 42.0, units="db", lo=-30, hi=12),
           knob("NAM B", "namB", 30.0 / 42.0, units="db", lo=-30, hi=12),
           knob("IR B", "irB", 30.0 / 42.0, units="db", lo=-30, hi=12),
           toggle("A", "chanA", ("OFF", "ON"), 1), toggle("B", "chanB", ("OFF", "ON"), 1)],
          "",
          "Lettore di modelli Neural Amp Modeler con il core NAM ufficiale: A1 (WaveNet), A2 slimmable "
          "(A2-Full + A2-Lite nello stesso file), LSTM; file .nam e .namb (TONE3000). Due canali A e B, "
          "ciascuno con modello e IR propri; tonestack, calibrazione d'ingresso e modi d'uscita "
          "Raw / Normalized / Calibrated come nel plugin NAM. I file si caricano dal pannello di zoom e "
          "vengono verificati contro manomissioni prima dell'uso.",
          style="nam", text=(240, 232, 220), accent=(240, 232, 220), subtitle="NEURAL AMP MODELER", stereo=True),
]
