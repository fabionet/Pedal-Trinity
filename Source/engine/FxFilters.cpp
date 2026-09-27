/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Filtri: EQ grafici a gyrator, EQ parametrici, wah / filtri dinamici,
    simulatori di chitarra acustica.

    EQ grafico (config):  freqs=100,200,...  qmax=3.5  qmin=0.9  range=15
                          pre=dB (guadagno stadio d'ingresso)  lvl=15 (gamma LEVEL)
      Ogni banda e' un RLC serie (gyrator) collegato al cursore: la larghezza
      di banda si stringe all'aumentare del guadagno ("proportional Q").
    EQ parametrico:       types=ls,pk,pk,hs  f0=lo,hi f1=lo,hi ...  q=0.7,1,1,0.7  range=15
    Wah:                  type=auto|touch|lfo|pedal  f=lo,hi  q=lo,hi  filt=bp|lp
                          attack=ms release=ms rate=lo,hi
    Acoustic:             modes=f:g:q|f:g:q;... (risonanze del corpo per modo)  top=Hz
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        class GraphicEQEffect : public Effect
        {
        public:
            explicit GraphicEQEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                freqs = cfg.list ("freqs");
                if (freqs.empty()) freqs = { 100, 200, 400, 800, 1600, 3200, 6400 };
                qs = cfg.list ("qs");
                qMax = cfg.num ("qmax", 3.5);
                qMin = cfg.num ("qmin", 0.9);
                range = (float) cfg.num ("range", 15);
                lvlRange = (float) cfg.num ("lvl", 15);
                for (size_t b = 0; b < freqs.size() && b < 12; ++b)
                    bands[b] = role (*this, ("b" + std::to_string (b)).c_str(), 0.5f);
                pLevel = role (*this, "level", 0.5f);
                for (auto& g : last) g = -999;
            }
            void prepare (double s, int) override { sr = s; for (auto& g : last) g = -999; reset(); }
            void reset() override { for (auto& ch : f) for (auto& b : ch) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                const int nb = (int) std::min<size_t> (freqs.size(), 12);
                for (int b = 0; b < nb; ++b)
                {
                    const float g = (bands[b].get (*this) - 0.5f) * 2.0f * range;
                    if (std::abs (g - last[b]) > 0.01f)
                    {
                        last[b] = g;
                        const double q0 = qs.size() > (size_t) b ? qs[(size_t) b] : qMax;
                        const double q = qMin + (q0 - qMin) * std::abs (g) / range;
                        for (int c = 0; c < 2; ++c) f[c][b].peak (sr, freqs[(size_t) b], q, g);
                    }
                }
                gainTarget = dbToGain ((pLevel.get (*this) - 0.5f) * 2.0f * lvlRange);
                for (int i = 0; i < n; ++i)
                {
                    gainNow += 0.002f * (gainTarget - gainNow);
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (int b = 0; b < nb; ++b) y = f[c][b].process (y);
                        ch[c][i] = y * gainNow;
                    }
                }
            }
        private:
            Config cfg;
            std::vector<double> freqs, qs;
            double qMax = 3.5, qMin = 0.9, sr = 48000;
            float range = 15, lvlRange = 15, last[12] {}, gainTarget = 1, gainNow = 1;
            RoleParam bands[12], pLevel;
            pt::dsp::Biquad f[2][12];
        };

        //==============================================================================
        class ParametricEQEffect : public Effect
        {
        public:
            explicit ParametricEQEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto t = cfg.str ("types", "ls,pk,pk,hs");
                size_t i = 0;
                while (i < t.size() && nb < 6)
                {
                    auto j = t.find (',', i); if (j == std::string::npos) j = t.size();
                    const auto k = t.substr (i, j - i);
                    types[nb] = k == "ls" ? 0 : k == "hs" ? 2 : k == "lp" ? 3 : k == "hp" ? 4 : 1;
                    auto fr = cfg.list (("f" + std::to_string (nb)).c_str());
                    fLo[nb] = fr.size() > 0 ? fr[0] : 1000; fHi[nb] = fr.size() > 1 ? fr[1] : fLo[nb];
                    const std::string idx = std::to_string (nb);
                    g[nb] = role (*this, ("g" + idx).c_str(), 0.5f);
                    fq[nb] = role (*this, ("f" + idx).c_str(), 0.5f);
                    qq[nb] = role (*this, ("q" + idx).c_str(), 0.5f);
                    ++nb; i = j + 1;
                }
                qDef = cfg.list ("q");
                range = (float) cfg.num ("range", 15);
                pLevel = role (*this, "level", 0.5f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int b = 0; b < nb; ++b)
                {
                    const double freq = fLo[b] * std::pow (fHi[b] / fLo[b], (double) fq[b].get (*this));
                    const double q0 = qDef.size() > (size_t) b ? qDef[(size_t) b] : 0.9;
                    const double q = qq[b].present() ? q0 * std::pow (8.0, (qq[b].get (*this) - 0.5) * 2.0) : q0;
                    const double gain = (g[b].get (*this) - 0.5) * 2.0 * range;
                    for (int c = 0; c < 2; ++c)
                    {
                        switch (types[b])
                        {
                            case 0: f[c][b].lowShelf (sr, freq, q, gain); break;
                            case 2: f[c][b].highShelf (sr, freq, q, gain); break;
                            case 3: f[c][b].lowPass (sr, freq, q); break;
                            case 4: f[c][b].highPass (sr, freq, q); break;
                            default: f[c][b].peak (sr, freq, q, gain); break;
                        }
                    }
                }
                const float lvl = dbToGain ((pLevel.get (*this) - 0.5f) * 2.0f * range);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (int b = 0; b < nb; ++b) y = f[c][b].process (y);
                        ch[c][i] = y * lvl;
                    }
            }
        private:
            Config cfg;
            int nb = 0, types[6] {};
            double fLo[6] {}, fHi[6] {}, sr = 48000;
            std::vector<double> qDef;
            float range = 15;
            RoleParam g[6], fq[6], qq[6], pLevel;
            pt::dsp::Biquad f[2][6];
        };

        //==============================================================================
        class WahEffect : public Effect
        {
        public:
            explicit WahEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "touch");
                auto fr = cfg.list ("f"); fLo = fr.size() > 0 ? fr[0] : 250; fHi = fr.size() > 1 ? fr[1] : 3000;
                auto q = cfg.list ("q"); qLo = q.size() > 0 ? q[0] : 1.5; qHi = q.size() > 1 ? q[1] : 12;
                lpMode = cfg.str ("filt", "bp") == "lp";
                atk = cfg.num ("attack", 4); rel = cfg.num ("release", 120);
                auto r = cfg.list ("rate"); rateLo = r.size() > 0 ? r[0] : 0.1; rateHi = r.size() > 1 ? r[1] : 8;
                pSens = role (*this, "sens", 0.5f);
                pPeak = role (*this, "peak", 0.5f);
                pFreq = role (*this, "freq", 0.3f);
                pMode = role (*this, "mode", 0.0f);
                pFilt = role (*this, "filter", lpMode ? 1.0f : 0.0f);
                pRate = role (*this, "rate", 0.3f);
                pDepth = role (*this, "depth", 0.7f);
                pDecay = role (*this, "decay", 0.5f);
                pLevel = role (*this, "level", 0.5f);
                pRange = role (*this, "range", 0.5f);
                pDrive = role (*this, "drive", 0.0f);
                pVoice = role (*this, "voice", 0.0f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { svf.reset(); env.env = 0; lfo.phase = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                const int voice = pVoice.step (*this);
                const float q = logMap (pPeak.get (*this), (float) qLo, (float) qHi) * (1.0f + 0.12f * (float) (voice % 4));
                const double rangeK = std::pow (2.0, (pRange.get (*this) - 0.5) * 1.4);    // RANGE: sposta la corsa
                const double fL = fLo * std::sqrt (rangeK), fH = fHi * rangeK;
                const float drive = 1.0f + 10.0f * pDrive.get (*this);
                const float sens = pSens.get (*this), base = pFreq.get (*this), depth = pDepth.get (*this);
                const bool down = pMode.step (*this) == 1 && type != "lfo";
                const bool lp = pFilt.present() ? pFilt.step (*this) > 0 : lpMode;
                env.set (sr, atk, logMap (pDecay.get (*this), (float) rel * 0.3f, (float) rel * 3.0f));
                lfo.setRate (sr, logMap (pRate.get (*this), (float) rateLo, (float) rateHi));
                const float outGain = pLevel.present() ? taperA (pLevel.get (*this)) * 2.0f : 1.0f;
                for (int i = 0; i < n; i += 8)
                {
                    const int len = std::min (8, n - i);
                    float e = 0;
                    for (int k = 0; k < len; ++k)
                    {
                        float x = 0;
                        for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (ch[c][i + k]));
                        e = env.tick (x);
                    }
                    float pos;
                    if (type == "pedal") pos = base;
                    else if (type == "lfo") { pos = base + depth * 0.5f * (1.0f + lfo.next (0)) * (1.0f - base); lfo.advance (len); }
                    else
                    {
                        const float drive = std::clamp (e * (2.0f + 60.0f * sens * sens), 0.0f, 1.0f);
                        pos = down ? std::clamp (base + (1.0f - base) * (1.0f - drive), 0.0f, 1.0f)
                                   : std::clamp (base + (1.0f - base) * drive, 0.0f, 1.0f);
                    }
                    svf.set (sr, fL * std::pow (fH / fL, (double) pos), q);
                    for (int k = 0; k < len; ++k)
                        for (int c = 0; c < numCh; ++c)
                        {
                            float l, b, h;
                            const float in = drive > 1.01f ? std::tanh (ch[c][i + k] * drive) / std::tanh (drive) : ch[c][i + k];
                            svf.tick (c, in, l, b, h);
                            ch[c][i + k] = (lp ? l : b * 1.6f) * outGain;
                        }
                }
            }
        private:
            Config cfg;
            std::string type;
            double fLo = 250, fHi = 3000, qLo = 1.5, qHi = 12, atk = 4, rel = 120, rateLo = 0.1, rateHi = 8, sr = 48000;
            bool lpMode = false;
            RoleParam pSens, pPeak, pFreq, pMode, pFilt, pRate, pDepth, pDecay, pLevel, pRange, pDrive, pVoice;
            SVF svf;
            Envelope env;
            LFO lfo;
        };

        //==============================================================================
        class AcousticEffect : public Effect
        {
        public:
            explicit AcousticEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                // modes=f:g:q|f:g:q;f:g:q|...  (';' separa i modi, '|' le risonanze)
                const auto m = cfg.str ("modes", "110:6:1.2|220:3:1.5|3000:4:0.8");
                size_t i = 0;
                while (i <= m.size() && numModes < 4)
                {
                    auto j = m.find (';', i); if (j == std::string::npos) j = m.size();
                    const auto mode = m.substr (i, j - i);
                    size_t a = 0; int k = 0;
                    while (a <= mode.size() && k < 4)
                    {
                        auto b = mode.find ('|', a); if (b == std::string::npos) b = mode.size();
                        const auto t = mode.substr (a, b - a);
                        float f = 100, g = 0, q = 1;
                        std::sscanf (t.c_str(), "%f:%f:%f", &f, &g, &q);
                        res[numModes][k++] = { f, g, q };
                        a = b + 1;
                    }
                    resCount[numModes] = k;
                    ++numModes; i = j + 1;
                }
                topHz = cfg.num ("top", 5000);
                cutHz = cfg.num ("cut", 600);       // attenuazione del "quack" dei pick-up magnetici
                pTop = role (*this, "top", 0.5f);
                pBody = role (*this, "body", 0.5f);
                pLevel = role (*this, "level", 0.5f);
                pMode = role (*this, "mode", 0.0f);
                pReverb = role (*this, "reverb", 0.0f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                const int m = std::clamp (pMode.step (*this), 0, std::max (0, numModes - 1));
                const float body = pBody.get (*this) * 2.0f, top = (pTop.get (*this) - 0.5f) * 24.0f;
                for (int c = 0; c < 2; ++c)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        if (k < resCount[m]) f[c][k].peak (sr, res[m][k].f, res[m][k].q, res[m][k].g * body);
                        else f[c][k].peak (sr, 1000, 1, 0);
                    }
                    f[c][4].peak (sr, cutHz, 0.8, -7.0);
                    f[c][5].highShelf (sr, topHz, 0.7, top);
                }
                const float lvl = taperA (pLevel.get (*this)) * 2.0f;
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (auto& b : f[c]) y = b.process (y);
                        ch[c][i] = y * lvl;
                    }
            }
        private:
            struct Res { float f, g, q; };
            Config cfg;
            Res res[4][4] {};
            int resCount[4] {}, numModes = 0;
            double topHz = 5000, cutHz = 600, sr = 48000;
            RoleParam pTop, pBody, pLevel, pMode, pReverb;
            pt::dsp::Biquad f[2][6];
        };
    }

    std::unique_ptr<Effect> makeGraphicEQ (const ModelDef& d)    { return std::make_unique<GraphicEQEffect> (d); }
    std::unique_ptr<Effect> makeParametricEQ (const ModelDef& d) { return std::make_unique<ParametricEQEffect> (d); }
    std::unique_ptr<Effect> makeWah (const ModelDef& d)          { return std::make_unique<WahEffect> (d); }
    std::unique_ptr<Effect> makeAcoustic (const ModelDef& d)     { return std::make_unique<AcousticEffect> (d); }
}
