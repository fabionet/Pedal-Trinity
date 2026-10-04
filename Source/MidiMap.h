/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Programmazione MIDI: una pedaliera MIDI esterna (o la traccia MIDI della
    DAW) comanda i pedali della pedaliera.

    Ogni assegnazione collega un messaggio in ingresso (Control Change, nota o
    Program Change, su un canale o su tutti) a una funzione:
      - footswitch di uno slot (acceso/spento) o un suo comando (pomello,
        selettore, levetta, cursore), con la gamma min..max;
      - INPUT, OUTPUT, bypass generale, preset successivo/precedente.
    Le assegnazioni seguono la posizione dello slot (Slot 3 resta Slot 3 anche
    cambiando il pedale) e sono salvate nel progetto (non nei preset).

    Thread audio: i messaggi sugli slot sono applicati subito nel blocco audio
    (valori atomici, nessun lock, nessuna allocazione); tutto il resto passa al
    thread dei messaggi con una coda senza lock. La tabella delle assegnazioni
    e' pubblicata come istantanea immutabile (come la catena).

    MIDI OUT: con il "ritorno dello stato" attivo, ogni funzione assegnata
    rimanda il proprio valore sullo stesso messaggio (LED e display della
    pedaliera esterna restano allineati); nel plugin esce dalla porta MIDI del
    plugin, nello Standalone dal dispositivo MIDI OUT scelto.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <vector>

class PedalTrinityProcessor;

namespace pt::midi
{
    enum class Source : uint8_t { CC = 0, Note = 1, Program = 2 };
    enum class Target : uint8_t { SlotSwitch = 0, SlotControl = 1, InputGain = 2, OutputGain = 3, Bypass = 4, PresetNext = 5, PresetPrev = 6 };
    /** Come un messaggio comanda un interruttore (footswitch, bypass). */
    enum class Mode : uint8_t
    {
        Follow = 0,     // valore >= 64 acceso, < 64 spento (pedaliere con interruttori a scatto)
        Toggle = 1,     // ogni pressione (>= 64) inverte; il rilascio e' ignorato (interruttori momentanei)
        Momentary = 2   // acceso finche' e' premuto: pressione e rilascio invertono
    };

    inline constexpr int maxMappings = 256;

    struct Mapping
    {
        int channel = 0;            // 0 = tutti i canali (omni), 1..16
        Source source = Source::CC;
        int number = 0;             // CC o nota 0..127 (Program Change: numero di programma)
        Target target = Target::SlotSwitch;
        int slot = -1;              // slot 0..99 (solo funzioni di slot)
        int control = -1;           // comando 0..11 (SlotControl)
        Mode mode = Mode::Follow;
        float lo = 0.0f, hi = 1.0f; // gamma del comando per i valori 0..127

        bool sameSource (const Mapping& o) const noexcept { return channel == o.channel && source == o.source && number == o.number; }
        bool sameTarget (const Mapping& o) const noexcept
        {
            return target == o.target && (target != Target::SlotSwitch || slot == o.slot)
                   && (target != Target::SlotControl || (slot == o.slot && control == o.control));
        }
        bool isSwitch() const noexcept { return target == Target::SlotSwitch || target == Target::Bypass; }
    };

    /** Accesso ai dispositivi MIDI dello Standalone (nullptr nel plugin: il MIDI arriva dalla DAW). */
    struct StandaloneDevices
    {
        juce::AudioDeviceManager* manager = nullptr;
        std::function<void()> outputChanged;           // riapplica il MIDI OUT predefinito al lettore audio
        static StandaloneDevices& get() { static StandaloneDevices d; return d; }
    };

    class MidiManager : public juce::ChangeBroadcaster,
                        private juce::Timer,
                        private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        explicit MidiManager (PedalTrinityProcessor&);
        ~MidiManager() override;

