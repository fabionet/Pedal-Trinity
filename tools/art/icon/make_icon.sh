#!/usr/bin/env bash
# Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
# Rigenera l'icona del programma da icon.svg: Resources/icon.png (512 px, usata da JUCE e dal .deb)
# e packaging/windows/PedalTrinity.ico (256/128/64/48/32/16 px, installer e Standalone Windows).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/../../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
rsvg-convert -w 512 -h 512 "$HERE/icon.svg" -o "$ROOT/Resources/icon.png"
for s in 256 128 64 48 32 16; do rsvg-convert -w $s -h $s "$HERE/icon.svg" -o "$TMP/i$s.png"; done
convert "$TMP"/i256.png "$TMP"/i128.png "$TMP"/i64.png "$TMP"/i48.png "$TMP"/i32.png "$TMP"/i16.png \
        -define icon:auto-resize= "$ROOT/packaging/windows/PedalTrinity.ico"
identify "$ROOT/Resources/icon.png" "$ROOT/packaging/windows/PedalTrinity.ico"
