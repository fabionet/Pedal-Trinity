from catalog import model, knob, outer, inner, selector, toggle, slider

# Pedal Trinity - gruppo OVERDRIVE / BOOST / PREAMP (e drive per basso).
# Fonti: docs/research/circuits_overdrive.json/.md (valori stadio per stadio), docs/research/boss_catalog.json
# (elenco, comandi, colori). ED-9 (TS808) sta in catalog_core.py.
# Le edizioni limitate riusano la netlist del modello base (stesso circuito, cambia solo l'estetica).


# ------------------------------------------------------------------ utilita'
def _n(x):
    """Numero in notazione SI compatta per la netlist (4.7k, 22n, 0.45 ...)."""
    if x == 0:
        return "0"
    a = abs(x)
    if 1e-3 <= a < 1e3:
        return "%.4g" % x
    for mult, suf in ((1e6, "M"), (1e3, "k"), (1e-6, "u"), (1e-9, "n"), (1e-12, "p")):
        if a >= mult * 0.9999 or suf == "p":
            return "%.4g%s" % (x / mult, suf)


def _sw(i, vals):
    """sw(i,v0,v1,...) con i valori formattati."""
    return "sw(%d,%s)" % (i, ",".join(_n(v) for v in vals))


def boss_tone(P, Cin, Cg, t, idx=None):
    """
    Tono passivo BOSS (OD-2, BD-2, OD-3, SD-2): Cin in serie, pot P, lug1 -> Cg a massa, wiper = uscita.
    Risolto esattamente (carico del level trascurato):
        H = K (1 + s a P Cg) / (1 + s P Cser),  K = Cin/(Cin+Cg), Cser = Cin*Cg/(Cin+Cg)
    cioe' 'gain a=K' + 'shelf1 R=P Cs=a*Cg Cp=Cser-a*Cg' (Cp negativo = enfasi degli alti, stabile).
    a=0: passa-basso a 1/(2 pi P Cser); a=1: bassi attenuati di K, alti a 0 dB.
    t   = lista di termini-prodotto la cui somma e' la posizione del pot (es. ["taper(1,B)"]).
    Cin/Cg possono essere liste (valori per posizione della levetta/selettore idx).
    """
    if isinstance(Cin, (list, tuple)) or isinstance(Cg, (list, tuple)):
        n = len(Cin) if isinstance(Cin, (list, tuple)) else len(Cg)
        ci = list(Cin) if isinstance(Cin, (list, tuple)) else [Cin] * n
        cg = list(Cg) if isinstance(Cg, (list, tuple)) else [Cg] * n
        K = _sw(idx, [a / (a + b) for a, b in zip(ci, cg)])
        cser = _sw(idx, [a * b / (a + b) for a, b in zip(ci, cg)])
        cgx = _sw(idx, cg) + "*"
        ncgx = _sw(idx, [-v for v in cg]) + "*"
    else:
        K = _n(Cin / (Cin + Cg))
        cser = _n(Cin * Cg / (Cin + Cg))
        cgx = _n(Cg) + "*"
        ncgx = _n(-Cg) + "*"
    cs = "+".join(cgx + term for term in t)
    cp = cser + "+" + "+".join(ncgx + term for term in t)
    return "gain a=%s; shelf1 R=%s Cs=%s Cp=%s" % (K, _n(P), cs, cp)


# ------------------------------------------------------------------ colori
YELLOW = (240, 200, 30)
MUSTARD = (214, 160, 40)
BLUE = (40, 110, 200)
BLACK = (30, 30, 33)
SILVER = (196, 199, 204)


# ================================================================== OD-1 (1977)
OD1_CTRL = [knob("LEVEL", "level"), knob("OVER DRIVE", "drive")]
OD1_NET = """
hpf R=470k C=47n;
hpf R=100k C=4.7n;
fbclip Rg=4.7k Cg=47n Rf=33k+pot(1,1M,B) Cf=47p d=si up=1 dn=2 rail=4.2;
opinv Ri=10k Ci=0 Rf=10k Cf=18n rail=4.2;
vol a=taper(0,B)
"""
OD1_NOTE = ("Buffer, passa-alto 4.7n/100k (339 Hz), op-amp con 33k + OVER DRIVE 1M in retroazione (8-220x, "
            "gamba 4.7k/47n a 720 Hz) e diodi 1S2473 asimmetrici 1:2, poi unico filtro fisso invertente 10k/18n "
            "(884 Hz) e LEVEL 10kB (schema di fabbrica, clone Aion Parhelion; Cf 47p della versione quad).")

