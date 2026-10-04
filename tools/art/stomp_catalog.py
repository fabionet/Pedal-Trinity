"""
Pedal Trinity - aiuti per i cataloghi dei pedali a scatola con la forma del pedale vero (MXR, Electro-Harmonix):
catalog_mxr.py e catalog_ehx.py descrivono l'ASPETTO (forma, misure, posizione dei comandi per etichetta,
colori, serigrafie, LED, footswitch); la PARTE FUNZIONALE (famiglia, comandi con ruoli/default/gamme, config,
note) viene da tools/art/sound_stage3_analog.json e sound_stage3_digital.json (tappa 3A) e da
sound_stage3b_analog.json / sound_stage3b_digital.json (tappa 3B) quando contengono l'id del pedale.
Finche' un id manca si usa un segnaposto costruito dai comandi della scheda (etichette e ordine reali,
famiglia e config provvisori), segnalato da placeholder=True nel modello.

Formato dei JSON (per id): {"family", "category", "controls": [{label, role, kind, default, units, lo, hi,
choices, short}], "config", "stereo", "notes"}.
"""
import json
import os

import catalog
from catalog import model

HERE = os.path.dirname(os.path.abspath(__file__))
SOUND_FILES = [os.path.join(HERE, "sound_stage3_analog.json"), os.path.join(HERE, "sound_stage3_digital.json"),
               os.path.join(HERE, "sound_stage3b_analog.json"), os.path.join(HERE, "sound_stage3b_digital.json")]
_SOUND = None
SOUND_ERRORS = []

CAT = {"fuzz": "Fuzz", "overdrive": "Overdrive / Boost", "boost": "Overdrive / Boost", "distorsione": "Distorsione",
       "compressore": "Dinamica", "eq": "Equalizzatori", "filtro": "Wah / Filtri", "wah": "Wah / Filtri",
       "synth": "Synth", "octave": "Pitch / Ottave", "pitch": "Pitch / Ottave", "chorus": "Chorus / Dimension",
       "flanger": "Flanger", "phaser": "Phaser", "tremolo": "Tremolo / Pan / Slicer", "vibrato": "Vibrato / Rotary",
       "rotary": "Vibrato / Rotary", "delay": "Delay", "reverb": "Riverbero", "looper": "Tuner / Looper",
       "utility": "Utility / Routing", "multi": "Chorus / Dimension"}

UNITS = {"dial", "db", "hz", "ms", "percent", "semitone", "choice"}
KINDS = {"knob", "outer", "inner", "selector", "toggle", "slider", "button"}


def sound():
    """Schede funzionali (unione dei due JSON); un file mancante o non valido vale come vuoto."""
    global _SOUND
    if _SOUND is None:
        _SOUND = {}
        for p in SOUND_FILES:
            if not os.path.exists(p):
                continue
            try:
                with open(p, encoding="utf-8") as f:
                    d = json.load(f)
            except Exception as e:          # file in scrittura o malformato: segnaposto
                SOUND_ERRORS.append("%s: %s" % (os.path.basename(p), e))
                continue
            if isinstance(d, dict):
                for k, v in d.items():
                    if isinstance(v, dict) and not k.startswith("_"):
                        _SOUND[k] = v
    return _SOUND


def _num(v, dflt):
    try:
        return float(v)
    except (TypeError, ValueError):
        return float(dflt)


ABBR = [("SENSITIVITY", "SENS"), ("FUNDAMENTAL", "FUNDAMENT."), ("FILTER", "FILT"), ("BYPASS", "BYP"),
        ("NORMAL", "NORM"), ("STRONG", "STR"), ("SQUARE WAVE", "SQUARE"), ("ATTACK", "ATK"), ("DELAY", "DLY"),
        ("VOLUME", "VOL"), ("LEVEL", "LVL"), ("FREQUENCY", "FREQ"), ("RESONANCE", "RES"), ("OCTAVE", "OCT"),
        ("MASTER", "MST"), ("FEEDBACK", "FDBK"), ("DEPTH", "DPTH")]


def short_label(s):
    """Etichette dei JSON piu' lunghe di 10 caratteri: abbreviazioni come sui pedali, poi troncamento."""
    s = str(s or "?").strip()
    for a, b in ABBR:
        if len(s) <= 10:
            break
        s = s.replace(a, b)
    return s[:10]


