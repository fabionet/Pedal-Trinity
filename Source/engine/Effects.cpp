/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Families.h"
#include "Circuit.h"
#include "FxClassic.h"
#include "FxCommon.h"

namespace pt::engine
{
    std::unique_ptr<Effect> makeCircuit (const ModelDef& d) { return std::make_unique<CircuitEffect> (d); }

    namespace
    {
        /** SPL-3: nessuna elaborazione propria, l'instradamento lo esegue Chain leggendo i comandi. */
        class SplitterEffect final : public Effect
        {
        public:
            using Effect::Effect;
            void prepare (double, int) override {}
            void reset() override {}
            void process (float* const*, int, int) override {}
        };
    }

    static std::unique_ptr<Effect> makeFamily (const ModelDef&);

    namespace
    {
        //==============================================================================
        /** Involucro senza click per le famiglie della prima versione del motore (1.3.1).

            Queste famiglie leggono i comandi una volta per blocco: girando un pomello il guadagno, i coefficienti dei
            filtri o il bilanciamento saltano al bordo del blocco (click fino a 2000x con clicksS2). L'involucro non tocca
            l'algoritmo della famiglia:
              - a comandi FERMI passa all'effetto interno i blocchi interi con gli stessi valori: l'uscita e' identica bit
                per bit a quella della famiglia da sola (i comandi sono copiati all'interno anche in prepare() e reset(),
                prima che la famiglia li legga, e al primo blocco);
              - mentre un pomello si muove il valore passato all'interno scorre su due poli in cascata da 12 ms (5 ms per
                i wah, che seguono il piede; la velocita' del comando parte da zero, niente gradino della derivata) e
                l'interno elabora un campione alla volta: il gradino al bordo del pezzo diventa trascurabile. Appena il
                valore arriva al bersaglio (1e-5) si torna ai blocchi interi: il costo per campione si paga solo mentre
                un comando si muove;
              - un selettore o una levetta (comandi a scatti) che cambia fa scendere l'uscita a zero con un coseno
                rialzato di 6 ms, a volume zero lo scatto passa all'interno, poi l'uscita risale in 6 ms (come i
                selettori dei pedali digitali). I pulsanti passano subito.
              - sub = 0: solo le dissolvenze dei comandi a scatti; i pomelli passano diretti a ogni blocco, come prima
                (wah a induttore, che liscia gia' pedale e trimmer per conto suo a pezzi di pochi campioni).
            Nessuna allocazione: tutto lo stato e' nell'oggetto. */
        class SmoothFamily final : public Effect
        {
        public:
            SmoothFamily (const ModelDef& d, std::unique_ptr<Effect> innerFx, int subBlock, double tauMs)
                : Effect (d), fx (std::move (innerFx)), sub (std::max (1, subBlock)), knobs (subBlock > 0), tau (0.001 * tauMs)
            {
                for (int k = 0; k < d.numControls && k < maxControls; ++k)
                {
                    const auto kd = d.controls[k].kind;
                    button[k] = kd == ControlKind::Button;
                    stepped[k] = button[k] || kd == ControlKind::Selector || kd == ControlKind::Toggle || d.controls[k].steps > 1;
                }
                syncAll();
            }
            void prepare (double s, int maxBlock) override
            {
                syncAll();
                fx->prepare (s, maxBlock);
                a = (float) std::exp (-(double) sub / (tau * s));
                fadeStep = (float) (1.0 / std::max (1.0, 0.006 * s));
            }
            void reset() override { syncAll(); fx->reset(); }
            float readout (int i) const override { return fx->readout (i); }
            void trigger (int action) override { fx->trigger (action); }
            void messageThreadUpdate() override { fx->messageThreadUpdate(); }
            bool loadFile (const std::string& path) override { const bool ok = fx->loadFile (path); loadedFile = fx->loadedFile; return ok; }
            bool acceptsFiles() const override { return fx->acceptsFiles(); }
            std::string saveState() const override { return fx->saveState(); }
            void restoreState (const std::string& st) override { fx->restoreState (st); }

