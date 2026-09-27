/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Vista di un singolo pedale: immagine renderizzata + comandi regolabili
    sull'immagine, LED, zona del footswitch, display (accordatore/looper).
    Scala liberamente: la stessa vista serve lo slot e il pannello zoom.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Controls.h"
#include "../engine/Chain.h"

namespace pt::ui
{
    class PedalView : public juce::Component, private juce::Timer
    {
    public:
        PedalView (engine::Chain&, int slotIndex, juce::Component* popupParent);
        ~PedalView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Ricollega la vista allo slot (dopo spostamenti o cambi di pedale). */
        void bindTo (int slotIndex);
        int boundIndex() const { return index; }
        const engine::ModelDef* model() const { return def; }

        /** Bocca di una presa jack in coordinate della vista (uscita = fianco destro; line 0 = A, 1 = B).
            false se lo slot e' vuoto. */
        bool jackPoint (bool output, int line, juce::Point<float>& out) const;
        /** Pixel della vista per pixel logico dell'immagine del pedale. */
        float imageScale() const;

    private:
        class Canvas;
        void timerCallback() override;
        void rebuild();
        juce::Rectangle<float> imageArea() const;

        engine::Chain& chain;
        juce::Component* popupParent;
        int index = -1;
        std::shared_ptr<engine::Slot> slotRef;   // mantiene vivo lo slot finche' la vista lo usa
        engine::Slot* slot = nullptr;
        const engine::ModelDef* def = nullptr;
        juce::SharedResourcePointer<Assets> assets;
        juce::SharedResourcePointer<ThemeManager> themes;
        std::unique_ptr<Canvas> canvas;
        juce::Image source, scaled;
        juce::Rectangle<int> scaledFor;
        bool lastOn = false;
    };
}
