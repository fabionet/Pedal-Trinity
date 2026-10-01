/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "NamSecurity.h"
#include "NamWeights.h"

#include <juce_cryptography/juce_cryptography.h>
#include <cmath>
#include <filesystem>
#include <regex>
#include <set>
#include "json.hpp"
#include <NAM/activations.h>
#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#include <NAM/model_config.h>
#include <NAM/slimmable.h>
#include <namb/get_dsp_namb.h>
#include "ResamplingNAM.h"

namespace pt::namsafe
{
    namespace
    {
        using json = nlohmann::json;

        constexpr juce::int64 maxNamBytes = 64 * 1024 * 1024, maxNambBytes = 32 * 1024 * 1024;
        constexpr int maxJsonDepth = 48, maxNesting = 4;
        constexpr std::size_t maxTotalWeights = 2 * maxWeights;

        [[noreturn]] void fail (const std::string& why) { throw SecurityError (why); }

        /** Profondita' massima di annidamento del JSON, calcolata sul testo prima del parser. */
        int jsonDepth (const char* p, std::size_t n)
        {
            int depth = 0, maxDepth = 0;
            bool inString = false, escape = false;
            for (std::size_t i = 0; i < n; ++i)
            {
                const char ch = p[i];
                if (inString)
                {
                    if (escape) escape = false;
                    else if (ch == '\\') escape = true;
                    else if (ch == '"') inString = false;
                    continue;
                }
                if (ch == '"') inString = true;
                else if (ch == '{' || ch == '[') maxDepth = std::max (maxDepth, ++depth);
                else if (ch == '}' || ch == ']') --depth;
                if (depth < 0) return 1 << 20;
            }
            return inString || depth != 0 ? 1 << 20 : maxDepth;
        }

        void checkLevel (const json& meta, const char* key, double lo, double hi)
        {
            if (! meta.contains (key) || meta[key].is_null()) return;
            if (! meta[key].is_number()) fail (std::string ("metadato non numerico: ") + key);
            const double v = meta[key].get<double>();
            if (! std::isfinite (v) || v < lo || v > hi) fail (std::string ("metadato fuori dai limiti: ") + key);
        }

        /** Convalida ricorsiva di una specifica di modello (file o sotto-modello). */
        void validateSpec (const json& j, int nesting, std::size_t& totalWeights, std::string& topArch)
        {
            if (nesting > maxNesting) fail ("troppi modelli annidati");
            if (! j.is_object()) fail ("la radice del modello non e' un oggetto JSON");
            for (const char* key : { "version", "architecture", "config", "weights" })
                if (! j.contains (key)) fail (std::string ("chiave obbligatoria mancante: ") + key);
            if (! j["version"].is_string() || ! j["architecture"].is_string() || ! j["config"].is_object() || ! j["weights"].is_array())
                fail ("tipi delle chiavi obbligatorie non validi");

            const std::string version = j["version"].get<std::string>();
            if (! std::regex_match (version, std::regex ("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}")))
                fail ("versione del file non valida: " + version);
            if (nam::is_version_supported (version) == nam::Supported::NO)
                fail ("versione " + version + " non supportata da NeuralAmpModelerCore");

            const std::string arch = j["architecture"].get<std::string>();
            static const std::set<std::string> allowed { "WaveNet", "LSTM", "Linear", "SlimmableContainer" };
            if (allowed.count (arch) == 0) fail ("architettura non ammessa: " + arch);
            if (nesting == 0) topArch = arch;

            if (j.contains ("metadata") && ! j["metadata"].is_null())
            {
                const auto& meta = j["metadata"];
                if (! meta.is_object()) fail ("metadati non validi");
                checkLevel (meta, "loudness", -90.0, 30.0);
                checkLevel (meta, "input_level_dbu", -60.0, 60.0);
                checkLevel (meta, "output_level_dbu", -60.0, 60.0);
            }
            if (j.contains ("sample_rate") && ! j["sample_rate"].is_null())
            {
                if (! j["sample_rate"].is_number()) fail ("sample rate non numerico");
                const double sr = j["sample_rate"].get<double>();
                if (! (sr == -1.0 || (std::isfinite (sr) && sr >= 8000.0 && sr <= 384000.0)))
                    fail ("sample rate non plausibile");
            }

            const auto& w = j["weights"];
            if (w.size() > maxWeights) fail ("il modello ha troppi pesi");
            totalWeights += w.size();
            if (totalWeights > maxTotalWeights) fail ("i modelli contengono troppi pesi in totale");
            for (const auto& x : w)
            {
                if (! x.is_number()) fail ("peso non numerico");
                const double v = x.get<double>();
                if (! std::isfinite (v) || std::abs (v) > 1.0e6) fail ("peso non finito o fuori scala");
            }

            // prima i modelli annidati (il core li costruisce durante l'analisi della configurazione)
            const auto& cfg = j["config"];
            if (arch == "SlimmableContainer")
            {
                if (! w.empty()) fail ("un contenitore A2 non deve avere pesi propri");
                if (! cfg.contains ("submodels") || ! cfg["submodels"].is_array() || cfg["submodels"].empty() || cfg["submodels"].size() > 8)
                    fail ("sotto-modelli A2 mancanti o in numero non valido");
                for (const auto& sub : cfg["submodels"])
                {
                    if (! sub.is_object() || ! sub.contains ("model") || ! sub.contains ("max_value") || ! sub["max_value"].is_number())
                        fail ("sotto-modello A2 non valido");
                    const double mv = sub["max_value"].get<double>();
                    if (! std::isfinite (mv) || mv < 0.0 || mv > 1.0) fail ("soglia del sotto-modello A2 non valida");
                    validateSpec (sub["model"], nesting + 1, totalWeights, topArch);
                }
                return;
            }
            for (const auto& item : cfg.items())
                if (item.value().is_object() && item.value().contains ("architecture"))
                    validateSpec (item.value(), nesting + 1, totalWeights, topArch);   // es. condition_dsp

            // pesi esatti per questa configurazione
            std::unique_ptr<nam::ModelConfig> mc;
            try
            {
                mc = nam::parse_model_config_json (arch, cfg, 48000.0);
            }
            catch (const SecurityError&) { throw; }
            catch (const std::exception& e) { fail (std::string ("configurazione non valida: ") + e.what()); }
            if (mc == nullptr) fail ("configurazione non valida");
            checkWeightCount (*mc, arch, w.size());
        }

