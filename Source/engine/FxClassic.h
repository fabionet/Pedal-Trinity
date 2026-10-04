/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Mattoni comuni dei modelli della tappa 3 (modulazioni analogiche a circuito,
    delay a BBD dettagliati, delay/riverberi digitali multimodo, pitch e synth
    polifonici). Tutto header-only, senza allocazioni nel thread audio: la
    memoria si alloca in prepare().

    Questi modelli si agganciano alle famiglie esistenti con valori nuovi di
    "type=" nella config; makeClassic() li riconosce prima dello switch di
    createEffect() e restituisce nullptr per tutti gli altri modelli, che quindi
    restano identici bit per bit.
*/

#pragma once

#include "FxCommon.h"
#include "Families.h"
#include <array>

namespace pt::engine
{
    /** Effetti della tappa 3: nullptr se la config non e' di questo gruppo. */
    std::unique_ptr<Effect> makeClassic (const ModelDef&);

    // fabbriche dei singoli file
    std::unique_ptr<Effect> makeClassicMod (const ModelDef&, const std::string& type);     // FxClassicMod.cpp
    std::unique_ptr<Effect> makeClassicTime (const ModelDef&, const std::string& type);    // FxClassicTime.cpp
    std::unique_ptr<Effect> makePolyPitch (const ModelDef&, const std::string& type);      // FxPolyPitch.cpp
    std::unique_ptr<Effect> makeInstrument (const ModelDef&, const std::string& type);     // FxInstrument.cpp
    /** Tappa 3B: tipi nuovi riconosciuti per nome (FxClassicB*.cpp), nullptr per tutti gli altri. */
    std::unique_ptr<Effect> makeStage3B (const ModelDef&, const std::string& type);         // FxClassicB.cpp

    namespace cx
    {
        constexpr float pi = 3.14159265358979f;

        //==========================================================================
        /** Mappa esponenziale a tre punti (estremi e centro corsa). */
        inline float map3 (float x, float lo, float mid, float hi) noexcept
        {
            x = std::clamp (x, 0.0f, 1.0f);
            return x < 0.5f ? lo * std::pow (mid / lo, 2.0f * x) : mid * std::pow (hi / mid, 2.0f * x - 1.0f);
        }
        /** Curva C (logaritmica inversa) dei potenziometri. */
        inline float taperC (float x) noexcept { return 1.0f - taperA (1.0f - x); }

