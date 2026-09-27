/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Immagini incorporate: filmstrip 3D dei comandi (condivise) e render
    dei pedali. Si usa tramite juce::SharedResourcePointer<Assets>.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Strips.h"

namespace pt::ui
{
    class Assets
    {
    public:
        Assets();

        /** Disegna il fotogramma "index" della filmstrip con l'ancoraggio in (x, y) (px logici). */
        void drawFrame (juce::Graphics& g, int stripId, int index, float x, float y) const;
        static juce::Rectangle<float> frameBounds (int stripId, float x, float y);
        static int frameForProportion (int stripId, double proportion);
        static int frameCount (int stripId);

        /** Immagine di un pedale (dalla cache di JUCE). */
        static juce::Image pedalImage (const char* resource);

        static juce::Image loadResource (const char* name);
        static juce::String resourceAsString (const char* name);
        static juce::MemoryBlock resourceAsBlock (const char* name);

    private:
        juce::Image images[numStrips];
    };
}
