/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 5 (Behringer, Cuvave/M-VAVE): pedali digitali e preampli con IR ("type=i5...").

    type=i5cab   - simulatore di cabinet IR BOX: banco di risposte all'impulso GENERATE (FxStage5TimeData.h: risonanza
                   dell'altoparlante nel mobile, mobile aperto/chiuso, pettine fra i coni, voce dell'altoparlante, filtro
                   del microfono, prime riflessioni; nessuna IR copiata), LOW CUT / HI CUT, IR dell'utente (.wav).
                   Ruoli: locut level hicut cab.  Config: bank=irbox.
    type=i5cube  - Cube Baby / Cube Baby Bass / Cube Baby AC / MINI-AMP / MINI-EFX (unit=cube|bass|ac|amp|efx):
                   preampli e saturazioni come netlist del motore Circuit (SubCircuit), modulazione bipolare
                   (chorus | phaser o tremolo), delay a nastro, riverbero (VerbCore), banco di cabinet generati.
                   Ruoli: level cab reverb mix feedback time mod tone gain type space comp antifb bass mid treble boost.
    type=i5dly   - delay digitali multimodo (EM600 Echo Machine, DIG Delay, ELEMENTAL/PRO):
                   modes=Nome:tipo:minms:maxms[:suddivisione]|...  tipi: clean analog tape bubble trem reverse sweep mic
                   lofi dual duck dtape ice pattern slap swell multi1 multi2 pingpong tap
                   color=1 (TYPE tape/digi/analog)  elemental=1 (PARAM/MOD del manuale ELEMENTAL)
                   mixlaw=wet|xfade|fixed wet=  mod= param= (riserve)  bw=Hz  fbmax=  stereo=0|1
                   Ruoli: mix feedback time mode mod color param pingpong.
    type=i5verb  - riverberi multimodo (RV600 Reverb Machine, DR400, DIG Reverb, MINI-UNIVERSE, GALAXIA) sul VerbCore e su
                   FDN/molle/piastra propri: modes=Nome:tipo:t60min:t60max|...  timerole=decay|predelay pre=lo,hi (ms)
                   params=universe|galaxia  dual=1 (DR400: TONE/F.BACK e R.TIME/D.TIME)  mixlaw=wet|xfade  bw=Hz
                   Ruoli: mix decay time tone lend mod p1 p2 p3 mode hold.
    type=i5shift - pitch shifter/armonizzatore polifonico (US600 unit=us, DIG Pitch unit=dig): ottave esatte dal banco di
                   filtri complessi, resto granulare. Ruoli: time balance key shift mode wet dry.
    type=i5oct   - octaver polifonico SO400 (POLY con gamma, OCT 2, DRIVE). Ruoli: mode direct oct1 oct2. Config: range=lo,hi.
    type=i5env   - filtri a inviluppo (FM600 unit=fm: BP/TF/LP, UP/DOWN; DW400 unit=dw: UP, DOWN, SYNTH, HUMAN VOICE,
                   TEMPO). Ruoli: sens range peak filter dir decay freq mode.
    type=i5rot   - altoparlante rotante RM600 (modelli 145, L16, 122). Ruoli: fast slow drive balance ramp model speed.
    type=i5fx6   - multi-effetto FX600 (chorus, flanger, phaser, delay, tremolo, pitch). Ruoli: level par1 par2 fx.
    type=i5flm   - flanger FL600 (11 modulazioni, DIGI/ANALOG/TAPE, polarita'). Ruoli: time rate feedback depth mode type pol.

    Tutta la memoria si alloca in prepare(); nessuna allocazione nel thread audio. Ogni anello di retroazione ha
    guadagno <= 1 e una limitazione morbida (loopSat). I cambi di modo passano per una dissolvenza (XFader).
*/

#include "FxStage5.h"
#include "FxStage5TimeData.h"
#include "NamSecurity.h"

// -Wswitch-enum: gli switch sui tipi dei modi hanno un 'default' voluto (come in FxVerbCore.h)
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wswitch-enum")

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        //==============================================================================
        std::vector<std::string> splitStr (const std::string& s, char sep)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find (sep, i);
                if (j == std::string::npos) j = s.size();
                out.push_back (s.substr (i, j - i));
                i = j + 1;
            }
            return out;
        }

        /** Modo "Nome:tipo:lo:hi[:sub]". */
        struct ModeDef { std::string name, kind; float lo = 0, hi = 0, sub = 1; };

        std::vector<ModeDef> parseModes (const std::string& s)
        {
            std::vector<ModeDef> out;
            for (const auto& tok : splitStr (s, '|'))
            {
                if (tok.empty()) continue;
                const auto p = splitStr (tok, ':');
                ModeDef m;
                m.name = p[0];
                m.kind = p.size() > 1 ? p[1] : "clean";
                m.lo = p.size() > 2 ? (float) std::atof (p[2].c_str()) : 0.0f;
                m.hi = p.size() > 3 ? (float) std::atof (p[3].c_str()) : 0.0f;
                m.sub = p.size() > 4 ? (float) std::atof (p[4].c_str()) : 1.0f;
                out.push_back (m);
            }
            if (out.empty()) out.push_back ({ "Delay", "clean", 50, 800, 1 });
            return out;
        }

        /** Leggi di miscela: 0 = solo wet sommato al dry, 1 = dissolvenza dry/wet (centro: entrambi pieni), 2 = wet fisso. */
        inline void mixGains (int law, float m, float fixedWet, float& dry, float& wet) noexcept
        {
            if (law == 1) { dry = std::min (1.0f, 2.0f * (1.0f - m)); wet = std::min (1.0f, 2.0f * m); }
            else if (law == 2) { dry = 1.0f; wet = fixedWet; }
            else { dry = 1.0f; wet = m; }
        }

        /** Diverso bit per bit (confronto esatto voluto: a comandi fermi l'uscita resta identica). */
        inline bool differs (float a, float b) noexcept { return std::fpclassify (a - b) != FP_ZERO; }

        inline int lawOf (const std::string& s) { return s == "xfade" ? 1 : s == "fixed" ? 2 : 0; }

        /** Coefficiente dell'all-pass del primo ordine per la frequenza f. */
        inline float apCoef (float f, float sr) noexcept { const float t = fastTanPi (f, sr); return (t - 1.0f) / (t + 1.0f); }

        //==============================================================================
        /** Biquad in doppia precisione per la generazione delle risposte all'impulso (fuori dal thread audio). */
        struct BqD
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
            void norm (double B0, double B1, double B2, double A0, double A1, double A2)
            { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
            static double w (double sr, double f) { return 2.0 * juce::MathConstants<double>::pi * std::clamp (f, 5.0, 0.46 * sr) / sr; }
            void lp (double sr, double f, double q) { const double c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q); norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
            void hp (double sr, double f, double q) { const double c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q); norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
            void peak (double sr, double f, double q, double db)
            {
                const double A = std::pow (10.0, db / 40.0), c = std::cos (w (sr, f)), al = std::sin (w (sr, f)) / (2 * q);
                norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
            }
            void shelf (double sr, double f, double db, bool high)
            {
                const double A = std::pow (10.0, db / 40.0), c = std::cos (w (sr, f)), s = std::sin (w (sr, f));
                const double al = s / 2 * std::sqrt (2.0), k = 2 * std::sqrt (A) * al;
                if (high) norm (A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                                (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
                else norm (A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                           (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
            }
        };

        /** Genera la risposta all'impulso di un cabinet dalla sua descrizione (deterministica). */
        juce::AudioBuffer<float> generateCabIR (const i5::CabSpec& c, double sr)
        {
            const int len = std::max (64, (int) (c.len * sr));
            std::vector<double> x ((size_t) len, 0.0);
            x[0] = 1.0;
            auto at = [&] (double sec) { return std::clamp ((int) std::lround (sec * sr), 1, len - 1); };
            // pettine fra i coni: percorsi diversi verso il microfono (0,06-0,4 ms)
            for (int k = 1; k < c.nspk; ++k) x[(size_t) at (0.00006 + 0.00009 * k)] += 0.6 / (1.0 + 0.3 * k);
            // mobile aperto: onda posteriore riflessa dalla parete, in opposizione di fase e passa-basso (buco nei bassi)
            if (c.open)
            {
                std::vector<double> back ((size_t) len, 0.0);
                back[(size_t) at (0.0016)] = -0.35;
                double s = 0;
                const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * 450.0 / sr);
                for (int i = 0; i < len; ++i) { s = back[(size_t) i] + a * (s - back[(size_t) i]); x[(size_t) i] += s * (1.0 - a) * 6.0; }
            }
            // prime riflessioni (pavimento, pareti): poche e basse, piu' forti col microfono lontano
            uint32_t seed = 2166136261u;
            for (const char* p = c.name; *p != 0; ++p) { seed ^= (uint8_t) *p; seed *= 16777619u; }
            auto rnd = [&seed] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (double) (seed & 0xffffff) / 16777216.0; };
            for (int r = 0; r < 6; ++r)
            {
                const double t = 0.0025 + 0.0016 * r + 0.006 * c.dist + 0.0004 * rnd();
                if (t < c.len * 0.8) x[(size_t) at (t)] += (0.04 + 0.14 * c.dist) * std::pow (0.72, r) * ((r & 1) ? -1.0 : 1.0);
            }
            // catena dei filtri: sistema, risonanza, voce, cono, increspature del break-up, microfono
            std::vector<BqD> f;
            auto add = [&f] () -> BqD& { f.emplace_back(); return f.back(); };
            add().hp (sr, c.hp, 0.707);
            add().peak (sr, c.fres, c.qres, c.gres);
            if (c.open) add().shelf (sr, 160.0, -2.5, false);
            for (int k = 0; k < 3; ++k)
                if (c.v[3 * k] > 0) add().peak (sr, c.v[3 * k], c.v[3 * k + 1], c.v[3 * k + 2]);
            add().lp (sr, c.lp, 0.8);
            add().lp (sr, c.lp * 1.18, 0.6);
            for (int k = 0; k < 3; ++k) add().peak (sr, 1800.0 + 3700.0 * rnd(), 3.0 + 3.0 * rnd(), (rnd() - 0.5) * 5.0);
            switch (c.mic)
            {
                case i5::Sm57:      add().peak (sr, 5600, 1.3, 5); add().peak (sr, 2500, 1.5, 1.5); add().shelf (sr, 10000, -8, true); add().shelf (sr, 120, -2, false); break;
                case i5::Md421:     add().peak (sr, 4500, 1.0, 4); add().shelf (sr, 150, 2, false); add().shelf (sr, 10000, -6, true); break;
                case i5::LargeCond: add().peak (sr, 11000, 0.8, 2); add().shelf (sr, 120, 2.5, false); break;
                case i5::Md441:     add().peak (sr, 7000, 0.9, 3); add().shelf (sr, 12000, -3, true); break;
                case i5::D112:      add().peak (sr, 90, 1.2, 6); add().peak (sr, 450, 1.0, -5); add().peak (sr, 3800, 1.5, 5); add().shelf (sr, 9000, -8, true); break;
                case i5::SmallCond: add().peak (sr, 9000, 1.0, 3); add().shelf (sr, 150, -2, false); break;
                case i5::Piezo:     add().peak (sr, 1200, 1.0, -3); add().peak (sr, 8000, 1.0, -3); break;
                case i5::Sm57Off:   add().peak (sr, 5000, 1.2, 2); add().shelf (sr, 5000, -6, true); break;
                case i5::Md441Edge: add().peak (sr, 6000, 0.9, 1); add().shelf (sr, 4000, -6, true); break;
                default:            add().shelf (sr, 6000, -8, true); add().shelf (sr, 150, 2, false); break;
            }
            std::vector<double> y ((size_t) len);
            for (int i = 0; i < len; ++i)
            {
                double v = x[(size_t) i];
                for (auto& q : f) v = q.tick (v);
                y[(size_t) i] = v;
            }
            // coda: dissolvenza dell'ultimo 25% (niente troncamento netto)
            const int tail = len / 4;
            for (int i = len - tail; i < len; ++i)
                y[(size_t) i] *= 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (double) (i - (len - tail)) / (double) tail);
            // normalizzazione: potenza media 150 Hz - 3 kHz = 1 (stessa sonorita' percepita fra i cabinet)
            double pw = 0;
            const int nf = 24;
            for (int k = 0; k < nf; ++k)
            {
                const double fr = 150.0 * std::pow (20.0, (double) k / (nf - 1)), wv = 2.0 * juce::MathConstants<double>::pi * fr / sr;
                double re = 0, im = 0;
                for (int i = 0; i < len; ++i) { re += y[(size_t) i] * std::cos (wv * i); im -= y[(size_t) i] * std::sin (wv * i); }
                pw += re * re + im * im;
            }
            const double g = 1.0 / std::sqrt (std::max (1e-12, pw / nf));
            juce::AudioBuffer<float> ir (1, len);
            for (int i = 0; i < len; ++i) ir.setSample (0, i, (float) (y[(size_t) i] * g));
            return ir;
        }

        /** Caricamento di un IR dell'utente (stessi controlli dell'IR loader a catalogo). */
        bool readUserIR (const juce::File& f, juce::AudioBuffer<float>& impulse, double& rate)
        {
            const auto ext = f.getFileExtension().toLowerCase();
            if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return false;
            if (! pt::namsafe::isSafeLocalFile (f.getFullPathName(), 32 * 1024 * 1024)) return false;
            if (! f.existsAsFile() || f.getSize() < 44 || f.getSize() > 32 * 1024 * 1024) return false;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
            if (reader == nullptr || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0
                || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples < 1
                || reader->lengthInSamples > (juce::int64) (reader->sampleRate * 10.0))
                return false;
            const int samples = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> tmp ((int) reader->numChannels, samples);
            if (! reader->read (&tmp, 0, samples, 0, true, reader->numChannels > 1)) return false;
            impulse.setSize (1, samples);
            float peak = 0;
            for (int i = 0; i < samples; ++i)
            {
                float v = tmp.getSample (0, i);
                if (tmp.getNumChannels() > 1) v = 0.5f * (v + tmp.getSample (1, i));
                if (! std::isfinite (v)) return false;
                peak = std::max (peak, std::abs (v));
                impulse.setSample (0, i, v);
            }
            if (peak > 64.0f) return false;
            rate = reader->sampleRate;
            return true;
        }

        //==============================================================================
        /** Banco di cabinet: una convoluzione per risposta (tutte caricate prima di prepare(): uscita deterministica),
            selezione con dissolvenza a potenza costante di 30 ms; -1 = simulazione spenta. Mono. */
        class CabBank
        {
        public:
            void setSpecs (const i5::CabSpec* s, int count) { specs = s; n = count; }

            void prepare (double sr, int maxBlock, const std::string& userFile)
            {
                block = std::max (16, maxBlock);
                convs.clear();
                for (int k = 0; k <= n; ++k)
                    convs.push_back (std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::Latency { 0 }, queue));
                for (int k = 0; k < n; ++k)
                    convs[(size_t) k]->loadImpulseResponse (generateCabIR (specs[k], sr), sr, juce::dsp::Convolution::Stereo::no,
                                                            juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
                // ultima: IR dell'utente (impulso unitario finche' non se ne carica uno)
                userOn.store (false);
                juce::AudioBuffer<float> imp; double rate = sr;
                if (! userFile.empty() && readUserIR (juce::File (userFile), imp, rate))
                {
                    convs[(size_t) n]->loadImpulseResponse (std::move (imp), rate, juce::dsp::Convolution::Stereo::no,
                                                            juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
                    userOn.store (true);
                }
                else
                {
                    juce::AudioBuffer<float> unit (1, 1); unit.setSample (0, 0, 1.0f);
                    convs[(size_t) n]->loadImpulseResponse (std::move (unit), sr, juce::dsp::Convolution::Stereo::no,
                                                            juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
                }
                for (auto& c : convs) c->prepare ({ sr, (juce::uint32) block, 1 });
                tmp.assign ((size_t) block, 0.0f);
                fadeLen = std::max (1, (int) (0.03 * sr));
                cur = prev = -2; fadePos = fadeLen;
            }
            void reset() { for (auto& c : convs) c->reset(); cur = prev = -2; fadePos = fadeLen; }

            /** IR dell'utente (thread dei messaggi): sostituisce i cabinet generati finche' resta caricata. */
            bool loadUser (const std::string& path, double sr)
            {
                juce::AudioBuffer<float> imp; double rate = sr;
                if (convs.empty() || ! readUserIR (juce::File (path), imp, rate)) return false;
                convs[(size_t) n]->loadImpulseResponse (std::move (imp), rate, juce::dsp::Convolution::Stereo::no,
                                                        juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
                userOn.store (true);
                return true;
            }

            /** n <= block campioni in place; sel = -1 spento, 0..n-1 cabinet. */
            void process (float* x, int len, int sel) noexcept
            {
                if (convs.empty()) return;
                int want = sel < 0 ? -1 : (userOn.load (std::memory_order_relaxed) ? n : std::clamp (sel, 0, n - 1));
                if (cur == -2) { cur = want; prev = want; fadePos = fadeLen; if (cur >= 0) convs[(size_t) cur]->reset(); }
                if (want != cur && fadePos >= fadeLen)
                {
                    prev = cur; cur = want; fadePos = 0;
                    if (cur >= 0) convs[(size_t) cur]->reset();
                }
                if (fadePos >= fadeLen) { run (cur, x, len); return; }
                std::copy (x, x + len, tmp.data());
                run (prev, tmp.data(), len);
                run (cur, x, len);
                for (int i = 0; i < len; ++i)
                {
                    const float t = std::min (1.0f, (float) (fadePos + i) / (float) fadeLen);
                    const float gNew = std::sin (1.5707963f * t), gOld = std::cos (1.5707963f * t);
                    x[i] = gNew * x[i] + gOld * tmp[(size_t) i];
                }
                fadePos += len;
            }
        private:
            void run (int k, float* x, int len) noexcept
            {
                if (k < 0) return;
                float* chs[1] = { x };
                juce::dsp::AudioBlock<float> blk (chs, 1, (size_t) len);
                convs[(size_t) k]->process (juce::dsp::ProcessContextReplacing<float> (blk));
            }
            const i5::CabSpec* specs = nullptr;
            int n = 0, block = 512, cur = -2, prev = -2, fadeLen = 1440, fadePos = 0;
            std::atomic<bool> userOn { false };
            juce::dsp::ConvolutionMessageQueue queue;
            std::vector<std::unique_ptr<juce::dsp::Convolution>> convs;
            std::vector<float> tmp;
        };

        //==============================================================================
        /** IR BOX (M-VAVE SK14): LOW CUT -> cabinet -> HI CUT -> VOL. */
        class I5Cab final : public Effect
        {
        public:
            explicit I5Cab (const ModelDef& d) : Effect (d)
            {
                bank.setSpecs (i5::kIrBoxCabs, 32);
                pLo = role (*this, "locut", 0.0f); pHi = role (*this, "hicut", 0.0f); pLevel = role (*this, "level", 0.5f);
                pCab = role (*this, "cab", 0.0f);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; blk = std::max (16, maxBlock);
                bank.prepare (s, blk, loadedFile);
                work.assign ((size_t) blk, 0.0f);
                loS.set (s, 20); hiS.set (s, 20); vS.set (s, 20);
                reset();
            }
            void reset() override { bank.reset(); hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset(); first = true; }
            bool acceptsFiles() const override { return true; }
            bool loadFile (const std::string& path) override
            {
                if (! bank.loadUser (path, sr)) return false;
                loadedFile = juce::File (path).getFullPathName().toStdString();
                return true;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float loT = logMap (pLo.get (*this), 30.0f, 360.0f), hiT = logMap (pHi.get (*this), 18000.0f, 1000.0f);
                const float vT = volKnob (pLevel.get (*this), 0.5f, 2.5f);
                if (first) { loS.snap (loT); hiS.snap (hiT); vS.snap (vT); first = false; }
                for (int off = 0; off < n; off += blk)
                {
                    const int m = std::min (blk, n - off);
                    for (int i = 0; i < m; ++i)
                    {
                        if ((i & 7) == 0)
                        {
                            const float lo = loS.next (loT), hi = hiS.next (hiT);
                            hp1.set (sr, lo, 0.5412); hp2.set (sr, lo, 1.3066); lp1.set (sr, hi, 0.5412); lp2.set (sr, hi, 1.3066);
                        }
                        work[(size_t) i] = hp2.hp (hp1.hp (monoIn (ch, numCh, off + i)));
                    }
                    bank.process (work.data(), m, pCab.step (*this));
                    for (int i = 0; i < m; ++i)
                    {
                        const float y = vS.next (vT) * lp2.lp (lp1.lp (work[(size_t) i]));
                        for (int c = 0; c < numCh; ++c) ch[c][off + i] = y;
                    }
                }
            }
        private:
            CabBank bank;
            float sr = 48000;
            int blk = 512;
            bool first = true;
            RoleParam pLo, pHi, pLevel, pCab;
            Svf hp1, hp2, lp1, lp2;
            Smooth loS, hiS, vS;
            std::vector<float> work;
        };

        //==============================================================================
        /** Chorus leggero (ritardo modulato). */
        struct ChorusLite
        {
            DelayLine d;
            Lfo l;
            float sr = 48000;
            void prepare (double s) { sr = (float) s; d.allocate ((int) (0.04 * s)); }
            void clear() { d.clear(); l.ph = 0; }
            inline float tick (float x, float hz, float baseMs, float depthMs, double phase = 0.0) noexcept
            {
                d.push (x);
                l.setHz (sr, hz);
                const float ms = baseMs + depthMs * l.sine (phase);
                l.step();
                return d.read (std::max (1.0f, ms * 0.001f * sr));
            }
        };

        /** Phaser a 6 all-pass con retroazione. */
        struct PhaserLite
        {
            Allpass1 ap[6];
            Lfo l;
            float sr = 48000, fb = 0;
            void prepare (double s) { sr = (float) s; clear(); }
            void clear() { for (auto& a : ap) a.reset(); fb = 0; l.ph = 0; }
            inline float tick (float x, float hz, float depth, float fbK, float fLo = 200.0f, float fHi = 2500.0f, int stages = 6) noexcept
            {
                l.setHz (sr, hz);
                const float pos = 0.5f + 0.5f * depth * l.tri();
                l.step();
                const float a = apCoef (fLo * std::pow (fHi / fLo, pos), sr);
                float v = x + loopSat (fbK * fb);
                for (int k = 0; k < stages; ++k) v = ap[k].tick (v, a);
                fb = v;
                return v;
            }
        };

        /** Compressore a inviluppo per i multi tascabili (amount 0 = escluso). */
        struct CompLite
        {
            float env = 0, a = 0, r = 0;
            Smooth g;
            void prepare (double sr) { a = (float) std::exp (-1.0 / (0.004 * sr)); r = (float) std::exp (-1.0 / (0.15 * sr)); g.set (sr, 3); env = 0; }
            inline float tick (float x, float amount) noexcept
            {
                const float v = std::abs (x);
                env = v > env ? v + a * (env - v) : v + r * (env - v);
                if (amount <= 0.001f) return x * g.next (1.0f);
                const float thr = 0.25f * std::pow (10.0f, -30.0f * amount / 20.0f), ratio = 1.0f + 7.0f * amount;
                float gg = env > thr ? std::pow (env / thr, 1.0f / ratio - 1.0f) : 1.0f;
                gg *= 1.0f + 1.6f * amount;                                         // compensazione
                return x * g.next (gg);
            }
        };

        /** Delay a nastro semplice (Cube Baby): ritardo lisciato, passa-basso e saturazione nell'anello, wow leggero. */
        struct TapeLite
        {
            DelayLine line;
            Smooth2 tS;
            OnePole lp, hp;
            Lfo wow;
            float sr = 48000, fbv = 0;
            void prepare (double s, double maxMs)
            {
                sr = (float) s; line.allocate ((int) (s * maxMs * 0.001 * 1.05) + 64);
                tS.set (s, 180); lp.set (s, 4200); hp.set (s, 90); wow.setHz (s, 0.7);
                clear();
            }
            void clear() { line.clear(); fbv = 0; lp.reset(); hp.reset(); tS.unset(); }
            inline float tick (float x, float ms, float fb) noexcept
            {
                const float d = tS.next (ms * 0.001f * sr) * (1.0f + 0.0012f * wow.sine());
                wow.step();
                const float y = line.read (std::max (2.0f, d));
                fbv = hp.hp (lp.lp (y));
                line.push (x + loopSat (fb * fbv));
                return y;
            }
        };

        //==============================================================================
        /** Cube Baby e famiglia (unit=cube|bass|ac|amp|efx). */
        class I5Cube final : public Effect
        {
        public:
            enum Unit { Cube, Bass, Ac, Amp, Efx };
            explicit I5Cube (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto u = cfg.str ("unit", "cube");
                unit = u == "bass" ? Bass : u == "ac" ? Ac : u == "amp" ? Amp : u == "efx" ? Efx : Cube;
                pLevel = role (*this, "level", 0.6f); pCab = role (*this, "cab", 0.0f); pRev = role (*this, "reverb", 0.0f);
                pMix = role (*this, "mix", 0.0f); pFb = role (*this, "feedback", 0.35f); pTime = role (*this, "time", 0.4f);
                pMod = role (*this, "mod", 0.5f); pTone = role (*this, "tone", 0.5f); pGain = role (*this, "gain", 0.5f);
                pType = role (*this, "type", 0.0f); pSpace = role (*this, "space", 0.5f); pComp = role (*this, "comp", 0.0f);
                pAnti = role (*this, "antifb", 0.0f); pBass = role (*this, "bass", 0.5f); pMid = role (*this, "mid", 0.5f);
                pTreb = role (*this, "treble", 0.5f); pBoost = role (*this, "boost", 0.0f);
                if (unit == Cube || unit == Amp)
                {
                    const auto* tab = unit == Cube ? i5::kCubeAmps : i5::kMiniAmps;
                    for (int k = 0; k < 9; ++k) nets[(size_t) k] = i5::ampNetlist (tab[k]);
                }
                else if (unit == Efx)
                    for (int k = 0; k < 9; ++k) nets[(size_t) k] = i5::kEfxNets[k];
                if (hasPre())
                    for (int k = 0; k < 9; ++k) pre[(size_t) k].init (nets[(size_t) k].c_str(), { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f });
                if (unit == Cube) bank.setSpecs (i5::kCubeCabs, 8);
                else if (unit == Bass) bank.setSpecs (i5::kBassCabs, 8);
                else if (unit == Ac) bank.setSpecs (i5::kAcCabs, 8);
            }
            bool hasPre() const { return unit == Cube || unit == Amp || unit == Efx; }
            bool hasCab() const { return unit == Cube || unit == Bass || unit == Ac; }

            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; blk = std::max (16, maxBlock);
                if (hasPre()) for (auto& p : pre) p.prepare (s, blk);
                if (hasCab()) bank.prepare (s, blk, loadedFile);
                work.assign ((size_t) blk, 0.0f); work2.assign ((size_t) blk, 0.0f);
                chorus.prepare (s); phaser.prepare (s); tape.prepare (s, 1050); verb.prepare (s); comp.prepare (s);
                verbInit = verb;
                trem.setHz (s, 4);
                for (auto* sm : { &lvS, &revS, &mixS, &fbS, &modS, &spS, &antS, &boS, &cpS }) sm->set (s, 25);
                fadeLen = std::max (1, (int) (0.02 * s));
                reset();
            }
            void reset() override
            {
                eqLo.prepare (sr, SmoothEq::LowShelf, unit == Ac ? 100 : 80, 0.7);
                eqMid.prepare (sr, SmoothEq::Peak, unit == Ac ? 700 : 500, 0.8);
                eqHi.prepare (sr, SmoothEq::HighShelf, unit == Ac ? 4000 : 3000, 0.7);
                static const float nf[4] = { 115, 165, 230, 330 };
                for (int k = 0; k < 4; ++k) notch[k].prepare (sr, SmoothEq::Peak, nf[k], 6.0);
                comp.prepare (sr); comp.g.init = false; tape.wow.ph = 0;
                if (hasPre()) for (auto& p : pre) p.reset();
                if (hasCab()) bank.reset();
                chorus.clear(); phaser.clear(); tape.clear(); verb = verbInit; trem.ph = 0;
                eqLo.reset(); eqMid.reset(); eqHi.reset(); for (auto& q : notch) q.reset();
                curType = -1; prevType = -1; fadePos = fadeLen; first = true;
                verbKey = -1;
            }
            bool acceptsFiles() const override { return hasCab(); }
            bool loadFile (const std::string& path) override
            {
                if (! hasCab() || ! bank.loadUser (path, sr)) return false;
                loadedFile = juce::File (path).getFullPathName().toStdString();
                return true;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                for (int off = 0; off < n; off += blk)
                {
                    const int m = std::min (blk, n - off);
                    for (int i = 0; i < m; ++i) work[(size_t) i] = monoIn (ch, numCh, off + i);
                    runBlock (m);
                    for (int i = 0; i < m; ++i)
                        for (int c = 0; c < numCh; ++c) ch[c][off + i] = work[(size_t) i];
                }
            }

        private:
            /** Trim d'uscita delle netlist (tarati col banco: stessa sonorita' a GAIN e EQ a meta' corsa). */
            float trimOf (int k) const noexcept
            {
                // dB: puliti +8 dB, saturati +11 dB sull'RMS della frase di chitarra del banco (boost +6 dB)
                static const float cube[9] = { -3.7f, -6.3f, -2.4f, 0.1f, -7.7f, -4.6f, -8.3f, -5.5f, -8.2f };
                static const float amp[9]  = { -3.3f, -5.4f, -7.5f, -4.5f, -9.9f, -9.8f, -10.1f, -9.9f, -10.4f };
                static const float efx[9]  = { -2.0f, 1.5f, -6.3f, -6.6f, -3.9f, -0.8f, -9.8f, -6.9f, -3.7f };
                return dbToGain (unit == Cube ? cube[k] : unit == Amp ? amp[k] : efx[k]);
            }

            void runPre (int m)
            {
                const int want = std::clamp (pType.step (*this), 0, 8);
                if (curType < 0) { curType = want; prevType = want; fadePos = fadeLen; }
                if (want != curType && fadePos >= fadeLen)
                {
                    prevType = curType; curType = want; fadePos = 0;
                    pre[(size_t) curType].reset();
                }
                const float g = pGain.get (*this);
                float bass = pBass.get (*this), mid = pMid.get (*this), treb = pTreb.get (*this), bright = 0.5f;
                if (unit == Cube) { const float t = pTone.get (*this); treb = 0.25f + 0.5f * t; bright = t; bass = 0.5f; mid = 0.5f; }
                auto setAll = [&] (SubCircuit& sc) { sc.set (0, g); sc.set (1, bass); sc.set (2, mid); sc.set (3, treb); sc.set (4, bright); };
                setAll (pre[(size_t) curType]);
                const float tc = trimOf (curType);
                if (fadePos >= fadeLen)
                {
                    pre[(size_t) curType].process (work.data(), m);
                    for (int i = 0; i < m; ++i) work[(size_t) i] *= tc;
                    return;
                }
                setAll (pre[(size_t) prevType]);
                std::copy (work.begin(), work.begin() + m, work2.begin());
                pre[(size_t) prevType].process (work2.data(), m);
                pre[(size_t) curType].process (work.data(), m);
                const float tp = trimOf (prevType);
                for (int i = 0; i < m; ++i)
                {
                    const float t = std::min (1.0f, (float) (fadePos + i) / (float) fadeLen);
                    const float gn = std::sin (1.5707963f * t), go = std::cos (1.5707963f * t);
                    work[(size_t) i] = gn * tc * work[(size_t) i] + go * tp * work2[(size_t) i];
                }
                fadePos += m;
            }

            void runBlock (int m)
            {
                if (first)
                {
                    lvS.snap (pLevel.get (*this)); revS.snap (pRev.get (*this)); mixS.snap (pMix.get (*this)); fbS.snap (pFb.get (*this));
                    modS.snap (pMod.get (*this)); spS.snap (pSpace.get (*this)); antS.snap (pAnti.get (*this)); boS.snap (pBoost.get (*this));
                    cpS.snap (pComp.get (*this));
                    first = false;
                }
                if (hasPre()) runPre (m);
                const float lvT = pLevel.get (*this);
                if (unit == Amp || unit == Efx)
                {
                    for (int i = 0; i < m; ++i) work[(size_t) i] = outRail (work[(size_t) i] * volKnob (lvS.next (lvT), 0.5f, 4.0f));
                    return;
                }
                const float revT = pRev.get (*this), mixT = pMix.get (*this), fbT = pFb.get (*this), modT = pMod.get (*this);
                const float spT = pSpace.get (*this), antT = pAnti.get (*this), boT = pBoost.get (*this), cpT = pComp.get (*this);
                const float timeK = pTime.get (*this);
                const float dbB = 24.0f * (pBass.get (*this) - 0.5f), dbM = 24.0f * (pMid.get (*this) - 0.5f), dbT = 24.0f * (pTreb.get (*this) - 0.5f);
                // riverbero: Cube = hall fissa; Bass/AC = SPACE a sinistra col tempo da TIME
                const float t60 = unit == Cube ? 2.2f : logMap (timeK, 0.5f, 5.0f);
                const int vk = (int) std::lround (t60 * 50.0f);
                if (vk != verbKey) { verb.set (VerbCore::Hall, t60, 6500.0f, 0.012f, 0.0f, 0.5f, 0.5f, 0); verbKey = vk; }
                const float dlyMs = unit == Cube ? logMap (timeK, 50.0f, 1000.0f) : logMap (timeK, 60.0f, 1000.0f);
                for (int i = 0; i < m; ++i)
                {
                    float x = work[(size_t) i];
                    if (unit != Cube)
                    {
                        if (unit == Bass)
                        {
                            const float b = boS.next (boT);
                            const float gb = dbToGain (15.0f * b);
                            x = b > 0.001f ? 0.6f * std::tanh (gb * x / 0.6f) : x;
                        }
                        x = eqHi.tick (eqMid.tick (eqLo.tick (x, dbB), dbM), dbT);
                        x = comp.tick (x, cpS.next (cpT));
                        if (unit == Ac)
                        {
                            const float a = antS.next (antT);
                            for (auto& q : notch) x = q.tick (x, -18.0f * a);
                        }
                    }
                    // MOD bipolare: sinistra chorus, centro spento, destra phaser (AC: tremolo)
                    const float md = modS.next (modT);
                    if (md < 0.44f)
                    {
                        const float a = (0.44f - md) / 0.44f;
                        const float w = chorus.tick (x, 0.3f + 2.2f * a, 7.0f, 1.0f + 3.0f * a);
                        const float k = std::min (1.0f, 2.0f * a);
                        x = x * (1.0f - 0.35f * k) + 0.7f * k * w;
                    }
                    else
                    {
                        chorus.tick (x, 1.0f, 7.0f, 1.0f);
                        if (md > 0.56f)
                        {
                            const float a = (md - 0.56f) / 0.44f;
                            if (unit == Ac)
                            {
                                trem.setHz (sr, 2.0f + 7.0f * a);
                                x *= 1.0f - 0.8f * a * (0.5f + 0.5f * trem.sine());
                                trem.step();
                            }
                            else
                            {
                                const float p = phaser.tick (x, 0.2f + 2.5f * a, 1.0f, 0.35f + 0.3f * a);
                                const float k = std::min (1.0f, 2.0f * a);
                                x = x + k * (0.7f * (x + p) - x);
                            }
                        }
                        else phaser.tick (x, 0.5f, 1.0f, 0.4f);
                    }
                    // delay e riverbero
                    float wetD = 0, wetR = 0;
                    if (unit == Cube)
                    {
                        const float mx = mixS.next (mixT);
                        const float y = tape.tick (x, dlyMs, 0.95f * fbS.next (fbT));
                        wetD = mx * y;
                        float l, r; verb.tick (x, l, r);
                        const float rv = revS.next (revT);
                        x = (1.0f - 0.5f * rv) * (x + wetD) + rv * 0.5f * (l + r);
                    }
                    else
                    {
                        const float sp = spS.next (spT);
                        const float aR = sp < 0.44f ? (0.44f - sp) / 0.44f : 0.0f, aD = sp > 0.56f ? (sp - 0.56f) / 0.44f : 0.0f;
                        const float y = tape.tick (x, dlyMs, 0.35f);
                        float l, r; verb.tick (x, l, r);
                        wetR = 0.5f * (l + r);
                        x = x + aD * y + aR * wetR;
                    }
                    work[(size_t) i] = x;
                }
                if (hasCab()) bank.process (work.data(), m, pCab.step (*this) - 1);
                for (int i = 0; i < m; ++i) work[(size_t) i] = outRail (work[(size_t) i] * volKnob (lvS.next (lvT), 0.6f, 3.0f));
            }

            Config cfg;
            Unit unit = Cube;
            float sr = 48000;
            int blk = 512, curType = -1, prevType = -1, fadeLen = 960, fadePos = 0, verbKey = -1;
            bool first = true;
            std::array<std::string, 9> nets;
            std::array<SubCircuit, 9> pre;
            CabBank bank;
            ChorusLite chorus;
            PhaserLite phaser;
            TapeLite tape;
            VerbCore verb, verbInit;
            CompLite comp;
            Lfo trem;
            SmoothEq eqLo, eqMid, eqHi, notch[4];
            Smooth lvS, revS, mixS, fbS, modS, spS, antS, boS, cpS;
            RoleParam pLevel, pCab, pRev, pMix, pFb, pTime, pMod, pTone, pGain, pType, pSpace, pComp, pAnti, pBass, pMid, pTreb, pBoost;
            std::vector<float> work, work2;
        };

        //==============================================================================
        /** Delay digitali multimodo (EM600, DIG Delay, ELEMENTAL/PRO). */
        class I5Delay final : public Effect
        {
        public:
            enum Kind { Clean, Analog, Tape, Bubble, Trem, Reverse, Sweep, Mic, Lofi, Dual, Duck, DTape, Ice, Pattern, Slap, Swell,
                        Multi1, Multi2, PingPong, Tap };
            explicit I5Delay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                modes = parseModes (cfg.str ("modes", ""));
                static const char* names[] = { "clean", "analog", "tape", "bubble", "trem", "reverse", "sweep", "mic", "lofi", "dual", "duck",
                                               "dtape", "ice", "pattern", "slap", "swell", "multi1", "multi2", "pingpong", "tap" };
                for (auto& m : modes)
                {
                    int k = 0;
                    for (int j = 0; j < 20; ++j) if (m.kind == names[j]) k = j;
                    kinds.push_back ((Kind) k);
                }
                color = cfg.num ("color", 0) > 0.5; elemental = cfg.num ("elemental", 0) > 0.5;
                law = lawOf (cfg.str ("mixlaw", "wet")); fixedWet = (float) cfg.num ("wet", 0.6);
                bw = (float) cfg.num ("bw", 12000); fbMax = (float) std::min (1.0, cfg.num ("fbmax", 0.98));
                stereoIn = cfg.num ("stereo", 0) > 0.5;
                pMix = role (*this, "mix", 0.4f); pFb = role (*this, "feedback", 0.35f); pTime = role (*this, "time", 0.5f);
                pMode = role (*this, "mode", 0.0f); pMod = role (*this, "mod", (float) cfg.num ("mod", 0.3));
                pColor = role (*this, "color", 1.0f); pParam = role (*this, "param", (float) cfg.num ("param", 0.4));
                pPing = role (*this, "pingpong", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                float maxMs = 100;
                for (auto& m : modes) maxMs = std::max (maxMs, m.hi * std::max (1.0f, m.sub));
                for (auto& l : line) l.allocate ((int) (s * maxMs * 0.001 * 2.1) + 512);
                for (auto& g : ice) g.prepare (s, 40);
                for (auto& t : det) t.prepare (s, 0.35f);
                for (auto& p : ph) p.prepare (s);
                tS.set (s, 140); fbS.set (s, 30); mixS.set (s, 25); modS.set (s, 40); parS.set (s, 40); duckS.set (s, 8); moS.set (s, 10); cutS.set (s, 20); hpS.set (s, 20); satS.set (s, 20);
                xf.prepare (s, 12);
                onset.prepare (s, 0.004f, 0.12);
                env.set (s, 5, 250);
                reset();
            }
            void reset() override
            {
                for (auto& l : line) l.clear();
                for (auto& g : ice) g.clear();
                for (auto& t : det) t.clear();
                for (auto& p : ph) p.clear();
                for (int c = 0; c < 2; ++c) { lp1[c].reset(); lp2[c].reset(); hp[c].reset(); bp[c].reset(); hold[c] = 0; holdN[c] = 0; }
                revPh = 0; icePh = 0; swell = 1; iceG = 1; tS.unset(); xf.current = -1; xf.pending = -1; xf.g = 1;
                first = true; wow.ph = 0; flut.ph = 0.3; sweepL.ph = 0; tremL.ph = 0; chorL.ph = 0; vibL.ph = 0; crk = 0; duckG = 1;
                for (auto* sm : { &duckS, &moS, &cutS, &hpS, &satS }) sm->init = false;
                for (auto& t : det) t.lfo.ph = 0;
                noise = Noise {}; env.env = 0;
                onset = OnsetB {}; onset.prepare (sr, 0.004f, 0.12);
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = std::clamp (pMode.step (*this), 0, (int) modes.size() - 1);
                auto targetOf = [&] (int k) { const auto& m = modes[(size_t) k]; return logMap (pTime.get (*this), m.lo, m.hi) * m.sub * 0.001f * sr; };
                const float fbT = pFb.get (*this) * fbMax, mixT = pMix.get (*this), modT = pMod.get (*this), parT = pParam.get (*this);
                const int col = color ? std::clamp (pColor.step (*this), 0, 2) : 1;     // 0 tape, 1 digi, 2 analog
                const bool pingT = pPing.step (*this) > 0;
                envRduck = (float) std::exp (-1.0 / ((elemental ? logMap (parT, 0.06f, 1.2f) : 0.25f) * sr));
                envR0 = (float) std::exp (-1.0 / (0.25 * sr));
                if (first) { tS.snap (targetOf (want)); fbS.snap (fbT); mixS.snap (mixT); modS.snap (modT); parS.snap (parT); first = false; }
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    const int mi = xf.current;
                    const Kind k = kinds[(size_t) mi];
                    if (changed) { tS.snap (targetOf (mi)); swell = 1; }
                    const float T = std::max (16.0f, tS.next (targetOf (mi)));
                    const float fb = fbS.next (fbT) * (k == Slap ? 0.4f : 1.0f), mix = mixS.next (mixT), md = modS.next (modT), pa = parS.next (parT);
                    const bool ping = k == PingPong || pingT;
                    const bool two = ping || stereoIn;
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float mono = 0.5f * (inL + inR);
                    // inviluppi d'ingresso: ducking e swell
                    env.r = k == Duck ? envRduck : envR0;
                    const float e = env.tick (mono);
                    duckG = duckS.next (k == Duck ? 1.0f / (1.0f + 15.0f * e) : 1.0f);
                    if (onset.tick (mono) && k == Swell) swell = 0;
                    swell = std::min (1.0f, swell + 1.0f / (0.45f * sr));
                    // modulazione della lettura (campioni)
                    float mo = 0;
                    const bool tapeCol = (color && col == 0) || k == Tape || k == DTape;
                    if (tapeCol)
                    {
                        const float depth = k == DTape ? 0.006f * md : 0.0025f * md;
                        crk += 0.0005f * (noise.uni() - crk);
                        mo += T * (depth * wow.sine() + 0.25f * depth * flut.sine() + (k == DTape ? 6.0f * md * crk : 0.0f) * 0.004f);
                    }
                    if (color && col == 1 && ! tapeCol) mo += (0.3f + 1.5f * md) * 0.001f * sr * chorL.sine();
                    if (color && col == 2) { vibL.setHz (sr, 5.0f); mo += 0.25f * md * 0.001f * sr * vibL.sine(); vibL.step(); }
                    if (k == Bubble) mo += 0.0016f * sr * wow.sine() + 0.004f * T * wow.sine (0.25);
                    if (elemental && k != Analog && k != Reverse && k != DTape)
                    {
                        chorL.setHz (sr, logMap (md, 0.1f, 5.0f));
                        mo += std::min (1.0f, 4.0f * md) * 0.0012f * sr * chorL.sine();
                    }
                    else chorL.setHz (sr, 0.9f);
                    wow.setHz (sr, k == Bubble ? 0.8f : 0.6f); flut.setHz (sr, 6.3f);
                    wow.step(); flut.step(); chorL.step();
                    const float d = std::clamp (T + moS.next (mo), 2.0f, (float) line[0].capacity() - 4.0f);
                    // filtri e colore dell'anello
                    float cut = bw, sat = 0, hpf = 40;
                    if (color) { if (col == 0) { cut = 4200; sat = 1.3f; } else if (col == 2) { cut = 2500; sat = 1.8f; } }
                    switch (k)
                    {
                        case Analog: cut = std::min (cut, 2800.0f); sat = std::max (sat, 1.6f); break;
                        case Tape:   cut = std::min (cut, 4500.0f); sat = std::max (sat, 1.3f); break;
                        case DTape:  cut = 4200.0f - 1800.0f * pa; sat = 1.0f + 5.0f * pa; break;
                        case Bubble: cut = 2500; break;
                        case Mic:    cut = 3400; hpf = 300; sat = 2.0f; break;
                        case Lofi:   cut = 3000; sat = elemental ? 1.0f + 6.0f * pa : 1.5f; break;
                        default: break;
                    }
                    // colore lisciato (cambi di TYPE e di modo senza salti)
                    const float cutN = cutS.next (cut), hpN = hpS.next (hpf);
                    sat = satS.next (sat);
                    if ((i & 7) == 0)
                        for (int c = 0; c < 2; ++c) { lp1[c].set (sr, cutN, 0.6); lp2[c].set (sr, cutN * 1.4f, 0.6); hp[c].set (sr, hpN); }
                    float wet[2] { 0, 0 }, v[2] { 0, 0 };
                    const int nc = two ? 2 : 1;
                    for (int c = 0; c < nc; ++c)
                    {
                        auto& L = line[c];
                        float y, extra = 0;
                        if (k == Reverse)
                        {
                            // due testine all'indietro sfalsate di mezzo segmento, finestre sin^2 (somma costante)
                            const float seg = T;
                            float acc = 0;
                            for (int h = 0; h < 2; ++h)
                            {
                                // fase incrementale: se il segmento cambia (TIME che scorre) la posizione non salta
                                double ph2 = revPh + 0.5 * h;
                                ph2 -= std::floor (ph2);
                                const float pos = (float) ph2 * seg;
                                const float w = std::sin (pi * pos / seg);
                                acc += w * w * L.read (std::min (2.0f * pos + 2.0f, (float) L.capacity() - 4.0f));
                            }
                            y = acc;
                        }
                        else if (k == Dual) { y = L.read (d); extra = L.read (std::min (d * (1.0f + 0.75f * pa), (float) L.capacity() - 4.0f)); }
                        else if (k == Multi1 || k == Multi2 || k == Pattern)
                        {
                            static const float pat[8][4] = { { 0.25f, 0.5f, 0.75f, 1.0f }, { 0.333f, 0.667f, 1.0f, 1.0f }, { 0.375f, 0.75f, 1.0f, 1.0f },
                                                             { 0.25f, 0.375f, 0.75f, 1.0f }, { 0.5f, 0.625f, 0.875f, 1.0f }, { 0.167f, 0.5f, 0.833f, 1.0f },
                                                             { 0.75f, 0.875f, 1.0f, 1.0f }, { 0.125f, 0.25f, 0.625f, 1.0f } };
                            static const float m1[4] = { 0.333f, 0.667f, 1.0f, 1.0f }, m2[4] = { 0.25f, 0.75f, 1.0f, 1.0f };
                            const float* tp = k == Multi1 ? m1 : k == Multi2 ? m2 : pat[std::clamp ((int) (pa * 7.99f), 0, 7)];
                            y = L.read (d);
                            for (int t = 0; t < 3; ++t) if (tp[t] < 0.999f) extra += (0.45f + 0.15f * (float) t) * L.read (std::max (2.0f, d * tp[t]));
                        }
                        else y = L.read (d);
                        // anello: passa-basso a 2 poli, passa-alto, saturazione, riduzione lo-fi
                        float f = hp[c].hp (lp2[c].lp (lp1[c].lp (y)));
                        if (sat > 0.01f) f = std::tanh (sat * f) / sat;
                        if (k == Mic) f = std::tanh (2.0f * f) * 0.5f;
                        if (k == Lofi)
                        {
                            if (++holdN[c] >= 4) { holdN[c] = 0; hold[c] = std::round (f * 512.0f) / 512.0f; }
                            f = hold[c];
                        }
                        if (k == Ice)
                        {
                            static const float st[7] = { -12, -7, -5, 0, 5, 7, 12 };
                            ice[c].setRatio (std::exp2 (st[std::clamp ((int) (pa * 6.99f), 0, 6)] / 12.0f));
                            f = ice[c].tick (f);
                        }
                        v[c] = f;
                        float w = (k == Dual) ? 0.65f * (f + extra) : f + extra;
                        // ramo wet: tremolo, sweep, phaser, detune, inviluppi
                        if (k == Trem)
                        {
                            if (c == 0) { tremL.setHz (sr, elemental ? logMap (pa, 1.0f, 12.0f) : 5.0f); tremL.step(); }
                            w *= 1.0f - 0.85f * (0.5f + 0.5f * tremL.sine (c ? 0.25 : 0.0));
                        }
                        if (k == Sweep)
                        {
                            if (c == 0) { sweepL.setHz (sr, 0.35f); sweepL.step(); }
                            const float fc = logMap (0.5f + 0.5f * sweepL.sine (c ? 0.25 : 0.0), 350.0f, 2800.0f);
                            bp[c].setG (fastTanPi (fc, sr), 1.0f / 2.2f);
                            w = 0.4f * w + 1.2f * bp[c].bp (w) / 2.2f;
                        }
                        if (elemental && (k == Analog || k == Reverse))
                        {
                            w = (1.0f - 0.5f * pa) * w + pa * ph[c].tick (w, 0.4f, 1.0f, 0.3f);
                            w = det[c].tick (w, 15.0f * md);
                        }
                        if (k == Ice)
                        {
                            if (c == 0)
                            {
                                icePh += 2.0 / (double) T;
                                icePh -= std::floor (icePh);
                                iceG += ((icePh < 0.7 ? 1.0f : 0.0f) - iceG) * 0.01f;
                            }
                            w *= iceG;
                        }
                        wet[c] = w * duckG * (k == Swell ? swell * swell : 1.0f);
                    }
                    if (k == Reverse) { revPh += 1.0 / (double) T; revPh -= std::floor (revPh); }
                    // scrittura (ping-pong: incrociata) con limitazione morbida dell'anello
                    if (two)
                    {
                        const float a = stereoIn ? inL : mono, b = stereoIn ? inR : mono;
                        if (ping) { line[0].push (mono + loopSat (fb * v[1])); line[1].push (loopSat (fb * v[0])); }
                        else { line[0].push (a + loopSat (fb * v[0])); line[1].push (b + loopSat (fb * v[1])); }
                    }
                    else { line[0].push (mono + loopSat (fb * v[0])); line[1].push (0.0f); wet[1] = wet[0]; }
                    float dg, wg;
                    mixGains (law, mix, fixedWet, dg, wg);
                    wg *= g;
                    if (numCh > 1) { ch[0][i] = dg * inL + wg * wet[0]; ch[1][i] = dg * inR + wg * wet[1]; }
                    else ch[0][i] = dg * inL + wg * 0.5f * (wet[0] + wet[1]);
                }
            }
        private:
            Config cfg;
            std::vector<ModeDef> modes;
            std::vector<Kind> kinds;
            bool color = false, elemental = false, stereoIn = false, first = true;
            int law = 0, holdN[2] {};
            float sr = 48000, bw = 12000, fbMax = 0.98f, fixedWet = 0.6f, envRduck = 0.99f, envR0 = 0.99f, hold[2] {}, swell = 1, iceG = 1, crk = 0, duckG = 1;
            double revPh = 0, icePh = 0;
            RoleParam pMix, pFb, pTime, pMode, pMod, pColor, pParam, pPing;
            DelayLine line[2];
            GrainShifter ice[2];
            DetunerB det[2];
            PhaserLite ph[2];
            Svf lp1[2], lp2[2], bp[2];
            OnePole hp[2];
            Lfo wow, flut, chorL, vibL, sweepL, tremL;
            Noise noise;
            Envelope env;
            OnsetB onset;
            Smooth2 tS;
            Smooth fbS, mixS, modS, parS, duckS, moS, cutS, hpS, satS;
            XFader xf;
        };

        //==============================================================================
        /** Involucro senza click per i cloni che riusano tale e quale la config di un modello a catalogo:
            "type=i5wrap || <config del modello di partenza>" (dentro, "itype=" sta per "type="). L'effetto interno e'
            quello che createEffect() costruisce per la config di partenza. A comandi fermi l'interno elabora blocchi
            interi con gli stessi valori: uscita uguale al modello di partenza. Mentre un pomello si muove il valore
            scorre (due poli in cascata da 12 ms) e l'interno elabora un campione alla volta, cosi' le famiglie che leggono
            i comandi una volta per blocco non producono gradini; un cambio di selettore o levetta passa per una discesa e
            risalita del volume (6 + 6 ms). */
        class I5Wrap final : public Effect
        {
        public:
            explicit I5Wrap (const ModelDef& d) : Effect (d), inner (d)
            {
                const std::string c = d.config != nullptr ? d.config : "";
                const auto p = c.find ("||");
                innerCfg = p == std::string::npos ? std::string() : c.substr (p + 2);
                for (size_t k; (k = innerCfg.find ("itype=")) != std::string::npos;) innerCfg.replace (k, 6, "type=");
                while (! innerCfg.empty() && innerCfg.front() == ' ') innerCfg.erase (0, 1);
                inner.config = innerCfg.c_str();
                fx = createEffect (inner);
                for (int k = 0; k < d.numControls; ++k)
                {
                    const auto kd = d.controls[k].kind;
                    stepped[k] = kd == ControlKind::Selector || kd == ControlKind::Toggle || kd == ControlKind::Button;
                    cur[k] = mid[k] = d.controls[k].def;
                }
            }
            void prepare (double s, int maxBlock) override
            {
                fx->prepare (s, maxBlock);
                a = (float) std::exp (-(double) sub / (0.012 * s));
                fadeStep = (float) (1.0 / (0.006 * s));
                first = true; gain = 1; fading = 0;
            }
            void reset() override { fx->reset(); first = true; gain = 1; fading = 0; }
            float readout (int i) const override { return fx->readout (i); }
            void trigger (int action) override { fx->trigger (action); }
            void messageThreadUpdate() override { fx->messageThreadUpdate(); }
            bool loadFile (const std::string& path) override { const bool ok = fx->loadFile (path); loadedFile = fx->loadedFile; return ok; }
            bool acceptsFiles() const override { return fx->acceptsFiles(); }
            std::string saveState() const override { return fx->saveState(); }
            void restoreState (const std::string& st) override { fx->restoreState (st); }

            void process (float* const* ch, int numCh, int n) override
            {
                const int nc = def.numControls;
                float target[maxControls];
                bool moving = false, stepChange = false;
                for (int k = 0; k < nc; ++k)
                {
                    target[k] = p (k);
                    if (first) { cur[k] = mid[k] = target[k]; fx->params[k].store (target[k]); continue; }
                    if (stepped[k])
                    {
                        if (def.controls[k].kind == ControlKind::Button) fx->params[k].store (target[k]);
                        else if (differs (target[k], fx->params[k].load (std::memory_order_relaxed))) stepChange = true;
                    }
                    else if (differs (cur[k], target[k])) moving = true;
                }
                first = false;
                if (stepChange && fading == 0) fading = -1;
                if (! moving && fading == 0)
                {
                    fx->process (ch, numCh, n);
                    return;
                }
                for (int off = 0; off < n; off += sub)
                {
                    const int m = std::min (sub, n - off);
                    for (int k = 0; k < nc; ++k)
                        if (! stepped[k] && differs (cur[k], target[k]))
                        {
                            // due poli in cascata: la velocita' del comando parte da zero (niente gradino della derivata)
                            mid[k] = target[k] + a * (mid[k] - target[k]);
                            cur[k] = mid[k] + a * (cur[k] - mid[k]);
                            if (std::abs (cur[k] - target[k]) < 1.0e-5f && std::abs (mid[k] - target[k]) < 1.0e-5f) cur[k] = mid[k] = target[k];
                            fx->params[k].store (cur[k]);
                        }
                    float* sp[2] = { ch[0] + off, numCh > 1 ? ch[1] + off : nullptr };
                    const float g0 = gain;
                    if (fading < 0 && gain <= 0.0f)
                    {
                        // a volume zero: si applicano i selettori e le levette
                        for (int k = 0; k < nc; ++k)
                            if (stepped[k] && def.controls[k].kind != ControlKind::Button) fx->params[k].store (p (k));
                        fading = 1;
                    }
                    fx->process (sp, numCh, m);
                    for (int i = 0; i < m; ++i)
                    {
                        if (fading < 0) gain = std::max (0.0f, gain - fadeStep);
                        else if (fading > 0) { gain = std::min (1.0f, gain + fadeStep); if (gain >= 1.0f) fading = 0; }
                        if (g0 < 1.0f || gain < 1.0f)
                            for (int c = 0; c < numCh; ++c) sp[c][i] *= gain;
                    }
                }
            }
        private:
            ModelDef inner;
            std::string innerCfg;
            std::unique_ptr<Effect> fx;
            static constexpr int sub = 1;
            float cur[maxControls] {}, mid[maxControls] {}, a = 0.99f, fadeStep = 0.003f, gain = 1;
            bool stepped[maxControls] {};
            bool first = true;
            int fading = 0;      // -1 discesa, +1 risalita
        };

        //==============================================================================
        /** Riverberi multimodo (RV600, DR400, DIG Reverb, MINI-UNIVERSE, GALAXIA). */
        class I5Verb final : public Effect
        {
        public:
            enum Kind { Room, SRoom, BRoom, Hall, PlateK, Spring, Spring63, Mod, Gate, Echo, Duck, Reverse, Tile, Cave, Space, Chamber,
                        Flanger, Chorus, Shimmer, Cloud, Bloom, Swell, Lofi, Dly, DlySRoom, DlyBRoom, DlyHall, DlyPlate, NumKinds };
            explicit I5Verb (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                modes = parseModes (cfg.str ("modes", ""));
                static const char* names[NumKinds] = { "room", "sroom", "broom", "hall", "plate", "spring", "spring63", "mod", "gate", "echo",
                                                       "duck", "reverse", "tile", "cave", "space", "chamber", "flanger", "chorus", "shimmer",
                                                       "cloud", "bloom", "swell", "lofi", "dly", "dlysroom", "dlybroom", "dlyhall", "dlyplate" };
                for (auto& m : modes)
                {
                    int k = Hall;
                    for (int j = 0; j < NumKinds; ++j) if (m.kind == names[j]) k = j;
                    kinds.push_back ((Kind) k);
                    if (m.hi <= 0) { m.lo = 0.5f; m.hi = 8.0f; }
                }
                preDelayRole = cfg.str ("timerole", "decay") == "predelay";
                auto pr = cfg.list ("pre"); preLo = pr.size() > 0 ? (float) pr[0] : 0.0f; preHi = pr.size() > 1 ? (float) pr[1] : 250.0f;
                const auto ps = cfg.str ("params", "");
                params = ps == "universe" ? 1 : ps == "galaxia" ? 2 : 0;
                dual = cfg.num ("dual", 0) > 0.5;
                law = lawOf (cfg.str ("mixlaw", "wet"));
                bw = (float) cfg.num ("bw", 12000); fbMax = (float) std::min (0.98, cfg.num ("fbmax", 0.95));
                pMix = role (*this, "mix", 0.4f); pDecay = role (*this, "decay", 0.5f); pTime = role (*this, "time", 0.3f);
                pTone = role (*this, "tone", 0.6f); pLend = role (*this, "lend", 0.5f); pMod = role (*this, "mod", 0.3f);
                pP1 = role (*this, "p1", 0.1f); pP2 = role (*this, "p2", 0.8f); pP3 = role (*this, "p3", 0.3f);
                pMode = role (*this, "mode", 0.0f); pHold = role (*this, "hold", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                core.prepare (s); fdn.prepare (s, 3.0f); spring.prepare (s);
                coreInit = core; fdnInit = fdn;          // stati iniziali: reset() li ricopia (stesse dimensioni: nessuna allocazione)
                dly.allocate ((int) (s * 2.1) + 64);
                for (auto& a : ap) a.allocate ((int) (0.14 * s) + 8);
                for (auto& d : drift) d.allocate ((int) (0.02 * s));
                for (auto& c : cho) c.prepare (s);
                for (auto& g : shim) g.prepare (s, 70);
                xf.prepare (s, 15);
                onset.prepare (s, 0.004f, 0.15);
                for (auto* sm : { &mixS, &holdS, &toneS, &p1S, &p2S, &lendS, &duckS, &fbS }) sm->set (s, 25);
                tS.set (s, 150);
                dlp[0].set (s, 6000); dhp[0].set (s, 60); dlp[1].set (s, 7000); dhp[1].set (s, 60);
                reset();
            }
            void reset() override
            {
                core = coreInit; fdn = fdnInit; spring.clear(); dly.clear();
                for (auto& a : ap) a.clear();
                for (auto& d : drift) d.clear();
                for (auto& c : cho) c.clear();
                for (auto& g : shim) g.clear();
                for (int c = 0; c < 2; ++c)
                {
                    hpW[c].reset(); lpW[c].reset(); dlp[c].reset(); dhp[c].reset();
                    lend[c].prepare (sr, SmoothEq::LowShelf, 250.0, 0.7);
                    hold1[c] = 0; holdN[c] = 0;
                }
                shFb = 0; dFb = 0; sw = 1; envV = 0; noise = Noise {}; driftL.ph = 0;
                onset = OnsetB {}; onset.prepare (sr, 0.004f, 0.15);
                for (auto* sm : { &mixS, &holdS, &toneS, &p1S, &p2S, &lendS, &duckS, &fbS }) sm->init = false;
                tS.unset();
                xf.current = -1; xf.pending = -1; xf.g = 1;
                lastCoreKey = -1; blockFirst = true;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = std::clamp (pMode.step (*this), 0, (int) modes.size() - 1);
                const bool hold = pHold.step (*this) > 0;
                // il modo corrente si aggiorna nel primo campione (dissolvenza): parametri del blocco dal modo attivo
                bool changed = false;
                float g = xf.tick (want, changed);
                const int mi = xf.current;
                if (changed) clearEngines();
                const Kind k = kinds[(size_t) mi];
                const auto& md = modes[(size_t) mi];
                // comandi dei motori lisciati a blocchi (t60, tono e dimensioni cambiano senza gradini)
                const float ab = blockFirst ? 1.0f : 1.0f - std::exp (-(float) n / (0.04f * sr));
                auto bs = [ab] (float& st, float t) { st += ab * (t - st); if (std::abs (st - t) < 1.0e-4f) st = t; return st; };
                const float decK = bs (decB, pDecay.get (*this)), timeK = bs (timeB, pTime.get (*this)), toneK = bs (toneB, pTone.get (*this));
                const float modK = bs (modB, pMod.get (*this)), p3 = bs (p3B, pP3.get (*this));
                const float p1 = pP1.get (*this), p2 = pP2.get (*this), lendK = pLend.get (*this);
                blockFirst = false;
                const bool isDly = k == Dly || k == DlySRoom || k == DlyBRoom || k == DlyHall || k == DlyPlate;
                // tempo di riverbero, pre-delay, tono
                float t60 = pDecay.present() ? logMap (decK, md.lo, md.hi) : logMap (timeK, md.lo, md.hi);
                if (isDly) t60 = k == DlySRoom ? 0.8f : k == DlyBRoom ? 1.6f : k == DlyHall ? 2.4f : 2.0f;
                float preS = preDelayRole ? linMap (timeK, preLo, preHi) * 0.001f : 0.012f;
                float toneHz = (params == 0 && pTone.present() && ! (dual && isDly)) ? logMap (toneK, 1200.0f, bw) : 0.75f * bw;
                const float modAmt = params == 1 ? p3 : modK;     // LENGTH / DWELL / DIFFUSION / ... secondo il modo
                if (hold) t60 = 1000.0f;
                // motore VerbCore per i modi classici
                VerbCore::Algo algo = VerbCore::Hall; int var = 0; float pa = 0.5f, pb = 0.5f, preFb = 0;
                bool useCore = true;
                switch (k)
                {
                    case Room: case SRoom:  algo = VerbCore::Room; break;
                    case BRoom:             algo = VerbCore::Room; var = 1; break;
                    case Hall: case Duck:   algo = VerbCore::Hall; break;
                    case PlateK:            algo = VerbCore::Plate_; break;
                    case Spring63:          algo = VerbCore::Spring6G15; pa = 0.55f; break;
                    case Mod:               algo = VerbCore::ModV; break;
                    case Gate:              algo = VerbCore::Dyna; var = 1; pa = 0.4f; break;
                    case Reverse:           algo = VerbCore::Reverse; pa = std::min (1.2f, 0.25f + 0.15f * t60); break;
                    case Tile:              algo = VerbCore::Room; var = 1; toneHz = std::min (0.9f * bw, toneHz * 1.5f); break;
                    case Cave:              algo = VerbCore::Hall; var = 1; preS += 0.07f; preFb = 0.3f; break;
                    case Space:             algo = VerbCore::Shim; pa = preDelayRole ? timeK : 0.5f; preS = 0.012f; break;
                    case Chamber:           algo = VerbCore::Plate_; var = 2; break;
                    case Flanger:           algo = VerbCore::Flerb; pa = 0.3f; pb = 0.5f; break;
                    case Chorus:            algo = VerbCore::Hall; break;
                    case Echo:
                        algo = VerbCore::EchoV;
                        if (preDelayRole) { preS = linMap (timeK, 0.04f, 0.6f); preFb = 0.85f * decK; t60 = 1.5f; }
                        else { preS = 0.28f; preFb = 0.3f + 0.45f * decK; }
                        break;
                    case DlySRoom:          algo = VerbCore::Room; break;
                    case DlyBRoom:          algo = VerbCore::Room; var = 1; break;
                    case DlyHall:           algo = VerbCore::Hall; break;
                    case DlyPlate:          algo = VerbCore::Plate_; break;
                    default:                useCore = false; break;
                }
                if (useCore && ! (k == Dly))
                {
                    const int key = (int) algo * 1000003 + var * 7919 + (int) std::lround (t60 * 100) * 31 + (int) std::lround (toneHz)
                                    + (int) std::lround (preS * 10000) * 17 + (int) std::lround (preFb * 100) * 13 + (int) std::lround (pa * 100);
                    if (key != lastCoreKey) { core.set (algo, t60, toneHz, preS, preFb, pa, pb, var); lastCoreKey = key; }
                }
                // motori propri
                const float tf = hold ? 1000.0f : t60;
                switch (k)
                {
                    case Shimmer: fdn.set (std::min (tf, hold ? 1000.0f : 12.0f), 9000.0f, 1.5f, 6.0f, 0.65f); break;
                    case Cloud:   fdn.set (tf, 9000.0f, 2.6f, 10.0f, 0.5f + 0.35f * modAmt); break;
                    case Bloom:   fdn.set (tf, 8500.0f, 2.0f, 8.0f, 0.75f); break;
                    case Swell:   fdn.set (tf, 8500.0f, 1.6f, 5.0f, 0.65f); break;
                    case Lofi:    fdn.set (tf, 7000.0f, 0.9f, 3.0f, 0.6f); break;
                    case Spring:
                    {
                        const int nsp = (params == 0) ? 2 : 1 + (int) std::lround (modAmt * 2.0f);
                        spring.set (VerbCore::springGain (hold ? 1000.0f : t60, 0.038f), params == 0 ? toneHz : 4500.0f, nsp, -0.6f);
                        break;
                    }
                    default: break;
                }
                // parametri dei filtri sul wet
                float hpT = 20, lpT = 0.9f * bw, lendDb = 0, noiseT = 0, crush = 0, srHold = 1, driftAmt = 0;
                if (params == 1)
                {
                    if (k != Lofi) hpT = logMap (p1, 20.0f, 1000.0f);
                    lpT = (k == Shimmer || k == Lofi) ? 10000.0f : logMap (p2, 800.0f, 16000.0f);
                    if (k == Room || k == Hall || k == PlateK || k == Lofi) driftAmt = p3;
                    if (k == Lofi) { srHold = logMap (p1, 1.0f, 10.0f); noiseT = 0.006f * p2; lpT = 5000.0f; }
                }
                else if (params == 2)
                {
                    lpT = k == Lofi ? 5000.0f : logMap (toneK, 1500.0f, 16000.0f);
                    lendDb = k == Lofi ? 0.0f : lin3 (lendK);
                    if (k == Room || k == Hall || k == PlateK || k == Lofi) driftAmt = modK;
                    if (k == Lofi) { noiseT = 0.006f * toneK; crush = lendK; srHold = 1.0f + 7.0f * lendK; }
                }
                const float pitchSemis = params == 1 ? 24.0f * p2 : 24.0f * modK;
                const float shimAmt = params == 1 ? p3 : 0.5f;
                const float riseS = logMap (modAmt, 0.1f, 3.0f);
                const float bloomLen = 0.3f + 1.0f * modAmt;
                const float dTarget = isDly ? logMap (timeK, md.lo, md.hi) * 0.001f * sr : 0.0f;
                const float fbT = (dual && isDly) ? toneK * fbMax : 0.0f;
                if (k == Shimmer)
                {
                    const float r = std::sqrt (std::exp2 (pitchSemis / 12.0f));
                    shim[0].setRatio (r); shim[1].setRatio (r);
                }
                driftL.setHz (sr, 0.6f);
                const float mixT = pMix.get (*this);
                // livelli dei motori pareggiati col banco (stessa sonorita' del wet fra i modi)
                const float kindGain = (k == Room || k == SRoom || k == BRoom) ? 0.55f : k == Tile ? 0.7f : k == Spring63 ? 0.5f
                                     : k == Spring ? 0.4f : 1.0f;
                for (int i = 0; i < n; ++i)
                {
                    if (i > 0)
                    {
                        g = xf.tick (want, changed);
                        if (changed) clearEngines();
                    }
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float hs = holdS.next (hold ? 1.0f : 0.0f);
                    float x = 0.5f * (inL + inR) * (1.0f - hs);
                    if (k == Chorus) x = 0.6f * (x + cho[0].tick (x, 0.8f, 8.0f, 3.0f));
                    float wl = 0, wr = 0;
                    if (isDly)
                    {
                        const float T = std::max (16.0f, tS.next (dTarget));
                        const float y = dly.read (T);
                        dFb = dhp[0].hp (dlp[0].lp (y));
                        dly.push (x + loopSat (fbS.next (fbT) * dFb));
                        wl = wr = y;
                        if (k != Dly) { float a, b; core.tick (0.5f * (x + y), a, b); wl += 0.7f * a; wr += 0.7f * b; }
                    }
                    else if (useCore)
                    {
                        core.tick (x, wl, wr);
                    }
                    else switch (k)
                    {
                        case Shimmer:
                        {
                            fdn.tick (x + (hold ? 0.0f : 0.45f * shimAmt) * loopSat (shFb), wl, wr);
                            const float s = shim[1].tick (shim[0].tick (0.5f * (wl + wr)));
                            shFb = dlp[1].lp (s);
                            wl = 0.8f * wl + 0.5f * shimAmt * shFb; wr = 0.8f * wr + 0.5f * shimAmt * shFb;
                            break;
                        }
                        case Cloud:
                        {
                            float v = x;
                            static const float L2[2] = { 0.023f, 0.037f };
                            for (int a = 0; a < 2; ++a)
                            {
                                const float gg = 0.5f + 0.3f * modAmt;
                                const float dd = ap[a].read (L2[a] * sr);
                                const float y = -gg * v + dd;
                                ap[a].push (v + gg * y);
                                v = y;
                            }
                            fdn.tick (v, wl, wr);
                            break;
                        }
                        case Bloom:
                        {
                            float v = x;
                            static const float L4[4] = { 0.031f, 0.043f, 0.059f, 0.073f };
                            for (int a = 0; a < 4; ++a)
                            {
                                const float dd = ap[a].read (std::min (L4[a] * bloomLen * sr, 0.135f * sr));
                                const float y = -0.7f * v + dd;
                                ap[a].push (v + 0.7f * y);
                                v = y;
                            }
                            fdn.tick (v, wl, wr);
                            break;
                        }
                        case Swell: case Lofi:
                            fdn.tick (x, wl, wr);
                            break;
                        case Spring:
                            spring.tick (x * 1.6f, wl, wr);
                            break;
                        default: break;
                    }
                    // inviluppi sul wet: ducking, swell
                    const float e = std::abs (x);
                    envV = e > envV ? e + 0.995f * (envV - e) : e + 0.9998f * (envV - e);
                    if (k == Duck) { const float dk = duckS.next (1.0f / (1.0f + 18.0f * envV)); wl *= dk; wr *= dk; }
                    if (onset.tick (x) && k == Swell) sw = 0;
                    if (k == Swell) { sw = std::min (1.0f, sw + 1.0f / (riseS * sr)); wl *= sw * sw; wr *= sw * sw; }
                    // filtri, deriva, lo-fi
                    if (params != 0)
                    {
                        const float hpN = p1S.next (hpT), lpN = p2S.next (lpT);
                        if ((i & 7) == 0) for (int c = 0; c < 2; ++c) { hpW[c].set (sr, hpN, 0.707); lpW[c].set (sr, lpN, 0.707); }
                        const float ld = lendS.next (lendDb);
                        float w2[2] = { wl, wr };
                        for (int c = 0; c < 2; ++c)
                        {
                            float v = lpW[c].lp (hpW[c].hp (w2[c]));
                            if (params == 2) v = lend[c].tick (v, ld);
                            if (driftAmt > 0.001f)
                            {
                                drift[c].push (v);
                                v = drift[c].read ((0.004f + 0.0015f * driftAmt * driftL.sine (c ? 0.5 : 0.0)) * sr);
                            }
                            if (k == Lofi)
                            {
                                if (++holdN[c] >= (int) srHold) { holdN[c] = 0; hold1[c] = v; }
                                v = hold1[c];
                                if (crush > 0.001f) { const float q = std::exp2 (12.0f - 8.0f * crush); v = std::round (v * q) / q; }
                                v += noiseT * noise.uni();
                            }
                            w2[c] = v;
                        }
                        driftL.step();
                        wl = w2[0]; wr = w2[1];
                    }
                    float dg, wg;
                    mixGains (law, mixS.next (mixT), 0.0f, dg, wg);
                    wg = std::max (wg, hs) * g * kindGain;
                    if (numCh > 1) { ch[0][i] = dg * inL + wg * wl; ch[1][i] = dg * inR + wg * wr; }
                    else ch[0][i] = dg * inL + wg * 0.5f * (wl + wr);
                }
            }
        private:
            /** LEND: -12 dB a sinistra, 0 a meta', +9 dB a destra. */
            static float lin3 (float x) noexcept { return x < 0.5f ? -12.0f + 24.0f * x : 18.0f * (x - 0.5f); }
            void clearEngines()
            {
                core.clear(); fdn.clear(); spring.clear(); dly.clear(); shFb = 0; dFb = 0;
                for (auto& a : ap) a.clear();
                for (auto& g : shim) g.clear();
                lastCoreKey = -1;
            }
            Config cfg;
            std::vector<ModeDef> modes;
            std::vector<Kind> kinds;
            bool preDelayRole = false, dual = false, blockFirst = true;
            int params = 0, law = 0, lastCoreKey = -1, holdN[2] {};
            float decB = 0, timeB = 0, toneB = 0, modB = 0, p3B = 0;
            float sr = 48000, preLo = 0, preHi = 250, bw = 12000, fbMax = 0.95f, shFb = 0, dFb = 0, sw = 1, envV = 0, hold1[2] {};
            RoleParam pMix, pDecay, pTime, pTone, pLend, pMod, pP1, pP2, pP3, pMode, pHold;
            VerbCore core, coreInit;
            Fdn fdn, fdnInit;
            SpringTank spring;
            DelayLine dly, ap[4], drift[2];
            ChorusLite cho[1];
            GrainShifter shim[2];
            Svf hpW[2], lpW[2];
            OnePole dlp[2], dhp[2];
            SmoothEq lend[2];
            Lfo driftL;
            Noise noise;
            OnsetB onset;
            XFader xf;
            Smooth mixS, holdS, toneS, p1S, p2S, lendS, duckS, fbS;
            Smooth2 tS;
        };

        //==============================================================================
        /** Gradi diatonici: semitoni per 'steps' gradi sopra/sotto la nota f0 nella scala maggiore di 'root'. */
        inline float diatonicSemis (int steps, int root, float f0) noexcept
        {
            static const int scale[7] = { 0, 2, 4, 5, 7, 9, 11 };
            if (f0 < 50.0f)
            {
                // senza nota stabile: intervallo maggiore "di riserva"
                const int oct = (int) std::floor (steps / 7.0);
                const int deg = ((steps % 7) + 7) % 7;
                return (float) (scale[deg] + 12 * oct);
            }
            const int note = (int) std::lround (69.0f + 12.0f * std::log2 (f0 / 440.0f));
            const int rel = ((note - root) % 12 + 12) % 12;
            int d = 0;
            for (int k = 0; k < 7; ++k) if (scale[k] <= rel) d = k;
            const int t = d + steps;
            const int oct = (int) std::floor (t / 7.0);
            return (float) (scale[((t % 7) + 7) % 7] + 12 * oct - scale[d]);
        }

        /** Pitch shifter / armonizzatore (US600) e trasposizione fissa (DIG Pitch). */
        class I5Shift final : public Effect
        {
        public:
            explicit I5Shift (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                dig = cfg.str ("unit", "us") == "dig";
                pTime = role (*this, "time", 0.3f); pBal = role (*this, "balance", 0.5f); pKey = role (*this, "key", 0.0f);
                pShift = role (*this, "shift", 0.0f); pMode = role (*this, "mode", 0.0f); pWet = role (*this, "wet", 0.6f);
                pDry = role (*this, "dry", 0.7f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 3);
                pv.prepare (s, 45); bv.prepare (s, 40);
                dl.allocate ((int) (0.2 * s) + 8);
                xf.prepare (s, 12);
                for (auto* sm : { &balS, &wetS, &dryS, &dtS }) sm->set (s, 25);
                semS.set (s, 8); f0S.set (s, 30);
                reset();
            }
            void reset() override
            {
                bank.reset(); pv.clear(); bv.clear(); dl.clear();
                onset = OnsetB {}; onset.prepare (sr, 0.004f, 0.1);
                for (auto* sm : { &balS, &wetS, &dryS, &dtS, &semS, &f0S }) sm->init = false;
                xf.current = -1; xf.pending = -1; xf.g = 1;
                bend = 0; bendT = 0; quiet = 0; flT = 10; f0 = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                static const float tabUs[12] = { -24, -12, -7, -5, -2, -1, 3, 4, 5, 7, 12, 24 };
                static const int harm[12] = { -14, -7, -5, -4, -3, -2, 2, 3, 4, 5, 7, 14 };
                static const float detC[12] = { -30, -25, -20, -15, -10, -5, 5, 10, 15, 20, 25, 30 };
                static const float tbar[12] = { -12, -12, -7, -5, -2, -1, 3, 4, 5, 7, 12, 12 };
                static const float tabDig[9] = { -12, -7, -3, -1, 1, 3, 5, 7, 12 };
                static const int circle[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };
                const int sh = pShift.step (*this);
                // il selettore PITCH (e il modo) cambiano con una dissolvenza del ramo trasposto
                const int modeSel = dig ? 0 : std::clamp (pMode.step (*this), 0, 4);
                const int want = dig ? sh : modeSel * 100 + (modeSel <= 2 ? sh : 0);
                const int root = circle[std::clamp (pKey.step (*this), 0, 11)];
                const float timeK = pTime.get (*this);
                const float glideS = logMap (timeK, 0.03f, 2.0f), flHz = logMap (timeK, 2.0f, 12.0f);
                const float dtT = linMap (timeK, 0.0f, 0.15f) * sr;
                const float balT = pBal.get (*this), wetT = pWet.get (*this), dryT = pDry.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    if (changed) { pv.clear(); bv.clear(); dl.clear(); bend = 0; semS.init = false; }
                    const int m = dig ? 0 : xf.current / 100;
                    const int shc = dig ? xf.current : (m <= 2 ? xf.current % 100 : sh);
                    const float x = monoIn (ch, numCh, i);
                    bank.push (x);
                    const float p = bankPitch (bank, 0.25f);
                    if (p > 60 && p < 1500) f0 = f0S.next (p);
                    float wet = 0, out;
                    if (dig)
                    {
                        wet = pv.tick (bank, x, tabDig[std::clamp (shc, 0, 8)]);
                        const float yd = volKnob (dryS.next (dryT), 0.6f, 1.6f) * x + volKnob (wetS.next (wetT), 0.6f, 1.6f) * wet * g;
                        for (int c = 0; c < numCh; ++c) ch[c][i] = yd;
                        continue;
                    }
                    const bool on = onset.tick (x);
                    const float lvl = onset.level();
                    if (m <= 2)
                    {
                        float s;
                        if (m == 0) s = tabUs[std::clamp (shc, 0, 11)];
                        else if (m == 1) s = diatonicSemis (harm[std::clamp (shc, 0, 11)], root, f0);
                        else s = detC[std::clamp (shc, 0, 11)] * 0.01f;
                        s = semS.next (s);
                        wet = pv.tick (bank, x, s);
                        if (m == 2) { dl.push (wet); wet = dl.read (std::max (1.0f, dtS.next (dtT))); }
                        const float b = balS.next (balT);
                        out = std::min (1.0f, 2.0f * (1.0f - b)) * x + std::min (1.0f, 2.0f * b) * wet * g;
                    }
                    else
                    {
                        // TREM BAR / FLUTTER: a ogni attacco (nel pedale: a coperchio premuto), effetto solo
                        const float tgt = tbar[std::clamp (sh, 0, 11)];
                        if (on) { bendT = 1; flT = 0; quiet = 0; }
                        quiet = lvl < 0.004f ? quiet + 1 : 0;
                        if (quiet > (int) (0.08f * sr)) bendT = 0;
                        float s;
                        if (m == 3)
                        {
                            const float stp = 1.0f / (glideS * sr);
                            bend = bendT > bend ? std::min (bendT, bend + stp) : std::max (bendT, bend - stp);
                            s = bend * tgt;
                        }
                        else
                        {
                            flT += 1.0f / sr;
                            s = tgt * std::exp (-flT / 1.2f) * (0.5f - 0.5f * std::cos (twoPi * flHz * flT));
                        }
                        wet = bv.tick (bank, x, s);
                        out = volKnob (balS.next (balT), 0.5f, 2.0f) * wet * g;
                    }
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            Config cfg;
            bool dig = false;
            float sr = 48000, bend = 0, bendT = 0, flT = 10, f0 = 0;
            int quiet = 0;
            RoleParam pTime, pBal, pKey, pShift, pMode, pWet, pDry;
            SpectralBank bank;
            PolyVoice pv;
            BendVoice bv;
            DelayLine dl;
            OnsetB onset;
            XFader xf;
            Smooth balS, wetS, dryS, dtS, semS, f0S;
        };

        //==============================================================================
        /** Octaver polifonico SO400: POLY (ottava solo sotto la gamma), OCT 2, DRIVE. */
        class I5Oct final : public Effect
        {
        public:
            explicit I5Oct (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto r = cfg.list ("range"); rLo = r.size() > 0 ? (float) r[0] : 110.0f; rHi = r.size() > 1 ? (float) r[1] : 900.0f;
                pMode = role (*this, "mode", 0.0f); pDir = role (*this, "direct", 0.7f); pO1 = role (*this, "oct1", 0.6f);
                pO2 = role (*this, "oct2", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 38.0f, 2400.0f, 4, 0.14f, 3);
                xf.prepare (s, 12);
                for (auto* sm : { &dS, &o1S, &o2S, &fS }) sm->set (s, 25);
                tone.set (s, 4500); dc.set (s, 20);
                reset();
            }
            void reset() override
            {
                bank.reset(); lp1.reset(); lp2.reset(); tone.reset(); dc.reset();
                for (auto* sm : { &dS, &o1S, &o2S, &fS }) sm->init = false;
                xf.current = -1; xf.pending = -1; xf.g = 1;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = std::clamp (pMode.step (*this), 0, 2);
                const float dT = pDir.get (*this), o1T = pO1.get (*this), o2T = pO2.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    const int m = xf.current;
                    const float o2k = o2S.next (o2T);
                    const float fc = fS.next (m == 0 ? logMap (o2k, rLo, rHi) : 1200.0f);
                    if ((i & 7) == 0) { lp1.set (sr, fc, 0.5412); lp2.set (sr, fc, 1.3066); }
                    const float x = monoIn (ch, numCh, i);
                    const float src = lp2.lp (lp1.lp (x));
                    bank.push (src);
                    const float oc1 = bankOctave (bank, src, -1), oc2 = bankOctave (bank, src, -2);
                    const float dir = dS.next (dT), o1 = o1S.next (o1T);
                    float y;
                    if (m == 2)
                    {
                        const float v = x + 1.4f * o1 * oc1;
                        const float dr = 1.0f + 40.0f * o2k * o2k;
                        y = dc.hp (tone.lp (std::tanh (2.0f * dr * v) * 0.35f)) * 1.4f * dir;
                    }
                    else
                    {
                        y = 1.2f * dir * x + 1.4f * o1 * oc1;
                        if (m == 1) y += 1.4f * o2k * oc2;
                    }
                    // dissolvenza dei cambi di modo sul risultato (il diretto resta)
                    y = g * y + (1.0f - g) * 1.2f * dir * x;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }
        private:
            Config cfg;
            float sr = 48000, rLo = 110, rHi = 900;
            RoleParam pMode, pDir, pO1, pO2;
            SpectralBank bank;
            Svf lp1, lp2;
            OnePole tone, dc;
            XFader xf;
            Smooth dS, o1S, o2S, fS;
        };

        //==============================================================================
        /** Filtri a inviluppo: FM600 (unit=fm) e DW400 (unit=dw). */
        class I5Env final : public Effect
        {
        public:
            explicit I5Env (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                dw = cfg.str ("unit", "fm") == "dw";
                auto f = cfg.list ("f"); fLo = f.size() > 0 ? (float) f[0] : 200.0f; fHi = f.size() > 1 ? (float) f[1] : 3200.0f;
                pSens = role (*this, "sens", 0.5f); pRange = role (*this, "range", 0.6f); pPeak = role (*this, "peak", 0.5f);
                pFilt = role (*this, "filter", 0.0f); pDir = role (*this, "dir", 0.0f); pDecay = role (*this, "decay", 0.5f);
                pFreq = role (*this, "freq", 0.3f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                xf.prepare (s, 12);
                for (auto* sm : { &sS, &rS, &qS, &mS }) sm->set (s, 25);
                posS.set (s, 3);
                onset.prepare (s, 0.004f, 0.1);
                lfo.setHz (s, 2.0);
                reset();
            }
            void reset() override
            {
                for (auto& f : flt) f.reset();
                env = 0; senv = 0; morph = 1; lfo.ph = 0;
                onset = OnsetB {}; onset.prepare (sr, 0.004f, 0.1);
                for (auto* sm : { &sS, &rS, &qS, &mS, &posS }) sm->init = false;
                xf.current = -1; xf.pending = -1; xf.g = 1;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                static const float vow[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                const int want = dw ? std::clamp (pMode.step (*this), 0, 4) : std::clamp (pFilt.step (*this), 0, 2) + 10 * (pDir.step (*this) > 0 ? 1 : 0);
                const float sT = pSens.get (*this), rT = pRange.get (*this), qT = pPeak.get (*this), dK = pDecay.get (*this), mT = pFreq.get (*this);
                const float rel = (float) std::exp (-1.0 / ((dw ? logMap (dK, 0.04f, 1.5f) : 0.12f) * sr)), att = (float) std::exp (-1.0 / (0.003 * sr));
                const float synDec = (float) std::exp (-1.0 / (logMap (dK, 0.05f, 1.5f) * sr));
                const float morphStep = 1.0f / (logMap (dK, 0.05f, 1.5f) * sr);
                const int v1 = std::clamp ((int) std::lround (sT * 4.0f), 0, 4), v2 = std::clamp ((int) std::lround (mT * 4.0f), 0, 4);
                const float lfoShape = 1.0f + 12.0f * dK;
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    if (changed) for (auto& f : flt) f.reset();
                    const int m = xf.current;
                    const float x = monoIn (ch, numCh, i);
                    const float a = std::abs (x);
                    env = a > env ? a + att * (env - a) : a + rel * (env - a);
                    const float sens = logMap (sS.next (sT), 1.0f, 40.0f);
                    float y = 0;
                    if (! dw)
                    {
                        // FM600: posizione dalla dinamica, escursione RANGE, risonanza PEAK, verso POL
                        const int filt = m % 10;
                        const bool down = m >= 10;
                        const float p = posS.next (std::min (1.0f, env * sens));
                        const float span = std::exp2 (0.3f + 4.0f * rS.next (rT));
                        const float f0 = down ? fLo * span * std::pow (1.0f / span, p) : fLo * std::pow (span, p);
                        const float q = logMap (qS.next (qT), 0.7f, 12.0f);
                        float l, b, h;
                        flt[0].setG (fastTanPi (std::min (f0, 0.4f * sr), sr), 1.0f / q);
                        flt[0].tick (x, l, b, h);
                        if (filt == 0) y = 1.6f * b / q * std::sqrt (q);
                        else if (filt == 2) y = l;
                        else
                        {
                            float l2, b2, h2;
                            flt[1].setG (fastTanPi (std::min (f0 * 2.6f, 0.4f * sr), sr), 1.0f / q);
                            flt[1].tick (x, l2, b2, h2);
                            y = 1.2f * (b + b2) / q * std::sqrt (q);
                        }
                    }
                    else
                    {
                        const float base = logMap (mS.next (mT), fLo, 1200.0f);
                        const bool on = onset.tick (x);
                        float f0 = base, q = 4.5f;
                        if (m == 0 || m == 1)
                        {
                            const float p = posS.next (std::min (1.0f, env * sens));
                            f0 = m == 0 ? base * std::pow (fHi / base, p) : fHi * std::pow (base / fHi, p);
                        }
                        else if (m == 2)
                        {
                            if (on) senv = 1.0f;
                            senv *= synDec;
                            f0 = std::min (0.4f * sr, base * (1.0f + 8.0f * senv * sS.next (sT))); q = 8.0f;
                        }
                        else if (m == 4)
                        {
                            const float lv = std::tanh (lfoShape * lfo.sine()) / std::tanh (lfoShape);
                            lfo.step();
                            f0 = logMap (0.5f + 0.5f * lv, 300.0f, 2500.0f);
                        }
                        if (m == 3)
                        {
                            // HUMAN VOICE: da una vocale all'altra a ogni nota, con un leggero overdrive
                            if (on) morph = 0;
                            morph = std::min (1.0f, morph + morphStep);
                            const float xd = std::tanh (2.0f * x) * 0.6f;
                            for (int k = 0; k < 3; ++k)
                            {
                                const float fk = vow[v1][k] + morph * (vow[v2][k] - vow[v1][k]);
                                const float qk = 8.0f + 2.0f * (float) k;
                                float l, b, h;
                                flt[k].setG (fastTanPi (fk, sr), 1.0f / qk);
                                flt[k].tick (xd, l, b, h);
                                y += (k == 0 ? 1.0f : k == 1 ? 0.7f : 0.45f) * b / qk * 2.5f;
                            }
                        }
                        else
                        {
                            float l, b, h;
                            flt[0].setG (fastTanPi (std::min (f0, 0.4f * sr), sr), 1.0f / q);
                            flt[0].tick (x, l, b, h);
                            y = m == 2 ? 0.5f * l : 0.15f * x + 1.6f * b / q * std::sqrt (q);
                        }
                    }
                    const float out = g * y + (1.0f - g) * x;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            Config cfg;
            bool dw = false;
            float sr = 48000, fLo = 200, fHi = 3200, env = 0, senv = 0, morph = 1;
            RoleParam pSens, pRange, pPeak, pFilt, pDir, pDecay, pFreq, pMode;
            Svf flt[3];
            Lfo lfo;
            OnsetB onset;
            XFader xf;
            Smooth sS, rS, qS, mS, posS;
        };

        //==============================================================================
        /** Altoparlante rotante RM600: modelli 145 e 122 (tromba + tamburo), L16 (deflettore unico tipo Vibratone). */
        class I5Rot final : public Effect
        {
        public:
            explicit I5Rot (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto f = cfg.list ("fast"); fLo = f.size() > 0 ? (float) f[0] : 3.5f; fHi = f.size() > 1 ? (float) f[1] : 8.0f;
                auto s = cfg.list ("slow"); sLo = s.size() > 0 ? (float) s[0] : 0.3f; sHi = s.size() > 1 ? (float) s[1] : 1.8f;
                pFast = role (*this, "fast", 0.5f); pSlow = role (*this, "slow", 0.5f); pDrive = role (*this, "drive", 0.2f);
                pBal = role (*this, "balance", 0.5f); pRamp = role (*this, "ramp", 0.5f); pModel = role (*this, "model", 0.0f);
                pSpeed = role (*this, "speed", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                rot.prepare (s);
                rotInit = rot;
                for (auto& dl : l16) dl.allocate ((int) (0.006 * s) + 8);
                bpL.set (s, 110, 0.7); bpH.set (s, 5000, 0.7); dc.set (s, 20); bright.set (s, 3000);
                xf.prepare (s, 15);
                for (auto* sm : { &drS, &balS }) sm->set (s, 25);
                reset();
            }
            void reset() override
            {
                rot = rotInit; for (auto& dl : l16) dl.clear(); bpL.reset(); bpH.reset(); dc.reset(); bright.reset();
                xf.current = -1; xf.pending = -1; xf.g = 1; first = true; uPh = 0; uV = -1;
                for (auto* sm : { &drS, &balS }) sm->init = false;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool fast = pSpeed.step (*this) > 0;
                const int model = std::clamp (pModel.step (*this), 0, 2);
                const float hornHz = fast ? logMap (pFast.get (*this), fLo, fHi) : logMap (pSlow.get (*this), sLo, sHi);
                static const float tauTab[3] = { 0.6f, 1.2f, 2.5f };              // RAMP: FAST, MED, SLOW (s)
                const float tau = tauTab[std::clamp (pRamp.step (*this), 0, 2)];
                const float drumHz = hornHz * (fast ? 0.89f : 0.83f) * (model == 2 ? 0.95f : 1.0f);
                rot.set (hornHz * (model == 2 ? 1.04f : 1.0f), drumHz, tau, tau * 3.5f, pBal.get (*this));
                if (first) { rot.jump(); uV = drumHz; first = false; }
                const float aU = 1.0f - std::exp (-1.0f / (tau * 2.0f * sr));
                const float drT = pDrive.get (*this), balT = pBal.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (model, changed);
                    if (changed) { rot.reset(); for (auto& dl : l16) dl.clear(); rot.jump(); }
                    float x = monoIn (ch, numCh, i);
                    // drive valvolare (escluso al minimo)
                    const float dr = drS.next (drT);
                    if (dr > 0.002f)
                    {
                        const float k = 1.0f + 30.0f * dr * dr;
                        const float z = k * x;
                        const float y = dc.hp (std::tanh (z + 0.2f * z * z)) / std::max (1.0f, 0.5f * k);
                        x = x + std::min (1.0f, dr * 20.0f) * (y - x);
                    }
                    float l, r;
                    if (xf.current == 1)
                    {
                        // L16: un solo rotore a deflettore su tutta la banda (100 Hz - 5 kHz)
                        uV += (drumHz - uV) * aU;
                        uPh += uV / sr; uPh -= std::floor (uPh);
                        const float v = bpH.lp (bpL.hp (x)) * 1.2f;
                        float o[2];
                        for (int m = 0; m < 2; ++m)
                        {
                            const float ang = twoPi * (float) (uPh + 0.25 * m);
                            l16[m].push (v);
                            o[m] = l16[m].read (2.0f + 0.0004f * sr * (1.0f + std::sin (ang))) * (0.65f + 0.35f * std::cos (ang));
                        }
                        l = o[0]; r = o[1];
                    }
                    else
                    {
                        rot.setBalance (balS.next (balT));
                        rot.tick (x, l, r);
                        if (xf.current == 2) { l += 0.25f * (l - bright.lp (l)); r += 0.25f * (r - bright.lp (r)); }
                    }
                    const float lv = (xf.current == 1 ? 0.75f : 0.6f) * g;     // livello pareggiato col banco
                    l *= lv; r *= lv;
                    if (numCh > 1) { ch[0][i] = l; ch[1][i] = r; }
                    else ch[0][i] = 0.5f * (l + r);
                }
            }
        private:
            Config cfg;
            float sr = 48000, fLo = 3.5f, fHi = 8, sLo = 0.3f, sHi = 1.8f, uV = -1;
            double uPh = 0;
            bool first = true;
            RoleParam pFast, pSlow, pDrive, pBal, pRamp, pModel, pSpeed;
            Rotary rot, rotInit;
            DelayLine l16[2];
            Svf bpL, bpH;
            OnePole dc, bright;
            XFader xf;
            Smooth drS, balS;
        };

        //==============================================================================
        /** FX600 / FX100: chorus, flanger, phaser, delay, tremolo, pitch shifter a due parametri. */
        class I5Fx6 final : public Effect
        {
        public:
            explicit I5Fx6 (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                bw = (float) cfg.num ("bw", 18000);
                pLevel = role (*this, "level", 0.5f); pP1 = role (*this, "par1", 0.4f); pP2 = role (*this, "par2", 0.5f);
                pFx = role (*this, "fx", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& c : cho) c.prepare (s);
                for (auto& f : fl) f.allocate ((int) (0.012 * s) + 8);
                for (auto& p : ph) p.prepare (s);
                for (auto& dd : dly) dd.allocate ((int) (1.05 * s) + 8);
                for (auto& g : shift) g.prepare (s, 50);
                xf.prepare (s, 12);
                for (auto* sm : { &lS, &p1S, &p2S }) sm->set (s, 25);
                tS.set (s, 120);
                for (int c = 0; c < 2; ++c) { aa1[c].set (s, bw, 0.5412); aa2[c].set (s, bw, 1.3066); dlp[c].set (s, 6000); }
                reset();
            }
            void reset() override
            {
                for (auto& c : cho) c.clear();
                for (auto& f : fl) f.clear();
                for (auto& p : ph) p.clear();
                for (auto& dd : dly) dd.clear();
                for (auto& g : shift) g.clear();
                for (int c = 0; c < 2; ++c) { aa1[c].reset(); aa2[c].reset(); dlp[c].reset(); flFb[c] = 0; dFb[c] = 0; }
                lfo.ph = 0; trem.ph = 0;
                xf.current = -1; xf.pending = -1; xf.g = 1;
                for (auto* sm : { &lS, &p1S, &p2S }) sm->init = false;
                tS.unset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = std::clamp (pFx.step (*this), 0, 5);
                const float lT = pLevel.get (*this), p1T = pP1.get (*this), p2T = pP2.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    const int m = xf.current;
                    if (changed) { for (auto& f : fl) f.clear(); for (auto& dd : dly) dd.clear(); flFb[0] = flFb[1] = dFb[0] = dFb[1] = 0; tS.snap (logMap (p1T, 20.0f, 1000.0f) * 0.001f * sr); }
                    const float lv = lS.next (lT), p1 = p1S.next (p1T), p2 = p2S.next (p2T);
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float mono = 0.5f * (inL + inR);
                    float out[2];
                    if (m == 4)
                    {
                        // TREMOLO: PAR 1 velocita' 1-12 Hz, PAR 2 forma (triangolo -> quadra), LEVEL profondita'
                        trem.setHz (sr, logMap (p1, 1.0f, 12.0f));
                        const float sh = 1.0f + 14.0f * p2;
                        const float w = std::tanh (sh * trem.tri()) / std::tanh (sh);
                        trem.step();
                        const float gg = 1.0f - lv * (0.5f + 0.5f * w);
                        out[0] = inL * (g * gg + (1.0f - g)); out[1] = inR * (g * gg + (1.0f - g));
                    }
                    else
                    {
                        float wet[2] { 0, 0 };
                        const float T = m == 3 ? std::max (16.0f, tS.next (logMap (p1, 20.0f, 1000.0f) * 0.001f * sr)) : 0.0f;
                        lfo.setHz (sr, logMap (p1, 0.05f, 6.0f));
                        for (int c = 0; c < 2; ++c)
                        {
                            const float x = mono;
                            float w = 0;
                            switch (m)
                            {
                                case 0: w = cho[c].tick (x, logMap (p1, 0.1f, 5.0f), 7.0f, 0.3f + 4.0f * p2, c ? 0.5 : 0.0); break;
                                case 1:
                                {
                                    const float dms = 0.4f + 0.3f * (1.0f + std::sin (twoPi * (float) (lfo.ph + (c ? 0.5 : 0.0)))) * (0.2f + 7.0f * p2) * 0.5f;
                                    fl[c].push (x + loopSat (0.6f * flFb[c]));
                                    flFb[c] = fl[c].read (std::max (1.0f, dms * 0.001f * sr));
                                    w = flFb[c];
                                    break;
                                }
                                case 2: w = ph[c].tick (x, logMap (p1, 0.05f, 6.0f), p2, 0.5f, 200.0f, 3000.0f, 4); break;
                                case 3:
                                {
                                    const float y = dly[c].read (T);
                                    dFb[c] = dlp[c].lp (y);
                                    dly[c].push (x + loopSat (0.92f * p2 * dFb[c]));
                                    w = y;
                                    break;
                                }
                                default:
                                {
                                    // PITCH SHIFTER: PAR 2 intervallo -12..+12 semitoni, PAR 1 detune fine (+-50 cent)
                                    const float semis = std::round (-12.0f + 24.0f * p2) + (p1 - 0.5f);
                                    shift[c].setRatio (std::exp2 (semis / 12.0f));
                                    w = shift[c].tick (x);
                                    break;
                                }
                            }
                            wet[c] = aa2[c].lp (aa1[c].lp (w));
                        }
                        lfo.step();
                        out[0] = inL + g * lv * wet[0];
                        out[1] = inR + g * lv * wet[1];
                        if (m == 2 || m == 1) { out[0] *= 0.75f; out[1] *= 0.75f; }
                    }
                    if (numCh > 1) { ch[0][i] = out[0]; ch[1][i] = out[1]; }
                    else ch[0][i] = 0.5f * (out[0] + out[1]);
                }
            }
        private:
            Config cfg;
            float sr = 48000, bw = 18000, flFb[2] {}, dFb[2] {};
            RoleParam pLevel, pP1, pP2, pFx;
            ChorusLite cho[2];
            DelayLine fl[2], dly[2];
            PhaserLite ph[2];
            GrainShifter shift[2];
            Svf aa1[2], aa2[2];
            OnePole dlp[2];
            Lfo lfo, trem;
            XFader xf;
            Smooth lS, p1S, p2S;
            Smooth2 tS;
        };

        //==============================================================================
        /** FL600 Flanger Machine (Liqua Flange): 11 modulazioni, TYPE DIGI/ANALOG/TAPE, polarita' del feedback. */
        class I5Flm final : public Effect
        {
        public:
            explicit I5Flm (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                pTime = role (*this, "time", 0.4f); pRate = role (*this, "rate", 0.35f); pFb = role (*this, "feedback", 0.5f);
                pDepth = role (*this, "depth", 0.6f); pMode = role (*this, "mode", 3.0f); pType = role (*this, "type", 1.0f);
                pPol = role (*this, "pol", 1.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& dl : line) dl.allocate ((int) (0.03 * s) + 8);
                for (auto& dl : dry) dl.allocate ((int) (0.01 * s) + 8);
                for (int c = 0; c < 2; ++c) { lp[c].set (s, 6000); }
                xf.prepare (s, 12);
                onset.prepare (s, 0.004f, 0.1);
                for (auto* sm : { &tS, &dS, &fbS, &rndS, &sgS }) sm->set (s, 25);
                posS[0].set (s, 4); posS[1].set (s, 4);
                env.set (s, 3, 150);
                reset();
            }
            void reset() override
            {
                for (auto& dl : line) dl.clear();
                for (auto& dl : dry) dl.clear();
                for (int c = 0; c < 2; ++c) { lp[c].reset(); fb[c] = 0; posS[c].init = false; }
                lfo.ph = 0; trg = 1; rnd = 0; stepHold = 0; noise = Noise {}; env.env = 0;
                onset = OnsetB {}; onset.prepare (sr, 0.004f, 0.1);
                xf.current = -1; xf.pending = -1; xf.g = 1;
                for (auto* sm : { &tS, &dS, &fbS, &rndS, &sgS }) sm->init = false;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = std::clamp (pMode.step (*this), 0, 10) + 16 * std::clamp (pType.step (*this), 0, 2);
                const float rateK = pRate.get (*this);
                const float hz = logMap (rateK, 0.05f, 10.0f);
                const bool still = rateK < 0.02f;
                const float sign = pPol.step (*this) > 0 ? 1.0f : -1.0f;
                const float tT = pTime.get (*this), dT = pDepth.get (*this), fbT = pFb.get (*this);
                lfo.setHz (sr, hz);
                for (int i = 0; i < n; ++i)
                {
                    bool changed = false;
                    const float g = xf.tick (want, changed);
                    if (changed) { for (auto& dl : line) dl.clear(); fb[0] = fb[1] = 0; }
                    const int m = xf.current % 16, type = xf.current / 16;
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    const float e = env.tick (x);
                    if (onset.tick (x)) trg = 0;
                    trg = std::min (1.0f, trg + hz / sr);
                    // forma della modulazione 0..1 per i due canali (il secondo in controfase)
                    float mod[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        const double off = c ? 0.5 : 0.0;
                        float v;
                        switch (m)
                        {
                            case 0: v = 1.0f - trg; break;                                    // TRG DWN
                            case 1: v = trg; break;                                           // TRG UP
                            case 2: case 3: case 6: v = still ? 0.5f : 0.5f + 0.5f * (m == 3 ? lfo.tri (off) : lfo.sine (off)); break;
                            case 4: v = rndS.next (rnd); break;                               // RANDOM (lisciato)
                            case 5: v = stepHold; break;                                      // STEP
                            case 7: v = std::min (1.0f, e * 12.0f); break;                    // ENV UP
                            case 8: v = 1.0f - std::min (1.0f, e * 12.0f); break;             // ENV DOWN
                            case 9: v = still ? 0.5f : 0.5f + 0.5f * lfo.saw (off); break;    // SAW UP
                            default: v = still ? 0.5f : 0.5f - 0.5f * lfo.saw (off); break;   // SAW DOWN
                        }
                        mod[c] = posS[c].next (v);
                    }
                    const double before = lfo.ph;
                    if (! still) lfo.step();
                    if (lfo.ph < before) { rnd = 0.5f + 0.5f * noise.uni(); }
                    if ((int) (lfo.ph * 8.0) != (int) (before * 8.0) || lfo.ph < before) stepHold = (float) ((int) (lfo.ph * 8.0)) / 7.0f;
                    const float tk = tS.next (tT), dk = dS.next (dT), fk = fbS.next (fbT) * 0.92f, sg = sgS.next (sign);
                    const bool chorus = m == 2;
                    const float centre = chorus ? 6.0f + 12.0f * tk : 0.6f + 6.0f * tk;      // ms
                    const float span = chorus ? 4.0f * dk : centre * 0.95f * dk;
                    float wet[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        float dMs = std::max (0.05f, centre - span + 2.0f * span * mod[c]);
                        float in = c == 0 ? (numCh > 1 ? inL : x) : (numCh > 1 ? inR : x);
                        if (type == 1) { fb[c] = lp[c].lp (fb[c]); }
                        line[c].push (in + loopSat (sg * fk * fb[c]));
                        float y;
                        if (type == 2)
                        {
                            // nastro: il diretto passa da una linea fissa, il ramo modulato le gira attorno (passaggio per lo zero)
                            dry[c].push (in);
                            const float d0 = 0.003f * sr;
                            y = line[c].read (std::max (1.0f, (dMs * 0.001f * sr) * 1.0f));
                            const float dd = dry[c].read (d0);
                            fb[c] = y;
                            wet[c] = 0.5f * (dd + sg * y) - 0.5f * in;
                            continue;
                        }
                        y = line[c].read (std::max (1.0f, dMs * 0.001f * sr));
                        if (type == 1) y = 1.1f * softSat (y / 1.1f);
                        fb[c] = y;
                        wet[c] = 0.5f * y - 0.5f * in;
                    }
                    // uscita: meta' diretto e meta' ritardato (pettine pieno); dissolvenza dei cambi sul ramo ritardato
                    const float oL = (numCh > 1 ? inL : x) + g * wet[0], oR = (numCh > 1 ? inR : x) + g * wet[1];
                    if (numCh > 1) { ch[0][i] = oL; ch[1][i] = oR; }
                    else ch[0][i] = 0.5f * (oL + oR);
                }
            }
        private:
            Config cfg;
            float sr = 48000, fb[2] {}, trg = 1, rnd = 0, stepHold = 0;
            RoleParam pTime, pRate, pFb, pDepth, pMode, pType, pPol;
            DelayLine line[2], dry[2];
            OnePole lp[2];
            Lfo lfo;
            Noise noise;
            Envelope env;
            OnsetB onset;
            XFader xf;
            Smooth tS, dS, fbS, rndS, sgS, posS[2];
        };
    }

    std::unique_ptr<Effect> makeI5Time (const ModelDef& d, const std::string& type)
    {
        if (type == "i5cab")   return std::make_unique<I5Cab> (d);
        if (type == "i5cube")  return std::make_unique<I5Cube> (d);
        if (type == "i5dly")   return std::make_unique<I5Delay> (d);
        if (type == "i5wrap")  return std::make_unique<I5Wrap> (d);
        if (type == "i5verb")  return std::make_unique<I5Verb> (d);
        if (type == "i5shift") return std::make_unique<I5Shift> (d);
        if (type == "i5oct")   return std::make_unique<I5Oct> (d);
        if (type == "i5env")   return std::make_unique<I5Env> (d);
        if (type == "i5rot")   return std::make_unique<I5Rot> (d);
        if (type == "i5fx6")   return std::make_unique<I5Fx6> (d);
        if (type == "i5flm")   return std::make_unique<I5Flm> (d);
        return nullptr;
    }
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
