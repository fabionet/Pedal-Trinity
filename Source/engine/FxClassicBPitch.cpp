/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pitch e synth digitali della tappa 3B:
      * b3pog      - POG3 (5 voci dal banco di filtri, ATTACK, filtro con Q, DETUNE) e Pico POG
                     (tono a bilanciere o filtro passa-basso/passa-alto sulle sole ottave);
      * b3fork     - Pitch Fork+ (due voci +-3 ottave con detune e modulazione incrociata) e
                     Pico Pitch Fork (10 intervalli, UP/DOWN/DUAL, glissato SWEEP);
      * b3ihm      - Intelligent Harmony Machine: armonie diatoniche TD-PSOLA nella tonalita'
                     scelta o trasposizione polifonica (Poly Override);
      * b3slammi   - Next Step Slammi e Slammi Plus: bend polifonico continuo +-3 ottave dal
                     pedale, DUAL e X-FADE;
      * b3ring     - Ring Thing: modulatore ad anello, banda laterale singola superiore/inferiore
                     (Hilbert, bande separate L/R) e pitch shift modulato;
      * b3atomic   - Pico Atomic Cluster: analisi FFT, gli N parziali piu' forti risintetizzati
                     con oscillatori aggiornati a SPEED, transizioni SHARP o SMOOTH;
      * b3vocoder  - V256 e Iron Lung: vocoder a FFT (8-256 bande) con synth robot/drone,
                     trasposizione, controllo dallo strumento e correzione d'intonazione;
      * b3superego - Superego+: congelamento a nuvola di granuli con ATTACK/DECAY/THRESHOLD/
                     LAYER/GLISS, 5 modi e 11 effetti interni.
*/

