/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Banco di filtri complessi per il pitch polifonico (POG, HOG, Pitch Fork,
    serie 9, Mono Synth, Voice Box).

    Perche' un banco di filtri e non un phase vocoder o un PSOLA:
      * polifonico senza riconoscere le note: ogni banda (1/4 d'ottava) contiene
        di regola un solo parziale della chitarra; la sua fase istantanea si
        moltiplica (ottave sopra, quinte = 3/2, terze = 5/4 ...) o si divide con
        radici complesse continue (ottave sotto) con intonazione esatta;
      * latenza = ritardo di gruppo delle bande (circa 3-25 ms dal registro alto
        al Mi basso), senza finestre FFT e senza blocchi;
      * nessuna funzione trigonometrica nel ciclo: solo moltiplicazioni
        complesse e normalizzazioni.
    Ogni banda e' una cascata di due poli complessi p = r e^{i w}. La fase della
    banda viene compensata con la risposta del filtro alla frequenza istantanea
    misurata (D = 1 - p conj(d)), cosi' bande vicine che seguono lo stesso
    parziale restano in fase anche dopo la moltiplicazione; le radici (1/2, 1/4,
    1/8) hanno 2^L rami: si scelgono continui nel tempo e allineati tra bande
    vicine sullo stesso parziale.
*/

#pragma once

#include "FxClassic.h"
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
 #include <xmmintrin.h>
#endif

namespace pt::engine::cx
{
    struct Cpx { float r = 1, i = 0; };
    /** 1/sqrt(x) veloce: stima hardware + un passo di Newton (errore ~1e-6). */
    inline float rsqrtFast (float x) noexcept
    {
       #if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
        const float y = _mm_cvtss_f32 (_mm_rsqrt_ss (_mm_set_ss (x)));
        return y * (1.5f - 0.5f * x * y * y);
       #else
        return 1.0f / std::sqrt (x);
       #endif
    }
    inline Cpx cmul (Cpx a, Cpx b) noexcept { return { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r }; }
    /** a * conj(b) */
    inline Cpx cmulc (Cpx a, Cpx b) noexcept { return { a.r * b.r + a.i * b.i, a.i * b.r - a.r * b.i }; }
    inline Cpx cnorm (Cpx a) noexcept
    {
        const float m = a.r * a.r + a.i * a.i;
        if (m < 1.0e-30f) return { 1, 0 };
        const float k = rsqrtFast (m);
        return { a.r * k, a.i * k };
    }
    /** Radice principale di un fasore unitario. */
    inline Cpx croot (Cpx u) noexcept
    {
        const float re = 1.0f + u.r;
        if (re < 1.0e-6f && std::abs (u.i) < 1.0e-3f) return { 0, 1 };
        return cnorm ({ re, u.i });
    }

    /** Trasformata di Hilbert a due catene di all-pass (Niemitalo): coppia in quadratura 0..0,9 fs/2. */
    struct Hilbert
    {
        float xa[4][2] {}, ya[4][2] {}, xb[4][2] {}, yb[4][2] {}, delayA = 0;
        inline void tick (float x, float& re, float& im) noexcept
        {
            static const float a[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static const float b[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            float va = x, vb = x;
            for (int k = 0; k < 4; ++k)
            {
                const float a2 = a[k] * a[k], b2 = b[k] * b[k];
                const float ya0 = a2 * (va + ya[k][1]) - xa[k][1];
                xa[k][1] = xa[k][0]; xa[k][0] = va; ya[k][1] = ya[k][0]; ya[k][0] = ya0; va = ya0;
                const float yb0 = b2 * (vb + yb[k][1]) - xb[k][1];
                xb[k][1] = xb[k][0]; xb[k][0] = vb; yb[k][1] = yb[k][0]; yb[k][0] = yb0; vb = yb0;
            }
            re = delayA; delayA = va;        // ramo A ritardato di un campione
            im = -vb;                        // segno: frequenze positive (e^{+i w t})
        }
        void reset() { *this = Hilbert(); }
    };

    /** Guadagno che rende unitaria una voce del banco per una sinusoide (misurato: 0,95 senza correzione). */
    constexpr float kBankGain = 1.05f;

    class SpectralBank
    {
    public:
        static constexpr int maxBands = 40;
        static constexpr int maxPow = 17;

        /** Vista di una banda (copia dei campi utili). */
        struct Band
        {
            float fc = 100, amp = 0, ampC = 0, w = 0;
            bool active = false;
            Cpx dn, root[4];
        };

        /** fLo..fHi Hz, perOct bande per ottava, bw = semi-banda relativa di ciascun polo,
            levels = livelli di radice da mantenere (0..3). */
        void prepare (double sampleRate, float fLo, float fHi, int perOct, float bw, int levels)
        {
            sr = (float) sampleRate;
            lv = std::clamp (levels, 0, 3);
            po = perOct;
            nb = std::min (maxBands, (int) std::floor (perOct * std::log2 (fHi / fLo)) + 1);
            for (int b = 0; b < maxBands; ++b)
            {
                const float f = fLo * std::pow (2.0f, (float) b / (float) perOct);
                fcA[b] = f;
                const float beta = std::max (bw * f, 9.0f);
                const float r = std::exp (-2.0f * pi * beta / sr);
                const float w = 2.0f * pi * std::min (f, 0.45f * sr) / sr;
                pr[b] = r * std::cos (w); pim[b] = r * std::sin (w);
                g[b] = 1.0f - r; gc[b] = 1.0f / (g[b] * g[b]);
                ecr[b] = std::cos (w); eci[b] = std::sin (w);
                dmax[b] = w * (std::pow (2.0f, 1.0f / (float) perOct) - 1.0f);
            }
            sameTol = 0.25f * (std::pow (2.0f, 1.0f / (float) perOct) - 1.0f);
            dsA = std::exp (-1.0f / (0.0007f * sr));
            reset();
        }

        void reset()
        {
            hilbert.reset();
            for (int b = 0; b < maxBands; ++b)
            {
                s1r[b] = s1i[b] = s2r[b] = s2i[b] = 0;
                ur[b] = upr[b] = dsr[b] = dnr[b] = cr[b] = 1; ui[b] = upi[b] = dsi[b] = dni[b] = ci[b] = 0;
                dm2[b] = 1;
                for (int L = 0; L < 4; ++L) { rr[L][b] = 1; ri[L][b] = 0; }
                amp[b] = ampC[b] = wgt[b] = 0; act[b] = 0;
                ampP[b] = ampP[b + 1] = ampP[b + 2] = 0;
            }
            maxAmp = 0; tick = 0;
        }

        int numBands() const noexcept { return nb; }
        int perOctave() const noexcept { return po; }
        float sampleRate() const noexcept { return sr; }
        float peak() const noexcept { return maxAmp; }
        inline Band band (int b) const noexcept
        {
            Band B;
            B.fc = fcA[b]; B.amp = amp[b]; B.ampC = ampC[b]; B.w = wgt[b]; B.active = act[b] > 0.5f;
            B.dn = { dnr[b], dni[b] };
            for (int L = 0; L < 4; ++L) B.root[L] = { rr[L][b], ri[L][b] };
            return B;
        }
        inline float fc (int b) const noexcept { return fcA[b]; }
        inline bool active (int b) const noexcept { return act[b] > 0.5f; }
        /** Ampiezza "posseduta" dalla banda (0 se inattiva). */
        inline float owned (int b) const noexcept { return act[b] * wgt[b] * ampC[b]; }
        inline Cpx root (int b, int L) const noexcept { return { rr[L][b], ri[L][b] }; }
        inline Cpx rotation (int b) const noexcept { return { dnr[b], dni[b] }; }

        /** Soglia relativa d'attivita' (frazione della banda piu' forte): 1e-3 normale, 0.25 = SPECTRAL GATE. */
        void setRelGate (float k) noexcept { relGateK = k; }

        /** Analizza un campione. Cicli senza salti, vettorizzabili (struttura ad array). */
        inline void push (float x, float gate = 2.0e-4f) noexcept
        {
            float xr, xi;
            hilbert.tick (x, xr, xi);
            const float gate2 = 0.25f * gate * gate;
            float mx = 0;
            for (int b = 0; b < nb; ++b)
            {
                // due poli complessi in cascata sull'ingresso analitico
                const float a1r = g[b] * xr + pr[b] * s1r[b] - pim[b] * s1i[b];
                const float a1i = g[b] * xi + pr[b] * s1i[b] + pim[b] * s1r[b];
                s1r[b] = a1r; s1i[b] = a1i;
                const float a2r = g[b] * a1r + pr[b] * s2r[b] - pim[b] * s2i[b];
                const float a2i = g[b] * a1i + pr[b] * s2i[b] + pim[b] * s2r[b];
                s2r[b] = a2r; s2i[b] = a2i;
                const float m2 = a2r * a2r + a2i * a2i;
                const float inv = 1.0f / std::sqrt (m2 + 1.0e-30f);
                amp[b] = m2 * inv;
                ampP[b + 1] = amp[b];
                const float live = (float) (m2 > gate2);            // sotto gate/2 la fase resta ferma
                const float nur = live * a2r * inv + (1.0f - live) * ur[b], nui = live * a2i * inv + (1.0f - live) * ui[b];
                ur[b] = nur; ui[b] = nui;
                const float dr = nur * upr[b] + nui * upi[b], di = nui * upr[b] - nur * upi[b];
                upr[b] = nur; upi[b] = nui;
                dsr[b] = dr + dsA * (dsr[b] - dr); dsi[b] = di + dsA * (dsi[b] - di);
            }
            for (int b = 0; b < nb; ++b) mx = std::max (mx, amp[b]);
            if (tick == 0)
                for (int b = 0; b < nb; ++b)
                {
                    // compensazione della fase del filtro alla frequenza istantanea (ogni 4 campioni)
                    const float k1 = 1.0f / std::sqrt (dsr[b] * dsr[b] + dsi[b] * dsi[b] + 1.0e-30f);
                    const float nr = dsr[b] * k1, ni = dsi[b] * k1;
                    dnr[b] = nr; dni[b] = ni;
                    const float Dr = 1.0f - (pr[b] * nr + pim[b] * ni), Di = -(pim[b] * nr - pr[b] * ni);
                    const float m = Dr * Dr + Di * Di;
                    dm2[b] = m;
                    const float k2 = 1.0f / std::sqrt (m + 1.0e-30f);
                    const float Dnr = Dr * k2, Dni = Di * k2;
                    cr[b] = Dnr * Dnr - Dni * Dni; ci[b] = 2.0f * Dnr * Dni;
                }
            tick = (tick + 1) & 3;
            for (int b = 0; b < nb; ++b)
            {
                const float vr = ur[b] * cr[b] - ui[b] * ci[b], vi = ur[b] * ci[b] + ui[b] * cr[b];
                rr[0][b] = vr; ri[0][b] = vi;
                ampC[b] = std::min (amp[b] * dm2[b] * gc[b], amp[b] * 6.0f);
            }
            for (int L = 1; L <= lv; ++L)
                for (int b = 0; b < nb; ++b)
                {
                    // radice principale continua nel tempo
                    const float re = 1.0f + rr[L - 1][b], im = ri[L - 1][b];
                    const float k = 1.0f / std::sqrt (re * re + im * im + 1.0e-20f);
                    float cr2 = re * k, ci2 = im * k;
                    const float sgn = 1.0f - 2.0f * (float) (cr2 * rr[L][b] + ci2 * ri[L][b] < 0.0f);
                    rr[L][b] = sgn * cr2; ri[L][b] = sgn * ci2;
                }
            maxAmp = mx;
            // attivita', pesi "chi vince fra vicini" e possesso del parziale
            const float relGate = std::max (gate, mx * relGateK);
            for (int b = 0; b < nb; ++b)
            {
                const float a0 = amp[b] * amp[b];
                const float am = ampP[b], ap = ampP[b + 2];          // vicini (0 ai bordi)
                // la banda "possiede" un parziale solo se la sua frequenza istantanea cade entro una spaziatura
                // dal centro: le bande sul fianco di un parziale lontano tacciono
                const float dvr = dnr[b] * ecr[b] + dni[b] * eci[b], dvi = dni[b] * ecr[b] - dnr[b] * eci[b];
                const float dev = std::abs (dvi) + 10.0f * (float) (dvr <= 0.0f);
                const float own = std::min (1.0f, std::max (0.0f, 2.0f - 2.0f * dev / dmax[b]));
                wgt[b] = own * a0 / (a0 + am * am + ap * ap + 1.0e-20f);
                act[b] = (float) (amp[b] > relGate);
            }
            if (lv > 0)
                for (int b = 1; b < nb; ++b)
                {
                    if (act[b - 1] < 0.5f || act[b] < 0.5f) continue;
                    // stesso parziale? frequenze istantanee quasi uguali
                    const float xi2 = dni[b] * dnr[b - 1] - dnr[b] * dni[b - 1];
                    if (std::abs (xi2) > sameTol * 2.0f * pi * fcA[b] / sr) continue;
                    const int weak = amp[b - 1] < amp[b] ? b - 1 : b, strong = weak == b ? b - 1 : b;
                    for (int L = 1; L <= lv; ++L)
                    {
                        float c_r = rr[L][weak], c_i = ri[L][weak];
                        const float sqr = c_r * c_r - c_i * c_i, sqi = 2.0f * c_r * c_i;
                        if (sqr * rr[L - 1][weak] + sqi * ri[L - 1][weak] < 0) { const float t = c_r; c_r = -c_i; c_i = t; }   // ramo coerente col padre
                        if (c_r * rr[L][strong] + c_i * ri[L][strong] < 0) { c_r = -c_r; c_i = -c_i; }
                        rr[L][weak] = c_r; ri[L][weak] = c_i;
                    }
                }
        }

        /** Fasore della banda per il rapporto num / 2^L (num 1..16). */
        inline Cpx ratioPhasor (int b, int num, int L) const noexcept
        {
            Cpx base = root (b, std::clamp (L, 0, 3)), acc { 1, 0 };
            while (num > 0)
            {
                if (num & 1) acc = cmul (acc, base);
                base = cmul (base, base);
                num >>= 1;
            }
            return acc;
        }

        /** Dissolvenza vicino a Nyquist per una frequenza generata f (Hz). */
        inline float nyqFade (float f) const noexcept
        {
            const float lo = 0.36f * sr, hi = 0.45f * sr;
            return f <= lo ? 1.0f : f >= hi ? 0.0f : (hi - f) / (hi - lo);
        }

        /** Somma di una voce di rapporto num/2^L su tutte le bande attive (ampiezze compensate). */
        inline float voice (int num, int L) const noexcept
        {
            const int nums[1] = { num }, levels[1] = { L };
            const float gains[1] = { 1.0f };
            return voices (nums, levels, gains, 1);
        }

        /** Piu' voci: potenze calcolate per livello su tutte le bande in cicli vettorizzabili. */
        inline float voices (const int* nums, const int* levels, const float* gains, int n) const noexcept
        {
            int maxN[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < n; ++k) if (std::abs (gains[k]) > 0.0f) maxN[std::clamp (levels[k], 0, 3)] = std::max (maxN[std::clamp (levels[k], 0, 3)], std::clamp (nums[k], 1, maxPow - 1));
            float a[maxBands], acc[maxBands];
            for (int b = 0; b < nb; ++b) { a[b] = act[b] * wgt[b] * ampC[b]; acc[b] = 0; }
            for (int L = 0; L < 4; ++L)
            {
                if (maxN[L] == 0) continue;
                float pr2[maxBands], pi2[maxBands];
                for (int b = 0; b < nb; ++b) { pr2[b] = rr[L][b]; pi2[b] = ri[L][b]; }
                for (int m = 1; m <= maxN[L]; ++m)
                {
                    float gm = 0;
                    int idx = -1;
                    for (int k = 0; k < n; ++k) if (levels[k] == L && nums[k] == m) { gm += gains[k]; idx = k; }
                    if (idx >= 0 && std::abs (gm) > 0.0f)
                    {
                        const float ratio = (float) m / (float) (1 << L);
                        for (int b = 0; b < nb; ++b) acc[b] += gm * nyqFade (fcA[b] * ratio) * pr2[b];
                    }
                    if (m < maxN[L])
                        for (int b = 0; b < nb; ++b)
                        {
                            const float t = pr2[b] * rr[L][b] - pi2[b] * ri[L][b];
                            pi2[b] = pr2[b] * ri[L][b] + pi2[b] * rr[L][b];
                            pr2[b] = t;
                        }
                }
            }
            float y = 0;
            for (int b = 0; b < nb; ++b) y += a[b] * acc[b];
            return y;
        }

        /** Frequenza istantanea della banda (Hz). */
        inline float instFreq (int b) const noexcept { return std::atan2 (dsi[b], dsr[b]) * sr / (2.0f * pi); }

    private:
        alignas (32) float fcA[maxBands] {}, pr[maxBands] {}, pim[maxBands] {}, g[maxBands] {}, gc[maxBands] {}, ecr[maxBands] {}, eci[maxBands] {}, dmax[maxBands] {};
        alignas (32) float s1r[maxBands] {}, s1i[maxBands] {}, s2r[maxBands] {}, s2i[maxBands] {};
        alignas (32) float ur[maxBands] {}, ui[maxBands] {}, upr[maxBands] {}, upi[maxBands] {}, dsr[maxBands] {}, dsi[maxBands] {};
        alignas (32) float dnr[maxBands] {}, dni[maxBands] {}, cr[maxBands] {}, ci[maxBands] {}, dm2[maxBands] {};
        alignas (32) float rr[4][maxBands] {}, ri[4][maxBands] {};
        alignas (32) float amp[maxBands] {}, ampC[maxBands] {}, wgt[maxBands] {}, act[maxBands] {}, ampP[maxBands + 2] {};
        Hilbert hilbert;
        float sr = 48000, sameTol = 0.04f, dsA = 0.97f, maxAmp = 0, relGateK = 1.0e-3f;
        int nb = 0, lv = 0, po = 4, tick = 0;
    };

    /** Stima della nota (monofonica) dal banco: la banda piu' grave con energia significativa,
        verificata contro le armoniche; restituisce Hz (0 = nessuna nota). */
    inline float bankPitch (const SpectralBank& bank, float relThr = 0.2f)
    {
        const float pk = bank.peak();
        if (pk < 5.0e-4f) return 0;
        const int nb = bank.numBands();
        for (int b = 0; b < nb; ++b)
        {
            const auto& B = bank.band (b);
            if (B.amp < relThr * pk) continue;
            // massimo locale
            if (b + 1 < nb && bank.band (b + 1).amp > B.amp * 1.05f)
            {
                // la banda successiva e' piu' forte: usa quella se segue lo stesso parziale
                continue;
            }
            return bank.instFreq (b);
        }
        return 0;
    }
}
