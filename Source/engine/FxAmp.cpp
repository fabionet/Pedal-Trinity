/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Simulazione di cabinet a risposta all'impulso (IR).
    Gli IR incorporati sono generati dal modello fisico dell'altoparlante
    (risonanza meccanica, roll-off, break-up del cono, filtro a pettine
    della posizione del microfono, prime riflessioni) e convoluti con
    juce::dsp::Convolution. L'utente puo' caricare un proprio file IR (.wav).

    Config: cabs=nome:fres:q:lp:hp:bu:bug|...   (bu = frequenza break-up, bug = dB)
    Ruoli: cab (selettore), mic (0 = centro cono, 1 = bordo), dist (distanza), level, mix
*/

#include "FxCommon.h"
#include "NamSecurity.h"
#include "Families.h"
#include "../dsp/Filters.h"

namespace pt::engine
{
    namespace
    {
        struct CabModel { std::string name; float fres = 100, q = 1.3f, lp = 5000, hp = 70, bu = 2500, bug = 4; };

        std::vector<CabModel> parseCabs (const std::string& s)
        {
            std::vector<CabModel> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find ('|', i); if (j == std::string::npos) j = s.size();
                const auto tok = s.substr (i, j - i);
                i = j + 1;
                if (tok.empty()) continue;
                CabModel c;
                char name[64] {};
                std::sscanf (tok.c_str(), "%63[^:]:%f:%f:%f:%f:%f:%f", name, &c.fres, &c.q, &c.lp, &c.hp, &c.bu, &c.bug);
                c.name = name;
                out.push_back (c);
            }
            if (out.empty())
                out = { { "1x12 open", 105, 1.4f, 5200, 75, 2400, 4 }, { "2x12 open", 95, 1.3f, 5000, 70, 2200, 5 },
                        { "4x12 closed", 85, 1.6f, 4600, 60, 2000, 6 }, { "4x10 bass", 70, 1.2f, 4000, 45, 1800, 3 } };
            return out;
        }

        class CabIREffect : public Effect
        {
        public:
            explicit CabIREffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                cabs = parseCabs (cfg.str ("cabs", ""));
                pCab = role (*this, "cab", 0.0f);
                pMic = role (*this, "mic", 0.3f);
                pDist = role (*this, "dist", 0.2f);
                pLevel = role (*this, "level", 0.5f);
                pMix = role (*this, "mix", 1.0f);
            }

            void prepare (double s, int maxBlock) override
            {
                sr = s;
                conv.prepare ({ sr, (juce::uint32) maxBlock, 2 });
                dry.setSize (2, maxBlock);
                loaded = -1;
                prepared = true;
                if (! loadedFile.empty() && ! loadUserIR (juce::File (loadedFile)))
                    loadedFile.clear();
                if (! userIR) loadIfNeeded (true);
            }
            void reset() override { conv.reset(); }

