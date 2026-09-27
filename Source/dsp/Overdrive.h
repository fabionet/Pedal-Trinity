/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    "Emerald Drive ED-9": overdrive ispirato al circuito del Tube Screamer
    (TS808/TS9). Modello semplificato del blocco di clipping:
      - l'op-amp amplifica solo la parte passa-alto (~720 Hz) del segnale
        -> tipica "gobba" sulle medie e bassi puliti;
      - i diodi in retroazione limitano morbidamente la parte amplificata;
      - l'uscita e' segnale pulito + parte clippata;
      - controllo di tono (passa-basso variabile + presenza) e volume.
    La sezione non lineare lavora in sovracampionamento 4x.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Filters.h"

namespace pt::dsp
{
    class Overdrive
    {
    public:
        void prepare (double sampleRate, int maxBlock, int numChannels)
        {
            sr = sampleRate;
            channels = juce::jlimit (1, maxCh, numChannels);
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) channels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversampling->initProcessing ((size_t) maxBlock);
            osr = sr * 4.0;

            gain.reset (osr, 0.03);
            level.reset (sr, 0.03);
            gain.setCurrentAndTargetValue (targetGain());
            level.setCurrentAndTargetValue (targetLevel());
            updateFilters();
            reset();
        }

        void reset()
        {
            for (int c = 0; c < maxCh; ++c)
            {
                hp720[c].reset(); fbLp[c].reset(); toneLp[c].reset(); presence[c].reset(); dcBlock[c].reset();
            }
            if (oversampling) oversampling->reset();
        }

        /** Parametri normalizzati 0..1 */
        void setParameters (float driveN, float toneN, float levelN)
        {
            drive = driveN; tone = toneN; lvl = levelN;
        }

        void process (juce::dsp::AudioBlock<float> block)
        {
            gain.setTargetValue (targetGain());
            level.setTargetValue (targetLevel());
            updateFilters();

            const int nch = juce::jmin ((int) block.getNumChannels(), channels);
            auto sub = block.getSubsetChannelBlock (0, (size_t) nch);
            auto os = oversampling->processSamplesUp (sub);
            const int osN = (int) os.getNumSamples();

            for (int i = 0; i < osN; ++i)
            {
                const float g = gain.getNextValue();
                for (int c = 0; c < nch; ++c)
                {
                    float* d = os.getChannelPointer ((size_t) c);
                    const float x = d[i];
                    const float h = hp720[c].highPass (x);
                    const float v = fbLp[c].lowPass (h * g);
                    d[i] = x + diodeSoft (v, 0.55f);
                }
            }

            oversampling->processSamplesDown (sub);

            const int n = (int) sub.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                const float l = level.getNextValue();
                for (int c = 0; c < nch; ++c)
                {
                    float* d = sub.getChannelPointer ((size_t) c);
                    float y = toneLp[c].lowPass (d[i]);
                    y = presence[c].process (y);
                    y = dcBlock[c].highPass (y);
                    d[i] = y * l;
                }
            }
        }

    private:
        static constexpr int maxCh = 2;

        float targetGain() const
        {
            // Potenziometro drive 500k logaritmico: G = 1 + (51k + Rd) / 4.7k
            const float rd = 500e3f * drive * drive;
            return 1.0f + (51e3f + rd) / 4.7e3f;
        }

        float targetLevel() const
        {
            return 4.0f * lvl * lvl * 0.9f;
        }

        void updateFilters()
        {
            const float rd = 500e3f * drive * drive;
            const double fbCut = 1.0 / (2.0 * pi * (51e3 + rd) * 51e-12);
            const double toneCut = 600.0 * std::pow (12.0, (double) tone);
            const double presDb = -2.0 + 6.0 * tone;
            for (int c = 0; c < maxCh; ++c)
            {
                hp720[c].setCutoff (osr, 720.0);
                fbLp[c].setCutoff (osr, juce::jmin (fbCut, osr * 0.2));
                toneLp[c].setCutoff (sr, toneCut);
                presence[c].highShelf (sr, 2500.0, 0.7, presDb);
                dcBlock[c].setCutoff (sr, 18.0);
            }
        }

        double sr = 44100.0, osr = 176400.0;
        int channels = 2;
        float drive = 0.5f, tone = 0.5f, lvl = 0.5f;

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain { 10.0f };
        juce::SmoothedValue<float> level;

        OnePole hp720[maxCh], fbLp[maxCh], toneLp[maxCh], dcBlock[maxCh];
        Biquad presence[maxCh];
    };
}
