#!/usr/bin/env python3
"""
Assembla il catalogo per il plugin C++:
  Resources/pedals/<id>.jpg      immagine del pedale (1.75x delle coordinate logiche)
  Source/engine/Catalog.inc      definizioni ModelDef (comandi, netlist/config, coordinate)

Richiede i render di tools/art/render_pedals.py (build/pedals/<id>.png e .json).
Uso: python3 tools/art/assemble_catalog.py
"""
import json
import os
import sys
import unicodedata

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import catalog  # noqa: E402
import pedal_geometry as PG  # noqa: E402

PEDALS = os.path.join(HERE, "build", "pedals")
RES = os.path.join(ROOT, "Resources", "pedals")
INC = os.path.join(ROOT, "Source", "engine", "Catalog.inc")
os.makedirs(RES, exist_ok=True)
STORE_SCALE = 1.75          # risoluzione salvata rispetto alle coordinate logiche
SLOT_BG = np.array([23, 24, 27], np.float32)

KIND = {"knob": "Knob", "outer": "KnobOuter", "inner": "KnobInner", "selector": "Selector",
        "toggle": "Toggle", "slider": "Slider", "button": "Button"}
UNITS = {"dial": "Dial", "db": "Db", "hz": "Hz", "ms": "Ms", "percent": "Percent", "semitone": "Semitone", "choice": "Choice"}


def ascii_only(s):
    return unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode("ascii")


def cstr(s, allow_utf8=False):
    if s is None:
        return "nullptr"
    s = str(s) if allow_utf8 else ascii_only(s)
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c == "\\": out.append("\\\\")
        elif c == '"': out.append('\\"')
        elif 32 <= b < 127: out.append(c)
        else: out.append("\\x%02x\"\"" % b)      # "" spezza la stringa: evita che \x assorba le cifre seguenti
    return '"' + "".join(out) + '"'


def f(v):
    return "%.3ff" % float(v)


def convert_image(mid, src_dir=None, res=None, stem=None, logical=None, max_side=None):
    src_dir = src_dir or PEDALS
    res = res or RES
    stem = stem or mid
    src = Image.open(os.path.join(src_dir, mid + ".png"))
    lw, lh = logical if logical else (src.width / 2.0, src.height / 2.0)
    k = STORE_SCALE
    if max_side:
        k = min(k, max_side / max(lw, lh))
    w, h = int(round(lw * k)), int(round(lh * k))
    img = src.convert("RGBA").resize((w, h), Image.LANCZOS)
    rgba = np.asarray(img).astype(np.float32)
    alpha = rgba[..., 3:4] / 255.0
    rgb = rgba[..., :3]
    if src.mode == "RGBA":
        # render trasparente (pedale + ombra): colore in JPEG, alfa in una maschera PNG a 8 bit.
        # Il colore dove l'alfa e' ~0 viene portato a nero (ombre) per non creare aloni.
        rgb = np.where(alpha > 0.01, rgb, 0.0)
        # ombra: il rumore del render si comprime male. Sfocatura leggera solo dove il colore e' nero
        # (ombra pura, non i bordi del pedale) e 128 livelli di alfa: maschere ~3 volte piu' piccole.
        a8 = rgba[..., 3]
        blurred = np.asarray(Image.fromarray(np.clip(a8, 0, 255).astype(np.uint8), "L")
                             .filter(ImageFilter.GaussianBlur(1.6))).astype(np.float32)
        shadow = (a8 < 245) & (rgb.max(axis=2) < 10)
        # l'ombra deve svanire prima del bordo dell'immagine (altrimenti su una pedana chiara
        # si vede un rettangolo): attenuazione verso i bordi e intensita' leggermente ridotta
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        edge = np.clip(np.minimum.reduce([xx, w - 1 - xx, yy, h - 1 - yy]) / (0.10 * min(w, h)), 0, 1)
        edge = edge * edge * (3 - 2 * edge)
        a8 = np.where(shadow, blurred * 0.85 * edge, a8)
        a8 = np.round(a8 / 2.0) * 2.0
        Image.fromarray(np.clip(a8, 0, 255).astype(np.uint8), "L").save(os.path.join(res, stem + "_a.png"), optimize=True)
    else:
        # render opaco (vecchia scena): bordi sfumati verso il colore dello slot
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        edge = np.minimum.reduce([xx, w - 1 - xx, yy, h - 1 - yy]) / (0.06 * min(w, h))
        k = np.clip(edge, 0, 1)[..., None] ** 1.5
        rgb = rgb * k + SLOT_BG * (1 - k)
        mask = os.path.join(res, stem + "_a.png")
        if os.path.exists(mask):
            os.remove(mask)
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(os.path.join(res, stem + ".jpg"), quality=86,
                                                               optimize=True, progressive=True)


