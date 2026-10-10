#!/bin/bash
# Texture e render Blender (normale + REAL MOD) dei soli id indicati; poi convert_and_inc.py --norm/--real per le immagini e i .inc
# Uso: render_batch.sh <file con gli id> <log>
IDS=$(cat "$1")
cd "$(dirname "$0")/../.."
python3 tools/art/make_catalog_textures.py $IDS > /dev/null || echo TEXFAIL
python3 tools/art/make_real_textures.py $IDS > /dev/null || echo TEXFAIL
blender -b --factory-startup -P tools/art/render_pedals.py -- --samples 32 --force $IDS 2>&1 | grep --line-buffered -E "RENDER|FINE|Error|Traceback"
blender -b --factory-startup -P tools/art/render_pedals.py -- --real --samples 32 --force $IDS 2>&1 | grep --line-buffered -E "RENDER|FINE|Error|Traceback"
echo BATCH_FINITO
