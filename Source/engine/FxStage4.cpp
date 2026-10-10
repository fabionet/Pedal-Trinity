/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 4: per i tipi che non cominciano con "i4"
    restituisce nullptr e makeClassic() prosegue con il percorso precedente, invariato.
*/

#include "FxStage4.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeStage4 (const ModelDef& d, const std::string& type)
    {
        if (type.size() < 3 || type.compare (0, 2, "i4") != 0) return nullptr;
        if (auto fx = makeI4Dyn (d, type))  return fx;
        if (auto fx = makeI4Mod (d, type))  return fx;
        if (auto fx = makeI4Time (d, type)) return fx;
        return nullptr;
    }
}
