/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Modulazioni della tappa 3 modellate sul circuito:
      * jfetphase  - phaser a 4 JFET in parallelo a una R fissa (conduttanza lineare nel
                     Vgs, tacche da fc = 1/(2 pi (R || Rds) C)), retroazione inseribile;
      * otaphase   - phaser a OTA (fc lineare con la corrente dell'LFO a rilassamento RC),
                     COLOR = rete di retroazione con passa-alto e salto di volume;
      * badstone   - 6 stadi con feedback variabile e posizione manuale;
      * optophase  - 6 stadi a optocoppia (LDR asimmetrica) con LFO o inviluppo modulato;
      * vibe       - 4 stadi a LDR con condensatori sfalsati e lampada con inerzia termica;
      * worm       - wah/phaser/tremolo/vibrato con LFO scollegabile;
      * pulsar     - tremolo a simmetria variabile, uscita stereo invertita;
      * bbdmod     - chorus/flanger/vibrato a BBD a clock reale (MXR, Small Clone, Electric
                     Mistress, Polychorus, Clone Theory): VCO del clock dall'LFO,
                     compander, anti-alias/ricostruzione, pre/de-enfasi, rumore della linea;
      * leslie     - cassa rotante con overdrive e compressore;
      * mod11      - multi-modulazione digitale a 11 algoritmi.
*/

#include "FxClassic.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        /** Dissolvenza per i cambi di algoritmo/modo: scende a 0 in 8 ms, cambia, risale. */
        struct ModeFader
        {
            int current = -1, pending = -1;
            float g = 1, step = 0.003f;
            void prepare (double sr) { step = (float) (1.0 / (0.015 * sr)); }
            /** Restituisce il guadagno; 'changed' = vero nel campione in cui si applica il nuovo modo. */
            inline float tick (int wanted, bool& changed) noexcept
            {
                changed = false;
                if (current < 0) { current = wanted; changed = true; }
                if (wanted != current) pending = wanted;
                if (pending >= 0)
                {
                    g -= step;
                    if (g <= 0) { g = 0; current = pending; pending = -1; changed = true; }
                }
                else if (g < 1) g = std::min (1.0f, g + step);
                return g;
            }
        };

        //==============================================================================
        /** Phaser a JFET (Phase 90 / EVH90). */
        class JfetPhaser final : public Effect
        {
        public:
            explicit JfetPhaser (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                stages = std::clamp ((int) cfg.num ("stages", 4), 2, 8);
                r = (float) cfg.num ("r", 24000); c = (float) cfg.num ("c", 47e-9);
                ron = (float) cfg.num ("ron", 2500);
                umin = (float) cfg.num ("umin", 0.04); umax = (float) cfg.num ("umax", 1.0);
                auto rt = cfg.list ("rate"); while (rt.size() < 3) rt.push_back (rt.empty() ? 1.0 : rt.back() * 2);
                r0 = (float) rt[0]; r1 = (float) rt[1]; r2 = (float) rt[2];
                fbBlock = (float) cfg.num ("fb", 0.3);
                gain = (float) cfg.num ("gain", 1.25);
                pRate = role (*this, "rate", 0.35f);
                pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override { sr = (float) s; fbS.set (s, 15); rateS.set (s, 30); reset(); }
            void reset() override { for (auto& a : ap) a.reset(); fb = 0; lfo.ph = 0.25; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float fbT = pMode.step (*this) == 0 ? fbBlock : 0.0f;      // SCRIPT toglie la R di retroazione
                for (int i = 0; i < n; ++i)
                {
                    lfo.setHz (sr, rateS.next (map3 (pRate.get (*this), r0, r1, r2)));
                    const float u = umin + (umax - umin) * (0.5f + 0.5f * lfo.tri());
                    lfo.step();
                    const float rds = ron / u;
                    const float R = r * rds / (r + rds);
                    const float a = apCoef (1.0f / (2.0f * pi * R * c), sr);
                    const float k = fbS.next (fbT);
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float v = x + k * fb;
                    for (int s = 0; s < stages; ++s) v = ap[s].tick (v, a);
                    fb = v;
                    const float y = 0.5f * (x + v) * gain;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            Config cfg;
            int stages = 4;
            float sr = 48000, r = 24000, c = 47e-9f, ron = 2500, umin = 0.04f, umax = 1, r0 = 0.37f, r1 = 1.9f, r2 = 7.7f,
                  fbBlock = 0.3f, gain = 1.25f, fb = 0;
            RoleParam pRate, pMode;
            Allpass1 ap[8];
            Lfo lfo;
            Smooth fbS, rateS;
        };

        //==============================================================================
        /** Phaser a OTA (Small Stone) e Bad Stone a 6 stadi. */
        class OtaPhaser final : public Effect
        {
        public:
            OtaPhaser (const ModelDef& d, bool bad) : Effect (d), cfg (d.config), badStone (bad)
            {
                stages = std::clamp ((int) cfg.num ("stages", bad ? 6 : 4), 2, 8);
                auto f = cfg.list ("fc"); fLo = f.size() > 0 ? (float) f[0] : 150.0f; fHi = f.size() > 1 ? (float) f[1] : 2600.0f;
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 8.0f;
                relax = (float) cfg.num ("relax", 0.22);
                fbCol = (float) cfg.num ("fb", 0.55); fbHp = (float) cfg.num ("fbhp", 480); colGain = (float) cfg.num ("colgain", 1.3);
                fbMax = (float) cfg.num ("fbmax", 0.85);
                pRate = role (*this, "rate", 0.35f);
                pMode = role (*this, "mode", 0.0f);
                pRes = role (*this, "res", 0.4f);
                pManual = role (*this, "manual", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s; hp.set (s, fbHp); fbS.set (s, 20); gS.set (s, 20); posS.set (s, 15); rateS.set (s, 30); reset();
            }
            void reset() override { for (auto& a : ap) a.reset(); fb = 0; hp.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool modeOn = pMode.step (*this) == 1;
                float fbT, gT;
                if (badStone) { fbT = pRes.get (*this) * fbMax; gT = 1.15f; }
                else { fbT = modeOn ? fbCol : 0.0f; gT = modeOn ? colGain : 1.0f; }
                const bool manual = badStone && modeOn;
                for (int i = 0; i < n; ++i)
                {
                    const float hz = rateS.next (logMap (pRate.get (*this), rLo, rHi));
                    float lfoV;
                    if (badStone) { lfo.setHz (sr, hz); lfoV = lfo.tri(); lfo.step(); }
                    else { osc.set (sr, hz, relax); lfoV = osc.tick(); }
                    float fc;
                    if (badStone)
                    {
                        // elemento CMOS: andamento esponenziale della frequenza con la tensione
                        const float pos = posS.next (manual ? pManual.get (*this) : 0.5f + 0.5f * lfoV);
                        fc = fLo * std::pow (fHi / fLo, pos);
                    }
                    else fc = fLo + (fHi - fLo) * (0.5f + 0.5f * lfoV);   // OTA: fc proporzionale alla corrente
                    const float a = apCoef (fc, sr);
                    const float k = fbS.next (fbT), g = gS.next (gT);
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float v = x + k * (badStone ? fb : hp.hp (fb));
                    v = softSat (v * 0.5f) * 2.0f;
                    for (int s = 0; s < stages; ++s) v = ap[s].tick (v, a);
                    fb = v;
                    const float y = 0.5f * (x + v) * g * 1.1f;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            Config cfg;
            bool badStone;
            int stages = 4;
            float sr = 48000, fLo = 150, fHi = 2600, rLo = 0.1f, rHi = 8, relax = 0.22f, fbCol = 0.55f, fbHp = 480, colGain = 1.3f,
                  fbMax = 0.85f, fb = 0;
            RoleParam pRate, pMode, pRes, pManual;
            Allpass1 ap[8];
            RelaxOsc osc;
            Lfo lfo;
            OnePole hp;
            Smooth fbS, gS, posS, rateS;
        };

        //==============================================================================
        /** Phaser a optocoppie con LFO o inviluppo (Poly Phase). */
        class OptoPhaser final : public Effect
        {
        public:
            explicit OptoPhaser (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                stages = std::clamp ((int) cfg.num ("stages", 6), 2, 8);
                c = (float) cfg.num ("c", 10e-9);
                rmin = cfg.num ("rmin", 4000); rmax = cfg.num ("rmax", 80000);
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 10.0f;
                fbMax = (float) cfg.num ("fbmax", 0.8);
                atk = cfg.num ("ldra", 4); rel = cfg.num ("ldrr", 40);
                pRes = role (*this, "res", 0.35f); pRate = role (*this, "rate", 0.35f);
                pSens = role (*this, "sens", 0.5f); pEnvMod = role (*this, "envmod", 0.3f);
                pModRate = role (*this, "modrate", 0.3f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                ldr.set (s, atk, rel, rmin, rmax, 0.8f);
                env.set (s, 4, 160);
                fbS.set (s, 20);
                reset();
            }
            void reset() override { for (auto& a : ap) a.reset(); fb = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool envMode = pMode.step (*this) == 1;
                lfo.setHz (sr, logMap (pRate.get (*this), rLo, rHi));
                lfo2.setHz (sr, logMap (pModRate.get (*this), rLo, rHi));
                const float sens = 40.0f * taperA (pSens.get (*this)), em = pEnvMod.get (*this);
                const float fbT = pRes.get (*this) * fbMax;
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float light;
                    if (envMode)
                    {
                        const float e = env.tick (x);
                        light = std::clamp (sens * e, 0.0f, 1.0f) * (1.0f - em * (0.5f + 0.5f * lfo2.tri()));
                    }
                    else light = 0.5f + 0.5f * lfo.tri();
                    lfo.step(); lfo2.step();
                    const float R = ldr.tick (light);
                    const float a = apCoef (1.0f / (2.0f * pi * R * c), sr);
                    float v = x + fbS.next (fbT) * fb;
                    for (int s = 0; s < stages; ++s) v = ap[s].tick (v, a);
                    fb = v;
                    const float y = 0.5f * (x + v) * 1.15f;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            Config cfg;
            int stages = 6;
            float sr = 48000, c = 10e-9f, rLo = 0.1f, rHi = 10, fbMax = 0.8f, fb = 0;
            double rmin = 4000, rmax = 80000, atk = 4, rel = 40;
            RoleParam pRes, pRate, pSens, pEnvMod, pModRate, pMode;
            Allpass1 ap[8];
            Ldr ldr;
            Lfo lfo, lfo2;
            Envelope env;
            Smooth fbS;
        };

        //==============================================================================
        /** Vibe a fotocellule (Good Vibes, Uni-Vibe). */
        class VibeFx final : public Effect
        {
        public:
            explicit VibeFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto cs = cfg.list ("caps");
                if (cs.size() < 4) cs = { 15e-9, 220e-9, 470e-12, 4.7e-9 };
                for (int k = 0; k < 4; ++k) caps[k] = (float) cs[(size_t) k];
                rmin = cfg.num ("rmin", 25000); rmax = cfg.num ("rmax", 600000);
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.5f; rHi = rt.size() > 1 ? (float) rt[1] : 10.0f;
                lampMs = cfg.num ("lamp", 25); atk = cfg.num ("ldra", 6); rel = cfg.num ("ldrr", 70);
                pLevel = role (*this, "level", 0.8f); pDepth = role (*this, "depth", 0.6f);
                pRate = role (*this, "rate", 0.4f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                static const float spread[4] = { 1.0f, 1.12f, 0.9f, 1.05f };     // tolleranza delle 4 LDR
                for (int k = 0; k < 4; ++k) ldr[k].set (s, atk * spread[k], rel * spread[k], rmin * spread[k], rmax * spread[k], 0.75f);
                lampA = (float) std::exp (-1.0 / (lampMs * 0.001 * s));
                volS.set (s, 20); wetS.set (s, 15);
                reset();
            }
            void reset() override { for (auto& a : ap) a.reset(); lamp = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                lfo.setHz (sr, logMap (pRate.get (*this), rLo, rHi));
                const float depth = pDepth.get (*this);
                const float vol = 1.3f * std::pow (pLevel.get (*this), 1.6f);
                const bool vib = pMode.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    // oscillatore a sfasamento: sinusoide un po' asimmetrica; la lampada scalda col quadrato della tensione
                    const float s = lfo.sine() + 0.12f * lfo.sine (0.25) * lfo.sine();
                    lfo.step();
                    const float drive = (1.0f - depth) * 0.35f + depth * (0.5f + 0.5f * s);
                    lamp = drive * drive + lampA * (lamp - drive * drive);
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float v = x;
                    for (int k = 0; k < 4; ++k)
                    {
                        const float R = ldr[k].tick (std::sqrt (std::max (0.0f, lamp)));
                        v = ap[k].tick (v, apCoef (1.0f / (2.0f * pi * R * caps[k]), sr));
                    }
                    const float w = wetS.next (vib ? 1.0f : 0.5f);
                    const float y = volS.next (vol) * ((1.0f - w) * x + w * v) * (1.4f - 0.8f * (w - 0.5f));
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            Config cfg;
            float caps[4] {}, sr = 48000, rLo = 0.5f, rHi = 10, lampA = 0.99f, lamp = 0;
            double rmin = 25000, rmax = 6e5, lampMs = 25, atk = 6, rel = 70;
            RoleParam pLevel, pDepth, pRate, pMode;
            Allpass1 ap[4];
            Ldr ldr[4];
            Lfo lfo;
            Smooth volS, wetS;
        };

        //==============================================================================
        /** The Worm: wah, phaser, tremolo, vibrato. */
        class WormFx final : public Effect
        {
        public:
            explicit WormFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 10.0f;
                pMode = role (*this, "mode", 1.0f); pDepth = role (*this, "depth", 0.6f);
                pRate = role (*this, "rate", 0.4f); pManual = role (*this, "manual", 0.0f);
            }
            void prepare (double s, int) override { sr = (float) s; fader.prepare (s); posS.set (s, 10); reset(); }
            void reset() override { for (auto& a : ap) a.reset(); wah.reset(); fb = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                lfo.setHz (sr, logMap (pRate.get (*this), rLo, rHi));
                const bool manual = pManual.step (*this) == 1;
                const float depth = pDepth.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (pMode.step (*this), changed);
                    if (changed) reset();
                    const float l = lfo.tri(); lfo.step();
                    const float pos = posS.next (manual ? depth : depth * (0.5f + 0.5f * l));
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float y;
                    switch (fader.current)
                    {
                        case 0:     // wah: passa-banda risonante spazzato
                        {
                            wah.setG (fastTanPi (350.0f * std::pow (7.0f, pos), sr), 1.0f / 5.0f);
                            y = 2.2f * wah.bp (x);
                            break;
                        }
                        case 1:     // phaser a 4 stadi
                        {
                            const float a = apCoef (150.0f * std::pow (20.0f, pos), sr);
                            float v = x + 0.3f * fb;
                            for (int s = 0; s < 4; ++s) v = ap[s].tick (v, a);
                            fb = v;
                            y = 0.5f * (x + v) * 1.2f;
                            break;
                        }
                        case 2:     // tremolo a VCA
                            y = x * (1.0f - (manual ? depth : depth * (0.5f + 0.5f * l)));
                            break;
                        default:    // vibrato: 8 all-pass senza dry
                        {
                            const float a = apCoef (300.0f * std::pow (8.0f, pos), sr);
                            float v = x;
                            for (int s = 0; s < 8; ++s) v = ap[s].tick (v, a);
                            y = v;
                            break;
                        }
                    }
                    y *= fg;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            Config cfg;
            float sr = 48000, rLo = 0.1f, rHi = 10, fb = 0;
            RoleParam pMode, pDepth, pRate, pManual;
            Allpass1 ap[8];
            Svf wah;
            Lfo lfo;
            ModeFader fader;
            Smooth posS;
        };

        //==============================================================================
        /** Stereo Pulsar: tremolo a forma variabile. */
        class PulsarFx final : public Effect
        {
        public:
            explicit PulsarFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.3f; rHi = rt.size() > 1 ? (float) rt[1] : 15.0f;
                pDepth = role (*this, "depth", 0.6f); pShape = role (*this, "shape", 0.5f);
                pRate = role (*this, "rate", 0.4f); pWave = role (*this, "wave", 0.0f);
            }
            void prepare (double s, int) override { sr = (float) s; edge[0].set (s, 120); edge[1].set (s, 120); shS.set (s, 20); dS.set (s, 20); wS.set (s, 8); }
            void reset() override { lfo.ph = 0; edge[0].reset(); edge[1].reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                lfo.setHz (sr, logMap (pRate.get (*this), rLo, rHi));
                const bool square = pWave.step (*this) == 1;
                // DEPTH: profondita' piena a ore 2 (0,8 della corsa), oltre il guadagno attraversa lo zero
                const float dk = pDepth.get (*this);
                const float dT = dk <= 0.8f ? dk / 0.8f : 1.0f + (dk - 0.8f) / 0.2f;
                for (int i = 0; i < n; ++i)
                {
                    const float sh = shS.next (pShape.get (*this)), depth = dS.next (dT);
                    const float duty = 0.08f + 0.84f * sh;
                    const float uq = lfo.ph < duty ? 1.0f : 0.0f;          // quadra a impulso variabile
                    const float ut = 0.5f + 0.5f * lfo.skew (1.0f - sh);   // triangolo/dente
                    lfo.step();
                    const float ws = wS.next (square ? 1.0f : 0.0f);
                    const float uA = ws * edge[0].lp (uq) + (1.0f - ws) * ut;
                    const float uB = ws * edge[1].lp (1.0f - uq) + (1.0f - ws) * (1.0f - ut);
                    const float gA = 1.0f - depth * uA, gB = 1.0f - depth * uB;
                    if (numCh > 1)
                    {
                        const float x = 0.5f * (ch[0][i] + ch[1][i]);
                        ch[0][i] = x * gA; ch[1][i] = x * gB;
                    }
                    else ch[0][i] *= gA;
                }
            }
        private:
            Config cfg;
            float sr = 48000, rLo = 0.3f, rHi = 15;
            RoleParam pDepth, pShape, pRate, pWave;
            Lfo lfo;
            OnePole edge[2];
            Smooth shS, dS, wS;
        };

        //==============================================================================
        /** Modulazioni a BBD con il clock reale. */
        class BbdModFx final : public Effect
        {
        public:
            enum Model { Zw38, M117, Mistress, Poly, Clone, CTheory };

            explicit BbdModFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "clone");
                model = m == "zw38" ? Zw38 : m == "m117" ? M117 : m == "mistress" ? Mistress : m == "poly" ? Poly
                      : m == "ctheory" ? CTheory : Clone;
                stages = (int) cfg.num ("stages", 1024);
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? (float) dl[0] : 1.0f; dHi = dl.size() > 1 ? (float) dl[1] : 10.0f;
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 10.0f;
                const auto lf = cfg.str ("lfo", "tri");
                lfoShape = lf == "sine" ? 1 : lf == "relax" ? 2 : 0;
                useComp = cfg.num ("comp", 0) > 0.5;
                aa = cfg.num ("aa", 9000);
                fbMax = (float) cfg.num ("fbmax", 0.9);
                noiseV = (float) cfg.num ("noise", 0.0003);
                sat = (float) cfg.num ("sat", 1.2);
                auto mx = cfg.list ("mix"); mixDry = mx.size() > 0 ? (float) mx[0] : 0.5f; mixWet = mx.size() > 1 ? (float) mx[1] : 0.5f;
                swing = cfg.list ("swing"); if (swing.empty()) swing = { 1.0 };
                pre = cfg.num ("pre", 0) > 0.5;
                lowPts = cfg.list ("low"); highPts = cfg.list ("high");
                evh = cfg.list ("evh");
                pRate = role (*this, "rate", 0.3f); pDepth = role (*this, "depth", 0.5f);
                pLevel = role (*this, "level", 0.7f); pManual = role (*this, "manual", 0.5f);
                pRes = role (*this, "res", 0.0f); pRange = role (*this, "range", 0.6f);
                pMode = role (*this, "mode", 0.0f); pBlend = role (*this, "blend", 1.0f);
                pLow = role (*this, "low", 0.3f); pHigh = role (*this, "high", 0.75f);
                pPreset = role (*this, "preset", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                bbd.allocate (stages);
                bbd.setLoss (std::min (0.3f, stages * 6.0e-5f));
                bbd.setNoise (noiseV);
                for (auto* f : { &aaIn1, &aaOut1 }) f->set (s, aa, 0.5412);
                for (auto* f : { &aaIn2, &aaOut2 }) f->set (s, aa, 1.3066);
                comp.set (s, 10, 10, 0.3f);
                preF.set (s, 3000); deF.set (s, 3000 / 2.5);
                dcF.set (s, 25);
                fbS.set (s, 20); posS.set (s, 6); rateS.set (s, 40); mixS.set (s, 20); lvlS.set (s, 20); wetS.set (s, 20); lowS.set (s, 30); highS.set (s, 30);
                reset();
            }
            void reset() override
            {
                bbd.clear(); aaIn1.reset(); aaIn2.reset(); aaOut1.reset(); aaOut2.reset(); comp.reset();
                preF.reset(); deF.reset(); dcF.reset(); lowF.reset(); highF.reset(); low2.reset(); fbState = 0;
                lfo.ph = 0.25;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // comandi letti per blocco
                const int mode = pMode.step (*this);
                const bool evhOn = model == M117 && pPreset.step (*this) == 1 && evh.size() >= 4;
                float rateK = pRate.get (*this), depth = pDepth.get (*this), manual = pManual.get (*this), res = pRes.get (*this);
                if (evhOn) { manual = (float) evh[0]; depth = (float) evh[1]; rateK = (float) evh[2]; res = (float) evh[3]; }
                float level = 1.0f, dryG = mixDry, wetG = mixWet;
                // ritardi della modalita' (Polychorus) e taglio di ricostruzione
                float lo = dLo, hi = dHi;
                if (model == Poly)
                {
                    static const float pl[4][2] = { { 0.5f, 8.0f }, { 0.5f, 8.0f }, { 6.0f, 25.0f }, { 40.0f, 100.0f } };
                    const int m = std::clamp (mode, 0, 3);
                    lo = pl[m][0]; hi = pl[m][1];
                    const bool blendOn = pBlend.step (*this) == 1;
                    dryG = blendOn ? 0.6f : 0.0f; wetG = blendOn ? 0.6f : 1.0f;
                }
                if (model == Zw38)
                {
                    level = 1.6f * taperA (pLevel.get (*this));
                    const float lowHz = lowPts.size() >= 3 ? map3 (pLow.get (*this), (float) lowPts[0], (float) lowPts[1], (float) lowPts[2]) : 70.0f;
                    const float highHz = highPts.size() >= 3 ? map3 (pHigh.get (*this), (float) highPts[0], (float) highPts[1], (float) highPts[2]) : 20000.0f;
                    lowT = lowHz; highT = std::min (highHz, 20000.0f);
                }
                if (model == CTheory)
                {
                    const float b = pBlend.get (*this);
                    dryG = 0.7f * (1.0f - b); wetG = 0.7f + 0.3f * b;
                }
                const float fbT = model == Clone || model == CTheory || model == Zw38 ? 0.0f : res * fbMax;
                const float lnR = std::log (hi / lo);
                // escursione del clock: chorus intorno al centro geometrico, flanger dal MANUAL, Mistress dal RANGE
                const float sw = (float) swing[(size_t) std::clamp (model == Clone ? (int) depth : 0, 0, (int) swing.size() - 1)];
                const double recon = model == Poly && mode == 3 ? std::min (aa, 2000.0) : aa;
                if (std::abs (recon - curRecon) > 1.0)
                {
                    curRecon = recon;
                    aaOut1.set (sr, recon, 0.5412); aaOut2.set (sr, recon, 1.3066);
                    aaIn1.set (sr, recon, 0.5412); aaIn2.set (sr, recon, 1.3066);
                }
                for (int i = 0; i < n; ++i)
                {
                    const float hz = rateS.next (logMap (rateK, rLo, rHi));
                    lfo.setHz (sr, hz);
                    float l;
                    switch (lfoShape) { case 1: l = lfo.sine(); break; default: l = lfo.tri(); break; }
                    lfo.step();
                    float pos;   // posizione 0..1 nel logaritmo del ritardo (0 = minimo)
                    switch (model)
                    {
                        case Zw38:    pos = 0.5f + 0.5f * depth * l; break;
                        case M117:    pos = std::clamp ((1.0f - manual) + 0.5f * depth * l, 0.0f, 1.0f); break;
                        case Mistress:
                        {
                            const float top = 0.1f + 0.9f * pRange.get (*this) * sw;
                            pos = mode == 1 ? pRange.get (*this) : top * (0.5f + 0.5f * l);
                            break;
                        }
                        case Poly:    pos = mode == 0 ? manual : std::clamp (manual + 0.5f * depth * l, 0.0f, 1.0f); break;
                        case Clone:   pos = 0.5f + 0.5f * sw * l; break;
                        default:      pos = 0.5f + 0.45f * depth * l; break;
                    }
                    pos = posS.next (pos);
                    const float dMs = lo * std::exp (lnR * pos);
                    const double cps = (double) stages / (2.0 * dMs * 0.001) / sr;

                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float v = x + fbS.next (fbT) * fbState;
                    if (pre) v = preEm (v);
                    if (useComp) v = comp.compress (v);
                    v = aaIn2.lp (aaIn1.lp (v));
                    v = sat * softSat (v / sat);
                    float y = bbd.tick (v, cps);
                    y = aaOut2.lp (aaOut1.lp (y));
                    if (useComp) y = comp.expand (y);
                    if (pre) y = deEm (y);
                    y = dcF.hp (y);
                    fbState = softSat (y);
                    float wet = y;
                    if (model == Zw38)
                    {
                        lowF.setG (fastTanPi (lowS.next (lowT), (float) sr), 1.4142f);    // passa-alto 12 dB/ott
                        highF.setFast ((float) sr, highS.next (highT));                    // passa-basso 6 dB/ott
                        wet = lowF.hp (wet);
                        wet = highF.lp (wet);
                    }
                    const float lv = lvlS.next (level);
                    const float dg = mixS.next (dryG);
                    if (model == Zw38 && numCh > 1)
                    {
                        ch[0][i] = wet * lv;         // uscita MONO con THRU collegato: solo il ritardato
                        ch[1][i] = x;                // THRU: dry bufferizzato
                    }
                    else
                    {
                        const float out = model == Zw38 ? x + lv * wet : dg * x + wetS.next (wetG) * wet;
                        for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = out;
                    }
                }
            }
        private:
            /** pre-enfasi (mensola +8 dB sopra ~1,2 kHz) e de-enfasi inversa esatta. */
            inline float preEm (float x) noexcept { const float l = preF.lp (x); return l + 2.5f * (x - l); }
            inline float deEm (float x) noexcept { const float l = deF.lp (x); return l + (x - l) / 2.5f; }

            Config cfg;
            Model model = Clone;
            int stages = 1024, lfoShape = 0;
            float dLo = 1, dHi = 10, rLo = 0.1f, rHi = 10, fbMax = 0.9f, noiseV = 3e-4f, sat = 1.2f, mixDry = 0.5f, mixWet = 0.5f, fbState = 0;
            double sr = 48000, aa = 9000, curRecon = -1;
            bool useComp = false, pre = false;
            std::vector<double> swing, lowPts, highPts, evh;
            RoleParam pRate, pDepth, pLevel, pManual, pRes, pRange, pMode, pBlend, pLow, pHigh, pPreset;
            BbdLine bbd;
            Svf aaIn1, aaIn2, aaOut1, aaOut2, lowF, low2;
            OnePole highF, preF, deF, dcF;
            Compander comp;
            Lfo lfo;
            Smooth fbS, posS, rateS, mixS, lvlS, wetS, lowS, highS;
            float lowT = 70, highT = 20000;
        };

        //==============================================================================
        /** Cassa rotante con overdrive e compressore (Lester G). */
        class LeslieFx final : public Effect
        {
        public:
            explicit LeslieFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                slow = cfg.list ("slow"); fast = cfg.list ("fast");
                if (slow.size() < 3) slow = { 0.1, 0.8, 3.2 };
                if (fast.size() < 3) fast = { 1.55, 6.2, 24.8 };
                pLevel = role (*this, "level", 0.55f); pSlow = role (*this, "slow", 0.5f); pFast = role (*this, "fast", 0.5f);
                pDrive = role (*this, "drive", 0.25f); pAttack = role (*this, "attack", 0.4f); pSustain = role (*this, "sustain", 0.0f);
                pBalance = role (*this, "balance", 0.5f); pAccel = role (*this, "accel", 0.5f);
                pSquash = role (*this, "squash", 0.0f); pSpeed = role (*this, "speed", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s; rot.prepare (s); volS.set (s, 20); drvS.set (s, 20); balS.set (s, 25); compG.set (s, 4); preHp.set (s, 40);
                reset();
            }
            void reset() override { rot.reset(); env = 0; first = true; preHp.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool isFast = pSpeed.step (*this) == 1;
                const float horn = isFast ? map3 (pFast.get (*this), (float) fast[0], (float) fast[1], (float) fast[2])
                                          : map3 (pSlow.get (*this), (float) slow[0], (float) slow[1], (float) slow[2]);
                const float drum = horn * (isFast ? 0.952f : 0.875f);
                const float tau = logMap (pAccel.get (*this), 3.0f, 0.25f);      // ACCELERATION: tempo di transizione
                rot.set (horn, drum, tau, tau * 3.5f, pBalance.get (*this));
                if (first) { rot.jump(); first = false; }
                // compressore: SUSTAIN tutto a sinistra = escluso
                const float sus = pSustain.get (*this);
                const bool squash = pSquash.step (*this) == 1;
                const float ratio = 1.0f + sus * (squash ? 9.0f : 3.0f);
                const float thr = squash ? 0.02f : 0.05f;
                const float atk = (float) std::exp (-1.0 / (logMap (pAttack.get (*this), 0.5f, 80.0f) * 0.001 * sr));
                const float relC = (float) std::exp (-1.0 / ((squash ? 0.12 : 0.25) * sr));
                const float dr = 1.0f + 24.0f * pDrive.get (*this) * pDrive.get (*this);
                const float vol = std::pow (pLevel.get (*this) / 0.55f, 1.5f);          // unita' al valore di fabbrica
                for (int i = 0; i < n; ++i)
                {
                    float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    {
                        const float a = std::abs (x);
                        env = a > env ? a + atk * (env - a) : a + relC * (env - a);
                        const float over = env / thr;
                        const float g = sus <= 0.001f ? 1.0f
                                      : over > 1.0f ? std::pow (over, 1.0f / ratio - 1.0f) * (1.0f + 0.5f * sus * (ratio - 1.0f) / ratio) : 1.0f + 0.5f * sus;
                        x *= compG.next (g);
                    }
                    // preamp valvolare: asimmetria e compensazione
                    const float d = drvS.next (dr);
                    const float z = d * x;
                    x = preHp.hp ((std::tanh (z + 0.15f * z * z) - 0.0f) / std::max (1.0f, 0.6f * d));
                    float l, r;
                    rot.setBalance (balS.next (pBalance.get (*this)));
                    rot.tick (x, l, r);
                    const float g = volS.next (vol);
                    if (numCh > 1) { ch[0][i] = l * g; ch[1][i] = r * g; }
                    else ch[0][i] = 0.5f * (l + r) * g;
                }
            }
        private:
            Config cfg;
            std::vector<double> slow, fast;
            float sr = 48000, env = 0;
            bool first = true;
            RoleParam pLevel, pSlow, pFast, pDrive, pAttack, pSustain, pBalance, pAccel, pSquash, pSpeed;
            Rotary rot;
            Smooth volS, drvS, balS, compG;
            OnePole preHp;
        };

        //==============================================================================
        /** MOD 11: undici algoritmi digitali. */
        class Mod11Fx final : public Effect
        {
        public:
            explicit Mod11Fx (const ModelDef& d) : Effect (d)
            {
                pDepth = role (*this, "depth", 0.5f); pRate = role (*this, "rate", 0.4f);
                pColor = role (*this, "color", 0.5f); pType = role (*this, "type", 0.4f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& l : dl) l.allocate ((int) (0.05 * s));
                dry.allocate ((int) (0.02 * s));
                rot.prepare (s);
                shifter.prepare (s, 50);
                for (int k = 0; k < 4; ++k) ldr[k].set (s, 6, 70, 25000, 600000, 0.75f);
                cross.set (s, 700, 0.707);
                bassLp.set (s, 250); bassLp2.set (s, 250);
                fader.prepare (s);
                colS.set (s, 20); depS.set (s, 20); rateS.set (s, 40);
                reset();
            }
            void reset() override
            {
                for (auto& l : dl) l.clear();
                dry.clear();
                rot.reset(); shifter.clear();
                for (auto& a : ap) a.reset();
                filt.reset(); cross.reset(); bassLp.reset(); bassLp2.reset(); edge.reset();
                fb = 0; lamp = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int typeWanted = pType.step (*this) * 4 + std::clamp (pMode.step (*this), 0, 2);
                const float rk = pRate.get (*this);
                const bool frozen = rk < 0.02f;                               // RATE al minimo: LFO fermo
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (typeWanted, changed);
                    if (changed) reset();
                    const int type = fader.current / 4, mode = fader.current % 4;
                    const float hz = rateS.next (logMap (rk, 0.05f, 10.0f));
                    lfo.setHz (sr, hz);
                    const float depth = depS.next (pDepth.get (*this)), color = colS.next (pColor.get (*this));
                    float s = frozen ? 2.0f * depth - 1.0f : lfo.sine(), t = frozen ? 2.0f * depth - 1.0f : lfo.tri();
                    if (! frozen) lfo.step();
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    const float vol = type <= 5 ? (color < 0.5f ? 4.0f * color * color : 2.0f * color) : 1.0f;   // unita' a ore 12, +6 dB al massimo
                    float y = x, yR = 0; bool stereo = false;
                    const float dep = frozen ? 1.0f : depth;
                    switch (type)
                    {
                        case 0:   // TREM: sinusoide, quadra, rampa
                        {
                            float w = mode == 0 ? s : mode == 1 ? edge.lp (lfo.square()) : lfo.saw();
                            if (mode == 1) edge.setFast (sr, 150.0f);
                            y = x * (1.0f - dep * (0.5f + 0.5f * w));
                            break;
                        }
                        case 1:   // HARM: bassi e acuti in controfase
                        {
                            cross.setG (fastTanPi (mode == 1 ? 1200.0f : 700.0f, sr), 1.4142f);
                            float lo, bp, hi; cross.tick (x, lo, bp, hi); hi = x - lo;
                            y = lo * (1.0f - dep * (0.5f + 0.5f * s)) + hi * (1.0f - dep * (0.5f - 0.5f * s));
                            break;
                        }
                        case 2:   // VIBR: ritardo modulato senza dry
                        {
                            dl[0].push (x);
                            const float w = mode == 1 ? t : s;
                            y = dl[0].read ((0.005f + 0.003f * dep * w) * sr);
                            break;
                        }
                        case 3:   // UNI: 4 stadi a LDR sfalsati
                        {
                            const float drive = 0.25f + 0.75f * dep * (0.5f + 0.5f * s);
                            lamp = drive * drive + 0.9992f * (lamp - drive * drive);
                            static const float caps[4] = { 15e-9f, 220e-9f, 470e-12f, 4.7e-9f };
                            float v = x;
                            for (int k = 0; k < 4; ++k)
                                v = ap[k].tick (v, apCoef (1.0f / (2.0f * pi * ldr[k].tick (std::sqrt (lamp)) * caps[k]), sr));
                            y = mode == 1 ? v : 0.7f * (x + v);
                            break;
                        }
                        case 4:   // CHORUS: singolo, tri-chorus, bass chorus
                        {
                            dl[0].push (x);
                            if (mode == 1)
                            {
                                float w = 0;
                                for (int k = 0; k < 3; ++k) w += dl[0].read ((0.008f + 0.004f * dep * lfo.sine (k / 3.0)) * sr);
                                y = 0.6f * x + 0.35f * w;
                            }
                            else
                            {
                                const float w = dl[0].read ((0.007f + 0.004f * dep * t) * sr);
                                if (mode == 2)
                                {
                                    // bass chorus: sotto 250 Hz resta il dry, solo gli acuti sono modulati
                                    const float lo = bassLp.lp (x), wHi = w - bassLp2.lp (w);
                                    y = lo + 0.7f * ((x - lo) + wHi);
                                }
                                else y = 0.7f * (x + w);
                            }
                            break;
                        }
                        case 5:   // ROTARY: DEPTH = bilanciamento tamburo/tromba
                        {
                            const float horn = (mode == 1 ? 6.7f : 0.8f) * logMap (rk, 0.6f, 1.6f);
                            rot.set (horn, horn * 0.9f, 0.9, 3.0, depth);
                            float l, r; rot.tick (x, l, r);
                            y = l; yR = r; stereo = true;
                            break;
                        }
                        case 6:   // FLANGE: COLOR = feedback (positivo / negativo)
                        {
                            const float d = (0.0005f + 0.0075f * (0.5f + 0.5f * dep * t)) * sr;
                            dl[0].push (x + (mode == 1 ? -0.9f : 0.9f) * color * fb);
                            fb = dl[0].read (d);
                            y = 0.6f * (x + fb);
                            break;
                        }
                        case 7:   // TZF: through-zero (il dry passa per lo stesso ritardo fisso) o barber-pole
                        {
                            const float fixed = 0.005f * sr;
                            dl[0].push (x + 0.85f * color * fb * (mode == 1 ? 0.6f : 1.0f));
                            if (mode == 1)
                            {
                                const float ph = (float) lfo.ph;
                                float w = 0;
                                for (int k = 0; k < 2; ++k)
                                {
                                    float p = ph + 0.5f * (float) k; p -= std::floor (p);
                                    const float win = std::sin (pi * p);
                                    w += win * win * dl[0].read ((0.0005f + 0.006f * (1.0f - p)) * sr);
                                }
                                fb = w;
                                y = 0.6f * (x + w);
                            }
                            else
                            {
                                fb = dl[0].read (fixed * (1.0f + dep * t));
                                y = 0.6f * (dl[0].read (fixed) + fb);
                            }
                            break;
                        }
                        case 8:   // PHASE 4/6/8 stadi, COLOR = feedback
                        {
                            const int st = 4 + 2 * mode;
                            const float a = apCoef (100.0f * std::pow (40.0f, 0.5f + 0.5f * dep * s), sr);
                            float v = x + 0.8f * color * fb;
                            for (int k = 0; k < st; ++k) v = ap[k].tick (v, a);
                            fb = v;
                            y = 0.6f * (x + v);
                            break;
                        }
                        case 9:   // PITCH: centro (COLOR) +- modulazione dell'LFO
                        {
                            const float semis = std::round ((color - 0.5f) * 24.0f) + dep * t;
                            shifter.setRatio (std::pow (2.0f, semis / 12.0f));
                            const float w = shifter.tick (x);
                            y = mode == 1 ? w : 0.7f * (x + w);
                            break;
                        }
                        default:  // FILT: LP / HP / BP, COLOR = risonanza
                        {
                            const float fc = 150.0f * std::pow (40.0f, 0.5f + 0.5f * dep * s);
                            filt.setG (fastTanPi (fc, sr), 1.0f / logMap (color, 0.7f, 12.0f));
                            float lo, bp, hi; filt.tick (x, lo, bp, hi);
                            y = mode == 0 ? lo : mode == 1 ? hi : bp * 1.5f;
                            break;
                        }
                    }
                    y *= vol * fg; yR *= vol * fg;
                    if (numCh > 1) { ch[0][i] = y; ch[1][i] = stereo ? yR : y; }
                    else ch[0][i] = stereo ? 0.5f * (y + yR) : y;
                }
            }
        private:
            float sr = 48000, fb = 0, lamp = 0;
            RoleParam pDepth, pRate, pColor, pType, pMode;
            DelayLine dl[1], dry;
            Rotary rot;
            GrainShifter shifter;
            Ldr ldr[4];
            Allpass1 ap[8];
            Svf filt, cross;
            OnePole bassLp, bassLp2, edge;
            Lfo lfo;
            ModeFader fader;
            Smooth colS, depS, rateS;
        };
    }

    std::unique_ptr<Effect> makeClassicMod (const ModelDef& d, const std::string& type)
    {
        if (type == "jfetphase") return std::make_unique<JfetPhaser> (d);
        if (type == "otaphase")  return std::make_unique<OtaPhaser> (d, false);
        if (type == "badstone")  return std::make_unique<OtaPhaser> (d, true);
        if (type == "optophase") return std::make_unique<OptoPhaser> (d);
        if (type == "vibe")      return std::make_unique<VibeFx> (d);
        if (type == "worm")      return std::make_unique<WormFx> (d);
        if (type == "pulsar")    return std::make_unique<PulsarFx> (d);
        if (type == "bbdmod")    return std::make_unique<BbdModFx> (d);
        if (type == "leslie")    return std::make_unique<LeslieFx> (d);
        if (type == "mod11")     return std::make_unique<Mod11Fx> (d);
        return nullptr;
    }
}
