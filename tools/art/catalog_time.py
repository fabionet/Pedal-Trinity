from catalog import model, knob, outer, inner, selector, toggle, slider, button

"""
Pedal Trinity - gruppo "time": delay analogici (BBD), delay digitali, eco a nastro, riverberi e looper.
Fonti: docs/research/digital_delay_reverb_amp.json/.md (tempi per modo dai manuali), circuits_modulation.json
(DM-2/DM-3/DM-2W: stadi BBD, clock, filtri), boss_catalog.json (comandi, colori).
Nota: nomi e sigle sono originali; il riferimento e' solo in 'inspired' e nelle note.
"""

WHITE = (225, 225, 220)
RED = (200, 40, 40)
BLACK = (30, 30, 33)
SILVER = (196, 199, 204)

# ---------------------------------------------------------------------------------------------- mode tables
# Delay digitali: Nome:minms:maxms:flag (nomi senza spazi, come vuole il parser del motore)
DD2_MODES = "S:12.5:50|M:50:200|L:200:800|HOLD:200:800:hold"
DD2_CH, DD2_SH = ["S 50ms", "M 200ms", "L 800ms", "HOLD"], ["S", "M", "L", "HOLD"]

# RE-201 / RE-2: combinazioni di testine (H1 H2 H3) e molla (r)
RE2_MODES = "100|010|001|011|100r|010r|001r|110r|011r|101r|111r"
RE2_CH = ["H1", "H2", "H3", "H2+H3", "H1+REV", "H2+REV", "H3+REV", "H1+H2+REV", "H2+H3+REV", "H1+H3+REV",
          "H1+H2+H3+REV"]
RE2_SH = ["1", "2", "3", "23", "1R", "2R", "3R", "12R", "23R", "13R", "123R"]


def looper(id, code, name, inspired, colour, label, maxs, notes, stereo=True, subtitle="LOOPER", accent=None):
    """Looper: pomello di livello + REC/PLAY, STOP, CLEAR (le tre funzioni principali)."""
    return model(id, code, name, inspired, "Tuner / Looper", "Looper", colour,
                 [knob(label, "level", 0.5), button("REC/PLAY", "rec"), button("STOP", "stop"),
                  button("CLEAR", "clear")],
                 "maxs=%d" % maxs, notes, subtitle=subtitle, stereo=stereo, accent=accent)


def dd_model(id, code, name, inspired, notes, **kw):
    """DD-2 / DD-3: quattro posizioni S/M/L/HOLD, banda ripetizioni 40 Hz-7 kHz."""
    return model(id, code, name, inspired, "Delay", "DigitalDelay", WHITE,
                 [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("D.TIME", "time", 0.5),
                  selector("MODE", "mode", DD2_CH, 2, DD2_SH)],
                 "modes=%s bw=7000 fbmax=1.0" % DD2_MODES, notes, subtitle="DIGITAL DELAY", **kw)


