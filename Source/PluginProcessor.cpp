/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PluginProcessor.h"
#include <cstring>
#include "PluginEditor.h"

namespace
{
    constexpr const char* inId = "in_gain";
    constexpr const char* outId = "out_gain";
    constexpr const char* bypassId = "bypass_all";
    constexpr const char* outBalId = "out_balance";
    constexpr const char* outLId = "out_level_l";
    constexpr const char* outRId = "out_level_r";
}

juce::AudioProcessorValueTreeState::ParameterLayout PedalTrinityProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { inId, 1 }, "Input", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { outId, 1 }, "Output", NormalisableRange<float> (-30.0f, 12.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { bypassId, 1 }, "Bypass", false));
    // uscita dello splitter (attiva in DUAL / STEREO): bilanciamento e livelli delle due linee
    auto pct = [] (float v, int) { return v == 0.0f ? String ("C") : (v < 0 ? "L " : "R ") + String (std::abs (v), 0); };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { outBalId, 2 }, "Out Balance", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { outLId, 2 }, "Out Left", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { outRId, 2 }, "Out Right", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (db)));
    return { p.begin(), p.end() };
}

PedalTrinityProcessor::PedalTrinityProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PedalTrinityGlobal", createLayout())
{
    inParam = apvts.getRawParameterValue (inId);
    outParam = apvts.getRawParameterValue (outId);
    bypassParam = apvts.getRawParameterValue (bypassId);
    outBalParam = apvts.getRawParameterValue (outBalId);
    outLParam = apvts.getRawParameterValue (outLId);
    outRParam = apvts.getRawParameterValue (outRId);
    uiState.setProperty ("view", 3, nullptr);
    uiState.setProperty ("first", 0, nullptr);
    loadDefaultChain();
}

void PedalTrinityProcessor::loadDefaultChain()
{
    juce::ValueTree t ("CHAIN");
    auto slot = [&t] (const char* id, bool on)
    {
        juce::ValueTree s ("SLOT");
        s.setProperty ("model", id, nullptr);
        s.setProperty ("on", on, nullptr);
        t.appendChild (s, nullptr);
    };
    slot ("ed9", true);
    slot ("mc2w", false);
    slot ("gq7", true);
    chain.fromValueTree (t);
}

bool PedalTrinityProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PedalTrinityProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    chain.prepare (sampleRate, maxBlock);
    inGain.reset (sampleRate, 0.03);
    outGain.reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inParam->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outParam->load()));
}

void PedalTrinityProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midi.processBlock (midiMessages);       // pedaliera MIDI: anche con il bypass generale attivo
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);
    if (n == 0) return;
    if (bypassParam->load() > 0.5f) return;

    const int nch = juce::jmin (2, buffer.getNumChannels());
    inGain.setTargetValue (juce::Decibels::decibelsToGain (inParam->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outParam->load()));
    chain.outBalance.store (outBalParam->load() / 100.0f);
    chain.outGainL.store (juce::Decibels::decibelsToGain (outLParam->load()));
    chain.outGainR.store (juce::Decibels::decibelsToGain (outRParam->load()));

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), nch, start, len);
        for (int i = 0; i < len; ++i)
        {
            const float g = inGain.getNextValue();
            for (int c = 0; c < nch; ++c) part.getWritePointer (c)[i] *= g;
        }
        chain.process (part, nch);
        float peak[2] = { 0.0f, 0.0f };
        for (int i = 0; i < len; ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < nch; ++c)
            {
                float& s = part.getWritePointer (c)[i];
                s = std::isfinite (s) ? s * g : 0.0f;       // protezione da valori non validi
                peak[c] = std::max (peak[c], std::abs (s));
            }
        }
        const int outCh = nch > 1 ? chain.outputChannels.load (std::memory_order_relaxed) : 1;
        outputMeter.channels.store (outCh, std::memory_order_relaxed);
        outputMeter.push (0, outCh > 1 ? peak[0] : std::max (peak[0], peak[1]));
        if (outCh > 1) outputMeter.push (1, peak[1]);
    }
}

