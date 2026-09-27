/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
    Catalogo dei modelli: i dati sono generati in Catalog.inc da tools/art/catalog.py.
*/

#include "Model.h"
#include <cstring>

namespace pt::engine
{
    namespace
    {
        #include "Catalog.inc"
    }

    int numModels() { return (int) (sizeof (models) / sizeof (models[0])); }
    const ModelDef& model (int index) { return models[index]; }

    const ModelDef* findModel (const char* id)
    {
        for (const auto& m : models)
            if (std::strcmp (m.id, id) == 0)
                return &m;
        return nullptr;
    }
}
