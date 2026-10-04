/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Pitch polifonico della tappa 3:
      * polyoct   - POG, POG2, Micro POG, HOG, HOG2: voci d'ottava e armoniche dal banco
                    di filtri complessi (FxSpectral.h) con intonazione esatta;
      * polyshift - Pitch Fork: ottave dal banco, altri intervalli dallo shifter granulare;
      * freeze    - Superego: congelamento a nuvola di granuli con layer e glissato;
      * voicebox  - armonizzatore TD-PSOLA (formanti conservati, GENDER = stiramento dei
                    granuli) con tonalita' stimata e vocoder a 32 bande.
*/

#include "FxSpectral.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        /** Rivelatore d'attacco: inviluppo veloce contro lento, con periodo refrattario. */
        struct Onset
        {
            Envelope fast, slow;
            int refr = 0, refrN = 4800;
            float thr = 0.003f;
            void prepare (double sr, float threshold = 0.003f) { fast.set (sr, 1, 25); slow.set (sr, 60, 300); refrN = (int) (0.09 * sr); thr = threshold; }
            inline bool tick (float x) noexcept
            {
                const float f = fast.tick (x), s = slow.tick (x);
                if (refr > 0) { --refr; return false; }
                if (f > thr && f > 1.7f * s + 0.0005f) { refr = refrN; return true; }
                return false;
            }
            float level() const noexcept { return fast.env; }
        };

        /** Detune "a velocita' fissa": ritardo modulato lentamente (+-cent proporzionali alla profondita'). */
        struct Detuner
        {
            DelayLine dl;
            Lfo lfo;
            float sr = 48000;
            void prepare (double s, float hz) { sr = (float) s; dl.allocate ((int) (0.03 * s)); lfo.setHz (s, hz); }
            void clear() { dl.clear(); }
            /** cents = deviazione di picco. */
            inline float tick (float x, float cents) noexcept
            {
                dl.push (x);
                const float dev = std::pow (2.0f, cents / 1200.0f) - 1.0f;
                const float amp = dev / (2.0f * pi * (float) (lfo.inc * sr)) * sr;          // campioni
                const float d = 2.0f + amp + amp * lfo.sine();
                lfo.step();
                return dl.read (std::max (1.0f, d));
            }
        };

        //==============================================================================
        class PolyOctFx final : public Effect
        {
        public:
            enum Model { Pog, Pog2, Micro, Hog, Hog2 };
            explicit PolyOctFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "pog");
                model = m == "pog2" ? Pog2 : m == "micro" ? Micro : m == "hog" ? Hog : m == "hog2" ? Hog2 : Pog;
                pGain = role (*this, "gain", 0.6f); pDirect = role (*this, "direct", 0.8f);
                pOct1 = role (*this, "oct1", 0.0f); pOct2 = role (*this, "oct2", 0.0f);
                pUp1 = role (*this, "up1", 0.0f); pUp1d = role (*this, "up1d", 0.0f);
                pUp2 = role (*this, "up2", 0.0f); pUp2d = role (*this, "up2d", 0.0f);
                pFreq = role (*this, "freq", 1.0f); pLpMode = role (*this, "lpmode", 0.0f);
                pAttack = role (*this, "attack", 0.0f); pDetune = role (*this, "detune", 0.0f); pQ = role (*this, "q", 0.0f);
                pGate = role (*this, "gate", 0.0f);
                for (int k = 0; k < 10; ++k) { char r[4] = { 'v', (char) ('0' + k), 0, 0 }; pV[k] = role (*this, r, 0.0f); }
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                const bool hog = model == Hog || model == Hog2;
                bank.prepare (s, 45.0f, 7000.0f, model == Hog2 ? 5 : 4, model == Hog2 ? 0.11f : 0.14f, 2);
                det1.prepare (s, 0.43f); det2.prepare (s, 0.31f);
                onset.prepare (s, 0.004f);
                for (auto& sm : gS) sm.set (s, 25);
                lpS.set (s, 30); dryS.set (s, 20); dfS.set (s, 10); ginS.set (s, 20); det1S.set (s, 20); det2S.set (s, 20);
                (void) hog;
                reset();
            }
            void reset() override { bank.reset(); det1.clear(); det2.clear(); lp.reset(); lpDry.reset(); swell = 1; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                auto vol = [] (float s) { return 1.56f * s * s; };          // cursore -> guadagno (unita' a 0,8)
                float gIn = 1.0f, gDry = vol (pDirect.get (*this));
                int nums[10], lev[10]; float gains[10];
                int nv = 0;
                float gDet1 = 0, gDet2 = 0, detCents = 8.0f, attackS = 0, q = 0.707f;
                bool dryInFilter = false, filterOn = pFreq.present();
                if (model == Pog)
                {
                    gIn = logMap (pGain.get (*this), 0.25f, 4.0f);
                    nums[nv] = 1; lev[nv] = 1; gains[nv++] = vol (pOct1.get (*this));
                    nums[nv] = 2; lev[nv] = 0; gains[nv++] = vol (pUp1.get (*this));
                    nums[nv] = 4; lev[nv] = 0; gains[nv++] = vol (pUp2.get (*this));
                    gDet1 = vol (pUp1d.get (*this)); gDet2 = vol (pUp2d.get (*this));
                    const bool m3 = pLpMode.step (*this) == 1;
                    q = m3 ? 2.0f : 1.0f; dryInFilter = m3;
                }
                else if (model == Pog2)
                {
                    nums[nv] = 1; lev[nv] = 2; gains[nv++] = vol (pOct2.get (*this));
                    nums[nv] = 1; lev[nv] = 1; gains[nv++] = vol (pOct1.get (*this));
                    const float dt = pDetune.get (*this);
                    // DETUNE: le ottave sopra passano per il detuner in proporzione
                    nums[nv] = 2; lev[nv] = 0; gains[nv++] = vol (pUp1.get (*this)) * (1.0f - 0.5f * dt);
                    nums[nv] = 4; lev[nv] = 0; gains[nv++] = vol (pUp2.get (*this)) * (1.0f - 0.5f * dt);
                    gDet1 = vol (pUp1.get (*this)) * 0.7f * dt; gDet2 = vol (pUp2.get (*this)) * 0.7f * dt;
                    detCents = 4.0f + 14.0f * dt;
                    attackS = pAttack.get (*this) * pAttack.get (*this) * 1.5f;
                    q = pQ.step (*this) == 1 ? 3.5f : 0.9f;
                }
                else if (model == Micro)
                {
                    filterOn = false;
                    nums[nv] = 1; lev[nv] = 1; gains[nv++] = vol (pOct1.get (*this));
                    nums[nv] = 2; lev[nv] = 0; gains[nv++] = vol (pUp1.get (*this));
                }
                else
                {
                    filterOn = false;
                    static const int hn[10] = { 1, 1, 1, 3, 2, 3, 4, 5, 8, 16 };
                    static const int hl[10] = { 2, 1, 0, 1, 0, 0, 0, 0, 0, 0 };
                    for (int k = 0; k < 10; ++k) { nums[nv] = hn[k]; lev[nv] = hl[k]; gains[nv++] = vol (pV[k].get (*this)); }
                }
                for (int k = 0; k < nv; ++k) gT[k] = gains[k];
                const float cut = filterOn ? logMap (pFreq.get (*this), 150.0f, 20000.0f) : 20000.0f;
                const bool gate = pGate.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    const float gi = ginS.next (gIn);
                    const float a = std::clamp (x * gi, -1.2f, 1.2f);                  // convertitore A/D
                    bank.setRelGate (gate ? 0.25f : 1.0e-3f);
                    bank.push (a);
                    float sm[10];
                    for (int k = 0; k < nv; ++k) sm[k] = gS[k].next (gT[k]);
                    float y = bank.voices (nums, lev, sm, nv) * kBankGain;
                    const float gd1 = det1S.next (gDet1), gd2 = det2S.next (gDet2);
                    if (gd1 > 1.0e-5f || gd2 > 1.0e-5f)
                    {
                        const float u1 = bank.voice (2, 0) * kBankGain, u2 = bank.voice (4, 0) * kBankGain;
                        y += det1.tick (u1 * gd1, detCents) + det2.tick (u2 * gd2, detCents * 1.3f);
                    }
                    if (attackS > 0.001f)
                    {
                        if (onset.tick (a)) duck = true;
                        // nuova nota: le voci scendono in 4 ms e poi rientrano con il tempo d'ATTACK
                        if (duck) { swell -= 1.0f / (0.004f * sr); if (swell <= 0) { swell = 0; duck = false; } }
                        else swell = std::min (1.0f, swell + 1.0f / (attackS * sr));
                        y *= swell * swell * (3.0f - 2.0f * swell);
                    }
                    y /= std::max (0.25f, gi);
                    float out;
                    const float gD = dryS.next (gDry);
                    if (filterOn)
                    {
                        const float c = lpS.next (cut);
                        lp.setG (fastTanPi (c, sr), 1.0f / q);
                        lpDry.setG (fastTanPi (c, sr), 1.0f / q);
                        const float k = dfS.next (dryInFilter ? 1.0f : 0.0f);
                        const float f1 = lp.lp (y + k * gD * x);                 // dry nel filtro (modo 3) ...
                        out = f1 + (1.0f - k) * gD * x;                          // ... o diretto (modo 1)
                    }
                    else out = y + gD * x;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
            float readout (int) const override { return 0; }
        private:
            Config cfg;
            Model model = Pog;
            float sr = 48000, swell = 1, gT[10] {};
            bool duck = false;
            RoleParam pGain, pDirect, pOct1, pOct2, pUp1, pUp1d, pUp2, pUp2d, pFreq, pLpMode, pAttack, pDetune, pQ, pGate, pV[10];
            SpectralBank bank;
            Detuner det1, det2;
            Onset onset;
            Svf lp, lpDry;
            Smooth gS[10], lpS, dryS, dfS, ginS, det1S, det2S;
        };

        //==============================================================================
        /** Pitch Fork. */
        class PolyShiftFx final : public Effect
        {
        public:
            explicit PolyShiftFx (const ModelDef& d) : Effect (d)
            {
                pBlend = role (*this, "blend", 0.5f); pShift = role (*this, "shift", 0.8f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 3);
                for (auto& g : gr) g.prepare (s, 45);
                fader.prepare (s);
                bS.set (s, 20);
                reset();
            }
            void reset() override { bank.reset(); for (auto& g : gr) g.clear(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sh = pShift.step (*this), mode = pMode.step (*this);
                const float bl = pBlend.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (sh * 3 + mode, changed);
                    if (changed) { voicesFor (fader.current / 3, fader.current % 3); for (auto& g : gr) g.clear(); }
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    bank.push (x);
                    float y = 0;
                    for (int v = 0; v < nVoices; ++v)
                    {
                        const auto& vc = voice[v];
                        if (vc.bank) y += bank.voice (vc.num, vc.level) * kBankGain;
                        else { gr[v].setRatio (vc.ratio); y += gr[v].tick (x); }
                    }
                    if (nVoices > 1) y *= 0.75f;
                    const float b = bS.next (bl);
                    const float out = (1.0f - b) * x + b * y * fg;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            struct Voice { bool bank = false; int num = 1, level = 0; float ratio = 1; };
            /** Voce per un intervallo in semitoni: ottave dal banco, il resto granulare. */
            static Voice make (float semis)
            {
                Voice v;
                const float a = std::abs (semis);
                if (a >= 11.99f && std::abs (a - 12.0f * std::round (a / 12.0f)) < 0.01f)
                {
                    const int o = (int) std::lround (a / 12.0f);
                    v.bank = true;
                    if (semis > 0) { v.num = 1 << o; v.level = 0; } else { v.num = 1; v.level = o; }
                }
                else v.ratio = std::pow (2.0f, semis / 12.0f);
                return v;
            }
            void voicesFor (int sh, int mode)
            {
                static const float iv[11] = { 0.17f, 1, 2, 4, 5, 7, 9, 10, 12, 24, 36 };
                if (mode < 2)
                {
                    const float s = sh == 0 ? 0.17f : iv[sh];
                    voice[0] = make (mode == 0 ? s : -s);
                    nVoices = 1;
                    return;
                }
                static const float dual[11][2] = { { 0.17f, -0.17f }, { 0.35f, -0.35f }, { 2, 9 }, { 4, 7 }, { 5, -7 }, { 7, -12 },
                                                   { 9, -7 }, { 10, 9 }, { 12, -12 }, { 24, -12 }, { 36, -12 } };
                voice[0] = make (dual[sh][0]); voice[1] = make (dual[sh][1]);
                nVoices = 2;
            }
            float sr = 48000;
            RoleParam pBlend, pShift, pMode;
            SpectralBank bank;
            GrainShifter gr[2];
            Voice voice[2];
            int nVoices = 1;
            struct Fader { int current = -1, pending = -1; float g = 1, step = 0.003f;
                void prepare (double s) { step = (float) (1.0 / (0.015 * s)); }
                float tick (int w, bool& ch) { ch = false; if (current < 0) { current = w; ch = true; }
                    if (w != current) pending = w;
                    if (pending >= 0) { g -= step; if (g <= 0) { g = 0; current = pending; pending = -1; ch = true; } }
                    else if (g < 1) g = std::min (1.0f, g + step);
                    return g; } } fader;
            Smooth bS;
        };

        //==============================================================================
        /** Superego: congelamento infinito. */
        class FreezeFx final : public Effect
        {
        public:
            explicit FreezeFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                grainS = (float) cfg.num ("grain", 0.16);
                nv = std::clamp ((int) cfg.num ("voices", 6), 2, 8);
                pSpeed = role (*this, "speed", 0.4f); pGliss = role (*this, "gliss", 0.2f);
                pDirect = role (*this, "direct", 0.7f); pLevel = role (*this, "level", 0.6f); pMode = role (*this, "mode", 1.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                capLen = (int) (grainS * s);
                ring.allocate ((int) (0.6 * s));
                for (auto& f : frz) f.assign ((size_t) capLen + 4, 0.0f);
                bank.prepare (s, 60.0f, 2000.0f, 4, 0.14f, 0);
                onset.prepare (s, 0.004f);
                dS.set (s, 20); wS.set (s, 20);
                reset();
            }
            void reset() override
            {
                ring.clear();
                for (auto& f : frz) std::fill (f.begin(), f.end(), 0.0f);
                for (auto& g : grains) g = {};
                cur = 0; capPending = -1; fadeNew = 1; level[0] = level[1] = 0; rate[0] = rate[1] = 1; pitch[0] = pitch[1] = 0;
                curPitch = 0; heldPitch = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = pMode.step (*this);                    // 0 LATCH, 1 MOMENTARY, 2 AUTO
                const float sp = pSpeed.get (*this);
                const float fadeS = logMap (sp, 0.01f, 3.0f);
                const float glissS = logMap (pGliss.get (*this), 0.004f, 2.0f);
                const float layer = mode == 0 ? sp : 0.0f;
                const float dry = 2.0f * taperA (pDirect.get (*this)), wet = 2.0f * taperA (pLevel.get (*this));
                const float glissOn = pGliss.get (*this) > 0.02f ? 1.0f : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    ring.push (x);
                    bank.push (x);
                    const float p = bankPitch (bank);
                    if (p > 0) curPitch = p;
                    if (onset.tick (x) && capPending < 0) capPending = (int) (0.035f * sr) + capLen;   // salta il transiente
                    if (capPending >= 0 && --capPending < 0) capture (layer, glissOn, glissS);
                    // MOMENTARY: il congelato segue la nota e sfuma quando la nota si spegne
                    if (mode == 1)
                    {
                        const float e = onset.level();
                        const float target = e > 0.002f ? 1.0f : 0.0f;
                        momentary += (target - momentary) / (fadeS * sr);
                        momentary = std::clamp (momentary, 0.0f, 1.0f);
                    }
                    else momentary = 1.0f;
                    fadeNew = std::min (1.0f, fadeNew + 1.0f / (fadeS * sr));
                    float y = 0;
                    for (int s = 0; s < 2; ++s)
                    {
                        const float g = s == cur ? fadeNew : 1.0f - fadeNew;
                        if (g <= 0.0001f || level[s] <= 0) continue;
                        // glissato: il rapporto di lettura scivola verso 1
                        rate[s] += (1.0f - rate[s]) * glissK;
                        y += g * level[s] * cloud (s, rate[s]);
                    }
                    const float out = dS.next (dry) * x + wS.next (wet) * momentary * y;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            struct Grain { float pos = 0, len = 1, t = 0; bool on = false; };
            /** Nuvola di nv granuli di Hann sfasati con partenze pseudo-casuali: suono continuo senza cuciture. */
            inline float cloud (int s, float r) noexcept
            {
                const auto& buf = frz[s];
                float acc = 0;
                const float glen = 0.35f * (float) capLen;
                const float room = std::max (0.0f, (float) capLen - 4.0f - glen * std::max (1.0f, r));
                for (int k = 0; k < nv; ++k)
                {
                    auto& g = grains[s * 8 + k];
                    if (! g.on)
                    {
                        g.on = true; g.len = glen;
                        g.t = glen * (float) k / (float) nv;
                        g.pos = rnd() * room;
                    }
                    const float w = std::sin (pi * g.t / g.len);
                    const float idx = std::clamp (g.pos + g.t * r, 0.0f, (float) capLen - 2.0f);
                    const int i0 = (int) idx; const float f = idx - (float) i0;
                    acc += w * w * (buf[(size_t) i0] + (buf[(size_t) i0 + 1] - buf[(size_t) i0]) * f);
                    g.t += 1.0f;
                    if (g.t >= g.len) { g.t = 0; g.pos = rnd() * room; }
                }
                return acc * 2.0f / (float) nv * 1.15f;
            }
            void capture (float layer, float glissOn, float glissS)
            {
                const int nxt = cur ^ 1;
                auto& dst = frz[nxt];
                // nuova cattura + strato del congelato precedente (LATCH)
                for (int k = 0; k < capLen; ++k)
                    dst[(size_t) k] = ring.read ((float) (capLen - k)) + layer * 0.8f * frz[cur][(size_t) k];
                level[nxt] = 1.0f;
                pitch[nxt] = curPitch;
                rate[nxt] = (glissOn > 0 && pitch[cur] > 0 && curPitch > 0) ? pitch[cur] / curPitch : 1.0f;
                glissK = 1.0f - std::exp (-1.0f / (glissS * sr));
                for (int k = 0; k < 8; ++k) grains[nxt * 8 + k].on = false;
                cur = nxt; fadeNew = 0;
            }
            inline float rnd() noexcept { return 0.5f + 0.5f * noise.uni(); }
            Config cfg;
            float sr = 48000, grainS = 0.16f, fadeNew = 1, level[2] {}, rate[2] { 1, 1 }, pitch[2] {}, curPitch = 0, heldPitch = 0,
                  glissK = 0.001f, momentary = 1;
            int capLen = 4096, nv = 6, cur = 0, capPending = -1;
            std::vector<float> frz[2];
            Grain grains[16];
            DelayLine ring;
            SpectralBank bank;
            Onset onset;
            Noise noise;
            RoleParam pSpeed, pGliss, pDirect, pLevel, pMode;
            Smooth dS, wS;
        };

        //==============================================================================
        /** TD-PSOLA per la voce: granuli di due periodi presi in ingresso e riposati a passo T/r;
            fmt stira il granulo (spostamento dei formanti, GENDER BENDER). */
        class Psola
        {
        public:
            void prepare (double s)
            {
                sr = (float) s;
                lat = (int) (0.03 * s);                 // 30 ms: granuli di 2 periodi fino a ~64 Hz con formanti +-6 semitoni
                in.allocate ((int) (0.2 * s));
                acc.assign ((size_t) accSize, 0.0f);
                clear();
            }
            void clear() { in.clear(); std::fill (acc.begin(), acc.end(), 0.0f); rd = 0; nextSyn = 0; }
            /** x = ingresso; f0 = nota (Hz, 0 = sorda); ratio = rapporto di pitch; fmt = fattore formanti. */
            inline float tick (float x, float f0, float ratio, float fmt) noexcept
            {
                in.push (x);
                const float T = f0 > 50 ? sr / f0 : sr / 140.0f;
                // nuovi granuli quando l'istante di sintesi raggiunge il tempo corrente
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
                // centro del granulo d'analisi a lat campioni nel passato, allineato al picco piu' vicino
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
                    const float t = ((float) k - half) * fmt;               // stiramento: formanti spostati di 1/fmt
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

        class VoiceBoxFx final : public Effect
        {
        public:
            explicit VoiceBoxFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                nb = std::clamp ((int) cfg.num ("bands", 32), 8, 40);
                pBlend = role (*this, "blend", 0.5f); pDryRev = role (*this, "dryrev", 0.2f); pHarmRev = role (*this, "harmrev", 0.3f);
                pGender = role (*this, "gender", 0.5f); pMix = role (*this, "voicemix", 0.5f); pMode = role (*this, "mode", 0.3f);
                pMic = role (*this, "micgain", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 70.0f, 1600.0f, 6, 0.1f, 0);
                for (auto& p : ps) p.prepare (s);
                fdn.prepare (s, 1.6f);
                fdn.set (2.2f, 6000.0f, 1.2f, 4.0f, 0.65f);
                for (int k = 0; k < nb; ++k)
                {
                    const float f = 100.0f * std::pow (80.0f, (float) k / (float) (nb - 1));
                    vm[k].set (s, f, 7.0); vc[k].set (s, f, 7.0);
                    ve[k] = 0;
                }
                envA = (float) std::exp (-1.0 / (0.012 * s));
                fader.prepare (s);
                bS.set (s, 20); f0S.set (s, 15);
                reset();
            }
            void reset() override
            {
                bank.reset();
                for (auto& p : ps) p.clear();
                fdn.clear();
                for (int k = 0; k < nb; ++k) { vm[k].reset(); vc[k].reset(); ve[k] = 0; }
                std::fill (std::begin (hist), std::end (hist), 0.0f);
                phase = 0; f0 = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = pMode.step (*this);
                const float micG = std::pow (2.0f, (float) pMic.step (*this) - 1.0f);
                const float gender = (pGender.get (*this) - 0.5f) * 2.0f;          // -1 maschile .. +1 femminile
                const float fmt = std::pow (2.0f, 0.5f * gender);                    // +-6 semitoni di formanti
                const float vmx = pMix.get (*this);
                const float dryRev = pDryRev.get (*this), harmRev = pHarmRev.get (*this);
                const float bl = pBlend.get (*this);
                updateKey();
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (mode, changed);
                    if (changed) for (auto& p : ps) p.clear();
                    const float x = (numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]) * micG;
                    bank.push (x);
                    const float p = bankPitch (bank, 0.25f);
                    if (p > 60 && p < 1500) { f0 = f0S.next (p); if (++histTick >= 64) { histTick = 0; addHist (p, bank.peak()); } }
                    float harm = 0;
                    if (fader.current == 0) harm = vocoder (x, fmt, vmx);
                    else
                    {
                        int semis[3]; float gains[3]; int nvv = 0;
                        intervals (fader.current, semis, gains, nvv, vmx);
                        for (int v = 0; v < nvv; ++v)
                            harm += gains[v] * ps[v].tick (x, f0, std::pow (2.0f, (float) semis[v] / 12.0f), fmt);
                    }
                    harm *= fg;
                    float rl, rr;
                    fdn.tick (dryRev * x + harmRev * harm, rl, rr);
                    const float b = bS.next (bl);
                    const float out = (1.0f - b) * x + b * harm + 0.6f * (rl + rr) * 0.5f;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out / micG;
                }
            }
        private:
            /** Vocoder: modulatore = ingresso, portante = dente di sega sulla nota (e ottava sotto, VOICE MIX);
                GENDER sposta la corrispondenza delle bande (formanti). */
            inline float vocoder (float x, float fmt, float vmx) noexcept
            {
                const float f = f0 > 60 ? f0 : 110.0f;
                phase += f / sr; phase -= std::floor (phase);
                phase2 += 0.5f * f / sr; phase2 -= std::floor (phase2);
                const float saw = (1.0f - vmx) * (2.0f * phase2 - 1.0f) + vmx * (2.0f * phase - 1.0f) + 0.05f * noise.uni();
                float y = 0;
                const float shift = std::log2 (fmt) * (float) (nb - 1) / std::log2 (80.0f);
                for (int k = 0; k < nb; ++k)
                {
                    const float m = vm[k].bp (x);
                    ve[k] = std::abs (m) + envA * (ve[k] - std::abs (m));
                }
                for (int k = 0; k < nb; ++k)
                {
                    const float src = std::clamp ((float) k - shift, 0.0f, (float) (nb - 1));
                    const int k0 = (int) src; const float fr = src - (float) k0;
                    const float e = ve[k0] + (ve[std::min (k0 + 1, nb - 1)] - ve[k0]) * fr;
                    y += e * vc[k].bp (saw);
                }
                return 1.5f * softSat (y * 1.2f / 1.5f);
            }
            /** Intervalli diatonici (semitoni) nella tonalita' stimata. */
            void intervals (int mode, int* semis, float* gains, int& nv, float vmx)
            {
                const float lo = 1.0f - 0.6f * std::max (0.0f, vmx - 0.5f) * 2.0f, hi = 1.0f - 0.6f * std::max (0.0f, 0.5f - vmx) * 2.0f;
                auto dia = [this] (int degrees) { return diatonic (degrees); };
                nv = 0;
                switch (mode)
                {
                    case 1: semis[nv] = 12; gains[nv++] = hi; semis[nv] = -12; gains[nv++] = lo; break;          // OCTAVES
                    case 2: semis[nv] = 0; gains[nv++] = 0.8f * lo; semis[nv] = 24; gains[nv++] = 0.5f * hi; break; // UNISON + WHISTLE
                    case 3: semis[nv] = dia (-2); gains[nv++] = 1.0f; break;                                     // LOW HARMONY
                    case 4: semis[nv] = dia (2); gains[nv++] = 1.0f; break;                                      // HIGH HARMONY
                    case 5: semis[nv] = dia (-2); gains[nv++] = lo; semis[nv] = dia (2); gains[nv++] = hi; break;
                    case 6: semis[nv] = dia (-2); gains[nv++] = lo; semis[nv] = dia (-4); gains[nv++] = lo; semis[nv] = dia (2); gains[nv++] = hi; break;
                    case 7: semis[nv] = dia (-2); gains[nv++] = lo; semis[nv] = dia (2); gains[nv++] = hi; semis[nv] = 12; gains[nv++] = hi; break;
                    default: semis[nv] = -12; gains[nv++] = lo; semis[nv] = dia (-3); gains[nv++] = lo; semis[nv] = dia (2); gains[nv++] = hi; break;
                }
            }
            int diatonic (int degrees) const
            {
                static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
                const int* sc = keyMinor ? minor : major;
                if (f0 <= 0) return degrees > 0 ? 4 : -3;
                const int note = ((int) std::lround (69.0f + 12.0f * std::log2 (f0 / 440.0f)) - keyRoot + 1200) % 12;
                int deg = 0; for (int k = 0; k < 7; ++k) if (sc[k] <= note) deg = k;
                const int t = deg + degrees, oct = (int) std::floor (t / 7.0f), td = ((t % 7) + 7) % 7;
                return sc[td] + 12 * oct - sc[deg] - (note - sc[deg]);
            }
            void addHist (float f, float a)
            {
                const int pc = ((int) std::lround (69.0f + 12.0f * std::log2 (f / 440.0f)) % 12 + 12) % 12;
                for (auto& h : hist) h *= 0.995f;
                hist[pc] += a;
            }
            /** Tonalita' dai profili di Krumhansl-Kessler (correlazione con l'istogramma delle classi). */
            void updateKey()
            {
                static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
                static const float mnr[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
                float best = -1e9f; int br = 0; bool bm = false;
                float sum = 0; for (float h : hist) sum += h;
                if (sum < 1e-6f) return;
                for (int r = 0; r < 12; ++r)
                    for (int m = 0; m < 2; ++m)
                    {
                        float c = 0;
                        for (int k = 0; k < 12; ++k) c += hist[(k + r) % 12] * (m ? mnr[k] : maj[k]);
                        if (c > best) { best = c; br = r; bm = m == 1; }
                    }
                keyRoot = br; keyMinor = bm;
            }
            Config cfg;
            int nb = 32, histTick = 0, keyRoot = 0;
            bool keyMinor = false;
            float sr = 48000, f0 = 0, phase = 0, phase2 = 0, envA = 0.99f, hist[12] {};
            RoleParam pBlend, pDryRev, pHarmRev, pGender, pMix, pMode, pMic;
            SpectralBank bank;
            Psola ps[3];
            Fdn fdn;
            Svf vm[40], vc[40];
            float ve[40] {};
            Noise noise;
            struct Fader { int current = -1, pending = -1; float g = 1, step = 0.003f;
                void prepare (double s) { step = (float) (1.0 / (0.01 * s)); }
                float tick (int w, bool& ch) { ch = false; if (current < 0) { current = w; ch = true; }
                    if (w != current) pending = w;
                    if (pending >= 0) { g -= step; if (g <= 0) { g = 0; current = pending; pending = -1; ch = true; } }
                    else if (g < 1) g = std::min (1.0f, g + step);
                    return g; } } fader;
            Smooth bS, f0S;
        };
    }

    std::unique_ptr<Effect> makePolyPitch (const ModelDef& d, const std::string& type)
    {
        if (type == "polyoct")   return std::make_unique<PolyOctFx> (d);
        if (type == "polyshift") return std::make_unique<PolyShiftFx> (d);
        if (type == "freeze")    return std::make_unique<FreezeFx> (d);
        if (type == "voicebox")  return std::make_unique<VoiceBoxFx> (d);
        return nullptr;
    }
}
