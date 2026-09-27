/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "NamWeights.h"

#include <limits>
#include <regex>
#include <vector>
#include <NAM/model_config.h>
#include <NAM/lstm.h>
#include <NAM/linear.h>
#include <NAM/convnet.h>
#include <NAM/wavenet/model.h>
#include <NAM/wavenet/slimmable.h>
#include <NAM/container.h>
#include <set>

namespace pt::namsafe
{
    namespace
    {
        void requireRange (long v, long lo, long hi, const char* what)
        {
            if (v < lo || v > hi)
                throw SecurityError (std::string ("parametro fuori dai limiti nel modello: ") + what);
        }

        /** Sonda: il core costruisce il modello con un vettore molto piu' lungo del necessario (nessuna
            lettura fuori limite possibile) e con valori tutti diversi; il messaggio d'errore sui pesi
            in eccesso rivela quanti ne ha consumati. */
        std::size_t probeWaveNet (nam::ModelConfig& cfg)
        {
            const std::size_t probeSize = maxWeights + 4096;
            std::vector<float> probe (probeSize);
            for (std::size_t i = 0; i < probeSize; ++i)
                probe[i] = (float) (i + 1);                  // interi esatti in float fino a 2^24

            try
            {
                auto dsp = cfg.create (probe, 48000.0);
            }
            catch (const SecurityError&) { throw; }
            catch (const std::exception& e)
            {
                const std::string msg = e.what();
                std::smatch m;
                // WaveNet generico: "Weight mismatch: assigned K weights, but N were provided." (K = usati + 1)
                if (std::regex_search (msg, m, std::regex ("assigned ([0-9]+) weights")))
                {
                    const auto k = std::stoull (m[1].str());
                    if (k >= 1 && k <= probeSize) return (std::size_t) (k - 1);
                }
                // percorso veloce A2: "weight stream has D trailing ..." (usati = forniti - D)
                if (std::regex_search (msg, m, std::regex ("has ([0-9]+) trailing")))
                {
                    const auto d = std::stoull (m[1].str());
                    if (d <= probeSize) return (std::size_t) (probeSize - d);
                }
                throw SecurityError ("configurazione del modello non valida (" + msg + ")");
            }
            // nessun errore: il modello userebbe piu' pesi del limite di sicurezza
            throw SecurityError ("il modello richiede troppi pesi (oltre il limite di sicurezza)");
        }
    }

    std::size_t expectedWeights (nam::ModelConfig& config, const std::string& architecture)
    {
        if (auto* l = dynamic_cast<nam::lstm::LSTMConfig*> (&config))
        {
            requireRange (l->num_layers, 1, 16, "LSTM num_layers");
            requireRange (l->input_size, 1, 64, "LSTM input_size");
            requireRange (l->hidden_size, 1, 1024, "LSTM hidden_size");
            requireRange (l->in_channels, 1, 2, "LSTM in_channels");
            requireRange (l->out_channels, 1, 2, "LSTM out_channels");
            // per cella: W (4H x (in + H)) + b (4H) + stato iniziale h (H) + c (H); testa: out x H + out
            std::size_t n = 0;
            const std::size_t h = (std::size_t) l->hidden_size;
            for (int i = 0; i < l->num_layers; ++i)
            {
                const std::size_t in = (std::size_t) (i == 0 ? l->input_size : l->hidden_size);
                n += 4 * h * (in + h) + 4 * h + 2 * h;
            }
            n += (std::size_t) l->out_channels * h + (std::size_t) l->out_channels;
            return n;
        }
        if (auto* lin = dynamic_cast<nam::linear::LinearConfig*> (&config))
        {
            requireRange (lin->receptive_field, 1, 1 << 20, "Linear receptive_field");
            return (std::size_t) lin->receptive_field + (lin->bias ? 1u : 0u);
        }
        if (dynamic_cast<nam::convnet::ConvNetConfig*> (&config) != nullptr)
            throw SecurityError ("architettura ConvNet non ammessa (solo WaveNet A1/A2, LSTM, Linear)");
        // WaveNet A1/A2 (anche la variante veloce A2 e lo slimmable): la sonda e' sicura per costruzione
        static const std::set<std::string> waveNetNames { "WaveNet", "SlimmableWavenet", "namb", "condition_dsp" };
        if (dynamic_cast<nam::wavenet::WaveNetConfig*> (&config) != nullptr
            || dynamic_cast<nam::slimmable_wavenet::SlimmableWavenetConfig*> (&config) != nullptr
            || waveNetNames.count (architecture) > 0)
            return probeWaveNet (config);
        throw SecurityError ("architettura non ammessa: " + architecture);
    }

    int linkAllArchitectures()
    {
        // riferimenti espliciti: il linker deve includere i file del core che registrano le architetture
        // (altrimenti i modelli A2 "SlimmableContainer" non avrebbero un parser)
        using Parser = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);
        static volatile Parser parsers[] = { &nam::container::create_config, &nam::lstm::create_config,
                                             &nam::linear::create_config, &nam::convnet::create_config,
                                             &nam::wavenet::create_config };
        int n = 0;
        for (auto p : parsers) n += p != nullptr ? 1 : 0;
        return n;
    }

    void checkWeightCount (nam::ModelConfig& config, const std::string& architecture, std::size_t provided)
    {
        if (provided > maxWeights)
            throw SecurityError ("il modello ha troppi pesi (" + std::to_string (provided) + ")");
        const std::size_t expected = expectedWeights (config, architecture);
        if (expected != provided)
            throw SecurityError ("file alterato o danneggiato: il modello richiede " + std::to_string (expected)
                                 + " pesi ma il file ne contiene " + std::to_string (provided));
    }
}
