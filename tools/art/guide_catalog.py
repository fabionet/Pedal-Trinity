#!/usr/bin/env python3
"""
Genera docs/guide/catalogo.tex: tabella di tutti i modelli del catalogo con
categoria, pedale di riferimento, tipo di emulazione e affidabilita' dei dati
(dai dossier di ricerca in docs/research).
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import catalog  # noqa: E402

EMU = {"Circuit": "circuito", "AmpSim": "circuito (valvole)", "BBDChorus": "BBD a clock", "BBDFlanger": "BBD a clock",
       "AnalogDelay": "BBD a clock", "Phaser": "all-pass JFET/OTA", "Compressor": "VCA/OTA", "GraphicEQ": "gyrator RLC",
       "Pitch": "flip-flop / DSP", "CabIR": "convoluzione IR"}
CONF = {"high": "alta", "medium": "media", "low": "bassa"}


def tex(s):
    s = str(s)
    for a, b in (("\\", r"\textbackslash{}"), ("&", r"\&"), ("%", r"\%"), ("$", r"\$"), ("#", r"\#"), ("_", r"\_"),
                 ("{", r"\{"), ("}", r"\}"), ("~", r"\textasciitilde{}"), ("^", r"\^{}")):
        s = s.replace(a, b)
    return s


def research_confidence():
    conf = {}
    for f in glob.glob(os.path.join(ROOT, "docs", "research", "*.json")):
        try:
            data = json.load(open(f))
        except Exception:
            continue
        if not isinstance(data, list):
            continue
        for p in data:
            code = str(p.get("code", "")).upper()
            c = str(p.get("confidence", "")).split()[0].lower() if p.get("confidence") else ""
            if code and c in CONF and code not in conf:
                conf[code] = CONF[c]
    return conf


def main():
    models, _ = catalog.load_all(strict=False)
    conf = research_confidence()
    rows = []
    for cat in catalog.CATEGORIES:
        ms = [m for m in models if m["category"] == cat]
        if not ms:
            continue
        rows.append(r"\multicolumn{5}{@{}l}{\rule{0pt}{1.4em}\textbf{\color{emerald}%s} \small(%d)}\\" % (tex(cat), len(ms)))
        for m in sorted(ms, key=lambda x: x["code"]):
            ref = m["inspired"]
            key = re.sub(r"^(BOSS|Ibanez|Fender)\s+", "", ref).split()[0].upper() if ref else ""
            c = conf.get(key, "-")
            if m["family"] not in ("Circuit", "AmpSim", "BBDChorus", "BBDFlanger", "AnalogDelay", "Phaser", "Compressor",
                                   "GraphicEQ") and c == "-":
                c = "specifiche"
            emu = EMU.get(m["family"], "DSP da specifiche")
            rows.append(r"\texttt{%s} & %s & %s & %s & %s\\" % (tex(m["code"]), tex(m["name"]), tex(ref), emu, c))
    out = [r"\begin{xltabular}{\linewidth}{@{}l l X l l@{}}",
           r"\toprule \textbf{Sigla} & \textbf{Nome} & \textbf{Suono di riferimento} & \textbf{Emulazione} & \textbf{Dati}\\ \midrule",
           r"\endhead", r"\bottomrule \endfoot"] + rows + [r"\end{xltabular}"]
    open(os.path.join(ROOT, "docs", "guide", "modelcount.tex"), "w").write("\\newcommand{\\modelcount}{%d}\n" % len(models))
    path = os.path.join(ROOT, "docs", "guide", "catalogo.tex")
    open(path, "w").write("% generato da tools/art/guide_catalog.py\n" + "\n".join(out) + "\n")
    print("scritto", path, len(models), "modelli")


if __name__ == "__main__":
    main()
