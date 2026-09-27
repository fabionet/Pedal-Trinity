/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "dsp/Overdrive.h"
#include "dsp/Distortion.h"
#include "dsp/GraphicEQ.h"

class PedalTrinityProcessor : public juce::AudioProcessor
{
public:
    PedalTrinityProcessor();
    ~PedalTrinityProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    /** Gestisce l'accensione/spegnimento senza click (dissolvenza 12 ms). */
    struct BypassFader
    {
        juce::SmoothedValue<float> mix;
        bool fullyOff() const { return ! mix.isSmoothing() && mix.getTargetValue() < 0.5f; }
    };

    template <typename ProcessFn, typename ResetFn>
    void runPedal (BypassFader& fader, bool on, juce::AudioBuffer<float>& buffer, int numCh, int numSamples,
                   ProcessFn&& process, ResetFn&& resetFn);

    std::atomic<float>* p (const juce::String& id) { return apvts.getRawParameterValue (id); }

    pt::dsp::Overdrive overdrive;
    pt::dsp::Distortion distortion;
    pt::dsp::GraphicEQ eq;
    BypassFader odFade, distFade, eqFade;
    juce::AudioBuffer<float> dryBuffer;

    std::atomic<float>* odOn {}; std::atomic<float>* odDrive {}; std::atomic<float>* odTone {}; std::atomic<float>* odLevel {};
    std::atomic<float>* distOn {}; std::atomic<float>* distLevel {}; std::atomic<float>* distGain {};
    std::atomic<float>* distLow {}; std::atomic<float>* distHigh {}; std::atomic<float>* distMid {};
    std::atomic<float>* distMidFreq {}; std::atomic<float>* distMode {};
    std::atomic<float>* eqOn {}; std::atomic<float>* eqLevel {};
    std::atomic<float>* eqBand[pt::ids::eqBands] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTrinityProcessor)
};
