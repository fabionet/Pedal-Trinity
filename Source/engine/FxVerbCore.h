/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Motore di riverbero multi-algoritmo (molle dispersive, FDN, plate di Dattorro,
    reverse, flerb, eco, tremolo, dinamico, auto-infinito, shimmer, poly, risonante).
    Spostato qui da FxClassicTime.cpp senza modifiche (tappa 3A) per poterlo usare
    anche nei riverberi e nei multi-effetto della tappa 3B.
*/

#pragma once

#include "FxClassic.h"

// -Wswitch-enum: gli switch del motore (codice della tappa 3A spostato qui senza modifiche) hanno 'default' voluti
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wswitch-enum")

namespace pt::engine::cx
{
    //==============================================================================
    /** Motore di riverbero multi-algoritmo condiviso dai riverberi e dal Holy Stain. */
    class VerbCore
    {
    public:
        enum Algo { GSpring, ASpring, Spring6G15, Hall, Room, Plate_, Reverse, Flerb, EchoV, Trem, ModV, Dyna, AutoInf, Shim, Poly, Resonant };

        void prepare (double s)
        {
            sr = (float) s;
            fdn.prepare (s, 2.4f); fdn2.prepare (s, 1.7f); plate.prepare (s); spring.prepare (s);
            pre.allocate ((int) (s * 2.2)); rev.allocate ((int) (s * 2.2)); flg.allocate ((int) (0.02 * s));
            comb.allocate ((int) (s / 40.0));
            revFade = std::max (1, (int) (0.03 * s));
            for (auto& sh : shifter) sh.prepare (s, 70);
            env.set (s, 3, 120);
            lfo.setHz (s, 0.3); trem.setHz (s, 4);
            dwellHp.set (s, 150); toneLp.set (s, 6000); fbLp.set (s, 6000);
            clear();
        }
        void clear()
        {
            fdn.clear(); fdn2.clear(); infCur = 0; plate.clear(); spring.clear(); pre.clear(); rev.clear(); flg.clear(); comb.clear();
            for (auto& sh : shifter) sh.clear();
            flgFb = 0; revPos = 0; revFill = 0; gate = 0; holdN = 0; infIn = 0; preFb = 0; shimFb[0] = shimFb[1] = 0; combFb = 0;
            lastE = 0; swell = 0; xfade = 1;
        }
        /** Parametri: t60 s (>=60 infinito), tono Hz, pre-delay s, feedback del pre-delay, a/b = parametri del tipo, variante 0..2. */
        void set (Algo a, float t60In, float toneHz, float preS, float preFbIn, float aIn, float bIn, int var)
        {
            if (a == Reverse && algo != Reverse) { rev.clear(); revPos = 0; revFill = 0; }   // niente registrazione vecchia
            algo = a; t60 = t60In; tone = toneHz; preD = preS; preFbK = preFbIn; pa = aIn; pb = bIn; variant = var;
            switch (algo)
            {
                case GSpring:    spring.set (springGain (t60, 0.036f), tone, 2, -0.55f); break;
                case ASpring:    spring.set (springGain (t60, 0.039f), tone, 3, -0.62f); break;
                case Spring6G15: spring.set (springGain (t60, 0.038f), std::min (tone, 5000.0f), 3, -0.68f); break;
                case Plate_:     plate.set (t60, tone, var == 2 ? 0.6f : 1.0f, var == 1 ? 4.0f : 10.0f); break;
                case Room:       fdn.set (t60, tone, var == 1 ? 0.55f : 0.38f, 1.0f, 0.55f); break;
                case EchoV:      plate.set (t60, tone, 1.0f, 8.0f); break;
                case Shim:       fdn.set (t60, tone * 1.3f, 1.4f, 6.0f, 0.65f); break;
                case AutoInf:    fdn.set (1000.0f, tone, 1.6f, 5.0f, 0.65f); fdn2.set (1000.0f, tone, 1.6f, 5.0f, 0.65f); break;
                case ModV:       fdn.set (t60, tone, 1.3f, var == 0 ? 14.0f : var == 1 ? 30.0f : 6.0f, 0.65f); break;
                default:         fdn.set (t60, tone, var == 1 ? 2.0f : 1.6f, var == 2 ? 10.0f : 4.0f, 0.65f); break;
            }
        }
        static float springGain (float t60, float loopS) { return t60 >= 60.0f ? 0.97f : std::pow (10.0f, -3.0f * loopS / std::max (0.1f, t60)); }

