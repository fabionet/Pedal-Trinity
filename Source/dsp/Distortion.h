/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    "Metal Core MC-2W": distorsore high-gain ispirato al MT-2 (versione
    Waza Craft con selettore S/C).
      - pre-filtro passa-alto + enfasi sulle medie;
      - due stadi di guadagno in cascata: il primo asimmetrico (morbido),
        il secondo con diodi verso massa (duro);
      - filtro anti-fruscio e EQ attivo a 3 bande con medie parametriche:
        LOW +-15 dB (100 Hz), HIGH +-15 dB (6 kHz), MIDDLE +-15 dB (200 Hz - 5 kHz).
    Modo S = timbro "standard"; modo C = piu' guadagno, bassi piu' stretti,
    clipping piu' morbido e acuti piu' levigati.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Filters.h"

namespace pt::dsp
{
    class Distortion
    {
    public:
        struct Params
        {
            float dist = 0.6f, level = 0.5f;          // 0..1
            float lowDb = 0, highDb = 0, midDb = 0;   // dB
            float midFreq = 1000.0f;                  // Hz
            bool custom = false;
        };

        void prepare (double sampleRate, int maxBlock, int numChannels)
        {
            sr = sampleRate;
            channels = juce::jlimit (1, maxCh, numChannels);
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) channels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampling->initProcessing ((size_t) maxBlock);
            osr = sr * 4.0;

            g1.reset (osr, 0.03);
            outGain.reset (sr, 0.03);
            low.reset (sr, 0.03); high.reset (sr, 0.03); mid.reset (sr, 0.03); freq.reset (sr, 0.03);
            snapToTargets();
            updateStaticFilters();
            updateEq();
            reset();
        }

        void reset()
        {
            for (int c = 0; c < maxCh; ++c)
            {
                preHp[c].reset(); preMid[c].reset(); stageLp[c].reset(); stageHp[c].reset();
                postLp[c].reset(); postLp2[c].reset(); eqLow[c].reset(); eqMid[c].reset(); eqHigh[c].reset();
                dcBlock[c].reset();
            }
            if (oversampling) oversampling->reset();
        }

        void setParameters (const Params& p) { params = p; }

        void process (juce::dsp::AudioBlock<float> block)
        {
            if (params.custom != lastMode)
                updateStaticFilters();
            setTargets();

            const int nch = juce::jmin ((int) block.getNumChannels(), channels);
            auto sub = block.getSubsetChannelBlock (0, (size_t) nch);
            const int n = (int) sub.getNumSamples();

            // ---- pre-filtri (frequenza base)
            for (int c = 0; c < nch; ++c)
            {
                float* d = sub.getChannelPointer ((size_t) c);
                for (int i = 0; i < n; ++i)
                    d[i] = preMid[c].process (preHp[c].process (d[i]));
            }

            // ---- stadi non lineari (4x)
            auto os = oversampling->processSamplesUp (sub);
            const int osN = (int) os.getNumSamples();
            const float g2 = dbToGain (params.custom ? 22.0f : 18.0f);
            const float bias = 0.18f, biasOut = fastTanh (bias);

            for (int i = 0; i < osN; ++i)
            {
                const float g = g1.getNextValue();
                for (int c = 0; c < nch; ++c)
                {
                    float* d = os.getChannelPointer ((size_t) c);
                    float s = fastTanh (g * d[i] + bias) - biasOut;           // stadio 1 asimmetrico
                    s = stageHp[c].highPass (stageLp[c].lowPass (s));        // accoppiamento interstadio
                    s = params.custom ? diodeSoft (g2 * s, 0.5f)            // stadio 2: diodi verso massa
                                      : diodeHard (g2 * s, 0.5f);
                    d[i] = s;
                }
            }
            oversampling->processSamplesDown (sub);

            // ---- post filtro + EQ a 3 bande, coefficienti aggiornati ogni 32 campioni
            for (int start = 0; start < n; start += subBlock)
            {
                const int len = juce::jmin (subBlock, n - start);
                if (low.isSmoothing() || high.isSmoothing() || mid.isSmoothing() || freq.isSmoothing() || eqDirty)
                {
                    low.skip (len); high.skip (len); mid.skip (len); freq.skip (len);
                    updateEq();
                }
                for (int i = start; i < start + len; ++i)
                {
                    const float og = outGain.getNextValue();
                    for (int c = 0; c < nch; ++c)
                    {
                        float* d = sub.getChannelPointer ((size_t) c);
                        float y = postLp2[c].lowPass (postLp[c].process (d[i]));
                        y = eqHigh[c].process (eqMid[c].process (eqLow[c].process (y)));
                        d[i] = dcBlock[c].highPass (y) * og;
                    }
                }
            }
        }

