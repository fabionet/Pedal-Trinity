/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Dinamica: compressori/sustainer a OTA, limiter, noise suppressor,
    slow gear (volume automatico) e pedale del volume.

    Compressore (config):
      type=sustainer|limiter   topo=ff|fb (feed-forward o retroazione come i VCA OTA)
      attack=lo,hi (ms)  release=ms  maxgain=dB  thr=lo,hi (dB)  ratio=lo,hi  knee=dB
      tone=Hz (frequenza della mensola del TONE)  enhance=Hz (LMB-3)
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        class CompressorEffect : public Effect
        {
        public:
            explicit CompressorEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "sustainer");
                feedback = cfg.str ("topo", "ff") == "fb";
                auto a = cfg.list ("attack"); atkLo = a.size() > 0 ? a[0] : 1; atkHi = a.size() > 1 ? a[1] : atkLo * 20;
                relMs = cfg.num ("release", 250);
                auto rel = cfg.list ("releaseRange"); relLo = rel.size() > 0 ? rel[0] : relMs; relHi = rel.size() > 1 ? rel[1] : relMs;
                maxGain = (float) cfg.num ("maxgain", 30);
                auto t = cfg.list ("thr"); thrLo = t.size() > 0 ? (float) t[0] : -40.0f; thrHi = t.size() > 1 ? (float) t[1] : 0.0f;
                auto r = cfg.list ("ratio"); ratioLo = r.size() > 0 ? (float) r[0] : 2.0f; ratioHi = r.size() > 1 ? (float) r[1] : 20.0f;
                knee = (float) cfg.num ("knee", 6);
                toneHz = cfg.num ("tone", 2500);
                enhHz = cfg.num ("enhance", 0);
                pLevel = role (*this, "level", 0.5f);
                pSustain = role (*this, "sustain", 0.5f);
                pAttack = role (*this, "attack", 0.5f);
                pRelease = role (*this, "release", 0.5f);
                pTone = role (*this, "tone", 0.5f);
                pThr = role (*this, "threshold", 0.5f);
                pRatio = role (*this, "ratio", 0.5f);
                pEnh = role (*this, "enhance", 0.0f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { env = 0; gain = 1; tone[0].reset(); tone[1].reset(); }

            void process (float* const* ch, int numCh, int n) override
            {
                const double atk = logMap (pAttack.get (*this), (float) atkLo, (float) atkHi);
                const double rel = logMap (pRelease.get (*this), (float) relLo, (float) relHi);
                const float aC = (float) std::exp (-1.0 / (atk * 0.001 * sr)), rC = (float) std::exp (-1.0 / (rel * 0.001 * sr));
                float thrDb, ratio, makeup;
                if (type == "limiter")
                {
                    thrDb = linMap (pThr.get (*this), thrLo, thrHi);
                    ratio = logMap (pRatio.get (*this), ratioLo, ratioHi);
                    makeup = 0.0f;
                }
                else
                {
                    // SUSTAIN: soglia che scende e guadagno massimo che sale (come un OTA con piu' corrente)
                    const float s = pSustain.get (*this);
                    thrDb = linMap (s, thrHi, thrLo);
                    ratio = linMap (s, ratioLo, ratioHi);
                    makeup = -thrDb * (1.0f - 1.0f / ratio) * 0.8f;
                    makeup = std::min (makeup, maxGain);
                }
                const float out = taperA (pLevel.get (*this)) * 2.5f;
                const double toneDb = pTone.present() ? (pTone.get (*this) - 0.5) * 20.0 : 0.0;
                for (auto& t : tone) t.highShelf (sr, toneHz, 0.7, toneDb);
                if (enhHz > 0)
                    for (auto& e : enh) e.peak (sr, enhHz, 0.8, pEnh.get (*this) * 12.0);

                for (int i = 0; i < n; ++i)
                {
                    float det = 0;
                    for (int c = 0; c < numCh; ++c) det = std::max (det, std::abs (feedback ? ch[c][i] * gain : ch[c][i]));
                    env = det > env ? det + aC * (env - det) : det + rC * (env - det);
                    const float lvl = gainToDb (env);
                    // curva di guadagno con ginocchio morbido
                    float over = lvl - thrDb, grDb = 0;
                    if (over > knee * 0.5f) grDb = over * (1.0f - 1.0f / ratio);
                    else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; grDb = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee); }
                    gain = dbToGain (makeup - grDb);
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i] * gain;
                        y = (float) tone[c].process (y);
                        if (enhHz > 0) y = enh[c].process (y);
                        ch[c][i] = y * out;
                    }
                }
            }
        private:
            Config cfg;
            std::string type;
            bool feedback = false;
            double atkLo = 1, atkHi = 20, relMs = 250, relLo = 250, relHi = 250, toneHz = 2500, enhHz = 0, sr = 48000;
            float maxGain = 30, thrLo = -40, thrHi = 0, ratioLo = 2, ratioHi = 20, knee = 6, env = 0, gain = 1;
            RoleParam pLevel, pSustain, pAttack, pRelease, pTone, pThr, pRatio, pEnh;
            pt::dsp::Biquad tone[2], enh[2];
        };

        //==============================================================================
        class NoiseGateEffect : public Effect
        {
        public:
            explicit NoiseGateEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto t = cfg.list ("thr"); thrLo = t.size() > 0 ? (float) t[0] : -80.0f; thrHi = t.size() > 1 ? (float) t[1] : -20.0f;
                auto dc = cfg.list ("decay"); decLo = dc.size() > 0 ? dc[0] : 5; decHi = dc.size() > 1 ? dc[1] : 1000;
                pThr = role (*this, "threshold", 0.4f);
                pDecay = role (*this, "decay", 0.4f);
                pLevel = role (*this, "level", 0.5f);
                pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override { sr = s; detector.set (sr, 0.3, 20.0); reset(); }
            void reset() override { g = 0; open = false; }
            void process (float* const* ch, int numCh, int n) override
            {
                const float thr = dbToGain (linMap (pThr.get (*this), thrLo, thrHi));
                const float relC = (float) std::exp (-1.0 / (logMap (pDecay.get (*this), (float) decLo, (float) decHi) * 0.001 * sr));
                const float atkC = (float) std::exp (-1.0 / (0.3e-3 * sr));
                const float lvl = taperA (pLevel.get (*this)) * 2.0f;
                const bool mute = pMode.step (*this) > 0;
                for (int i = 0; i < n; ++i)
                {
                    float x = 0;
                    for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (ch[c][i]));
                    const float e = detector.tick (x);
                    if (e > thr) open = true; else if (e < thr * 0.5f) open = false;   // isteresi 6 dB
                    const float target = mute ? 0.0f : (open ? 1.0f : 0.0f);
                    g = target > g ? target + atkC * (g - target) : target + relC * (g - target);
                    for (int c = 0; c < numCh; ++c) ch[c][i] *= g * lvl;
                }
            }
        private:
            Config cfg;
            float thrLo = -80, thrHi = -20, g = 0;
            double decLo = 5, decHi = 1000, sr = 48000;
            bool open = false;
            Envelope detector;
            RoleParam pThr, pDecay, pLevel, pMode;
        };

        //==============================================================================
        /** Volume automatico (Slow Gear): ogni nuova nota riparte da zero e sale. */
        class SlowGearEffect : public Effect
        {
        public:
            explicit SlowGearEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto a = cfg.list ("attack"); atkLo = a.size() > 0 ? a[0] : 50; atkHi = a.size() > 1 ? a[1] : 2000;
                pSens = role (*this, "sens", 0.5f);
                pAttack = role (*this, "attack", 0.5f);
            }
            void prepare (double s, int) override { sr = s; detector.set (sr, 0.5, 30.0); reset(); }
            void reset() override { g = 0; armed = true; }
            void process (float* const* ch, int numCh, int n) override
            {
                const float thr = dbToGain (linMap (pSens.get (*this), -20.0f, -60.0f));
                const double rise = logMap (pAttack.get (*this), (float) atkLo, (float) atkHi) * 0.001 * sr;
                for (int i = 0; i < n; ++i)
                {
                    float x = 0;
                    for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (ch[c][i]));
                    const float e = detector.tick (x);
                    if (e < thr * 0.3f) armed = true;
                    if (armed && e > thr) { armed = false; g = 0; }
                    g = std::min (1.0f, g + (float) (1.0 / rise));
                    const float curve = g * g * (3.0f - 2.0f * g);
                    for (int c = 0; c < numCh; ++c) ch[c][i] *= curve;
                }
            }
        private:
            Config cfg;
            double atkLo = 50, atkHi = 2000, sr = 48000;
            float g = 0;
            bool armed = true;
            Envelope detector;
            RoleParam pSens, pAttack;
        };

        //==============================================================================
        class VolumeEffect : public Effect
        {
        public:
            explicit VolumeEffect (const ModelDef& d) : Effect (d)
            {
                pVol = role (*this, "volume", 1.0f);
                pMin = role (*this, "min", 0.0f);
            }
            void prepare (double s, int) override { smooth.reset (s, 0.02); }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const float mn = taperA (pMin.get (*this));
                smooth.setTargetValue (mn + (1.0f - mn) * taperA (pVol.get (*this)));
                for (int i = 0; i < n; ++i)
                {
                    const float g = smooth.getNextValue();
                    for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
                }
            }
        private:
            RoleParam pVol, pMin;
            juce::SmoothedValue<float> smooth;
        };
    }

    std::unique_ptr<Effect> makeCompressor (const ModelDef& d) { return std::make_unique<CompressorEffect> (d); }
    std::unique_ptr<Effect> makeNoiseGate (const ModelDef& d)  { return std::make_unique<NoiseGateEffect> (d); }
    std::unique_ptr<Effect> makeSlowGear (const ModelDef& d)   { return std::make_unique<SlowGearEffect> (d); }
    std::unique_ptr<Effect> makeVolume (const ModelDef& d)     { return std::make_unique<VolumeEffect> (d); }
}
