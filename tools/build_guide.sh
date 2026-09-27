#!/usr/bin/env bash
# Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#
# Rigenera la guida PDF illustrata:
#   1. screenshot reali dell'interfaccia (Standalone --screenshot, scala 2x)
#   2. illustrazioni con i numeri di richiamo (tools/art/guide_images.py)
#   3. compilazione XeLaTeX -> docs/PedalTrinity_Guida.pdf (+ copia in Resources/)
# Dopo aver aggiornato la guida ricompilare il plugin (la guida e' incorporata).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${1:-$ROOT/build/PedalTrinity_artefacts/Release/Standalone/Pedal Trinity}"
TMP="$ROOT/tools/art/build"
mkdir -p "$TMP"

"$APP" --screenshot "$TMP/shot2x.png" --scale 2 \
    --set od_drive=6.5 --set dist_on=1 --set dist_mode=1 --set dist_gain=7 \
    --set dist_mid=-4 --set eq_0=4 --set eq_1=2 --set eq_3=-3 --set eq_4=-5 --set eq_6=6 --set eq_level=2
"$APP" --screenshot "$TMP/info2x.png" --scale 2 --info
python3 "$ROOT/tools/art/guide_images.py" "$TMP/shot2x.png" "$TMP/info2x.png"

cd "$ROOT/docs/guide"
xelatex -interaction=nonstopmode -halt-on-error PedalTrinity_Guida.tex >/dev/null
xelatex -interaction=nonstopmode -halt-on-error PedalTrinity_Guida.tex | grep -E "Output written"
cp PedalTrinity_Guida.pdf "$ROOT/docs/PedalTrinity_Guida.pdf"
cp PedalTrinity_Guida.pdf "$ROOT/Resources/PedalTrinity_Guida.pdf"
rm -f PedalTrinity_Guida.{aux,log,out,toc,pdf}
ls -la "$ROOT/docs/PedalTrinity_Guida.pdf"
