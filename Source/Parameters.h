/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pt::ids
{
    // Overdrive "Emerald Drive ED-9"
    inline constexpr const char* odOn    = "od_on";
    inline constexpr const char* odDrive = "od_drive";
    inline constexpr const char* odTone  = "od_tone";
    inline constexpr const char* odLevel = "od_level";

    // Distorsore "Metal Core MC-2W"
    inline constexpr const char* distOn      = "dist_on";
    inline constexpr const char* distLevel   = "dist_level";
    inline constexpr const char* distGain    = "dist_gain";
    inline constexpr const char* distLow     = "dist_low";
    inline constexpr const char* distHigh    = "dist_high";
    inline constexpr const char* distMid     = "dist_mid";
    inline constexpr const char* distMidFreq = "dist_midfreq";
    inline constexpr const char* distMode    = "dist_mode";

    // Equalizzatore grafico "GQ-7"
    inline constexpr const char* eqOn    = "eq_on";
    inline constexpr const char* eqLevel = "eq_level";
    inline constexpr int eqBands = 7;
    inline constexpr float eqFreqs[eqBands] = { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };

    inline juce::String eqBand (int i) { return "eq_" + juce::String (i); }
}

namespace pt
{
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        auto knob010 = [] (const char* id, const char* name, float def)
        {
            return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                          NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                                                          AudioParameterFloatAttributes().withLabel ("")
                                                              .withStringFromValueFunction ([] (float v, int) { return String (v, 1); }));
        };
        auto dbParam = [] (const juce::String& id, const juce::String& name, float range)
        {
            return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                          NormalisableRange<float> (-range, range, 0.01f), 0.0f,
                                                          AudioParameterFloatAttributes().withLabel ("dB")
                                                              .withStringFromValueFunction ([] (float v, int)
                                                              {
                                                                  return (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
                                                              }));
        };

        // --- Overdrive
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::odOn, 1 }, "Overdrive On", true));
        p.push_back (knob010 (ids::odDrive, "Overdrive Drive", 5.0f));
        p.push_back (knob010 (ids::odTone,  "Overdrive Tone",  5.0f));
        p.push_back (knob010 (ids::odLevel, "Overdrive Level", 5.0f));

        // --- Distorsore
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::distOn, 1 }, "Distortion On", false));
        p.push_back (knob010 (ids::distLevel, "Distortion Level", 5.0f));
        p.push_back (knob010 (ids::distGain,  "Distortion Dist",  6.0f));
        p.push_back (dbParam (ids::distLow,  "Distortion Low",    15.0f));
        p.push_back (dbParam (ids::distHigh, "Distortion High",   15.0f));
        p.push_back (dbParam (ids::distMid,  "Distortion Middle", 15.0f));

        // Mid freq 200 Hz .. 5 kHz, scala logaritmica (1 kHz circa al centro)
        NormalisableRange<float> freqRange (200.0f, 5000.0f,
            [] (float a, float b, float t) { return a * std::pow (b / a, t); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
            [] (float, float, float v) { return std::round (v); });
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::distMidFreq, 1 }, "Distortion Mid Freq",
                                                            freqRange, 1000.0f,
                                                            AudioParameterFloatAttributes().withLabel ("Hz")
                                                                .withStringFromValueFunction ([] (float v, int)
                                                                {
                                                                    return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz"
                                                                                        : String ((int) v) + " Hz";
                                                                })));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::distMode, 1 }, "Distortion Mode",
                                                             StringArray { "S (Standard)", "C (Custom)" }, 0));

        // --- Equalizzatore
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::eqOn, 1 }, "EQ On", true));
        const char* bandNames[] = { "100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz" };
        for (int i = 0; i < ids::eqBands; ++i)
            p.push_back (dbParam (ids::eqBand (i), String ("EQ ") + bandNames[i], 15.0f));
        p.push_back (dbParam (ids::eqLevel, "EQ Level", 15.0f));

        return { p.begin(), p.end() };
    }
}
