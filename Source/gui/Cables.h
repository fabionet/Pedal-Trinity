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
    };

    namespace cables
    {
        /** Lunghezza della spina (dalla bocca della presa all'attacco del cavo), px logici. */
        inline constexpr float plugLength = 46.0f;

        void drawCable (juce::Graphics&, const Cable&);
        void drawPlug (juce::Graphics&, juce::Point<float> jack, float dir, float scale, int line);
        /** Presa a pannello (dado esagonale e bocca) centrata in p. */
        void drawSocket (juce::Graphics&, juce::Point<float> p, float scale);
    }

    /** Strato trasparente sopra la pedaliera: cavi "sopra" e spine. Non riceve il mouse. */
    class CableOverlay : public juce::Component
    {
    public:
        CableOverlay() { setInterceptsMouseClicks (false, false); }
        void setCables (std::vector<Cable> c) { list = std::move (c); repaint(); }
        const std::vector<Cable>& getCables() const { return list; }
        void paint (juce::Graphics&) override;

    private:
        std::vector<Cable> list;
    };
}