def model_row(m, geo, image, name, code, kinds=None, config=None, body=None):
    ctrl_lines = []
    for k, c in enumerate(m["controls"]):
        g = geo["controls"][k]
        kind = (kinds[k] if kinds and kinds[k] else c["kind"])
        steps = len(c["choices"]) if c["kind"] == "selector" else (2 if c["kind"] == "toggle" else 0)
        choices = "|".join(ascii_only(x) for x in c["choices"]) if c.get("choices") else None
        ctrl_lines.append("{ %s, %s, CK::%s, %d, %s, %d, U::%s, %s, %s, %s, %s, %s, %s, %s, %s }" % (
            cstr(c["label"]), cstr(c["role"]), KIND[kind], g["strip"], f(c["default"]), steps,
            UNITS[c["units"]], f(c["lo"]), f(c["hi"]), cstr(choices) if choices else "nullptr",
            f(g["anchor"][0]), f(g["anchor"][1]), f(g["top"][0]), f(g["top"][1]), f(g["radius"])))
    led, foot = geo["led"], geo["foot"]
    disp = geo.get("display", [0, 0, 0, 0])
    r, gc, b = m["colour"]
    extra = ""
    if body:
        extra = ",\n      %s, %s, %s, %s, %s" % tuple(f(v) for v in body)
    return """    { %s, %s, %s, %s, %s, Family::%s, %d,
      { %s },
      %s,
      %s, 0xff%02x%02x%02xu, %s, %s, %s, %s, %s, %s,
      { %s }, { %s }, %s, %s, %s, %s,
      %s, %s, %s, %s, %s,
      %s%s },""" % (
        cstr(m["id"]), cstr(name), cstr(code), cstr(m["inspired"]), cstr(m["category"]), m["family"],
        len(m["controls"]), ",\n        ".join(ctrl_lines), cstr(m["config"] if config is None else config),
        cstr(m["style"]), r, gc, b, cstr(image) if image else "nullptr", f(geo["imageW"]), f(geo["imageH"]),
        f(led[0]), f(led[1]), f(led[2]),
        ", ".join(f(p[0]) for p in foot), ", ".join(f(p[1]) for p in foot),
        f(disp[0]), f(disp[1]), f(disp[2]), f(disp[3]),
        f(geo["jacks"][0]), f(geo["jacks"][1]), f(geo["jacks"][2]), f(geo["jacks"][3]), "true" if m["stereo"] else "false",
        cstr(m["notes"], allow_utf8=True), extra)


REAL_SRC = os.path.join(HERE, "build", "real")
REAL_RES = os.path.join(ROOT, "Resources", "pedals_real")
REAL_INC = os.path.join(ROOT, "Source", "engine", "CatalogReal.inc")


def main_real():
    """Catalogo REAL MOD: Resources/pedals_real/r_<id>.jpg (+ _a.png) e Source/engine/CatalogReal.inc."""
    import real_catalog
    import real_layout
    os.makedirs(REAL_RES, exist_ok=True)
    rows, missing = [], []
    for r in real_catalog.real_models(strict=False):
        mid = r["id"]
        if not os.path.exists(os.path.join(REAL_SRC, mid + ".png")):
            missing.append(mid)
            continue
        lay = real_layout.real_layout(r)
        geo = PG.geometry(r, lay)
        convert_image(mid, REAL_SRC, REAL_RES, "r_" + mid, (geo["imageW"], geo["imageH"]), max_side=1150)
        kinds = list(lay.get("kinds") or [None] * len(r["controls"]))
        for k, v in (lay.get("kinds_override") or {}).items():
            kinds[k] = v
        rows.append(model_row(r, geo, "r_" + mid + "_jpg", r["real_name"], r["real_code"], kinds, "", geo["body"]))
    with open(REAL_INC, "w") as fo:
        fo.write("// File GENERATO da tools/art/assemble_catalog.py --real - non modificare a mano.\n")
        fo.write("// Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3\n")
        fo.write("// Repliche REAL MOD: stessi comandi (stesso ordine) dei modelli Pedal Trinity, aspetto del pedale reale.\n")
        if rows:
            fo.write("const ModelDef realModels[] = {\n" + "\n".join(rows) + "\n};\n")
            fo.write("constexpr int realCount = (int) (sizeof (realModels) / sizeof (realModels[0]));\n")
        else:
            fo.write("const ModelDef realModels[1] = {};\nconstexpr int realCount = 0;\n")
    keep = {"r_" + r for r in [x["id"] for x in real_catalog.real_models(strict=False)] if r not in missing}
    for fn in os.listdir(REAL_RES):
        stem = fn[:-6] if fn.endswith("_a.png") else fn[:-4]
        if stem not in keep:
            os.remove(os.path.join(REAL_RES, fn))
    total = sum(os.path.getsize(os.path.join(REAL_RES, x)) for x in os.listdir(REAL_RES))
    print("REAL MOD: %d repliche, %d senza render %s, immagini %.1f MB" % (len(rows), len(missing), missing[:8], total / 1e6))


