/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Classe base di un effetto inserito in uno slot.
    I parametri sono atomici e normalizzati 0..1: l'interfaccia li scrive,
    il thread audio li legge senza lock.
*/

#pragma once

#include <atomic>
#include <memory>
#include <cmath>
#include <algorithm>
#include <string>
#include "Model.h"

namespace pt::engine
{
    class Effect
    {
    public:
        explicit Effect (const ModelDef& d) : def (d)
        {
            for (int i = 0; i < maxControls; ++i)
                params[i].store (i < d.numControls ? d.controls[i].def : 0.0f);
        }
        virtual ~Effect() = default;

        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void reset() = 0;
        /** Elaborazione in-place; numCh = 1 o 2. */
        virtual void process (float* const* ch, int numCh, int n) = 0;

        /** Valori da mostrare sul pedale (tuner: nota/cent, looper: stato). */
        virtual float readout (int /*index*/) const { return 0.0f; }
        /** Comandi a pulsante (looper): azione istantanea dal thread GUI. */
        virtual void trigger (int /*action*/) {}
        /** Lavoro non real-time (es. rigenerare un IR) eseguito dal thread dei messaggi. */
        virtual void messageThreadUpdate() {}
        /** Caricamento di un file (IR loader). Restituisce true se supportato. */
        virtual bool loadFile (const std::string& /*path*/) { return false; }
        virtual bool acceptsFiles() const { return false; }
        std::string loadedFile;
        /** Stato aggiuntivo (file caricati, opzioni) salvato nel preset come testo; thread dei messaggi. */
        virtual std::string saveState() const { return {}; }
        virtual void restoreState (const std::string&) {}

        float p (int i) const noexcept { return params[i].load (std::memory_order_relaxed); }

        /** Valore discreto di un selettore (0..steps-1). */
        int step (int i) const noexcept
        {
            const int s = std::max (1, (int) def.controls[i].steps);
            return std::clamp ((int) std::lround (p (i) * (s - 1)), 0, s - 1);
        }

        const ModelDef& def;
        std::atomic<float> params[maxControls];
    };

    /** Crea l'effetto adatto alla famiglia del modello. */
    std::unique_ptr<Effect> createEffect (const ModelDef&);
}