# ================================================================== SD-1 (1981)
SD1_CTRL = [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive")]
SD1_NET = """
hpf R=470k C=47n;
hpf R=100k C=18n;
fbclip Rg=4.7k Cg=47n Rf=33k+pot(2,1M,B) Cf=10p d=si up=1 dn=2 rail=4.2;
tonets R7=10k C4=18n P=20k t=taper(1,W) Rw=470 Cw=27n Rf=10k Cf=10n rail=4.2;
vol a=taper(0,B)
"""
SD1_NOTE = ("Come un TS ma con ingresso 18n/100k (88 Hz), DRIVE 1M (8-220x, Cf 10p = limite GBW del 4558), "
            "diodi 1S2473 asimmetrici 1:2, LPF 10k/18n (884 Hz) e tono attivo 20k W con 470R/27n e 10k//10n "
            "in retroazione (schema di fabbrica, tabella Aion Stratus).")

# ================================================================== BD-2 (1995)
BD2_CTRL = [knob("LEVEL", "level"), knob("TONE", "tone"), knob("GAIN", "gain")]
BD2_NET = """
hpf R=1M C=47n;
hpf R=220k C=100n;
opni Rg=1.5k Cg=150n Rf=22k+pot(2,250k,A) Cf=47p rail=3.8;
fmv R1=330k R2=1M R3=15k R4=100k C1=220p C2=100n C3=47n t=0 m=1 l=1;
dclip R=10k C=0 d=si up=2 dn=2;
hpf R=1M C=2.2n;
opni Rg=2.2k Cg=1u Rf=33k+pot(2,250k,A) Cf=100p rail=3.8;
shelf1 R=5.6k Cs=6.8n Cp=5.6n;
""" + boss_tone(10e3, 18e-9, 18e-9, ["taper(1,B)"]) + """;
vol a=taper(0,A);
peak f=120 Q=2.5 g=6.5
"""
BD2_NOTE = ("Due op-amp discreti con GAIN 250kA doppio (15.7-182x e 16-130x, il secondo satura sul rail 8 V), "
            "stack Fender fisso 330k/220p-100k-100n-47n-1M-15k (Yeh, verificato nodale), diodi 1SS133 2+2, "
            "shelf 5.6k//6.8n, tono passivo 18n/10kB/18n e gyrator +6.5 dB a 120 Hz (schema di fabbrica, kanengomibako).")

# ================================================================== OD-2 / OD-2R Turbo
def od2_net(r=False):
    """Due canali commutati dal selettore TURBO (indice 3): ogni stadio dell'altro canale diventa trasparente."""
    t = 3
    turbo_rf = ("4.7k*sw(3,0,1)+" if r else "") + "pot(2,250k,A)*sw(3,0,1)+1"
    net = """
    hpf R=1M C=22n;
    hpf R=100k C=18n;
    opni Rg=4.7k Cg=100n Rf=%s Cf=100p rail=%s railn=%s;
    hpf R=100k C=%s;
    lpf R=10k C=%s;
    hpf R=100k C=%s;
    opni Rg=4.7k Cg=150n Rf=%s Cf=100p rail=%s railn=%s;
    gain a=%s;
    shelf1 R=10k Cs=4.7n Cp=%s;
    fbclip Rg=1k Cg=220n Rf=pot(2,250k,A)*sw(3,1,0)+10 Cf=47p d=si up=1 dn=2 rail=3.9;
    lpf R=10k C=%s;
    """ % (turbo_rf, _sw(t, [9, 2.9]), _sw(t, [9, 2.5]),
           _sw(t, [100e-6, 22e-9]), _sw(t, [1e-12, 6.8e-9]), _sw(t, [100e-6, 10e-9 if r else 18e-9]),
           _sw(t, [1, 270e3]), _sw(t, [9, 2.9]), _sw(t, [9, 2.5]),
           _sw(t, [1, 0.31 if r else 0.24]), _sw(t, [0, 10e-9]), _sw(t, [15e-9, 1e-12]))
    net += boss_tone(10e3, 18e-9, 27e-9 if r else 22e-9, ["taper(1,B)"]) + ";\n"
    if r:
        net += "gain a=7.4; rail rail=4;\n"
    net += "vol a=taper(0,A)"
    return net


OD2_CTRL = [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive"),
            selector("TURBO", "mode", ["OFF", "ON"], 0, short=["OFF", "ON"])]

# ================================================================== SD-1W (Waza) S/C
SD1W_NET = """
hpf R=1M C=47n;
hpf R=100k C=18n;
fbclip Rg=sw(3,4.7k,3.2k) Cg=sw(3,47n,80n) Rf=33k+pot(2,1M,B) Cf=10p d=si up=1 dn=2 rail=3.8;
opni Rg=47k Cg=1u Rf=sw(3,1,10k) Cf=15n rail=3.8;
tonets R7=10k C4=18n P=20k t=taper(1,W) Rw=470 Cw=27n Rf=10k Cf=10n rail=3.8;
vol a=taper(0,B)
"""

# ================================================================== BD-2W (Waza) S/C
BD2W_NET = """
hpf R=1M C=1u;
hpf R=220k C=100n;
opni Rg=1.5k Cg=150n Rf=22k+pot(2,250k,A) Cf=47p rail=3.8;
fmv R1=330k R2=sw(3,1M,9.9k) R3=15k R4=100k C1=220p C2=100n C3=47n t=0 m=1 l=1;
dclip R=10k C=0 d=si up=2 dn=2;
hpf R=1M C=sw(3,2.2n,102.2n);
opni Rg=2.2k Cg=1u Rf=33k+pot(2,250k,A) Cf=47p rail=3.8;
shelf1 R=5.6k Cs=6.8n Cp=sw(3,5.6n,10.3n);
""" + boss_tone(10e3, 18e-9, [18e-9, 40e-9], ["taper(1,B)"], 3) + """;
vol a=taper(0,A);
peak f=sw(3,120,131) Q=2.5 g=6
"""

# ================================================================== OD-3
OD3_NET = """
hpf R=1M C=47n;
hpf R=100k C=18n;
peak f=500 Q=1 g=-4.5;
fbclip Rg=470 Cg=470n Rf=3.3k+pot(2,100k,A) Cf=22p d=si up=1 dn=1 rail=3.8;
gain db=-12.1;
lpf R=25k C=1.2n;
bjt g=3.5 vp=0.75 vn=0.65 soft=2.5 inv=1;
lpf R=4.7k C=12n;
opni Rg=1k Cg=10u Rf=6.8k Cf=0 rail=3.8;
gain a=0.76;
peak f=1600 Q=0.8 g=-4.4;
""" + boss_tone(10e3, 33e-9, 22e-9, ["taper(1,B)"]) + """;
vol a=taper(0,B)
"""

# ================================================================== OS-2 (miscela OD <-> DS)
# COLOR (indice 2) = c. Il ramo OD si 'apre' come (1-c)^2 e alimenta il ramo DS; mix finale (1-c) OD + c DS.
_C1 = "lin(2,1,0)*lin(2,1,0)"
OS2_NET = """
hpf R=1M C=47n;
hpf R=100k C=48n+-43.3n*%(c)s;
fbclip Rg=100 Cg=4.7u Rf=1k*%(c)s+pot(3,250k,A)*%(c)s+10 Cf=100p d=si up=1 dn=2 rail=4.2;
lpf R=20k C=10n*%(c)s+10p;
tap;
opni Rg=1.2k Cg=2.2u Rf=12k+pot(3,250k,A) Cf=100p rail=4.2;
dclip R=1k C=0 d=si;
tonebm R1=6.8k C1=100n R2=9k C2=18n t=0.4;
lpf R=20k C=820p;
mix wet=lin(2,0,1) dry=lin(2,1,0);
gain db=6;
tonets R7=100 C4=1p P=20k t=taper(1,B) Rw=4.7k Cw=10n Rf=10k Cf=1n rail=4.2;
vol a=taper(0,A)
""" % {"c": _C1}

# ================================================================== SD-2 (CRUNCH / LEAD)
# comandi: 0 LEVEL L, 1 LEVEL C, 2 TONE L, 3 TONE C, 4 DRIVE L, 5 DRIVE C, 6 MODE (0 crunch, 1 lead)
SD2_NET = """
hpf R=1M C=47n;
peak f=1050 Q=2.5 g=6;
opni Rg=4.7k Cg=82n Rf=pot(4,250k,A)*sw(6,0,1)+pot(5,250k,A)*sw(6,1,0) Cf=68p rail=4.2;
opinv Ri=15k Ci=68n Rf=sw(6,150k,1M) Cf=180p rail=4.2;
dclip R=1k C=0 d=led;
fbclip Rg=47k Cg=22n Rf=100k Cf=470p d=si up=1 dn=2 rail=4.2;
lpf R=2.2k C=15n;
shelf1 R=22k Cs=25.6n Cp=47n;
opinv Ri=33k Ci=0 Rf=100k Cf=220p rail=4.2;
""" + boss_tone(10e3, 100e-9, 27e-9, ["taper(2,B)*sw(6,0,1)", "taper(3,B)*sw(6,1,0)"]) + """;
vol a=taper(0,A)*sw(6,0,1)+taper(1,A)*sw(6,1,0)
"""

# ================================================================== ODB-3 (basso)
# 0 LEVEL, 1 HIGH, 2 LOW, 3 BALANCE, 4 GAIN
ODB3_NET = """
hpf R=1M C=47n;
hpf R=100k C=100n;
tap;
opni Rg=1.5k Cg=10u Rf=2.2k+pot(4,250k,A) Cf=330p rail=4.2;
fbclip Rg=1.8k Cg=100n Rf=100k Cf=330p d=led up=1 dn=1 rail=4.2;
gain a=0.35;
mix wet=lin(3,0,1) dry=lin(3,2,0);
hshelf f=4800 Q=0.7 g=lin(1,-15,15);
peak f=104 Q=1 g=lin(2,-15,15);
vol a=taper(0,A)
"""

# ================================================================== FA-1 FET Amplifier
FA1_NET = """
hpf R=3.3M C=10n;
opinv Ri=15k Ci=sw(3,517n,47n) Rf=pot(0,320k,A) Cf=0 rail=4.2;
lshelf f=150 Q=0.6 g=lin(2,-15,15);
hshelf f=3000 Q=0.6 g=lin(1,-15,15);
hpf R=10k C=10u
"""

# ================================================================== DN-2 Dyna Drive (dalle specifiche)
DN2_NET = """
hpf R=1M C=47n;
hpf R=100k C=15n;
opni Rg=2.2k Cg=100n Rf=10k+pot(2,220k,A) Cf=100p rail=3.8;
dclip R=2.2k C=4.7n d=led up=1 dn=1;
bjt g=1.5 vp=2.2 vn=1.8 soft=3 inv=1;
peak f=800 Q=0.7 g=3;
lpf R=10k C=4.7n;
""" + boss_tone(10e3, 22e-9, 22e-9, ["taper(1,B)"]) + """;
vol a=taper(0,A)*2
"""

# ================================================================== BC-2 Combo Drive (dalle specifiche)
BC2_NET = """
hpf R=1M C=22n;
opni Rg=4.7k Cg=100n Rf=1k+pot(3,470k,A) Cf=47p rail=3.8;
bjt g=2 vp=1.6 vn=2.4 soft=2 inv=1;
fmv R1=250k R2=1M R3=25k R4=56k C1=250p C2=20n C3=20n t=taper(2,B) m=0.5 l=taper(1,A);
gain db=18;
bjt g=1.5 vp=2.5 vn=2 soft=2.5 inv=1;
lpf2 f=5500 Q=0.7;
vol a=taper(0,A)
"""

# ================================================================== OD-1X (MDP, dalle misure)
OD1X_NET = """
hpf2 f=75 Q=0.9;
opni Rg=10k Cg=0 Rf=pot(3,22k,A) Cf=0 rail=3.8;
fbclip Rg=4.7k Cg=47n Rf=10k+pot(3,470k,A) Cf=47p d=si up=1 dn=2 rail=3.8;
peak f=100 Q=1.2 g=lin(1,-20,13);
peak f=4000 Q=1.5 g=-3;
peak f=7000 Q=0.9 g=lin(2,-15,12);
lpf2 f=10000 Q=0.7;
vol a=taper(0,A)*1.5
"""

# ================================================================== BB-1X (MDP basso, dalle specifiche)
# 0 LEVEL, 1 BLEND, 2 LOW, 3 HIGH, 4 DRIVE
BB1X_NET = """
hpf R=1M C=47n;
tap;
hpf R=100k C=47n;
opni Rg=2.2k Cg=4.7u Rf=4.7k+pot(4,470k,A) Cf=100p rail=3.8;
fbclip Rg=10k Cg=1u Rf=47k Cf=220p d=si up=1 dn=2 rail=3.8;
lpf2 f=4500 Q=0.7;
mix wet=lin(1,0,1) dry=lin(1,3,0);
lshelf f=100 Q=0.7 g=lin(2,-15,15);
hshelf f=3000 Q=0.7 g=lin(3,-15,15);
vol a=taper(0,A)
"""

# ================================================================== FB-2 Feedbacker/Booster (dalle specifiche)
# 0 BOOST, 1 FEEDBACK, 2 TONE, 3 CHARACTER
FB2_NET = """
hpf R=1M C=47n;
opni Rg=10k Cg=1u Rf=pot(0,100k,A) Cf=100p rail=4;
peak f=800 Q=0.7 g=lin(3,0,12);
hshelf f=2500 Q=0.7 g=lin(2,-10,10);
peak f=1500 Q=8 g=lin(1,0,12);
bjt g=1 vp=3 vn=3 soft=4 inv=0
"""

# ================================================================== BP-1W (dalle specifiche)
# 0 LEVEL, 1 GAIN, 2 MODE (NAT/RE/CE), 3 BUFFER (STD/VTG)
BP1W_NET = """
hshelf f=4000 Q=0.7 g=sw(3,0,-3);
hpf R=1M C=47n;
opni Rg=4.7k Cg=10u Rf=pot(1,100k,A) Cf=47p rail=4.2;
hpf2 f=sw(2,20,40,120) Q=0.6;
lshelf f=150 Q=0.7 g=sw(2,0,3,-2);
hshelf f=3500 Q=0.7 g=sw(2,0,-3,3);
bjt g=1 vp=sw(2,6,2.2,3.5) vn=sw(2,6,1.8,3.5) soft=sw(2,3,2,2.5) inv=0;
lpf R=10k C=sw(2,1n,2.7n,1.5n);
vol a=taper(0,A)
"""


# ================================================================== JB-2 (due lati, 4 modi)
def jb2_net():
    """0 DRIVE A,1 DRIVE B,2 TONE A,3 TONE B,4 LEVEL A,5 LEVEL B,6 MODE (A, B, A>B, B>A).
    Il lato B e' presente due volte (prima e dopo A); ogni blocco spento diventa trasparente."""
    m = 6
    onA, preB, postB = [1, 0, 1, 1], [0, 0, 0, 1], [0, 1, 1, 0]

    def S(flags, on=1, off=0):
        return _sw(m, [on if f else off for f in flags])

    def side_b(fl):
        s = S(fl)
        return """
        hpf R=100k C=%s;
        opni Rg=4.7k Cg=47n Rf=10k*%s+pot(1,500k,A)*%s+1 Cf=100p rail=%s;
        bjt g=1 vp=%s vn=%s soft=%s inv=0;
        peak f=700 Q=0.7 g=%s;
        hshelf f=1800 Q=0.6 g=lin(3,-12,8)*%s;
        lpf R=10k C=%s;
        gain a=taper(5,A)*2*%s+%s;
        """ % (S(fl, 22e-9, 100e-6), s, s, S(fl, 4.2, 20), S(fl, 0.65, 50), S(fl, 0.7, 50), S(fl, 4, 6),
               S(fl, 5, 0), s, S(fl, 4.7e-9, 1e-12), s, S(fl, 0, 1))

    sa = S(onA)
    K, cser = 0.5, 9e-9
    side_a = """
    opni Rg=1.5k Cg=150n Rf=22k*%(s)s+pot(0,250k,A)*%(s)s+1 Cf=47p rail=%(r)s;
    lshelf f=100 Q=0.5 g=%(ls)s;
    gain a=%(ga)s;
    hpf R=1M C=%(hc)s;
    opni Rg=2.2k Cg=1u Rf=33k*%(s)s+pot(0,250k,A)*%(s)s+1 Cf=100p rail=%(r)s;
    shelf1 R=5.6k Cs=6.8n Cp=%(cp)s;
    gain a=%(K)s;
    shelf1 R=10k Cs=18n*taper(2,B)*%(s)s Cp=%(cs)s+-18n*taper(2,B)*%(s)s;
    peak f=120 Q=2.5 g=%(pk)s;
    gain a=taper(4,A)*%(s)s+%(off)s;
    """ % dict(s=sa, r=S(onA, 3.8, 20), ls=S(onA, 14, 0), ga=S(onA, 0.14, 1), hc=S(onA, 2.2e-9, 100e-6),
               cp=S(onA, 5.6e-9, 0), K=S(onA, K, 1), cs=S(onA, cser, 0), pk=S(onA, 6.5, 0), off=S(onA, 0, 1))
    return "hpf R=1M C=47n;" + side_b(preB) + side_a + side_b(postB) + "rail rail=4.2"


# ================================================================== multi-modello (OD-20, OD-200)
def multi_net(sel, drive, low, tone_terms, level, attack, table, var=None, param=None, mid=None):
    """Catena unica con parametri per tipo (sw sul selettore 'sel').
    tap -> guadagno -> diodi a massa -> mix con il pre-clip (dry>0 = clipping 'in retroazione', morbido)."""
    col = lambda k: _sw(sel, [row[k] for row in table])
    hc = col("hpf") + ("*log(%d,2,0.5)" % param if param is not None else "")
    g = "pot(%d,470k,A)*%s" % (drive, col("g")) + ("*sw(%d,1,2)" % var if var is not None else "")
    net = """
    hpf R=100k C=%s;
    hshelf f=1200 Q=0.7 g=%s;
    tap;
    opni Rg=4.7k Cg=%s Rf=%s+1k Cf=47p rail=3.8;
    dclip R=2.2k C=%s d=si up=%s dn=%s;
    mix wet=1 dry=%s;
    lpf R=10k C=%s;
    peak f=%s Q=0.8 g=%s;
    lshelf f=120 Q=0.7 g=lin(%d,-12,12);
    """ % (hc, attack, col("cg"), g, col("dc"), col("up"), col("dn"), col("soft"), col("lp"), col("pf"), col("pg"), low)
    if mid is not None:
        net += "peak f=700 Q=0.7 g=lin(%d,-12,12);\n" % mid
    net += "hshelf f=2500 Q=0.7 g=%s;\n" % tone_terms
    net += "vol a=taper(%d,A)*2" % level
    return net


def _row(hpf, cg, g, dc, up, dn, soft, lp, pf, pg):
    return dict(hpf=hpf, cg=cg, g=g, dc=dc, up=up, dn=dn, soft=soft, lp=lp, pf=pf, pg=pg)


# tipi: OD(OD-1) TURBO(OD-2) BLUES(BD-2) DIST(DS-1) METAL(MT-2) LEAD STACK FUZZ
OD20_TABLE = [
    _row(4.7e-9, 47e-9, 0.5, 0, 1, 2, 1.0, 18e-9, 900, 2),
    _row(18e-9, 100e-9, 1.0, 0, 1, 1, 0.6, 10e-9, 1000, 1),
    _row(22e-9, 150e-9, 0.4, 0, 2, 2, 1.0, 4.7e-9, 500, -2),
    _row(2.6e-9, 1e-6, 1.0, 10e-9, 1, 1, 0.0, 6.8e-9, 600, -4),
    _row(15e-9, 1e-6, 2.0, 0, 1, 1, 0.0, 3.3e-9, 800, -8),
    _row(10e-9, 470e-9, 1.5, 4.7e-9, 1, 1, 0.2, 5.6e-9, 1100, 3),
    _row(33e-9, 1e-6, 0.8, 4.7e-9, 1, 2, 0.3, 4.7e-9, 700, 4),
    _row(100e-9, 10e-6, 2.0, 0, 1, 1, 0.0, 10e-9, 1000, -6),
]
OD20_TYPES = ["OD", "TURBO", "BLUES", "DIST", "METAL", "LEAD", "STACK", "FUZZ"]

# modi: OVERDRIVE BLUES SCREAM CNTR-OD X-DRIVE DIST STACK FUZZ
OD200_TABLE = [
    _row(4.7e-9, 47e-9, 0.5, 0, 1, 2, 1.0, 18e-9, 900, 2),
    _row(22e-9, 150e-9, 0.4, 0, 2, 2, 1.0, 4.7e-9, 500, -2),
    _row(1e-6 / 1e2, 47e-9, 0.5, 0, 1, 1, 1.0, 22e-9, 720, 4),
    _row(2.2e-9, 47e-9, 0.6, 0, 1, 1, 1.5, 12e-9, 1000, 3),
    _row(15e-9, 220e-9, 1.2, 4.7e-9, 1, 2, 0.4, 6.8e-9, 800, 1),
    _row(2.6e-9, 1e-6, 1.0, 10e-9, 1, 1, 0.0, 6.8e-9, 600, -4),
    _row(33e-9, 1e-6, 1.2, 4.7e-9, 1, 2, 0.3, 4.7e-9, 700, 4),
    _row(100e-9, 10e-6, 2.0, 0, 1, 1, 0.0, 10e-9, 1000, -6),
]
OD200_MODES = ["OVERDRIVE", "BLUES", "SCREAM", "CNTR OD", "X-DRIVE", "DIST", "STACK", "FUZZ"]


# ================================================================== catalogo
MODELS = [
    # ---------------------------------------------------------------- OD-1 e riedizione BOX-40
    model("od1", "SR-1", "Sunrise Drive", "BOSS OD-1 OverDrive", "Overdrive / Boost", "Circuit", YELLOW,
          OD1_CTRL, OD1_NET, OD1_NOTE, accent=(20, 20, 20), subtitle="OVERDRIVE"),
    model("od1_box40", "SR-1-40", "Sunrise Drive 40", "BOSS OD-1 (riedizione BOX-40)", "Overdrive / Boost",
          "Circuit", YELLOW, OD1_CTRL, OD1_NET,
          "Stesso circuito dell'OD-1 (riedizione del cofanetto 40 anni del 2017 con SP-1 e PH-1): 339 Hz in "
          "ingresso, 8-220x con diodi 1S2473 1:2 e filtro fisso a 884 Hz. Cambia solo l'estetica celebrativa.",
          accent=(150, 20, 20), subtitle="OVERDRIVE"),

    # ---------------------------------------------------------------- SD-1 e edizioni
    model("sd1", "HO-1", "Honey Overdrive", "BOSS SD-1 SUPER OverDrive", "Overdrive / Boost", "Circuit", YELLOW,
          SD1_CTRL, SD1_NET, SD1_NOTE, accent=(20, 20, 20), subtitle="OVERDRIVE"),
    model("sd1_4a", "HO-1-40", "Honey Overdrive 40", "BOSS SD-1-4A (40th Anniversary)", "Overdrive / Boost",
          "Circuit", BLACK, SD1_CTRL, SD1_NET,
          "Edizione 40 anni dell'SD-1 (2021): circuito identico (DRIVE 1M, diodi 1:2, tono attivo 20k W con 10n "
          "in retroazione); differisce solo l'enclosure scura con scritte dorate.",
          accent=(235, 190, 40), subtitle="OVERDRIVE"),
    model("sd1_b50a", "HO-1-50", "Honey Overdrive 50", "BOSS SD-1-B50A (50th Anniversary)", "Overdrive / Boost",
          "Circuit", (228, 186, 36), SD1_CTRL, SD1_NET,
          "Edizione 50 anni del marchio (2023): stessa netlist dell'SD-1 (88 Hz in ingresso, 8-220x, LPF 884 Hz, "
          "tono TS-like); cambia solo la grafica celebrativa.",
          accent=(120, 90, 20), subtitle="OVERDRIVE"),
    model("sd1_be", "HO-1-TG", "Honey Target", "BOSS SD-1 'bull's eye' (Ishibashi)", "Overdrive / Boost",
          "Circuit", YELLOW, SD1_CTRL, SD1_NET,
          "Esclusiva di un negozio giapponese con grafica a bersaglio: il circuito e' quello dell'SD-1 di serie "
          "(diodi 1S2473 asimmetrici, tono 20k W); differenza solo estetica.",
          accent=(200, 30, 30), subtitle="OVERDRIVE"),
    model("sd1w", "HO-1C", "Honey Overdrive Custom", "BOSS SD-1W Waza Craft", "Overdrive / Boost", "Circuit",
          YELLOW,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive"),
           toggle("MODE", "mode", ("S", "C"), 0)],
          SD1W_NET,
          "SD-1 a op-amp discreti su rail 8 V, stessi valori; in C la gamba di guadagno riceve 10k+33n in parallelo "
          "(fino a 324x, 50 dB, piu' bassi nel clipping) e un op-amp in piu' da +1.7 dB sotto 1 kHz "
          "(trace kanengomibako).",
          accent=(20, 20, 20), subtitle="OVERDRIVE"),

    # ---------------------------------------------------------------- OD-2 / OD-2R
    model("od2", "TT-2", "Twin Turbo Drive", "BOSS OD-2 Turbo OverDrive", "Overdrive / Boost", "Circuit", MUSTARD,
          OD2_CTRL, od2_net(False),
          "Due circuiti: Normal (op-amp discreto, 1k+220n, DRIVE 250kA 1-251x, diodi 1:2, LPF 1.06 kHz) e Turbo "
          "(due op-amp discreti a 5.6 V, 1-54x poi 58x fisso con saturazione sui rail, shelf -9.9 dB); tono passivo "
          "18n/10kB/22n (schema di fabbrica).",
          accent=(20, 20, 20), subtitle="TURBO OVERDRIVE"),
    model("od2r", "TT-2R", "Twin Turbo Drive R", "BOSS OD-2R Turbo OverDrive", "Overdrive / Boost", "Circuit",
          MUSTARD,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive"),
           selector("TURBO", "mode", ["OFF", "ON"], 0, short=["OFF", "ON"])],
          od2_net(True),
          "Come l'OD-2 ma con 4.7k in serie al DRIVE Turbo (2-55x), interstadio 10n (159 Hz) e partitore 0.31 "
          "(Aion Aurum), tono 18n/10kB/27n e recupero di volume a due transistor (x7.4).",
          accent=(20, 20, 20), subtitle="TURBO OVERDRIVE"),

    # ---------------------------------------------------------------- OS-2
    model("os2", "CB-2", "Color Blend Drive", "BOSS OS-2 OverDrive/Distortion", "Overdrive / Boost", "Circuit",
          (245, 165, 35),
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("COLOR", "blend"), knob("DRIVE", "drive")],
          OS2_NET,
          "Ramo OD (339 Hz, 100R+4.7u, 1k+DRIVE 250kA, diodi 1:2, LPF 796 Hz) e ramo DS stile RAT (12k+250kA su "
          "1.2k+2.2u, 1SS133 a massa, tono fisso 234 Hz/982 Hz) miscelati da COLOR, poi tono TS 20k con 4.7k+10n "
          "(trace kanengomibako). Approssimazione: il ramo DS e' alimentato dal ramo OD che si apre verso DS.",
          accent=(20, 20, 20), subtitle="OVERDRIVE/DISTORTION"),

    # ---------------------------------------------------------------- SD-2
    model("sd2", "CL-2", "Crunch Lead Dual", "BOSS SD-2 Dual OverDrive", "Overdrive / Boost", "Circuit",
          (238, 190, 40),
          [outer("LEVEL", "level"), inner("LEVEL CR", "level2"),
           outer("TONE", "tone"), inner("TONE CR", "tone2"),
           outer("DRIVE", "drive", 0.6), inner("DRIVE CR", "drive2", 0.4),
           selector("MODE", "mode", ["CRUNCH", "LEAD"], 1, short=["CRN", "LEAD"])],
          SD2_NET,
          "Canale Lead dal clone Aion Tachyon: gyrator a 1.05 kHz, GAIN 250kA (1-54x), invertente 66.7x che satura, "
          "LED a massa, diodi 1:2 in retroazione, shelf dei bassi 100-282 Hz, tono 100n/10kB/27n. Il Crunch "
          "(senza trace) e' modellato dalle specifiche con l'invertente a 10x.",
          accent=(20, 20, 20), subtitle="DUAL OVERDRIVE"),

    # ---------------------------------------------------------------- ODB-3 (basso)
    model("odb3", "LG-3", "Low End Grit", "BOSS ODB-3 Bass OverDrive", "Basso", "Circuit", (240, 205, 40),
          [knob("LEVEL", "level"), outer("HIGH", "high", 0.5, "db", -15, 15), inner("LOW", "low", 0.5, "db", -15, 15),
           knob("BALANCE", "blend"), knob("GAIN", "gain")],
          ODB3_NET,
          "Ramo drive con due op-amp discreti (2.5-169x, poi 56.6x con LED rossi in retroazione e LPF 4.8 kHz) "
          "miscelato col pulito da BALANCE; EQ attivo HIGH ~4.8 kHz e LOW a gyrator ~104 Hz (schema hobbistico "
          "schematicsonline, ramo pulito semplificato).",
          accent=(20, 20, 20), subtitle="BASS OVERDRIVE"),

    # ---------------------------------------------------------------- BD-2 e edizioni
    model("bd2", "CD-2", "Cobalt Driver", "BOSS BD-2 Blues Driver", "Overdrive / Boost", "Circuit", BLUE,
          BD2_CTRL, BD2_NET, BD2_NOTE, accent=(235, 235, 235), subtitle="BLUES OVERDRIVE"),
    model("bd2_b50a", "CD-2-50", "Cobalt Driver 50", "BOSS BD-2-B50A (50th Anniversary)", "Overdrive / Boost",
          "Circuit", (30, 80, 170), BD2_CTRL, BD2_NET,
          "Edizione 50 anni (2023) del BD-2: stessa netlist (due stadi discreti con GAIN doppio, stack fisso, "
          "gyrator a 120 Hz); cambiano solo tonalita' e grafica celebrativa.",
          accent=(230, 200, 110), subtitle="BLUES OVERDRIVE"),
    model("bd2_10m", "CD-2-10M", "Cobalt Driver Gold", "BOSS BD-2 10 million commemorative", "Overdrive / Boost",
          "Circuit", (205, 165, 70), BD2_CTRL, BD2_NET,
          "Esemplare commemorativo dei 10 milioni di compatti (non in vendita): circuito del BD-2 di serie, "
          "enclosure celebrativa dorata.",
          accent=(30, 60, 150), subtitle="BLUES OVERDRIVE"),
    model("bd2w", "CD-2C", "Cobalt Driver Custom", "BOSS BD-2W Waza Craft", "Overdrive / Boost", "Circuit", BLUE,
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("GAIN", "gain"),
           toggle("MODE", "mode", ("S", "C"), 0)],
          BD2W_NET,
          "BD-2 tutto discreto (Miller 47p). In C: 10k in parallelo al bass 1M dello stack e 100n sul 2.2n "
          "(bassi piu' stretti), tono con 40n a massa e 10.3n sullo shelf (piu' grasso), gyrator a 131 Hz "
          "(kanengomibako, PedalPCB).",
          accent=(235, 235, 235), subtitle="BLUES OVERDRIVE"),

    # ---------------------------------------------------------------- OD-3
    model("od3", "SO-3", "Sunny Overdrive", "BOSS OD-3 OverDrive", "Overdrive / Boost", "Circuit", (242, 205, 35),
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive")],
          OD3_NET,
          "Notch a T pontato (-4.5 dB a 500 Hz), op-amp discreto 470R+470n con DRIVE 100kA (8-220x) e 1SS133 "
          "simmetrici, JFET con 1N4004 gate-drain (secondo clipping morbido), M5218 con LPF 2.8 kHz, notch -4.4 dB "
          "a 1.6 kHz, tono 33n/10kB/22n (trace kanengomibako, Aion Heliodor).",
          accent=(20, 20, 20), subtitle="OVERDRIVE"),

    # ---------------------------------------------------------------- DN-2
    model("dn2", "TD-2", "Touch Dynamo", "BOSS DN-2 Dyna Drive", "Overdrive / Boost", "Circuit", (240, 120, 40),
          [knob("LEVEL", "level"), knob("TONE", "tone"), knob("DRIVE", "drive")],
          DN2_NET,
          "Modellato dalle specifiche (niente trace): guadagno moderato e soglia alta a LED, cosi' resta pulito "
          "con plettrata leggera e satura solo con attacco forte come la tecnologia 'Dyna Amp'; tono passivo BOSS.",
          accent=(20, 20, 20), subtitle="DYNAMIC DRIVE"),

    # ---------------------------------------------------------------- BC-2
    model("bc2", "CC-2", "Combo Crunch", "BOSS BC-2 Combo Drive", "Overdrive / Boost", "Circuit", (125, 30, 40),
          [knob("LEVEL", "level"), knob("BASS", "bass"), knob("TREBLE", "treble"), knob("SOUND", "gain")],
          BC2_NET,
          "Modellato dalle specifiche (combo valvolare): SOUND da pulito a drive (1-100x), saturazioni asimmetriche "
          "morbide tipo triodo, stack Fender 250k/1M/25k/56k con BASS e TREBLE, taglio d'altoparlante a 5.5 kHz.",
          accent=(235, 215, 170), subtitle="COMBO DRIVE"),

    # ---------------------------------------------------------------- OD-1X
    model("od1x", "PD-1X", "Prime Drive X", "BOSS OD-1X OverDrive", "Overdrive / Boost", "Circuit",
          (218, 170, 55),
          [knob("LEVEL", "level"), knob("LOW", "low", 0.5, "db", -20, 13), knob("HIGH", "high", 0.5, "db", -15, 12),
           knob("DRIVE", "drive")],
          OD1X_NET,
          "Pedale MDP digitale modellato dalle misure kanengomibako: taglio ripido sotto 80 Hz, DRIVE con ~40 dB di "
          "escursione e clipping asimmetrico (armoniche pari), LOW a campana su 100 Hz (-20/+13 dB), HIGH ~7 kHz "
          "con avvallamento a 4 kHz e passa-basso ripido oltre 10 kHz.",
          accent=(20, 20, 20), subtitle="OVERDRIVE"),

    # ---------------------------------------------------------------- BB-1X (basso)
    model("bb1x", "DD-1X", "Deep Drive X", "BOSS BB-1X Bass Driver", "Basso", "Circuit", (58, 60, 64),
          [knob("LEVEL", "level"), knob("BLEND", "blend"), knob("LOW", "low", 0.5, "db", -15, 15),
           knob("HIGH", "high", 0.5, "db", -15, 15), knob("DRIVE", "drive")],
          BB1X_NET,
          "Drive per basso MDP modellato dalle specifiche: ramo saturo (op-amp 4.7k+DRIVE e diodi asimmetrici, "
          "LPF 4.5 kHz) miscelato col segnale diretto da BLEND per non perdere le fondamentali; LOW/HIGH a mensola.",
          accent=(230, 60, 60), subtitle="BASS DRIVER"),

    # ---------------------------------------------------------------- JB-2
    model("jb2", "TW-2", "Twin Temper", "BOSS JB-2 Angry Driver (BOSS x JHS)", "Overdrive / Boost", "Circuit",
          (225, 225, 220),
          [outer("DRIVE A", "drive"), inner("DRIVE B", "drive2"),
           outer("TONE A", "tone"), inner("TONE B", "tone2"),
           outer("LEVEL A", "level"), inner("LEVEL B", "level2"),
           selector("MODE", "mode", ["A", "B", "A>B", "B>A"], 2, short=["A", "B", "A>B", "B>A"])],
          jb2_net(),
          "Modellato dalle specifiche: lato A = struttura BD-2 (due stadi discreti, stack fisso come mensola, tono "
          "passivo, gyrator 120 Hz), lato B = drive a op-amp stile Marshall con clipping duro e medie in evidenza; "
          "MODE: solo A, solo B, serie nei due ordini (parallelo e A/B a pedale non riprodotti).",
          accent=(30, 30, 30), subtitle="DUAL DRIVE"),

    # ---------------------------------------------------------------- OD-20
    model("od20", "DA-20", "Drive Arena", "BOSS OD-20 Drive Zone", "Overdrive / Boost", "Circuit", (225, 180, 40),
          [selector("TYPE", "mode", OD20_TYPES, 0, short=["OD", "TRB", "BLU", "DST", "MTL", "LEAD", "STK", "FUZ"]),
           knob("DRIVE", "drive"), knob("BOTTOM", "low", 0.5, "db", -12, 12), knob("TONE", "tone", 0.5, "db", -12, 12),
           knob("LEVEL", "level"), knob("ATTACK", "attack", 0.4, "db", -6, 9),
           toggle("VARIATION", "var", ("A", "B"), 0)],
          multi_net(0, 1, 2, "lin(3,-12,12)", 4, "lin(5,-6,9)", OD20_TABLE, var=6),
          "Multimodello COSM modellato dalle specifiche con gli stessi blocchi: per ogni tipo cambiano passa-alto "
          "d'ingresso, guadagno, soglia dei diodi, dosaggio clipping morbido/duro e voicing; VARIATION raddoppia il "
          "guadagno. 8 dei 22 tipi originali; HEAVY OCTAVE non riprodotto.",
          accent=(20, 20, 20), subtitle="DRIVE ZONE"),

    # ---------------------------------------------------------------- OD-200
    model("od200", "HF-200", "Hybrid Forge", "BOSS OD-200 Hybrid Drive", "Overdrive / Boost", "Circuit", BLACK,
          [selector("MODE", "mode", OD200_MODES, 0,
                    short=["OD", "BLUE", "SCRM", "CNTR", "X-DR", "DIST", "STCK", "FUZZ"]),
           knob("DRIVE", "drive"), knob("LOW", "low", 0.5, "db", -12, 12), knob("MIDDLE", "mid", 0.5, "db", -12, 12),
           knob("HIGH", "high", 0.5, "db", -12, 12), knob("LEVEL", "level"), knob("PARAM", "param")],
          multi_net(0, 1, 2, "lin(4,-12,12)", 5, "0", OD200_TABLE, param=6, mid=3),
          "Ibrido analogico/DSP modellato dalle specifiche: 8 dei 12 modi (OD, blues, TS, 'klon', X-drive, dist, "
          "stack, fuzz) come tabelle di parametri sugli stessi blocchi, EQ a 3 bande; PARAM sposta il passa-alto "
          "d'ingresso (stretto/grasso). Boost e memorie non riprodotti.",
          accent=(240, 120, 30), subtitle="HYBRID DRIVE"),

    # ---------------------------------------------------------------- FA-1
    model("fa1", "FL-1", "FET Lift", "BOSS FA-1 FET Amplifier", "Overdrive / Boost", "Circuit", (40, 40, 44),
          [knob("VOLUME", "gain"), knob("TREBLE", "treble", 0.5, "db", -15, 15),
           knob("BASS", "bass", 0.5, "db", -15, 15), toggle("LOW CUT", "mode", ("FLAT", "CUT"), 0)],
          FA1_NET,
          "Preamp: source follower JFET (4.8 Hz), low cut 47n su 15k (226 Hz, oppure 20 Hz in FLAT), HA1457 "
          "invertente con 470k // VOLUME 1MA (fino a 21x), Baxandall attivo BASS 50kB / TREBLE 50kB "
          "(clone Aion Prism).",
          accent=(230, 230, 230), subtitle="PREAMP"),

    # ---------------------------------------------------------------- FB-2
    model("fb2", "HB-2", "Howl Booster", "BOSS FB-2 Feedbacker/Booster", "Overdrive / Boost", "Circuit", SILVER,
          [knob("BOOST", "gain"), knob("FEEDBACK", "feedback", 0.3), knob("TONE", "tone"),
           knob("CHARACTER", "character", 0.3)],
          FB2_NET,
          "Modellato dalle specifiche: booster fino a +20 dB con CHARACTER da piatto a mid boost (800 Hz) e TONE a "
          "mensola. La generazione di feedback digitale non e' riproducibile: FEEDBACK regola un picco risonante "
          "stretto a 1.5 kHz che ne imita l'innesco.",
          accent=(200, 40, 40), subtitle="FEEDBACKER/BOOSTER"),

    # ---------------------------------------------------------------- BP-1W
    model("bp1w", "VP-1", "Vintage Preamp Boost", "BOSS BP-1W Booster/Preamp Waza Craft", "Overdrive / Boost",
          "Circuit", SILVER,
          [knob("LEVEL", "level", 0.7), knob("GAIN", "gain", 0.3),
           selector("MODE", "mode", ["NAT", "RE", "CE"], 0, short=["NAT", "RE", "CE"]),
           toggle("BUFFER", "buffer", ("STD", "VTG"), 0)],
          BP1W_NET,
          "Modellato dalle specifiche (nessun trace): NAT boost pulito, RE preamp da eco a nastro (bassi pieni, alti "
          "morbidi, saturazione compressiva), CE preamp da chorus (bassi stretti, brillante); GAIN fino a +27 dB. "
          "BUFFER VTG simula il carico da 100 kOhm sul pickup.",
          accent=(30, 30, 30), subtitle="BOOSTER/PREAMP"),
]
