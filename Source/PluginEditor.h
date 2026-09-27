/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/Assets.h"
#include "gui/Controls.h"
#include "gui/InfoPanel.h"

/** Tavola a dimensione logica fissa (UILayout) che viene poi scalata. */
class PedalBoard : public juce::Component
{
public:
    explicit PedalBoard (const pt::ui::Assets& a) : assets (a) { setOpaque (true); }
    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (assets.background(), getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }

private:
    const pt::ui::Assets& assets;
};

class PedalTrinityEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PedalTrinityEditor (PedalTrinityProcessor&);
    ~PedalTrinityEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showInfo (bool shouldShow);

private:
    void timerCallback() override;

    PedalTrinityProcessor& processor;
    pt::ui::PedalLookAndFeel lookAndFeel;
    pt::ui::Assets assets;
    PedalBoard board { assets };

    juce::OwnedArray<pt::ui::FilmstripKnob> knobs;
    juce::OwnedArray<pt::ui::FaderCap> faders;
    juce::OwnedArray<pt::ui::FootSwitch> footswitches;
    juce::OwnedArray<pt::ui::LedGlow> leds;
    std::unique_ptr<pt::ui::ModeToggle> modeToggle;
    pt::ui::InfoButton infoButton;
    std::unique_ptr<pt::ui::InfoPanel> infoPanel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    juce::StringArray ledParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTrinityEditor)
};