    private:
        static constexpr int maxCh = 2, subBlock = 32;

        float g1Target() const
        {
            const float db = 14.0f + 32.0f * params.dist + (params.custom ? 5.0f : 0.0f);
            return dbToGain (db);
        }

        void snapToTargets()
        {
            g1.setCurrentAndTargetValue (g1Target());
            outGain.setCurrentAndTargetValue (5.6f * params.level * params.level);
            low.setCurrentAndTargetValue (params.lowDb);
            high.setCurrentAndTargetValue (params.highDb);
            mid.setCurrentAndTargetValue (params.midDb);
            freq.setCurrentAndTargetValue (params.midFreq);
        }

        void setTargets()
        {
            g1.setTargetValue (g1Target());
            outGain.setTargetValue (5.6f * params.level * params.level);
            auto setT = [this] (auto& s, float v) { if (std::abs (v - s.getTargetValue()) > 1.0e-6f) { s.setTargetValue (v); eqDirty = true; } };
            setT (low, params.lowDb);
            setT (high, params.highDb);
            setT (mid, params.midDb);
            setT (freq, params.midFreq);
        }

        void updateStaticFilters()
        {
            lastMode = params.custom;
            for (int c = 0; c < maxCh; ++c)
            {
                preHp[c].highPass (sr, lastMode ? 125.0 : 80.0, 0.707);
                preMid[c].peak (sr, lastMode ? 1200.0 : 900.0, 0.8, lastMode ? 5.0 : 8.0);
                stageLp[c].setCutoff (osr, 7000.0);
                stageHp[c].setCutoff (osr, lastMode ? 180.0 : 140.0);
                postLp[c].lowPass (sr, lastMode ? 5200.0 : 6500.0, 0.707);
                postLp2[c].setCutoff (sr, 9000.0);
                dcBlock[c].setCutoff (sr, 18.0);
            }
        }

        void updateEq()
        {
            eqDirty = false;
            Biquad l, m, h;
            l.lowShelf (sr, 100.0, 0.707, low.getCurrentValue());
            m.peak (sr, freq.getCurrentValue(), 1.0, mid.getCurrentValue());
            h.highShelf (sr, 6000.0, 0.707, high.getCurrentValue());
            for (int c = 0; c < maxCh; ++c)
            {
                eqLow[c].copyCoefficientsFrom (l);
                eqMid[c].copyCoefficientsFrom (m);
                eqHigh[c].copyCoefficientsFrom (h);
            }
        }

        double sr = 44100.0, osr = 176400.0;
        int channels = 2;
        Params params;
        bool lastMode = false, eqDirty = true;

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> g1 { 10.0f };
        juce::SmoothedValue<float> outGain, low, high, mid;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 1000.0f };

        Biquad preHp[maxCh], preMid[maxCh], postLp[maxCh], eqLow[maxCh], eqMid[maxCh], eqHigh[maxCh];
        OnePole stageLp[maxCh], stageHp[maxCh], postLp2[maxCh], dcBlock[maxCh];
    };
}