        /** Prova a vuoto: uscita finita, niente picchi esplosivi, calcolo in tempo reale. */
        void audition (nam::DSP& model, ModelInfo& info)
        {
            if (auto* s = dynamic_cast<nam::SlimmableModel*> (&model)) s->SetSlimmableSize (1.0);   // caso peggiore
            const double sr = model.GetExpectedSampleRate() > 0.0 ? model.GetExpectedSampleRate() : 48000.0;
            const int block = 512, total = (int) (sr * 0.5);
            model.Reset (sr, block);                     // Reset() esegue anche il prewarm
            std::vector<NAM_SAMPLE> in ((size_t) block), out ((size_t) block);
            NAM_SAMPLE* ip[1] = { in.data() };
            NAM_SAMPLE* op[1] = { out.data() };
            double peak = 0.0;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0; pos < total; pos += block)
            {
                for (int i = 0; i < block; ++i)
                {
                    const double t = (double) (pos + i) / sr;
                    double v = 0.0;
                    for (int h = 1; h <= 5; ++h) v += std::sin (juce::MathConstants<double>::twoPi * 110.0 * h * t) / h;
                    in[(size_t) i] = (NAM_SAMPLE) (0.25 * std::exp (-3.0 * t) * v);
                }
                model.process (ip, op, block);
                for (auto y : out)
                {
                    if (! std::isfinite ((double) y)) fail ("il modello produce valori non validi (NaN/Inf): file alterato?");
                    peak = std::max (peak, std::abs ((double) y));
                }
            }
            info.realtimeLoad = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / 0.5;
            if (peak > 100.0) fail ("il modello produce livelli esplosivi (oltre +40 dB): file alterato?");
            // rifiuto solo se inutilizzabile anche a computer scarico; sopra l'80% il modello si carica con un avviso
            // (la misura dipende dal carico del momento: una DAW impegnata non deve far scartare un file integro)
            if (info.realtimeLoad > 8.0) fail ("il modello e' troppo pesante per il tempo reale (oltre 8 volte)");
            info.sampleRate = sr;
        }
    }

    bool hasModelExtension (const juce::File& f)
    {
        const auto ext = f.getFileExtension().toLowerCase();
        return ext == ".nam" || ext == ".namb";
    }

    bool plausibleJson (const juce::String& text, size_t maxBytes, int maxDepth)
    {
        const auto utf8 = text.toRawUTF8();
        const size_t n = text.getNumBytesAsUTF8();
        return n <= maxBytes && jsonDepth (utf8, n) <= maxDepth;
    }

    bool isSafeLocalFile (const juce::String& path, juce::int64 maxBytes)
    {
        if (path.isEmpty() || path.length() > 4096 || path.containsChar (0)) return false;
        if (path.startsWith ("\\\\") || path.startsWith ("//") || path.startsWith ("\\??\\")) return false;   // rete / spazi speciali
        if (! juce::File::isAbsolutePath (path)) return false;
        std::error_code ec;
        const std::filesystem::path p (path.toStdString());
        const auto st = std::filesystem::status (p, ec);
        if (ec || ! std::filesystem::is_regular_file (st)) return false;
        const auto size = std::filesystem::file_size (p, ec);
        return ! ec && size > 0 && (juce::int64) size <= maxBytes;
    }

    std::string sha256Of (const juce::File& f)
    {
        // mai leggere un dispositivo o una FIFO: l'impronta resterebbe in lettura per sempre
        if (! isSafeLocalFile (f.getFullPathName(), maxNamBytes)) return {};
        return juce::SHA256 (f).toHexString().toStdString();
    }

    std::unique_ptr<nam::DSP> loadModel (const juce::File& file, ModelInfo& info)
    {
        linkAllArchitectures();
        // tanh approssimata come nel plugin NAM ufficiale (stesso suono, molto meno CPU);
        // va impostata prima di costruire i modelli, una sola volta
        static const bool fastTanh = [] { nam::activations::Activation::enable_fast_tanh(); return true; }();
        juce::ignoreUnused (fastTanh);
        info = {};
        if (! hasModelExtension (file)) fail ("sono ammessi solo file .nam o .namb");
        if (! isSafeLocalFile (file.getFullPathName(), maxNamBytes))
            fail ("percorso non ammesso: serve un file locale regolare (niente percorsi di rete o file speciali)");
        if (! file.existsAsFile()) fail ("il file non esiste o non e' un file regolare");
        if (! hasModelExtension (file)) fail ("sono ammessi solo file .nam o .namb");
        const bool binary = file.getFileExtension().toLowerCase() == ".namb";
        const auto size = file.getSize();
        if (size < (binary ? 80 : 20) || size > (binary ? maxNambBytes : maxNamBytes))
            fail ("dimensione del file non plausibile per un modello NAM");

        juce::MemoryBlock data;
        if (! file.loadFileAsData (data) || (juce::int64) data.getSize() != size) fail ("file non leggibile");
        info.sha256 = juce::SHA256 (data).toHexString().toStdString();
        info.binary = binary;

        std::unique_ptr<nam::DSP> model;
        if (binary)
        {
            const auto* bytes = static_cast<const uint8_t*> (data.getData());
            const uint32_t magic = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
            if (magic != 0x4E414D42u) fail ("non e' un file .namb (firma NAMB assente)");
            try { model = nam::get_dsp_namb (bytes, data.getSize()); }
            catch (const SecurityError&) { throw; }
            catch (const std::exception& e) { fail (std::string ("file .namb non valido: ") + e.what()); }
            info.architecture = "namb";
            info.kind = "NAMB (modello singolo)";
        }
        else
        {
            const auto* text = static_cast<const char*> (data.getData());
            std::size_t n = data.getSize(), start = 0;
            if (n >= 3 && (uint8_t) text[0] == 0xEF && (uint8_t) text[1] == 0xBB && (uint8_t) text[2] == 0xBF) start = 3;   // BOM
            while (start < n && std::isspace ((unsigned char) text[start])) ++start;
            if (start >= n || text[start] != '{') fail ("non e' un file .nam (JSON atteso)");
            if (jsonDepth (text + start, n - start) > maxJsonDepth) fail ("struttura JSON anomala (annidamento eccessivo)");

            json j;
            try { j = json::parse (text + start, text + n); }
            catch (const std::exception& e) { fail (std::string ("JSON non valido: ") + e.what()); }

            std::string topArch;
            validateSpec (j, 0, info.weights, topArch);
            info.architecture = topArch;
            try { model = nam::get_dsp (j); }
            catch (const SecurityError&) { throw; }
            catch (const std::exception& e) { fail (std::string ("il core NAM rifiuta il modello: ") + e.what()); }
            if (topArch == "SlimmableContainer") info.kind = "A2 (Full + Lite)";
            else if (topArch == "LSTM") info.kind = "LSTM";
            else if (topArch == "Linear") info.kind = "Lineare (IR)";
            else info.kind = "WaveNet (A1/A2)";
        }
        if (model == nullptr) fail ("modello vuoto");
        if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
            fail ("il modello deve avere 1 ingresso e 1 uscita");
        info.slimmable = dynamic_cast<nam::SlimmableModel*> (model.get()) != nullptr;
        audition (*model, info);
        return model;
    }
}
