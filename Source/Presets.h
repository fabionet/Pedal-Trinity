/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Preset: file XML ".ptpreset" con la catena completa (pedali, posizioni,
    manopole, on/off) e i parametri globali.
      Linux:   ~/.config/PedalTrinity/Presets
      Windows: %APPDATA%\PedalTrinity\Presets
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PedalTrinityProcessor;

namespace pt
{
    /** Controllo preliminare di un testo XML di stato o preset (prima del parser): dimensione massima e
        annidamento limitato, cosi' un file costruito ad arte non esaurisce memoria o stack. */
    bool plausibleStateXml (const char* text, size_t size);

    class PresetManager
    {
    public:
        explicit PresetManager (PedalTrinityProcessor& p) : processor (p) {}

        static juce::File folder();
        juce::Array<juce::File> userPresets() const;
        static juce::StringArray factoryNames();

        /** Privacy: toglie dallo stato i percorsi locali dei file (IR, modelli NAM) e le loro impronte. */
        static void stripLocalReferences (juce::ValueTree& state);
        bool save (const juce::String& name);                 // salva nella cartella utente
        bool saveTo (const juce::File& file, bool includeExternalReferences = false); // esporta; i riferimenti locali sono esclusi
        bool load (const juce::File& file);                   // carica / importa
        bool loadFactory (int index);
        bool remove (const juce::File& file);
        void initialise();                                    // catena vuota

        juce::String currentName() const { return current; }

    private:
        PedalTrinityProcessor& processor;
        juce::String current { "Default" };
    };
}
