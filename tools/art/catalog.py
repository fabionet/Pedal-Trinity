"""
Pedal Trinity - catalogo dei modelli (fonte unica).

Ogni gruppo di pedali sta in un file catalog_<gruppo>.py che definisce
MODELS = [ model(...), ... ] usando le funzioni di questo modulo.
Questo file li raccoglie, li valida e fornisce i dati a:
  make_catalog_textures.py, render_pedals.py, assemble_catalog.py

Regole (vedi CATALOG_SPEC.md):
  * nome e sigla sul pedale sono ORIGINALI (niente "BOSS", niente sigle BOSS);
    il riferimento al pedale ispiratore va solo in 'inspired'.
  * i comandi hanno etichette come sul pedale reale.
"""

import importlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

FAMILIES = ["Circuit", "Compressor", "NoiseGate", "GraphicEQ", "ParametricEQ", "Wah", "BBDChorus", "BBDFlanger",
            "Phaser", "Tremolo", "AnalogDelay", "DigitalDelay", "TapeEcho", "Reverb", "Pitch", "Synth", "Acoustic",
            "AmpSim", "CabIR", "Router", "Volume", "Tuner", "Looper", "SlowGear", "Splitter", "Nam"]

CATEGORIES = ["Overdrive / Boost", "Distorsione", "Fuzz", "Metal", "Basso", "Dinamica", "Equalizzatori",
              "Wah / Filtri", "Chorus / Dimension", "Flanger", "Phaser", "Tremolo / Pan / Slicer",
              "Vibrato / Rotary", "Delay", "Eco a nastro", "Riverbero", "Pitch / Ottave", "Synth", "Acustica",
              "Amp / IR", "Utility / Routing", "Tuner / Looper", "Splitter"]

UNITS = {"dial", "db", "hz", "ms", "percent", "semitone", "choice"}


# ------------------------------------------------------------------ comandi
def _ctrl(kind, label, role, default=0.5, units="dial", lo=0.0, hi=10.0, choices=None, short=None):
    return dict(kind=kind, label=label, role=role, default=float(default), units=units, lo=float(lo), hi=float(hi),
                choices=choices, short=short)


def knob(label, role, default=0.5, units="dial", lo=0, hi=10):
    """Pomello singolo."""
    return _ctrl("knob", label, role, default, units, lo, hi)


def outer(label, role, default=0.5, units="dial", lo=0, hi=10):
    """Anello esterno di un pomello concentrico (va seguito subito da inner())."""
    return _ctrl("outer", label, role, default, units, lo, hi)


def inner(label, role, default=0.5, units="dial", lo=0, hi=10):
    """Pomello interno del concentrico."""
    return _ctrl("inner", label, role, default, units, lo, hi)


def selector(label, role, choices, default=0, short=None):
    """Pomello a scatti. choices = lista di nomi; default = indice. short = sigle per la serigrafia."""
    n = len(choices)
    return _ctrl("selector", label, role, default / max(1, n - 1), "choice", 0, n - 1, choices, short)


def toggle(label, role, choices=("S", "C"), default=0):
    """Levetta a 2 posizioni."""
    return _ctrl("toggle", label, role, float(default), "choice", 0, 1, list(choices))


def slider(label, role, default=0.5, lo=-15, hi=15, units="db"):
    """Cursore verticale (EQ grafici)."""
    return _ctrl("slider", label, role, default, units, lo, hi)


def button(label, role):
    """Pulsante (looper: rec/stop/clear/undo)."""
    return _ctrl("button", label, role, 0.0)


# ------------------------------------------------------------------ modello
def model(id, code, name, inspired, category, family, colour, controls, config, notes,
          style="boss", text=None, accent=None, subtitle=None, stereo=False, look=None, real=None):
    """
    id        identificatore stabile (minuscolo, es. "ds1")
    code      sigla ORIGINALE mostrata sul pedale (es. "DX-1")
    name      nome ORIGINALE mostrato sul pedale (es. "Orange Crunch")
    inspired  pedale di riferimento (es. "BOSS DS-1") - solo descrittivo
    category  una delle CATEGORIES
    family    una delle FAMILIES
    colour    colore dell'enclosure (r, g, b)
    controls  lista di comandi (knob/outer/inner/selector/toggle/slider/button)
    config    netlist a stadi (Circuit/AmpSim) oppure "chiave=valore ..." (altre famiglie)
    notes     descrizione breve in italiano (1-3 frasi, include le fonti principali)
    style     "boss" (enclosure compatta), "ts" (tipo Tube Screamer), "nam" (contenitore grande NAM-A1A2),
              "treadle" (pedale a bilanciere: wah, volume, espressione) o "box" (scatola di misura reale)
    text      colore delle scritte (default automatico chiaro/scuro)
    accent    colore del nome sul pedale (default automatico)
    subtitle  sottotitolo sul pedale (default: categoria in maiuscolo)
    look      solo stili "treadle"/"box": forma, misure, posizione dei comandi e finiture (vedi shaped_layout.py)
    real      replica REAL MOD di un pedale non BOSS: dict(code=..., name=...) con sigla e nome reali
    """
    return dict(id=id, code=code, name=name, inspired=inspired, category=category, family=family,
                colour=tuple(colour), controls=controls, config=" ".join(config.split()), notes=notes,
                style=style, text=text, accent=accent, subtitle=subtitle, stereo=stereo, look=look, real=real)


