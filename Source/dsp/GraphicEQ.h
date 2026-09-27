/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    "Graphic EQ GQ-7": equalizzatore grafico a 7 bande ispirato al GE-7.
    Bande a 100, 200, 400, 800, 1.6k, 3.2k, 6.4k Hz (+-15 dB, filtri a
    campana con Q ~1.1) piu' il cursore LEVEL (+-15 dB).
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Filters.h"
#include "../Parameters.h"

namespace pt::dsp
{
    class GraphicEQ
    {
    public:
        static constexpr int bands = pt::ids::eqBands;

        void prepare (double sampleRate, int /*maxBlock*/, int numChannels)
        {
            sr = sampleRate;
            channels = juce::jlimit (1, maxCh, numChannels);
            for (int b = 0; b < bands; ++b)
            {
                gains[b].reset (sr, 0.03);
                gains[b].setCurrentAndTargetValue (targets[b]);
            }
            level.reset (sr, 0.03);
            level.setCurrentAndTargetValue (dbToGain (levelDb));
            updateAll();
            reset();
        }

        void reset()
        {
            for (auto& ch : filters)
                for (auto& f : ch)
                    f.reset();
        }

        void setParameters (const float* bandDb, float newLevelDb)
        {
            for (int b = 0; b < bands; ++b)
                targets[b] = bandDb[b];
            levelDb = newLevelDb;
        }

        void process (juce::dsp::AudioBlock<float> block)
        {
            for (int b = 0; b < bands; ++b)
                gains[b].setTargetValue (targets[b]);
            level.setTargetValue (dbToGain (levelDb));

            const int nch = juce::jmin ((int) block.getNumChannels(), channels);
            const int n = (int) block.getNumSamples();

            for (int start = 0; start < n; start += subBlock)
            {
                const int len = juce::jmin (subBlock, n - start);
                for (int b = 0; b < bands; ++b)
                {
                    if (gains[b].isSmoothing())
                    {
                        gains[b].skip (len);
                        updateBand (b);
                    }
                }
                for (int c = 0; c < nch; ++c)
                {
                    float* d = block.getChannelPointer ((size_t) c) + start;
                    auto& fl = filters[(size_t) c];
                    for (int i = 0; i < len; ++i)
                    {
                        float y = d[i];
                        for (int b = 0; b < bands; ++b)
                            y = fl[(size_t) b].process (y);
                        d[i] = y;
                    }
                }
                for (int i = start; i < start + len; ++i)
                {
                    const float l = level.getNextValue();
                    for (int c = 0; c < nch; ++c)
                        block.getChannelPointer ((size_t) c)[i] *= l;
                }
            }
        }

    private:
        static constexpr int maxCh = 2, subBlock = 32;

        void updateBand (int b)
        {
            Biquad proto;
            proto.peak (sr, pt::ids::eqFreqs[b], 1.1, gains[b].getCurrentValue());
            for (auto& ch : filters)
                ch[(size_t) b].copyCoefficientsFrom (proto);
        }

        void updateAll()
        {
            for (int b = 0; b < bands; ++b)
                updateBand (b);
        }

        double sr = 44100.0;
        int channels = 2;
        float targets[bands] {};
        float levelDb = 0.0f;
        juce::SmoothedValue<float> gains[bands];
        juce::SmoothedValue<float> level;
        std::array<std::array<Biquad, bands>, maxCh> filters;
    };
}