            void process (float* const* ch, int numCh, int n) override
            {
                const int nc = std::min (def.numControls, maxControls);
                bool moving = false, stepChange = false;
                for (int k = 0; k < nc; ++k)
                {
                    const float t = p (k);
                    target[k] = t;
                    if (first) { cur[k] = mid[k] = t; fx->params[k].store (t, std::memory_order_relaxed); continue; }
                    if (button[k] || (! knobs && ! stepped[k])) fx->params[k].store (t, std::memory_order_relaxed);
                    else if (stepped[k]) { if (differs (t, fx->params[k].load (std::memory_order_relaxed))) stepChange = true; }
                    else if (differs (cur[k], t) || differs (mid[k], t)) moving = true;
                }
                first = false;
                if (stepChange && fading >= 0) fading = -1;        // anche durante una risalita: si torna giu' senza salti
                if (! moving && fading == 0)
                {
                    fx->process (ch, numCh, n);
                    return;
                }
                for (int off = 0; off < n;)
                {
                    int m = n - off;
                    if (moving) m = std::min (m, sub);
                    if (fading < 0) m = std::min (m, std::max (1, (int) std::ceil (fadeU / fadeStep)));
                    else if (fading > 0) m = std::min (m, 64);
                    if (moving)
                    {
                        bool still = false;
                        for (int k = 0; k < nc; ++k)
                            if (! stepped[k] && (differs (cur[k], target[k]) || differs (mid[k], target[k])))
                            {
                                mid[k] = target[k] + a * (mid[k] - target[k]);
                                cur[k] = mid[k] + a * (cur[k] - mid[k]);
                                if (std::abs (cur[k] - target[k]) < 1.0e-5f && std::abs (mid[k] - target[k]) < 1.0e-5f) cur[k] = mid[k] = target[k];
                                else still = true;
                                fx->params[k].store (cur[k], std::memory_order_relaxed);
                            }
                        moving = still;
                    }
                    float* sp[2] = { ch[0] + off, numCh > 1 ? ch[1] + off : nullptr };
                    fx->process (sp, numCh, m);
                    if (fading != 0)
                    {
                        for (int i = 0; i < m; ++i)
                        {
                            if (fading < 0) fadeU = std::max (0.0f, fadeU - fadeStep);
                            else fadeU = std::min (1.0f, fadeU + fadeStep);
                            const float g = 0.5f - 0.5f * std::cos (3.14159265f * fadeU);
                            for (int c = 0; c < numCh; ++c) sp[c][i] *= g;
                        }
                        if (fading < 0 && fadeU <= 0.0f)
                        {
                            // a volume zero: i selettori e le levette passano all'effetto interno
                            for (int k = 0; k < nc; ++k)
                                if (stepped[k] && ! button[k]) fx->params[k].store (p (k), std::memory_order_relaxed);
                            fading = 1;
                        }
                        else if (fading > 0 && fadeU >= 1.0f) fading = 0;
                    }
                    off += m;
                }
            }
        private:
            static bool differs (float x, float y) noexcept { return std::fpclassify (x - y) != FP_ZERO; }
            void syncAll()
            {
                for (int k = 0; k < std::min (def.numControls, maxControls); ++k)
                {
                    cur[k] = mid[k] = p (k);
                    fx->params[k].store (cur[k], std::memory_order_relaxed);
                }
                fading = 0; fadeU = 1.0f; first = true;
            }
            std::unique_ptr<Effect> fx;
            const int sub;
            const bool knobs;        // false: i pomelli passano diretti (la famiglia li liscia gia' da se')
            const double tau;        // costante di tempo di ciascuno dei due poli (s)
            float cur[maxControls] {}, mid[maxControls] {}, target[maxControls] {}, a = 0.98f, fadeStep = 0.0035f, fadeU = 1.0f;
            bool stepped[maxControls] {}, button[maxControls] {}, first = true;
            int fading = 0;      // -1 discesa, +1 risalita
        };

