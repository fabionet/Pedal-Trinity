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
        #include "CatalogReal.inc"          // realModels[], realCount (tools/art/assemble_catalog.py --real)
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

    int numRealModels() { return realCount; }
    const ModelDef& realModel (int index) { return realModels[index]; }

    const ModelDef* findRealModel (const char* id)
    {
        for (int i = 0; i < realCount; ++i)
            if (std::strcmp (realModels[i].id, id) == 0)
                return &realModels[i];
        return nullptr;
    }
}
