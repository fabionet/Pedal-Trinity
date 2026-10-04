/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Mattoni comuni dei modelli della tappa 3B (modulazioni, delay, riverberi, pitch,
    synth, looper e multi-effetto di priorita' B). Si appoggiano a quelli della
    tappa 3A (FxClassic.h, FxSpectral.h, FxVerbCore.h) senza modificarli.
    Tutta la memoria si alloca in prepare(); nessuna allocazione nel thread audio.

    I modelli si riconoscono per "type=b3..." nella config (makeStage3B), nomi mai
    usati dai pedali precedenti, che quindi restano identici bit per bit.
*/

#pragma once

#include "FxSpectral.h"
#include "FxVerbCore.h"
#include "Circuit.h"

namespace pt::engine
{
    // fabbriche dei singoli file della tappa 3B
    std::unique_ptr<Effect> makeB3Mod (const ModelDef&, const std::string& type);     // FxClassicBMod.cpp
    std::unique_ptr<Effect> makeB3Time (const ModelDef&, const std::string& type);    // FxClassicBTime.cpp
    std::unique_ptr<Effect> makeB3Pitch (const ModelDef&, const std::string& type);   // FxClassicBPitch.cpp
    std::unique_ptr<Effect> makeB3Multi (const ModelDef&, const std::string& type);   // FxClassicBMulti.cpp

    namespace cx
    {
        //==========================================================================
        /** Dissolvenza per i cambi di algoritmo/modo: scende a 0, cambia, risale. */
        struct XFader
        {
            int current = -1, pending = -1;
            float g = 1, step = 0.003f;
            void prepare (double sr, double ms = 12.0) { step = (float) (1.0 / (ms * 0.001 * sr)); }
            /** 'changed' = vero nel campione in cui si applica il nuovo modo. */
            inline float tick (int wanted, bool& changed) noexcept
            {
                changed = false;
                if (current < 0) { current = wanted; changed = true; }
                if (wanted != current) pending = wanted;
                if (pending >= 0)
                {
                    g -= step;
                    if (g <= 0) { g = 0; current = pending; pending = -1; changed = true; }
                }
                else if (g < 1) g = std::min (1.0f, g + step);
                return g;
            }
        };

        inline float monoIn (float* const* ch, int numCh, int i) noexcept { return numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]; }
        inline float pow2 (float x) noexcept { return std::exp2 (x); }
        /** Limitazione morbida degli anelli di retroazione (come i binari degli operazionali): |y| < 2. */
        inline float loopSat (float x) noexcept { return 2.0f * softSat (x * 0.5f); }
        /** Binari d'uscita: lineare fino a 4 V, poi ginocchio morbido che non supera 6 V. */
        inline float outRail (float y) noexcept
        {
            const float a = std::abs (y);
            return a <= 4.0f ? y : std::copysign (4.0f + 2.0f * std::tanh ((a - 4.0f) * 0.5f), y);
        }
        /** Pomello di volume: unita' alla posizione 'unity', guadagno massimo maxG a fine corsa, muto a zero. */
        inline float volKnob (float k, float unity, float maxG) noexcept
        {
            return k <= 0.0f ? 0.0f : std::pow (k / unity, std::log (maxG) / std::log (1.0f / unity));
        }

        //==========================================================================
        /** Biquad RBJ (forma trasposta II) per mensole, picchi e filtri fissi. */
        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            void reset() { z1 = z2 = 0; }
            inline float tick (float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void norm (double B0, double B1, double B2, double A0, double A1, double A2)
            {
                b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
            }
            static double w0 (double sr, double f) { return 2.0 * juce::MathConstants<double>::pi * std::clamp (f, 5.0, 0.47 * sr) / sr; }
            void lowpass (double sr, double f, double q)
            {
                const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
                norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
            }
            void highpass (double sr, double f, double q)
            {
                const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
                norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
            }
            /** Passa-banda a guadagno 0 dB al centro. */
            void bandpass (double sr, double f, double q)
            {
                const double w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
                norm (al, 0, -al, 1 + al, -2 * c, 1 - al);
            }
            void peak (double sr, double f, double q, double db)
            {
                const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2 * q);
                norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
            }
            void lowshelf (double sr, double f, double db, double S = 0.8)
            {
                const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), s = std::sin (w);
                const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), k = 2 * std::sqrt (A) * al;
                norm (A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                      (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
            }
            void highshelf (double sr, double f, double db, double S = 0.8)
            {
                const double A = std::pow (10.0, db / 40.0), w = w0 (sr, f), c = std::cos (w), s = std::sin (w);
                const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), k = 2 * std::sqrt (A) * al;
                norm (A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                      (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
            }
        };

        /** Mensola o picco con guadagno (dB) lisciato campione per campione: niente salti dei coefficienti. */
        struct SmoothEq
        {
            enum Kind { LowShelf, HighShelf, Peak };
            Biquad bq;
            Smooth s;
            Kind kind = Peak;
            double sr = 48000, f = 1000, q = 0.7;
            float cur = 1.0e9f;
            void prepare (double sampleRate, Kind k, double freq, double qq)
            {
                sr = sampleRate; kind = k; f = freq; q = qq; s.set (sampleRate, 25); s.init = false; cur = 1.0e9f; bq.reset();
            }
            void reset() { bq.reset(); }
            inline float tick (float x, float db) noexcept
            {
                const float d = s.next (db);
                if (std::abs (d - cur) > 0.02f)
                {
                    cur = d;
                    if (kind == LowShelf) bq.lowshelf (sr, f, d, q);
                    else if (kind == HighShelf) bq.highshelf (sr, f, d, q);
                    else bq.peak (sr, f, q, d);
                }
                return bq.tick (x);
            }
        };

        /** Bilanciere (tilt) intorno a fc: +-dB opposti sotto e sopra (un polo, somma piatta a 0 dB). */
        struct Tilt
        {
            OnePole lp;
            void set (double sr, double fc) { lp.set (sr, fc); }
            void reset() { lp.reset(); }
            inline float tick (float x, float gLow, float gHigh) noexcept { const float l = lp.lp (x); return gLow * l + gHigh * (x - l); }
        };

        //==========================================================================
        /** Catena BBD completa per chorus/flanger/delay analogici: pre-enfasi, compander,
            anti-alias, BbdLine a clock reale, ricostruzione, de-enfasi, blocco della continua. */
        class BbdChain
        {
        public:
            void prepare (double s, int st, double filterHz, float noiseV, bool compander, bool emphasis, float satV,
                          float lossPerStage = 6.0e-5f, double tauMs = 10.0)
            {
                sr = s; stages = std::max (8, st);
                bbd.allocate (stages);
                bbd.setLoss (std::min (0.35f, (float) stages * lossPerStage));
                bbd.setNoise (noiseV);
                useComp = compander; pre = emphasis; sat = satV;
                comp.set (s, tauMs, tauMs, 0.3f);
                preF.set (s, 3000); deF.set (s, 3000 / 2.5);
                dc.set (s, 22);
                cur = -1; setFilter (filterHz);
                reset();
            }
            void setFilter (double hz)
            {
                if (std::abs (hz - cur) < 1.0) return;
                cur = hz;
                a1.set (sr, hz, 0.5412); a2.set (sr, hz, 1.3066); r1.set (sr, hz, 0.5412); r2.set (sr, hz, 1.3066);
            }
            void reset() { bbd.clear(); a1.reset(); a2.reset(); r1.reset(); r2.reset(); comp.reset(); preF.reset(); deF.reset(); dc.reset(); }
            int numStages() const noexcept { return stages; }
            /** Un campione con ritardo dMs (millisecondi). */
            inline float tick (float x, float dMs) noexcept
            {
                const double cps = (double) stages / (2.0 * std::max (0.05f, dMs) * 0.001) / sr;
                float v = x;
                if (pre) { const float l = preF.lp (v); v = l + 2.5f * (v - l); }
                if (useComp) v = comp.compress (v);
                v = a2.lp (a1.lp (v));
                v = sat * softSat (v / sat);
                float y = bbd.tick (v, cps);
                y = r2.lp (r1.lp (y));
                if (useComp) y = comp.expand (y);
                if (pre) { const float l = deF.lp (y); y = l + (y - l) / 2.5f; }
                return dc.hp (y);
            }
        private:
            double sr = 48000, cur = -1;
            int stages = 1024;
            bool useComp = false, pre = false;
            float sat = 1.2f;
            BbdLine bbd;
            Svf a1, a2, r1, r2;
            OnePole preF, deF, dc;
            Compander comp;
        };

        //==========================================================================
        /** Rivelatore d'attacco: inviluppo veloce contro lento, con periodo refrattario. */
        struct OnsetB
        {
            Envelope fast, slow;
            int refr = 0, refrN = 4800;
            float thr = 0.003f;
            void prepare (double sr, float threshold = 0.003f, double refrS = 0.09) { fast.set (sr, 1, 25); slow.set (sr, 60, 300); refrN = (int) (refrS * sr); thr = threshold; }
            void setThreshold (float t) noexcept { thr = t; }
            inline bool tick (float x) noexcept
            {
                const float f = fast.tick (x), s = slow.tick (x);
                if (refr > 0) { --refr; return false; }
                if (f > thr && f > 1.7f * s + 0.0005f) { refr = refrN; return true; }
                return false;
            }
            float level() const noexcept { return fast.env; }
        };

        /** Detune a ritardo modulato lentamente (+-cent di picco). */
        struct DetunerB
        {
            DelayLine dl;
            Lfo lfo;
            float sr = 48000;
            void prepare (double s, float hz) { sr = (float) s; dl.allocate ((int) (0.04 * s)); lfo.setHz (s, hz); }
            void clear() { dl.clear(); }
            inline float tick (float x, float cents) noexcept
            {
                dl.push (x);
                const float dev = std::exp2 (cents / 1200.0f) - 1.0f;
                const float amp = dev / (2.0f * pi * (float) (lfo.inc * sr)) * sr;
                const float d = 2.0f + amp + amp * lfo.sine();
                lfo.step();
                return dl.read (std::max (1.0f, d));
            }
        };

        //==========================================================================
        /** Ottava k (-3..3) polifonica dal banco di filtri (k = 0: segnale diretto). */
        inline float bankOctave (const SpectralBank& b, float x, int k) noexcept
        {
            if (k == 0) return x;
            k = std::clamp (k, -3, 3);
            return (k > 0 ? b.voice (1 << k, 0) : b.voice (1, -k)) * kBankGain;
        }

        /** Voce trasposta di un intervallo qualsiasi (semitoni, +-36): ottave esatte dal banco,
            resto (+-6 semitoni) dallo shifter granulare sincronizzato. Polifonica. */
        struct PolyVoice
        {
            GrainShifter gr;
            void prepare (double sr, double winMs = 45) { gr.prepare (sr, winMs); }
            void clear() { gr.clear(); }
            inline float tick (const SpectralBank& b, float x, float semis) noexcept
            {
                const int k = std::clamp ((int) std::lround (semis / 12.0f), -3, 3);
                const float res = semis - 12.0f * (float) k;
                const float src = bankOctave (b, x, k);
                gr.setRatio (std::exp2 (res / 12.0f));
                const float g = gr.tick (src);
                return std::abs (res) < 0.004f ? src : g;                 // ottava esatta (o unisono): niente granuli
            }
        };

        /** Bend continuo polifonico (+-36 semitoni): due percorsi (ottava dal banco + shifter
            granulare) assegnati per parita' dell'ottava e incrociati fra 4 e 8 semitoni di resto,
            cosi' il passaggio d'ottava avviene sempre su un percorso a volume zero. */
        struct BendVoice
        {
            GrainShifter gr[2];
            float uw[2] { 0, 0 }, uk = 0.002f, lastRes[2] { 1.0e9f, 1.0e9f };
            void prepare (double sr, double winMs = 40) { for (auto& g : gr) g.prepare (sr, winMs); uk = (float) (1.0 / (0.012 * sr)); }
            void clear() { for (auto& g : gr) g.clear(); uw[0] = uw[1] = 0; }
            inline float tick (const SpectralBank& b, float x, float s) noexcept
            {
                s = std::clamp (s, -36.0f, 36.0f);
                const int kLo = std::clamp ((int) std::floor (s / 12.0f), -3, 3);
                const float r = s - 12.0f * (float) kLo;
                const float t = std::clamp ((r - 4.0f) * 0.25f, 0.0f, 1.0f), w = t * t * (3.0f - 2.0f * t);
                auto path = [&] (int k, float res) noexcept
                {
                    auto& g = gr[k & 1];
                    if (std::abs (res - lastRes[k & 1]) > 1.0e-5f) { lastRes[k & 1] = res; g.setRatio (std::exp2 (res / 12.0f)); }
                    const float src = bankOctave (b, x, k);
                    const float y = g.tick (src);
                    // resto quasi nullo: l'ottava esatta del banco passa diretta (i granuli a rapporto 1 pettinano i gravi);
                    // il passaggio diretto <-> granuli e' una dissolvenza a tempo (12 ms), anche durante i bend veloci
                    float& u = uw[k & 1];
                    u = std::abs (res) > 0.015f ? std::min (1.0f, u + uk) : std::max (0.0f, u - uk);
                    return src + u * u * (3.0f - 2.0f * u) * (y - src);
                };
                const float lo = path (kLo, r), hi = path (kLo + 1, r - 12.0f);
                return (1.0f - w) * lo + w * hi;
            }
        };

        //==========================================================================
        /** TD-PSOLA (copia del motore del Voice Box della tappa 3A): granuli di due periodi presi
            in ingresso e riposati a passo T/r; fmt stira il granulo (formanti). */
        class PsolaB
        {
        public:
            void prepare (double s)
            {
                sr = (float) s;
                lat = (int) (0.03 * s);
                in.allocate ((int) (0.2 * s));
                acc.assign ((size_t) accSize, 0.0f);
                clear();
            }
            void clear() { in.clear(); std::fill (acc.begin(), acc.end(), 0.0f); rd = 0; nextSyn = 0; }
            inline float tick (float x, float f0, float ratio, float fmt) noexcept
            {
                in.push (x);
                const float T = f0 > 50 ? sr / f0 : sr / 140.0f;
                while (nextSyn <= 0)
                {
                    spawn (T, fmt);
                    nextSyn += T / std::max (0.25f, ratio);
                }
                nextSyn -= 1.0f;
                const float y = acc[(size_t) rd];
                acc[(size_t) rd] = 0;
                rd = (rd + 1) & (accSize - 1);
                return y;
            }
            int latency() const { return lat; }
        private:
            void spawn (float T, float fmt) noexcept
            {
                const int len = std::min ((int) (2.0f * T), accSize / 2 - 4);
                float best = -1; int bestOff = 0;
                const int search = (int) (0.5f * T);
                for (int o = -search; o <= search; o += 2)
                {
                    const float v = in.read ((float) (lat + o));
                    if (v > best) { best = v; bestOff = o; }
                }
                const float centre = (float) (lat + bestOff);
                const float half = 0.5f * (float) len;
                for (int k = 0; k < len; ++k)
                {
                    const float t = ((float) k - half) * fmt;
                    const float w = 0.5f + 0.5f * std::cos (pi * ((float) k - half) / half);
                    const float d = centre - t;
                    acc[(size_t) ((rd + k) & (accSize - 1))] += w * in.read (std::max (1.0f, d));
                }
            }
            static constexpr int accSize = 8192;
            float sr = 48000, nextSyn = 0;
            int rd = 0, lat = 900;
            DelayLine in;
            std::vector<float> acc;
        };

        /** Nota monofonica stabile dal banco (tenuta nel rilascio). */
        struct MonoPitchB
        {
            float held = 0;
            inline float tick (const SpectralBank& bank, float rel = 0.25f) noexcept
            {
                const float p = bankPitch (bank, rel);
                if (p > 30 && p < 2500) held = p;
                return held;
            }
        };

        //==========================================================================
        /** Circuito analogico a stadi (motore Circuit) dentro un effetto: netlist e comandi propri.
            Elabora un buffer mono in place; i comandi si impostano con set(). */
        class SubCircuit
        {
        public:
            SubCircuit() = default;
            SubCircuit (const SubCircuit&) = delete;
            SubCircuit& operator= (const SubCircuit&) = delete;
            /** netlist + valori iniziali; steps[k] = posizioni dei selettori (0 = continuo). */
            void init (const char* netlist, std::initializer_list<float> defaults, std::initializer_list<int> steps = {})
            {
                def = ModelDef {};
                def.id = "sub"; def.name = "sub"; def.code = "sub"; def.inspiredBy = ""; def.category = "";
                def.family = Family::Circuit; def.config = netlist; def.style = "boss"; def.image = ""; def.notes = "";
                int k = 0;
                for (float v : defaults)
                {
                    if (k >= maxControls) break;
                    const int st = k < (int) steps.size() ? *(steps.begin() + k) : 0;
                    def.controls[k] = ControlDef { "", "", st > 1 ? ControlKind::Selector : ControlKind::Knob, 2, v, (uint8_t) st,
                                                   Units::Dial, 0.0f, 1.0f, nullptr, 0, 0, 0, 0, 0 };
                    ++k;
                }
                def.numControls = k;
                fx = std::make_unique<CircuitEffect> (def);
            }
            void prepare (double sr, int maxBlock) { if (fx) fx->prepare (sr, std::max (16, maxBlock)); }
            void reset() { if (fx) fx->reset(); }
            void set (int k, float v) noexcept { if (fx && k < def.numControls) fx->params[k].store (v); }
            void process (float* x, int n) noexcept { if (fx) { float* chs[1] = { x }; fx->process (chs, 1, n); } }
            bool ok() const { return fx != nullptr && fx->error.empty(); }
        private:
            ModelDef def {};
            std::unique_ptr<CircuitEffect> fx;
        };

        //==========================================================================
        /** Flanger + chorus digitali con Filter Matrix (Stereo Electric Mistress digitale, Neo
            Mistress, sezione dell'Epitome): sotto la soglia del RATE l'LFO si ferma e il pomello
            diventa la posizione manuale del pettine. Uscite stereo con LFO in controfase. */
        struct MistressEngine
        {
            DelayLine fl[2], chl[2];
            Lfo lfo, lfoC;
            Smooth posS[2], rateS;
            float fb[2] {}, sr = 48000;
            void prepare (double s)
            {
                sr = (float) s;
                for (auto& d : fl) d.allocate ((int) (0.02 * s));
                for (auto& d : chl) d.allocate ((int) (0.04 * s));
                for (auto& p : posS) p.set (s, 20);
                rateS.set (s, 40);
                reset();
            }
            void reset() { for (auto& d : fl) d.clear(); for (auto& d : chl) d.clear(); fb[0] = fb[1] = 0; lfo.ph = 0.25; lfoC.ph = 0; }
            /** rateK 0..1 (sotto matrixAt = Filter Matrix manuale), profondita' flanger/chorus 0..1,
                feedback 0..1 (0,93 = 'maximum color'), escursione dell'LFO 0..1. */
            inline void tick (float x, float rateK, float matrixAt, float flDepth, float chDepth, float fbK, float sweep,
                              float& outL, float& outR) noexcept
            {
                const bool matrix = rateK < matrixAt;
                const float hz = rateS.next (logMap (matrix ? 0.0f : (rateK - matrixAt) / (1.0f - matrixAt), 0.05f, 6.0f));
                lfo.setHz (sr, hz); lfoC.setHz (sr, hz * 1.17f);
                float wetF[2], wetC[2];
                for (int c = 0; c < 2; ++c)
                {
                    float pos = matrix ? rateK / matrixAt : 0.5f + 0.5f * sweep * lfo.tri (c ? 0.5 : 0.0);
                    pos = posS[c].next (pos);
                    // flanger 0,25..9 ms esponenziale (pettine da 110 Hz a 4 kHz)
                    const float dMs = 0.25f * std::pow (36.0f, 1.0f - pos);
                    fl[c].push (x + loopSat (fbK * fb[c]));
                    fb[c] = fl[c].read (dMs * 0.001f * sr);
                    wetF[c] = fb[c];
                    // chorus 6..14 ms
                    chl[c].push (x);
                    wetC[c] = chl[c].read ((0.010f + 0.004f * lfoC.sine (c ? 0.25 : 0.0)) * sr);
                }
                if (! matrix) { lfo.step(); lfoC.step(); }
                outL = x + flDepth * wetF[0] + chDepth * 0.8f * wetC[0];
                outR = x + flDepth * wetF[1] + chDepth * 0.8f * wetC[1];
                const float n = 1.0f / (1.0f + 0.5f * flDepth + 0.3f * chDepth);
                outL *= n; outR *= n;
            }
        };
    }
}
