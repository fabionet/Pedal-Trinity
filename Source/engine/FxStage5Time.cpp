/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 5 (Behringer, Cuvave/M-VAVE): pedali digitali e preampli con IR ("type=i5...").
*/

#include "FxStage5.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeI5Time (const ModelDef&, const std::string&)
    {
        return nullptr;
    }
}
