/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Caricamento delle immagini incorporate (foto dei pedali e filmstrip 3D).
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UILayout.h"

namespace pt::ui
{
    class Assets
    {
    public:
        Assets();

        const juce::Image& background() const { return bg; }

        /** Disegna il fotogramma "index" della filmstrip con l'ancoraggio in (x, y) (px @1x). */
        void drawFrame (juce::Graphics& g, StripId id, int index, float x, float y) const;

        /** Rettangolo (px @1x) occupato da un fotogramma ancorato in (x, y). */
        static juce::Rectangle<float> frameBounds (StripId id, float x, float y);

        static int frameForProportion (StripId id, double proportion);

        static juce::Image loadResource (const char* name);
        static juce::String resourceAsString (const char* name);
        static juce::MemoryBlock resourceAsBlock (const char* name);

    private:
        juce::Image bg;
        juce::Image images[numStrips];
    };
}
