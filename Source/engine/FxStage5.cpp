/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 5: per i tipi che non cominciano con "i5"
    restituisce nullptr e makeClassic() prosegue con il percorso precedente, invariato.
*/

#include "FxStage5.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeStage5 (const ModelDef& d, const std::string& type)
    {
        if (type.size() < 3 || type.compare (0, 2, "i5") != 0) return nullptr;
        if (auto fx = makeI5Dyn (d, type))  return fx;
        if (auto fx = makeI5Mod (d, type))  return fx;
        if (auto fx = makeI5Time (d, type)) return fx;
        return nullptr;
    }
}
