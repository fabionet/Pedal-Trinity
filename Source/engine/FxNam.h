/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    NAM-A1A2 Model: lettore di modelli Neural Amp Modeler (A1, A2-Lite/A2-Full,
    LSTM; file .nam e .namb) con due canali A e B, ciascuno con il proprio
    modello e la propria risposta all'impulso (IR).

    Catena di ogni canale, come nel plugin NAM ufficiale v0.7.15:
      ingresso (INPUT + calibrazione) -> noise gate (trigger) -> modello (ricampionato
      alla sua frequenza) -> noise gate (guadagno) -> volume NAM -> tonestack
      BASS/MIDDLE/TREBLE -> IR -> volume IR -> filtro anti-DC 5 Hz -> uscita
      (OUTPUT + modo Raw / Normalized / Calibrated).
    In una catena stereo il canale A elabora la linea A e il B la linea B; in un
    tratto mono entrambi ricevono lo stesso segnale e le uscite si sommano.

    I file vengono validati da NamSecurity (modelli) e da loadIr (risposte
    all'impulso); il caricamento avviene nel thread dei messaggi e il nuovo
    modello passa al thread audio senza lock.
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <juce_dsp/juce_dsp.h>
#include "Effect.h"

namespace pt::engine
{
    class NamEffect final : public Effect
    {
    public:
        // comandi sul pedale (ordine del catalogo)
        enum Ctrl { Input, Bass, Middle, Treble, Output, NamA, IrA, NamB, IrB, ChanA, ChanB };
        // meter sul display (picchi): per canale IN, NAM, IR
        enum Meter { InA, NamOutA, IrOutA, InB, NamOutB, IrOutB, numMeters };

        struct Options
        {
            bool calibrateInput = false;        // come il plugin NAM: calibrazione d'ingresso disattivata
            double calibrationLevel = 12.0;     // dBu
            int outputMode = 1;                 // 0 Raw, 1 Normalized, 2 Calibrated
            bool noiseGate = true;
            double gateThreshold = -80.0;       // dB
            bool toneStack = true;
            bool irEnabled = true;
            double slim[2] { 1.0, 1.0 };        // A2 slimmable: 0 = Lite ... 1 = Full
        };

        struct ChannelStatus
        {
            bool hasNam = false, hasIr = false, slimmable = false, warning = false;
            std::string namFile, irFile, namKind, message;
            double namLoad = 0.0;
        };

        explicit NamEffect (const ModelDef&);
        ~NamEffect() override;

        void prepare (double sampleRate, int maxBlock) override;
        void reset() override;
        void process (float* const* ch, int numCh, int n) override;
        float readout (int index) const override;
        void messageThreadUpdate() override;
        std::string saveState() const override;
        void restoreState (const std::string&) override;

        //==================== thread dei messaggi (pannello di zoom)
        /** Carica e verifica un modello; con 'expectedSha' il file deve avere esattamente quell'impronta SHA-256. */
        bool loadNam (int channel, const juce::File&, const std::string& expectedSha = {});
        void clearNam (int channel);
        bool loadIr (int channel, const juce::File&);
        void clearIr (int channel);
        ChannelStatus status (int channel) const { return state[(size_t) juce::jlimit (0, 1, channel)]; }
        Options options() const;
        void setOptions (const Options&);
        /** Estensioni ammesse nei selettori di file. */
        static juce::String modelWildcard() { return "*.nam;*.namb"; }
        static juce::String irWildcard() { return "*.wav;*.aif;*.aiff"; }

    private:
        struct ModelSlot;
        struct Channel;
        void stage (int channel, std::unique_ptr<ModelSlot>);
        void runChannel (Channel&, int index, const float* in, float* out, int n);

        std::array<std::unique_ptr<Channel>, 2> chans;
        std::array<ChannelStatus, 2> state;                 // thread dei messaggi
        std::array<ModelSlot*, 2> latest { nullptr, nullptr };   // ultimo modello consegnato (thread dei messaggi)
        mutable std::array<std::atomic<float>, numMeters> meters;

        std::atomic<bool> calibrate { false }, gateOn { true }, eqOn { true }, irOn { true };
        std::atomic<double> calLevel { 12.0 }, gateThreshold { -80.0 };
        std::atomic<int> outputMode { 1 };
        double slim[2] { 1.0, 1.0 };
        std::string irPath[2], irSha[2], namPath[2], namSha[2];
        double sampleRate = 48000.0;
        int maxBlock = 512;
        bool prepared = false;
    };
}
