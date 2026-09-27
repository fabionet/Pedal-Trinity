/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/Pedalboard.h"
#include "gui/InfoPanel.h"
#include "gui/OptionsPanel.h"

class PedalTrinityEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           private juce::ChangeListener
{
public:
    explicit PedalTrinityEditor (PedalTrinityProcessor&);
    ~PedalTrinityEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showInfo (bool shouldShow);
    void showOptions (bool shouldShow);
    void showZoom (int slotIndex);
    void setView (int pedalsVisible);
    void scrollBy (int delta);

    /** Risoluzioni supportate dai tasti di zoom (da 1280x760 a 2K). */
    static const juce::Array<juce::Point<int>>& sizePresets();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyTheme();
    void refresh();
    void zoomStep (int dir);
    void showPresetMenu();
    void addPedal();
    void saveUiState();
    juce::Rectangle<int> rackArea() const;

    PedalTrinityProcessor& processor;
    pt::ui::PedalLookAndFeel lookAndFeel;

    juce::TextButton presetButton, firstButton { "<<" }, prevButton { "<" }, nextButton { ">" }, lastButton { ">>" },
                     addButton { "+ Pedale" }, zoomOut { "-" }, zoomIn { "+" }, infoButton { "INFO" };
    juce::TextButton viewButtons[4];
    juce::Label pageLabel, zoomLabel, logo;

    std::unique_ptr<pt::ui::Pedalboard> board;
    std::unique_ptr<pt::ui::ZoomPanel> zoomPanel;
    std::unique_ptr<pt::ui::InfoPanel> infoPanel;
    std::unique_ptr<pt::ui::OptionsPanel> optionsPanel;
    std::unique_ptr<juce::Button> optionsButton;
    juce::SharedResourcePointer<pt::ui::ThemeManager> themes;
    juce::TooltipWindow tooltips { this, 500 };
    std::unique_ptr<juce::FileChooser> chooser;

    int view = 3, first = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTrinityEditor)
};
