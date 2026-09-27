/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Descrizione dei modelli di pedale (catalogo). Ogni modello indica:
      - nome originale, pedale di riferimento, categoria e famiglia DSP;
      - i comandi (pomelli, pomelli concentrici, selettori, levette, cursori);
      - la descrizione del circuito (per la famiglia "Circuit") oppure i
        parametri di voce per le altre famiglie;
      - l'immagine renderizzata e la posizione dei comandi su di essa.
    Il catalogo vero e proprio e' generato in Catalog.inc (tools/art/catalog.py).
*/

#pragma once

#include <cstdint>

namespace pt::engine
{
    inline constexpr int maxControls = 12;

    enum class Family : uint8_t
    {
        Circuit,        // pedali a guadagno/clipping modellati stadio per stadio
        Compressor,     // compressori/sustainer/limiter a VCA/OTA
        NoiseGate,
        GraphicEQ,
        ParametricEQ,
        Wah,            // auto-wah, touch wah, filtri dinamici
        BBDChorus,      // chorus / dimension / vibrato a BBD
        BBDFlanger,
        Phaser,
        Tremolo,        // tremolo / pan / slicer
        AnalogDelay,    // BBD delay
        DigitalDelay,   // delay digitali multimodo
        TapeEcho,       // Space Echo
        Reverb,
        Pitch,          // octaver / pitch shifter / harmonist
        Synth,
        Acoustic,
        AmpSim,
        CabIR,
        Router,         // AB-2 / LS-2
        Volume,
        Tuner,
        Looper,
        SlowGear,
        Count
    };

    enum class ControlKind : uint8_t
    {
        Knob,           // pomello singolo
        KnobOuter,      // anello esterno di un pomello concentrico
        KnobInner,      // pomello interno di un pomello concentrico
        Selector,       // pomello a scatti (MODE); "steps" posizioni
        Toggle,         // levetta a 2 posizioni
        Slider,         // cursore verticale (EQ grafici)
        Button          // pulsante (looper, tap)
    };

    enum class Units : uint8_t { Dial, Db, Hz, Ms, Percent, Semitone, Choice };

    struct ControlDef
    {
        const char* label;
        const char* role;       // ruolo per le famiglie DSP: "level", "rate", "time", "b0".."b9"...
        ControlKind kind;
        uint8_t strip;          // filmstrip: 0 ts grande, 1 ts piccolo, 2 boss, 3 boss esterno, 4 boss interno,
                                // 5 cursore, 6 levetta, 255 disegnato (pulsanti)
        float def;              // valore normalizzato 0..1 di fabbrica
        uint8_t steps;          // posizioni per Selector/Toggle (0 = continuo)
        Units units;
        float lo, hi;           // gamma mostrata nel fumetto (unita' fisiche)
        const char* choices;    // "S|C" oppure "Room|Hall|..." per selettori
        float x, y;             // ancoraggio (base) del comando nell'immagine, px @1x
        float tx, ty;           // centro della sommita' (pomelli) / fine corsa max (cursori)
        float radius;           // raggio di presa px @1x
    };

    struct ModelDef
    {
        const char* id;          // identificatore stabile nei preset, es. "ds1"
        const char* name;        // nome originale mostrato sul pedale
        const char* code;        // sigla originale Pedal Trinity, es. "DX-1"
        const char* inspiredBy;  // riferimento, es. "BOSS DS-1"
        const char* category;    // categoria per il menu
        Family family;
        int numControls;
        ControlDef controls[maxControls];
        const char* config;      // famiglia Circuit: netlist a stadi; altre famiglie: "chiave=valore ..."
        const char* style;       // "boss" / "ts" / "wide": famiglia grafica dell'enclosure
        uint32_t colour;         // colore principale (per menu e segnaposto)
        const char* image;       // risorsa BinaryData dell'immagine del pedale
        float imageW, imageH;    // dimensioni logiche (px @1x) dell'immagine
        float ledX, ledY, ledR;  // LED di stato
        float footX[4], footY[4];// zona del pedale/footswitch
        float dispX, dispY, dispW, dispH;   // finestra del display (accordatore/looper), 0 se assente
        bool stereo;             // elaborazione stereo nativa
        const char* notes;       // descrizione breve (italiano)
    };

    const ModelDef* findModel (const char* id);
    int numModels();
    const ModelDef& model (int index);
}
