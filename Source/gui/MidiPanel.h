/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    MIDI nell'interfaccia:
      - MidiPanel: impostazioni MIDI (dispositivi MIDI IN/OUT nello
        Standalone, ritorno dello stato, Program Change) ed elenco delle
        assegnazioni, modificabili (canale, modo degli interruttori, gamma)
        o eliminabili; tasti "Impara" per le funzioni senza un comando sullo
        schermo (preset successivo/precedente, bypass generale);
      - MidiLearnBar: barra della mappatura con il tocco (tasto MIDI nella
        barra principale): si tocca un comando e si muove quello della
        pedaliera MIDI.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "../MidiMap.h"

namespace pt::ui
{
    class MidiPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit MidiPanel (midi::MidiManager&);
        ~MidiPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        std::function<void()> onClose;
        /** "Mappa con il tocco": chiude il pannello e avvia la mappatura sulla pedaliera. */
        std::function<void()> onStartTouchLearn;

    private:
        class Row;
        class RowList;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void rebuildRows();
        void rebuildDevices();
        void syncControls();
        juce::Rectangle<int> card() const;

        midi::MidiManager& manager;
        juce::SharedResourcePointer<ThemeManager> themes;
        juce::Label title, devLabel, inLabel, outLabel, pluginInfo, optLabel, listLabel, monitor, learnStatus, empty;
        juce::OwnedArray<juce::ToggleButton> inputs;
        juce::ComboBox output;
        juce::ToggleButton feedback { "Rimanda lo stato sul MIDI OUT (LED e display della pedaliera MIDI)" },
                           programChange { "Program Change senza assegnazione: carica il preset con quel numero" };
        juce::TextButton touchLearn { "Mappa con il tocco" }, learnNext { "Impara: preset +" }, learnPrev { "Impara: preset -" },
                         learnBypass { "Impara: bypass" }, clearAll { "Cancella tutte" }, closeButton { "Chiudi" };
        juce::Viewport rowView;
        std::unique_ptr<RowList> rows;
        juce::Array<juce::MidiDeviceInfo> outDevices;
        int shownVersion = -1;
    };

    //==============================================================================
    class MidiLearnBar : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit MidiLearnBar (midi::MidiManager&);
        ~MidiLearnBar() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<void()> onShowList, onDone;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        void timerCallback() override { repaint (0, 0, 40, getHeight()); }     // spia lampeggiante
        midi::MidiManager& manager;
        juce::SharedResourcePointer<ThemeManager> themes;
        juce::TextButton list { "Assegnazioni..." }, done { "Fine" };
    };
}