        /** Parametro lisciato (one-pole) per evitare scatti sui comandi continui. */
        struct Smooth
        {
            float y = 0, a = 0.999f;
            bool init = false;
            void set (double sr, double ms) { a = (float) std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sr)); }
            void snap (float v) { y = v; init = true; }
            inline float next (float t) noexcept
            {
                if (! init) { y = t; init = true; }
                y = t + a * (y - t);
                return y;
            }
        };

        /** tan(pi f / fs) approssimata per filtri modulati (errore < 0,1% sotto fs/4). */
        inline float fastTanPi (float f, float sr) noexcept
        {
            const float w = pi * std::min (f, 0.45f * sr) / sr;
            const float w2 = w * w;
            return w * (15.0f - w2) / (15.0f - 6.0f * w2);      // Pade' [3/2]: errore < 0,1% sotto fs/4, ~6% a 0,45 fs
        }

        /** Filtro a un polo TPT (passa-basso; passa-alto = x - lp). */
        struct OnePole
        {
            float G = 0.5f, s = 0;
            void set (double sr, double fc)
            {
                const double g = std::tan (juce::MathConstants<double>::pi * std::clamp (fc, 1.0, sr * 0.49) / sr);
                G = (float) (g / (1.0 + g));
            }
            /** Variante veloce per tagli modulati campione per campione. */
            inline void setFast (float sr, float fc) noexcept { const float g = fastTanPi (std::max (1.0f, fc), sr); G = g / (1.0f + g); }
            void reset() { s = 0; }
            inline float lp (float x) noexcept { const float v = (x - s) * G; const float y = v + s; s = y + v; return y; }
            inline float hp (float x) noexcept { return x - lp (x); }
        };

        /** Filtro di secondo ordine SVF a un canale (low/band/high). */
        struct Svf
        {
            float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
            void set (double sr, double f, double q)
            {
                g = (float) std::tan (juce::MathConstants<double>::pi * std::clamp (f, 5.0, sr * 0.47) / sr);
                k = (float) (1.0 / std::max (0.05, q));
                a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
            }
            /** Coefficienti veloci per filtri modulati campione per campione (tan gia' calcolata). */
            inline void setG (float gg, float kk) noexcept { g = gg; k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
            void reset() { ic1 = ic2 = 0; }
            inline void tick (float x, float& lp, float& bp, float& hp) noexcept
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
                lp = v2; bp = v1; hp = x - k * v1 - v2;
            }
            inline float lp (float x) noexcept { float l, b, h; tick (x, l, b, h); return l; }
            inline float bp (float x) noexcept { float l, b, h; tick (x, l, b, h); return b; }
            inline float hp (float x) noexcept { float l, b, h; tick (x, l, b, h); return h; }
        };

        /** Cascata di filtri analogici descritta in config: "f:q,f:q,..." (q = 0 -> polo RC). */
        struct FilterChain
        {
            static constexpr int maxN = 6;
            Svf svf[maxN];
            OnePole rc[maxN];
            bool isRc[maxN] {};
            int n = 0;
            void configure (double sr, const std::string& spec, double fallbackHz)
            {
                n = 0;
                size_t i = 0;
                while (i < spec.size() && n < maxN)
                {
                    auto j = spec.find (',', i); if (j == std::string::npos) j = spec.size();
                    const auto tok = spec.substr (i, j - i);
                    const auto c = tok.find (':');
                    const double f = std::atof (tok.c_str());
                    const double q = c == std::string::npos ? 0.0 : std::atof (tok.c_str() + c + 1);
                    if (f > 0)
                    {
                        isRc[n] = q <= 0.0;
                        if (isRc[n]) rc[n].set (sr, f); else svf[n].set (sr, f, q);
                        ++n;
                    }
                    i = j + 1;
                }
                if (n == 0) { isRc[0] = false; svf[0].set (sr, fallbackHz, 0.707); n = 1; }
            }
            void reset() { for (auto& s : svf) s.reset(); for (auto& r : rc) r.reset(); }
            inline float tick (float x) noexcept
            {
                for (int k = 0; k < n; ++k) x = isRc[k] ? rc[k].lp (x) : svf[k].lp (x);
                return x;
            }
        };

        /** Generatore di rumore deterministico (xorshift). */
        struct Noise
        {
            uint32_t s = 0x9E3779B9u;
            inline float uni() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * 4.6566129e-10f; }
            inline float gauss() noexcept { return (uni() + uni() + uni()) * 1.0f; }
        };

        //==========================================================================
        /** Oscillatore di bassa frequenza con le forme dei circuiti reali. */
        struct Lfo
        {
            double ph = 0, inc = 0;
            void setHz (double sr, double hz) { inc = hz / sr; }
            inline void step() noexcept { ph += inc; ph -= std::floor (ph); }
            inline float tri (double off = 0) const noexcept { double p = ph + off; p -= std::floor (p); return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }
            inline float sine (double off = 0) const noexcept { return (float) std::sin ((ph + off) * 6.283185307179586); }
            inline float saw (double off = 0) const noexcept { double p = ph + off; p -= std::floor (p); return (float) (2.0 * p - 1.0); }
            inline float square (double off = 0) const noexcept { double p = ph + off; p -= std::floor (p); return p < 0.5 ? 1.0f : -1.0f; }
            /** Triangolo a simmetria variabile: skew 0 = dente crescente, 0.5 = triangolo, 1 = decrescente. */
            inline float skew (float sk, double off = 0) const noexcept
            {
                double p = ph + off; p -= std::floor (p);
                const double s = std::clamp ((double) sk, 0.001, 0.999);
                return (float) (p < s ? -1.0 + 2.0 * p / s : 1.0 - 2.0 * (p - s) / (1.0 - s));
            }
        };

        /** Oscillatore a rilassamento RC (condensatore tra due soglie): triangolo con lati curvi.
            h = semi-distanza delle soglie dal centro (0..0.45); h piccolo = triangolo quasi lineare. */
        struct RelaxOsc
        {
            float v = 0.5f, k = 0.01f, h = 0.2f;
            bool up = true;
            void set (double sr, double hz, float hh)
            {
                h = std::clamp (hh, 0.02f, 0.45f);
                const double half = 0.5 / std::max (1.0e-3, hz);                      // durata di un lato
                const double rc = half / std::log ((0.5 + h) / (0.5 - h));
                k = (float) (1.0 - std::exp (-1.0 / (rc * sr)));
            }
            /** Uscita normalizzata -1..1. */
            inline float tick() noexcept
            {
                v += ((up ? 1.0f : 0.0f) - v) * k;
                if (up && v >= 0.5f + h) up = false;
                else if (! up && v <= 0.5f - h) up = true;
                return (v - 0.5f) / h;
            }
        };

        //==========================================================================
        /** Linea BBD: N stadi a due fasi, N/2 celle che avanzano a ogni periodo di clock,
            campionamento dell'ingresso all'istante del clock (interpolazione lineare),
            uscita tenuta (ZOH) integrata sull'intervallo del campione audio,
            perdita di trasferimento N*eps (passa-basso nel dominio del clock),
            rumore e saturazione della cella. */
        class BbdLine
        {
        public:
            void allocate (int stages)
            {
                n = std::max (4, stages / 2);
                cells.assign ((size_t) n, 0.0f);
                clear();
            }
            void clear()
            {
                std::fill (cells.begin(), cells.end(), 0.0f);
                w = 0; tnext = 0; held = 0; prevIn = 0;
            }
            void setLoss (float nEps) { loss = std::clamp (nEps, 0.0f, 0.6f); }
            void setNoise (float rmsV) { noiseAmp = rmsV; }
            int cellsCount() const { return n; }

            /** cps = f_clock / fs (periodi di clock per campione audio). */
            inline float tick (float in, double cps) noexcept
            {
                const double period = 1.0 / std::max (1.0e-4, cps);
                double t = 0, acc = 0;
                int guard = 0;
                while (tnext < 1.0 && guard++ < 256)
                {
                    acc += held * (tnext - t);
                    t = tnext;
                    const float x = prevIn + (in - prevIn) * (float) tnext;
                    const float out = cells[(size_t) w];
                    cells[(size_t) w] = x + (noiseAmp > 0 ? noiseAmp * noise.gauss() : 0.0f);
                    w = (w + 1 == n) ? 0 : w + 1;
                    held = (1.0f - loss) * out + loss * held;
                    tnext += period;
                }
                acc += held * (1.0 - t);
                tnext -= 1.0;
                if (tnext < 0) tnext = 0;
                prevIn = in;
                return (float) acc;
            }

        private:
            std::vector<float> cells;
            int n = 512, w = 0;
            double tnext = 0;
            float held = 0, prevIn = 0, loss = 0.05f, noiseAmp = 0;
            Noise noise;
        };

        /** Compander NE570/SA571: compressore 2:1 (raddrizzatore a valor medio, gain = sqrt(ref/env))
            ed espansore 1:2 (gain = env/ref). Costanti di tempo separate come i condensatori reali. */
        struct Compander
        {
            float envC = 0, envE = 0, aC = 0.99f, aE = 0.99f, ref = 0.3f, floorC = 3.0e-4f;
            void set (double sr, double tauCms, double tauEms, float reference = 0.3f)
            {
                aC = (float) std::exp (-1.0 / (tauCms * 0.001 * sr));
                aE = (float) std::exp (-1.0 / (tauEms * 0.001 * sr));
                ref = reference; floorC = reference * 1.0e-3f;
            }
            void reset() { envC = envE = 0; }
            inline float compress (float x) noexcept
            {
                envC = std::abs (x) + aC * (envC - std::abs (x));
                return x * std::sqrt (ref * 0.6366f / std::max (envC, floorC));     // 0.6366 = media di |sin|: env -> ampiezza
            }
            inline float expand (float y) noexcept
            {
                envE = std::abs (y) + aE * (envE - std::abs (y));
                return y * envE / (0.6366f * ref);
            }
        };

        /** All-pass del primo ordine (stadio di phaser). */
        struct Allpass1
        {
            float x1 = 0, y1 = 0;
            inline float tick (float x, float a) noexcept { const float y = a * x + x1 - a * y1; x1 = x; y1 = y; return y; }
            void reset() { x1 = y1 = 0; }
        };
        /** Coefficiente dell'all-pass per frequenza d'angolo fc (Hz). */
        inline float apCoef (float fc, float sr) noexcept
        {
            const float t = fastTanPi (fc, sr);
            return (t - 1.0f) / (t + 1.0f);
        }

        /** Fotoresistenza: resistenza in dominio logaritmico con discesa veloce (luce che arriva)
            e risalita lenta (memoria della LDR). light 0..1. */
        struct Ldr
        {
            float logR = 13.0f, aDown = 0.9f, aUp = 0.99f, lnMin = 9.0f, lnMax = 13.0f, gamma = 0.7f;
            void set (double sr, double attackMs, double releaseMs, double rmin, double rmax, float g = 0.7f)
            {
                aDown = (float) std::exp (-1.0 / (attackMs * 0.001 * sr));
                aUp = (float) std::exp (-1.0 / (releaseMs * 0.001 * sr));
                lnMin = (float) std::log (rmin); lnMax = (float) std::log (rmax); gamma = g;
                logR = lnMax;
            }
            inline float tick (float light) noexcept
            {
                const float target = lnMax + (lnMin - lnMax) * std::pow (std::clamp (light, 0.0f, 1.0f), gamma);
                const float a = target < logR ? aDown : aUp;
                logR = target + a * (logR - target);
                return std::exp (logR);
            }
        };

        //==========================================================================
        /** Linea di ritardo semplice con lettura frazionaria lineare (economica per molti tap). */
        class Ring
        {
        public:
            void allocate (int maxSamples)
            {
                size = 1; while (size < maxSamples + 4) size <<= 1;
                mask = size - 1; buf.assign ((size_t) size, 0.0f); w = 0;
            }
            void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
            inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
            /** d >= 1: d = 1 e' l'ultimo campione scritto. */
            inline float read (float d) const noexcept
            {
                d = std::clamp (d, 1.0f, (float) (size - 3));
                const int di = (int) d; const float f = d - (float) di;
                const float a = buf[(size_t) ((w - di) & mask)], b = buf[(size_t) ((w - di - 1) & mask)];
                return a + (b - a) * f;
            }
            inline float at (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
            int capacity() const { return size - 4; }
        private:
            std::vector<float> buf;
            int size = 0, mask = 0, w = 0;
        };

        /** Pitch shifter granulare a due testine sincronizzate (tipo SOLA): ogni testina legge un
            granulo con finestra sin^2 (somma costante); quando una testina riparte, il suo nuovo
            ritardo viene scelto per correlazione con la testina in pieno volume, cosi' le due
            forme d'onda si sovrappongono in fase (niente battimenti/"warble"). Polifonico per
            natura; latenza media circa granulo/2 + ricerca. */
        class GrainShifter
        {
        public:
            void prepare (double sr, double windowMs)
            {
                fs = (float) sr;
                L = (float) (windowMs * 0.001 * sr);
                search = (int) (0.012 * sr);                   // fino a ~83 Hz di periodo
                cw = (int) (0.006 * sr);
                line.allocate ((int) (L * 2.2f) + search + cw + 16);
                clear();
            }
            void clear()
            {
                line.clear();
                age[0] = 0; age[1] = 0.5f * L;
                d[0] = d[1] = startDelay (ratio);
            }
            void setRatio (float r) noexcept { ratio = std::clamp (r, 0.25f, 2.5f); }
            inline float tick (float x) noexcept
            {
                line.push (x);
                float y = 0;
                for (int k = 0; k < 2; ++k)
                {
                    if (age[k] >= L)
                    {
                        age[k] -= L;
                        const int o = k ^ 1;
                        d[k] = align (startDelay (ratio), d[o]);
                    }
                    const float w = std::sin (pi * age[k] / L);
                    y += w * w * line.read (std::max (1.0f, d[k]));
                    d[k] += 1.0f - ratio;
                    age[k] += 1.0f;
                }
                return y;
            }
        private:
            float startDelay (float r) const noexcept { return 2.0f + (r > 1.0f ? (r - 1.0f) * L : 0.0f); }
            /** Ritardo iniziale d0 + delta (0..search) che massimizza la correlazione con la testina o. */
            float align (float d0, float dOther) const noexcept
            {
                float best = -1e30f; int bestD = 0;
                for (int delta = 0; delta <= search; delta += 2)
                {
                    float c = 0;
                    for (int j = 0; j < cw; j += 3)
                        c += line.read (d0 + (float) (delta + j)) * line.read (std::max (1.0f, dOther) + (float) j);
                    if (c > best) { best = c; bestD = delta; }
                }
                return d0 + (float) bestD;
            }
            DelayLine line;
            float fs = 48000, L = 2000, ratio = 1, age[2] {}, d[2] { 2, 2 };
            int search = 576, cw = 288;
        };

        //==========================================================================
        /** Riverbero FDN a 8 linee (Householder), con diffusione d'ingresso, smorzamento
            per linea, modulazione e uscita stereo. size scala le lunghezze (0.3 room .. 2 hall). */
        class Fdn
        {
        public:
            void prepare (double s, float maxSize = 2.2f)
            {
                sr = s;
                static const int base[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
                for (int k = 0; k < 8; ++k)
                {
                    baseLen[k] = base[k] * (float) (sr / 44100.0);
                    lines[k].allocate ((int) (baseLen[k] * maxSize) + 64);
                }
                static const int dl[4] = { 142, 107, 379, 277 };
                for (int k = 0; k < 4; ++k) { difLen[k] = dl[k] * (float) (sr / 44100.0); dif[k].allocate ((int) difLen[k] + 8); }
                lfo.setHz (sr, 0.6);
                clear();
            }
            void clear() { for (auto& l : lines) l.clear(); for (auto& d : dif) d.clear(); std::fill (std::begin (damp), std::end (damp), 0.0f); }
            /** t60 in s (>= 60 = infinito), cutoff Hz dello smorzamento, size relativa, modulazione in campioni. */
            void set (float t60, float cutoff, float size, float modSamples, float diffusion = 0.6f)
            {
                sz = size; mod = modSamples; difG = diffusion;
                dampC = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * std::clamp ((double) cutoff, 200.0, sr * 0.45) / sr);
                for (int k = 0; k < 8; ++k)
                    g[k] = t60 >= 60.0f ? 1.0f : std::pow (10.0f, -3.0f * (baseLen[k] * sz) / (std::max (0.05f, t60) * (float) sr));
                if (t60 >= 60.0f) dampC = std::min (dampC, 0.2f);
            }
            inline void tick (float in, float& outL, float& outR) noexcept
            {
                float v = in;
                for (int k = 0; k < 4; ++k)
                {
                    const float d = dif[k].read (difLen[k]);
                    const float y = -difG * v + d;
                    dif[k].push (v + difG * y);
                    v = y;
                }
                const float m = lfo.sine(); lfo.step();
                float o[8], sum = 0;
                for (int k = 0; k < 8; ++k)
                {
                    const float md = (k & 1 ? mod : -mod) * m * (0.7f + 0.06f * (float) k);
                    o[k] = lines[k].read (baseLen[k] * sz + md);
                    damp[k] = o[k] + dampC * (damp[k] - o[k]);
                    o[k] = damp[k] * g[k];
                    sum += o[k];
                }
                const float h = sum * 0.25f;
                for (int k = 0; k < 8; ++k)
                    lines[k].push (o[k] - h + v * ((k & 1) ? 0.4f : -0.4f));
                outL = 0.5f * (o[0] + o[2] + o[4] + o[6]);
                outR = 0.5f * (o[1] + o[3] + o[5] + o[7]);
            }
        private:
            double sr = 48000;
            DelayLine lines[8], dif[4];
            float baseLen[8] {}, difLen[4] {}, g[8] {}, damp[8] {};
            float dampC = 0.3f, sz = 1, mod = 0, difG = 0.6f;
            Lfo lfo;
        };

        /** Plate di Dattorro (1997) con dimensione, decadimento, smorzamento e modulazione del serbatoio. */
        class Plate
        {
        public:
            void prepare (double s)
            {
                sr = s;
                const double k = sr / 29761.0;
                static const int inLen[4] = { 142, 107, 379, 277 };
                for (int i = 0; i < 4; ++i) { idl[i] = (float) (inLen[i] * k); inAp[i].allocate ((int) idl[i] + 8); }
                static const int tank[8] = { 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
                for (int i = 0; i < 8; ++i) { tl[i] = (float) (tank[i] * k); tk[i].allocate ((int) (tl[i] * 2.2f) + 64); }
                pre.allocate ((int) (sr * 0.1) + 8);
                lfo.setHz (sr, 1.0);
                clear();
            }
            void clear() { for (auto& a : inAp) a.clear(); for (auto& t : tk) t.clear(); pre.clear(); bwS = d1 = d2 = 0; fbL = fbR = 0; }
            void set (float t60, float cutoff, float size, float modExc)
            {
                sz = std::clamp (size, 0.2f, 2.0f);
                // decadimento per giro del serbatoio (~ somma dei ritardi)
                const float loopS = 0.5f * (tl[0] + tl[1] + tl[2] + tl[3]) * sz / (float) sr;   // due guadagni per meta' serbatoio
                decay = t60 >= 60.0f ? 1.0f : std::min (0.995f, std::pow (10.0f, -3.0f * loopS / std::max (0.05f, t60)));
                dampC = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * std::clamp ((double) cutoff, 300.0, sr * 0.45) / sr);
                exc = modExc * (float) (sr / 29761.0);
            }
            inline void tick (float in, float& outL, float& outR) noexcept
            {
                bwS = in + 0.3f * (bwS - in);
                float v = bwS;
                static const float ig[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
                for (int i = 0; i < 4; ++i)
                {
                    const float d = inAp[i].read (idl[i]);
                    const float y = -ig[i] * v + d;
                    inAp[i].push (v + ig[i] * y);
                    v = y;
                }
                const float m = lfo.sine(); lfo.step();
                // ramo sinistro
                float a = v + decay * fbR;
                {
                    const float dd = tk[0].read (tl[0] * sz + exc * m);
                    const float y = 0.7f * a + dd;            // all-pass di diffusione con decay diffusion 1 (segno Dattorro)
                    tk[0].push (a - 0.7f * y);
                    a = y;
                }
                tk[1].push (a);
                float b = tk[1].read (tl[1] * sz);
                d1 = b + dampC * (d1 - b); b = d1 * decay;
                {
                    const float dd = tk[2].read (tl[2] * sz);
                    const float y = -0.5f * b + dd;
                    tk[2].push (b + 0.5f * y);
                    b = y;
                }
                tk[3].push (b);
                fbL = tk[3].read (tl[3] * sz);
                // ramo destro
                float c = v + decay * fbL;
                {
                    const float dd = tk[4].read (tl[4] * sz - exc * m);
                    const float y = 0.7f * c + dd;
                    tk[4].push (c - 0.7f * y);
                    c = y;
                }
                tk[5].push (c);
                float e = tk[5].read (tl[5] * sz);
                d2 = e + dampC * (d2 - e); e = d2 * decay;
                {
                    const float dd = tk[6].read (tl[6] * sz);
                    const float y = -0.5f * e + dd;
                    tk[6].push (e + 0.5f * y);
                    e = y;
                }
                tk[7].push (e);
                fbR = tk[7].read (tl[7] * sz);
                // prese d'uscita (tabella di Dattorro, scalate)
                const float q = (float) (sr / 29761.0) * sz;
                outL = 0.6f * (tk[5].read (266 * q) + tk[5].read (2974 * q) - tk[6].read (1913 * q) + tk[7].read (1996 * q)
                               - tk[1].read (1990 * q) - tk[2].read (187 * q) - tk[3].read (1066 * q));
                outR = 0.6f * (tk[1].read (353 * q) + tk[1].read (3627 * q) - tk[2].read (1228 * q) + tk[3].read (2673 * q)
                               - tk[5].read (2111 * q) - tk[6].read (335 * q) - tk[7].read (121 * q));
            }
        private:
            double sr = 48000;
            DelayLine inAp[4], tk[8], pre;
            float idl[4] {}, tl[8] {};
            float sz = 1, decay = 0.5f, dampC = 0.3f, exc = 8, bwS = 0, d1 = 0, d2 = 0, fbL = 0, fbR = 0;
            Lfo lfo;
        };

        /** Serbatoio a molle: per ogni molla una cascata di all-pass "stirati" (z^-K) che
            ritarda le basse piu' delle alte (chirp discendente), anello di ritorno con
            passa-basso e perdite. springs = 1..3. */
        class SpringTank
        {
        public:
            static constexpr int maxSprings = 3, stages = 48;
            void prepare (double s)
            {
                sr = s;
                K = std::max (1, (int) std::lround (sr / (2.0 * 4300.0)));
                for (int sp = 0; sp < maxSprings; ++sp)
                {
                    xh[sp].assign ((size_t) (stages * K), 0.0f);
                    yh[sp].assign ((size_t) (stages * K), 0.0f);
                    loop[sp].allocate ((int) (sr * 0.12));
                }
                clear();
            }
            void clear()
            {
                for (int sp = 0; sp < maxSprings; ++sp)
                {
                    std::fill (xh[sp].begin(), xh[sp].end(), 0.0f); std::fill (yh[sp].begin(), yh[sp].end(), 0.0f);
                    loop[sp].clear(); lp[sp].reset(); hp[sp].reset(); last[sp] = 0;
                }
                idx = 0;
            }
            /** decay = guadagno d'anello (0..0.95), tone = taglio Hz, n = numero di molle, a = dispersione (-0.4..-0.7). */
            void set (float decayGain, float toneHz, int nSprings, float disp = -0.6f)
            {
                fb = std::clamp (decayGain, 0.0f, 0.97f);
                ns = std::clamp (nSprings, 1, maxSprings);
                a = disp;
                for (int sp = 0; sp < maxSprings; ++sp) { lp[sp].set (sr, toneHz); hp[sp].set (sr, 70.0 + 30.0 * sp); }
            }
            inline void tick (float in, float& outL, float& outR) noexcept
            {
                static const float lens[maxSprings] = { 0.0331f, 0.0389f, 0.0437f };
                float sumL = 0, sumR = 0;
                const int kk = idx;          // posizione nel buffer circolare di lunghezza K per stadio
                for (int sp = 0; sp < ns; ++sp)
                {
                    float v = in + fb * last[sp];
                    float* xs = xh[sp].data();
                    float* ys = yh[sp].data();
                    for (int st = 0; st < stages; ++st)
                    {
                        const int p = st * K + kk;
                        const float xd = xs[p], yd = ys[p];
                        const float y = a * v + xd - a * yd;      // H(z) = (a + z^-K) / (1 + a z^-K)
                        xs[p] = v; ys[p] = y;
                        v = y;
                    }
                    v = lp[sp].lp (v);
                    v = hp[sp].hp (v);
                    loop[sp].push (v);
                    last[sp] = loop[sp].read ((float) (lens[sp] * sr));
                    if (sp & 1) sumR += v; else sumL += v;
                    if (sp == 2) sumR += 0.5f * v;
                }
                idx = (idx + 1) % K;
                if (ns == 1) sumR = sumL;
                outL = sumL; outR = sumR;
            }
        private:
            double sr = 48000;
            int K = 5, idx = 0, ns = 2;
            float fb = 0.7f, a = -0.6f;
            std::vector<float> xh[maxSprings], yh[maxSprings];
            Ring loop[maxSprings];
            OnePole lp[maxSprings], hp[maxSprings];
            float last[maxSprings] {};
        };

        //==========================================================================
        /** Cassa rotante (tromba + rotore): crossover 800 Hz, Doppler della tromba con ritardo
            modulato, diagrammi direzionali, due microfoni a 90 gradi, inerzie separate. */
        class Rotary
        {
        public:
            void prepare (double s)
            {
                sr = s;
                cross.set (sr, 800.0, 0.707);
                for (auto& d : hornDl) d.allocate ((int) (sr * 0.004) + 8);
                for (auto& d : drumDl) d.allocate ((int) (sr * 0.004) + 8);
                for (auto& f : hornLp) f.set (sr, 7000.0);
                reset();
            }
            void reset()
            {
                cross.reset();
                for (auto& d : hornDl) d.clear();
                for (auto& d : drumDl) d.clear();
                hornPh = 0; drumPh = 0.3;
            }
            /** Velocita' bersaglio (Hz), tempi di accelerazione (s) di tromba e rotore, bilanciamento 0..1. */
            void set (double hornHz, double drumHz, double hornTau, double drumTau, float balance, float depth = 1.0f)
            {
                hornT = hornHz; drumT = drumHz;
                aH = 1.0 - std::exp (-1.0 / (std::max (0.02, hornTau) * sr));
                aD = 1.0 - std::exp (-1.0 / (std::max (0.02, drumTau) * sr));
                bal = balance; dep = depth;
            }
            void jump() { hornV = hornT; drumV = drumT; }
            void setBalance (float b) noexcept { bal = b; }
            inline void tick (float x, float& l, float& r) noexcept
            {
                hornV += (hornT - hornV) * aH;
                drumV += (drumT - drumV) * aD;
                hornPh += hornV / sr; hornPh -= std::floor (hornPh);
                drumPh += drumV / sr; drumPh -= std::floor (drumPh);
                float lo, bp, hi;
                cross.tick (x, lo, bp, hi);
                hi = x - lo;                                // complementare: somma piatta a rotore fermo
                const float hornGain = 1.25f * std::min (1.0f, 2.0f * bal), drumGain = std::min (1.0f, 2.0f - 2.0f * bal);
                const float base = (float) (0.00045 * sr) * dep;  // escursione Doppler 0,45 ms (raggio 15 cm)
                hornDl[0].push (hi); drumDl[0].push (lo);
                float out[2];
                for (int m = 0; m < 2; ++m)
                {
                    const float ang = 6.2831853f * (float) (hornPh + 0.25 * m);
                    const float c = std::cos (ang), sn = std::sin (ang);
                    const float d = 2.0f + base * (1.0f + sn);
                    float h = hornDl[0].read (d);
                    h *= 0.62f + 0.38f * dep * c;                 // tromba verso il microfono = piu' forte
                    hornLp[m].setFast ((float) sr, 4500.0f + 4000.0f * (1.0f + c * dep));
                    h = hornLp[m].lp (h);
                    const float angD = 6.2831853f * (float) (drumPh + 0.25 * m);
                    const float cd = std::cos (angD), sd = std::sin (angD);
                    float dr = drumDl[0].read (2.0f + 0.35f * base * (1.0f + sd));
                    dr *= 0.78f + 0.22f * dep * cd;
                    out[m] = hornGain * h + drumGain * dr;
                }
                l = out[0]; r = out[1];
            }
            double hornHzNow() const { return hornV; }
        private:
            double sr = 48000, hornPh = 0, drumPh = 0, hornV = 0.8, drumV = 0.7, hornT = 0.8, drumT = 0.7, aH = 1e-4, aD = 1e-5;
            float bal = 0.5f, dep = 1.0f;
            Svf cross;
            DelayLine hornDl[1], drumDl[1];
            OnePole hornLp[2];
        };
    }
}
