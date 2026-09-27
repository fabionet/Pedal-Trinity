#!/usr/bin/env bash
# Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
#
# Crea il pacchetto .deb a partire da una build Release gia' compilata.
# Uso: packaging/linux/build_deb.sh [cartella_build] [cartella_output]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="${2:-$ROOT/dist}"
ART="$BUILD/PedalTrinity_artefacts/Release"

PKG="pedal-trinity"
VERSION="1.0.0~beta-1"
ARCH="$(dpkg --print-architecture)"
MAINTAINER="FabioNET <19152770+fabionet@users.noreply.github.com>"

for f in "$ART/VST3/Pedal Trinity.vst3" "$ART/LV2/Pedal Trinity.lv2" "$ART/Standalone/Pedal Trinity"; do
    [[ -e "$f" ]] || { echo "Manca $f: compila prima il progetto (cmake --build)"; exit 1; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
D="$STAGE/$PKG"

install -d "$D/DEBIAN" "$D/usr/bin" "$D/usr/lib/vst3" "$D/usr/lib/lv2" \
           "$D/usr/share/applications" "$D/usr/share/doc/$PKG" \
           "$D/usr/share/icons/hicolor/512x512/apps" "$D/usr/share/icons/hicolor/128x128/apps" \
           "$D/usr/share/metainfo"

cp -r "$ART/VST3/Pedal Trinity.vst3" "$D/usr/lib/vst3/"
cp -r "$ART/LV2/Pedal Trinity.lv2" "$D/usr/lib/lv2/"
install -m 0755 "$ART/Standalone/Pedal Trinity" "$D/usr/bin/pedal-trinity"
find "$D/usr/lib" -name "*.so" -exec strip --strip-unneeded {} \; -exec chmod 0644 {} \;
strip --strip-unneeded "$D/usr/bin/pedal-trinity"

install -m 0644 "$ROOT/packaging/linux/pedal-trinity.desktop" "$D/usr/share/applications/"
install -m 0644 "$ROOT/packaging/linux/com.fabionet.pedaltrinity.metainfo.xml" "$D/usr/share/metainfo/"
install -m 0644 "$ROOT/Resources/icon.png" "$D/usr/share/icons/hicolor/512x512/apps/pedal-trinity.png"
if command -v convert >/dev/null; then
    convert "$ROOT/Resources/icon.png" -resize 128x128 "$D/usr/share/icons/hicolor/128x128/apps/pedal-trinity.png"
else
    rmdir "$D/usr/share/icons/hicolor/128x128/apps"
fi

install -m 0644 "$ROOT/docs/PedalTrinity_Guida.pdf" "$D/usr/share/doc/$PKG/"
install -d "$D/usr/share/man/man1" "$D/usr/share/lintian/overrides"
gzip -9n -c "$ROOT/packaging/linux/pedal-trinity.1" > "$D/usr/share/man/man1/pedal-trinity.1.gz"
# JUCE incorpora libpng/zlib per decodificare le immagini: e' il comportamento standard dei plugin JUCE
printf '%s: embedded-library libpng *\n%s: initial-upload-closes-no-bugs *\n' "$PKG" "$PKG" \
    > "$D/usr/share/lintian/overrides/$PKG"
install -m 0644 "$ROOT/README.md" "$D/usr/share/doc/$PKG/"
cat > "$D/usr/share/doc/$PKG/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Pedal Trinity
Upstream-Contact: FabioNET
Source: https://github.com/fabionet/Pedal-Trinity

Files: *
Copyright: 2026 FabioNET
License: GPL-3+

Files: JUCE/*
Copyright: Raw Material Software Limited
License: GPL-3+

License: GPL-3+
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 3 can be found in "/usr/share/common-licenses/GPL-3".
EOF
printf '%s (%s) unstable; urgency=medium\n\n  * Prima release beta: pedaliera fino a 100 slot, catalogo di 219 pedali.\n  * Emulazione dei circuiti analogici, preset di fabbrica e utente.\n  * Viste 3/6/9/18, zoom del pedale, finestra da 1280x760 a 2560x1440.\n  * Formati VST3, LV2 e Standalone.\n\n -- %s  %s\n' \
    "$PKG" "$VERSION" "$MAINTAINER" "$(date -R)" | gzip -9n > "$D/usr/share/doc/$PKG/changelog.Debian.gz"

# --- dipendenze: librerie collegate (dpkg-shlibdeps) + librerie X11 caricate a runtime da JUCE
SHLIBS=""
if command -v dpkg-shlibdeps >/dev/null; then
    TMPD="$(mktemp -d)"; mkdir -p "$TMPD/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG" "$PKG" > "$TMPD/debian/control"
    SHLIBS="$(cd "$TMPD" && dpkg-shlibdeps -O -e "$D/usr/bin/pedal-trinity" \
                -e "$D/usr/lib/vst3/Pedal Trinity.vst3/Contents/"*"/Pedal Trinity.so" 2>/dev/null \
              | sed -n 's/^shlibs:Depends=//p')"
    rm -rf "$TMPD"
fi
[[ -n "$SHLIBS" ]] || SHLIBS="libc6 (>= 2.34), libstdc++6 (>= 12), libgcc-s1, libfreetype6, libasound2t64 | libasound2"
DEPENDS="$SHLIBS, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1"

# permessi standard Debian
find "$D/usr" -type d -exec chmod 0755 {} +
find "$D/usr" -type f -exec chmod 0644 {} +
chmod 0755 "$D/usr/bin/pedal-trinity"
SIZE="$(du -sk "$D/usr" | cut -f1)"
cat > "$D/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: sound
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE
Depends: $DEPENDS
Recommends: libjack-jackd2-0 | libjack0
Suggests: carla, ardour
Maintainer: $MAINTAINER
Homepage: https://github.com/fabionet/Pedal-Trinity
Description: pedaliera per chitarra con 219 pedali emulati (VST3/LV2/Standalone)
 Pedal Trinity e' una pedaliera virtuale fino a 100 slot con un catalogo di
 219 modelli originali ispirati ai pedali compatti per chitarra: overdrive,
 distorsori, fuzz, modulazioni BBD, delay, riverberi, pitch, dinamica, EQ,
 simulatori di amplificatore, cabinet IR, splitter A/B e utilita'.
 Gli stadi analogici sono emulati dalla circuitazione (clipper a diodi,
 reti di tono, BBD a clock), con preset di fabbrica e utente e viste
 da 3, 6, 9 o 18 pedali.
 Include i formati VST3, LV2 e un'applicazione standalone (ALSA/JACK).
 Autore: FabioNET. Licenza: GNU GPL v3.
EOF

mkdir -p "$OUT"
DEB="$OUT/${PKG}_1.0.0-beta_${ARCH}.deb"   # niente "~": GitHub lo rinomina negli allegati
dpkg-deb --root-owner-group -Zxz --build "$D" "$DEB"
echo "Creato: $DEB"
dpkg-deb --info "$DEB" | sed -n '1,20p'
