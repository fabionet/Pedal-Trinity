/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pt;

PedalTrinityProcessor::PedalTrinityProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PedalTrinity", createParameterLayout())
{
    odOn = p (ids::odOn); odDrive = p (ids::odDrive); odTone = p (ids::odTone); odLevel = p (ids::odLevel);
    distOn = p (ids::distOn); distLevel = p (ids::distLevel); distGain = p (ids::distGain);
    distLow = p (ids::distLow); distHigh = p (ids::distHigh); distMid = p (ids::distMid);
    distMidFreq = p (ids::distMidFreq); distMode = p (ids::distMode);
    eqOn = p (ids::eqOn); eqLevel = p (ids::eqLevel);
    for (int i = 0; i < ids::eqBands; ++i)
        eqBand[i] = p (ids::eqBand (i));
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
    const int ch = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 1);
    overdrive.prepare (sampleRate, samplesPerBlock, ch);
    distortion.prepare (sampleRate, samplesPerBlock, ch);
    eq.prepare (sampleRate, samplesPerBlock, ch);
    dryBuffer.setSize (ch, samplesPerBlock, false, false, true);

    for (auto* f : { &odFade, &distFade, &eqFade })
        f->mix.reset (sampleRate, 0.012);
    odFade.mix.setCurrentAndTargetValue (odOn->load() > 0.5f ? 1.0f : 0.0f);
    distFade.mix.setCurrentAndTargetValue (distOn->load() > 0.5f ? 1.0f : 0.0f);
    eqFade.mix.setCurrentAndTargetValue (eqOn->load() > 0.5f ? 1.0f : 0.0f);
}

template <typename ProcessFn, typename ResetFn>
void PedalTrinityProcessor::runPedal (BypassFader& fader, bool on, juce::AudioBuffer<float>& buffer,
                                      int numCh, int numSamples, ProcessFn&& process,
                                      ResetFn&& resetFn)
{
    const bool wasOff = fader.fullyOff();
    fader.mix.setTargetValue (on ? 1.0f : 0.0f);
    if (fader.fullyOff())
        return;                         // pedale spento: vero bypass
    if (wasOff)
        resetFn();                      // riaccensione: stato dei filtri pulito

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) numSamples);

    if (! fader.mix.isSmoothing() && fader.mix.getTargetValue() > 0.5f)
    {
        process (block);
        return;
    }

    for (int c = 0; c < numCh; ++c)
        dryBuffer.copyFrom (c, 0, buffer, c, 0, numSamples);
    process (block);
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = fader.mix.getNextValue();
        for (int c = 0; c < numCh; ++c)
        {
            float* w = buffer.getWritePointer (c);
            const float d = dryBuffer.getSample (c, i);
            w[i] = d + (w[i] - d) * m;
        }
    }
}

void PedalTrinityProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, n);

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (n == 0 || numCh == 0)
        return;

    if (n > dryBuffer.getNumSamples() || numCh > dryBuffer.getNumChannels())
    {
        // l'host ha superato la dimensione di blocco dichiarata: si processa a pezzi
        const int maxN = dryBuffer.getNumSamples();
        for (int start = 0; start < n; start += maxN)
        {
            const int len = juce::jmin (maxN, n - start);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), numCh, start, len);
            juce::MidiBuffer dummy;
            processBlock (part, dummy);
        }
        return;
    }

    // --- Overdrive
    overdrive.setParameters (odDrive->load() / 10.0f, odTone->load() / 10.0f, odLevel->load() / 10.0f);
    runPedal (odFade, odOn->load() > 0.5f, buffer, numCh, n,
              [this] (juce::dsp::AudioBlock<float> b) { overdrive.process (b); },
              [this] { overdrive.reset(); });

    // --- Distorsore
    dsp::Distortion::Params dp;
    dp.dist = distGain->load() / 10.0f;
    dp.level = distLevel->load() / 10.0f;
    dp.lowDb = distLow->load();
    dp.highDb = distHigh->load();
    dp.midDb = distMid->load();
    dp.midFreq = distMidFreq->load();
    dp.custom = distMode->load() > 0.5f;
    distortion.setParameters (dp);
    runPedal (distFade, distOn->load() > 0.5f, buffer, numCh, n,
              [this] (juce::dsp::AudioBlock<float> b) { distortion.process (b); },
              [this] { distortion.reset(); });

    // --- Equalizzatore
    float bands[ids::eqBands];
    for (int i = 0; i < ids::eqBands; ++i)
        bands[i] = eqBand[i]->load();
    eq.setParameters (bands, eqLevel->load());
    runPedal (eqFade, eqOn->load() > 0.5f, buffer, numCh, n,
              [this] (juce::dsp::AudioBlock<float> b) { eq.process (b); },
              [this] { eq.reset(); });
}

void PedalTrinityProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PedalTrinityProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PedalTrinityProcessor::createEditor()
{
    return new PedalTrinityEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalTrinityProcessor();
}
