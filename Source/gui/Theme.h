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
    enum class BoardStyle { Rails, BrushedRails, Tolex, Wood, Carpet, Tweed, DiamondPlate, Carbon, Stone };

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

    /** Colori dei cavi selezionabili nelle opzioni. L'ultimo, "Multicolore", da' a ogni cavo un colore diverso. */
    struct CableColour { const char* name; juce::Colour body, sheen; };
    const std::vector<CableColour>& cableColours();
    /** Indice di "Multicolore" in cableColours(). */
    int multiCableIndex();

    /** Rapporto di contrasto WCAG 2.1 tra due colori (1..21). */
    double contrastRatio (juce::Colour, juce::Colour);
    /** Colore dei segni sulla pedana (celle libere) leggibile su una pedana di questo colore. */
    juce::Colour inkFor (juce::Colour board);

    /** Disegna la pedana del tema nell'area indicata (scale = px per px di riferimento @1280). */
    void paintBoard (juce::Graphics&, const Theme&, juce::Rectangle<float> area, float scale);
    /** Pedana con un'immagine dell'utente (riempie l'area, cornice scura e vignettatura). */
    void paintBoardImage (juce::Graphics&, const juce::Image&, juce::Rectangle<float> area, float scale);

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
        /** Colore del cavo n-esimo della pedaliera (diverso per ogni cavo in "Multicolore"). */
        const CableColour& cableFor (int cableIndex) const;
        /** Anteprime senza salvare le preferenze (screenshot della guida). */
        void previewCableColour (int);
        void previewBoardSource (const juce::String&);

        /** Pedana personalizzata: "" = quella del tema, id di un tema = la pedana di quel tema,
            "image" = immagine scelta dall'utente (boardImageFile). */
        juce::String boardSource() const { return boardSrc; }
        void setBoardSource (const juce::String&);
        /** Colore personalizzato della pedana (trasparente = colori originali del materiale). */
        juce::Colour boardTint() const { return tint; }
        void setBoardTint (juce::Colour);
        juce::File boardImageFile() const { return boardImagePath; }
        /** Imposta l'immagine della pedana (jpg/png, verificata come le foto REAL MOD): errore o stringa vuota. */
        juce::String setBoardImage (const juce::File&);
        /** Immagine della pedana gia' decodificata (non valida se assente o rifiutata). */
        const juce::Image& boardImage() const { return boardImg; }
        /** Tema da usare per disegnare la pedana: materiale e colori scelti (tavolozza del tema corrente). */
        Theme boardTheme() const;
        /** Chiave che cambia quando cambia l'aspetto della pedana (cache delle immagini). */
        juce::String boardKey() const;

        /** Modalita' REAL MOD PEDALBOARD: repliche fedeli dei pedali reali (o le foto personali). */
        bool realMode() const { return real; }
        void setRealMode (bool);
        /** Come setRealMode ma senza salvarla nelle preferenze (screenshot della guida). */
        void previewRealMode (bool);
        /** Lista REAL MOD in uso: "" = lista classica (repliche BOSS, cartella RealPhotos), altrimenti una lista personalizzata. */
        juce::String realList() const { return realListName; }
        void setRealList (const juce::String&);

    private:
        void save();
        std::unique_ptr<juce::PropertiesFile> props;
        void loadBoardImage();
        int index = 0, cableColour = 0;
        bool cables = true, real = false;
        juce::String boardSrc, realListName;
        juce::Colour tint { juce::Colours::transparentBlack };
        juce::File boardImagePath;
        juce::Image boardImg;
    };
}
