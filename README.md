# Pedal Trinity

**Overdrive · Distorsore · Equalizzatore grafico a 7 bande** in un unico plugin per chitarra,
con interfaccia fotorealistica e pomelli 3D.

![Pedal Trinity](docs/images/screenshot.png)

| | |
|---|---|
| **Versione** | 1.0.0 beta |
| **Autore** | FabioNET |
| **Licenza** | [GNU GPL v3](LICENSE) |
| **Formati** | VST3 · LV2 · Standalone |
| **Sistemi** | Linux (pacchetto `.deb`) · Windows 10/11 x64 (installer, driver **ASIO**) |
| **Framework** | C++17 · JUCE 7.0.12 |

## I tre pedali

| Pedale | Ispirato a | Controlli |
|---|---|---|
| **Emerald Drive ED-9** | Ibanez Tube Screamer (TS808/TS9) | DRIVE, TONE, LEVEL, footswitch |
| **Metal Core MC-2W** | BOSS MT-2W Metal Zone Waza Craft | LEVEL, DIST, LOW/HIGH e MIDDLE/MID FREQ (pomelli concentrici), levetta S/C, footswitch |
| **Graphic EQ GQ-7** | BOSS GE-7 | 100, 200, 400, 800, 1.6k, 3.2k, 6.4k Hz (±15 dB), LEVEL (±15 dB), footswitch |

Catena del segnale: `IN → ED-9 → MC-2W → GQ-7 → OUT`. Ogni pedale ha un bypass senza click.
Il tasto **INFO** sulla targhetta mostra licenza, autore e versione, e apre la
[guida illustrata in PDF](docs/PedalTrinity_Guida.pdf).

## Installazione

### Linux (Ubuntu 22.04+, Debian 12+, Mint 21+)
```bash
sudo apt install ./pedal-trinity_1.0.0~beta-1_amd64.deb
```
Installa `/usr/lib/vst3/Pedal Trinity.vst3`, `/usr/lib/lv2/Pedal Trinity.lv2` e l'app `pedal-trinity`
(nel menu *Audio*). La guida si trova in `/usr/share/doc/pedal-trinity/`.

### Windows 10/11 (x64)
Esegui `PedalTrinity-1.0.0-beta-Windows-x64-Setup.exe`. Il VST3 va in
`C:\Program Files\Common Files\VST3`, l'LV2 in `C:\Program Files\Common Files\LV2`.
L'app standalone supporta i driver **ASIO** (selezionati automaticamente al primo avvio se presenti),
WASAPI e DirectSound.

## Rami del repository

| Ramo | Contenuto |
|---|---|
| `main-linux` | sorgenti + CI Linux (build, autotest, pacchetto `.deb`, archivio `.tar.xz`) |
| `main-windows` | sorgenti + CI Windows (build MSVC con ASIO SDK, autotest, installer Inno Setup, `.zip`) |

## Compilazione dai sorgenti

Linux:
```bash
sudo apt install build-essential cmake ninja-build libasound2-dev libjack-jackd2-dev \
  libfreetype6-dev libfontconfig1-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release     # scarica JUCE 7.0.12
cmake --build build
"build/PedalTrinity_artefacts/Release/Standalone/Pedal Trinity" --selftest
bash packaging/linux/build_deb.sh build dist
```

Windows (Visual Studio 2022):
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DPT_ASIO_SDK_DIR=C:\percorso\asiosdk
cmake --build build --config Release
iscc packaging\windows\PedalTrinity.iss
```
Lo [Steinberg ASIO SDK](https://www.steinberg.net/developers/) (disponibile anche con licenza GPLv3)
si scarica separatamente; la CI lo scarica automaticamente.

## Struttura

```
Source/               codice C++ (JUCE 7)
  dsp/                Overdrive.h, Distortion.h, GraphicEQ.h, Filters.h
  gui/                controlli fotorealistici, pannello Info, layout generato
  StandaloneApp.cpp   app standalone (ASIO, --selftest, --screenshot)
Resources/            foto dei pedali, filmstrip 3D dei pomelli, licenza, guida PDF
docs/                 guida illustrata (sorgente LaTeX + PDF)
packaging/            .deb (Linux) e installer Inno Setup (Windows)
tools/art/            strumenti usati una sola volta per generare le immagini
                      (Blender + Python): non servono per compilare il plugin
```

## Grafica

La "foto" frontale dei pedali e i fotogrammi 3D dei pomelli sono **render originali** creati con
Blender (`tools/art/`), non fotografie di prodotti commerciali. Il plugin le carica come immagini
incorporate: la compilazione richiede solo un compilatore C++ e CMake.

## Marchi

Pedal Trinity è un progetto indipendente. *Ibanez* e *Tube Screamer* sono marchi di Hoshino Gakki Co.;
*BOSS*, *MT-2*, *Metal Zone*, *Waza Craft* e *GE-7* sono marchi di Roland Corporation.
I nomi sono citati solo per indicare il suono di riferimento: nessuna affiliazione o approvazione.
VST è un marchio di Steinberg Media Technologies GmbH. ASIO è un marchio e software di Steinberg Media Technologies GmbH.

## Licenza

Copyright © 2026 FabioNET — distribuito secondo i termini della
**GNU General Public License v3.0** (o successiva). Vedi [LICENSE](LICENSE).
