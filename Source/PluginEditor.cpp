/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "PluginEditor.h"
#include "Version.h"

using namespace pt;
using namespace pt::ui;

namespace
{
    juce::String pedalOnParam (const juce::String& pedal)
    {
        if (pedal == "ed9") return ids::odOn;
        if (pedal == "mc2") return ids::distOn;
        return ids::eqOn;
    }

    /** Nome breve mostrato nel fumetto del valore. */
    juce::String shortName (const juce::String& id)
    {
        static const std::map<juce::String, juce::String> names {
            { "od_drive", "DRIVE" }, { "od_tone", "TONE" }, { "od_level", "LEVEL" },
            { "dist_level", "LEVEL" }, { "dist_gain", "DIST" }, { "dist_low", "LOW" }, { "dist_high", "HIGH" },
            { "dist_mid", "MIDDLE" }, { "dist_midfreq", "MID FREQ" },
            { "eq_0", "100 Hz" }, { "eq_1", "200 Hz" }, { "eq_2", "400 Hz" }, { "eq_3", "800 Hz" },
            { "eq_4", "1.6 kHz" }, { "eq_5", "3.2 kHz" }, { "eq_6", "6.4 kHz" }, { "eq_level", "LEVEL" } };
        auto it = names.find (id);
        return it != names.end() ? it->second : id;
    }
}

PedalTrinityEditor::PedalTrinityEditor (PedalTrinityProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processor.apvts;

    board.setSize ((int) uiWidth, (int) std::ceil (uiHeight));
    addAndMakeVisible (board);

    auto setupSlider = [&] (juce::Slider& s, const juce::String& id)
    {
        s.setPopupDisplayEnabled (true, false, &board);
        board.addAndMakeVisible (s);
        sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, id, s));
        if (auto* param = apvts.getParameter (id))
        {
            s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            s.setTitle (param->getName (64));
        }
        auto base = s.textFromValueFunction;
        const auto label = shortName (id);
        s.textFromValueFunction = [base, label] (double v) { return label + "  " + (base ? base (v) : juce::String (v, 1)); };
        s.updateText();
    };

    // --- pomelli: prima i pomelli esterni, poi quelli interni concentrici (sopra)
    for (const auto& k : pt::ui::knobs)
    {
        if (k.strip == bossInner)
            continue;
        float exclusion = 0.0f;
        if (k.strip == bossOuter)
            for (const auto& inner : pt::ui::knobs)
                if (inner.strip == bossInner && std::abs (inner.ax - k.ax) < 1.0f)
                    exclusion = inner.radius * 1.05f;
        auto* knob = knobs.add (new FilmstripKnob (assets, k, exclusion));
        setupSlider (*knob, k.paramId);
    }
    for (const auto& k : pt::ui::knobs)
    {
        if (k.strip != bossInner)
            continue;
        auto* knob = knobs.add (new FilmstripKnob (assets, k));
        setupSlider (*knob, k.paramId);
    }

    // --- cursori del GQ-7
    for (const auto& f : pt::ui::faders)
    {
        auto* fader = faders.add (new FaderCap (assets, f));
        setupSlider (*fader, f.paramId);
    }

    // --- levetta S/C
    if (auto* mode = apvts.getParameter (ids::distMode))
    {
        modeToggle = std::make_unique<ModeToggle> (assets, *mode);
        board.addAndMakeVisible (*modeToggle);
    }

    // --- footswitch e LED
    for (const auto& fs : pt::ui::footswitches)
    {
        auto* b = footswitches.add (new FootSwitch (fs));
        board.addAndMakeVisible (b);
        buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, pedalOnParam (fs.pedal), *b));
    }
    for (const auto& l : pt::ui::leds)
    {
        auto* led = leds.add (new LedGlow (l));
        board.addAndMakeVisible (led);
        ledParams.add (pedalOnParam (l.pedal));
    }

    infoButton.onClick = [this] { showInfo (true); };
    board.addAndMakeVisible (infoButton);

    // --- dimensioni: proporzioni fisse, ridimensionabile dal 50% al 200%
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (uiWidth / uiHeight);
        c->setSizeLimits ((int) (uiWidth * 0.5f), (int) (uiHeight * 0.5f), (int) (uiWidth * 2.0f), (int) (uiHeight * 2.0f));
    }
    setSize ((int) uiWidth, (int) std::round (uiHeight));

    timerCallback();
    startTimerHz (20);
}

PedalTrinityEditor::~PedalTrinityEditor()
{
    stopTimer();
    sliderAttachments.clear();
    buttonAttachments.clear();
    setLookAndFeel (nullptr);
}

void PedalTrinityEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void PedalTrinityEditor::resized()
{
    const float scale = (float) getWidth() / uiWidth;
    board.setTransform (juce::AffineTransform::scale (scale));
}

void PedalTrinityEditor::timerCallback()
{
    for (int i = 0; i < leds.size(); ++i)
        if (auto* v = processor.apvts.getRawParameterValue (ledParams[i]))
            leds[i]->setOn (v->load() > 0.5f);
}

void PedalTrinityEditor::showInfo (bool shouldShow)
{
    if (shouldShow)
    {
        infoPanel = std::make_unique<InfoPanel>();
        // chiusura asincrona: il pannello non puo' distruggersi dentro il proprio callback
        const juce::Component::SafePointer<PedalTrinityEditor> safeThis (this);
        infoPanel->onClose = [safeThis]
        {
            juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->showInfo (false); });
        };
        infoPanel->setBounds (board.getLocalBounds());
        board.addAndMakeVisible (*infoPanel);
        infoPanel->grabKeyboardFocus();
    }
    else
    {
        infoPanel.reset();
    }
}
