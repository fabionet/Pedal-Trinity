/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 4 (Ibanez): delay, chorus e flanger digitali ("type=i4...").
*/

#include "FxStage4.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeI4Time (const ModelDef&, const std::string&)
    {
        return nullptr;
    }
}
