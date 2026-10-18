/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 6: per i tipi che non cominciano con "i6"
    restituisce nullptr e makeClassic() prosegue con il percorso precedente, invariato.
*/

#include "FxStage6.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeStage6 (const ModelDef& d, const std::string& type)
    {
        if (type.size() < 3 || type.compare (0, 2, "i6") != 0) return nullptr;
        if (auto fx = makeI6Cab (d, type)) return fx;
        return nullptr;
    }
}