#include "FxClassicB.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        inline float sliderGain (float s) noexcept { return 1.56f * s * s; }        // cursore -> guadagno (unita' a 0,8)

        //==============================================================================
        /** POG3 e Pico POG. */
        class B3Pog final : public Effect
        {
        public:
            explicit B3Pog (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                pico = cfg.str ("model", "pog3") == "pico";
                pGain = role (*this, "gain", 0.2f); pVol = role (*this, "level", 0.6f); pDry = role (*this, "direct", 0.8f);
                pOct2 = role (*this, "oct2", 0.0f); pOct1 = role (*this, "oct1", 0.0f); pFifth = role (*this, "fifth", 0.0f);
                pUp1 = role (*this, "up1", 0.0f); pUp2 = role (*this, "up2", 0.0f); pAttack = role (*this, "attack", 0.0f);
                pFreq = role (*this, "freq", 1.0f); pDetune = role (*this, "detune", 0.0f); pQ = role (*this, "q", 0.2f);
                pTone = role (*this, "tone", 0.5f); pFilt = role (*this, "filter", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 2);
                det1.prepare (s, 0.43f); det2.prepare (s, 0.31f);
                onset.prepare (s, 0.004f);
                for (auto& g : gS) g.set (s, 25);
                for (auto* sm : { &dryS, &ginS, &volS, &cutS, &qS, &dtS, &toneS, &toneHS }) sm->set (s, 25);
                fader.prepare (s, 12);
                tlLo.set (s, 300); tlHi.set (s, 800);
                reset();
            }
            void reset() override { bank.reset(); det1.clear(); det2.clear(); lp.reset(); swell = 1; duck = false; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                int nums[5], lev[5]; float gains[5]; int nv = 0;
                float gIn = 1.0f, gDry, vol = 1.0f, detAmt = 0, attackS = 0, cut = 20000, q = 0.707f;
                int fmode = 0;
                if (pico)
                {
                    gDry = sliderGain (pDry.get (*this));
                    nums[nv] = 1; lev[nv] = 1; gains[nv++] = sliderGain (pOct1.get (*this));
                    nums[nv] = 2; lev[nv] = 0; gains[nv++] = sliderGain (pUp1.get (*this));
                    fmode = std::clamp (pFilt.step (*this), 0, 2);                  // 0 TONE, 1 LPF, 2 HPF
                    const float t = pTone.get (*this);
                    if (fmode == 1) { cut = logMap (t, 150.0f, 9000.0f); q = 2.5f; }
                    else if (fmode == 2) { cut = logMap (t, 40.0f, 3000.0f); q = 1.2f; }
                }
                else
                {
                    gIn = 0.5f + 2.5f * pGain.get (*this);                          // INPUT GAIN 0,5..3x
                    gDry = sliderGain (pDry.get (*this));
                    vol = 2.0f * taperA (pVol.get (*this)) / 0.96f;
                    nums[nv] = 1; lev[nv] = 2; gains[nv++] = sliderGain (pOct2.get (*this));
                    nums[nv] = 1; lev[nv] = 1; gains[nv++] = sliderGain (pOct1.get (*this));
                    nums[nv] = 3; lev[nv] = 1; gains[nv++] = sliderGain (pFifth.get (*this));
                    detAmt = pDetune.get (*this);
                    nums[nv] = 2; lev[nv] = 0; gains[nv++] = sliderGain (pUp1.get (*this)) * (1.0f - 0.5f * detAmt);
                    nums[nv] = 4; lev[nv] = 0; gains[nv++] = sliderGain (pUp2.get (*this)) * (1.0f - 0.5f * detAmt);
                    attackS = pAttack.get (*this) * pAttack.get (*this) * 3.0f;      // fino a 3 s
                    cut = logMap (pFreq.get (*this), 100.0f, 20000.0f);
                    q = logMap (pQ.get (*this), 0.6f, 8.0f);
                    fmode = 1;
                }
                const float gUp1 = sliderGain (pUp1.get (*this)), gUp2 = sliderGain (pUp2.get (*this));
                const float tl = (pTone.get (*this) - 0.5f) * 2.0f;
                const float glT = std::pow (10.0f, -0.4f * tl), ghT = std::pow (10.0f, 0.4f * tl);   // bilanciere +-8 dB
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (fmode, changed);
                    if (changed) lp.reset();
                    const float x = monoIn (ch, numCh, i);
                    const float gi = ginS.next (gIn);
                    const float a = std::clamp (x * gi, -1.5f, 1.5f);                // convertitore A/D
                    bank.push (a);
                    float sm[5];
                    for (int k = 0; k < nv; ++k) sm[k] = gS[k].next (gains[k]);
                    float y = bank.voices (nums, lev, sm, nv) * kBankGain;
                    const float dt = dtS.next (detAmt);
                    if (dt > 1.0e-4f)
                    {
                        // DETUNE: le ottave sopra passano in proporzione per due linee a detune lento
                        const float u1 = bank.voice (2, 0) * kBankGain, u2 = bank.voice (4, 0) * kBankGain;
                        const float c = 4.0f + 16.0f * dt;
                        y += 0.7f * dt * (det1.tick (u1 * gUp1, c) + det2.tick (u2 * gUp2, c * 1.3f));
                    }
                    if (attackS > 0.001f)
                    {
                        if (onset.tick (a)) duck = true;
                        if (duck) { swell -= 1.0f / (0.004f * sr); if (swell <= 0) { swell = 0; duck = false; } }
                        else swell = std::min (1.0f, swell + 1.0f / (attackS * sr));
                        y *= swell * swell * (3.0f - 2.0f * swell);
                    }
                    y /= std::max (0.25f, gi);
                    const float c = cutS.next (cut), qq = qS.next (q);
                    float out;
                    if (pico && fader.current == 0)
                    {
                        // TONE: bilanciere su tutte le voci (mensole a ~300 Hz e ~800 Hz)
                        const float gl = toneS.next (glT), gh = toneHS.next (ghT);
                        const float v = y + dryS.next (gDry) * x;
                        const float lo = tlLo.lp (v), hi = v - tlHi.lp (v);
                        out = v + (gl - 1.0f) * lo + (gh - 1.0f) * hi;
                    }
                    else
                    {
                        lp.setG (fastTanPi (c, sr), 1.0f / qq);
                        float l, b, h; lp.tick (y, l, b, h);
                        const float f = fader.current == 2 ? h : l;
                        out = fg * f + (1.0f - fg) * y + dryS.next (gDry) * x;
                    }
                    out = outRail (out * volS.next (vol));
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = out;
                }
            }
        private:
            Config cfg;
            bool pico = false, duck = false;
            float sr = 48000, swell = 1;
            RoleParam pGain, pVol, pDry, pOct2, pOct1, pFifth, pUp1, pUp2, pAttack, pFreq, pDetune, pQ, pTone, pFilt;
            SpectralBank bank;
            DetunerB det1, det2;
            OnsetB onset;
            Svf lp;
            OnePole tlLo, tlHi;
            XFader fader;
            Smooth gS[5], dryS, ginS, volS, cutS, qS, dtS, toneS, toneHS;
        };

        //==============================================================================
        /** Pitch Fork+ e Pico Pitch Fork. */
        class B3Fork final : public Effect
        {
        public:
            explicit B3Fork (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                pico = cfg.str ("model", "plus") == "pico";
                pDry = role (*this, "direct", 0.7f); pS1 = role (*this, "shift1", 0.7f); pS2 = role (*this, "shift2", 0.0f);
                pI1 = role (*this, "int1", 0.6667f); pD1 = role (*this, "det1", 0.5f); pI2 = role (*this, "int2", 0.3333f);
                pD2 = role (*this, "det2", 0.5f); pX12 = role (*this, "xmod12", 0.0f); pX21 = role (*this, "xmod21", 0.0f);
                pVol = role (*this, "level", 0.6f); pBlend = role (*this, "blend", 0.5f); pSweep = role (*this, "sweep", 0.0f);
                pShift = role (*this, "shift", 0.7f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 3);
                for (auto& v : bv) v.prepare (s, 40);
                for (auto& e : env) e.set (s, 2, 40);
                fader.prepare (s, 15);
                for (auto* sm : { &dryS, &s1S, &s2S, &volS, &blS, &x12S, &x21S }) sm->set (s, 20);
                semS[0].set (s, 15); semS[1].set (s, 15);
                det.set (s, 2, 300);
                reset();
            }
            void reset() override { bank.reset(); for (auto& v : bv) v.clear(); glide = 1; quiet = (int) (sr); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                float t1 = 0, t2 = 0, g1 = 1, g2 = 0, dryG, wetG, vol = 1;
                int key = 0;
                if (pico)
                {
                    static const float iv[10] = { 0.17f, 1, 2, 4, 5, 7, 9, 12, 24, 36 };
                    static const float dual[10][2] = { { 0.17f, -0.17f }, { 0.35f, -0.35f }, { 2, 9 }, { 4, 7 }, { 5, -7 }, { 7, -12 },
                                                       { 9, -7 }, { 12, -12 }, { 24, -12 }, { 36, -12 } };
                    const int sh = std::clamp (pShift.step (*this), 0, 9), m = std::clamp (pMode.step (*this), 0, 2);
                    key = sh * 3 + m;
                    if (m < 2) { t1 = m == 0 ? iv[sh] : -iv[sh]; g2 = 0; }
                    else { t1 = dual[sh][0]; t2 = dual[sh][1]; g1 = g2 = 0.75f; }
                    const float b = pBlend.get (*this);
                    dryG = b < 0.5f ? 1.0f : 2.0f * (1.0f - b); wetG = b < 0.5f ? 2.0f * b : 1.0f;
                    vol = volKnob (pVol.get (*this), 0.5f, 3.0f);
                }
                else
                {
                    // SHIFT: semitoni interi +-36; DETUNE +-99 cent sommato all'intervallo
                    t1 = std::round ((pI1.get (*this) - 0.5f) * 72.0f) + (pD1.get (*this) - 0.5f) * 1.98f;
                    t2 = std::round ((pI2.get (*this) - 0.5f) * 72.0f) + (pD2.get (*this) - 0.5f) * 1.98f;
                    g1 = sliderGain (pS1.get (*this)); g2 = sliderGain (pS2.get (*this));
                    dryG = sliderGain (pDry.get (*this)); wetG = 1.0f;
                }
                // SWEEP (Pico): glissato dall'unisono all'intervallo quando si riprende a suonare dopo una pausa
                const float sweepS = pico ? (pSweep.get (*this) < 0.02f ? 0.0f : 4.0f * pSweep.get (*this) * pSweep.get (*this)) : 0.0f;
                const float x12 = pX12.get (*this), x21 = pX21.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (key, changed);
                    if (changed || fader.current == key) { aT1 = t1; aT2 = t2; }
                    if (changed && pico) { semS[0].snap (t1); semS[1].snap (t2); bv[0].clear(); bv[1].clear(); }   // a volume zero: niente glissato
                    const float x = monoIn (ch, numCh, i);
                    bank.push (x);
                    const float e = det.tick (x);
                    if (e < 0.002f) ++quiet; else { if (quiet > (int) (0.3f * sr)) glide = 0; quiet = 0; }
                    if (sweepS > 0) glide = std::min (1.0f, glide + 1.0f / (sweepS * sr)); else glide = 1;
                    const float s1 = semS[0].next (pico ? aT1 : t1) * glide, s2 = semS[1].next (pico ? aT2 : t2) * glide;
                    float y1 = bv[0].tick (bank, x, s1);
                    float y2 = g2 > 0.0f || s2S.y > 1e-4f ? bv[1].tick (bank, x, s2) : 0.0f;
                    if (! pico)
                    {
                        // X-MOD: modulazione ad anello di una voce con l'altra (portante normalizzata)
                        const float a12 = x12S.next (x12), a21 = x21S.next (x21);
                        const float n1 = y1 / (env[0].tick (y1) * 1.4f + 0.003f), n2 = y2 / (env[1].tick (y2) * 1.4f + 0.003f);
                        const float y2m = (1.0f - a12) * y2 + a12 * y2 * n1, y1m = (1.0f - a21) * y1 + a21 * y1 * n2;
                        y1 = y1m; y2 = y2m;
                    }
                    const float y = s1S.next (g1) * y1 + s2S.next (g2) * y2;
                    const float out = outRail (volS.next (vol) * (dryS.next (dryG) * x + blS.next (wetG) * y * (pico ? fg : 1.0f)));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            Config cfg;
            bool pico = false;
            float sr = 48000, glide = 1, aT1 = 0, aT2 = 0;
            int quiet = 0;
            RoleParam pDry, pS1, pS2, pI1, pD1, pI2, pD2, pX12, pX21, pVol, pBlend, pSweep, pShift, pMode;
            SpectralBank bank;
            BendVoice bv[2];
            Envelope env[2], det;
            XFader fader;
            Smooth dryS, s1S, s2S, volS, blS, x12S, x21S, semS[2];
        };

        //==============================================================================
        /** Intelligent Harmony Machine. */
        class B3Ihm final : public Effect
        {
        public:
            explicit B3Ihm (const ModelDef& d) : Effect (d)
            {
                pKey = role (*this, "key", 0.0f); pSharp = role (*this, "sharp", 0.0f); pMinor = role (*this, "minor", 0.0f);
                pPoly = role (*this, "poly", 0.0f); pInt = role (*this, "interval", 0.3f); pMix = role (*this, "blend", 0.5f);
                pVol = role (*this, "level", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 1);
                for (auto& p : ps) p.prepare (s);
                for (auto& v : pv) v.prepare (s, 45);
                dbl.prepare (s, 0.37f);
                dblDl.allocate ((int) (0.04 * s));
                fader.prepare (s, 15);
                for (auto& r : ratS) r.set (s, 12);
                f0S.set (s, 8); mixS.set (s, 20); volS.set (s, 20);
                reset();
            }
            void reset() override { bank.reset(); for (auto& p : ps) p.clear(); for (auto& v : pv) v.clear(); dbl.clear(); dblDl.clear(); f0 = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool poly = pPoly.step (*this) == 1;
                const bool minor = pMinor.step (*this) == 1;
                const int iv = std::clamp (pInt.step (*this), 0, 10);
                static const int naturals[7] = { 0, 2, 4, 5, 7, 9, 11 };
                const int root = (naturals[std::clamp (pKey.step (*this), 0, 6)] + (pSharp.step (*this) == 0 ? 1 : 0)) % 12;
                const float vol = volKnob (pVol.get (*this), 0.5f, 3.0f);
                const int want = (poly ? 100 : 0) + iv;
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (want, changed);
                    if (changed) { for (auto& p : ps) p.clear(); for (auto& v : pv) v.clear(); }
                    const float x = monoIn (ch, numCh, i);
                    bank.push (x);
                    const float p = bankPitch (bank, 0.25f);
                    if (p > 60 && p < 1500) f0 = f0S.next (p);
                    float wet = 0;
                    const int cur = fader.current;
                    if (cur >= 100)
                    {
                        // Poly Override: trasposizione polifonica fissa (3e e 2e maggiori/minori dalla levetta)
                        const int k = cur - 100;
                        static const float pi1[11] = { 0, -5, -4, 4, 5, 7, -2, 2, -12, 12, 7 };
                        float s = pi1[k];
                        if (minor && (k == 2 || k == 3 || k == 6 || k == 7)) s += s > 0 ? -1.0f : 1.0f;
                        if (k == 0)
                        {
                            // doubler: copia leggermente stonata e ritardata
                            dblDl.push (x);
                            wet = dbl.tick (dblDl.read (0.018f * sr), 9.0f);
                        }
                        else
                        {
                            wet = pv[0].tick (bank, x, s);
                            if (k == 10) wet = 0.7f * (wet + pv[1].tick (bank, x, 12.0f));
                        }
                    }
                    else
                    {
                        // Intelligent: armonie diatoniche nella tonalita' (gradi della scala), TD-PSOLA
                        static const int steps[11][2] = { { -5, 99 }, { -3, 99 }, { -2, 99 }, { 2, 99 }, { 3, 99 }, { 4, 99 }, { 5, 99 }, { 6, 99 },
                                                          { 2, 4 }, { 2, 6 }, { 4, 7 } };
                        const auto& st = steps[cur];
                        for (int v = 0; v < 2; ++v)
                        {
                            if (st[v] == 99) continue;
                            const float semis = (float) diatonic (st[v], root, minor);
                            const float r = ratS[v].next (std::exp2 (semis / 12.0f));
                            wet += (st[1] == 99 ? 1.0f : 0.7f) * ps[v].tick (x, f0, r, 1.0f);
                        }
                    }
                    wet *= fg;
                    const float m = mixS.next (pMix.get (*this));
                    const float out = outRail (volS.next (vol) * ((1.0f - m) * x + m * wet));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            /** Semitoni del grado 'degrees' sopra/sotto la nota attuale nella scala della tonalita'. */
            int diatonic (int degrees, int root, bool minor) const
            {
                static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorS[7] = { 0, 2, 3, 5, 7, 8, 10 };
                const int* sc = minor ? minorS : major;
                if (f0 <= 0) return degrees > 0 ? 4 : -3;
                const int note = ((int) std::lround (69.0f + 12.0f * std::log2 (f0 / 440.0f)) - root + 1200) % 12;
                int deg = 0; for (int k = 0; k < 7; ++k) if (sc[k] <= note) deg = k;
                const int t = deg + degrees, oct = (int) std::floor (t / 7.0f), td = ((t % 7) + 7) % 7;
                return sc[td] + 12 * oct - sc[deg];                      // le note fuori scala si muovono in parallelo
            }
            float sr = 48000, f0 = 0;
            RoleParam pKey, pSharp, pMinor, pPoly, pInt, pMix, pVol;
            SpectralBank bank;
            PsolaB ps[2];
            PolyVoice pv[2];
            DetunerB dbl;
            DelayLine dblDl;
            XFader fader;
            Smooth ratS[2], f0S, mixS, volS;
        };

        //==============================================================================
        /** Next Step Slammi e Slammi Plus. */
        class B3Slammi final : public Effect
        {
        public:
            explicit B3Slammi (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                plus = cfg.str ("model", "next") == "plus";
                pPedal = role (*this, "pedal", 0.0f); pMax = role (*this, "shift", 0.8f); pDir = role (*this, "dir", 0.0f);
                pDry = role (*this, "direct", 0.0f); pBlend = role (*this, "blend", 1.0f); pPitchMix = role (*this, "pitchmix", 0.0f);
                pXf = role (*this, "xfade", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 3);
                for (auto& v : bv) v.prepare (s, 40);
                for (auto* sm : { &pS, &dryS, &blS, &g2S, &xS, &g1S, &s1S, &s2S }) sm->set (s, 15);
                reset();
            }
            void reset() override { bank.reset(); for (auto& v : bv) v.clear(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                static const float maxN[11] = { 0.25f, 1, 2, 4, 5, 7, 9, 10, 12, 24, 36 };            // Next Step: D, 1/2, M2, M3, P4, P5, M6, m7, 1-2-3 ottave
                static const float maxP[11] = { 0.25f, 1, 2, 3, 4, 5, 7, 9, 12, 24, 36 };             // Plus: D m2 M2 m3 M3 P4 P5 M6 1-2-3 ottave
                static const float dualP[11] = { -0.25f, 9, 7, 7, -7, -12, -7, -12, -12, -12, -12 };  // seconda voce in DUAL (stima dalla tabella)
                const int sh = std::clamp (pMax.step (*this), 0, 10);
                const float top = plus ? maxP[sh] : maxN[sh];
                const int dir = plus ? std::clamp (pDir.step (*this), 0, 2) : std::clamp (pDir.step (*this), 0, 1);   // 0 su, 1 giu', 2 dual
                const bool xfade = plus && pXf.step (*this) == 1;
                const float pm = pPitchMix.get (*this);
                const float b = pBlend.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    bank.push (x);
                    const float ped = pS.next (pPedal.get (*this));
                    float s1, s2 = 0, g1 = 1, g2 = 0, dryMix = 0;
                    const float sign = dir == 1 ? -1.0f : 1.0f;
                    if (! xfade)
                    {
                        // PITCH: tallone = PITCH MIX (Plus, mezzi toni fino allo SHIFT), punta = SHIFT
                        const float heel = plus && dir != 2 ? std::round (pm * top) : 0.0f;
                        s1 = sign * (heel + (top - heel) * ped);
                        if (dir == 2) { s2 = dualP[sh] * ped; g1 = g2 = 0.75f; }
                    }
                    else
                    {
                        // X-FADE: dal dry (tallone) alla voce trasposta (punta); in DUAL il tallone e' una seconda voce
                        s1 = sign * top; g1 = ped;
                        if (dir == 2) { s1 = top; s2 = std::round ((pm - 0.5f) * 24.0f); g2 = 1.0f - ped; }
                        else dryMix = 1.0f - ped;
                    }
                    s1 = s1S.next (s1); s2 = s2S.next (s2);                                   // salti a semitoni (PITCH MIX, SHIFT, DIR) lisciati
                    float y = g1S.next (g1) * bv[0].tick (bank, x, s1);
                    const float gg2 = g2S.next (g2);
                    if (gg2 > 1.0e-4f) y += gg2 * bv[1].tick (bank, x, s2);
                    y += xS.next (dryMix) * x;
                    float out;
                    if (plus) { const float bb = blS.next (b); out = (1.0f - bb) * x + bb * y; }
                    else out = y + dryS.next (pDry.get (*this)) * x;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            Config cfg;
            bool plus = false;
            float sr = 48000;
            RoleParam pPedal, pMax, pDir, pDry, pBlend, pPitchMix, pXf;
            SpectralBank bank;
            BendVoice bv[2];
            Smooth pS, dryS, blS, g2S, xS, g1S, s1S, s2S;
        };

        //==============================================================================
        /** Ring Thing. */
        class B3Ring final : public Effect
        {
        public:
            explicit B3Ring (const ModelDef& d) : Effect (d)
            {
                pBlend = role (*this, "blend", 0.5f); pWave = role (*this, "wave", 0.5f); pFilt = role (*this, "filter", 1.0f);
                pFine = role (*this, "fine", 0.5f); pCoarse = role (*this, "coarse", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 3);
                bend.prepare (s, 40);
                hil.reset();
                fader.prepare (s, 12);
                for (auto* sm : { &blS, &fS, &wS, &cS, &fiS }) sm->set (s, 25);
                reset();
            }
            void reset() override { bank.reset(); bend.clear(); hil.reset(); lpL.reset(); lpR.reset(); ph = 0; mph = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = std::clamp (pMode.step (*this), 0, 3);             // RM, UB, LB, PS
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (mode, changed);
                    if (changed) { bend.clear(); }
                    const int m = fader.current;
                    const float x = monoIn (ch, numCh, i);
                    const float wk = wS.next (pWave.get (*this)), fk = fS.next (pFilt.get (*this));
                    const float ck = cS.next (pCoarse.get (*this)), fi = fiS.next (pFine.get (*this));
                    float yL, yR;
                    if (m == 3)
                    {
                        // PS: COARSE +-2 ottave a semitoni, FINE fra i passi; con FILTER/RATE alzato: modulazione (RATE, DEPTH)
                        bank.push (x);
                        const bool modOn = fk > 0.02f;
                        float semis = std::round ((ck - 0.5f) * 48.0f);
                        if (! modOn) semis += (fi - 0.5f) * 2.0f;
                        else
                        {
                            mph += logMap (fk, 0.05f, 12.0f) / sr; mph -= std::floor (mph);
                            semis += 12.0f * fi * waveRm ((float) mph, wk);
                        }
                        yL = yR = bend.tick (bank, x, semis);
                    }
                    else
                    {
                        const float f = logMap (ck, 0.1f, 2940.0f) * std::exp2 ((fi - 0.5f) * 2.0f / 12.0f);
                        ph += f / sr; ph -= std::floor (ph);
                        if (m == 0)
                        {
                            yL = yR = x * waveRm ((float) ph, wk);
                        }
                        else
                        {
                            // SSB: segnale analitico per la portante complessa (WAVE: centro sinusoide, a sinistra 2a armonica, a destra 3a)
                            float re, im;
                            hil.tick (x, re, im);
                            const float a = 6.2831853f * (float) ph;
                            float cr = std::cos (a), ci = std::sin (a);
                            const float h2 = std::max (0.0f, 0.5f - wk) * 1.2f, h3 = std::max (0.0f, wk - 0.5f) * 1.2f;
                            cr += h2 * std::cos (2.0f * a) + h3 * std::cos (3.0f * a);
                            ci += h2 * std::sin (2.0f * a) + h3 * std::sin (3.0f * a);
                            const float nrm = 1.0f / (1.0f + h2 + h3);
                            const float upper = (re * cr - im * ci) * nrm, lower = (re * cr + im * ci) * nrm;
                            yL = m == 1 ? upper : lower;                                       // la banda scelta sempre a sinistra
                            yR = m == 1 ? lower : upper;
                        }
                        lpL.setG (fastTanPi (logMap (fk, 150.0f, 20000.0f), sr), 1.0f / 0.8f);
                        lpR.setG (lpL.g, lpL.k);
                        yL = lpL.lp (yL); yR = lpR.lp (yR);
                        if (fk < 0.01f) { yL *= fk * 100.0f; yR *= fk * 100.0f; }
                    }
                    const float b = blS.next (pBlend.get (*this));
                    const float oL = outRail ((1.0f - b) * x + b * fg * yL), oR = outRail ((1.0f - b) * x + b * fg * yR);
                    if (numCh > 1) { ch[0][i] = oL; ch[1][i] = oR; }
                    else ch[0][i] = oL;
                }
            }
        private:
            /** WAVE del modulatore: quadra -> sinusoide -> rampa giu' (dissolvenze), poi rampa su -> triangolo. */
            static inline float waveRm (float p, float w) noexcept
            {
                const float sine = std::sin (6.2831853f * p);
                const float sq = std::tanh (8.0f * sine);
                const float rd = 1.0f - 2.0f * p, ru = 2.0f * p - 1.0f;
                const float tri = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
                if (w < 0.25f) { const float t = w / 0.25f; return sq + (sine - sq) * t; }
                if (w < 0.5f) { const float t = (w - 0.25f) / 0.25f; return sine + (rd - sine) * t; }
                const float t = (w - 0.5f) / 0.5f; return ru + (tri - ru) * t;
            }
            float sr = 48000;
            double ph = 0, mph = 0;
            RoleParam pBlend, pWave, pFilt, pFine, pCoarse, pMode;
            SpectralBank bank;
            BendVoice bend;
            Hilbert hil;
            Svf lpL, lpR;
            XFader fader;
            Smooth blS, fS, wS, cS, fiS;
        };

        //==============================================================================
        /** Pico Atomic Cluster: decomposizione spettrale in 'atomi' risintetizzati. */
        class B3Atomic final : public Effect
        {
        public:
            static constexpr int maxAtoms = 48;
            explicit B3Atomic (const ModelDef& d) : Effect (d)
            {
                pVol = role (*this, "level", 0.6f); pBlend = role (*this, "blend", 0.7f); pSpeed = role (*this, "rate", 0.5f);
                pAtoms = role (*this, "atoms", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                order = s > 70000 ? 12 : 11;
                N = 1 << order;
                fft = std::make_unique<juce::dsp::FFT> (order);
                ring.assign ((size_t) N, 0.0f);
                work.assign ((size_t) N * 2, 0.0f);
                win.assign ((size_t) N, 0.0f);
                for (int k = 0; k < N; ++k) win[(size_t) k] = 0.5f - 0.5f * std::cos (6.2831853f * (float) k / (float) N);
                winSum = 0.5f * (float) N;
                volS.set (s, 20); blS.set (s, 20);
                reset();
            }
            void reset() override
            {
                std::fill (ring.begin(), ring.end(), 0.0f);
                for (auto& o : osc) o = {};
                wp = 0; countdown = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // SPEED: aggiornamento da 1,5 s a 4 ms; ATOMS: da 1 a 48 oscillazioni
                const int period = std::max (32, (int) (logMap (pSpeed.get (*this), 1.5f, 0.004f) * sr));
                const int K = std::clamp (1 + (int) std::lround (47.0f * std::pow (pAtoms.get (*this), 1.6f)), 1, maxAtoms);
                const bool smooth = pMode.step (*this) == 1;
                const float vol = volKnob (pVol.get (*this), 0.6f, 2.5f);
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    ring[(size_t) wp] = x; wp = (wp + 1) & (N - 1);
                    if (--countdown <= 0) { countdown = period; refresh (K, smooth ? period : (int) (0.002f * sr)); }
                    float y = 0;
                    for (int k = 0; k < maxAtoms; ++k)
                    {
                        auto& o = osc[k];
                        if (o.amp <= 0.0f && o.target <= 0.0f) continue;
                        if (o.left > 0) { o.amp += o.dA; o.w += o.dW; --o.left; if (o.left == 0) { o.amp = o.target; o.w = o.wT; } }
                        o.ph += o.w; if (o.ph > 6.2831853f) o.ph -= 6.2831853f;
                        y += o.amp * std::sin (o.ph);
                    }
                    const float b = blS.next (pBlend.get (*this));
                    const float out = outRail (volS.next (vol) * ((1.0f - b) * x + b * y));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            struct Osc { float amp = 0, target = 0, dA = 0, w = 0, wT = 0, dW = 0, ph = 0; int left = 0; };
            void refresh (int K, int rampN)
            {
                for (int k = 0; k < N; ++k) work[(size_t) k] = ring[(size_t) ((wp + k) & (N - 1))] * win[(size_t) k];
                std::fill (work.begin() + N, work.end(), 0.0f);
                fft->performFrequencyOnlyForwardTransform (work.data());
                // picchi locali, i K piu' forti
                float pk[maxAtoms]; float pf[maxAtoms]; int np = 0;
                const int kLo = std::max (2, (int) (40.0f * (float) N / sr)), kHi = std::min (N / 2 - 2, (int) (9000.0f * (float) N / sr));
                float mx = 0; for (int k = kLo; k <= kHi; ++k) mx = std::max (mx, work[(size_t) k]);
                const float floorM = std::max (mx * 0.003f, 2.0e-4f * winSum);
                for (int k = kLo; k <= kHi; ++k)
                {
                    const float m = work[(size_t) k];
                    if (m < floorM || m < work[(size_t) k - 1] || m < work[(size_t) k + 1]) continue;
                    // parabola sul logaritmo per frequenza e ampiezza
                    const float a = std::log (work[(size_t) k - 1] + 1e-12f), b = std::log (m + 1e-12f), c = std::log (work[(size_t) k + 1] + 1e-12f);
                    const float den = a - 2.0f * b + c;
                    const float dd = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
                    const float amp = 2.0f * std::exp (b - 0.25f * (a - c) * dd) / winSum;   // Hann: picco = A * N / 4
                    const float f = ((float) k + dd) * sr / (float) N;
                    if (np < K) { pk[np] = amp; pf[np] = f; ++np; }
                    else
                    {
                        int w = 0; for (int j = 1; j < np; ++j) if (pk[j] < pk[w]) w = j;
                        if (amp > pk[w]) { pk[w] = amp; pf[w] = f; }
                    }
                }
                // assegnazione agli oscillatori: stesso oscillatore se la frequenza e' vicina (fase continua)
                bool used[maxAtoms] {};
                bool taken[maxAtoms] {};
                for (int j = 0; j < np; ++j)
                {
                    const float wT = 6.2831853f * pf[j] / sr;
                    int best = -1; float bd = 0.03f;
                    for (int k = 0; k < maxAtoms; ++k)
                        if (! used[k] && osc[k].target > 0.0f) { const float dv = std::abs (osc[k].wT - wT) / wT; if (dv < bd) { bd = dv; best = k; } }
                    if (best >= 0) { used[best] = true; taken[j] = true; setOsc (osc[best], pk[j], wT, rampN); }
                }
                for (int j = 0; j < np; ++j)
                {
                    if (taken[j]) continue;
                    const float wT = 6.2831853f * pf[j] / sr;
                    for (int k = 0; k < maxAtoms; ++k)
                        if (! used[k] && osc[k].target <= 0.0f && osc[k].amp <= 1e-6f) { used[k] = true; osc[k].w = wT; osc[k].amp = 0; setOsc (osc[k], pk[j], wT, rampN); break; }
                }
                for (int k = 0; k < maxAtoms; ++k) if (! used[k] && osc[k].target > 0.0f) setOsc (osc[k], 0.0f, osc[k].wT, rampN);
            }
            static void setOsc (Osc& o, float a, float wT, int rampN)
            {
                o.target = a; o.wT = wT;
                o.left = std::max (1, rampN);
                o.dA = (a - o.amp) / (float) o.left;
                o.dW = (wT - o.w) / (float) o.left;
                if (o.amp <= 1e-6f) { o.w = wT; o.dW = 0; }
            }
            float sr = 48000, winSum = 1024;
            int order = 11, N = 2048, wp = 0, countdown = 0;
            std::unique_ptr<juce::dsp::FFT> fft;
            std::vector<float> ring, work, win;
            Osc osc[maxAtoms];
            RoleParam pVol, pBlend, pSpeed, pAtoms, pMode;
            Smooth volS, blS;
        };

        //==============================================================================
        /** Vocoder a FFT (8-256 bande) per V256 e Iron Lung. */
        class FftVocoder
        {
        public:
            static constexpr int order = 10, N = 1 << order, hop = N / 4, maxBands = 256;
            void prepare (double s)
            {
                sr = (float) s;
                fft = std::make_unique<juce::dsp::FFT> (order);
                for (auto* v : { &inM, &inC, &outAcc }) v->assign ((size_t) N, 0.0f);
                for (auto* v : { &bufM, &bufC }) v->assign ((size_t) N * 2, 0.0f);
                win.assign ((size_t) N, 0.0f);
                for (int k = 0; k < N; ++k) win[(size_t) k] = 0.5f - 0.5f * std::cos (6.2831853f * (float) k / (float) N);
                clear();
            }
            void clear()
            {
                for (auto* v : { &inM, &inC, &outAcc }) std::fill (v->begin(), v->end(), 0.0f);
                std::fill (std::begin (envB), std::end (envB), 0.0f);
                pos = 0; rd = 0; bandsSet = -1;
            }
            /** bands 8..256, formant = fattore di spostamento dei formanti, tone -1..1 (armoniche/brillantezza). */
            void set (int bands, float formant, float tone) { nb = std::clamp (bands, 4, maxBands); fmt = formant; tn = tone; }
            /** Un campione di modulatore (voce) e portante; uscita ritardata di N campioni. */
            inline float tick (float mod, float car) noexcept
            {
                // il campione piu' vecchio esce (tutte le finestre che lo coprono sono gia' sommate)
                const float y = outAcc[(size_t) pos];
                outAcc[(size_t) pos] = 0.0f;
                inM[(size_t) pos] = mod; inC[(size_t) pos] = car;
                pos = (pos + 1) & (N - 1);
                if (++cnt >= hop) { cnt = 0; frame(); }
                return y;
            }
        private:
            void layout (float sampleRate)
            {
                // bordi delle bande logaritmici 80 Hz - 11 kHz in bin (frazionari)
                for (int b = 0; b <= nb; ++b)
                    edge[b] = 80.0f * std::pow (11000.0f / 80.0f, (float) b / (float) nb) * (float) N / sampleRate;
                bandsSet = nb;
            }
            void frame()
            {
                if (bandsSet != nb) layout (sr);
                for (int k = 0; k < N; ++k)
                {
                    const size_t idx = (size_t) ((pos + k) & (N - 1));
                    bufM[(size_t) k] = inM[idx] * win[(size_t) k];
                    bufC[(size_t) k] = inC[idx] * win[(size_t) k];
                }
                fft->performRealOnlyForwardTransform (bufM.data(), true);
                fft->performRealOnlyForwardTransform (bufC.data(), true);
                // energia per banda di modulatore (con spostamento dei formanti) e portante
                const float relA = 0.55f;
                float emB[maxBands], ecB[maxBands], emTot = 0, ecTot = 0;
                for (int b = 0; b < nb; ++b)
                {
                    const float lo = edge[b], hi = edge[b + 1];
                    const int k0 = std::max (1, (int) std::floor (lo)), k1 = std::max (k0, std::min (N / 2 - 1, (int) std::ceil (hi)));
                    float em = 0, ec = 0;
                    for (int k = k0; k <= k1; ++k)
                    {
                        const float km = std::clamp ((float) k / fmt, 1.0f, (float) (N / 2 - 2));
                        const int kmI = (int) km; const float fr = km - (float) kmI;
                        const float m0 = mag2 (bufM, kmI), m1 = mag2 (bufM, kmI + 1);
                        em += m0 + (m1 - m0) * fr;
                        ec += mag2 (bufC, k);
                    }
                    emB[b] = em; ecB[b] = ec; emTot += em; ecTot += ec;
                }
                // guadagni regolarizzati (pavimento di portante) e normalizzati: l'energia d'uscita segue quella del modulatore
                const float ecFloor = 2.0e-3f * ecTot / (float) nb + 1.0e-9f;
                float eOut = 0, g[maxBands];
                for (int b = 0; b < nb; ++b) { g[b] = std::min (8.0f, std::sqrt (emB[b] / (ecB[b] + ecFloor))); eOut += g[b] * g[b] * ecB[b]; }
                const float kN = std::clamp (std::sqrt (emTot / (eOut + 1.0e-9f)), 0.0f, 4.0f);
                for (int b = 0; b < nb; ++b)
                {
                    const float lo = edge[b], hi = edge[b + 1];
                    const int k0 = std::max (1, (int) std::floor (lo)), k1 = std::max (k0, std::min (N / 2 - 1, (int) std::ceil (hi)));
                    const float gb = g[b] * kN;
                    envB[b] = gb > envB[b] ? gb : gb + relA * (envB[b] - gb);              // rilascio ~40 ms
                    for (int k = k0; k <= k1; ++k) gainBin[k] = std::min (envB[b], 16.0f);
                }
                // fuori dalle bande: zero; TONE: inclinazione +-10 dB sopra 2 kHz
                const int kMin = std::max (1, (int) std::floor (edge[0])), kMax = std::min (N / 2 - 1, (int) std::ceil (edge[nb]));
                const float tiltK = 2000.0f * (float) N / sr;
                for (int k = 0; k <= N / 2; ++k)
                {
                    float gk = (k >= kMin && k <= kMax) ? gainBin[k] : 0.0f;
                    if ((float) k > tiltK) gk *= std::pow (10.0f, 0.5f * tn * std::min (1.0f, std::log2 ((float) k / tiltK)));
                    bufC[(size_t) (2 * k)] *= gk; bufC[(size_t) (2 * k + 1)] *= gk;
                }
                fft->performRealOnlyInverseTransform (bufC.data());
                // sintesi con finestra di Hann (somma dei quadrati = 1,5 a passo N/4)
                const float norm = 1.0f / 1.5f;
                for (int k = 0; k < N; ++k)
                {
                    const size_t idx = (size_t) ((pos + k) & (N - 1));
                    outAcc[idx] += bufC[(size_t) k] * win[(size_t) k] * norm;
                }
            }
            static inline float mag2 (const std::vector<float>& b, int k) noexcept { return b[(size_t) (2 * k)] * b[(size_t) (2 * k)] + b[(size_t) (2 * k + 1)] * b[(size_t) (2 * k + 1)]; }
            float sr = 48000, fmt = 1, tn = 0;
            int nb = 64, bandsSet = -1, pos = 0, cnt = 0, rd = 0;
            float edge[maxBands + 1] {}, envB[maxBands] {}, gainBin[N / 2 + 2] {};
            std::unique_ptr<juce::dsp::FFT> fft;
            std::vector<float> inM, inC, outAcc, bufM, bufC, win;
        };

        class B3Vocoder final : public Effect
        {
        public:
            enum Mode { Robo1, Robo2, Robo3, Single, Major, Minor, Transpose, InstCtrl, Reflex };
            explicit B3Vocoder (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                iron = cfg.str ("model", "v256") == "ironlung";
                pBlend = role (*this, "blend", 1.0f); pBands = role (*this, "bands", 0.5f); pTone = role (*this, "tone", 0.5f);
                pGender = role (*this, "gender", 0.5f); pPitch = role (*this, "pitch", 0.5f); pMode = role (*this, "mode", 0.0f);
                pMic = role (*this, "micgain", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                voc.prepare (s);
                bank.prepare (s, 70.0f, 1600.0f, 6, 0.1f, 0);
                bankI.prepare (s, 50.0f, 2000.0f, 4, 0.14f, 0);
                ps.prepare (s);
                dryDl.allocate (FftVocoder::N + 64);
                fader.prepare (s, 15);
                blS.set (s, 20); f0S.set (s, 15); ratS.set (s, 30); carS.set (s, 20); micS.set (s, 20);
                reset();
            }
            void reset() override { voc.clear(); bank.reset(); bankI.reset(); ps.clear(); dryDl.clear(); f0 = 0; fI = 0; for (auto& p : ph) p = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = iron ? -1 : std::clamp (pMode.step (*this), 0, 8);
                const float micT = std::exp2 ((float) pMic.step (*this) - 1.0f);
                const float bk = pBands.get (*this);
                const int bands = iron ? 256 : (int) std::lround (8.0f * std::pow (32.0f, bk));
                const float fmt = std::exp2 ((pGender.get (*this) - 0.5f) * 1.0f);          // +-6 semitoni di formanti
                const float tone = (pTone.get (*this) - 0.5f) * 2.0f;
                voc.set (bands, fmt, tone);
                const float pk = pPitch.get (*this);
                const bool synthOn = pk > 0.03f && pk < 0.97f;
                const float pitchMul = std::exp2 ((pk - 0.5f) * 2.0f);                        // +-1 ottava
                const float glide = 1.0f - std::exp (-1.0f / ((0.005f + 0.4f * std::abs (bk - 0.5f) * 2.0f) * sr));
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (mode + 1, changed);
                    if (changed) { voc.clear(); ps.clear(); }
                    const int m = fader.current - 1;
                    const float micG = micS.next (micT);
                    // ingresso unico: in mono voce e strumento coincidono; in stereo L = voce (microfono), R = strumento
                    const float voice = ch[0][i] * micG;
                    const float inst = numCh > 1 ? ch[1][i] : 0.0f;
                    float wet = 0;
                    if (m == -1)
                    {
                        // Iron Lung: portante = strumento (R) o, in mono, lo stesso segnale (spostamento dei formanti)
                        const float car = numCh > 1 ? inst : voice;
                        const float carH = car + std::max (0.0f, tone) * 0.5f * std::tanh (4.0f * car);   // TONE oltre ore 12: armoniche
                        wet = voc.tick (voice, carH);
                    }
                    else if (m <= Minor)
                    {
                        float car = numCh > 1 ? inst : 0.0f;
                        if (synthOn)
                        {
                            if (m <= Robo3) car += 0.3f * buzz (130.81f * pitchMul, 0);                   // robot: Do3
                            else if (m == Single) car += 0.3f * organ (130.81f * pitchMul, 0);
                            else if (m == Major) car += 0.2f * (organ (261.63f * pitchMul, 0) + organ (329.63f * pitchMul, 1) + organ (392.0f * pitchMul, 2));
                            else car += 0.2f * (organ (440.0f * pitchMul, 0) + organ (523.25f * pitchMul, 1) + organ (659.26f * pitchMul, 2));
                        }
                        wet = voc.tick (voice, car + std::max (0.0f, tone) * 0.003f * noise.uni());
                    }
                    else
                    {
                        // trasposizione / controllo dallo strumento / correzione d'intonazione (TD-PSOLA)
                        bank.push (voice);
                        const float p = bankPitch (bank, 0.25f);
                        if (p > 60 && p < 1500) f0 = f0S.next (p);
                        float ratio = 1.0f;
                        if (m == Transpose) ratio = pitchMul;
                        else if (m == InstCtrl)
                        {
                            if (numCh > 1) { bankI.push (inst); const float q = bankPitch (bankI, 0.25f); if (q > 40 && q < 1500 && bankI.peak() > 0.003f * (1.0f + 20.0f * (1.0f - pk))) fI = q; }
                            ratio = (fI > 0 && f0 > 0) ? fI / f0 : 1.0f;
                        }
                        else if (f0 > 0)
                        {
                            const float nt = std::round (12.0f * std::log2 (f0 / 440.0f));
                            ratio = 440.0f * std::exp2 (nt / 12.0f) / f0;
                        }
                        rat += (std::clamp (ratio, 0.5f, 2.0f) - rat) * glide;
                        wet = ps.tick (voice, f0, rat, fmt);
                        dryDl.push (voice);
                    }
                    if (m != Transpose && m != InstCtrl && m != Reflex) dryDl.push (voice);
                    // dry allineato alla latenza del vocoder (N campioni) per il BLEND
                    const float dry = dryDl.read ((float) (m >= Transpose ? ps.latency() : FftVocoder::N));
                    const float b = blS.next (pBlend.get (*this));
                    const float out = outRail (((1.0f - b) * dry + b * fg * wet) / micG);
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            inline float buzz (float f, int k) noexcept
            {
                ph[k] += f / sr; ph[k] -= std::floor (ph[k]);
                return ph[k] < 0.18f ? 1.0f : -0.22f;                                         // impulso stretto ricco di armoniche
            }
            inline float organ (float f, int k) noexcept
            {
                ph[k] += f / sr; ph[k] -= std::floor (ph[k]);
                const float a = 6.2831853f * ph[k];
                return std::sin (a) + 0.6f * std::sin (2 * a) + 0.4f * std::sin (3 * a) + 0.3f * std::sin (4 * a) + 0.2f * std::sin (6 * a) + 0.15f * std::sin (8 * a);
            }
            Config cfg;
            bool iron = false;
            float sr = 48000, f0 = 0, fI = 0, rat = 1, ph[3] {};
            RoleParam pBlend, pBands, pTone, pGender, pPitch, pMode, pMic;
            FftVocoder voc;
            SpectralBank bank, bankI;
            PsolaB ps;
            DelayLine dryDl;
            XFader fader;
            Noise noise;
            Smooth blS, f0S, ratS, carS, micS;
        };

        //==============================================================================
        /** Superego+: congelamento a granuli con 5 modi e 11 effetti. */
        class B3Superego final : public Effect
        {
        public:
            enum Mode { Moment, Sustain, Auto, Latch, Live };
            enum Fx { Flange, Phase, Mod, Rotary_, Trem1, Trem2, PitchFx, Filter, Detune, Echo, DelayFx };
            explicit B3Superego (const ModelDef& d) : Effect (d)
            {
                pDry = role (*this, "direct", 0.6f); pEff = role (*this, "level", 0.6f); pAtk = role (*this, "attack", 0.2f);
                pDec = role (*this, "decay", 0.6f); pRate = role (*this, "rate", 0.5f); pDepth = role (*this, "depth", 0.5f);
                pThr = role (*this, "threshold", 0.6f); pLayer = role (*this, "layer", 0.0f); pGliss = role (*this, "gliss", 0.0f);
                pType = role (*this, "type", 0.0f); pMode = role (*this, "mode", 0.5f); pFxOn = role (*this, "fxon", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                capLen = (int) (0.16 * s);
                ring.allocate ((int) (0.6 * s));
                for (auto& f : frz) f.assign ((size_t) capLen + 4, 0.0f);
                bank.prepare (s, 60.0f, 2000.0f, 4, 0.14f, 0);
                onset.prepare (s, 0.004f);
                gateEnv.set (s, 2, 80);
                mist.prepare (s);
                rot.prepare (s);
                for (auto& g : sh) g.prepare (s, 50);
                dl.allocate ((int) (1.6 * s));
                fxFader.prepare (s, 15);
                for (auto* sm : { &dS, &wS, &rS, &dpS, &fxS, &fxOnS }) sm->set (s, 20);
                for (auto& sm : stS) sm.set (s, 20);
                reset();
            }
            void reset() override
            {
                ring.clear(); dl.clear();
                for (auto& f : frz) std::fill (f.begin(), f.end(), 0.0f);
                for (auto& g : grains) g = {};
                cur = 0; capPending = -1; level[0] = level[1] = 0; rate[0] = rate[1] = 1; pitch[0] = pitch[1] = 0; curPitch = 0;
                mist.reset(); rot.reset(); for (auto& a : ap) a.reset(); filt.reset(); for (auto& g : sh) g.clear();
                fbD = 0; quietN = 0; held = false; fxPh = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = std::clamp (pMode.step (*this), 0, 4);
                const float atkS = logMap (pAtk.get (*this), 0.004f, 4.0f);
                const float dk = pDec.get (*this);
                const bool infinite = dk > 0.97f;
                const float decS = logMap (dk, 0.03f, 10.0f);
                const float decC = (float) std::exp (-6.9 / (decS * sr));
                onset.setThreshold (0.1f * std::pow (0.01f, pThr.get (*this)));          // CW: anche il tocco leggero cattura
                const float layer = pLayer.get (*this);
                const float glissS = pGliss.get (*this) < 0.02f ? 0.0f : logMap (pGliss.get (*this), 0.02f, 3.0f);
                const float dry = 2.0f * taperA (pDry.get (*this)) * 1.41f / 1.27f;      // fino a +3 dB
                const float eff = 2.0f * taperA (pEff.get (*this));
                const int fxType = std::clamp (pType.step (*this), 0, 10);
                const bool fxOn = pFxOn.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    ring.push (x);
                    float frozen = 0;
                    if (mode != Live)
                    {
                        bank.push (x);
                        const float p = bankPitch (bank);
                        if (p > 0) curPitch = p;
                        const bool on = onset.tick (x);
                        const float e = gateEnv.tick (x);
                        const bool playing = e > 0.002f;
                        quietN = playing ? 0 : quietN + 1;
                        // trigger per modo (nessun footswitch nel plugin: il segnale fa da pedale tenuto)
                        bool trig = false;
                        if (mode == Moment) { if (on && ! held) { trig = true; held = true; } if (quietN > (int) (0.3f * sr)) held = false; }
                        else trig = on;
                        if (trig && capPending < 0) capPending = (int) (0.035f * sr) + capLen;
                        if (capPending >= 0 && --capPending < 0) capture (layer, glissS);
                        // inviluppo del congelato: ATTACK in entrata, DECAY in uscita
                        bool sustainOn = true;
                        if (mode == Moment) sustainOn = held;
                        else if (mode == Sustain) sustainOn = quietN < (int) (0.3f * sr);
                        for (int s = 0; s < 2; ++s)
                        {
                            if (level[s] <= 0) continue;
                            const bool fadingOld = s != cur;
                            if (fadingOld) fadeG[s] *= (float) std::exp (-1.0 / (std::max (0.01f, atkS) * sr));     // il vecchio lascia il posto al nuovo
                            else if (fadeG[s] < 1.0f) fadeG[s] = std::min (1.0f, fadeG[s] + 1.0f / (atkS * sr));
                            const bool decaying = (mode == Auto && ! infinite) || ! sustainOn;
                            if (decaying && mode != Latch) level[s] *= decC;
                            if (glissS > 0) rate[s] += (1.0f - rate[s]) * glissK;
                            frozen += fadeG[s] * level[s] * cloud (s, rate[s]);
                            if (level[s] < 1.0e-4f) level[s] = 0;
                        }
                    }
                    // effetti interni (sul congelato o, in LIVE EFFECTS, sul segnale diretto)
                    const float src = mode == Live ? x : frozen;
                    bool chg;
                    const float fg = fxFader.tick (fxOn ? fxType : 11, chg);            // 11 = effetti spenti
                    if (chg) { mist.reset(); rot.reset(); for (auto& a : ap) a.reset(); filt.reset(); for (auto& g : sh) g.clear(); dl.clear(); fbD = 0; }
                    float fl = src, fr = src;
                    if (fxFader.current < 11) effect (fxFader.current, src, fl, fr);
                    fl = fg * fl + (1.0f - fg) * src; fr = fg * fr + (1.0f - fg) * src;
                    const float dg = dS.next (dry), wg = wS.next (eff);
                    if (mode == Live)
                    {
                        // LIVE EFFECTS: il dry passa negli effetti; DRY = diretto, EFFECT = livello degli effetti accesi
                        const float on = fxOnS.next (fxOn ? 1.0f : 0.0f);
                        if (numCh > 1) { const float a = ch[0][i], b = ch[1][i]; ch[0][i] = outRail (dg * a + on * wg * fl); ch[1][i] = outRail (dg * b + on * wg * fr); }
                        else ch[0][i] = outRail (dg * x + on * wg * 0.5f * (fl + fr));
                    }
                    else
                    {
                        if (numCh > 1) { const float a = ch[0][i], b = ch[1][i]; ch[0][i] = outRail (dg * a + wg * fl); ch[1][i] = outRail (dg * b + wg * fr); }
                        else ch[0][i] = outRail (dg * x + wg * 0.5f * (fl + fr));
                    }
                }
            }
        private:
            struct Grain { float pos = 0, len = 1, t = 0; bool on = false; };
            inline float cloud (int s, float r) noexcept
            {
                const auto& buf = frz[s];
                float acc = 0;
                const float glen = 0.35f * (float) capLen;
                const float room = std::max (0.0f, (float) capLen - 4.0f - glen * std::max (1.0f, r));
                for (int k = 0; k < nv; ++k)
                {
                    auto& g = grains[s * 8 + k];
                    if (! g.on) { g.on = true; g.len = glen; g.t = glen * (float) k / (float) nv; g.pos = rnd() * room; }
                    const float w = std::sin (pi * g.t / g.len);
                    const float idx = std::clamp (g.pos + g.t * r, 0.0f, (float) capLen - 2.0f);
                    const int i0 = (int) idx; const float f = idx - (float) i0;
                    acc += w * w * (buf[(size_t) i0] + (buf[(size_t) i0 + 1] - buf[(size_t) i0]) * f);
                    g.t += 1.0f;
                    if (g.t >= g.len) { g.t = 0; g.pos = rnd() * room; }
                }
                return acc * 2.0f / (float) nv * 1.15f;
            }
            void capture (float layer, float glissS)
            {
                const int nxt = cur ^ 1;
                auto& dst = frz[nxt];
                // LAYER: il congelato precedente entra nel nuovo (come il feedback di un delay)
                const float prevL = level[cur];
                float pk = 0;
                for (int k = 0; k < capLen; ++k)
                {
                    dst[(size_t) k] = ring.read ((float) (capLen - k)) + layer * prevL * frz[cur][(size_t) k];
                    pk = std::max (pk, std::abs (dst[(size_t) k]));
                }
                // strati che si accumulano (LATCH, LAYER al massimo): il congelato resta sotto 1 V di picco
                if (pk > 1.0f) { const float k1 = 1.0f / pk; for (int k = 0; k < capLen; ++k) dst[(size_t) k] *= k1; }
                level[nxt] = 1.0f; fadeG[nxt] = 0.0f;
                pitch[nxt] = curPitch;
                rate[nxt] = (glissS > 0 && pitch[cur] > 0 && curPitch > 0) ? std::clamp (pitch[cur] / curPitch, 0.5f, 2.0f) : 1.0f;
                glissK = glissS > 0 ? 1.0f - std::exp (-1.0f / (glissS * sr)) : 1.0f;
                for (int k = 0; k < 8; ++k) grains[nxt * 8 + k].on = false;
                cur = nxt;
            }
            void effect (int t, float x, float& l, float& r) noexcept
            {
                const float rk = rS.next (pRate.get (*this)), dk = dpS.next (pDepth.get (*this));
                switch (t)
                {
                    case Flange:   // Electric Mistress: RATE = filter matrix fino a ore 11, poi velocita'; DEPTH = feedback
                        mist.tick (x, rk, 0.42f, 1.0f, 0.0f, 0.93f * dk, 1.0f, l, r); break;
                    case Phase:    // Small Stone: 4 all-pass, DEPTH = colore
                    {
                        fxPh += logMap (rk, 0.1f, 8.0f) / sr; fxPh -= std::floor (fxPh);
                        const float lf = fxPh < 0.5f ? 4.0f * fxPh - 1.0f : 3.0f - 4.0f * fxPh;
                        const float a = apCoef (150.0f + 2450.0f * (0.5f + 0.5f * lf), sr);
                        float v = x + loopSat (0.6f * dk * fbD);
                        for (int k = 0; k < 4; ++k) v = ap[k].tick (v, a);
                        fbD = v; l = r = 0.55f * (x + v) * 1.1f; break;
                    }
                    case Mod:      // DEPTH a ore 12 = 0; sinistra vibrato, destra chorus
                    {
                        fxPh += logMap (rk, 0.1f, 8.0f) / sr; fxPh -= std::floor (fxPh);
                        const float s = std::sin (6.2831853f * fxPh);
                        dl.push (x);
                        const float amt = std::abs (dk - 0.5f) * 2.0f;
                        if (dk < 0.5f) l = r = dl.read ((0.004f + 0.003f * amt * s) * sr);
                        else { const float w = dl.read ((0.009f + 0.004f * amt * s) * sr); l = r = 0.7f * (x + amt * w); }
                        break;
                    }
                    case Rotary_:  // DEPTH: bilanciamento tromba/rotore (a sinistra piu' tromba)
                    {
                        const float horn = logMap (rk, 0.6f, 7.0f);
                        rot.set (horn, horn * 0.9f, 0.8, 2.5, 1.0f - dk);
                        rot.tick (x, l, r); break;
                    }
                    case Trem1: case Trem2:
                    {
                        fxPh += logMap (rk, 0.5f, 14.0f) / sr; fxPh -= std::floor (fxPh);
                        const float s = std::sin (6.2831853f * fxPh);
                        const float u = t == Trem1 ? 0.5f + 0.5f * s : stS[0].next (s > 0 ? 1.0f : 0.0f);
                        l = r = x * (1.0f - dk * u); break;
                    }
                    case PitchFx:  // RATE a ore 12 = unisono; a destra scala maggiore (con 3a e 7a minori) fino a +1 ottava, a sinistra fino a -1
                    {
                        static const float up[10] = { 0, 2, 3, 4, 5, 7, 9, 10, 11, 12 }, dn[8] = { 0, -1, -3, -5, -7, -8, -10, -12 };
                        float semis;
                        if (rk >= 0.5f) semis = up[std::clamp ((int) ((rk - 0.5f) * 2.0f * 9.99f), 0, 9)];
                        else semis = dn[std::clamp ((int) ((0.5f - rk) * 2.0f * 7.99f), 0, 7)];
                        sh[0].setRatio (std::exp2 (semis / 12.0f));
                        const float y = sh[0].tick (x);
                        l = r = (1.0f - 0.5f * dk) * y + 0.5f * dk * x; break;
                    }
                    case Filter:   // passa-basso risonante: RATE frequenza, DEPTH risonanza
                    {
                        filt.setG (fastTanPi (logMap (rk, 120.0f, 12000.0f), sr), 1.0f / logMap (dk, 0.7f, 10.0f));
                        l = r = filt.lp (x); break;
                    }
                    case Detune:   // due copie stonate +-; DEPTH quantita', RATE velocita' della deriva
                    {
                        fxPh += logMap (rk, 0.05f, 2.0f) / sr; fxPh -= std::floor (fxPh);
                        const float c = 30.0f * dk * (0.7f + 0.3f * std::sin (6.2831853f * fxPh));
                        sh[0].setRatio (std::exp2 (c / 1200.0f)); sh[1].setRatio (std::exp2 (-c / 1200.0f));
                        l = 0.6f * x + 0.6f * sh[0].tick (x); r = 0.6f * x + 0.6f * sh[1].tick (x); break;
                    }
                    case Echo: case DelayFx:   // RATE = tempo, DEPTH = livello delle ripetizioni; feedback preimpostato
                    {
                        const float tm = logMap (rk, 0.06f, 1.5f);
                        float d = tm * sr;
                        if (t == DelayFx) { fxPh += 0.7f / sr; fxPh -= std::floor (fxPh); d *= 1.0f + 0.004f * std::sin (6.2831853f * fxPh); }
                        float y = dl.read (d);
                        if (t == DelayFx) { lpD.setFast (sr, 2800.0f); y = lpD.lp (y); y = 1.2f * softSat (y / 1.2f); }
                        dl.push (x + loopSat ((t == Echo ? 0.45f : 0.5f) * y));
                        l = r = x + dk * y; break;
                    }
                    default: l = r = x; break;
                }
            }
            inline float rnd() noexcept { return 0.5f + 0.5f * noise.uni(); }
            float sr = 48000, level[2] {}, rate[2] { 1, 1 }, pitch[2] {}, fadeG[2] {}, curPitch = 0, glissK = 1, fbD = 0, fxPh = 0;
            int capLen = 4096, nv = 6, cur = 0, capPending = -1, quietN = 0;
            bool held = false;
            std::vector<float> frz[2];
            Grain grains[16];
            DelayLine ring, dl;
            SpectralBank bank;
            OnsetB onset;
            Envelope gateEnv;
            MistressEngine mist;
            Rotary rot;
            Allpass1 ap[4];
            Svf filt;
            GrainShifter sh[2];
            OnePole lpD;
            XFader fxFader;
            Noise noise;
            RoleParam pDry, pEff, pAtk, pDec, pRate, pDepth, pThr, pLayer, pGliss, pType, pMode, pFxOn;
            Smooth dS, wS, rS, dpS, fxS, stS[1], fxOnS;
        };
    }

    std::unique_ptr<Effect> makeB3Pitch (const ModelDef& d, const std::string& type)
    {
        if (type == "b3pog")      return std::make_unique<B3Pog> (d);
        if (type == "b3fork")     return std::make_unique<B3Fork> (d);
        if (type == "b3ihm")      return std::make_unique<B3Ihm> (d);
        if (type == "b3slammi")   return std::make_unique<B3Slammi> (d);
        if (type == "b3ring")     return std::make_unique<B3Ring> (d);
        if (type == "b3atomic")   return std::make_unique<B3Atomic> (d);
        if (type == "b3vocoder")  return std::make_unique<B3Vocoder> (d);
        if (type == "b3superego") return std::make_unique<B3Superego> (d);
        return nullptr;
    }
}
