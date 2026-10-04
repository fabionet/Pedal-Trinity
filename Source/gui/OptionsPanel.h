/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pannello OPZIONI: tema della pedaliera (anteprime con la pedana vera e due
    pedali), pedana personalizzata (materiale di un altro tema, colore scelto o
    immagine propria), cavi jack visibili e colore dei cavi, modalita' REAL MOD
    con le sue liste di pedali (la classica BOSS e quelle personalizzate, ognuna
    nella propria cartella). Le scelte valgono subito e vengono ricordate
    (PedalTrinity/Options.settings).
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
        /** Apre impostazioni e assegnazioni MIDI. */
        std::function<void()> onMidi;

    private:
        class ThemeTile;
        class TileGrid;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void syncControls();
        void fillLists();
        juce::Rectangle<int> card() const;
        /** Chiede un nome di lista e chiama done(nome) con OK. */
        void askName (const juce::String& title, const juce::String& message, const juce::String& initial,
                      std::function<void (const juce::String&)> done);
        void showError (const juce::String& title, const juce::String& message);
        void chooseBoardColour();

        juce::SharedResourcePointer<ThemeManager> themes;
        juce::Viewport tileView;
        std::unique_ptr<TileGrid> grid;
        juce::ToggleButton showCables { "Mostra i cavi jack" };
        juce::ComboBox cableColour, boardSource, realList;
        juce::TextButton closeButton { "Chiudi" };
        juce::TextButton boardImage { "Immagine..." }, boardColour { "Colore..." }, boardReset { "Colore originale" };
        juce::Label title, themeLabel, boardLabel, cableLabel, colourLabel, cableInfo, realLabel, realInfo, listLabel;
        juce::ToggleButton realMode { "REAL MOD PEDALBOARD" };
        juce::TextButton newList { "Nuova..." }, copyList { "Duplica..." }, renameList { "Rinomina..." }, deleteList { "Elimina" };
        juce::TextButton openPhotos { "Apri cartella" }, reloadPhotos { "Ricarica foto" };
        juce::Label midiLabel, midiInfo;
        juce::TextButton midiButton { "Impostazioni e assegnazioni MIDI..." };
        juce::SharedResourcePointer<RealPhotos> photos;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::StringArray listNames;            // voci di realList dopo la classica
    };
}
