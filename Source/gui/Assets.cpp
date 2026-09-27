/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Assets.h"
#include "BinaryData.h"

namespace pt::ui
{
    juce::Image Assets::loadResource (const char* name)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (name, size))
            return juce::ImageCache::getFromMemory (data, size);
        jassertfalse;
        return {};
    }

    juce::String Assets::resourceAsString (const char* name)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (name, size))
            return juce::String::fromUTF8 (data, size);
        return {};
    }

    juce::MemoryBlock Assets::resourceAsBlock (const char* name)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (name, size))
            return juce::MemoryBlock (data, (size_t) size);
        return {};
    }

    Assets::Assets()
    {
        bg = loadResource ("background_jpg");
        for (int i = 0; i < numStrips; ++i)
            images[i] = loadResource (strips[i].resource);
    }

    juce::Rectangle<float> Assets::frameBounds (StripId id, float x, float y)
    {
        const auto& s = strips[id];
        return { x - s.anchorX / renderScale, y - s.anchorY / renderScale,
                 (float) s.frameW / renderScale, (float) s.frameH / renderScale };
    }

    int Assets::frameForProportion (StripId id, double proportion)
    {
        const int n = strips[id].frames;
        return juce::jlimit (0, n - 1, juce::roundToInt (proportion * (n - 1)));
    }

    void Assets::drawFrame (juce::Graphics& g, StripId id, int index, float x, float y) const
    {
        const auto& s = strips[id];
        const auto& img = images[id];
        if (! img.isValid())
            return;
        index = juce::jlimit (0, s.frames - 1, index);
        const int sx = (index % s.cols) * s.frameW;
        const int sy = (index / s.cols) * s.frameH;
        const auto dst = frameBounds (id, x, y);
        // posizionamento sub-pixel: evita "saltelli" del pomello quando l'interfaccia e' scalata
        g.drawImage (img.getClippedImage ({ sx, sy, s.frameW, s.frameH }), dst, juce::RectanglePlacement::stretchToFit);
    }
}
