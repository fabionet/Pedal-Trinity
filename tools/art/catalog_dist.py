from catalog import model, knob, outer, inner, selector, toggle, slider
# Pedal Trinity - distorsioni, fuzz e metal (esclusi DS-1 "ds1" e MT-2W "mc2w", gia' in catalog_core.py).
# Fonti: docs/research/circuits_distortion.json/.md (service note BOSS, tracce Aion / Schematic Heaven /
#        Electric Druid), docs/research/boss_catalog.json (comandi, colori).
# Le edizioni limitate riusano la netlist del modello base (variabili DS1_NET / MT2_NET).
import re

from catalog_core import MODELS as _CORE

_BY_ID = {m["id"]: m for m in _CORE}

# ------------------------------------------------------------------ netlist condivise
# DS-1: identica a quella di catalog_core (TONE=0, LEVEL=1, DIST=2)
DS1_NET = _BY_ID["ds1"]["config"]
DS1_CTRL = lambda: [knob("TONE", "tone"), knob("LEVEL", "level"), knob("DIST", "gain")]
DS1_NOTE = ("Stesso circuito del DS-1 (booster 2SC2240, op-amp con DIST 100kB, diodi 1N4148 a massa dopo 2.2k/10n, "
            "tono LPF 234 Hz / HPF 804 Hz; service note BOSS). ")

# MT-2: la netlist dell'MT-2W in modo S (la levetta MODE, indice 6, non esiste: sw(6,a,b) -> a)
MT2_NET = re.sub(r"sw\(6,([^,()]+),[^()]+\)", r"\1", _BY_ID["mc2w"]["config"])
MT2_CTRL = lambda: [knob("LEVEL", "level"),
                    outer("LOW", "low", 0.5, "db", -15, 15), inner("HIGH", "high", 0.5, "db", -15, 15),
                    outer("MIDDLE", "mid", 0.5, "db", -15, 15), inner("MID FREQ", "midfreq", 0.5, "hz", 200, 5000),
                    knob("DIST", "gain", 0.6)]
MT2_NOTE = ("Pre-EQ a gyrator 952 Hz, op-amp con DIST 250kA (2-252x), diodi 1SS133 a massa, gyrator fissi 4.9 kHz e "
            "105 Hz, EQ attivo LOW 105 Hz / HIGH shelf / MIDDLE Wien 227-5386 Hz (schema di fabbrica, Electric Druid). ")


def hm2_net(mode=None):
    """HM-2 (service note 1983). mode = indice della levetta S/C (HM-2W) oppure None.
    Comandi: LEVEL=0, LOW=1, HIGH=2, DIST=3."""
    s = (lambda a, b: "sw(%d,%s,%s)" % (mode, a, b)) if mode is not None else (lambda a, b: a)
    return """
          hpf R=1M C=47n;
          hpf R=22k C=47n;
          bjt g=1+taper(3,C)*12 vp=4 vn=3.5 soft=2.5;
          lpf R=470k C=100p;
          bjt g=%s vp=3.5 vn=4 soft=2.5;
          lpf R=470k C=100p;
          fbclip Rg=47k+potr(3,250k,B) Cg=47n Rf=%s Cf=100p d=si up=1 dn=2 rail=4;
          series RL=10k d=ge;
          dclip R=10k C=1n d=si;
          hpf R=68k C=1u;
          peak f=%s Q=3.7 g=lin(1,-18,18);
          peak f=958 Q=3.4 g=lin(2,-14,14);
          peak f=%s Q=3.8 g=lin(2,-14,14);
          vol a=taper(0,A)*%s
          """ % (s("11", "16"), s("220k", "330k"), s("86.7", "75"), s("1280", "1500"), s("1", "1.41"))


HM_CTRL = lambda: [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -18, 18),
                   knob("HIGH", "high", 0.5, "db", -14, 14), knob("DIST", "gain", 0.7)]

BLACK = (30, 30, 33)
ORANGE = (236, 110, 30)

