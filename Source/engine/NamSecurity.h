/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Caricamento sicuro dei modelli Neural Amp Modeler (.nam e .namb).

    Regola di sicurezza per file alterati o costruiti ad arte, in quest'ordine:
      1. file regolare con estensione .nam o .namb (nient'altro) e contenuto coerente con
         l'estensione (.namb inizia con "NAMB", .nam e' un oggetto JSON); limiti di dimensione;
      2. .nam: annidamento JSON limitato (prima del parser), chiavi obbligatorie, versione
         supportata dal core, architettura ammessa, metadati di calibrazione plausibili,
         pesi numerici finiti e in una gamma ragionevole, sample rate plausibile;
      3. ogni modello (anche annidato: sotto-modelli A2, condition DSP) deve avere
         ESATTAMENTE i pesi che la sua configurazione consuma (NamWeights): un file troncato
         o manipolato non puo' far leggere memoria oltre i limiti;
      4. .namb: header, dimensioni, offset e CRC32 verificati dal loader rinforzato;
      5. prova a vuoto prima dell'uso: un segnale di prova deve dare uscita finita, senza
         picchi esplosivi, e il modello deve girare abbondantemente in tempo reale;
      6. impronta SHA-256 del file: salvata nel preset e confrontata alla riapertura.
    In caso di errore il modello NON viene caricato e il messaggio spiega il motivo.
*/

#pragma once

#include <memory>
#include <string>
#include <juce_core/juce_core.h>

namespace nam { class DSP; }

namespace pt::namsafe
{
    struct ModelInfo
    {
        std::string architecture;           // "WaveNet", "SlimmableContainer", "LSTM", ...
        std::string kind;                   // descrizione breve: "A1", "A2 (Full + Lite)", "LSTM"...
        std::string sha256;                 // impronta del file
        bool slimmable = false;
        bool binary = false;                // .namb
        double sampleRate = 48000.0;
        std::size_t weights = 0;            // pesi totali verificati
        double realtimeLoad = 0.0;          // tempo di calcolo / durata audio (prova a vuoto)
    };

    /** Valida e carica un modello. Lancia SecurityError (messaggio in italiano) se il file non e' ammesso. */
    std::unique_ptr<nam::DSP> loadModel (const juce::File& file, ModelInfo& info);

    /** Impronta SHA-256 (esadecimale) di un file, vuota se non leggibile. */
    std::string sha256Of (const juce::File& file);

    /** true se l'estensione e' ammessa (.nam / .namb, maiuscole comprese). */
    bool hasModelExtension (const juce::File& file);
}
