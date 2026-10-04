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

    Instradamento (come con cavi veri):
      - il segnale entra mono (ingresso 1) e ogni pedale riceve un solo cavo;
        un pedale stereo in un tratto mono usa solo le prese A;
      - lo splitter SPL-3 (al massimo uno) divide la catena dal suo punto in poi:
          MONO   il segnale prosegue mono (nessuna divisione);
          DUAL   due catene mono indipendenti: corsia A -> uscita L, corsia B -> R;
          STEREO una catena stereo: i pedali stereo elaborano A e B, un pedale
                 mono prende solo A e riporta il segnale in mono finche' un
                 pedale stereo non lo riallarga;
      - i pedali staccati con i cavi (patched = false) restano sulla pedaliera
        ma il segnale passa oltre, come se non ci fossero;
      - all'uscita il segnale mono va su L e R; con lo splitter attivo si
        applicano BALANCE e livelli LEFT/RIGHT d'uscita.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "Effect.h"

namespace pt::engine
{
    inline constexpr int maxSlots = 100;

    enum class SplitMode : int { Mono = 0, Dual = 1, Stereo = 2 };

    /** Uno slot della catena: pedale (o vuoto) + stato on/off + corsia. */
    struct Slot
    {
        const ModelDef* def = nullptr;          // nullptr = slot vuoto
        std::unique_ptr<Effect> fx;
        std::atomic<bool> enabled { true };
        std::atomic<int> lane { 0 };            // 0 = A, 1 = B (conta solo dopo lo splitter in DUAL)
        std::atomic<bool> patched { true };     // collegato con i cavi: false = staccato dalla catena (il segnale lo salta)
        // stato usato solo dal thread audio
        float fade = 1.0f;
        bool wasOff = false;

        bool isSplitter() const noexcept { return def != nullptr && def->family == Family::Splitter; }
    };

    /** Picchi per i meter: il thread audio accumula, l'interfaccia legge e azzera. */
    struct LevelMeter
    {
        std::atomic<float> peak[2] { { 0.0f }, { 0.0f } };
        std::atomic<int> channels { 1 };

        void push (int ch, float v) noexcept
        {
            float cur = peak[ch].load (std::memory_order_relaxed);
            while (v > cur && ! peak[ch].compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }
        float take (int ch) noexcept { return peak[ch].exchange (0.0f, std::memory_order_relaxed); }
    };

    class Chain : public juce::ChangeBroadcaster, private juce::Timer
    {
    public:
        Chain();
        ~Chain() override;

        //======================== thread audio
        void prepare (double sampleRate, int maxBlock);
        /** Canale 0 = ingresso mono (gia' con il guadagno d'ingresso); in uscita L/R (o somma se mono). */
        void process (juce::AudioBuffer<float>& buffer, int numChannels);

        //======================== thread dei messaggi
        int size() const;
        Slot* slot (int index) const;
        /** Riferimento condiviso: mantiene vivo lo slot anche se viene rimosso dalla catena. */
        std::shared_ptr<Slot> slotRef (int index) const;
        bool canAdd() const { return size() < maxSlots; }
        /** Inserisce uno slot (vuoto se modelId e' vuoto) nella posizione indicata (-1 = in fondo).
            Restituisce l'indice, -1 se la catena e' piena o se si tenta un secondo splitter. */
        int insert (int index, const juce::String& modelId, int lane = 0);
        void remove (int index);
        void move (int from, int to);
        /** Sposta uno slot e lo assegna a una corsia (A = 0, B = 1). */
        void moveTo (int from, int to, int lane);
        /** Cambia il pedale di uno slot; false se si tenta un secondo splitter. */
        bool setModel (int index, const juce::String& modelId);
        void setEnabled (int index, bool on);
        void setParam (int index, int control, float value);
        void setLane (int index, int lane);
        /** Cavi: un pedale staccato resta sulla pedaliera ma il segnale lo salta (lo splitter non si stacca). */
        void setPatched (int index, bool patched);
        bool isPatched (int index) const;
        /** Ricollega tutti i pedali staccati (catena completa come all'inizio). */
        void patchAll();

        //======================== MIDI (thread audio, senza lock e senza allocazioni)
        /** Comando MIDI su uno slot: kind 0 = acceso/spento (value >= 0.5 acceso), 1 = inverte l'acceso,
            2 = valore del comando 'control' (0..1). false se lo slot non esiste. */
        bool applyFromAudio (int slotIndex, int kind, int control, float value) noexcept;
        /** Lettura dal thread dei messaggi dello stato di uno slot (per il ritorno MIDI): -1 se assente. */
        float readForMidi (int slotIndex, int control) const;

        /** Comando toccato a mano nell'interfaccia (slot, comando; -1 = footswitch): per la mappatura MIDI. */
        std::function<void (int slot, int control)> onUserTouch;
        void notifyTouch (int slotIndex, int control) { if (onUserTouch) onUserTouch (slotIndex, control); }
        void clear();

        /** Indice dello splitter, -1 se assente. */
        int splitterIndex() const;
        /** Modalita' dello splitter (MONO se assente). */
        SplitMode splitMode() const;
        /** true se lo splitter c'e' ed e' in DUAL o STEREO. */
        bool isSplitActive() const { return splitMode() != SplitMode::Mono; }
        /** Si puo' mettere lo splitter nello slot indicato (nessun altro splitter nella catena)? */
        bool canPlaceSplitter (int index) const;
        int lane (int index) const;

        juce::ValueTree toValueTree() const;
        void fromValueTree (const juce::ValueTree&);

        double getSampleRate() const { return sampleRate; }

        //======================== uscita (scritti dal processore, letti dal thread audio)
        std::atomic<float> outBalance { 0.0f };       // -1 (sinistra) .. +1 (destra)
        std::atomic<float> outGainL { 1.0f }, outGainR { 1.0f };

        LevelMeter inputMeter;          // ingresso della catena (A/B se lo splitter e' nel primo slot)
        std::atomic<int> outputChannels { 1 };   // 2 quando lo splitter e' attivo

    private:
        struct Snapshot
        {
            std::vector<std::shared_ptr<Slot>> slots;
            int split = -1;
        };

        void publish();
        void timerCallback() override;
        std::shared_ptr<Slot> makeSlot (const juce::String& modelId) const;
        int splitterIndexLocked() const;
        void runSlot (Slot&, float* const* ch, int nch, int n);

        std::vector<std::shared_ptr<Slot>> model;           // thread dei messaggi
        mutable juce::CriticalSection modelLock;             // protegge 'model' per get/setState da altri thread

        std::atomic<Snapshot*> active { nullptr };
        std::atomic<Snapshot*> inUse { nullptr };
        juce::CriticalSection publishLock;                   // protegge retired/current (mai usato dal thread audio)
        std::vector<std::unique_ptr<Snapshot>> retired;      // istantanee in attesa di essere liberate
        std::unique_ptr<Snapshot> current;                   // proprietario dell'istantanea attiva

        double sampleRate = 48000.0;
        int blockSize = 512;
        juce::AudioBuffer<float> lines, dry;                 // linee A/B e copia "dry" per le dissolvenze
        float lastGain[4] { 1.0f, 1.0f, 1.0f, 1.0f };       // mandate A/B e uscite L/R (rampe senza click)
    };
}