        /** Pezzo (campioni) con cui la famiglia vecchia elabora mentre un comando si muove; -1 = senza involucro, 0 = solo
            le dissolvenze dei selettori. */
        int smoothSub (const ModelDef& d)
        {
            switch (d.family)
            {
                case Family::Wah:          return Config (d.config).str ("type") == "inductor" ? 0 : 1;
                case Family::Compressor:
                case Family::GraphicEQ:
                case Family::ParametricEQ:
                case Family::BBDChorus:
                case Family::BBDFlanger:
                case Family::AnalogDelay:
                case Family::Phaser:
                case Family::Tremolo:
                case Family::DigitalDelay:
                case Family::TapeEcho:
                case Family::Reverb:
                case Family::Pitch:
                case Family::Synth:
                case Family::Acoustic:     return 1;
                // senza involucro: Circuit/AmpSim hanno la lisciatura della 1.2.7; Volume ha gia' un SmoothedValue per
                // campione; NoiseGate e SlowGear usano i comandi solo per soglie e costanti di tempo di inviluppi che
                // scorrono campione per campione; CabIR (convoluzione con stato a blocchi, LEVEL/MIX lisciati nella
                // famiglia), Tuner (dissolvenza del MUTE nella famiglia), Looper, Router, Splitter e Nam non leggono
                // comandi che producano gradini nel segnale o non tollerano blocchi spezzati
                default:                   return -1;
            }
        }
    }

    std::unique_ptr<Effect> createEffect (const ModelDef& d)
    {
        // modelli della tappa 3 e successive (type= nuovi): gia' lisciati, restano invariati
        if (auto fx = makeClassic (d)) return fx;
        auto fx = makeFamily (d);
        if (const int sub = smoothSub (d); sub >= 0 && fx != nullptr)
            return std::make_unique<SmoothFamily> (d, std::move (fx), sub, d.family == Family::Wah ? 5.0 : 12.0);
        return fx;
    }

    std::unique_ptr<Effect> createEffectRaw (const ModelDef& d)
    {
        if (auto fx = makeClassic (d)) return fx;
        return makeFamily (d);
    }

    /** Famiglie della prima versione del motore (percorso originale di createEffect, senza involucro). */
    static std::unique_ptr<Effect> makeFamily (const ModelDef& d)
    {
        switch (d.family)
        {
            case Family::Circuit:      return makeCircuit (d);
            case Family::Compressor:   return makeCompressor (d);
            case Family::NoiseGate:    return makeNoiseGate (d);
            case Family::GraphicEQ:    return makeGraphicEQ (d);
            case Family::ParametricEQ: return makeParametricEQ (d);
            case Family::Wah:          return makeWah (d);
            case Family::BBDChorus:
            case Family::BBDFlanger:
            case Family::AnalogDelay:  return makeBbd (d);
            case Family::Phaser:       return makePhaser (d);
            case Family::Tremolo:      return makeTremolo (d);
            case Family::DigitalDelay: return makeDigitalDelay (d);
            case Family::TapeEcho:     return makeTapeEcho (d);
            case Family::Reverb:       return makeReverb (d);
            case Family::Pitch:        return makePitch (d);
            case Family::Synth:        return makeSynth (d);
            case Family::Acoustic:     return makeAcoustic (d);
            case Family::AmpSim:       return makeCircuit (d);      // gli amp sono netlist con triodi
            case Family::CabIR:        return makeCabIR (d);
            case Family::Router:       return makeRouter (d);
            case Family::Volume:       return makeVolume (d);
            case Family::Tuner:        return makeTuner (d);
            case Family::Looper:       return makeLooper (d);
            case Family::SlowGear:     return makeSlowGear (d);
            case Family::Splitter:     return std::make_unique<SplitterEffect> (d);
            case Family::Nam:          return makeNam (d);
            case Family::Count:        break;
        }
        return makeVolume (d);
    }
}
