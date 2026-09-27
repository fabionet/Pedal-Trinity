/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Utilita' comuni alle famiglie DSP: configurazione del modello, ruoli dei
    comandi, filtri (TPT state-variable, biquad), linee di ritardo, LFO,
    inseguitori d'inviluppo. Tutto senza allocazioni nel thread audio.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include "Effect.h"
#include "../dsp/Filters.h"

namespace pt::engine
{
    constexpr float twoPi = 6.283185307179586f;

    //==============================================================================
    /** Configurazione "chiave=valore chiave=v1,v2,..." di un modello. */
    class Config
    {
    public:
        explicit Config (const char* text)
        {
            std::string s = text != nullptr ? text : "";
            size_t i = 0;
            while (i < s.size())
            {
                while (i < s.size() && (s[i] == ' ' || s[i] == ';' || s[i] == '\n')) ++i;
                size_t j = i;
                while (j < s.size() && s[j] != ' ' && s[j] != ';' && s[j] != '\n') ++j;
                const auto tok = s.substr (i, j - i);
                const auto eq = tok.find ('=');
                if (eq != std::string::npos)
                    values[tok.substr (0, eq)] = tok.substr (eq + 1);
                i = j;
            }
        }

        double num (const char* key, double fallback) const
        {
            auto it = values.find (key);
            if (it == values.end()) return fallback;
            return std::atof (it->second.c_str());
        }

        std::vector<double> list (const char* key) const
        {
            std::vector<double> out;
            auto it = values.find (key);
            if (it == values.end()) return out;
            const auto& v = it->second;
            size_t i = 0;
            while (i <= v.size())
            {
                auto j = v.find (',', i);
                if (j == std::string::npos) j = v.size();
                if (j > i) out.push_back (std::atof (v.substr (i, j - i).c_str()));
                i = j + 1;
            }
            return out;
        }

        std::string str (const char* key, const char* fallback = "") const
        {
            auto it = values.find (key);
            return it == values.end() ? std::string (fallback) : it->second;
        }

    private:
        std::map<std::string, std::string> values;
    };

    //==============================================================================
    /** Accesso ai comandi per ruolo (risolto una volta alla costruzione). */
    class Roles
    {
    public:
        explicit Roles (const ModelDef& d) : def (d) {}
        int index (const char* role) const
        {
            for (int i = 0; i < def.numControls; ++i)
                if (def.controls[i].role != nullptr && std::strcmp (def.controls[i].role, role) == 0)
                    return i;
            return -1;
        }
    private:
        const ModelDef& def;
    };

    /** Parametro letto per ruolo, con valore di riserva se il pedale non ha quel comando. */
    struct RoleParam
    {
        int idx = -1;
        float fallback = 0.5f;
        float get (const Effect& e) const noexcept { return idx >= 0 ? e.p (idx) : fallback; }
        int step (const Effect& e) const noexcept { return idx >= 0 ? e.step (idx) : (int) fallback; }
        bool present() const noexcept { return idx >= 0; }
    };

    inline RoleParam role (const Effect& e, const char* name, float fallback = 0.5f)
    {
        return { Roles (e.def).index (name), fallback };
    }

    //==============================================================================
    /** Filtro state-variable TPT (Zavalishin / Cytomic): stabile anche modulato. */
    struct SVF
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        float ic1[2] {}, ic2[2] {};

