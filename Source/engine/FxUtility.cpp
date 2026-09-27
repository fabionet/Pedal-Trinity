/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Utility: selettori di linea e looper.

    Router, type=ab2 (passivo, bidirezionale; mai A e B insieme):
      ruolo "select" (A/B) + ruolo "direction":
        0 = SELETTORE  A,B -> OUT   (A = ingresso sinistro, B = ingresso destro)
        1 = SPLITTER   IN -> A|B    (A = uscita sinistra,  B = uscita destra)
    Router, type=ls2 (bufferizzato, 6 modi come il manuale):
      ruoli "mode" (6 posizioni), "levelA", "levelB" (muto..+20 dB, centro 0 dB), "select" (stato del pedale)
      Nel plugin i due "loop" sono i due canali della coppia stereo: A = sinistro, B = destro.
    Looper: registrazione/riproduzione/sovraincisione fino a 'maxs' secondi (default 60).
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        class RouterEffect : public Effect
        {
        public:
            explicit RouterEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "ab2");
                pSelect = role (*this, "select", 0.0f);
                pDir = role (*this, "direction", 0.0f);
                pMode = role (*this, "mode", 0.0f);
                pLevelA = role (*this, "levelA", 0.5f);
                pLevelB = role (*this, "levelB", 0.5f);
            }
            void prepare (double s, int) override { ga.reset (s, 0.01); gb.reset (s, 0.01); }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                if (numCh < 2) return;               // con un solo canale non c'e' nulla da instradare
                const bool b = pSelect.step (*this) > 0;
                auto levelOf = [] (float x) { return x < 0.02f ? 0.0f : dbToGain (x < 0.5f ? (x - 0.5f) * 80.0f : (x - 0.5f) * 40.0f); };
                if (type == "ab2")
                {
                    const bool splitter = pDir.step (*this) > 0;
                    ga.setTargetValue (b ? 0.0f : 1.0f);
                    gb.setTargetValue (b ? 1.0f : 0.0f);
                    for (int i = 0; i < n; ++i)
                    {
                        const float a = ga.getNextValue(), bb = gb.getNextValue();
                        if (splitter)
                        {
                            const float m = 0.5f * (ch[0][i] + ch[1][i]);
                            ch[0][i] = m * a;       // uscita A
                            ch[1][i] = m * bb;      // uscita B
                        }
                        else
                        {
                            const float sel = ch[0][i] * a + ch[1][i] * bb;   // ingresso A o B verso l'uscita
                            ch[0][i] = sel; ch[1][i] = sel;
                        }
                    }
                    return;
                }
                // LS-2: 0 BYPASS<->A, 1 BYPASS<->B, 2 A<->B, 3 BYPASS<->A+B, 4 A+B MIX, 5 OUTPUT SELECT
                const int mode = std::clamp (pMode.step (*this), 0, 5);
                const float la = levelOf (pLevelA.get (*this)), lb = levelOf (pLevelB.get (*this));
                for (int i = 0; i < n; ++i)
                {
                    const float inA = ch[0][i], inB = ch[1][i], mono = 0.5f * (inA + inB);
                    float l = inA, r = inB;
                    switch (mode)
                    {
                        case 0: if (b) { l = inA * la; r = inA * la; } break;
                        case 1: if (b) { l = inB * lb; r = inB * lb; } break;
                        case 2: { const float v = b ? inB * lb : inA * la; l = v; r = v; } break;
                        case 3: if (b) { const float v = inA * la + inB * lb; l = v; r = v; } break;
                        case 4: { const float v = inA * la + inB * lb; l = v; r = v; } break;
                        default: l = b ? 0.0f : mono * la; r = b ? mono * lb : 0.0f; break;   // OUTPUT SELECT
                    }
                    ch[0][i] = l; ch[1][i] = r;
                }
            }
        private:
            Config cfg;
            std::string type;
            RoleParam pSelect, pDir, pMode, pLevelA, pLevelB;
            juce::SmoothedValue<float> ga, gb;
        };

        //==============================================================================
        class LooperEffect : public Effect
        {
        public:
            enum State { Empty, Recording, Playing, Overdubbing, Stopped };
            enum Action { Pedal = 0, Stop = 1, Clear = 2, Undo = 3 };

            explicit LooperEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                maxSeconds = cfg.num ("maxs", 60);
                pLevel = role (*this, "level", 0.7f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                const size_t len = (size_t) (maxSeconds * sr);
                for (auto& b : buf) for (auto& c : b) c.assign (len, 0.0f);
                reset();
            }
            void reset() override { state = Empty; length = 0; pos = 0; cur = 0; undoFilled = 0; hasUndo = false; }
            void trigger (int action) override { pending.store (action + 1); }
            /** 0 = stato, 1 = posizione 0..1, 2 = durata in secondi */
            float readout (int i) const override
            {
                if (i == 0) return (float) stateOut.load();
                if (i == 1) return posOut.load();
                return lenOut.load();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                if (const int a = pending.exchange (0); a > 0) handle (a - 1);
                const float lvl = taperA (pLevel.get (*this)) * 2.0f;
                const size_t cap = buf[0][0].size();
                for (int i = 0; i < n; ++i)
                {
                    const bool capture = hasUndo && undoFilled < length;
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& loop = buf[cur][c];
                        const float in = ch[std::min (c, numCh - 1)][i];
                        float out = 0;
                        if (state == Recording && pos < cap)
                            loop[pos] = in;
                        else if (length > 0 && (state == Playing || state == Overdubbing))
                        {
                            out = loop[pos];
                            // cattura incrementale del contenuto precedente (per l'annullamento)
                            if (capture) buf[cur ^ 1][c][pos] = out;
                            if (state == Overdubbing) loop[pos] = out * 0.97f + in;
                        }
                        if (c < numCh) ch[c][i] = in + out * lvl;
                    }
                    if (capture && (state == Playing || state == Overdubbing)) ++undoFilled;
                    if (state == Recording) { if (++pos >= cap) { length = cap; pos = 0; state = Playing; } }
                    else if ((state == Playing || state == Overdubbing) && length > 0) { if (++pos >= length) pos = 0; }
                }
                stateOut.store ((int) state);
                posOut.store (length > 0 ? (float) pos / (float) length : 0.0f);
                lenOut.store ((float) ((state == Recording ? pos : length) / sr));
            }
        private:
            void handle (int a)
            {
                switch (a)
                {
                    case Pedal:
                        if (state == Empty || (state == Stopped && length == 0)) { state = Recording; pos = 0; hasUndo = false; }
                        else if (state == Recording) { length = pos; pos = 0; state = Playing; }
                        else if (state == Playing) { hasUndo = true; undoFilled = 0; state = Overdubbing; }
                        else if (state == Overdubbing) state = Playing;
                        else if (state == Stopped) { pos = 0; state = Playing; }
                        break;
                    case Stop: if (state != Empty) { state = length > 0 ? Stopped : Empty; pos = 0; } break;
                    case Clear: state = Empty; length = 0; pos = 0; hasUndo = false; break;
                    case Undo:
                        // scambio dei buffer: O(1), nessuna copia nel thread audio
                        if (hasUndo && undoFilled >= length) { cur ^= 1; hasUndo = false; if (state == Overdubbing) state = Playing; }
                        break;
                    default: break;
                }
            }

            Config cfg;
            double maxSeconds = 60, sr = 48000;
            RoleParam pLevel;
            std::vector<float> buf[2][2];
            size_t length = 0, pos = 0, undoFilled = 0;
            int cur = 0;
            State state = Empty;
            bool hasUndo = false;
            std::atomic<int> pending { 0 }, stateOut { 0 };
            std::atomic<float> posOut { 0 }, lenOut { 0 };
        };
    }

    std::unique_ptr<Effect> makeRouter (const ModelDef& d) { return std::make_unique<RouterEffect> (d); }
    std::unique_ptr<Effect> makeLooper (const ModelDef& d) { return std::make_unique<LooperEffect> (d); }
}
