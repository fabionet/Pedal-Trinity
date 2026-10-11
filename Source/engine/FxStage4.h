/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 4 (Ibanez): i tipi nuovi cominciano tutti con
    "i4" nella config (mai usati dai modelli precedenti, che restano identici bit per
    bit). Ogni gruppo di effetti ha il suo file:
      - FxStage4Dyn.cpp:  compressori, EQ, filtri, wah, utility (makeI4Dyn);
      - FxStage4Mod.cpp:  phaser, chorus, flanger, tremolo, delay BBD (makeI4Mod);
      - FxStage4Time.cpp: delay, chorus e flanger digitali (makeI4Time).
    Le distorsioni e gli overdrive sono netlist della famiglia Circuit (Circuit.cpp).
    Tutta la memoria si alloca in prepare(); nessuna allocazione nel thread audio.
*/

#pragma once

#include "FxClassicB.h"

namespace pt::engine
{
    namespace cx
    {
        /** Lisciatura del secondo ordine (due poli in cascata, ciascuno a meta' della costante di tempo: stesso ritardo medio
            del polo singolo) per i comandi che spostano un tempo di ritardo: la velocita' parte da zero, quindi niente salto
            d'intonazione all'istante del cambio. A comando fermo restituisce il valore esatto (come Smooth). */
        struct Smooth2
        {
            Smooth a, b;
            void set (double sr, double ms) { a.set (sr, 0.5 * ms); b.set (sr, 0.5 * ms); }
            void snap (float v) { a.snap (v); b.snap (v); }
            void unset() { a.init = b.init = false; }
            inline float next (float t) noexcept { return b.next (a.next (t)); }
        };
    }

    std::unique_ptr<Effect> makeI4Dyn (const ModelDef&, const std::string& type);     // FxStage4Dyn.cpp
    std::unique_ptr<Effect> makeI4Mod (const ModelDef&, const std::string& type);     // FxStage4Mod.cpp
    std::unique_ptr<Effect> makeI4Time (const ModelDef&, const std::string& type);    // FxStage4Time.cpp
}
