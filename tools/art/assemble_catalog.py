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
from PIL import Image

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


def convert_image(mid):
    src = Image.open(os.path.join(PEDALS, mid + ".png")).convert("RGB")
    lw, lh = src.width / 2.0, src.height / 2.0
    w, h = int(round(lw * STORE_SCALE)), int(round(lh * STORE_SCALE))
    img = src.resize((w, h), Image.LANCZOS)
    a = np.asarray(img).astype(np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    edge = np.minimum.reduce([xx, w - 1 - xx, yy, h - 1 - yy]) / (0.06 * min(w, h))
    k = np.clip(edge, 0, 1)[..., None] ** 1.5
    a = a * k + SLOT_BG * (1 - k)                  # bordi sfumati verso il colore dello slot
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(os.path.join(RES, mid + ".jpg"), quality=86,
                                                               optimize=True, progressive=True)


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
        if fn.endswith(".jpg") and fn[:-4] not in keep:
            os.remove(os.path.join(RES, fn))
    total = sum(os.path.getsize(os.path.join(RES, x)) for x in os.listdir(RES))
    print("Catalogo: %d modelli (%d senza immagine), immagini %.1f MB, esclusi %d %s" % (
        len(rows), len(missing), total / 1e6, len(skipped), skipped[:12]))


if __name__ == "__main__":
    main()
