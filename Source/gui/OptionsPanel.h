/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pannello OPZIONI: tema della pedaliera (anteprime con la pedana vera e due
    pedali), cavi jack visibili e colore dei cavi, modalita' REAL MOD e foto personali. Le scelte valgono subito e
    vengono ricordate (PedalTrinity/Options.settings).
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "RealMod.h"

namespace pt::ui
{
    class OptionsPanel : public juce::Component, private juce::ChangeListener
    {
    public:
        OptionsPanel();
        ~OptionsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        std::function<void()> onClose;

    private:
        class ThemeTile;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void syncControls();
        juce::Rectangle<int> card() const;

        juce::SharedResourcePointer<ThemeManager> themes;
        juce::OwnedArray<ThemeTile> tiles;
        juce::ToggleButton showCables { "Mostra i cavi jack" };
        juce::ComboBox cableColour;
        juce::TextButton closeButton { "Chiudi" };
        juce::Label title, themeLabel, cableLabel, colourLabel, realLabel, realInfo;
        juce::ToggleButton realMode { "REAL MOD PEDALBOARD: repliche dei pedali reali" };
        juce::TextButton openPhotos { "Cartella foto personali..." }, reloadPhotos { "Ricarica foto" };
        juce::SharedResourcePointer<RealPhotos> photos;
    };
}
