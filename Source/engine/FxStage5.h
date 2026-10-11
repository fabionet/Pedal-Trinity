/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 5 (Behringer, Cuvave/M-VAVE): i tipi nuovi
    cominciano tutti con "i5" nella config (mai usati dai modelli precedenti, che
    restano identici bit per bit). Ogni gruppo di effetti ha il suo file:
      - FxStage5Dyn.cpp:  compressori, EQ, filtri, wah, volume, utility, synth (makeI5Dyn);
      - FxStage5Mod.cpp:  chorus, flanger, phaser, tremolo, vibrato, delay BBD (makeI5Mod);
      - FxStage5Time.cpp: pedali digitali, preampli con IR (makeI5Time).
    Overdrive, distorsioni, fuzz e preampli analogici sono netlist della famiglia
    Circuit (Circuit.cpp). Si possono riusare i mattoni della tappa 4 (FxStage4.h).
    Tutta la memoria si alloca in prepare(); nessuna allocazione nel thread audio.
*/

#pragma once

#include "FxStage4.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeI5Dyn (const ModelDef&, const std::string& type);     // FxStage5Dyn.cpp
    std::unique_ptr<Effect> makeI5Mod (const ModelDef&, const std::string& type);     // FxStage5Mod.cpp
    std::unique_ptr<Effect> makeI5Time (const ModelDef&, const std::string& type);    // FxStage5Time.cpp
}
