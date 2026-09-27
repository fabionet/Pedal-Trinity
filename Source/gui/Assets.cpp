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
        // colore in JPEG ("<id>_jpg") + trasparenza e ombra in una maschera PNG ("<id>_a_png"):
        // il pedale si appoggia sulla pedana del tema scelto
        if (resource == nullptr) return {};
        const auto key = juce::String ("pedal:") + resource;
        auto cached = juce::ImageCache::getFromHashCode (key.hashCode64());
        if (cached.isValid()) return cached;

        auto colour = loadResource (resource);
        const auto maskName = juce::String (resource).upToLastOccurrenceOf ("_jpg", false, false) + "_a_png";
        const auto mask = loadResource (maskName.toRawUTF8());
        if (colour.isValid() && mask.isValid() && mask.getBounds() == colour.getBounds())
        {
            juce::Image out (juce::Image::ARGB, colour.getWidth(), colour.getHeight(), false);
            const juce::Image::BitmapData src (colour, juce::Image::BitmapData::readOnly);
            const juce::Image::BitmapData m (mask, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < out.getHeight(); ++y)
                for (int x = 0; x < out.getWidth(); ++x)
                    dst.setPixelColour (x, y, src.getPixelColour (x, y).withAlpha (m.getPixelColour (x, y).getRed()));
            colour = out;
        }
        juce::ImageCache::addImageToCache (colour, key.hashCode64());
        return colour;
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