def json_control(c):
    """Comando del JSON funzionale -> dizionario del catalogo (come catalog._ctrl)."""
    kind = str(c.get("kind") or "knob").lower()
    if kind not in KINDS:
        kind = "knob"
    choices = c.get("choices")
    if isinstance(choices, str):
        choices = [x for x in choices.split("|") if x]
    choices = [str(x) for x in choices] if choices else None
    if kind == "toggle" and not choices:
        choices = ["OFF", "ON"]
    if kind == "selector" and not choices:
        kind = "knob"
    units = str(c.get("units") or ("choice" if kind in ("selector", "toggle") else "dial")).lower()
    if units not in UNITS:
        units = "dial"
    default = _num(c.get("default"), 0.5 if kind not in ("toggle", "button") else 0.0)
    if kind == "selector":
        n = len(choices)
        if default > 1.0:            # indice della posizione invece del valore normalizzato
            default = min(n - 1, max(0.0, default)) / max(1, n - 1)
        lo, hi = 0.0, float(n - 1)
    elif kind == "toggle":
        default = 1.0 if default >= 0.5 else 0.0
        lo, hi = 0.0, 1.0
    else:
        lo, hi = _num(c.get("lo"), 0.0), _num(c.get("hi"), 10.0)
    default = min(1.0, max(0.0, default))
    short = c.get("short")
    if isinstance(short, str):
        short = [x for x in short.split("|") if x]
    label = short_label(c.get("label"))
    return dict(kind=kind, label=label, role=str(c.get("role") or "level"), default=default, units=units, lo=lo, hi=hi,
                choices=choices, short=[str(x)[:4] for x in short] if short else None)


def foot_selectors(ctrls, look):
    """Selettori a 2 posizioni del JSON che sul pedale vero sono un footswitch di sezione (non quello dell'effetto):
    diventano levette (stessi valori 0/1, unita' 'choice') cosi' l'interfaccia li mette sul loro footswitch."""
    import stomp_layout
    feet = [f for f in (look.get("feet") or [])[(0 if look.get("sections") else 1):]]
    out = []
    for c in ctrls:
        if c["kind"] == "selector" and len(c.get("choices") or []) == 2:
            ck = stomp_layout.key(c["label"])
            for f in feet:
                names = [stomp_layout.key(f[0])] + [stomp_layout.key(a) for a in (f[3] if len(f) > 3 and f[3] else [])]
                if ck in names or (len(ck) >= 2 and names[0].startswith(ck)):
                    c = dict(c, kind="toggle", default=1.0 if c["default"] >= 0.5 else 0.0, lo=0.0, hi=1.0, short=None)
                    break
        out.append(c)
    return out


def stomp(id, code, name, inspired, real, category, colour, look, placeholder, notes, subtitle=None, text=None,
          accent=None, stereo=False):
    """Modello a scatola con la forma del pedale vero.
    placeholder = (famiglia, comandi, config) provvisori, usati finche' l'id manca nei JSON funzionali."""
    sd = sound().get(id)
    fam, ctrls, cfg = placeholder
    is_ph = True
    if sd:
        try:
            jc = [json_control(c) for c in sd.get("controls") or []]
            jf = sd.get("family")
            if jc and jf in catalog.FAMILIES:
                ctrls, fam, cfg = jc, jf, str(sd.get("config") or "")
                is_ph = False
                if sd.get("category") in catalog.CATEGORIES:
                    category = sd["category"]
                if sd.get("notes"):
                    notes = str(sd["notes"])
                if "stereo" in sd:
                    stereo = bool(sd["stereo"])
        except Exception as e:
            SOUND_ERRORS.append("%s: %s" % (id, e))
    if not is_ph:
        ctrls = foot_selectors(ctrls, look)
    m = model(id, code, name, inspired, category, fam, colour, ctrls, cfg, notes, style="box", text=text,
              accent=accent, subtitle=subtitle, stereo=stereo, look=look, real=dict(code=real[0], name=real[1]))
    m["placeholder"] = is_ph
    return m


def look(shape, dims, place, feet=None, leds=None, wedge=None, extra=None, finish="paint", gfx=None, knob=None,
         body=None, **kw):
    """dims = (larghezza, profondita', altezza) mm; place = [(etichetta, x%, y%, tipo), ...] (vedi stomp_layout.py);
    finish = paint | hammer | raw | brushed | satin | sparkle | matte; body = colore dei fianchi/fondo se diverso."""
    d = dict(shape=shape, stomp=True, dims=tuple(dims), place=[tuple(p) for p in place], feet=feet or [("ON/OFF", 50, 80)],
             leds=leds or [], wedge=wedge, extra=extra or [], finish=finish, gfx=gfx or {}, knob=knob, body=body)
    d.update(kw)
    return d


# ------------------------------------------------------------------ pedali a bilanciere (tappa 3B)
TREADLE_KEYS = ("PEDAL", "TREADLE", "ROCKER", "SENSOR", "WAH", "SWEEP", "VOLUME", "VOL", "PAN", "BEND", "EXP", "POSITION")
TREADLE_ROLES = ("freq", "pedal", "volume", "pan", "bend", "position", "sweep", "treadle", "expression")


