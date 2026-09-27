/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Presets.h"
#include "PluginProcessor.h"

namespace pt
{
    namespace
    {
        struct FactoryPreset { const char* name; const char* slots; };
        // "modello:on:p0,p1,..." separati da ';' (valori normalizzati 0..1)
        const FactoryPreset factory[] = {
            { "Pedal Trinity (originale)", "ed9:1:0.5,0.5,0.5;mc2w:0:0.5,0.5,0.5,0.5,0.5,0.6,0;gq7:1" },
            { "Blues pulito", "cs3:1;bd2:1:0.5,0.5,0.35;ce2:1;rv6:1" },
            { "Rock classico", "ed9:1:0.2,0.6,0.8;ds1:1:0.55,0.55,0.6;dd3:1;rv6:1" },
            { "Metal moderno", "ns2:1;sd1:1:0.85,0.55,0.1;mc2w:1:0.45,0.6,0.55,0.25,0.4,0.75,1;gq7:1" },
            { "Ambient", "cs3:1;ce5:1;dd8:1;rv6:1;tr2:0" },
            { "Fuzz vintage", "tb2w:1;ce2:1;re2:1" },
            { "Amplificatore + cabinet", "ed9:1:0.2,0.5,0.7;ir2:1;rv5:1" },
        };
    }

    juce::File PresetManager::folder()
    {
        // Linux: ~/.config (XDG) - Windows: %APPDATA%
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("PedalTrinity").getChildFile ("Presets");
        f.createDirectory();
        return f;
    }

    juce::Array<juce::File> PresetManager::userPresets() const
    {
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.ptpreset");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });
        return files;
    }

    juce::StringArray PresetManager::factoryNames()
    {
        juce::StringArray s;
        for (auto& f : factory) s.add (f.name);
        return s;
    }

    bool PresetManager::saveTo (const juce::File& file)
    {
        auto state = processor.captureState();
        state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
        if (auto xml = state.createXml())
            if (xml->writeTo (file))
            {
                current = file.getFileNameWithoutExtension();
                return true;
            }
        return false;
    }

    bool PresetManager::save (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty()) return false;
        return saveTo (folder().getChildFile (clean).withFileExtension ("ptpreset"));
    }

    bool PresetManager::load (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("PedalTrinityState")) return false;
        processor.restoreState (juce::ValueTree::fromXml (*xml));
        current = file.getFileNameWithoutExtension();
        return true;
    }

    bool PresetManager::loadFactory (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) std::size (factory))) return false;
        juce::ValueTree chain ("CHAIN");
        for (auto& tok : juce::StringArray::fromTokens (factory[index].slots, ";", ""))
        {
            auto parts = juce::StringArray::fromTokens (tok, ":", "");
            juce::ValueTree s ("SLOT");
            s.setProperty ("model", parts[0], nullptr);
            s.setProperty ("on", parts.size() < 2 || parts[1] != "0", nullptr);
            if (parts.size() > 2)
            {
                auto vals = juce::StringArray::fromTokens (parts[2], ",", "");
                for (int i = 0; i < vals.size(); ++i) s.setProperty ("p" + juce::String (i), vals[i].getFloatValue(), nullptr);
            }
            chain.appendChild (s, nullptr);
        }
        processor.chain.fromValueTree (chain);
        current = factory[index].name;
        return true;
    }

    bool PresetManager::remove (const juce::File& file)
    {
        return file.isAChildOf (folder()) && file.deleteFile();
    }

    void PresetManager::initialise()
    {
        processor.chain.fromValueTree (juce::ValueTree ("CHAIN"));
        processor.chain.insert (-1, {});
        current = "Nuovo";
    }
}
