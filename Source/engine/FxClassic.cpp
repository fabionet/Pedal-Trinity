/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 3 alle famiglie esistenti: la config di questi
    pedali usa valori nuovi di "type=" (mai usati dai modelli precedenti), cosi' i
    pedali gia' a catalogo seguono il percorso originale di createEffect() senza
    alcuna differenza.
*/

#include "FxClassic.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeClassic (const ModelDef& d)
    {
        if (d.config == nullptr || std::strstr (d.config, "type=") == nullptr) return nullptr;
        const Config cfg (d.config);
        const auto type = cfg.str ("type");
        // tappa 3B: tipi con nomi nuovi ("b3..."), mai usati dai modelli precedenti
        if (auto fx = makeStage3B (d, type)) return fx;
        const auto f = d.family;
        if (f == Family::Phaser || f == Family::Tremolo || f == Family::BBDChorus || f == Family::BBDFlanger)
            return makeClassicMod (d, type);
        if (f == Family::AnalogDelay || f == Family::DigitalDelay || f == Family::Reverb || f == Family::Looper)
            return makeClassicTime (d, type);
        if (f == Family::Pitch)
            return makePolyPitch (d, type);
        if (f == Family::Synth)
        {
            if (auto fx = makePolyPitch (d, type)) return fx;
            return makeInstrument (d, type);
        }
        return nullptr;
    }
}
