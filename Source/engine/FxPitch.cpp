/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pitch: octaver analogico a flip-flop (OC-2), octaver polifonico,
    pitch shifter, harmonist con riconoscimento della nota; synth a
    inseguimento di pitch; accordatore.

    Pitch (config):
      type=analog|poly|shifter|harmonist
      range=semitoni (shifter: +-range)  modes=...  (per poly: oct1,oct2,up)
      lp=Hz (filtro d'ingresso dell'analogico) gain=x (preamp d'ingresso)
    Synth:  type=analog|digital  f=lo,hi (filtro)  waves=n
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        /** Riconoscimento della frequenza fondamentale (YIN semplificato, decimazione 4x). */
        class PitchDetector
        {
        public:
            void prepare (double sr)
            {
                fsd = sr / 4.0;
                buf.assign (2048, 0.0f);
                diff.assign (1024, 0.0f);
                w = 0; count = 0; freq = 0; conf = 0; acc = 0; accN = 0;
            }
            /** Da chiamare per ogni campione; aggiorna la stima ogni 256 campioni decimati. */
            inline void push (float x) noexcept
            {
                acc += x; ++accN;
                if (accN < 4) return;
                buf[(size_t) w] = acc * 0.25f; acc = 0; accN = 0;
                w = (w + 1) & 2047;
                if (++count >= 256) { count = 0; analyse(); }
            }
            float frequency() const noexcept { return freq; }
            float confidence() const noexcept { return conf; }
        private:
            void analyse() noexcept
            {
                const int W = 700;
                const int tauMin = (int) (fsd / 1200.0), tauMax = std::min (1000, (int) (fsd / 55.0));
                auto at = [this] (int k) { return buf[(size_t) ((w - 1 - k) & 2047)]; };
                float energy = 0;
                for (int j = 0; j < W; ++j) energy += at (j) * at (j);
                if (energy < 1e-5f) { conf = 0; return; }
                float running = 0;
                int best = -1;
                for (int tau = 1; tau <= tauMax; ++tau)
                {
                    float s = 0;
                    for (int j = 0; j < W; ++j) { const float d = at (j) - at (j + tau); s += d * d; }
                    running += s;
                    diff[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
                    if (tau >= tauMin && best < 0 && diff[(size_t) tau] < 0.12f && tau > 2 && diff[(size_t) tau] < diff[(size_t) tau - 1])
                    {
                        // cerca il minimo locale successivo
                        int t = tau;
                        while (t + 1 <= tauMax)
                        {
                            float s2 = 0;
                            for (int j = 0; j < W; ++j) { const float d = at (j) - at (j + t + 1); s2 += d * d; }
                            running += s2;
                            diff[(size_t) t + 1] = s2 * (float) (t + 1) / running;
                            if (diff[(size_t) t + 1] >= diff[(size_t) t]) break;
                            ++t;
                        }
                        best = t;
                        break;
                    }
                }
                if (best < 2 || best >= tauMax) { conf *= 0.5f; return; }
                const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = (best + 1 <= tauMax) ? diff[(size_t) best + 1] : b;
                const float den = a - 2 * b + c;
                const float shift = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
                freq = (float) (fsd / ((float) best + shift));
                conf = 1.0f - b;
            }
            std::vector<float> buf, diff;
            double fsd = 12000;
            int w = 0, count = 0, accN = 0;
            float acc = 0, freq = 0, conf = 0;
        };

        //==============================================================================
        class PitchEffect : public Effect
        {
        public:
            explicit PitchEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "analog");
                range = (float) cfg.num ("range", 12);
                lpHz = cfg.num ("lp", 700);
                inGain = (float) cfg.num ("gain", 5);
                pOct1 = role (*this, "oct1", 0.6f);
                pOct2 = role (*this, "oct2", 0.0f);
                pDirect = role (*this, "direct", 0.7f);
                pUp = role (*this, "up", 0.0f);
                pShift = role (*this, "shift", 0.5f);
                pFine = role (*this, "fine", 0.5f);
                pBalance = role (*this, "balance", 0.5f);
                pKey = role (*this, "key", 0.0f);
                pHarmony = role (*this, "harmony", 0.5f);
                pMode = role (*this, "mode", 0.0f);
                pLevel = role (*this, "level", 0.6f);
                pHarmony2 = role (*this, "harmony2", 0.5f);
                pLevel2 = role (*this, "level2", 0.0f);
                pTuneDown = role (*this, "tunedown", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                inHp.set (sr, 50, 0.707);
                inLp.set (sr, lpHz, 0.707);
                inLp2.set (sr, lpHz, 0.707);
                outLp.set (sr, 1500, 0.707);
                peak.set (sr, 0.1, 30);
                for (auto& p : shift) p.prepare (sr, 70);
                for (auto& p : shift2) p.prepare (sr, 90);
                detector.prepare (sr);
                reset();
            }
            void reset() override
            {
                inHp.reset(); inLp.reset(); inLp2.reset(); outLp.reset();
                ff1 = ff2 = 1; high = false;
                for (auto& p : shift) p.clear();
                for (auto& p : shift2) p.clear();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                if (type == "analog") { processAnalog (ch, numCh, n); return; }
                float ratio1 = 1, ratio2 = 1, mix1 = 0, mix2 = 0, direct = pDirect.get (*this);
                if (type == "poly")
                {
                    ratio1 = 0.5f; ratio2 = pMode.step (*this) == 2 ? 2.0f : 0.25f;
                    mix1 = pOct1.get (*this); mix2 = pMode.step (*this) == 2 ? pUp.get (*this) : pOct2.get (*this);
                    if (pUp.present() && pMode.step (*this) != 2) mix2 = pOct2.get (*this);
                }
                else if (type == "shifter")
                {
                    const float semis = std::round ((pShift.get (*this) - 0.5f) * 2.0f * range)
                                        + (pFine.present() ? (pFine.get (*this) - 0.5f) : 0.0f);
                    ratio1 = std::pow (2.0f, semis / 12.0f);
                    const float bal = pBalance.get (*this);
                    mix1 = bal * 1.2f; direct = 1.0f - bal;
                }
                else // harmonist: intervallo diatonico nella tonalita' scelta (voce A e voce B)
                {
                    const int key = std::clamp ((int) std::lround (pKey.get (*this) * 11.0f), 0, 11);
                    ratio1 = std::pow (2.0f, harmonySemis (pHarmony.get (*this), key) / 12.0f);
                    mix1 = pLevel.get (*this) * 1.2f;
                    if (pLevel2.present())
                    {
                        ratio2 = std::pow (2.0f, harmonySemis (pHarmony2.get (*this), key) / 12.0f);
                        mix2 = pLevel2.get (*this) * 1.2f;
                    }
                }
                if (pTuneDown.present())      // XS-100: accordatura ribassata (0..-12 semitoni) su tutte le voci
                {
                    const float down = std::pow (2.0f, -std::round (pTuneDown.get (*this) * 12.0f) / 12.0f);
                    ratio1 *= down; ratio2 *= down;
                }
                shift[0].setRatio (ratio1); shift[1].setRatio (ratio1);
                shift2[0].setRatio (ratio2); shift2[1].setRatio (ratio2);
                for (int i = 0; i < n; ++i)
                {
                    const float mono = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    detector.push (mono);
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float x = ch[c][i];
                        float y = direct * x + mix1 * shift[c].tick (x);
                        if (mix2 > 0) y += mix2 * shift2[c].tick (x);
                        ch[c][i] = y;
                    }
                }
            }
            float readout (int) const override { return detector.frequency(); }
        private:
            /** Semitoni per un intervallo diatonico (selettore a 12 posizioni) nella tonalita' key. */
            float harmonySemis (float sel, int key) const
            {
                static const int scale[7] = { 0, 2, 4, 5, 7, 9, 11 };
                static const int intervals[] = { -12, -6, -5, -4, -3, -2, 2, 3, 4, 5, 6, 12 };
                const int iv = intervals[std::clamp ((int) std::lround (sel * 11.0f), 0, 11)];
                if (std::abs (iv) == 12 || detector.confidence() < 0.6f || detector.frequency() < 50) return (float) iv;
                const float midi = 69.0f + 12.0f * std::log2 (detector.frequency() / 440.0f);
                const int note = ((int) std::lround (midi) - key + 1200) % 12;
                int degree = 0;
                for (int k = 0; k < 7; ++k) if (scale[k] <= note) degree = k;
                const int steps = iv > 0 ? iv - 1 : iv + 1;
                const int target = degree + steps;
                const int oct = (int) std::floor (target / 7.0);
                const int tDeg = ((target % 7) + 7) % 7;
                return (float) (scale[tDeg] + 12 * oct - scale[degree]);
            }

            /** OC-2: flip-flop divisori sul segnale filtrato, moltiplicato per +-0.5. */
            void processAnalog (float* const* ch, int numCh, int n)
            {
                const float o1 = pOct1.get (*this) * 1.6f, o2 = pOct2.get (*this) * 1.6f, dir = pDirect.get (*this) * 1.2f;
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    float l, b, h;
                    inHp.tick (0, x * inGain, l, b, h);
                    float f = h;
                    inLp.tick (0, f, l, b, h); f = l;
                    inLp2.tick (0, f, l, b, h); f = l;
                    // comparatore con isteresi proporzionale al picco (rivelatore di picco)
                    const float pk = std::max (peak.tick (f), 1e-4f);
                    const float thr = 0.25f * pk;
                    bool edge = false;
                    if (! high && f > thr) { high = true; edge = true; }
                    else if (high && f < -thr) high = false;
                    if (edge) { ff1 = -ff1; if (ff1 > 0) ff2 = -ff2; }
                    const float sub1 = f * 0.5f * (float) ff1, sub2 = f * 0.5f * (float) ff2;
                    float oct = o1 * sub1 + o2 * sub2;
                    outLp.tick (0, oct, l, b, h);
                    oct = l / inGain * 2.0f;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = dir * ch[c][i] + oct;
                }
            }

            Config cfg;
            std::string type;
            float range = 12, inGain = 5;
            double lpHz = 700, sr = 48000;
            RoleParam pOct1, pOct2, pDirect, pUp, pShift, pFine, pBalance, pKey, pHarmony, pMode, pLevel, pHarmony2, pLevel2, pTuneDown;
            SVF inHp, inLp, inLp2, outLp;
            Envelope peak;
            int ff1 = 1, ff2 = 1;
            bool high = false;
            PitchShifter shift[2], shift2[2];
            PitchDetector detector;
        };

        //==============================================================================
        /** Synth per chitarra: segue la nota, genera un'onda e la filtra con inviluppo. */
        class SynthEffect : public Effect
        {
        public:
            explicit SynthEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "analog");
                auto f = cfg.list ("f"); fLo = f.size() > 0 ? f[0] : 120; fHi = f.size() > 1 ? f[1] : 6000;
                pWave = role (*this, "wave", 0.0f);
                pFreq = role (*this, "freq", 0.4f);
                pRes = role (*this, "res", 0.5f);
                pDecay = role (*this, "decay", 0.5f);
                pSens = role (*this, "sens", 0.5f);
                pLevel = role (*this, "level", 0.6f);
                pDirect = role (*this, "direct", 0.0f);
                pMode = role (*this, "mode", 0.0f);
                pRange = role (*this, "range", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s; detector.prepare (sr); follower.set (sr, 2, 80); filterEnv = 0;
                inLp.set (sr, 900, 0.707); reset();
            }
            void reset() override { svf.reset(); phase = 0; high = false; sq = 1; }
            void process (float* const* ch, int numCh, int n) override
            {
                const int wave = pWave.step (*this) % 3;
                bassRange = pRange.step (*this) > 0;
                const float q = logMap (pRes.get (*this), 0.7f, 14.0f);
                const float decay = (float) std::exp (-1.0 / (logMap (pDecay.get (*this), 30.0f, 1500.0f) * 0.001 * sr));
                const float sens = pSens.get (*this), base = pFreq.get (*this);
                const float lvl = pLevel.get (*this) * 1.5f, dir = pDirect.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    detector.push (x);
                    const float amp = follower.tick (x);
                    // nuova nota -> riapertura del filtro
                    if (amp > 0.02f && ! gate) { gate = true; filterEnv = 1.0f; }
                    if (amp < 0.008f) gate = false;
                    filterEnv *= decay;
                    float osc;
                    if (type == "analog")
                    {
                        // onda quadra dal comparatore (come i synth analogici BOSS)
                        float l, b, h;
                        inLp.tick (0, x, l, b, h);
                        if (! high && l > 0.01f) { high = true; sq = -sq; }
                        else if (high && l < -0.01f) high = false;
                        osc = (wave == 1) ? (float) sq : (wave == 2 ? (float) sq * 0.5f + l * 4.0f : (high ? 1.0f : -1.0f));
                    }
                    else
                    {
                        float f0 = detector.frequency() > 40 ? detector.frequency() : 110.0f;
                        if (bassRange) f0 *= 0.5f;                            // GTR/BASS
                        phase += f0 / sr; phase -= std::floor (phase);
                        osc = wave == 0 ? (float) (2.0 * phase - 1.0) : wave == 1 ? (phase < 0.5 ? 1.0f : -1.0f)
                                                                                 : (phase < 0.25 ? 1.0f : -1.0f);
                    }
                    osc *= std::min (1.0f, amp * 6.0f);
                    const float pos = std::clamp (base + sens * filterEnv, 0.0f, 1.0f);
                    svf.set (sr, fLo * std::pow (fHi / fLo, (double) pos), q);
                    float l, b, h;
                    svf.tick (0, osc, l, b, h);
                    const float y = l * lvl;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = dir * ch[c][i] + y;
                }
            }
        private:
            Config cfg;
            std::string type;
            double fLo = 120, fHi = 6000, sr = 48000, phase = 0;
            RoleParam pWave, pFreq, pRes, pDecay, pSens, pLevel, pDirect, pMode, pRange;
            bool bassRange = false;
            PitchDetector detector;
            Envelope follower;
            SVF svf, inLp;
            float filterEnv = 0;
            bool gate = false, high = false;
            int sq = 1;
        };

        //==============================================================================
        /** Accordatore: silenzia l'uscita (modo MUTE) e rileva la nota. */
        class TunerEffect : public Effect
        {
        public:
            explicit TunerEffect (const ModelDef& d) : Effect (d)
            {
                pMode = role (*this, "mode", 1.0f);
                pRef = role (*this, "ref", 0.5f);
            }
            void prepare (double s, int) override { detector.prepare (s); }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const bool mute = pMode.step (*this) > 0 || ! pMode.present();
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    detector.push (x);
                    if (mute) for (int c = 0; c < numCh; ++c) ch[c][i] = 0.0f;
                }
                const float f = detector.frequency();
                if (detector.confidence() > 0.7f && f > 20)
                {
                    const float ref = 436.0f + 9.0f * pRef.get (*this);      // 436..445 Hz
                    const float midi = 69.0f + 12.0f * std::log2 (f / ref);
                    const float nearest = std::round (midi);
                    note.store ((float) (((int) nearest % 12 + 12) % 12));
                    cents.store ((midi - nearest) * 100.0f);
                    freq.store (f);
                    valid.store (1.0f);
                }
                else valid.store (valid.load() * 0.95f);
            }
            /** 0 = frequenza, 1 = cent, 2 = nota (0 = C), 3 = validita' */
            float readout (int i) const override
            {
                switch (i) { case 0: return freq.load(); case 1: return cents.load(); case 2: return note.load(); default: return valid.load(); }
            }
        private:
            RoleParam pMode, pRef;
            PitchDetector detector;
            std::atomic<float> freq { 0 }, cents { 0 }, note { 0 }, valid { 0 };
        };
    }

    std::unique_ptr<Effect> makePitch (const ModelDef& d) { return std::make_unique<PitchEffect> (d); }
    std::unique_ptr<Effect> makeSynth (const ModelDef& d) { return std::make_unique<SynthEffect> (d); }
    std::unique_ptr<Effect> makeTuner (const ModelDef& d) { return std::make_unique<TunerEffect> (d); }
}
