/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Temi dell'interfaccia: la pedana su cui poggiano i pedali (disegnata
    proceduralmente: guide in alluminio con tessuto a strappo, tolex, legno,
    alluminio spazzolato, moquette) e la tavolozza coordinata di barra,
    pannelli, intestazioni e pulsanti. Barra, pannelli e intestazioni restano
    scuri in ogni tema: i testi sono sempre leggibili (contrasto verificato
    dall'autotest secondo i criteri WCAG).

    Le preferenze (tema, cavi) sono globali: valgono per tutte le istanze del
    plugin e per lo Standalone e sono salvate in PedalTrinity/Options.settings.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace pt::ui
{
    enum class BoardStyle { Rails, BrushedRails, Tolex, Wood, Carpet };

    struct Theme
    {
        juce::String id, name, description;
        BoardStyle board;
        juce::Colour boardBase, boardAlt;   // materiale della pedana e colore secondario (fessure, venature)
        juce::Colour trim;                  // telaio / profilo / angolari / viti
        juce::Colour ink;                   // segni disegnati sulla pedana (celle libere, lettere delle corsie)
        juce::Colour window, barTop, barBottom;
        juce::Colour accent, text, textDim;
        juce::Colour panelTop, panelBottom, header;
        juce::Colour button, selected;
    };

    const std::vector<Theme>& allThemes();

    /** Colori dei cavi selezionabili nelle opzioni. */
    struct CableColour { const char* name; juce::Colour body, sheen; };
    const std::vector<CableColour>& cableColours();

    /** Rapporto di contrasto WCAG 2.1 tra due colori (1..21). */
    double contrastRatio (juce::Colour, juce::Colour);

    /** Disegna la pedana del tema nell'area indicata (scale = px per px di riferimento @1280). */
    void paintBoard (juce::Graphics&, const Theme&, juce::Rectangle<float> area, float scale);

    /** Tema corrente e opzioni, condivisi (juce::SharedResourcePointer<ThemeManager>). */
    class ThemeManager : public juce::ChangeBroadcaster
    {
    public:
        ThemeManager();
        ~ThemeManager() override;

        const Theme& current() const;
        int currentIndex() const { return index; }
        void select (int themeIndex);
        /** Tema scelto senza salvarlo nelle preferenze (screenshot della guida). */
        void preview (int themeIndex);

        bool showCables() const { return cables; }
        void setShowCables (bool);
        int cableColourIndex() const { return cableColour; }
        void setCableColour (int);
        const CableColour& cable() const;

    private:
        void save();
        std::unique_ptr<juce::PropertiesFile> props;
        int index = 0, cableColour = 0;
        bool cables = true;
    };
}
