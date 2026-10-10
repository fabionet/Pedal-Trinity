/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 4 (Ibanez): compressori, EQ, filtri, wah e utility ("type=i4...").
*/

#include "FxStage4.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeI4Dyn (const ModelDef&, const std::string&)
    {
        return nullptr;
    }
}
