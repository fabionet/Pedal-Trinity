/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 3B: i tipi nuovi cominciano tutti con "b3"
    (mai usati dai modelli precedenti); per ogni altro tipo restituisce nullptr e
    makeClassic() prosegue con il percorso della tappa 3A, invariato.
*/

#include "FxClassicB.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeStage3B (const ModelDef& d, const std::string& type)
    {
        if (type.size() < 3 || type.compare (0, 2, "b3") != 0) return nullptr;
        if (auto fx = makeB3Mod (d, type))   return fx;
        if (auto fx = makeB3Time (d, type))  return fx;
        if (auto fx = makeB3Pitch (d, type)) return fx;
        if (auto fx = makeB3Multi (d, type)) return fx;
        if (auto fx = makeInstrument (d, type)) return fx;     // Bass Mono Synth (FxInstrument.cpp)
        return nullptr;
    }
}
