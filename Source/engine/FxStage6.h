/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Aggancio dei modelli della tappa 6 (simulatori di cassa): i tipi nuovi cominciano
    con "i6" nella config (mai usati dai modelli precedenti, che restano identici bit
    per bit). Le casse (formato, altoparlante, microfono, posizione) sono in
    FxStage6Cab.cpp (makeI6Cab). Gli amplificatori sono netlist della famiglia
    AmpSim/Circuit (Circuit.cpp). Tutta la memoria si alloca in prepare().
*/

#pragma once

#include "FxStage5.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeI6Cab (const ModelDef&, const std::string& type);     // FxStage6Cab.cpp
}
