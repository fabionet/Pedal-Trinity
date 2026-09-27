/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/Chain.h"
#include "Presets.h"

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
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Stato completo (catena + parametri globali + interfaccia) per i preset. */
    juce::ValueTree captureState() const;
    void restoreState (const juce::ValueTree&);

    /** Catena di default (i tre pedali originali). */
    void loadDefaultChain();

    juce::AudioProcessorValueTreeState apvts;
    pt::engine::Chain chain;
    pt::PresetManager presets { *this };
    juce::ValueTree uiState { "UI" };       // vista, pagina, zoom (salvati nel progetto)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void migrateFromV1 (const juce::XmlElement&);

    juce::SmoothedValue<float> inGain, outGain;
    std::atomic<float>* inParam {};
    std::atomic<float>* outParam {};
    std::atomic<float>* bypassParam {};
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTrinityProcessor)
};