//==============================================================================
juce::ValueTree PedalTrinityProcessor::captureState() const
{
    juce::ValueTree root ("PedalTrinityState");
    root.setProperty ("version", "1.2.0-beta", nullptr);
    root.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    root.appendChild (chain.toValueTree(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);
    return root;
}

void PedalTrinityProcessor::restoreState (const juce::ValueTree& root)
{
    const auto global = root.getChildWithName (apvts.state.getType());
    if (global.isValid()) apvts.replaceState (global);
    const auto ch = root.getChildWithName ("CHAIN");
    if (ch.isValid()) chain.fromValueTree (ch);
    const auto ui = root.getChildWithName ("UI");
    if (ui.isValid()) uiState.copyPropertiesFrom (ui, nullptr);
}

void PedalTrinityProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = captureState();
    state.appendChild (midi.toValueTree(), nullptr);        // assegnazioni MIDI: nel progetto, non nei preset
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PedalTrinityProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // stato del progetto (anche di progetti ricevuti da altri): formato "VC2!" + lunghezza + XML,
    // dimensione e annidamento controllati prima del parser
    if (data == nullptr || sizeInBytes < 9 || sizeInBytes > 8 * 1024 * 1024) return;
    const auto* bytes = static_cast<const char*> (data);
    if (std::memcmp (bytes, "VC2!", 4) == 0 && ! pt::plausibleStateXml (bytes + 8, (size_t) sizeInBytes - 8)) return;
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("PedalTrinityState"))
        {
            const auto root = juce::ValueTree::fromXml (*xml);
            restoreState (root);
            midi.fromValueTree (root.getChildWithName ("MIDI"));
        }
        else if (xml->hasTagName ("PedalTrinity"))
            migrateFromV1 (*xml);
    }
}

/** Stati salvati dalla versione con 3 pedali fissi: diventano i primi tre slot. */
void PedalTrinityProcessor::migrateFromV1 (const juce::XmlElement& xml)
{
    auto v1 = juce::ValueTree::fromXml (xml);
    auto param = [&v1] (const char* id, float def)
    {
        for (int i = 0; i < v1.getNumChildren(); ++i)
            if (v1.getChild (i).getProperty ("id").toString() == id)
                return (float) v1.getChild (i).getProperty ("value");
        return def;
    };
    juce::ValueTree t ("CHAIN");
    auto slot = [&t] (const char* id, bool on, std::initializer_list<float> p)
    {
        juce::ValueTree s ("SLOT");
        s.setProperty ("model", id, nullptr);
        s.setProperty ("on", on, nullptr);
        int k = 0;
        for (float v : p) s.setProperty ("p" + juce::String (k++), v, nullptr);
        t.appendChild (s, nullptr);
    };
    // ED-9: DRIVE, TONE, LEVEL (0..10 -> 0..1)
    slot ("ed9", param ("od_on", 1) > 0.5f, { param ("od_drive", 5) / 10, param ("od_tone", 5) / 10, param ("od_level", 5) / 10 });
    // MC-2W: LEVEL, LOW, HIGH, MIDDLE, MID FREQ, DIST, MODE
    auto db = [] (float v) { return (v + 15.0f) / 30.0f; };
    const float mf = std::log (param ("dist_midfreq", 1000) / 200.0f) / std::log (25.0f);
    slot ("mc2w", param ("dist_on", 0) > 0.5f, { param ("dist_level", 5) / 10, db (param ("dist_low", 0)), db (param ("dist_high", 0)),
                                                 db (param ("dist_mid", 0)), mf, param ("dist_gain", 6) / 10, param ("dist_mode", 0) });
    slot ("gq7", param ("eq_on", 1) > 0.5f, { db (param ("eq_0", 0)), db (param ("eq_1", 0)), db (param ("eq_2", 0)), db (param ("eq_3", 0)),
                                              db (param ("eq_4", 0)), db (param ("eq_5", 0)), db (param ("eq_6", 0)), db (param ("eq_level", 0)) });
    chain.fromValueTree (t);
}

juce::AudioProcessorEditor* PedalTrinityProcessor::createEditor()
{
    return new PedalTrinityEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalTrinityProcessor();
}
