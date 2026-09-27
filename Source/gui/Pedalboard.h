/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    La pedaliera: meter INPUT a sinistra, celle dei pedali al centro,
    meter OUTPUT a destra, cavi jack che collegano tutto.

    Disposizione delle celle:
      - catena singola (senza splitter, o splitter MONO/STEREO): una fila di
        3/6/9 celle, oppure due file da 9 nella vista 18 (il cavo che torna
        a capo passa sotto la pedaliera);
      - splitter in DUAL: due file, corsia A sopra e corsia B sotto; gli slot
        prima dello splitter stanno nella fila A, la corsia B parte dalla
        colonna dopo lo splitter.
    La pagina si misura in celle (catena singola) o in colonne (DUAL).
*/

#pragma once

#include "Rack.h"
#include "Meters.h"
#include "Cables.h"

class PedalTrinityProcessor;

namespace pt::ui
{
    class Pedalboard : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
    {
    public:
        struct Callbacks
        {
            std::function<void (int)> zoom;
            std::function<void (int delta)> scroll;
        };

        Pedalboard (PedalTrinityProcessor&, juce::Component* popupParent, Callbacks);
        ~Pedalboard() override;

        /** Pagina: prima cella/colonna visibile e vista (3, 6, 9, 18). */
        void setPage (int first, int view);
        /** Da chiamare quando la catena cambia. */
        void refresh();

        bool isDual() const;
        /** Lunghezza della catena e di una pagina, nelle unita' della pagina (celle o colonne). */
        int contentLength() const;
        int pageLength() const;
        /** Unita' di pagina in cui si trova lo slot (per portarlo in vista). */
        int unitOfSlot (int slotIndex) const;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        struct Cell
        {
            int slot = -1;          // -1 = cella vuota (in coda), -2 = nessuna cella (fila B prima dello splitter)
            int row = 0, col = 0;   // posizione sulla pagina
            int lane = 0;
            juce::Rectangle<int> bounds;
        };
        struct End { enum Kind { Input, Output, SlotIn, SlotOut } kind; int slot = -1; int line = 0; };

        void timerCallback() override;
        void layout();
        void updateCables();
        bool endPoint (const End&, const End& other, juce::Point<float>& p, float& dir, float& scale, int& row) const;
        int cellIndexAt (juce::Point<int>) const;
        SlotComponent* componentFor (int slotIndex) const;
        juce::Rectangle<int> boardArea() const;
        float rowJackY (int row, int line) const;

        PedalTrinityProcessor& processor;
        engine::Chain& chain;
        juce::Component* popupParent;
        Callbacks cb;

        MeterPanel inPanel, outPanel;
        juce::OwnedArray<SlotComponent> slotComps;
        std::vector<Cell> cells;
        CableOverlay overlay;
        std::vector<Cable> underCables;
        juce::TextButton pageLeft { "<" }, pageRight { ">" }, addA { "+" }, addB { "+" };

        int first = 0, view = 3, dropCell = -1;
        int rows = 1, cols = 3;
        bool dual = false;
        int splitIndex = -1;
        juce::Rectangle<int> cellGrid;
    };
}