def main():
    models, errors = catalog.load_all(strict=False)
    if errors:
        print("ATTENZIONE: errori nel catalogo (i modelli con errori vengono esclusi):\n  " + "\n  ".join(errors))
    bad = {e.split(" ")[0] for e in errors}
    rows, skipped, missing = [], [], []
    for m in models:
        mid = m["id"]
        if mid in bad:
            skipped.append(mid); continue
        geo = PG.geometry(m)
        has_image = os.path.exists(os.path.join(PEDALS, mid + ".png"))
        if has_image:
            convert_image(mid)
        else:
            missing.append(mid)
        ctrl_lines = []
        for k, c in enumerate(m["controls"]):
            g = geo["controls"][k]
            steps = len(c["choices"]) if c["kind"] == "selector" else (2 if c["kind"] == "toggle" else 0)
            choices = "|".join(ascii_only(x) for x in c["choices"]) if c.get("choices") else None
            ctrl_lines.append("{ %s, %s, CK::%s, %d, %s, %d, U::%s, %s, %s, %s, %s, %s, %s, %s, %s }" % (
                cstr(c["label"]), cstr(c["role"]), KIND[c["kind"]], g["strip"], f(c["default"]), steps,
                UNITS[c["units"]], f(c["lo"]), f(c["hi"]), cstr(choices) if choices else "nullptr",
                f(g["anchor"][0]), f(g["anchor"][1]), f(g["top"][0]), f(g["top"][1]), f(g["radius"])))
        led = geo["led"]
        foot = geo["foot"]
        disp = geo.get("display", [0, 0, 0, 0])
        r, gc, b = m["colour"]
        rows.append("""    { %s, %s, %s, %s, %s, Family::%s, %d,
      { %s },
      %s,
      %s, 0xff%02x%02x%02xu, %s, %s, %s, %s, %s, %s,
      { %s }, { %s }, %s, %s, %s, %s,
      %s, %s, %s, %s, %s,
      %s },""" % (
            cstr(mid), cstr(m["name"]), cstr(m["code"]), cstr(m["inspired"]), cstr(m["category"]), m["family"],
            len(m["controls"]), ",\n        ".join(ctrl_lines), cstr(m["config"]),
            cstr(m["style"]), r, gc, b, cstr(mid + "_jpg") if has_image else "nullptr", f(geo["imageW"]), f(geo["imageH"]),
            f(led[0]), f(led[1]), f(led[2]),
            ", ".join(f(p[0]) for p in foot), ", ".join(f(p[1]) for p in foot),
            f(disp[0]), f(disp[1]), f(disp[2]), f(disp[3]),
            f(geo["jacks"][0]), f(geo["jacks"][1]), f(geo["jacks"][2]), f(geo["jacks"][3]), "true" if m["stereo"] else "false",
            cstr(m["notes"], allow_utf8=True)))
    with open(INC, "w") as fo:
        fo.write("// File GENERATO da tools/art/assemble_catalog.py - non modificare a mano.\n")
        fo.write("// Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3\n")
        fo.write("using CK = ControlKind;\nusing U = Units;\nconst ModelDef models[] = {\n")
        fo.write("\n".join(rows))
        fo.write("\n};\n")
    # rimuove immagini di modelli non piu' presenti
    keep = {m["id"] for m in models if m["id"] not in skipped and m["id"] not in missing}
    for fn in os.listdir(RES):
        stem = fn[:-6] if fn.endswith("_a.png") else fn[:-4]
        if (fn.endswith(".jpg") or fn.endswith("_a.png")) and stem not in keep:
            os.remove(os.path.join(RES, fn))
    total = sum(os.path.getsize(os.path.join(RES, x)) for x in os.listdir(RES))
    print("Catalogo: %d modelli (%d senza immagine), immagini %.1f MB, esclusi %d %s" % (
        len(rows), len(missing), total / 1e6, len(skipped), skipped[:12]))


if __name__ == "__main__":
    if "--real" in sys.argv:
        main_real()
    else:
        main()