MODELS = [
    # ================================================================== DELAY ANALOGICI (BBD)
    model("dm1", "KD-1", "Bucket Machine", "BOSS DM-1 Delay Machine", "Delay", "AnalogDelay",
          (170, 172, 176),
          [knob("DLY TIME", "time", 0.5, "ms", 20, 300), knob("INTENSITY", "feedback", 0.35),
           knob("DLY VOLUME", "level", 0.5)],
          "type=delay stages=4096 clk=6827,102400 aa=3300 comp=1 fbmax=1.05 sat=1",
          "Delay analogico da pavimento a BBD da 4096 stadi con compander; ritardo = stadi/(2*clock). "
          "Gamma e filtri non documentati: si usano quelli del successore DM-2 (20-300 ms, banda ~3.3 kHz) - inferenza.",
          accent=(20, 20, 20), subtitle="ANALOG DELAY"),

    model("dm2", "KD-2", "Rose Echo", "BOSS DM-2 Delay", "Delay", "AnalogDelay",
          (200, 40, 70),
          [knob("REP. RATE", "time", 0.5, "ms", 20, 300), knob("ECHO", "level", 0.5),
           knob("INTENSITY", "feedback", 0.35)],
          "type=delay stages=4096 clk=6827,102400 aa=3300 comp=1 fbmax=1.1 sat=1",
          "BBD MN3005/MN3205 a 4096 stadi, clock 6.8-102 kHz = 20-300 ms, compander NE570 2:1/1:2 e pre/de-enfasi "
          "410 Hz/2.3 kHz; filtri anti-alias e ricostruzione del 3° ordine + SK (Q 5.4): banda wet -3 dB a 3.3 kHz "
          "con gobba a ~2 kHz, per questo auto-oscilla da INTENSITY a ore 12 (schema clone Aion).",
          accent=(20, 20, 20), subtitle="ANALOG DELAY"),

    model("dm3", "KD-3", "Crimson Echo", "BOSS DM-3 Delay", "Delay", "AnalogDelay",
          (150, 30, 40),
          [knob("REP. RATE", "time", 0.5, "ms", 20, 300), knob("ECHO", "level", 0.5),
           knob("INTENSITY", "feedback", 0.35), knob("TONE", "tone", 0.5)],
          "type=delay stages=4096 clk=6830,102000 aa=3300 comp=1 fbmax=1.1 sat=1",
          "BBD MN3205 + MN3102 (4096 stadi, clock 6.83-102 kHz = 20-300 ms), compander uPC1571C, stessi filtri "
          "del DM-2 (primo condensatore anti-alias 10n) e in piu' il controllo TONE sulle ripetizioni; uscite "
          "MAIN + DIRECT (schema BOSS DM-3).",
          text=(235, 230, 225), subtitle="ANALOG DELAY"),

    model("dm2w", "KD-2C", "Rose Echo Custom", "BOSS DM-2W Delay Waza Craft", "Delay", "AnalogDelay",
          (200, 40, 70),
          [knob("REP. RATE", "time", 0.5, "ms", 20, 800), knob("ECHO", "level", 0.5),
           knob("INTENSITY", "feedback", 0.35), toggle("MODE", "mode", ("S", "C"), 0)],
          "type=delay stages=4096 clk=2560,102400 aa=3300 comp=1 fbmax=1.05 sat=1",
          "Riedizione del DM-2: modo S come l'originale (20-300 ms), modo C (Custom) fino a 800 ms e piu' pulito; "
          "qui il clock copre 2.56-102 kHz (20-800 ms) con i filtri del DM-2. Uscite OUTPUT + DIRECT (manuale DM-2W).",
          accent=(20, 20, 20), subtitle="ANALOG DELAY"),

    model("dm101", "KD-101", "Analog Echo Lab", "BOSS DM-101 Delay Machine", "Delay", "AnalogDelay",
          (180, 182, 186),
          [selector("MODE", "mode", ["CLASSIC", "VINTAGE", "MODERN", "MULTI-HEAD", "NON-LINEAR", "AMBIENCE",
                                     "REFLECT", "DOUBLING+DLY", "WIDE", "DUAL MOD", "PAN", "PATTERN"], 0,
                    ["CLAS", "VINT", "MODN", "MHD", "NLIN", "AMB", "REFL", "DBL", "WIDE", "DMOD", "PAN", "PATT"]),
           knob("VARIATION", "variation", 0.5), knob("DLY TIME", "time", 0.5, "ms", 20, 1400),
           knob("INTENSITY", "feedback", 0.35), knob("DLY VOLUME", "level", 0.5),
           knob("MOD RATE", "rate", 0.3), knob("MOD DEPTH", "depth", 0.2)],
          "type=delay stages=16384 clk=5851,409600 rate=0.1,8 lfo=1 aa=4500 comp=1 fbmax=1.05 sat=1 stereo=1",
          "Delay analogico a piu' BBD con controllo digitale, 12 modi e memorie; qui modellato come catena di "
          "4x4096 stadi (20-1400 ms, gamma da verificare) con compander e filtri stile DM. Uscite stereo A/B.",
          accent=(20, 20, 20), subtitle="ANALOG DELAY", stereo=True),

    # ================================================================== DELAY DIGITALI
    dd_model("dd2", "QD-2", "Snow Echo", "BOSS DD-2 Digital Delay",
             "Primo delay digitale compatto (IC custom derivato dall'SDE-3000, 12 bit con compressione "
             "logaritmica): S 12.5-50, M 50-200, L 200-800 ms, HOLD 200-800 ms con feedback escluso; "
             "ripetizioni 40 Hz-7 kHz (manuale DD-2).", accent=(20, 20, 20)),

    model("dsd2", "QS-2", "Sample Echo", "BOSS DSD-2 Digital Sampler/Delay", "Delay", "DigitalDelay",
          (205, 215, 225),
          [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["DELAY S", "DELAY L", "REC/PLAY", "PLAY ONLY"], 1, ["S", "L", "REC", "PLAY"])],
          "modes=DelayS:50:200|DelayL:200:800|RecPlay:200:800:hold|PlayOnly:200:800:hold bw=7000 fbmax=1.0",
          "Delay digitale con campionatore (fino a 800 ms): DELAY S 50-200 ms, DELAY L 200-800 ms, SAMPLER "
          "REC-PLAY e PLAY ONLY (ripetizione del campione, qui come hold); banda come DD-2 (boss_catalog).",
          accent=(20, 60, 150), subtitle="SAMPLER/DELAY"),

    model("dsd3", "QS-3", "Sample Echo II", "BOSS DSD-3 Digital Sampler/Delay", "Delay", "DigitalDelay",
          (205, 215, 225),
          [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["DELAY S", "DELAY L", "REC/PLAY", "PLAY ONLY"], 1, ["S", "L", "REC", "PLAY"])],
          "modes=DelayS:50:200|DelayL:200:800|RecPlay:200:800:hold|PlayOnly:200:800:hold bw=7000 fbmax=1.0",
          "Evoluzione del DSD-2: delay S (fino a 200 ms) e L (fino a 800 ms) piu' campionatore con ingresso "
          "TRIG; minimi dei modi presi dal DSD-2 (inferenza).",
          accent=(150, 30, 40), subtitle="SAMPLER/DELAY"),

    dd_model("dd3", "QD-3", "Ice Delay", "BOSS DD-3 Digital Delay",
             "Stesse gamme del DD-2 (S 12.5-50 / M 50-200 / L 200-800 ms, HOLD 200-800 ms), conversione 12 bit "
             "companded, ripetizioni 40 Hz-7 kHz che si scuriscono a ogni giro; OUTPUT + DIRECT OUT (manuale DD-3).",
             accent=(20, 60, 150)),

    model("dd5", "QD-5", "Frost Delay", "BOSS DD-5 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("D.TIME", "time", 0.5),
           selector("MODE", "mode", ["50ms", "200ms", "800ms", "2000ms", "HOLD", "REVERSE", "E/D",
                                     "QUARTER", "DOT 8TH", "8TH", "TRIPLET"], 2,
                    ["50", "200", "800", "2K", "HOLD", "REV", "E/D", "1/4", "3/16", "1/8", "1/6"])],
          "modes=50ms:1:50|200ms:50:200|800ms:200:800|2000ms:800:2000|HOLD:50:2000:hold|REVERSE:1000:2000:reverse"
          "|E/D:1:400|Q:100:2000|Q.8:75:1500|8:50:1000|T4:67:1333 bw=14000 fbmax=1.0",
          "16 bit lineare a 32 kHz: modi 1-50, 50-200, 200-800, 800-2000 ms, HOLD 2 s, REVERSE 1-2 s, E/D 1-400 ms "
          "e 4 modi TEMPO (1/4, 1/8 puntata, 1/8, terzina = 1, 0.75, 0.5, 0.67 del tap) (manuale DD-5).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY"),

    model("dd6", "QD-6", "Glacier Delay", "BOSS DD-6 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("D.TIME", "time", 0.5),
           selector("MODE", "mode", ["80ms", "300ms", "800ms", "2600ms", "REVERSE", "HOLD", "WARP"], 2,
                    ["80", "300", "800", "2.6K", "REV", "HOLD", "WARP"])],
          "modes=80ms:1:80|300ms:80:300|800ms:300:800|2600ms:800:2600|REVERSE:300:2600:reverse|HOLD:50:5200:hold"
          "|WARP:20:800:warm bw=12000 fbmax=1.0",
          "Delay stereo fino a 5.2 s: modi 80/300/800/2600 ms (minimi non indicati dal manuale, qui contigui), "
          "REVERSE, HOLD 5.2 s sound-on-sound e WARP che spinge feedback e livello (manuale DD-6).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY", stereo=True),

    model("dd7", "QD-7", "Polar Delay", "BOSS DD-7 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35), knob("D.TIME", "time", 0.5),
           selector("MODE", "mode", ["50ms", "200ms", "800ms", "3200ms", "HOLD", "MODULATE", "ANALOG", "REVERSE"], 2,
                    ["50", "200", "800", "3.2K", "HOLD", "MOD", "ANLG", "REV"])],
          "modes=50ms:1:50|200ms:50:200|800ms:200:800|3200ms:800:3200|HOLD:50:40000:hold|MODULATE:20:800:mod"
          "|ANALOG:20:800:analog|REVERSE:300:3200:reverse bw=12000 fbmax=1.0",
          "Gamme 1-50, 50-200, 200-800, 800-3200 ms, MODULATE e ANALOG (modello DM-2) 20-800 ms, REVERSE "
          "300-3200 ms, HOLD 40 s; uscita Long che raddoppia i tempi (manuale DD-7).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY", stereo=True),

    model("te2", "QT-2", "Nebula Echo", "BOSS TE-2 Tera Echo", "Delay", "DigitalDelay",
          (215, 217, 220),
          [knob("E.LEVEL", "level", 0.5), knob("TONE", "tone", 0.5), knob("FEEDBACK", "feedback", 0.4),
           knob("S-TIME", "time", 0.5)],
          "modes=AMBIENT:40:1000:mod+warm+pan bw=9000 fbmax=0.95",
          "Eco ambientale MDP: multitap con spaziatura crescente e filtro sensibile all'inviluppo; TONE sposta la "
          "risonanza, S-TIME la lunghezza, pedale tenuto = freeze di ~1 s (manuale TE-2, Sound On Sound). "
          "Algoritmo proprietario: modello approssimato.",
          accent=(40, 110, 200), subtitle="AMBIENT ECHO", stereo=True),

    model("dd8", "QD-8", "Arctic Delay", "BOSS DD-8 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [knob("E.LEVEL", "level", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["STANDARD", "ANALOG", "TAPE", "WARM", "REVERSE", "+RV", "SHIM", "MOD",
                                     "WARP", "GLT", "LOOP"], 0,
                    ["STD", "ANLG", "TAPE", "WARM", "REV", "+RV", "SHIM", "MOD", "WARP", "GLT", "LOOP"]),
           toggle("CARRYOVER", "carryover", ("OFF", "ON"), 0)],
          "modes=STANDARD:20:800|ANALOG:20:800:analog|TAPE:20:800:tape|WARM:20:800:warm|REVERSE:300:5000:reverse"
          "|+RV:20:800:warm|SHIM:200:800:shimmer|MOD:20:800:mod|WARP:20:800:mod+warm|GLT:10:400"
          "|LOOP:50:40000:hold bw=12000 fbmax=1.0",
          "Undici modi: STANDARD/ANALOG/TAPE/WARM/+RV/MOD/WARP 20-800 ms, REVERSE 300-5000 ms, SHIM 200-800 ms, "
          "GLT 10-400 ms, LOOP 40 s mono; tap 67 ms-10 s e interruttore CARRYOVER (manuale DD-8).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY", stereo=True),

    model("dd3t", "QD-3T", "Ice Delay Tap", "BOSS DD-3T Digital Delay", "Delay", "DigitalDelay", WHITE,
          [knob("E.LEVEL", "level", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["S.50ms", "M.200ms", "L.800ms", "SHORT LOOP"], 2, ["S", "M", "L", "LOOP"])],
          "modes=S:12.5:50|M:50:200|L:200:800|LOOP:200:800:hold bw=7000 fbmax=1.0",
          "Voce del DD-3 (S 12.5-50, M 50-200, L 200-800 ms, banda 7 kHz) con tap da pedale o jack TEMPO "
          "(133-800 ms) e SHORT LOOP 200-800 ms mentre il pedale e' premuto (manuale DD-3T).",
          accent=(20, 60, 150), subtitle="DIGITAL DELAY"),

    model("sde3", "QE-3", "Twin Line Delay", "BOSS SDE-3 Dual Digital Delay", "Delay", "DigitalDelay",
          (45, 70, 80),
          [knob("TIME", "time", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("LEVEL", "level", 0.5),
           knob("DEPTH", "mod", 0.2), knob("RATE", "rate", 0.3), knob("HI CUT", "tone", 0.6),
           knob("OFFSET", "offset", 0.5), toggle("CARRYOVER", "carryover", ("OFF", "ON"), 0)],
          "modes=DUAL:1:1600:mod+warm bw=12000 fbmax=1.0",
          "Due delay paralleli in stile rack SDE-3000: 800 ms in stereo, 1600 ms in mono, OFFSET fra le linee, "
          "HI CUT e modulazione (recensione Guitar World, affidabilita' bassa).",
          text=(230, 232, 235), accent=(240, 200, 30), subtitle="DUAL DIGITAL DELAY", stereo=True),

    model("dd20", "QD-20", "Titan Delay", "BOSS DD-20 Giga Delay", "Delay", "DigitalDelay",
          (60, 62, 66),
          [selector("MODE", "mode", ["SMOOTH", "STANDARD", "ANALOG", "TAPE", "MODULATE", "DUAL", "REVERSE", "PAN",
                                     "WARP", "TWIST", "SOS"], 1,
                    ["SMTH", "STD", "ANLG", "TAPE", "MOD", "DUAL", "REV", "PAN", "WARP", "TWST", "SOS"]),
           knob("DELAY TIME", "time", 0.6), knob("E.LEVEL", "level", 0.5), knob("F.BACK", "feedback", 0.35),
           knob("TONE", "tone", 0.5)],
          "modes=SMOOTH:1:23000:warm|STANDARD:1:23000|ANALOG:1:23000:analog|TAPE:1:23000:tape|MODULATE:1:23000:mod"
          "|DUAL:1:23000:dual|REVERSE:1:23000:reverse|PAN:1:23000:pan|WARP:1:23000:warm|TWIST:1:23000:mod"
          "|SOS:1:23000:hold bw=14000 fbmax=1.0",
          "Delay twin 1 ms-23 s a passi di 1 ms con 11 modi (SMOOTH, TAPE modello RE-201, ANALOG modello DM-2, "
          "DUAL, PAN, MODULATE, REVERSE, SOS 23 s, TWIST, WARP); TONE piatto al centro (manuale DD-20).",
          text=(230, 232, 235), accent=(240, 200, 30), subtitle="DIGITAL DELAY", stereo=True),

    model("dd200", "QD-200", "Delay Lab", "BOSS DD-200 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [selector("MODE", "mode", ["STANDARD", "ANALOG", "TAPE", "DRUM", "SHIMMER", "AMBIENT", "PAD ECHO",
                                     "PATTERN", "LO-FI", "DUAL", "DUCKING", "REVERSE"], 0,
                    ["STD", "ANLG", "TAPE", "DRUM", "SHIM", "AMB", "PAD", "PATT", "LOFI", "DUAL", "DUCK", "REV"]),
           knob("TIME", "time", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("E.LEVEL", "level", 0.5),
           knob("PARAM", "param", 0.5), knob("TONE", "tone", 0.5), knob("MOD DEPTH", "mod", 0.2)],
          "modes=STANDARD:1:10000:mod|ANALOG:1:10000:analog+mod|TAPE:1:10000:tape|DRUM:1:10000:tape+warm"
          "|SHIMMER:1:10000:shimmer+mod|AMBIENT:1:10000:mod+warm+pan|PAD:1:10000:mod+warm|PATTERN:1:10000:pan+mod"
          "|LOFI:1:10000:lofi+mod|DUAL:1:10000:dual+mod|DUCKING:1:10000:mod|REVERSE:1:10000:reverse"
          " bw=16000 fbmax=1.0",
          "96 kHz / 32 bit, 12 modi con PARAM dedicato: TAPE (RE-201, 3 testine), DRUM (eco a tamburo magnetico, "
          "4 testine), SHIMMER, PATTERN a 16 tap, LO-FI, DUAL, DUCKING, REVERSE; fino a circa 10 s (manuale DD-200).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY", stereo=True),

    model("dd500", "QD-500", "Delay Lab Pro", "BOSS DD-500 Digital Delay", "Delay", "DigitalDelay", WHITE,
          [selector("MODE", "mode", ["STANDARD", "ANALOG", "TAPE", "VINTAGE DIG", "DUAL", "PATTERN", "REVERSE",
                                     "SFX", "SHIMMER", "FILTER", "SLOW ATTACK", "AMBIENT"], 0,
                    ["STD", "ANLG", "TAPE", "VDIG", "DUAL", "PATT", "REV", "SFX", "SHIM", "FILT", "SLOW", "AMB"]),
           knob("TIME", "time", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("E.LEVEL", "level", 0.5),
           knob("TONE", "tone", 0.5), knob("MOD DEPTH", "mod", 0.2)],
          "modes=STANDARD:1:10000:mod|ANALOG:1:10000:analog+mod|TAPE:1:10000:tape|VINTAGE:1:10000:warm+mod"
          "|DUAL:1:10000:dual+mod|PATTERN:1:10000:pan+mod|REVERSE:1:10000:reverse|SFX:1:10000:lofi+mod"
          "|SHIMMER:1:10000:shimmer+mod|FILTER:1:10000:mod+warm|SLOW:1:10000:mod|AMBIENT:1:10000:mod+warm+pan"
          " bw=16000 fbmax=1.0",
          "96 kHz / 32 bit fino a 10 s: ANALOG con numero di stadi BBD, TAPE (RE-201 a 1x/2x/3x), VINTAGE DIGITAL "
          "(SDE-3000/SDE-2000, TIMEx2 dimezza fs), DUAL, PATTERN 16 tap, SFX, FILTER, SLOW ATTACK (manuale DD-500).",
          accent=(20, 20, 20), subtitle="DIGITAL DELAY", stereo=True),

    model("sde3000d", "QE-3KD", "Rack Twin Delay", "BOSS SDE-3000D Dual Digital Delay", "Delay", "DigitalDelay",
          (40, 40, 44),
          [selector("MODE", "mode", ["NORMAL", "TIME x2", "DUAL"], 0, ["NORM", "X2", "DUAL"]),
           knob("TIME", "time", 0.5), knob("FEEDBACK", "feedback", 0.35), knob("OUT", "level", 0.5),
           knob("RATE", "rate", 0.3), knob("DEPTH", "mod", 0.2), knob("FILTER", "tone", 0.6)],
          "modes=NORMAL:1:1500:mod|TIMEx2:2:3000:mod+warm|DUAL:1:1500:mod+dual bw=13000 fbmax=1.0",
          "Due SDE-3000 da rack in un pedale: per ogni linea TIME, FEEDBACK, OUT, RATE, DEPTH e FILTER (high-cut) "
          "piu' fase invertibile; TIMEx2 dimezza la frequenza di campionamento e raddoppia il tempo (gamme stimate, "
          "da verificare sul manuale).",
          text=(230, 232, 235), accent=(240, 120, 30), subtitle="DUAL DIGITAL DELAY", stereo=True),

    model("sde3000evh", "QE-3KV", "Striped Twin Delay", "BOSS SDE-3000EVH Dual Digital Delay", "Delay",
          "DigitalDelay", (25, 25, 28),
          [selector("MODE", "mode", ["NORMAL", "TIME x2", "DUAL"], 2, ["NORM", "X2", "DUAL"]),
           knob("TIME", "time", 0.5), knob("FEEDBACK", "feedback", 0.3), knob("OUT", "level", 0.5),
           knob("RATE", "rate", 0.3), knob("DEPTH", "mod", 0.25), knob("FILTER", "tone", 0.6)],
          "modes=NORMAL:1:1500:mod|TIMEx2:2:3000:mod+warm|DUAL:1:1500:mod+dual bw=13000 fbmax=1.0",
          "Stessa elettronica dell'SDE-3000D con preset e routing di un celebre chitarrista (uscite DIRECT + EFX "
          "L/R, send/return); grafica a strisce rosse/bianche/nere. Gamme stimate come l'SDE-3000D.",
          text=(235, 235, 235), accent=(200, 30, 30), subtitle="DUAL DIGITAL DELAY", stereo=True),

    # ================================================================== ECO A NASTRO
    model("re2", "TK-2", "Tape Drift", "BOSS RE-2 Space Echo", "Eco a nastro", "TapeEcho",
          (35, 38, 36),
          [selector("MODE", "mode", RE2_CH, 3, RE2_SH),
           outer("REP. RATE", "time", 0.5), inner("WOW&FLUT", "wow", 0.3),
           outer("INTENSITY", "feedback", 0.4), inner("TONE", "treble", 0.5),
           outer("ECHO", "level", 0.5), inner("REVERB", "reverb", 0.3),
           toggle("CARRYOVER", "carryover", ("OFF", "ON"), 0)],
          "heads=1,2,3 maxms=600 minms=180 bw=5000 wow=0.002 modes=" + RE2_MODES,
          "Modello RE-201: testine a T, 2T, 3T (velocita' del nastro con REPEAT RATE, ~600 ms massimi su H3), "
          "11 combinazioni testine/molla, saturazione e wow & flutter; INTENSITY al massimo oscilla (manuale RE-2).",
          text=(225, 225, 215), accent=(120, 190, 120), subtitle="TAPE ECHO", stereo=True),

    model("re20", "TK-20", "Tape Drift Twin", "BOSS RE-20 Space Echo", "Eco a nastro", "TapeEcho",
          (45, 80, 55),
          [selector("MODE", "mode", RE2_CH + ["REV ONLY"], 3, RE2_SH + ["R"]),
           knob("REP. RATE", "time", 0.5), knob("INTENSITY", "feedback", 0.4), knob("ECHO VOL", "level", 0.5),
           knob("REVERB VOL", "reverb", 0.3), knob("BASS", "bass", 0.5), knob("TREBLE", "treble", 0.5),
           knob("INPUT VOL", "sat", 0.4)],
          "heads=1,2,3 maxms=600 minms=180 bw=5000 wow=0.0025 modes=" + RE2_MODES + "|000r",
          "COSM del RE-201: selettore a 12 posizioni (1-4 solo eco, 5-11 eco + molla, 12 solo riverbero), BASS/TREBLE "
          "sull'eco, INPUT VOLUME che satura il nastro; tap fino a 3 s su H3 (manuale RE-20).",
          text=(225, 225, 215), accent=(230, 230, 220), subtitle="TAPE ECHO", stereo=True),

    model("re202", "TK-202", "Tape Drift Deluxe", "BOSS RE-202 Space Echo", "Eco a nastro", "TapeEcho",
          (30, 55, 40),
          [selector("MODE", "mode", ["H1", "H2", "H3", "H1+H2", "H2+H3", "H1+H3", "H1+H2+H3", "H1+H4", "H3+H4",
                                     "H1+H3+H4", "H1+H2+H4", "ALL"], 4,
                    ["1", "2", "3", "12", "23", "13", "123", "14", "34", "134", "124", "ALL"]),
           knob("REP. RATE", "time", 0.5), knob("INTENSITY", "feedback", 0.4), knob("ECHO VOL", "level", 0.5),
           knob("REVERB VOL", "reverb", 0.3), knob("BASS", "bass", 0.5), knob("TREBLE", "treble", 0.5),
           outer("SATURATION", "sat", 0.4), inner("WOW&FLUT", "wow", 0.3),
           toggle("TAPE", "tape", ("NEW", "AGED"), 0)],
          "heads=1,2,3,4 maxms=800 minms=240 bw=5000 wow=0.002 "
          "modes=1000r|0100r|0010r|1100r|0110r|1010r|1110r|1001r|0011r|1011r|1101r|1111r",
          "Space Echo a 4 testine (T, 2T, 3T, 4T) e 12 modi, riverbero separato (molla, hall, plate, room), "
          "SATURATION preamp + nastro, WOW & FLUTTER, nastro nuovo/usato; 48 kHz / 32 bit float (manuale RE-202).",
          text=(225, 225, 215), accent=(140, 200, 80), subtitle="TAPE ECHO", stereo=True),

    # ================================================================== RIVERBERI
    model("rv2", "RY-2", "Night Hall", "BOSS RV-2 Digital Reverb", "Riverbero", "Reverb",
          (58, 60, 66),
          [knob("E.LEVEL", "level", 0.5), knob("PRE-EQ", "tone", 0.5), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["ROOM", "HALL 1", "HALL 2", "PLATE", "DELAY", "GATE"], 1,
                    ["ROOM", "HL1", "HL2", "PLT", "DLY", "GATE"])],
          "modes=ROOM:room|HALL1:hall|HALL2:hall|PLATE:plate|DELAY:delay|GATE:gate decay=0.3,6 predelay=10 bw=10000",
          "Primo riverbero digitale compatto: 31.25 kHz, 12 bit lineare, banda 30 Hz-10 kHz; ROOM (8 m2), HALL 1 "
          "(15 m2), HALL 2 (30 m2), PLATE, DELAY ping-pong, GATE; uscite A/B (manuale RV-2).",
          text=(230, 232, 235), accent=(40, 120, 200), subtitle="DIGITAL REVERB", stereo=True),

    model("rv3", "RY-3", "Night Hall & Echo", "BOSS RV-3 Digital Reverb/Delay", "Riverbero", "Reverb",
          (50, 52, 58),
          [knob("BALANCE", "level", 0.5), knob("TONE/F.BK", "tone", 0.5), knob("R/D.TIME", "time", 0.5),
           selector("MODE", "mode", ["DELAY 1", "DELAY 2", "DELAY 3", "DLY+ROOM1", "DLY+ROOM2", "DLY+HALL",
                                     "DLY+PLATE", "ROOM 1", "ROOM 2", "HALL", "PLATE"], 9,
                    ["D1", "D2", "D3", "DR1", "DR2", "DH", "DP", "R1", "R2", "H", "P"])],
          "modes=DELAY1:delay|DELAY2:delay|DELAY3:delay|DROOM1:room|DROOM2:room|DHALL:hall|DPLATE:plate"
          "|ROOM1:room|ROOM2:room|HALL:hall|PLATE:plate decay=0.3,8 predelay=10 bw=12000",
          "Riverbero + delay a 32 kHz / 16 bit: modi 1-3 delay 32-125, 125-500, 500-2000 ms; 4-7 delay 32-1000 ms "
          "+ riverbero a tempo fisso; 8-11 ROOM1/ROOM2/HALL/PLATE; comandi a doppia funzione (manuale RV-3).",
          text=(230, 232, 235), accent=(240, 200, 30), subtitle="DIGITAL REVERB/DELAY", stereo=True),

    model("rv5", "RY-5", "Slate Reverb", "BOSS RV-5 Digital Reverb", "Riverbero", "Reverb",
          (48, 52, 60),
          [knob("E.LEVEL", "level", 0.5), knob("TONE", "tone", 0.5), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["SPRING", "PLATE", "HALL", "ROOM", "GATE", "MODULATE"], 2,
                    ["SPR", "PLT", "HALL", "ROOM", "GATE", "MOD"])],
          "modes=SPRING:spring|PLATE:plate|HALL:hall|ROOM:room|GATE:gate|MODULATE:modulate decay=0.3,8 predelay=10"
          " bw=12000",
          "Riverbero stereo 2-in/2-out a 6 modi: SPRING (modello a due molle con interferenza), PLATE, HALL, ROOM, "
          "GATE e MODULATE (hall modulata); TONE caldo/brillante, tempi non pubblicati (manuale RV-5).",
          text=(230, 232, 235), accent=(40, 170, 220), subtitle="DIGITAL REVERB", stereo=True),

    model("frv1", "RY-63", "Walnut Spring 63", "BOSS FRV-1 '63 Fender Reverb", "Riverbero", "Reverb",
          (120, 80, 50),
          [knob("DWELL", "time", 0.5), knob("TONE", "tone", 0.5), knob("MIXER", "level", 0.4)],
          "modes=SPRING63:spring decay=1,4 predelay=0 bw=4500",
          "Modello dell'unita' di riverbero a valvole 6G15 del 1963: 12AT7 in ingresso, 6K6GT pilota su trasformatore, "
          "vasca a 3 molle lunghe, 7025 in recupero; DWELL = spinta nella molla (twang), MIXER = dry/wet (manuale FRV-1).",
          text=(235, 225, 205), accent=(235, 225, 205), subtitle="SPRING REVERB"),

    model("rv6", "RY-6", "Midnight Reverb", "BOSS RV-6 Reverb", "Riverbero", "Reverb",
          (35, 40, 58),
          [knob("E.LEVEL", "level", 0.5), knob("TONE", "tone", 0.5), knob("TIME", "time", 0.5),
           selector("MODE", "mode", ["ROOM", "HALL", "PLATE", "SPRING", "MODULATE", "+DELAY", "SHIMMER", "DYNAMIC"],
                    1, ["ROOM", "HALL", "PLT", "SPR", "MOD", "+DLY", "SHIM", "DYN"])],
          "modes=ROOM:room|HALL:hall|PLATE:plate|SPRING:spring|MODULATE:modulate|+DELAY:delay|SHIMMER:shimmer"
          "|DYNAMIC:dynamic decay=0.3,9 predelay=12 bw=12000",
          "Riverbero stereo a 8 modi: ROOM, HALL, PLATE, SPRING, MODULATE, +DELAY (TONE = feedback del delay), "
          "SHIMMER a ottava e DYNAMIC che si abbassa quando si suona forte; EXP = profondita' (manuale RV-6).",
          text=(230, 232, 235), accent=(90, 170, 240), subtitle="REVERB", stereo=True),

    model("rv200", "RY-200", "Abyss Verb", "BOSS RV-200 Reverb", "Riverbero", "Reverb",
          (25, 40, 80),
          [selector("MODE", "mode", ["ROOM", "HALL", "PLATE", "SPRING", "SHIMMER", "ARPVERB", "SLOWVERB",
                                     "MODULATE", "+DELAY", "LO-FI", "GATE", "REVERSE"], 1,
                    ["ROOM", "HALL", "PLT", "SPR", "SHIM", "ARP", "SLOW", "MOD", "+DLY", "LOFI", "GATE", "REV"]),
           knob("TIME", "time", 0.5, "ms", 100, 10000), knob("PRE-DELAY", "predelay", 0.1, "ms", 0, 200),
           knob("E.LEVEL", "level", 0.5), knob("PARAM", "param", 0.5), knob("LOW", "low", 0.5, "db", -12, 12),
           knob("HIGH", "tone", 0.5, "db", -12, 12)],
          "modes=ROOM:room|HALL:hall|PLATE:plate|SPRING:spring|SHIMMER:shimmer|ARPVERB:shimmer|SLOWVERB:reverse"
          "|MODULATE:modulate|+DELAY:delay|LOFI:lofi|GATE:gate|REVERSE:reverse decay=0.1,10 predelay=20 bw=14000",
          "96 kHz / 32 bit, 12 modi con PARAM dedicato (taglia della stanza, smorzamento plate, 1-3 molle, "
          "release shimmer, distorsione lo-fi...), TIME 0.1-10 s, LOW/HIGH e DENSITY (manuale RV-200).",
          text=(230, 232, 235), accent=(120, 190, 250), subtitle="REVERB", stereo=True),

    model("rv500", "RY-500", "Abyss Verb Pro", "BOSS RV-500 Reverb", "Riverbero", "Reverb",
          (25, 38, 75),
          [selector("MODE", "mode", ["ROOM", "HALL", "PLATE", "SPRING", "SHIMMER", "FAST DECAY", "EARLY REFL",
                                     "NON-LINEAR", "SFX", "DUAL", "VINTAGE HALL", "TAPE ECHO"], 1,
                    ["ROOM", "HALL", "PLT", "SPR", "SHIM", "FAST", "ER", "NLIN", "SFX", "DUAL", "VHL", "ECHO"]),
           knob("TIME", "time", 0.5, "ms", 100, 10000), knob("PRE-DELAY", "predelay", 0.1, "ms", 0, 200),
           knob("E.LEVEL", "level", 0.5), knob("LOW", "low", 0.5, "db", -24, 12),
           knob("HIGH", "tone", 0.5, "db", -24, 12)],
          "modes=ROOM:room|HALL:hall|PLATE:plate|SPRING:spring|SHIMMER:shimmer|FASTDECAY:room|EARLYREF:room"
          "|NONLINEAR:gate|SFX:lofi|DUAL:hall|VHALL:plate|TAPEECHO:delay decay=0.1,10 predelay=20 bw=14000",
          "96 kHz / 32 bit, 12 modi tra cui modello del riverbero digitale SRV-2000 e dell'eco a nastro RE-201; "
          "TIME 0.1-10 s (ER 0.1-1 s), PRE-DELAY 0-200 ms, low cut 20-800 Hz, EQ -24..+12 dB (manuale RV-500).",
          text=(230, 232, 235), accent=(120, 190, 250), subtitle="REVERB", stereo=True),

    # ================================================================== LOOPER
    looper("rc2", "LQ-2", "Phrase Looper", "BOSS RC-2 Loop Station", RED, "PHRASE LVL", 60,
           "Primo looper compatto: 16 minuti in 11 frasi, loop quantize, ritmi guida, undo/redo, ingresso AUX "
           "(manuale RC-2). Nel plugin la memoria e' limitata a 60 s per istanza."),
    looper("rc3", "LQ-3", "Loop Keeper", "BOSS RC-3 Loop Station", RED, "LOOP LEVEL", 60,
           "Looper stereo compatto: circa 3 ore in 99 frasi, ritmi guida, auto-rec, WAV 44.1 kHz/16 bit via USB "
           "(manuale RC-3). Nel plugin 60 s per istanza."),
    looper("rc1", "LQ-1", "Loop Ring", "BOSS RC-1 Loop Station", RED, "LEVEL", 60,
           "Looper compatto stereo con anello di LED: circa 12 minuti, frase minima 0.25 s, overdub e undo/redo "
           "(manuale RC-1). Nel plugin 60 s per istanza."),
    looper("rc5", "LQ-5", "Loop Keeper Pro", "BOSS RC-5 Loop Station", RED, "LOOP LEVEL", 60,
           "Looper compatto con display: 1.5 h per traccia (13 h totali), 99 memorie, 57x2 ritmi, AD/DA 32 bit, "
           "MIDI (manuale RC-5). Nel plugin 60 s per istanza."),
    looper("rc1bk", "LQ-1K", "Loop Ring Black", "BOSS RC-1BK Loop Station (Black)", BLACK, "LEVEL", 60,
           "Edizione nera limitata del looper ad anello di LED: stessi dati dell'RC-1 (circa 12 min stereo, "
           "undo/redo). Nel plugin 60 s per istanza.", accent=(200, 40, 40)),
    looper("rc20", "LQ-20", "Twin Looper", "BOSS RC-20 Loop Station", RED, "LEVEL", 90,
           "Primo looper twin: pedali REC/PLAY/OVERDUB e STOP, frasi selezionabili, reverse, undo, ingressi "
           "strumento/mic/aux (boss_catalog). Nel plugin 90 s per istanza.", stereo=False),
    looper("rc20xl", "LQ-20X", "Twin Looper XL", "BOSS RC-20XL Loop Station", RED, "LEVEL", 90,
           "Evoluzione twin con 16 minuti in 11 frasi, reverse, loop quantize, guida ritmica e ingresso mic "
           "(manuale RC-20XL). Nel plugin 90 s per istanza."),
    looper("rc30", "LQ-30", "Dual Track Looper", "BOSS RC-30 Loop Station", RED, "LOOP LEVEL", 90,
           "Looper twin a due tracce sincronizzate: circa 3 ore in 99 frasi, LOOP FX, ritmi, ingresso XLR "
           "(manuale RC-30). Nel plugin 90 s per istanza."),
    looper("rc500", "LQ-500", "Dual Track Pro", "BOSS RC-500 Loop Station", BLACK, "LOOP LEVEL", 90,
           "Looper a due tracce in formato 500 con ingresso mic (phantom), ritmi, CTL/EXP e MIDI (boss_catalog). "
           "Nel plugin 90 s per istanza.", accent=(200, 40, 40)),
    looper("rc50", "LQ-50", "Triple Phrase Looper", "BOSS RC-50 Loop Station", (170, 172, 176), "LEVEL", 90,
           "Looper da pavimento a 3 frasi con pedali REC/PLAY/OVERDUB, STOP, UNDO/REDO e TAP TEMPO, uscite "
           "MAIN e SUB (boss_catalog). Nel plugin 90 s per istanza.", accent=(20, 20, 20)),
    looper("rc300", "LQ-300", "Loop Command", "BOSS RC-300 Loop Station", (40, 40, 44), "MASTER", 90,
           "Looper da pavimento a 3 tracce con effetti di loop e pedale d'espressione integrato (boss_catalog). "
           "Nel plugin 90 s per istanza.", accent=(200, 40, 40)),
    looper("rc10r", "LQ-10R", "Groove Looper", "BOSS RC-10R Rhythm Loop Station", BLACK, "LOOP LEVEL", 90,
           "Looper a due tracce con oltre 280 ritmi e 16 kit di batteria, circa 6 ore, WAV 44.1 kHz/32f "
           "(manuale RC-10R). Nel plugin 90 s per istanza.", accent=(230, 140, 40)),
    looper("rc600", "LQ-600", "Loop Commander Six", "BOSS RC-600 Loop Station", BLACK, "OUTPUT LVL", 90,
           "Looper da pavimento a 6 tracce con footswitch REC/PLAY/STOP per traccia, UNDO/REDO, ALL START/STOP, "
           "due ingressi mic (boss_catalog). Nel plugin 90 s per istanza.", accent=(200, 40, 40)),
]