        //======================== thread audio
        /** Legge i messaggi in ingresso, applica quelli sugli slot, svuota il buffer e vi scrive il MIDI OUT. */
        void processBlock (juce::MidiBuffer&) noexcept;

        //======================== thread dei messaggi
        std::vector<Mapping> mappings() const { return list; }
        void setMappings (std::vector<Mapping>);
        /** Aggiunge un'assegnazione: sostituisce quelle con lo stesso messaggio o la stessa funzione. */
        void addMapping (const Mapping&);
        void removeMapping (int index);
        void updateMapping (int index, const Mapping&);
        void clearMappings() { setMappings ({}); }
        /** Cresce a ogni modifica dell'elenco delle assegnazioni. */
        int version() const { return listVersion; }

        /** Mappatura: tocca un comando nell'interfaccia (o premi un tasto "Impara"), poi muovi il comando MIDI. */
        void setLearning (bool);
        bool isLearning() const { return learning; }
        /** Funzione in attesa del messaggio MIDI (target valido solo se hasArmed()). */
        bool hasArmed() const { return armedValid; }
        const Mapping& armed() const { return armedTarget; }
        void arm (const Mapping& target);
        /** Testo di stato della mappatura (per la barra nell'interfaccia). */
        juce::String learnStatus() const { return status; }

        bool feedbackEnabled() const { return feedback; }
        void setFeedbackEnabled (bool);
        bool programChangeSelectsPreset() const { return pcPresets; }
        void setProgramChangeSelectsPreset (bool b) { pcPresets = b; sendChangeMessage(); }
        /** Ultimo messaggio ricevuto (monitor), vuoto se nessuno. */
        juce::String lastMessage() const { return lastText; }

        juce::ValueTree toValueTree() const;
        void fromValueTree (const juce::ValueTree&);

        juce::String describeSource (const Mapping&) const;
        juce::String describeTarget (const Mapping&) const;
        static juce::String noteName (int note);

        /** Autotest: valuta un messaggio come se arrivasse dal thread audio e scarica subito le code. */
        void injectForTest (const juce::MidiMessage&);
        /** Autotest: messaggi del ritorno dello stato (tutti, come al primo invio). */
        juce::MidiBuffer feedbackForTest();

    private:
        struct Table { int count = 0; Mapping m[maxMappings]; bool pcPresets = true; };
        struct Event { uint8_t kind = 0, channel = 1, number = 0, value = 0; };   // kind: 0 CC, 1 nota, 2 Program

        void timerCallback() override;
        void parameterChanged (const juce::String& id, float) override;
        void publish();
        void drainEvents();
        void handleGlobal (const Mapping&, const Event&);
        void handleLearn (const Event&);
        void sendFeedback (bool force);
        void stepPreset (int delta);
        void loadPresetIndex (int index);
        static bool matches (const Mapping&, const Event&) noexcept;

        PedalTrinityProcessor& processor;
        std::vector<Mapping> list;                         // thread dei messaggi
        std::vector<int> lastSent;                         // ultimo valore rimandato per ogni assegnazione
        std::atomic<Table*> active { nullptr };
        std::atomic<Table*> inUse { nullptr };
        std::vector<std::unique_ptr<Table>> retired;
        std::unique_ptr<Table> current;

        juce::AbstractFifo inFifo { 512 }, outFifo { 512 };
        Event inEvents[512];
        juce::MidiMessage outEvents[512];
        std::atomic<bool> slotsChanged { false };
        std::atomic<int> pendingParam { -1 };              // fader INPUT/OUTPUT toccato durante la mappatura
        bool suppressParamLearn = false;

        bool learning = false, armedValid = false, feedback = true, pcPresets = true;
        Mapping armedTarget;
        juce::String status, lastText;
        int learnedIndex = -1;                             // assegnazione appena imparata (riconoscimento interruttori momentanei)
        juce::uint32 learnedAt = 0;
        int presetIndex = -1, listVersion = 0;
        bool forceFeedback = true;
    };
}