# ------------------------------------------------------------------ raccolta e validazione
GROUP_FILES = ["catalog_core", "catalog_drive", "catalog_dist", "catalog_mod", "catalog_time", "catalog_misc", "catalog_misc2",
               "catalog_wah", "catalog_volume", "catalog_mxr", "catalog_ehx", "catalog_mxr_b", "catalog_ehx_b"]

BANNED = re.compile(r"\bBOSS\b|\bRoland\b|\bIbanez\b|Tube\s*Screamer|Metal\s*Zone|Blues\s*Driver|Waza|"
                    r"Dunlop|Cry\s*Baby|Ernie\s*Ball|Morley|De\s*Armond|\bMXR\b|\bVox\b|Electro\s*-?\s*Harmonix|\bEHX\b|"
                    r"Sovtek|\bMuff\b|Memory\s*(Man|Boy|Toy)|Holy\s*(Grail|Stain)|\bPOG\b|Small\s*(Stone|Clone)|Mistress|"
                    r"Q-?Tron|Superego|Cathedral|Canyon|Oceans\s*1|Hazarai|Zakk|Wylde|\bEVH\b|Van\s*Halen|Dimebag|\bSlash\b|"
                    r"Dookie", re.I)
STYLES = ("boss", "ts", "nam", "treadle", "box")


def load_all(strict=True):
    models, seen, errors = [], set(), []
    for g in GROUP_FILES:
        path = os.path.join(HERE, g + ".py")
        if not os.path.exists(path):
            continue
        mod = importlib.import_module(g)
        for m in getattr(mod, "MODELS", []):
            m["group"] = g
            models.append(m)
    for m in models:
        e = validate(m)
        if m["id"] in seen:
            e.append("id duplicato")
        seen.add(m["id"])
        errors += ["%s (%s): %s" % (m["id"], m["group"], x) for x in e]
    codes = {}
    for m in models:
        codes.setdefault(m["code"], []).append(m["id"])
    errors += ["sigla duplicata %s: %s" % (c, ids) for c, ids in codes.items() if len(ids) > 1]
    if errors and strict:
        raise SystemExit("ERRORI NEL CATALOGO:\n  " + "\n  ".join(errors))
    return models, errors


def validate(m):
    e = []
    if not re.fullmatch(r"[a-z0-9_]+", m["id"]): e.append("id non valido")
    if m["family"] not in FAMILIES: e.append("famiglia sconosciuta %s" % m["family"])
    if m["category"] not in CATEGORIES: e.append("categoria sconosciuta %s" % m["category"])
    if BANNED.search(m["name"]) or BANNED.search(m["code"]): e.append("nome/sigla con marchio registrato")
    if not 1 <= len(m["controls"]) <= 12: e.append("numero di comandi non valido")
    if len(m["name"]) > 22: e.append("nome troppo lungo (max 22)")
    if len(m["code"]) > 8: e.append("sigla troppo lunga (max 8)")
    if m["style"] not in STYLES: e.append("stile sconosciuto %s" % m["style"])
    if m["style"] in ("treadle", "box") and not isinstance(m.get("look"), dict): e.append("stile %s senza 'look'" % m["style"])
    for k, c in enumerate(m["controls"]):
        if c["kind"] == "outer" and (k + 1 >= len(m["controls"]) or m["controls"][k + 1]["kind"] != "inner"):
            e.append("outer non seguito da inner: %s" % c["label"])
        if c["units"] not in UNITS: e.append("unita' %s" % c["units"])
        if len(c["label"]) > 10: e.append("etichetta troppo lunga: %s" % c["label"])
    rot = [c for c in m["controls"] if c["kind"] in ("knob", "selector", "outer")]
    if m["style"] == "treadle":
        # il primo comando e' il bilanciere (strip 254): non occupa una posizione rotativa
        if m["controls"][0]["kind"] != "knob": e.append("stile treadle: il primo comando deve essere il bilanciere")
        rot = rot[1:]
    stomp = m["style"] == "box" and isinstance(m.get("look"), dict) and m["look"].get("stomp")
    if len(rot) > (12 if stomp else (9 if m["style"] == "nam" else 8)):
        e.append("troppi pomelli (max 8 posizioni, 9 sul contenitore grande)")
    sl = [c for c in m["controls"] if c["kind"] == "slider"]
    if sl and rot and not stomp: e.append("cursori e pomelli insieme non supportati")
    if len(sl) > (12 if stomp else 11): e.append("troppi cursori")
    if m["style"] == "ts" and len(rot) != 3: e.append("stile ts richiede 3 pomelli")
    if m["style"] == "nam" and (len(rot) != 9 or len([c for c in m["controls"] if c["kind"] == "toggle"]) != 2):
        e.append("stile nam: 9 pomelli e 2 footswitch")
    return e


if __name__ == "__main__":
    models, errors = load_all(strict=False)
    print("%d modelli" % len(models))
    from collections import Counter
    for k, v in sorted(Counter(m["category"] for m in models).items()):
        print("  %-26s %d" % (k, v))
    if errors:
        print("ERRORI:\n  " + "\n  ".join(errors))
        sys.exit(1)