def _treadle_first(ctrls, pedal):
    """Il bilanciere deve essere il primo comando (strip 254): lo si cerca tra i comandi del JSON (etichetta o
    ruolo da bilanciere) e lo si porta in testa; se manca si usa quello del segnaposto."""
    import stomp_layout
    best = None
    for k, c in enumerate(ctrls):
        if c["kind"] != "knob":
            continue
        lk = stomp_layout.key(c["label"])
        sc = 0
        if lk.startswith("PEDAL") or lk.startswith("TREADLE") or lk.startswith("ROCKER") or lk.startswith("SENSOR"):
            sc = 3
        elif c.get("role") in TREADLE_ROLES:
            sc = 2
        elif any(lk.startswith(w) for w in TREADLE_KEYS):
            sc = 1
        if sc and (best is None or sc > best[0]):
            best = (sc, k)
    if best is None:
        return ([pedal] + list(ctrls))[:12]
    k = best[1]
    return [ctrls[k]] + ctrls[:k] + ctrls[k + 1:]


def _treadle_place(ctrls, spec):
    """look["place"] per etichetta dei comandi effettivi: spec = [(etichetta, lato, pct, tipo[, alias]), ...] posizioni del
    pedale vero (lato "L"/"R" della fiancata, pct dal tallone). Assegnazione per somiglianza delle etichette, poi
    nell'ordine; i comandi senza posizione vanno sui posti liberi (alternando i lati)."""
    import stomp_layout
    entries = [(e[0], e[1], e[2], e[3], e[4] if len(e) > 4 else None) for e in spec]
    out, used = {}, set()
    pairs = []
    for i, c in enumerate(ctrls[1:], 1):
        for j, e in enumerate(entries):
            sc = stomp_layout._score(c["label"], (e[0], 0, 0, "knob", e[4]))
            if sc > 0:
                pairs.append((sc, -abs(i - j) * 0.001, i, j))
    pairs.sort(reverse=True)
    done = set()
    for sc, _, i, j in pairs:
        if i in done or j in used:
            continue
        e = entries[j]
        out[ctrls[i]["label"].upper()] = (e[1], e[2], e[3])
        done.add(i)
        used.add(j)
    free = [entries[j] for j in range(len(entries)) if j not in used]
    extra = []
    for i, c in enumerate(ctrls[1:], 1):
        if i in done:
            continue
        if free:
            e = free.pop(0)
            out[c["label"].upper()] = (e[1], e[2], e[3])
        else:
            extra.append(c)
    for k, c in enumerate(extra):
        side = "R" if k % 2 == 0 else "L"
        out[c["label"].upper()] = (side, 30 + 12 * (k // 2), "knob")
    return out


def rocker(id, code, name, inspired, real, category, colour, look, placeholder, notes, subtitle=None, stereo=False,
           place_spec=None):
    """Pedale a bilanciere (wah, volume, pan, pitch a pedale; anche la serie 'Next Step', che oscilla tutta intera)
    con la sagoma del pedale vero (shaped_layout.py, stile "treadle"). Parte funzionale come stomp(): JSON se c'e',
    altrimenti segnaposto; il bilanciere e' sempre il primo comando."""
    sd = sound().get(id)
    fam, ctrls, cfg = placeholder
    pedal = ctrls[0]
    is_ph = True
    if sd:
        try:
            jc = [json_control(c) for c in sd.get("controls") or []]
            jf = sd.get("family")
            if jc and jf in catalog.FAMILIES:
                ctrls, fam, cfg = _treadle_first(jc, pedal), jf, str(sd.get("config") or "")
                is_ph = False
                if sd.get("category") in catalog.CATEGORIES:
                    category = sd["category"]
                if sd.get("notes"):
                    notes = str(sd["notes"])
                if "stereo" in sd:
                    stereo = bool(sd["stereo"])
        except Exception as e:
            SOUND_ERRORS.append("%s: %s" % (id, e))
    lk = dict(look)
    lk["place"] = _treadle_place(ctrls, place_spec or [])
    m = model(id, code, name, inspired, category, fam, colour, ctrls, cfg, notes, style="treadle", subtitle=subtitle,
              stereo=stereo, look=lk, real=dict(code=real[0], name=real[1]))
    m["placeholder"] = is_ph
    return m


def treadle_look(shape, dims, leds=None, finish="crinkle", rocker=None, rocker_finish=None, tread=None, body_motif=None,
                 **extra):
    """Aspetto di un pedale a bilanciere (come catalog_wah.look): dims = (larghezza, lunghezza, altezza) mm;
    leds = [(lato, pct, colore)]; tread = (motivo, colore1, colore2) della gomma."""
    d = dict(shape=shape, dims=tuple(dims), place={}, leds=leds or [], finish=finish, rocker_colour=rocker,
             rocker_finish=rocker_finish or finish, tread=tread or ("ribs", (14, 14, 15), (14, 14, 15)),
             body_motif=body_motif)
    d.update(extra)
    return d
