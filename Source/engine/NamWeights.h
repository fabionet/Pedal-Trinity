/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Regola di sicurezza sui pesi dei modelli NAM.

    Il core NAM legge i pesi con un iteratore senza controllare la fine del
    vettore in diversi modelli (LSTM, WaveNet generico): un file .nam/.namb
    alterato con MENO pesi di quelli richiesti dalla sua configurazione farebbe
    leggere memoria oltre il vettore. Prima di costruire un modello si ricava
    quindi il numero ESATTO di pesi che la configurazione consumera':
      - LSTM: formula (celle, bias, stati iniziali, testa);
      - Linear: il core controlla gia' la dimensione esatta;
      - WaveNet / SlimmableWavenet (A1, A2): "sonda" - si fa costruire il modello
        al core con un vettore volutamente molto piu' lungo (nessuna lettura puo'
        uscire) di valori tutti diversi; il core segnala quanti pesi ha usato;
      - ConvNet e architetture sconosciute: rifiutate.
    Se il numero non coincide con i pesi del file, il file e' rifiutato.
*/

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace nam { struct ModelConfig; }

namespace pt::namsafe
{
    /** Errore di sicurezza: il file non viene caricato (messaggio in italiano per l'interfaccia). */
    struct SecurityError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Limite di pesi per singolo modello (i modelli A1/A2 ne hanno da 10 mila a qualche centinaio di migliaia). */
    inline constexpr std::size_t maxWeights = 4u * 1024u * 1024u;

    /** Numero di pesi che la configurazione consumera'. Lancia SecurityError se non determinabile in sicurezza. */
    std::size_t expectedWeights (nam::ModelConfig& config, const std::string& architecture);

    /** Da chiamare una volta: forza il linker a includere tutte le architetture del core (restituisce quante). */
    int linkAllArchitectures();

    /** Verifica che 'provided' sia esattamente il numero richiesto; lancia SecurityError altrimenti. */
    void checkWeightCount (nam::ModelConfig& config, const std::string& architecture, std::size_t provided);
}
