#!/usr/bin/env bash
# Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#
# Rigenera la guida PDF illustrata:
#   1. screenshot reali dell'interfaccia (Standalone --screenshot)
#   2. illustrazioni con i richiami numerati (tools/art/guide_images.py)
#   3. tabella del catalogo (tools/art/guide_catalog.py)
#   4. compilazione XeLaTeX -> docs/PedalTrinity_Guida.pdf (+ copia in Resources/)
# Dopo aver aggiornato la guida ricompilare il plugin (la guida e' incorporata).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${1:-$ROOT/build/PedalTrinity_artefacts/Release/Standalone/Pedal Trinity}"
TMP="$ROOT/tools/art/build/guide"
mkdir -p "$TMP"

"$APP" --screenshot "$TMP/main.png" --scale 2 --size 1280x760 --view 3 --chain ed9,ds1,mc2w,gq7,ce2,dd3 \
    --set 0:0=0.62 --set 1:2=0.7 --set 2:5=0.75
"$APP" --screenshot "$TMP/view6.png" --size 1920x1080 --view 6 \
    --chain cs3,bd2,ds1,ce2,dd3,rv6,sd1,mc2w,gq7,ph3,tr2,re2
"$APP" --screenshot "$TMP/view18.png" --size 1920x1080 --view 18 \
    --chain ns2,cs3,ed9,sd1,bd2,ds1,mc2w,hm2,fz2,gq7,aw3,ce2,dc2,bf2,ph1,dm2,dd3,rv6,re2,tu3
"$APP" --screenshot "$TMP/zoom.png" --size 1600x900 --view 3 --chain ed9,mc2w,gq7 --zoom 1
"$APP" --screenshot "$TMP/info.png" --size 1280x760 --info
python3 "$ROOT/tools/art/guide_images.py" "$TMP"
python3 "$ROOT/tools/art/guide_catalog.py"

cd "$ROOT/docs/guide"
xelatex -interaction=nonstopmode -halt-on-error PedalTrinity_Guida.tex >/dev/null
xelatex -interaction=nonstopmode -halt-on-error PedalTrinity_Guida.tex | grep -E "Output written"
cp PedalTrinity_Guida.pdf "$ROOT/docs/PedalTrinity_Guida.pdf"
cp PedalTrinity_Guida.pdf "$ROOT/Resources/PedalTrinity_Guida.pdf"
rm -f PedalTrinity_Guida.{aux,log,out,toc,pdf}
ls -la "$ROOT/docs/PedalTrinity_Guida.pdf"