        void set (double sr, double f, double q)
        {
            g = (float) std::tan (juce::MathConstants<double>::pi * std::clamp (f, 10.0, sr * 0.45) / sr);
            k = (float) (1.0 / std::max (0.05, q));
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
        /** Restituisce low, band, high. */
        inline void tick (int ch, float x, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch];
            ic2[ch] = 2 * v2 - ic2[ch];
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    //==============================================================================
    /** Linea di ritardo circolare con lettura frazionaria (Hermite a 4 punti). */
    class DelayLine
    {
    public:
        void allocate (int maxSamples)
        {
            size = 1;
            while (size < maxSamples + 4) size <<= 1;
            mask = size - 1;
            buf.assign ((size_t) size, 0.0f);
            w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
        /** Uscita ritardata di d campioni (d >= 1) rispetto all'ultimo push:
            read(1) = campione appena precedente all'ultimo scritto. */
        inline float read (float d) const noexcept
        {
            d = std::clamp (d, 1.0f, (float) (size - 4));
            const int di = (int) d;
            const float f = d - (float) di;
            // at(1) = ultimo campione scritto; il punto cercato sta tra at(di+1) e at(di+2)
            auto at = [this] (int k) { return buf[(size_t) ((w - k) & mask)]; };
            const float p0 = at (di), p1 = at (di + 1), p2 = at (di + 2), p3 = at (di + 3);
            const float c1 = 0.5f * (p2 - p0);
            const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
            const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
            return ((c3 * f + c2) * f + c1) * f + p1;   // interpolazione di Hermite
        }
        int capacity() const { return size - 4; }

    private:
        std::vector<float> buf;
        int size = 0, mask = 0, w = 0;
    };

    //==============================================================================
    struct LFO
    {
        double phase = 0, inc = 0;
        void setRate (double sr, double hz) { inc = hz / sr; }
        /** forma: 0 triangolo, 1 sinusoide, 2 quadra arrotondata, 3 dente di sega */
        inline float next (int shape, double offset = 0.0) noexcept
        {
            double ph = phase + offset;
            ph -= std::floor (ph);
            float v;
            switch (shape)
            {
                case 1:  v = (float) std::sin (ph * 6.283185307179586); break;
                case 2:  v = (float) std::tanh (6.0 * std::sin (ph * 6.283185307179586)) / 0.99999f; break;
                case 3:  v = (float) (2.0 * ph - 1.0); break;
                default: v = (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
            }
            return v;
        }
        inline void advance (int n = 1) noexcept { phase += inc * n; phase -= std::floor (phase); }
    };

    //==============================================================================
    /** Inseguitore d'inviluppo tipo raddrizzatore + RC (attacco/rilascio in ms). */
    struct Envelope
    {
        float a = 0, r = 0, env = 0;
        void set (double sr, double attackMs, double releaseMs)
        {
            a = (float) std::exp (-1.0 / (std::max (0.01, attackMs) * 0.001 * sr));
            r = (float) std::exp (-1.0 / (std::max (0.01, releaseMs) * 0.001 * sr));
        }
        inline float tick (float x) noexcept
        {
            const float v = std::abs (x);
            env = v > env ? v + a * (env - v) : v + r * (env - v);
            return env;
        }
    };

    //==============================================================================
    /** Pitch shifter a due testine con finestre di Hann (tipo "rotating head"). */
    class PitchShifter
    {
    public:
        void prepare (double sr, double windowMs = 60.0)
        {
            win = (float) (windowMs * 0.001 * sr);
            line.allocate ((int) win * 2 + 8);
            phase = 0;
        }
        void clear() { line.clear(); phase = 0; }
        void setRatio (float r) noexcept { ratio = r; }
        inline float tick (float x) noexcept
        {
            line.push (x);
            phase += (1.0 - ratio) / win;
            phase -= std::floor (phase);
            const double ph2 = phase + 0.5 - std::floor (phase + 0.5);
            const float w1 = 0.5f - 0.5f * (float) std::cos (6.283185307179586 * phase);
            const float w2 = 1.0f - w1;
            return line.read ((float) (phase * win) + 2.0f) * w1 + line.read ((float) (ph2 * win) + 2.0f) * w2;
        }
    private:
        DelayLine line;
        double phase = 0;
        float win = 2880, ratio = 1;
    };

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    /** Curva "audio" dei potenziometri (15% a meta' corsa). */
    inline float taperA (float x) noexcept
    {
        return (std::pow (32.11f, std::clamp (x, 0.0f, 1.0f)) - 1.0f) / 31.11f;
    }

    /** Mappa logaritmica 0..1 -> lo..hi. */
    inline float logMap (float x, float lo, float hi) noexcept { return lo * std::pow (hi / lo, std::clamp (x, 0.0f, 1.0f)); }
    inline float linMap (float x, float lo, float hi) noexcept { return lo + (hi - lo) * std::clamp (x, 0.0f, 1.0f); }

    /** Saturazione dolce tipo compander/BBD. */
    inline float softSat (float x) noexcept { return x / std::sqrt (1.0f + x * x); }
}