MODELS = [
    # ================================================================== DISTORSIONE
    # ------------------------------------------------------------------ BOSS DB-5 Driver
    model("db5", "DV-5", "Steel Driver", "BOSS DB-5 Driver", "Distorsione", "Circuit",
          (196, 199, 204),
          [knob("DRIVE", "drive", 0.6), knob("LEVEL", "level"), knob("TONE", "tone")],
          """
          hpf R=1M C=47n;
          bjt g=8 vp=4 vn=3.5 soft=2;
          opni Rg=10k Cg=1u Rf=pot(0,500k,A)+1k Cf=100p rail=4;
          dclip R=4.7k C=4.7n d=si;
          lpf R=potr(2,50k,A)+2.2k C=10n;
          vol a=taper(1,A)
          """,
          "Distorsore/booster pre-compatto: dati di circuito non pubblicati, netlist plausibile con transistor d'ingresso, "
          "op-amp con DRIVE in retroazione (1-51x), diodi Si a massa e tono passa-basso variabile 300 Hz-7 kHz "
          "(ricostruzione, boss_catalog).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS PD-1 Rocker Distortion
    model("pd1", "RK-1", "Treadle Grind", "BOSS PD-1 Rocker Distortion", "Distorsione", "Circuit",
          (45, 45, 48),
          [knob("PEDAL", "level", 0.8), knob("DIST", "gain", 0.6)],
          """
          hpf R=1M C=47n;
          bjt g=20 vp=4 vn=3.2 soft=2;
          opni Rg=4.7k Cg=1u Rf=pot(1,220k,A)+1k Cf=100p rail=4;
          dclip R=2.2k C=10n d=si;
          lpf R=10k C=4.7n;
          vol a=taper(0,A)
          """,
          "Distorsione a pedale basculante della serie Rocker: il pedale fa da volume, DIST regola il guadagno. "
          "Schema non disponibile: netlist plausibile stile DS-1 (transistor + op-amp + diodi Si a massa, "
          "passa-basso fisso 3.4 kHz). Il pedale e' reso come pomello PEDAL.",
          accent=(200, 200, 205), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS HM-2 Heavy Metal
    model("hm2", "SW-2", "Buzzsaw Black", "BOSS HM-2 Heavy Metal", "Distorsione", "Circuit",
          BLACK, HM_CTRL(), hm2_net(),
          "Due stadi a transistor in retroazione parallela (~22+21 dB, LPF 3.4 kHz), op-amp con diodi asimmetrici "
          "1:2 e DIST 250k a doppia funzione, coppia al germanio in serie (zona morta ~0.25 V), diodi Si a massa, "
          "COLOR MIX a gyrator: LOW 87 Hz Q 3.7, HIGH 958 Hz + 1.28 kHz (service note 1983, D. Ross).",
          accent=(236, 110, 30), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS HM-2W (Waza Craft)
    model("hm2w", "SW-2W", "Buzzsaw Craft", "BOSS HM-2W Heavy Metal Waza Craft", "Distorsione", "Circuit",
          BLACK, HM_CTRL() + [toggle("MODE", "mode", ("S", "C"), 0)], hm2_net(mode=4),
          "Modo S = circuito HM-2 (service note 1983) con +3 dB di livello massimo. Modo C (valori non pubblicati, "
          "stima): piu' guadagno sul secondo transistor e sull'op-amp, bassi spostati a 75 Hz e medio-alti a 1.5 kHz.",
          accent=(236, 110, 30), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS DF-2 Super Feedbacker & Distortion
    model("df2", "FB-2", "Feedback Crunch", "BOSS DF-2 Super Feedbacker & Distortion", "Distorsione", "Circuit",
          ORANGE,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DIST", "gain", 0.6), knob("OVER TONE", "overtone", 0.0)],
          """
          hpf R=1M C=47n;
          tap;
          rect g=2 mix=1 vk=0.03;
          lpf R=10k C=10n;
          mix wet=taper(3,B)*0.6 dry=1;
          hpf R=10k C=47n;
          bjt g=27 vp=4.8 vn=3.6 soft=2.2;
          lpf R=470k C=47p;
          opni Rg=4.7k Cg=1u Rf=pot(2,250k,A)+1k Cf=100p rail=3.8;
          dclip R=2.2k C=10n d=si;
          tonebm R1=6.8k C1=100n R2=6.8k C2=22n t=taper(1,B);
          gain db=6;
          vol a=taper(0,B)
          """,
          "Percorso tipo DS-1: booster 2SC732 (~28 dB), op-amp 1.2-54x con DIST 250kA, 1SS133 a massa dopo 2.2k/10n, "
          "tono LPF 234 Hz / HPF 804 Hz (service note 1984). Il feedbacker a PLL (fondamentale/ottava iniettata "
          "prima della distorsione) e' approssimato con un'ottava raddrizzata sempre attiva dosata da OVER TONE.",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS DS-2 Turbo Distortion
    model("ds2", "TX-2", "Twin Turbo", "BOSS DS-2 Turbo Distortion", "Distorsione", "Circuit",
          ORANGE,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DIST", "gain", 0.6),
           selector("TURBO", "mode", ["I", "II"], 0, short=["I", "II"])],
          """
          hpf R=1M C=1u;
          dclip R=4.7k C=0 d=ge;
          bjt g=9 vp=3.6 vn=3.6 soft=2.5;
          lshelf f=60 Q=0.7 g=-15;
          peak f=1060 Q=3.4 g=sw(3,0,12);
          hshelf f=3000 Q=0.7 g=sw(3,0,4);
          gain db=sw(3,0,4);
          opni Rg=1k+potr(2,250k,A) Cg=220n Rf=1M Cf=100p rail=3.6;
          dclip R=4.7k C=0 d=si;
          lshelf f=300 Q=0.6 g=lin(1,6,-6);
          hshelf f=1500 Q=0.6 g=lin(1,-10,10);
          lpf2 f=3300 Q=0.5;
          vol a=taper(0,A)
          """,
          "Limitatore d'ingresso a diodi 1SS188FM (bassa soglia), pre-gain 2SC3378 (~9x), op-amp discreto molto "
          "concentrato sulle medie (723 Hz-1.6 kHz, fino a ~60 dB), 1SS133 a massa, tono attivo a inclinazione e "
          "passa-basso 3.3 kHz. TURBO II: Sallen-Key risonante 1.06 kHz Q 3.4 + ramo acuti (schema di fabbrica).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS MZ-2 Digital Metalizer
    model("mz2", "DZ-2", "Pixel Metal", "BOSS MZ-2 Digital Metalizer", "Distorsione", "Circuit",
          BLACK,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive", 0.6)],
          """
          hpf R=1M C=47n;
          lshelf f=408 Q=0.7 g=-9;
          opni Rg=4.7k Cg=100n Rf=270k Cf=100p rail=2.2;
          tap;
          lpf2 f=1030 Q=2.35;
          mix wet=1 dry=0.4;
          opni Rg=4.7k Cg=1u Rf=pot(2,250k,A)+1k Cf=100p rail=2.2;
          lpf R=47k C=1n;
          tonebm R1=47k C1=10n R2=10k C2=10n t=taper(1,B);
          vol a=taper(0,A)*1.5
          """,
          "Solo la sezione analogica (service note 1988): op-amp discreti a 5 V che tosano ai binari (primo stadio 58x "
          "con pre-enfasi 408 Hz), Sallen-Key 1.03 kHz Q 2.35 in parallelo al ramo diretto, DRIVE 250kA. "
          "I modi digitali SGL/DOUB/CHO (raddoppio e chorus a 12 bit) non sono emulati: comando MODE omesso.",
          accent=(200, 40, 40), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS PW-2 Power Driver
    model("pw2", "AM-2", "Amber Muscle", "BOSS PW-2 Power Driver", "Distorsione", "Circuit",
          (245, 160, 30),
          [knob("LEVEL", "level"), knob("FAT", "fat", 0.5, "db", -15, 15),
           knob("MUSCLE", "muscle", 0.5, "db", -15, 15), knob("DRIVE", "drive", 0.6)],
          """
          hpf R=1M C=47n;
          bjt g=20 vp=3.5 vn=2.5 soft=2.5;
          opni Rg=470 Cg=4.7u Rf=pot(3,50k,A)+100 Cf=100p rail=2 railn=2.2;
          opinv Ri=10k Ci=0 Rf=32k Cf=0 rail=3.8;
          peak f=41 Q=1.8 g=lin(1,-15,15);
          peak f=1070 Q=6.7 g=lin(2,-15,15);
          lpf R=10k C=2.2n;
          vol a=taper(0,A)*0.3
          """,
          "Pre-gain 2SC2458 (~20x), op-amp discreto a JFET alimentato a 4.5 V con DRIVE 50kA (1.2-108x) che tosa ai "
          "binari (niente diodi), invertente M5218 x3.2, FAT gyrator 41 Hz Q 1.8 e MUSCLE 1.07 kHz Q 6.7 "
          "(traccia Schematic Heaven).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS XT-2 Xtortion
    model("xt2", "XR-2", "Red Contour", "BOSS XT-2 Xtortion", "Distorsione", "Circuit",
          (165, 25, 35),
          [knob("LEVEL", "level"), knob("CONTOUR", "contour"), knob("PUNCH", "punch"), knob("DIST", "gain", 0.6)],
          """
          hpf R=1M C=47n;
          peak f=1040 Q=2.6 g=lin(2,-8,14);
          peak f=596 Q=4 g=lin(2,-4,8);
          peak f=2970 Q=2.9 g=lin(2,3,6);
          opni Rg=270 Cg=4.7u Rf=pot(3,50k,A)+100 Cf=47p rail=3.6;
          dclip R=2k C=33n d=si;
          peak f=4000 Q=2.6 g=9;
          peak f=37.5 Q=1.6 g=8;
          peak f=900 Q=0.8 g=lin(1,-12,8);
          vol a=taper(0,A)*0.5
          """,
          "PUNCH a tre gyrator prima del guadagno (1.04 kHz Q 2.6, ~0.6 kHz, 2.97 kHz: tutto a sinistra acuti, a destra "
          "medie), op-amp 1.4-187x (DIST 50kA), diodi Si a massa con 2k/33n (2.4 kHz), gyrator fissi 4 kHz e 37.5 Hz, "
          "CONTOUR scavo/gobba ~900 Hz (traccia A. Taber; guadagni dei gyrator fissi stimati).",
          accent=(235, 230, 225), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS MD-2 Mega Distortion
    model("md2", "MG-2", "Crimson Mega", "BOSS MD-2 Mega Distortion", "Distorsione", "Circuit",
          (200, 40, 40),
          [knob("LEVEL", "level"),
           outer("BOTTOM", "bottom", 0.5, "db", -15, 15), inner("TONE", "tone"),
           outer("GAIN BOOST", "boost", 0.5), inner("DIST", "gain", 0.6)],
          """
          hpf R=100k C=39n;
          opni Rg=2.2k+potr(3,15k,B) Cg=22u Rf=1M Cf=1.5n rail=3.6;
          opni Rg=1k Cg=10u Rf=pot(4,100k,A)+1k Cf=470p rail=3.8;
          opinv Ri=10k Ci=0 Rf=22k Cf=0 rail=3.8;
          bjt g=35 vp=2.5 vn=3.5 soft=4;
          gain db=-10;
          shelf1 R=10k Cs=27n Cp=18n;
          lpf R=56k C=220p;
          hshelf f=3000 Q=0.7 g=lin(2,-12,12);
          peak f=117 Q=4.1 g=lin(1,-15,15);
          vol a=taper(0,A)*0.5
          """,
          "Nessun diodo: op-amp discreto a JFET con GAIN BOOST (rete a T, guadagno forte sotto ~100 Hz), TL072 2-102x con "
          "DIST, invertente x2.2 e stadio push-pull complementare 2SA1048/2SC2458 che taglia le due semionde in modo "
          "diverso; BOTTOM gyrator 117 Hz Q 4.1 (traccia Schematic Heaven, mirosol).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS ST-2 Power Stack
    model("st2", "SK-2", "Tower Stack", "BOSS ST-2 Power Stack", "Distorsione", "Circuit",
          BLACK,
          [knob("LEVEL", "level"), knob("BASS", "bass"), knob("TREBLE", "treble"), knob("SOUND", "gain", 0.5)],
          """
          hpf R=1M C=47n;
          hpf R=100k C=10n;
          opni Rg=4.7k Cg=1u Rf=pot(3,470k,A)+4.7k Cf=47p rail=3.8;
          peak f=700 Q=0.7 g=lin(3,1,5);
          fbclip Rg=10k Cg=100n Rf=100k+pot(3,470k,A) Cf=220p d=si up=1 dn=2 rail=3.8;
          bjt g=3 vp=3 vn=2 soft=2;
          fmv R1=220k R2=1M R3=25k R4=33k C1=470p C2=22n C3=22n t=taper(2,B) m=0.5 l=taper(1,A);
          vol a=taper(0,A)
          """,
          "Schema non pubblicato (65 mA fanno pensare a DSP, non verificato): netlist plausibile di preamp da testata "
          "valvolare con SOUND che alza insieme guadagno e presenza di medie, clipping asimmetrico 1:2 e stack "
          "passivo tipo Marshall (BASS/TREBLE, MIDDLE fisso a meta').",
          accent=(200, 40, 40), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS DA-2 Adaptive Distortion
    model("da2", "EA-2", "Ember Adapt", "BOSS DA-2 Adaptive Distortion", "Distorsione", "Circuit",
          (200, 85, 25),
          [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -12, 12),
           knob("HIGH", "high", 0.5, "db", -12, 12), knob("A-DIST", "gain", 0.6)],
          """
          hpf R=1M C=47n;
          lshelf f=220 Q=0.7 g=-9;
          opni Rg=2.2k Cg=1u Rf=pot(3,220k,A)+4.7k Cf=47p rail=3.8;
          fbclip Rg=4.7k Cg=100n Rf=47k Cf=100p d=si up=1 dn=1 rail=3.8;
          dclip R=2.2k C=4.7n d=si;
          lshelf f=220 Q=0.7 g=9;
          lshelf f=100 Q=0.7 g=lin(1,-12,12);
          hshelf f=3000 Q=0.7 g=lin(2,-12,12);
          lpf R=10k C=2.2n;
          vol a=taper(0,A)
          """,
          "Distorsione digitale MDP con processori multipli adattivi (45 mA): approssimata con pre-enfasi che toglie "
          "bassi prima del clipping e li restituisce dopo (bassi stretti, alti pieni), due clipper Si in cascata ed "
          "EQ LOW/HIGH a mensola +-12 dB (ricostruzione dal comportamento descritto da BOSS).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS DS-1X
    model("ds1x", "DX-1X", "Crunch X", "BOSS DS-1X Distortion", "Distorsione", "Circuit",
          (225, 80, 35),
          [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -12, 12),
           knob("HIGH", "high", 0.5, "db", -12, 12), knob("DIST", "gain", 0.6)],
          """
          hpf R=470k C=47n;
          lshelf f=250 Q=0.7 g=-8;
          bjt g=40 vp=4.8 vn=3.6 soft=2.2;
          lpf R=10k C=1n;
          opni Rg=4.7k Cg=1u Rf=pot(3,100k,B)+10 Cf=100p rail=3.8;
          dclip R=2.2k C=10n d=si;
          lshelf f=250 Q=0.7 g=8;
          peak f=600 Q=0.8 g=-4;
          lshelf f=120 Q=0.7 g=lin(1,-12,12);
          hshelf f=2500 Q=0.7 g=lin(2,-12,12);
          vol a=taper(0,A)
          """,
          "Distorsione digitale MDP (60 mA) che adatta il clipping al registro: approssimata con la catena DS-1 "
          "(booster + op-amp + diodi Si) e pre/de-enfasi dei bassi per avere bassi stretti e acuti pieni; "
          "il tono passivo e' sostituito da LOW/HIGH a mensola (ricostruzione dalle specifiche BOSS).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS DS-1W (Waza Craft)
    model("ds1w", "DX-1W", "Orange Crunch Craft", "BOSS DS-1W Distortion Waza Craft", "Distorsione", "Circuit",
          ORANGE,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DIST", "gain"), toggle("MODE", "mode", ("S", "C"), 0)],
          """
          hpf R=470k C=47n;
          hpf R=10k C=26n;
          bjt g=sw(3,60,75) vp=4.2 vn=3.4 soft=2.2;
          lpf R=10k C=1n;
          opni Rg=4.7k*sw(3,1,0.6) Cg=1u Rf=pot(2,100k,B)+10 Cf=100p rail=4;
          dclip R=2.2k C=10n d=si up=sw(3,1,2) dn=sw(3,1,2);
          tonebm R1=6.8k C1=sw(3,100n,68n) R2=6.8k C2=sw(3,22n,47n) t=taper(1,B);
          vol a=taper(0,B)*sw(3,1,0)+taper(0,A)*sw(3,0,2)
          """,
          "Modo S = DS-1 (op-amp discreto, alimentazione 8.45 V). Modo C: due diodi in serie per verso (~1.3 V), piu' "
          "sensibilita' d'ingresso, condensatori del tono cambiati (meno scavo sulle medie), +6 dB e curva di volume "
          "diversa (pagina BOSS, traccia freestompboxes; valori di C stimati).",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ edizioni DS-1
    model("ds1_4a", "DX-1A4", "Orange Crunch 40", "BOSS DS-1-4A 40th Anniversary", "Distorsione", "Circuit",
          (25, 25, 28), DS1_CTRL(), DS1_NET,
          DS1_NOTE + "Edizione 40 anni (2017): solo estetica, enclosure nera con grafica commemorativa.",
          accent=(236, 110, 30), subtitle="DISTORTION"),
    model("ds1_b50a", "DX-1G5", "Orange Crunch 50", "BOSS DS-1-B50A 50th Anniversary", "Distorsione", "Circuit",
          ORANGE, DS1_CTRL(), DS1_NET,
          DS1_NOTE + "Edizione 50 anni BOSS (2023): solo estetica, colore originale con finiture commemorative (da verificare).",
          accent=(190, 150, 60), subtitle="DISTORTION"),
    model("ds1_wh", "DX-1WH", "Orange Crunch White", "BOSS DS-1-WH (White)", "Distorsione", "Circuit",
          (225, 225, 220), DS1_CTRL(), DS1_NET,
          DS1_NOTE + "Edizione limitata bianca (2024): solo estetica.",
          accent=(236, 110, 30), subtitle="DISTORTION"),
    model("ds1_bk", "DX-1BK", "Orange Crunch Black", "BOSS DS-1-BK (Black)", "Distorsione", "Circuit",
          BLACK, DS1_CTRL(), DS1_NET,
          DS1_NOTE + "Edizione limitata nera: solo estetica.",
          accent=(236, 110, 30), subtitle="DISTORTION"),
    model("ds1_6m", "DX-1M6", "Orange Crunch 6M", "BOSS DS-1-6M 6 Million", "Distorsione", "Circuit",
          (200, 165, 70), DS1_CTRL(), DS1_NET,
          DS1_NOTE + "Esemplare commemorativo dei 6 milioni di compatti (non in vendita): solo estetica, colore stimato.",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ================================================================== FUZZ
    # ------------------------------------------------------------------ BOSS FZ-2 Hyper Fuzz
    model("fz2", "HZ-2", "Hyper Hive", "BOSS FZ-2 Hyper Fuzz", "Fuzz", "Circuit",
          (150, 175, 190),
          [knob("LEVEL", "level"),
           outer("BASS", "bass", 0.5, "db", -15, 15), inner("TREBLE", "treble", 0.5, "db", -15, 15),
           knob("GAIN", "gain", 0.6),
           selector("MODE", "mode", ["GAIN BOOST", "FUZZ I", "FUZZ II"], 1, short=["GB", "I", "II"])],
          """
          hpf R=1M C=47n;
          opni Rg=1.5k Cg=2.2u Rf=pot(3,50k,A)+2.2k Cf=47p rail=3.6;
          tap;
          lpf R=10k C=33n;
          rect g=1 mix=0.85 vk=0.05;
          bjt g=3 vp=1.5 vn=1.5 soft=3 inv=0;
          dclip R=1k C=0 d=si;
          lpf R=10k C=sw(4,10p,15n,10p);
          peak f=1000 Q=0.7 g=sw(4,0,0,-17);
          lshelf f=80 Q=0.7 g=sw(4,0,0,5);
          gain db=sw(4,0,8.6,21.6);
          rail rail=3.6;
          mix wet=sw(4,0,1,1) dry=sw(4,1,0,0);
          peak f=104 Q=3.1 g=lin(1,-15,15);
          hshelf f=3200 Q=0.7 g=lin(2,-15,15);
          vol a=taper(0,A)*sw(4,0,0.4,0.4)+sw(4,0.2,0,0)
          """,
          "Boost a op-amp discreto 2.5-36x (GAIN 50kA), passa-basso 482 Hz, coppia differenziale Superfuzz che "
          "raddrizza (ottava, transistor non appaiati), 1N914 a massa; FUZZ I piatto x2.7 con LPF 1.06 kHz, FUZZ II "
          "x12 con scavo -15 dB a 1 kHz; BASS gyrator 104 Hz, TREBLE mensola 3.2 kHz; in GAIN BOOST il LEVEL e' "
          "escluso (Aion Hypercube).",
          accent=(20, 20, 20), subtitle="OCTAVE FUZZ"),

    # ------------------------------------------------------------------ BOSS FZ-3 Fuzz
    model("fz3", "SZ-3", "Silicon Buzz", "BOSS FZ-3 Fuzz", "Fuzz", "Circuit",
          (150, 150, 155),
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("FUZZ", "gain", 0.7)],
          """
          hpf R=1M C=47n;
          bjt g=3.3 vp=3.8 vn=3.8 soft=2;
          lpf R=2.2k C=10n;
          hpf R=6.8k C=1u;
          bjt g=4+taper(2,C)*80 vp=3.2 vn=1.4 soft=2.5;
          lpf R=100k C=1n;
          hpf R=100k C=18n;
          bjt g=1 vp=3.5 vn=3.5 soft=2;
          lpf R=10k C=8.2n;
          tonebm R1=47k C1=10n R2=47k C2=10n t=taper(1,B);
          vol a=taper(0,A)
          """,
          "Pre-gain 2SC2458 (x3.3, passa-basso 7.2 kHz), coppia tipo Fuzz Face al silicio con FUZZ 1kC sul bypass "
          "d'emettitore (clipping asimmetrico, niente diodi), recupero con LPF 1.94 kHz e tono Big Muff 339/339 Hz, "
          "piatto a meta' (Aion Argent, traccia 2021).",
          accent=(20, 20, 20), subtitle="FUZZ"),

    # ------------------------------------------------------------------ BOSS FZ-5 Fuzz
    model("fz5", "TZ-5", "Triple Fuzz", "BOSS FZ-5 Fuzz", "Fuzz", "Circuit",
          (175, 178, 182),
          [knob("LEVEL", "level"), knob("FUZZ", "gain", 0.7),
           selector("MODE", "mode", ["S", "F", "O"], 1, short=["S", "F", "O"])],
          """
          hpf R=sw(2,100k,22k,100k) C=sw(2,10n,2.2u,47n);
          bjt g=sw(2,6,4,5)+taper(1,A)*sw(2,150,120,60) vp=sw(2,1.5,3.5,2) vn=sw(2,1.2,1.2,2) soft=sw(2,3,2,4);
          rect g=1 mix=sw(2,0,0,0.9) vk=0.03;
          peak f=1500 Q=1 g=sw(2,4,0,2);
          lpf R=10k C=sw(2,4.7n,6.8n,10n);
          vol a=taper(0,A)*sw(2,1,0.6,1)
          """,
          "Fuzz digitale COSM con tre modelli: S = Maestro FZ-1A (tre transistor, sottile e ronzante), F = Fuzz Face "
          "(pieno e asimmetrico), O = Octavia (ottava raddrizzata). Approssimato con stadio a transistor a "
          "escursione/ginocchio per modo e raddrizzatore (ricostruzione dalle specifiche BOSS).",
          accent=(20, 20, 20), subtitle="FUZZ"),

    # ------------------------------------------------------------------ BOSS TB-2W Tone Bender (Waza Craft)
    model("tb2w", "GB-2W", "Germanium Blade", "BOSS TB-2W Tone Bender Waza Craft", "Fuzz", "Circuit",
          (160, 162, 168),
          [knob("ATTACK", "gain", 0.7), knob("LEVEL", "level")],
          """
          hpf R=10k C=4.7u;
          bjt g=15 vp=1.8 vn=0.9 soft=2.5;
          hpf R=10k C=10u;
          bjt g=8+taper(0,A)*60 vp=2.5 vn=1.2 soft=2.5;
          bjt g=3 vp=3.5 vn=2 soft=2;
          lpf R=10k C=3.3n;
          vol a=taper(1,A)*0.5
          """,
          "Tre transistor al germanio in cascata tipo MkII (bassa impedenza d'ingresso, clipping asimmetrico morbido), "
          "ATTACK sul guadagno del secondo stadio: netlist plausibile, schema non pubblicato (collaborazione Sola "
          "Sound). La levetta OUTPUT BUF/THRU agisce solo sul bypass ed e' omessa.",
          accent=(20, 20, 20), subtitle="FUZZ"),

    # ------------------------------------------------------------------ BOSS FZ-1W Fuzz (Waza Craft)
    model("fz1w", "VZ-1W", "Vintage Buzz Craft", "BOSS FZ-1W Fuzz Waza Craft", "Fuzz", "Circuit",
          (150, 152, 156),
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("FUZZ", "gain", 0.7),
           toggle("MODE", "mode", ("V", "M"), 0)],
          """
          hpf R=22k C=sw(3,100n,220n);
          bjt g=sw(3,8,6)+taper(2,A)*sw(3,120,200) vp=sw(3,1.8,3) vn=sw(3,1.2,2.5) soft=sw(3,2.5,3);
          bjt g=2 vp=sw(3,1.8,3) vn=sw(3,1.4,2.5) soft=2;
          peak f=sw(3,1200,800) Q=0.8 g=sw(3,3,5);
          lpf R=potr(1,47k,A)+2.2k C=sw(3,4.7n,3.3n);
          vol a=taper(0,A)*0.5
          """,
          "Fuzz al silicio con ingresso a 22 kOhm. Vintage (ispirato al Maestro FZ-1): ronzante, escursione ridotta, "
          "si pulisce col volume; Modern: piu' guadagno e medie concentrate, TONE agisce solo sulla brillantezza. "
          "Schema non pubblicato: netlist plausibile dalle specifiche BOSS.",
          accent=(20, 20, 20), subtitle="FUZZ"),

    # ================================================================== METAL
    # ------------------------------------------------------------------ BOSS MT-2 Metal Zone
    model("mt2", "MF-2", "Metal Forge", "BOSS MT-2 Metal Zone", "Metal", "Circuit",
          BLACK, MT2_CTRL(), MT2_NET,
          MT2_NOTE + "Stessa netlist dell'MT-2W in modo S, senza levetta.",
          accent=(240, 120, 30), subtitle="DISTORTION"),
    model("mt2_3a", "MF-2A3", "Metal Forge 30", "BOSS MT-2-3A 30th Anniversary", "Metal", "Circuit",
          (40, 40, 44), MT2_CTRL(), MT2_NET,
          MT2_NOTE + "Edizione 30 anni (2021): stesso circuito, differenza solo estetica (grafica commemorativa).",
          accent=(190, 150, 60), subtitle="DISTORTION"),
    model("mt2_8m", "MF-2M8", "Metal Forge 8M", "BOSS MT-2-8M 8 Million", "Metal", "Circuit",
          (175, 150, 90), MT2_CTRL(), MT2_NET,
          MT2_NOTE + "Esemplare commemorativo degli 8 milioni (non in vendita): solo estetica, colore stimato.",
          accent=(20, 20, 20), subtitle="DISTORTION"),

    # ------------------------------------------------------------------ BOSS HM-3 Hyper Metal
    model("hm3", "SW-3", "Hyper Saw", "BOSS HM-3 Hyper Metal", "Metal", "Circuit",
          BLACK,
          [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -15, 15),
           knob("HIGH", "high", 0.5, "db", -15, 15), knob("DIST", "gain", 0.7)],
          """
          lpf R=4.7k C=6.8n;
          hpf R=47k C=47n;
          opni Rg=220 Cg=10u Rf=pot(3,50k,A)+220 Cf=47p rail=3.6;
          lpf R=10k C=10n;
          fbclip Rg=22k Cg=47n Rf=220k Cf=100p d=si up=2 dn=1 rail=3.6;
          dclip R=2.2k C=0 d=si;
          shelf1 R=10k Cs=10n Cp=15n;
          lpf R=3.3k C=10n;
          peak f=86.7 Q=3.7 g=lin(1,-15,15);
          peak f=890 Q=4.6 g=lin(2,-15,15);
          peak f=5390 Q=4.1 g=lin(2,-12,12);
          vol a=taper(0,A)
          """,
          "Pre-filtro 72 Hz-5 kHz, op-amp 2-229x (DIST 50kA), passa-basso 1.59 kHz, secondo op-amp x11 con diodi "
          "asimmetrici 2:1, 1SS133 a massa, mensola 1.6 kHz; LOW gyrator 87 Hz Q 3.7, HIGH due gyrator 890 Hz e "
          "5.39 kHz (service note 1993).",
          accent=(240, 120, 30), subtitle="METAL DISTORTION"),

    # ------------------------------------------------------------------ BOSS ML-2 Metal Core
    model("ml2", "IA-2", "Iron Anvil", "BOSS ML-2 Metal Core", "Metal", "Circuit",
          (35, 35, 38),
          [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -15, 15),
           knob("HIGH", "high", 0.5, "db", -15, 15), knob("DIST", "gain", 0.7)],
          """
          hpf R=1M C=47n;
          hpf R=100k C=15n;
          opni Rg=1k Cg=4.7u Rf=pot(3,250k,A)+2.2k Cf=47p rail=3.8;
          dclip R=2.2k C=0 d=si;
          opni Rg=2.2k Cg=1u Rf=100k Cf=100p rail=3.8;
          dclip R=2.2k C=10n d=si;
          peak f=800 Q=0.8 g=-6;
          peak f=70 Q=1.2 g=lin(1,-15,15);
          hshelf f=3500 Q=0.7 g=lin(2,-15,15);
          vol a=taper(0,A)
          """,
          "Schema non disponibile (60 mA, forse con DSP, non verificato): netlist plausibile ad altissimo guadagno con "
          "due op-amp e diodi Si in cascata, passa-alto 106 Hz prima del clipping, scavo fisso a 800 Hz, LOW a 70 Hz "
          "per i bassi da 7 corde e HIGH a mensola.",
          accent=(200, 200, 205), subtitle="METAL DISTORTION"),
]
