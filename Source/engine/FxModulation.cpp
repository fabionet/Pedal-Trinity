/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Famiglie di modulazione:
      * BBD (chorus, dimension, vibrato, flanger, delay analogico) con
        emulazione del bucket-brigade "a clock": N/2 celle avanzano a ogni
        periodo di clock, ritardo = N / (2 f_clk), ingresso sample&hold,
        filtri anti-alias/ricostruzione di Butterworth del 4° ordine,
        compander 2:1 / 1:2 (NE570/571), saturazione della linea.
      * Phaser a stadi all-pass con JFET/OTA (mappatura esponenziale).
      * Tremolo / pan / slicer / rotary.

    Chiavi di configurazione (BBD):
      type=chorus|dimension|vibrato|flanger|delay  stages=1024  clk=lo,hi (Hz)
      rate=lo,hi (Hz)  lfo=0..3 (tri, sine, square, saw)  aa=fc  comp=0|1
      fbmax=0.95  stereo=0|1  wet=1  sat=1.0  rise=ms (vibrato)
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        /** Filtro di Butterworth passa-basso del 4° ordine (due SVF). */
        struct Butter4
        {
            SVF a, b;
            void set (double sr, double fc) { a.set (sr, fc, 0.5412); b.set (sr, fc, 1.3066); }
            void reset() { a.reset(); b.reset(); }
            inline float tick (int ch, float x) noexcept
            {
                float lp, bp, hp;
                a.tick (ch, x, lp, bp, hp);
                b.tick (ch, lp, lp, bp, hp);
                return lp;
            }
        };

        /** Singolo BBD a clock variabile. */
        struct Bucket
        {
            std::vector<float> cells;
            int cellsN = 512, w = 0;
            double phase = 0;      // frazione del periodo di clock
            float held = 0;        // uscita tenuta tra due clock (ZOH)

            void allocate (int stages) { cellsN = std::max (8, stages / 2); cells.assign ((size_t) cellsN, 0.0f); w = 0; }
            void clear() { std::fill (cells.begin(), cells.end(), 0.0f); held = 0; phase = 0; }

            /** Avanza di un campione audio: clockPerSample = f_clk / fs. */
            inline float tick (float in, double clockPerSample) noexcept
            {
                phase += clockPerSample;
                int ticks = (int) phase;
                phase -= ticks;
                ticks = std::min (ticks, 64);
                for (int t = 0; t < ticks; ++t)
                {
                    // l'uscita della catena e' la cella piu' vecchia; la nuova carica entra in testa
                    held = cells[(size_t) w];
                    cells[(size_t) w] = in;
                    w = (w + 1) % cellsN;
                }
                return held;
            }
        };

        /** Compander 2:1 / 1:2 in stile NE570. */
        struct Compander
        {
            Envelope inEnv, outEnv;
            void set (double sr) { inEnv.set (sr, 1.0, 12.0); outEnv.set (sr, 1.0, 12.0); }
            inline float compress (float x) noexcept
            {
                const float e = std::max (inEnv.tick (x), 1.0e-3f);
                return x * 0.35f / std::sqrt (0.35f * e);
            }
            inline float expand (float x) noexcept
            {
                const float e = std::max (outEnv.tick (x), 1.0e-3f);
                return x * e / 0.35f;
            }
        };

        //==============================================================================
        class BbdEffect : public Effect
        {
        public:
            explicit BbdEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                baseType = cfg.str ("type", "chorus");
                stages = (int) cfg.num ("stages", 1024);
                auto clk = cfg.list ("clk");
                clkLo = clk.size() > 0 ? clk[0] : 20e3;
                clkHi = clk.size() > 1 ? clk[1] : 200e3;
                auto rate = cfg.list ("rate");
                rateLo = rate.size() > 0 ? rate[0] : 0.1;
                rateHi = rate.size() > 1 ? rate[1] : 8.0;
                lfoShape = (int) cfg.num ("lfo", 0);
                aaFc = cfg.num ("aa", 8000);
                useComp = cfg.num ("comp", 1) > 0.5;
                fbMax = (float) cfg.num ("fbmax", 0.9);
                stereoOut = cfg.num ("stereo", 0) > 0.5;
                wetGain = (float) cfg.num ("wet", 1.0);
                satLevel = (float) cfg.num ("sat", 1.0);
                riseMs = cfg.num ("rise", 0);
                depthFixed = (float) cfg.num ("depth", -1);
                modes = cfg.list ("modes");      // dimension: coppie rate,depth per ogni pulsante

                pRate = role (*this, "rate", 0.4f);
                pDepth = role (*this, "depth", depthFixed >= 0 ? depthFixed : 0.5f);
                pLevel = role (*this, "level", 1.0f);
                pManual = role (*this, "manual", 0.5f);
                pRes = role (*this, "res", 0.0f);
                pTime = role (*this, "time", 0.5f);
                pFeedback = role (*this, "feedback", 0.3f);
                pRise = role (*this, "rise", 0.3f);
                pMode = role (*this, "mode", 0.0f);
                pTone = role (*this, "tone", 0.5f);
                pLow = role (*this, "low", 0.5f);
                pHigh = role (*this, "high", 0.5f);
                pVDepth = role (*this, "vdepth", -1.0f);
                pVRate = role (*this, "vrate", -1.0f);
                pLatch = role (*this, "latch", 2.0f);
                // posizioni del selettore di modo che attivano il vibrato (es. "VIB", "VINT VIB")
                if (pMode.present())
                {
                    const char* ch = def.controls[pMode.idx].choices;
                    std::string all = ch != nullptr ? ch : "";
                    size_t i = 0; int k = 0;
                    while (i <= all.size())
                    {
                        auto j = all.find ('|', i); if (j == std::string::npos) j = all.size();
                        if (all.substr (i, j - i).find ("VIB") != std::string::npos) vibratoSteps.push_back (k);
                        ++k; i = j + 1;
                    }
                    modeIsSC = all == "S|C";
                }
            }

            void prepare (double s, int) override
            {
                sr = s;
                for (auto& b : bbd) b.allocate (stages);
                for (auto& f : aaIn) f.set (sr, aaFc);
                for (auto& f : aaOut) f.set (sr, aaFc);
                for (auto& c : comp) c.set (sr);
                toneLp.set (sr, 6000, 0.707);
                eqLow.set (sr, 200, 0.707);
                reset();
            }

            void reset() override
            {
                for (auto& b : bbd) b.clear();
                for (auto& f : aaIn) f.reset();
                for (auto& f : aaOut) f.reset();
                toneLp.reset(); eqLow.reset();
                fb[0] = fb[1] = 0;
                riseGain = 0;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                const int modeStep = pMode.present() ? pMode.step (*this) : 0;
                const bool vibMode = std::find (vibratoSteps.begin(), vibratoSteps.end(), modeStep) != vibratoSteps.end();
                std::string type = vibMode ? std::string ("vibrato") : baseType;
                if (pLatch.present() && pLatch.step (*this) == 1) return;          // VB-2: posizione BYPASS
                float rate = logMap (pRate.get (*this), (float) rateLo, (float) rateHi);
                float depth = pDepth.get (*this);
                if (vibMode && pVRate.present()) rate = logMap (pVRate.get (*this), 1.0f, 10.0f);
                if (vibMode && pVDepth.present()) depth = pVDepth.get (*this);
                float rateUse = rate;
                if (type == "dimension" && modes.size() >= 2)
                {
                    const int m = std::clamp (pMode.step (*this), 0, (int) modes.size() / 2 - 1);
                    rateUse = (float) modes[(size_t) (2 * m)];
                    depth = (float) modes[(size_t) (2 * m + 1)];
                }
                lfo.setRate (sr, rateUse);

                const float level = pLevel.get (*this) * wetGain;
                const float res = pRes.get (*this) * fbMax;
                const float feedback = pFeedback.get (*this) * fbMax;
                // tono (CE-3/CE-5: filtro, DM: nessuno)
                if (pTone.present()) toneLp.set (sr, logMap (pTone.get (*this), 1500.0f, 12000.0f), 0.707);
                const double riseCoef = riseMs > 0 ? std::exp (-1.0 / (logMap (pRise.get (*this), 50.0f, (float) riseMs) * 0.001 * sr)) : 0.0;

                // DM-2W: modo S = circuito originale (clock minimo ~6.8 kHz, 300 ms), C = esteso
                const double cLo = (baseType == "delay" && modeIsSC && modeStep == 0) ? std::max (clkLo, 6800.0) : clkLo;
                const double lnRange = std::log (clkHi / cLo);
                for (int i = 0; i < n; ++i)
                {
                    const float l = lfo.next (lfoShape);
                    const float l2 = lfo.next (lfoShape, 0.5);   // seconda linea in controfase
                    lfo.advance();

                    // posizione del clock (0..1 su scala logaritmica) per le due linee
                    double pos1, pos2;
                    if (type == "delay")
                        pos1 = pos2 = 1.0 - pTime.get (*this);           // TIME al massimo = clock minimo
                    else if (type == "flanger")
                    {
                        const double c = 1.0 - pManual.get (*this);
                        pos1 = std::clamp (c + 0.5 * depth * l, 0.0, 1.0);
                        pos2 = std::clamp (c + 0.5 * depth * l2, 0.0, 1.0);
                    }
                    else
                    {
                        pos1 = std::clamp (0.5 + 0.5 * depth * l, 0.0, 1.0);
                        pos2 = std::clamp (0.5 + 0.5 * depth * l2, 0.0, 1.0);
                    }
                    const double clk1 = cLo * std::exp (lnRange * pos1);
                    const double clk2 = cLo * std::exp (lnRange * pos2);

                    if (riseMs > 0) riseGain = 1.0f + (float) riseCoef * (riseGain - 1.0f);

                    const float inL = ch[0][i];
                    const float inR = numCh > 1 ? ch[1][i] : inL;
                    const float mono = 0.5f * (inL + inR);

                    auto line = [&] (int k, float x, double clk) -> float
                    {
                        float v = aaIn[k].tick (0, x + (type == "delay" ? feedback : res) * fb[k]);
                        if (useComp) v = comp[k].compress (v);
                        v = satLevel * softSat (v / satLevel);
                        float y = bbd[k].tick (v, clk / sr);
                        y = aaOut[k].tick (0, y);
                        if (useComp) y = comp[k].expand (y);
                        fb[k] = y;
                        return y;
                    };

                    float outL, outR;
                    if (type == "dimension")
                    {
                        const float w1 = line (0, mono, clk1), w2 = line (1, mono, clk2);
                        outL = inL + level * (w1 - 0.6f * w2) * 0.7f;
                        outR = inR + level * (w2 - 0.6f * w1) * 0.7f;
                    }
                    else
                    {
                        float w = line (0, mono, clk1);
                        if (pTone.present()) { float lp, bp, hp; toneLp.tick (0, w, lp, bp, hp); w = lp; }
                        if (type == "vibrato")
                        {
                            const float g = riseMs > 0 && baseType == "vibrato" ? riseGain : 1.0f;
                            outL = inL * (1.0f - g) + w * g * level;
                            outR = inR * (1.0f - g) + w * g * level;
                        }
                        else if (type == "delay")
                        {
                            outL = inL + level * w;
                            outR = inR + level * w;
                        }
                        else if (stereoOut && numCh > 1)
                        {
                            outL = inL + level * w;          // uscita A: diretto + effetto
                            outR = inR - level * w;          // uscita B: effetto in controfase
                        }
                        else
                        {
                            outL = inL + level * w;
                            outR = inR + level * w;
                        }
                    }
                    ch[0][i] = outL;
                    if (numCh > 1) ch[1][i] = outR;
                }
            }

        private:
            Config cfg;
            std::string baseType;
            std::vector<int> vibratoSteps;
            bool modeIsSC = false;
            RoleParam pVDepth, pVRate, pLatch;
            int stages = 1024, lfoShape = 0;
            double clkLo = 20e3, clkHi = 200e3, rateLo = 0.1, rateHi = 8, aaFc = 8000, riseMs = 0, sr = 48000;
            bool useComp = true, stereoOut = false;
            float fbMax = 0.9f, wetGain = 1, satLevel = 1, depthFixed = -1, riseGain = 0;
            std::vector<double> modes;
            RoleParam pRate, pDepth, pLevel, pManual, pRes, pTime, pFeedback, pRise, pMode, pTone, pLow, pHigh;
            Bucket bbd[2];
            Butter4 aaIn[2], aaOut[2];
            Compander comp[2];
            SVF toneLp, eqLow;
            LFO lfo;
            float fb[2] {};
        };

        //==============================================================================
        /** Phaser: cascata di all-pass del 1° ordine con elemento variabile JFET/OTA. */
        class PhaserEffect : public Effect
        {
        public:
            explicit PhaserEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                stageList = cfg.list ("stages");
                if (stageList.empty()) stageList = { 4 };
                auto f = cfg.list ("f");
                fLo = f.size() > 0 ? f[0] : 200; fHi = f.size() > 1 ? f[1] : 3000;
                auto r = cfg.list ("rate");
                rateLo = r.size() > 0 ? r[0] : 0.1; rateHi = r.size() > 1 ? r[1] : 8;
                fbMax = (float) cfg.num ("fbmax", 0.7);
                lfoShape = (int) cfg.num ("lfo", 0);
                pRate = role (*this, "rate", 0.4f);
                pDepth = role (*this, "depth", (float) cfg.num ("depth", 1.0));
                pRes = role (*this, "res", (float) cfg.num ("resfix", 0.0));
                pMode = role (*this, "mode", 0.0f);
                pManual = role (*this, "manual", 0.5f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { for (auto& c : ap) for (auto& v : c) v = {}; fb[0] = fb[1] = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                const int nst = (int) std::clamp (stageList[(size_t) std::clamp (pMode.step (*this), 0, (int) stageList.size() - 1)], 2.0, 12.0);
                lfo.setRate (sr, logMap (pRate.get (*this), (float) rateLo, (float) rateHi));
                const float depth = pDepth.get (*this), res = pRes.get (*this) * fbMax;
                const float centre = pManual.present() ? pManual.get (*this) : 0.5f;
                for (int i = 0; i < n; ++i)
                {
                    const float l = lfo.next (lfoShape);
                    lfo.advance();
                    const double pos = std::clamp ((double) centre + 0.5 * depth * l, 0.0, 1.0);
                    const double f = fLo * std::pow (fHi / fLo, pos);
                    const double t = std::tan (juce::MathConstants<double>::pi * f / sr);
                    const float a = (float) ((t - 1.0) / (t + 1.0));
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float x = ch[c][i];
                        float v = x + res * fb[c];
                        for (int s = 0; s < nst; ++s)
                        {
                            auto& st = ap[c][s];
                            const float y = a * v + st.x1 - a * st.y1;
                            st.x1 = v; st.y1 = y;
                            v = y;
                        }
                        fb[c] = v;
                        ch[c][i] = 0.5f * (x + v) * 1.4f;   // miscela 50/50: le tacche nascono qui
                    }
                }
            }
        private:
            struct AP { float x1 = 0, y1 = 0; };
            Config cfg;
            std::vector<double> stageList;
            double fLo = 200, fHi = 3000, rateLo = 0.1, rateHi = 8, sr = 48000;
            float fbMax = 0.7f;
            int lfoShape = 0;
            RoleParam pRate, pDepth, pRes, pMode, pManual;
            AP ap[2][12];
            float fb[2] {};
            LFO lfo;
        };

        //==============================================================================
        /** Tremolo (OTA), pan stereo, slicer a pattern, rotary. */
        class TremoloEffect : public Effect
        {
        public:
            explicit TremoloEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "tremolo");
                auto r = cfg.list ("rate");
                rateLo = r.size() > 0 ? r[0] : 0.5; rateHi = r.size() > 1 ? r[1] : 12;
                pRate = role (*this, "rate", 0.5f);
                pDepth = role (*this, "depth", 0.7f);
                pWave = role (*this, "wave", 0.0f);
                pMode = role (*this, "mode", 0.0f);
                pLevel = role (*this, "level", 0.5f);
                pVariation = role (*this, "variation", 0.0f);
                pBank = role (*this, "bank", 0.0f);
                pSlow = role (*this, "slow", 0.3f);
                pFast = role (*this, "fast", 0.6f);
                pDrive = role (*this, "drive", 0.0f);
                pBalance = role (*this, "balance", 0.5f);
                pRise = role (*this, "rise", 0.4f);
                pDirect = role (*this, "direct", 0.0f);
                pVoice = role (*this, "voice", 0.0f);
            }
            void prepare (double s, int) override { sr = s; smooth.reset (sr, 0.002); cross.set (sr, 800, 0.7); reset(); }
            void reset() override { lfo.phase = 0; hornPhase = drumPhase = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                const float rate = logMap (pRate.get (*this), (float) rateLo, (float) rateHi);
                const float depth = pDepth.get (*this), wave = pWave.get (*this);
                if (type == "slicer")
                {
                    // 16 passi per battuta; il selettore sceglie il pattern
                    static const uint16_t patterns[] = { 0xFFFF, 0xAAAA, 0xEEEE, 0xDB6D, 0xF0F0, 0xCCCC, 0xB5AD, 0x9249,
                                                         0xE4E4, 0xFAFA, 0x8888, 0xA5A5 };
                    const int np = (int) (sizeof (patterns) / sizeof (patterns[0]));
                    const int pat = (pMode.step (*this) + (int) std::lround (pVariation.get (*this) * 3.0f)
                                     + 4 * pBank.step (*this)) % np;
                    lfo.setRate (sr, rate / 4.0);     // una battuta = 16 passi
                    for (int i = 0; i < n; ++i)
                    {
                        const double ph = lfo.phase;
                        lfo.advance();
                        const int stepIdx = (int) (ph * 16.0) & 15;
                        const bool on = (patterns[pat] >> (15 - stepIdx)) & 1;
                        const float within = (float) (ph * 16.0 - std::floor (ph * 16.0));
                        const float duty = 0.35f + 0.6f * wave;
                        smooth.setTargetValue (on && within < duty ? 1.0f : 1.0f - depth);
                        const float g = smooth.getNextValue();
                        for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
                    }
                    return;
                }
                if (type == "rotary")
                {
                    // corno e tamburo con effetto Doppler (AM + ritardo modulato semplificato)
                    const bool fast = pMode.step (*this) > 0;
                    const double slowHz = logMap (pSlow.get (*this), 0.4f, 2.0f), fastHz = logMap (pFast.get (*this), 3.5f, 9.0f);
                    const double hornTarget = fast ? fastHz : slowHz, drumTarget = hornTarget * 0.88;
                    const double accel = 1.0 / logMap (pRise.get (*this), 0.3f, 4.0f);     // RISE TIME
                    const float drive = 1.0f + 8.0f * pDrive.get (*this), bal = pBalance.get (*this);
                    const float wet = pLevel.present() ? pLevel.get (*this) * 1.4f : 1.0f, dry = pDirect.get (*this);
                    const float hornDepth = 0.25f + 0.1f * (float) pVoice.step (*this);
                    for (int i = 0; i < n; ++i)
                    {
                        hornRate += (hornTarget - hornRate) * accel * 1.6 / sr;
                        drumRate += (drumTarget - drumRate) * accel * 0.5 / sr;
                        hornPhase += hornRate / sr; hornPhase -= std::floor (hornPhase);
                        drumPhase += drumRate / sr; drumPhase -= std::floor (drumPhase);
                        const float hs = std::sin ((float) hornPhase * twoPi), dsn = std::sin ((float) drumPhase * twoPi);
                        const float in = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                        const float x = std::tanh (in * drive) / std::tanh (drive);   // OVERDRIVE del preamp
                        float lp, bp, hp;
                        cross.tick (0, x, lp, bp, hp);
                        const float horn = hp * (1.0f + (hornDepth + 0.1f * depth) * hs) * (0.5f + bal);
                        const float drum = lp * (1.0f + 0.25f * depth * dsn) * (1.5f - bal);
                        const float l = horn * (0.8f + 0.2f * hs) + drum * (0.8f + 0.2f * dsn);
                        const float r = horn * (0.8f - 0.2f * hs) + drum * (0.8f - 0.2f * dsn);
                        ch[0][i] = wet * l + dry * in;
                        if (numCh > 1) ch[1][i] = wet * r + dry * in;
                    }
                    return;
                }
                lfo.setRate (sr, rate);
                const float k = 1.0f + 12.0f * wave;           // WAVE: triangolo -> quadra
                const float norm = std::tanh (k);
                for (int i = 0; i < n; ++i)
                {
                    const float tri = lfo.next (0);
                    lfo.advance();
                    const float s = std::tanh (k * tri) / norm;      // -1..1
                    if (type == "pan" && numCh > 1)
                    {
                        const float pos = 0.5f + 0.5f * depth * s;
                        const float gl = std::cos (pos * 1.5707963f) * 1.414f, gr = std::sin (pos * 1.5707963f) * 1.414f;
                        const float m = 0.5f * (ch[0][i] + ch[1][i]);
                        ch[0][i] = m * gl;
                        ch[1][i] = m * gr;
                    }
                    else
                    {
                        const float g = 1.0f - depth * (0.5f + 0.5f * s);
                        for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
                    }
                }
            }
        private:
            Config cfg;
            std::string type;
            double rateLo = 0.5, rateHi = 12, sr = 48000, hornPhase = 0, drumPhase = 0, hornRate = 0.8, drumRate = 0.7;
            RoleParam pRate, pDepth, pWave, pMode, pLevel, pVariation, pBank, pSlow, pFast, pDrive, pBalance, pRise, pDirect, pVoice;
            LFO lfo;
            SVF cross;
            juce::SmoothedValue<float> smooth;
        };
    }

    std::unique_ptr<Effect> makeBbd (const ModelDef& d)     { return std::make_unique<BbdEffect> (d); }
    std::unique_ptr<Effect> makePhaser (const ModelDef& d)  { return std::make_unique<PhaserEffect> (d); }
    std::unique_ptr<Effect> makeTremolo (const ModelDef& d) { return std::make_unique<TremoloEffect> (d); }
}
