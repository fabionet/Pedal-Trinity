/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "FxNam.h"
#include "NamSecurity.h"
#include "NamWeights.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <NAM/dsp.h>
#include <NAM/slimmable.h>
#include "ResamplingNAM.h"
#include "ToneStack.h"
#include "dsp/NoiseGate.h"
#include "dsp/RecursiveLinearFilter.h"

namespace pt::engine
{
    namespace
    {
        constexpr double dcBlockerHz = 5.0;          // come il plugin NAM (kDCBlockerFrequency)
        constexpr size_t irMaxLength = 8192;          // come AudioDSPTools ImpulseResponse::mMaxLength

        double dbToAmp (double db) { return std::pow (10.0, db / 20.0); }
        float knobDb (float v, float lo, float hi) { return lo + (hi - lo) * v; }
        float peak (const double* x, int n)
        {
            double p = 0.0;
            for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i]));
            return (float) p;
        }
        void pushPeak (std::atomic<float>& m, float v)
        {
            float cur = m.load (std::memory_order_relaxed);
            while (v > cur && ! m.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }
    }

    /** Modello caricato e verificato, con i dati di calibrazione del file. */
    struct NamEffect::ModelSlot
    {
        std::unique_ptr<ResamplingNAM> nam;
        bool hasInputLevel = false, hasOutputLevel = false, hasLoudness = false;
        double inputLevel = 0.0, outputLevel = 0.0, loudness = 0.0;
    };

    /** Stato di un canale usato dal thread audio. */
    struct NamEffect::Channel
    {
        std::unique_ptr<ModelSlot> current;                 // modello attivo (thread audio)
        std::atomic<ModelSlot*> pending { nullptr };        // nuovo modello in arrivo
        std::atomic<ModelSlot*> retired { nullptr };        // modello sostituito da liberare
        dsp::noise_gate::Trigger trigger;
        dsp::noise_gate::Gain gateGain;
        dsp::tone_stack::BasicNamToneStack toneStack;
        recursive_linear_filter::HighPass highPass;
        juce::dsp::Convolution ir;
        std::atomic<bool> irLoaded { false };
        juce::AudioBuffer<float> irBuffer;
        std::vector<double> in, out;
        double gain[4] { 1.0, 1.0, 1.0, 1.0 };               // ingresso, NAM, IR, uscita (rampe)
        float lastTone[3] { -1.0f, -1.0f, -1.0f };

        Channel() { trigger.AddListener (&gateGain); }
    };

    NamEffect::NamEffect (const ModelDef& d) : Effect (d)
    {
        pt::namsafe::linkAllArchitectures();
        for (auto& c : chans) c = std::make_unique<Channel>();
        for (auto& m : meters) m.store (0.0f);
    }

    NamEffect::~NamEffect()
    {
        for (auto& c : chans)
        {
            delete c->pending.exchange (nullptr);
            delete c->retired.exchange (nullptr);
        }
    }

    //==============================================================================
    void NamEffect::prepare (double sr, int block)
    {
        sampleRate = sr;
        maxBlock = juce::jmax (16, block);
        for (auto& cp : chans)
        {
            auto& c = *cp;
            if (auto* p = c.pending.exchange (nullptr))      // modelli caricati prima della preparazione
            {
                delete c.retired.exchange (c.current.release());
                c.current.reset (p);
            }
            if (c.current != nullptr && c.current->nam != nullptr)
                c.current->nam->Reset (sr, maxBlock);
            c.in.assign ((size_t) maxBlock, 0.0);
            c.out.assign ((size_t) maxBlock, 0.0);
            c.toneStack.Reset (sr, maxBlock);
            c.highPass.SetParams (recursive_linear_filter::HighPassParams (sr, dcBlockerHz));
            c.ir.prepare ({ sr, (juce::uint32) maxBlock, 1 });
            c.irBuffer.setSize (1, maxBlock);
            // prima allocazione dei buffer interni (qui, non nel thread audio)
            double* ptr[1] = { c.in.data() };
            c.trigger.SetSampleRate (sr);
            c.trigger.Process (ptr, 1, (size_t) maxBlock);
            c.gateGain.Process (ptr, 1, (size_t) maxBlock);
            c.toneStack.Process (ptr, 1, maxBlock);
            c.highPass.Process (ptr, 1, (size_t) maxBlock);
            c.lastTone[0] = c.lastTone[1] = c.lastTone[2] = -1.0f;
        }
        // i file caricati prima della preparazione dell'audio vanno riletti alla frequenza giusta
        if (! prepared)
            for (int k = 0; k < 2; ++k)
                if (! irPath[k].empty() && ! chans[(size_t) k]->irLoaded.load()) loadIr (k, juce::File (irPath[k]));
        prepared = true;
    }

    void NamEffect::reset()
    {
        for (auto& cp : chans)
        {
            cp->ir.reset();
            std::fill (cp->out.begin(), cp->out.end(), 0.0);
        }
    }

    float NamEffect::readout (int index) const
    {
        if (! juce::isPositiveAndBelow (index, (int) numMeters)) return 0.0f;
        return meters[(size_t) index].exchange (0.0f, std::memory_order_relaxed);
    }

    //==============================================================================
    void NamEffect::runChannel (Channel& c, int k, const float* in, float* out, int n)
    {
        // modello nuovo dal thread dei messaggi (scambio senza lock)
        if (auto* p = c.pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (c.retired.load (std::memory_order_acquire) == nullptr)
            {
                c.retired.store (c.current.release(), std::memory_order_release);
                c.current.reset (p);
            }
            else
                c.pending.store (p, std::memory_order_release);   // si riprova al prossimo blocco
        }

        const ModelSlot* slot = c.current.get();
        const bool hasModel = slot != nullptr && slot->nam != nullptr;
        if (! hasModel && ! c.irLoaded.load (std::memory_order_acquire))
        {
            // canale vuoto (nessun modello ne' IR): il segnale passa inalterato, senza i guadagni fino a +60 dB
            if (out != in) std::copy (in, in + n, out);
            double pk = 0.0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, (double) std::abs (in[i]));
            for (int m = 0; m < 3; ++m) pushPeak (meters[(size_t) (k * 3 + m)], (float) pk);
            return;
        }

        // guadagni come nel plugin NAM (_SetInputGain / _SetOutputGain)
        double inDb = knobDb (p (Input), -20.0f, 20.0f);
        if (hasModel && slot->hasInputLevel && calibrate.load (std::memory_order_relaxed))
            inDb += calLevel.load (std::memory_order_relaxed) - slot->inputLevel;
        double outDb = knobDb (p (Output), -40.0f, 40.0f);
        if (hasModel)
        {
            const int mode = outputMode.load (std::memory_order_relaxed);
            if (mode == 1 && slot->hasLoudness) outDb += -18.0 - slot->loudness;
            else if (mode == 2 && slot->hasOutputLevel) outDb += slot->outputLevel - calLevel.load (std::memory_order_relaxed);
        }
        const double target[4] = { dbToAmp (inDb), dbToAmp (knobDb (p (k == 0 ? NamA : NamB), -30.0f, 12.0f)),
                                    dbToAmp (knobDb (p (k == 0 ? IrA : IrB), -30.0f, 12.0f)), dbToAmp (outDb) };
        auto ramp = [n] (double* x, double& g, double to)
        {
            const double step = (to - g) / (double) std::max (1, n);
            for (int i = 0; i < n; ++i) x[i] *= (g += step);
            g = to;
        };

        double* x = c.in.data();
        for (int i = 0; i < n; ++i) x[i] = (double) in[i];
        ramp (x, c.gain[0], target[0]);
        pushPeak (meters[(size_t) (k * 3)], peak (x, n));

        // noise gate: il trigger legge l'ingresso, il guadagno si applica dopo il modello
        const bool gate = gateOn.load (std::memory_order_relaxed);
        double* xp[1] = { x };
        double** trig = xp;
        if (gate)
        {
            c.trigger.SetParams (dsp::noise_gate::TriggerParams (0.01, gateThreshold.load (std::memory_order_relaxed), 0.1, 0.005, 0.01, 0.05));
            trig = c.trigger.Process (xp, 1, (size_t) n);
        }
        double* y = c.out.data();
        double* yp[1] = { y };
        if (hasModel) slot->nam->process (trig, yp, n);
        else std::copy (trig[0], trig[0] + n, y);                  // senza modello: passa l'ingresso (come il plugin)
        double** stage = gate ? c.gateGain.Process (yp, 1, (size_t) n) : yp;
        if (stage[0] != y) std::copy (stage[0], stage[0] + n, y);
        ramp (y, c.gain[1], target[1]);
        pushPeak (meters[(size_t) (k * 3 + 1)], peak (y, n));

        // tonestack del plugin NAM (valori 0..10, 5 = centro)
        if (eqOn.load (std::memory_order_relaxed))
        {
            const float tone[3] = { p (Bass) * 10.0f, p (Middle) * 10.0f, p (Treble) * 10.0f };
            const char* names[3] = { "bass", "middle", "treble" };
            for (int t = 0; t < 3; ++t)
                if (tone[t] != c.lastTone[t]) { c.toneStack.SetParam (names[t], tone[t]); c.lastTone[t] = tone[t]; }
            double** ts = c.toneStack.Process (yp, 1, n);
            if (ts[0] != y) std::copy (ts[0], ts[0] + n, y);
        }

        // risposta all'impulso (convoluzione a partizioni, IR preparato come nel plugin NAM)
        if (irOn.load (std::memory_order_relaxed) && c.irLoaded.load (std::memory_order_acquire))
        {
            float* f = c.irBuffer.getWritePointer (0);
            for (int i = 0; i < n; ++i) f[i] = (float) y[i];
            juce::dsp::AudioBlock<float> blk (c.irBuffer.getArrayOfWritePointers(), 1, (size_t) n);
            c.ir.process (juce::dsp::ProcessContextReplacing<float> (blk));
            for (int i = 0; i < n; ++i) y[i] = (double) f[i];
        }
        ramp (y, c.gain[2], target[2]);
        pushPeak (meters[(size_t) (k * 3 + 2)], peak (y, n));

        double** hp = c.highPass.Process (yp, 1, (size_t) n);
        ramp (hp[0], c.gain[3], target[3]);
        for (int i = 0; i < n; ++i)
        {
            const double v = hp[0][i];
            out[i] = std::isfinite (v) ? (float) v : 0.0f;
        }
    }

    void NamEffect::process (float* const* ch, int numCh, int n)
    {
        if (! prepared || n <= 0 || n > maxBlock) return;
        const bool onA = step (ChanA) > 0, onB = step (ChanB) > 0;
        auto active = [this] (int k)
        {
            const auto& c = *chans[(size_t) k];
            return (c.current != nullptr && c.current->nam != nullptr) || c.pending.load (std::memory_order_relaxed) != nullptr
                   || c.irLoaded.load (std::memory_order_relaxed);
        };
        if (numCh >= 2)
        {
            // catena stereo: A elabora la linea A, B la linea B; un canale spento lascia passare la sua linea
            if (onA) runChannel (*chans[0], 0, ch[0], ch[0], n);
            if (onB) runChannel (*chans[1], 1, ch[1], ch[1], n);
            return;
        }
        // tratto mono: i canali attivi ricevono lo stesso segnale e si sommano (due ampli miscelati)
        const bool useA = onA && active (0), useB = onB && active (1);
        if (! useA && ! useB)
        {
            if (onA) runChannel (*chans[0], 0, ch[0], ch[0], n);     // canale vuoto: passa inalterato
            return;
        }
        float tmp[4096];
        if (n > 4096) return;
        if (useA && useB)
        {
            runChannel (*chans[1], 1, ch[0], tmp, n);
            runChannel (*chans[0], 0, ch[0], ch[0], n);
            for (int i = 0; i < n; ++i) ch[0][i] += tmp[i];
        }
        else if (useA) runChannel (*chans[0], 0, ch[0], ch[0], n);
        else runChannel (*chans[1], 1, ch[0], ch[0], n);
    }

    //==============================================================================
    void NamEffect::stage (int k, std::unique_ptr<ModelSlot> slot)
    {
        auto& c = *chans[(size_t) k];
        if (slot != nullptr && slot->nam != nullptr && sampleRate > 0)
            slot->nam->Reset (sampleRate, maxBlock);
        latest[(size_t) k] = slot.get();
        if (! prepared)
        {
            // audio non ancora avviato: nessun thread audio da sincronizzare
            delete c.pending.exchange (nullptr);
            c.current = std::move (slot);
            return;
        }
        delete c.pending.exchange (slot.release());          // un eventuale modello non ancora preso va scartato
    }

    void NamEffect::messageThreadUpdate()
    {
        for (auto& c : chans)
            delete c->retired.exchange (nullptr, std::memory_order_acq_rel);
    }

    bool NamEffect::loadNam (int k, const juce::File& file, const std::string& expectedSha)
    {
        k = juce::jlimit (0, 1, k);
        auto& st = state[(size_t) k];
        pt::namsafe::ModelInfo info;
        try
        {
            auto model = pt::namsafe::loadModel (file, info);
            // impronta calcolata sugli stessi byte appena verificati e caricati (nessuna finestra tra controllo e uso)
            if (! expectedSha.empty() && info.sha256 != expectedSha)
                throw pt::namsafe::SecurityError ("il file e' stato modificato dopo il salvataggio del preset (impronta SHA-256 diversa). "
                                                  "Ricaricalo dal pannello zoom se il cambiamento e' voluto.");
            auto slot = std::make_unique<ModelSlot>();
            slot->hasInputLevel = model->HasInputLevel();
            slot->hasOutputLevel = model->HasOutputLevel();
            slot->hasLoudness = model->HasLoudness();
            if (slot->hasInputLevel) slot->inputLevel = model->GetInputLevel();
            if (slot->hasOutputLevel) slot->outputLevel = model->GetOutputLevel();
            if (slot->hasLoudness) slot->loudness = model->GetLoudness();
            slot->nam = std::make_unique<ResamplingNAM> (std::move (model), sampleRate > 0 ? sampleRate : 48000.0);
            if (auto* s = slot->nam->GetSlimmableModel()) s->SetSlimmableSize (slim[k]);
            stage (k, std::move (slot));
            namPath[k] = file.getFullPathName().toStdString();
            namSha[k] = info.sha256;
            st.hasNam = true;
            st.slimmable = info.slimmable;
            st.namFile = file.getFileName().toStdString();
            st.namKind = info.kind;
            st.namLoad = info.realtimeLoad;
            st.warning = false;
            st.message = "Modello verificato: " + info.kind + ", " + std::to_string (info.weights) + " pesi, carico "
                         + std::to_string ((int) std::lround (info.realtimeLoad * 100.0)) + "% del tempo reale."
                         + (info.realtimeLoad > 0.8 ? " Attenzione: carico alto, con buffer piccoli l'audio potrebbe interrompersi." : "");
            return true;
        }
        catch (const std::exception& e)
        {
            st.warning = true;
            st.message = std::string ("File rifiutato: ") + e.what();
            return false;
        }
    }

    void NamEffect::clearNam (int k)
    {
        k = juce::jlimit (0, 1, k);
        stage (k, std::make_unique<ModelSlot>());
        namPath[k].clear(); namSha[k].clear();
        auto& st = state[(size_t) k];
        st.hasNam = false; st.slimmable = false; st.namFile.clear(); st.namKind.clear();
        st.warning = false; st.message = "Nessun modello: il canale lascia passare il segnale.";
    }

    bool NamEffect::loadIr (int k, const juce::File& file)
    {
        k = juce::jlimit (0, 1, k);
        auto& st = state[(size_t) k];
        auto fail = [&st] (const std::string& why) { st.warning = true; st.message = "IR rifiutato: " + why; return false; };
        const auto ext = file.getFileExtension().toLowerCase();
        if (! file.existsAsFile()) return fail ("il file non esiste");
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return fail ("sono ammessi solo file .wav o .aiff");
        if (file.getSize() < 44 || file.getSize() > 32 * 1024 * 1024) return fail ("dimensione non plausibile");

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr) return fail ("formato audio non riconosciuto o file danneggiato");
        const double irRate = reader->sampleRate;
        if (! (irRate >= 8000.0 && irRate <= 384000.0)) return fail ("frequenza di campionamento non plausibile");
        if (reader->numChannels < 1 || reader->numChannels > 8) return fail ("numero di canali non valido");
        if (reader->lengthInSamples < 1 || reader->lengthInSamples > (juce::int64) (irRate * 10.0)) return fail ("durata non plausibile (max 10 s)");

        // come AudioDSPTools: primo canale, al massimo 8192 campioni alla frequenza di lavoro
        const double workRate = sampleRate > 0 ? sampleRate : 48000.0;
        const int keep = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) std::ceil (irMaxLength * irRate / workRate));
        juce::AudioBuffer<float> buf (1, keep);
        reader->read (&buf, 0, keep, 0, true, false);
        const float* s = buf.getReadPointer (0);
        float pk = 0.0f;
        for (int i = 0; i < keep; ++i)
        {
            if (! std::isfinite (s[i])) return fail ("campioni non validi (NaN/Inf)");
            pk = std::max (pk, std::abs (s[i]));
        }
        if (pk > 64.0f) return fail ("livelli non plausibili");
        // guadagno del plugin NAM: -18 dB e compensazione della frequenza di campionamento
        buf.applyGain ((float) (std::pow (10.0, -18.0 * 0.05) * 48000.0 / workRate));
        auto& c = *chans[(size_t) k];
        c.ir.loadImpulseResponse (std::move (buf), irRate, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        c.irLoaded.store (true, std::memory_order_release);
        irPath[k] = file.getFullPathName().toStdString();
        irSha[k] = pt::namsafe::sha256Of (file);
        st.hasIr = true;
        st.irFile = file.getFileName().toStdString();
        st.warning = false;
        st.message = "IR caricato: " + st.irFile;
        return true;
    }

    void NamEffect::clearIr (int k)
    {
        k = juce::jlimit (0, 1, k);
        chans[(size_t) k]->irLoaded.store (false, std::memory_order_release);
        irPath[k].clear(); irSha[k].clear();
        auto& st = state[(size_t) k];
        st.hasIr = false; st.irFile.clear();
    }

    //==============================================================================
    NamEffect::Options NamEffect::options() const
    {
        Options o;
        o.calibrateInput = calibrate.load();
        o.calibrationLevel = calLevel.load();
        o.outputMode = outputMode.load();
        o.noiseGate = gateOn.load();
        o.gateThreshold = gateThreshold.load();
        o.toneStack = eqOn.load();
        o.irEnabled = irOn.load();
        o.slim[0] = slim[0]; o.slim[1] = slim[1];
        return o;
    }

    void NamEffect::setOptions (const Options& o)
    {
        calibrate.store (o.calibrateInput);
        calLevel.store (juce::jlimit (-60.0, 60.0, o.calibrationLevel));
        outputMode.store (juce::jlimit (0, 2, o.outputMode));
        gateOn.store (o.noiseGate);
        gateThreshold.store (juce::jlimit (-100.0, 0.0, o.gateThreshold));
        eqOn.store (o.toneStack);
        irOn.store (o.irEnabled);
        for (int k = 0; k < 2; ++k)
        {
            slim[k] = juce::jlimit (0.0, 1.0, o.slim[k]);
            // modello consegnato piu' di recente: SetSlimmableSize e' sicuro da questo thread (core NAM)
            if (auto* ls = latest[(size_t) k]; ls != nullptr && ls->nam != nullptr)
                if (auto* sm = ls->nam->GetSlimmableModel()) sm->SetSlimmableSize (slim[k]);
        }
    }

    std::string NamEffect::saveState() const
    {
        auto* obj = new juce::DynamicObject();
        juce::var root (obj);
        const char* ids[2] = { "A", "B" };
        for (int k = 0; k < 2; ++k)
        {
            obj->setProperty (juce::String ("nam") + ids[k], juce::String (namPath[k]));
            obj->setProperty (juce::String ("namSha") + ids[k], juce::String (namSha[k]));
            obj->setProperty (juce::String ("ir") + ids[k], juce::String (irPath[k]));
            obj->setProperty (juce::String ("irSha") + ids[k], juce::String (irSha[k]));
            obj->setProperty (juce::String ("slim") + ids[k], slim[k]);
        }
        obj->setProperty ("calibrate", calibrate.load());
        obj->setProperty ("calLevel", calLevel.load());
        obj->setProperty ("outputMode", outputMode.load());
        obj->setProperty ("gate", gateOn.load());
        obj->setProperty ("gateThreshold", gateThreshold.load());
        obj->setProperty ("toneStack", eqOn.load());
        obj->setProperty ("ir", irOn.load());
        return juce::JSON::toString (root, true).toStdString();
    }

    void NamEffect::restoreState (const std::string& text)
    {
        const auto v = juce::JSON::parse (juce::String::fromUTF8 (text.c_str()));
        if (! v.isObject()) return;
        Options o = options();
        o.calibrateInput = (bool) v.getProperty ("calibrate", o.calibrateInput);
        o.calibrationLevel = (double) v.getProperty ("calLevel", o.calibrationLevel);
        o.outputMode = (int) v.getProperty ("outputMode", o.outputMode);
        o.noiseGate = (bool) v.getProperty ("gate", o.noiseGate);
        o.gateThreshold = (double) v.getProperty ("gateThreshold", o.gateThreshold);
        o.toneStack = (bool) v.getProperty ("toneStack", o.toneStack);
        o.irEnabled = (bool) v.getProperty ("ir", o.irEnabled);
        const char* ids[2] = { "A", "B" };
        for (int k = 0; k < 2; ++k) o.slim[k] = (double) v.getProperty (juce::String ("slim") + ids[k], 1.0);
        setOptions (o);

        for (int k = 0; k < 2; ++k)
        {
            auto& st = state[(size_t) k];
            // regola di sicurezza: il file deve essere identico a quello salvato nel preset (SHA-256)
            auto reopen = [&st] (const juce::String& path, const juce::String& sha, const char* what) -> bool
            {
                if (path.isEmpty()) return false;
                const juce::File f (path);
                if (! f.existsAsFile())
                {
                    st.warning = true;
                    st.message = std::string (what) + " non trovato: " + f.getFileName().toStdString();
                    return false;
                }
                if (sha.isNotEmpty() && pt::namsafe::sha256Of (f) != sha.toStdString())
                {
                    st.warning = true;
                    st.message = std::string (what) + " modificato dopo il salvataggio del preset (impronta SHA-256 diversa): "
                                 "non caricato. Ricaricalo dal pannello zoom se il cambiamento e' voluto.";
                    return false;
                }
                return true;
            };
            const auto nam = v.getProperty (juce::String ("nam") + ids[k], {}).toString();
            const auto namSaved = v.getProperty (juce::String ("namSha") + ids[k], {}).toString();
            if (reopen (nam, namSaved, "Il modello"))
                loadNam (k, juce::File (nam), namSaved.toStdString());
            const auto ir = v.getProperty (juce::String ("ir") + ids[k], {}).toString();
            if (reopen (ir, v.getProperty (juce::String ("irSha") + ids[k], {}).toString(), "L'IR"))
            {
                if (prepared) loadIr (k, juce::File (ir));
                else { irPath[k] = ir.toStdString(); irSha[k] = v.getProperty (juce::String ("irSha") + ids[k], {}).toString().toStdString(); }
            }
        }
    }

    std::unique_ptr<Effect> makeNam (const ModelDef& d) { return std::make_unique<NamEffect> (d); }
}
