/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Cavi jack della pedaliera: spine da 6,3 mm infilate nelle prese dei
    pedali, cavo in gomma che cede sotto il proprio peso, prese a pannello
    dei meter. Tutto disegnato vettorialmente e scalato con i pedali.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pt::ui
{
    struct Cable
    {
        juce::Point<float> a, b;    // bocche delle prese (sorgente, destinazione)
        float dirA = 1.0f;          // verso d'uscita della spina in a: +1 destra, -1 sinistra, 0 nessuna spina
        float dirB = -1.0f;         // verso della spina in b
        float scale = 1.0f;         // pixel per pixel logico del pedale (dimensioni di spine e cavo)
        int line = 0;               // 0 = A, 1 = B (colore dell'anello sulla spina)
        bool under = false;         // passa sotto la pedaliera (ritorno tra due file)
        float laneY = 0.0f;         // cavo "under": quota del passaggio tra le due file
        int id = -1;                // collegamento della pedaliera a cui appartiene (spine trascinabili)
        juce::Colour body, sheen;   // colore della gomma (trasparente = colore delle opzioni)
    };

    namespace cables
    {
        /** Lunghezza della spina (dalla bocca della presa all'attacco del cavo), px logici. */
        inline constexpr float plugLength = 46.0f;

        void drawCable (juce::Graphics&, const Cable&, juce::Colour body = juce::Colour (0xff141416),
                        juce::Colour sheen = juce::Colour (0xff2a2b30));
        void drawPlug (juce::Graphics&, juce::Point<float> jack, float dir, float scale, int line);
        /** Presa a pannello (dado esagonale e bocca) centrata in p. */
        void drawSocket (juce::Graphics&, juce::Point<float> p, float scale);
    }

    /** Strato trasparente sopra la pedaliera: cavi "sopra" e spine.
        Riceve il mouse solo sulle spine: una spina si stacca trascinandola e si infila in un altro
        pedale (o si lascia nel vuoto per staccare il pedale dalla catena). */
    class CableOverlay : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        /** Esito del passaggio sopra un possibile bersaglio durante il trascinamento di una spina. */
        struct Target { bool any = false, ok = false; juce::Rectangle<float> area; juce::String hint; };

        CableOverlay();
        void setCables (std::vector<Cable> c);
        /** Colore della gomma e visibilita' dei cavi (opzioni). */
        void setStyle (juce::Colour b, juce::Colour s, bool show) { body = b; sheen = s; visible = show; repaint(); }
        const std::vector<Cable>& getCables() const { return list; }
        bool isDraggingPlug() const { return drag >= 0; }
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        /** Spina presa (id del collegamento, true = estremita' di destinazione). */
        std::function<void (int id, bool destEnd)> onGrab;
        /** Spina trascinata sopra un punto della pedaliera: dove andrebbe a finire. */
        std::function<Target (int id, bool destEnd, juce::Point<float>)> onMove;
        /** Spina rilasciata; moved = false se e' stato solo un clic (nessun cambiamento). */
        std::function<void (int id, bool destEnd, juce::Point<float>, bool moved)> onDrop;

    private:
        juce::Rectangle<float> plugArea (const Cable&, bool endB) const;
        std::vector<Cable> list;
        juce::Colour body { 0xff141416 }, sheen { 0xff2a2b30 };
        bool visible = true;
        int drag = -1;                  // indice in list della spina trascinata
        bool dragB = false;
        juce::Point<float> grabOffset, mouse;
        Target target;
    };
}
