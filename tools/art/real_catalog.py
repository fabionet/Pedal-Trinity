"""
Pedal Trinity - catalogo REAL MOD (Python puro).

Per ogni modello del catalogo ispirato a un pedale BOSS costruisce la
descrizione della sua replica fedele: sigla e nome reali, famiglia di
contenitore (compatto, Twin, serie 200/500, unita' da pavimento, pedali a
bilanciere...), misure, colori e disposizione dei comandi del pedale originale.

I dati reali (misure, disposizione, colori) sono in real_specs.json: fatti
tecnici raccolti dalle schede e dai manuali ufficiali, senza immagini.
Le repliche sono modellate da zero in Blender: nessuna foto, nessun logo.

La replica conserva gli stessi comandi, nello stesso ordine, del modello
Pedal Trinity: cambia solo l'aspetto (il suono e i preset restano identici).
"""
import copy
import json
import os
import re

import catalog

HERE = os.path.dirname(os.path.abspath(__file__))
SPECS = os.path.join(HERE, "real_specs.json")

SERIES = ("compact", "twin", "series200", "series500", "floor", "treadle_volume", "treadle_wah", "rocker",
          "vintage_box", "tabletop", "other")

# misure tipiche (mm) per famiglia, se la scheda non le riporta
DEFAULT_DIMS = {"compact": (73, 129, 59), "twin": (173, 158, 57), "series200": (101, 138, 63),
                "series500": (170, 138, 62), "floor": (238, 160, 62), "treadle_volume": (101, 316, 81),
                "treadle_wah": (105, 275, 88), "rocker": (130, 250, 80), "vintage_box": (150, 140, 70),
                "tabletop": (300, 200, 90), "other": (120, 140, 62)}

# scritte che non devono mai comparire sulle repliche (marchi e loghi del produttore)
# nomi di aziende (BOSS, Roland e i marchi dei partner delle edizioni in collaborazione): mai stampati;
# restano solo i nomi di prodotto, per identificare il pedale di riferimento
NO_BRAND = re.compile(r"\bBOSS\b|\bRoland\b|\bFender\b|\bIbanez\b|\bJHS\b", re.I)


def hex_rgb(h, fallback):
    if not h or not isinstance(h, str):
        return tuple(fallback)
    h = h.strip().lstrip("#")
    if not re.fullmatch(r"[0-9a-fA-F]{6}", h):
        return tuple(fallback)
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def split_reference(inspired):
    """'BOSS DS-1X Distortion' -> ('DS-1X', 'Distortion')."""
    s = re.sub(r"^BOSS\s+", "", inspired or "").strip()
    s = re.sub(r"\s*\(.*?\)\s*", " ", s).strip()
    parts = s.split(None, 1)
    return (parts[0], parts[1] if len(parts) > 1 else "") if parts else ("", "")


# nomi stampati sui pedali quando il riferimento del catalogo riporta solo la sigla
PRINTED_NAMES = {"DS-1": "Distortion", "GE-7": "Equalizer", "GE-10": "Graphic Equalizer", "GE-6": "Equalizer"}


def load_specs():
    if not os.path.exists(SPECS):
        return {}
    with open(SPECS) as f:
        data = json.load(f)
    return {d["id"]: d for d in data if isinstance(d, dict) and "id" in d}


def clean_text(s):
    s = NO_BRAND.sub("", str(s or "")).strip()
    return re.sub(r"\s{2,}", " ", s)


def real_models(strict=True):
    """Lista delle repliche: dizionari del catalogo con i campi 'real_*' aggiunti."""
    models, errors = catalog.load_all(strict=strict)
    specs = load_specs()
    out = []
    for m in models:
        if not (m.get("inspired") or "").startswith("BOSS"):
            continue
        sp = specs.get(m["id"], {})
        code, name = split_reference(m["inspired"])
        name = PRINTED_NAMES.get(code, name)
        r = copy.deepcopy(m)
        r["real_code"] = clean_text(sp.get("code") or code) or code
        r["real_name"] = clean_text(sp.get("name") or name) or name
        series = sp.get("series") if sp.get("series") in SERIES else "compact"
        r["series"] = series
        dims = sp.get("dims_mm") if isinstance(sp.get("dims_mm"), list) and len(sp.get("dims_mm")) == 3 else None
        if series == "compact" or not dims or not all(isinstance(v, (int, float)) and 30 <= v <= 700 for v in dims):
            dims = DEFAULT_DIMS[series]
        r["dims"] = tuple(float(v) / 1000.0 for v in dims)
        r["colour"] = hex_rgb(sp.get("body_colour_hex"), m["colour"])
        r["text"] = hex_rgb(sp.get("text_colour_hex"), m.get("text") or (0, 0, 0)) if sp.get("text_colour_hex") else m.get("text")
        if dims[2] > dims[1] * 1.3 and series not in ("compact",):
            dims = (dims[0], dims[2], dims[1])      # unita' "in piedi" (es. FA-1): appoggiata sul dorso
            r["dims"] = tuple(float(v) / 1000.0 for v in dims)
        r["real_layout"] = sp.get("layout") if isinstance(sp.get("layout"), list) else None
        r["real_knob_order"] = sp.get("knob_order_left_to_right") if isinstance(sp.get("knob_order_left_to_right"), list) else None
        r["real_concentric"] = [p for p in (sp.get("concentric_outer_inner") or []) if isinstance(p, list) and len(p) == 2]
        r["real_footswitches"] = sp.get("footswitches")
        r["real_display"] = bool(sp.get("display"))
        r["real_notes"] = sp.get("notes") or ""
        r["style"] = "real_" + series
        out.append(r)
    return out


if __name__ == "__main__":
    import collections
    rs = real_models()
    print(len(rs), collections.Counter(r["series"] for r in rs))