            void messageThreadUpdate() override { if (prepared) loadIfNeeded (false); }
            bool acceptsFiles() const override { return true; }
            bool loadFile (const std::string& path) override
            {
                const juce::File f (path);
                if (! loadUserIR (f)) return false;
                loadedFile = f.getFullPathName().toStdString();
                return true;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                const float mix = pMix.get (*this), lvl = taperA (pLevel.get (*this)) * 2.5f;
                for (int c = 0; c < numCh; ++c) dry.copyFrom (c, 0, ch[c], n);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                conv.process (juce::dsp::ProcessContextReplacing<float> (block));
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < n; ++i)
                        ch[c][i] = lvl * (mix * ch[c][i] + (1.0f - mix) * dry.getSample (c, i));
            }

            /** Caricamento di un IR dell'utente (thread del messaggio). */
            bool loadUserIR (const juce::File& f)
            {
                const auto ext = f.getFileExtension().toLowerCase();
                if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return false;
                // percorso dal preset: niente rete o file speciali, prima di qualsiasi accesso
                if (! pt::namsafe::isSafeLocalFile (f.getFullPathName(), 32 * 1024 * 1024)) return false;
                if (! f.existsAsFile()
                    || f.getSize() < 44 || f.getSize() > 32 * 1024 * 1024)
                    return false;

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
                if (reader == nullptr || ! std::isfinite (reader->sampleRate)
                    || reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0
                    || reader->numChannels < 1 || reader->numChannels > 2
                    || reader->lengthInSamples < 1
                    || reader->lengthInSamples > (juce::int64) (reader->sampleRate * 10.0))
                    return false;

                const int channels = (int) reader->numChannels;
                const int samples = (int) reader->lengthInSamples;
                juce::AudioBuffer<float> impulse (channels, samples);
                if (! reader->read (&impulse, 0, samples, 0, true, channels > 1))
                    return false;

                float peak = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < samples; ++i)
                    {
                        const float value = impulse.getSample (ch, i);
                        if (! std::isfinite (value)) return false;
                        peak = std::max (peak, std::abs (value));
                    }
                if (peak > 64.0f) return false;

                conv.loadImpulseResponse (std::move (impulse), reader->sampleRate,
                                          channels > 1 ? juce::dsp::Convolution::Stereo::yes
                                                       : juce::dsp::Convolution::Stereo::no,
                                          juce::dsp::Convolution::Trim::yes,
                                          juce::dsp::Convolution::Normalise::yes);
                userIR = true;
                return true;
            }

        private:
            /** Rigenera l'IR se cambiano cabinet o microfono (fuori dal ciclo campione per campione). */
            void loadIfNeeded (bool force)
            {
                if (userIR) return;
                const int cab = std::clamp (pCab.step (*this), 0, (int) cabs.size() - 1);
                const int mic = (int) std::lround (pMic.get (*this) * 8), dist = (int) std::lround (pDist.get (*this) * 8);
                const int key = cab * 100 + mic * 10 + dist;
                if (! force && key == loaded) return;
                loaded = key;
                const auto& m = cabs[(size_t) cab];
                const int len = (int) (0.09 * sr);
                juce::AudioBuffer<float> ir (1, len);
                ir.clear();
                pt::dsp::Biquad res, hp, lp1, lp2, bu, off;
                res.peak (sr, m.fres, m.q, 7.0);                       // risonanza dell'altoparlante nel mobile
                hp.highPass (sr, m.hp, 0.9);
                lp1.lowPass (sr, m.lp * (1.0f - 0.45f * mic / 8.0f), 0.707);   // fuori asse = meno alte
                lp2.lowPass (sr, m.lp * 1.3f, 0.6);
                bu.peak (sr, m.bu, 2.2, m.bug);                        // break-up del cono
                off.peak (sr, 3500 + 900 * mic / 8.0, 1.5, -2.0f * mic / 8.0f);
                const int combDelay = (int) ((0.00015 + 0.0009 * dist / 8.0) * sr);   // riflessione del pannello
                juce::Random rng (1234 + key);
                for (int i = 0; i < len; ++i)
                {
                    float x = i == 0 ? 1.0f : 0.0f;
                    if (i == combDelay) x += 0.35f;
                    // prime riflessioni della stanza (poche, basse)
                    if (i > (int) (0.004 * sr) && rng.nextFloat() < 0.002f) x += (rng.nextFloat() - 0.5f) * 0.08f * std::exp (-(float) i / (0.03f * (float) sr));
                    x = hp.process (x); x = res.process (x); x = bu.process (x); x = off.process (x);
                    x = lp1.process (x); x = lp2.process (x);
                    ir.setSample (0, i, x);
                }
                conv.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                                          juce::dsp::Convolution::Normalise::yes);
            }

            Config cfg;
            std::vector<CabModel> cabs;
            double sr = 48000;
            int loaded = -1;
            bool userIR = false, prepared = false;
            RoleParam pCab, pMic, pDist, pLevel, pMix;
            juce::dsp::Convolution conv;
            juce::AudioBuffer<float> dry;
        };
    }

    std::unique_ptr<Effect> makeCabIR (const ModelDef& d) { return std::make_unique<CabIREffect> (d); }
}
