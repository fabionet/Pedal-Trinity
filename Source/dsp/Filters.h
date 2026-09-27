/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Filtri elementari senza allocazioni (sicuri per il thread audio):
    biquad RBJ "Audio EQ Cookbook" e filtri a un polo.
*/

#pragma once

#include <cmath>
#include <algorithm>

namespace pt::dsp
{
    constexpr double pi = 3.14159265358979323846;

    /** Biquad in forma trasposta diretta II (un canale). */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float s1 = 0, s2 = 0;

        void reset() noexcept { s1 = s2 = 0; }

        inline float process (float x) noexcept
        {
            const float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }

        void copyCoefficientsFrom (const Biquad& o) noexcept
        {
            b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2;
        }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        static double clampFreq (double f, double sr) { return std::clamp (f, 10.0, sr * 0.49); }

        void lowPass (double sr, double f, double q)
        {
            const double w = 2 * pi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void highPass (double sr, double f, double q)
        {
            const double w = 2 * pi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void peak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2 * pi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
            set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
        }

        void lowShelf (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2 * pi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
            const double sA = 2 * std::sqrt (A) * a;
            set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                 (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
        }

        void highShelf (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2 * pi * clampFreq (f, sr) / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
            const double sA = 2 * std::sqrt (A) * a;
            set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                 (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
        }
    };

    /** Filtro a un polo (passa-basso e passa-alto dalla stessa struttura). */
    struct OnePole
    {
        float a = 0, z = 0;

        void reset() noexcept { z = 0; }
        void setCutoff (double sr, double f) noexcept
        {
            a = (float) std::exp (-2.0 * pi * Biquad::clampFreq (f, sr) / sr);
        }
        inline float lowPass (float x) noexcept  { z = x + a * (z - x); return z; }
        inline float highPass (float x) noexcept { return x - lowPass (x); }
    };

    /** Diodi in antiparallelo, ginocchio regolabile: k=2 morbido, k=4 piu' duro. */
    inline float diodeSoft (float v, float vd) noexcept
    {
        const float a = v / vd;
        return v / std::sqrt (1.0f + a * a);
    }

    inline float diodeHard (float v, float vd) noexcept
    {
        const float a = v / vd, a2 = a * a;
        return v / std::sqrt (std::sqrt (1.0f + a2 * a2));
    }

    /** tanh razionale veloce (errore < 0.5% nell'intervallo utile). */
    inline float fastTanh (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
}
