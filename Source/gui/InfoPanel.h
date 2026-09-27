/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Finestra informazioni: versione, autore, licenza GNU GPL v3 completa,
    crediti e apertura della guida PDF illustrata incorporata nel plugin.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pt::ui
{
    class InfoPanel : public juce::Component
    {
    public:
        InfoPanel();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        std::function<void()> onClose;

        /** Estrae la guida PDF incorporata e la apre con il visualizzatore di sistema. */
        static bool openGuide();

    private:
        juce::Rectangle<int> card() const;

        juce::Label title, subtitle;
        juce::TextEditor body, license;
        juce::TextButton guideButton { "Apri la guida PDF" },
                         webButton { "Sito del progetto" },
                         closeButton { "Chiudi" };
    };
}
