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

    juce::Image Assets::pedalImage (const char* resource)
    {
        return resource != nullptr ? loadResource (resource) : juce::Image();
    }

    Assets::Assets()
    {
        for (int i = 0; i < numStrips; ++i)
            images[i] = loadResource (strips[i].resource);
    }

    juce::Rectangle<float> Assets::frameBounds (int id, float x, float y)
    {
        if (! juce::isPositiveAndBelow (id, (int) numStrips)) return { x - 10, y - 10, 20, 20 };
        const auto& s = strips[id];
        return { x - s.anchorX / renderScale, y - s.anchorY / renderScale,
                 (float) s.frameW / renderScale, (float) s.frameH / renderScale };
    }

    int Assets::frameCount (int id)
    {
        return juce::isPositiveAndBelow (id, (int) numStrips) ? strips[id].frames : 1;
    }

    int Assets::frameForProportion (int id, double proportion)
    {
        const int n = frameCount (id);
        return juce::jlimit (0, n - 1, juce::roundToInt (proportion * (n - 1)));
    }

    void Assets::drawFrame (juce::Graphics& g, int id, int index, float x, float y) const
    {
        if (! juce::isPositiveAndBelow (id, (int) numStrips)) return;
        const auto& s = strips[id];
        const auto& img = images[id];
        if (! img.isValid()) return;
        index = juce::jlimit (0, s.frames - 1, index);
        const int sx = (index % s.cols) * s.frameW, sy = (index / s.cols) * s.frameH;
        g.drawImage (img.getClippedImage ({ sx, sy, s.frameW, s.frameH }), frameBounds (id, x, y),
                     juce::RectanglePlacement::stretchToFit);
    }
}
