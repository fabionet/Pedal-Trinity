/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Catena di pedali (fino a 100 slot).

    Il modello della catena vive nel thread dei messaggi. Ogni modifica
    strutturale (aggiunta, rimozione, spostamento, cambio di pedale)
    produce una nuova "istantanea" immutabile che viene pubblicata al
    thread audio con un puntatore atomico. Il thread audio non blocca
    mai e non libera mai memoria: le istantanee vecchie (e gli effetti
    rimossi) vengono distrutte nel thread dei messaggi quando il thread
    audio ha smesso di usarle.
    I parametri dei pedali sono atomici: girare un pomello non ricostruisce
    nulla.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "Effect.h"

namespace pt::engine
{
    inline constexpr int maxSlots = 100;

    /** Uno slot della catena: pedale (o vuoto) + stato on/off. */
    struct Slot
    {
        const ModelDef* def = nullptr;          // nullptr = slot vuoto
        std::unique_ptr<Effect> fx;
        std::atomic<bool> enabled { true };
        // stato usato solo dal thread audio
        float fade = 1.0f;
        bool wasOff = false;
    };

    class Chain : public juce::ChangeBroadcaster, private juce::Timer
    {
    public:
        Chain();
        ~Chain() override;

        //======================== thread audio
        void prepare (double sampleRate, int maxBlock);
        void process (juce::AudioBuffer<float>& buffer, int numChannels);

        //======================== thread dei messaggi
        int size() const;
        Slot* slot (int index) const;
        /** Riferimento condiviso: mantiene vivo lo slot anche se viene rimosso dalla catena. */
        std::shared_ptr<Slot> slotRef (int index) const;
        bool canAdd() const { return size() < maxSlots; }
        /** Inserisce uno slot (vuoto se modelId e' vuoto) nella posizione indicata (-1 = in fondo). */
        int insert (int index, const juce::String& modelId);
        void remove (int index);
        void move (int from, int to);
        void setModel (int index, const juce::String& modelId);
        void setEnabled (int index, bool on);
        void setParam (int index, int control, float value);
        void clear();

        juce::ValueTree toValueTree() const;
        void fromValueTree (const juce::ValueTree&);

        /** Numero di pedali (non vuoti) e stato per la GUI. */
        double getSampleRate() const { return sampleRate; }

    private:
        struct Snapshot { std::vector<std::shared_ptr<Slot>> slots; };

        void publish();
        void timerCallback() override;
        std::shared_ptr<Slot> makeSlot (const juce::String& modelId) const;

        std::vector<std::shared_ptr<Slot>> model;           // thread dei messaggi
        mutable juce::CriticalSection modelLock;             // protegge 'model' per get/setState da altri thread

        std::atomic<Snapshot*> active { nullptr };
        std::atomic<Snapshot*> inUse { nullptr };
        juce::CriticalSection publishLock;                   // protegge retired/current (mai usato dal thread audio)
        std::vector<std::unique_ptr<Snapshot>> retired;      // istantanee in attesa di essere liberate
        std::unique_ptr<Snapshot> current;                   // proprietario dell'istantanea attiva

        double sampleRate = 48000.0;
        int blockSize = 512;
        juce::AudioBuffer<float> dry;
    };
}