        /** Elabora un campione mono -> wet stereo. */
        inline void tick (float x, float& wl, float& wr) noexcept
        {
            float in = x;
            // pre-delay con ricircolo (Cathedral FEEDBACK) o eco (ECHO)
            if (preD > 0.0005f || algo == EchoV)
            {
                const float pd = algo == EchoV ? std::max (0.02f, preD) : preD;
                pre.push (x + preFbK * preFb);
                preFb = pre.read (pd * sr);
                in = algo == EchoV ? x : preFb;
            }
            const float e = env.tick (x);
            float l = 0, r = 0;
            switch (algo)
            {
                case GSpring: case ASpring: case Spring6G15:
                {
                    float v = in;
                    if (algo == Spring6G15)      // DWELL: valvola che pilota il trasduttore
                    {
                        const float dw = 1.0f + 6.0f * pa;
                        v = std::tanh (dwellHp.hp (v) * dw) / std::sqrt (dw);
                    }
                    spring.tick (v * 1.6f, l, r);
                    l *= 1.1f; r *= 1.1f;
                    break;
                }
                case Plate_: plate.tick (in, l, r); l *= 1.2f; r *= 1.2f; break;
                case EchoV:
                {
                    // eco (pre-delay come linea con feedback) + plate sulla somma
                    plate.tick (0.6f * in + 0.5f * preFb, l, r);
                    l += 0.8f * preFb; r += 0.8f * preFb;
                    break;
                }
                case Reverse:
                {
                    // riverbero denso registrato e riletto al contrario per segmenti (swell finale)
                    float a, b; fdn.tick (in, a, b);
                    // attacco della registrazione (30 ms a coseno rialzato): riletto al contrario, l'inizio del serbatoio dopo
                    // clear() diventa una dissolvenza e non un gradino a fine segmento
                    float att = 1.0f;
                    if (revFill < revFade) { att = 0.5f - 0.5f * std::cos (pi * (float) revFill / (float) revFade); ++revFill; }
                    rev.push (att * (0.5f * (a + b) + 0.3f * in));
                    const float seg = std::max (0.05f, pa) * sr;
                    float acc = 0;
                    for (int k = 0; k < 2; ++k)
                    {
                        const float pos = std::fmod ((float) revPos + 0.5f * seg * (float) k, seg);
                        const float ramp = pos / seg;                   // la coda cresce verso la fine del segmento
                        const float win = std::sin (pi * pos / seg);
                        acc += win * ramp * rev.read (2.0f * pos + 1.0f);
                    }
                    ++revPos;
                    l = r = 1.4f * acc;
                    break;
                }
                case Shim:
                {
                    float v = in + 0.45f * pa * 2.0f * shimFb[0];
                    fdn.tick (v, l, r);
                    static const float iv[3][2] = { { 12, 12 }, { 7, 12 }, { -12, 12 } };
                    const auto* ints = iv[std::clamp (variant, 0, 2)];
                    shifter[0].setRatio (std::pow (2.0f, ints[0] / 12.0f));
                    shifter[1].setRatio (std::pow (2.0f, ints[1] / 12.0f));
                    shimFb[0] = 0.5f * (shifter[0].tick (l) + shifter[1].tick (r));
                    fbLp.setFast (sr, 7000.0f); shimFb[0] = fbLp.lp (shimFb[0]);
                    break;
                }
                case Poly:
                {
                    fdn.tick (in, l, r);
                    static const float iv[3][2] = { { 12, -12 }, { 7, 12 }, { 5, -7 } };
                    const auto* ints = iv[std::clamp (variant, 0, 2)];
                    shifter[0].setRatio (std::pow (2.0f, (ints[0] * (variant < 3 ? 1.0f : pa)) / 12.0f));
                    shifter[1].setRatio (std::pow (2.0f, ints[1] / 12.0f));
                    const float a = shifter[0].tick (l), b = shifter[1].tick (r);
                    l = 0.6f * l + 0.7f * a; r = 0.6f * r + 0.7f * b;
                    break;
                }
                case AutoInf:
                {
                    // due serbatoi infiniti alternati: ogni nuova nota entra in quello libero (pulito) per
                    // 150 ms e la coda precedente sfuma in fadeS secondi
                    if (e > 0.004f && e > 1.8f * lastE && holdN <= 0)
                    {
                        infCur ^= 1;
                        (infCur ? fdn2 : fdn).clear();
                        holdN = (int) (0.15f * sr); xfade = 0;
                    }
                    lastE = 0.998f * lastE + 0.002f * e;
                    const float gIn = holdN-- > 0 ? 1.0f : 0.0f;
                    infIn += (gIn - infIn) * 0.004f;
                    const float fadeS = 0.2f + 2.0f * pa;
                    xfade = std::min (1.0f, xfade + 1.0f / (fadeS * sr));
                    float l1, r1, l2, r2;
                    fdn.tick (infCur == 0 ? in * infIn : 0.0f, l1, r1);
                    fdn2.tick (infCur == 1 ? in * infIn : 0.0f, l2, r2);
                    const float gNew = std::sin (1.5707963f * xfade), gOld = std::cos (1.5707963f * xfade);
                    l = infCur == 0 ? gNew * l1 + gOld * l2 : gNew * l2 + gOld * l1;
                    r = infCur == 0 ? gNew * r1 + gOld * r2 : gNew * r2 + gOld * r1;
                    break;
                }
                default:
                    fdn.tick (in, l, r);
                    if (algo == Room) { l *= 1.3f; r *= 1.3f; }
                    break;
            }
            // post-elaborazioni
            if (algo == Flerb)
            {
                lfo.setHz (sr, 0.15f + 2.0f * pa);
                const float m = 0.5f + 0.5f * lfo.tri(); lfo.step();
                const float s = 0.5f * (l + r);
                flg.push (s + 0.6f * pb * flgFb);
                flgFb = flg.read ((0.0008f + 0.006f * m) * sr);
                l = 0.7f * (l + flgFb); r = 0.7f * (r + flgFb);
            }
            else if (algo == Trem)
            {
                trem.setHz (sr, variant == 0 ? 2.0f : variant == 1 ? 4.5f : 8.0f);
                if (pa >= 0) trem.setHz (sr, logMap (pa, 1.0f, 10.0f));
                const float g = 1.0f - std::clamp (pb, 0.0f, 1.0f) * (0.5f + 0.5f * trem.sine());
                trem.step();
                l *= g; r *= g;
            }
            else if (algo == Dyna)
            {
                const int m = variant;      // 0 swell, 1 gate, 2 duck
                if (m == 0)
                {
                    if (e > 0.004f && e > 1.8f * lastE) swell = 0;
                    lastE = 0.998f * lastE + 0.002f * e;
                    swell = std::min (1.0f, swell + 1.0f / ((0.15f + 1.5f * pa) * sr));
                    l *= swell * swell; r *= swell * swell;
                }
                else if (m == 1)
                {
                    if (e > 0.003f) holdN = (int) ((0.1f + 0.6f * pa) * sr);
                    gate += ((holdN-- > 0 ? 1.0f : 0.0f) - gate) * 0.003f;
                    l *= gate; r *= gate;
                }
                else
                {
                    const float duck = 1.0f / (1.0f + (6.0f + 30.0f * pa) * e);
                    l *= duck; r *= duck;
                }
            }
            else if (algo == Resonant)
            {
                const float f0 = logMap (pa, 55.0f, 880.0f);
                const float s = 0.5f * (l + r);
                comb.push (s + (0.6f + 0.38f * pb) * combFb);
                combD = (sr / f0) + 0.999f * (combD - sr / f0);          // accordatura lisciata (niente salti del pettine)
                combFb = comb.read (combD);
                l = 0.5f * l + 0.6f * combFb; r = 0.5f * r + 0.6f * combFb;
            }
            wl = 1.5f * softSat (l / 1.5f);
            wr = 1.5f * softSat (r / 1.5f);
        }
    private:
        float sr = 48000, t60 = 2, tone = 6000, preD = 0, preFbK = 0, pa = 0.5f, pb = 0.5f, flgFb = 0, preFb = 0, shimFb[2] {},
              gate = 0, infIn = 0, combFb = 0, combD = 200, lastE = 0, swell = 1, xfade = 1;
        int variant = 0, holdN = 0, revFill = 0, revFade = 1440;
        long long revPos = 0;
        Algo algo = Hall;
        Fdn fdn, fdn2;
        int infCur = 0;
        Plate plate;
        SpringTank spring;
        DelayLine pre, rev, flg, comb;
        GrainShifter shifter[2];
        Envelope env;
        Lfo lfo, trem;
        OnePole dwellHp, toneLp, fbLp;
    };
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
