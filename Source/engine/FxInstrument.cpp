/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Strumenti polifonici e synth della tappa 3:
      * nine      - serie 9 (B9, C9, KEY9, MEL9, SYNTH9, BASS9): dal banco di filtri si
                    estraggono le fondamentali (le bande che sono armoniche di una banda piu'
                    grave e forte vengono soppresse), ognuna pilota una "ricetta" di parziali
                    (rapporti n/2^L con fase esatta), con inviluppo d'organo, percussivo, a
                    swell (nastro) o che segue la chitarra; poi filtro, formanti, chorus/
                    vibrato/tremolo/rotary/phaser, overdrive e nastro;
      * monosynth - synth monofonico con nota dalla banda piu' grave del banco;
      * sitar     - Ravish: lead con formante 'jawari' che scorre e 13 corde simpatiche.
*/

#include "FxSpectral.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        inline float polyBlep (float t, float dt) noexcept
        {
            if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
            return 0.0f;
        }

        //==============================================================================
        /** Voce monofonica (Mono Synth e preset monofonici del SYNTH9). */
        struct MonoVoice
        {
            float sr = 48000, ph[3] {}, freq = 110, env = 0, fenv = 0, lfoPh = 0, arpPh = 0;
            Svf f1, f2;
            Noise noise;
            void prepare (double s) { sr = (float) s; f1.reset(); f2.reset(); }
            inline float saw (int k, float f) noexcept
            {
                const float dt = std::min (0.45f, f / sr);
                ph[k] += dt; ph[k] -= std::floor (ph[k]);
                return 2.0f * ph[k] - 1.0f - polyBlep (ph[k], dt);
            }
            inline float sqr (int k, float f, float pw = 0.5f) noexcept
            {
                const float dt = std::min (0.45f, f / sr);
                ph[k] += dt; ph[k] -= std::floor (ph[k]);
                float v = ph[k] < pw ? 1.0f : -1.0f;
                v += polyBlep (ph[k], dt);
                float t2 = ph[k] - pw; if (t2 < 0) t2 += 1.0f;
                v -= polyBlep (t2, dt);
                return v;
            }
            /** Filtro a 4 poli (due SVF) con taglio fc e risonanza q. */
            inline float lp4 (float x, float fc, float q) noexcept
            {
                const float g = fastTanPi (std::clamp (fc, 30.0f, 0.42f * sr), sr);
                f1.setG (g, 1.0f / 0.54f); f2.setG (g, 1.0f / std::max (0.54f, q));
                return f2.lp (f1.lp (x));
            }
        };

        /** Stima monofonica stabile della nota dal banco (con tenuta durante il rilascio). */
        struct MonoPitch
        {
            float f = 0, held = 110;
            inline float tick (const SpectralBank& bank) noexcept
            {
                const float p = bankPitch (bank, 0.25f);
                if (p > 30 && p < 2500) { f = p; held = p; }
                return held;
            }
        };

        //==============================================================================
        class NineFx final : public Effect
        {
        public:
            enum Model { B9, C9, Key9, Mel9, Synth9, Bass9, String9 };
            static constexpr int maxP = 16;
            struct Partial { int num = 1, lev = 0; float gain = 0, dec = 0; };
            struct Recipe
            {
                Partial p[maxP]; int np = 0;
                int env = 0;                 // 0 organo, 1 percussivo, 2 swell, 3 segue l'ingresso
                float attack = 0.004f, decay = 2.0f, release = 0.05f, level = 1.0f;
                int mod = 0;                 // 0 -, 1 chorus, 2 vibrato, 3 tremolo, 4 rotary, 5 phaser, 6 ensemble
                float modRate = 0, modDepth = 0.5f;
                float drive = 0, lpf = 20000, lpq = 0.7f, fenv = 0, fdecay = 0.4f;
                float formant[3] { 0, 0, 0 }; float formQ = 5;
                float noise = 0, click = 0, ring = 0;
                bool tape = false, mono = false;
                bool freeze = false, freezeManual = false;   // tappa 3B (STRING9): congelamento automatico o manuale
                float subBelow = 0;                          // ottava sotto solo per le bande sotto questa frequenza
                int poly = 8;
                float monoOct = 1.0f; int monoWave = 0; float glide = 0.003f;
                void add (int num, int lev, float g, float d = 0) { if (np < maxP && g > 0) p[np++] = { num, lev, g, d }; }
                /** Drawbar Hammond: livelli 0..8 (-3 dB per scatto). */
                void drawbars (const int* d, float scale = 1.0f)
                {
                    static const int nm[9] = { 1, 3, 1, 2, 3, 4, 5, 6, 8 }, lv[9] = { 1, 1, 0, 0, 0, 0, 0, 0, 0 };
                    for (int k = 0; k < 9; ++k) if (d[k] > 0) add (nm[k], lv[k], scale * std::pow (10.0f, -3.0f * (8 - d[k]) / 20.0f));
                }
                void saw (int lev, int n, float g, float bright = 1.0f)
                {
                    for (int k = 1; k <= n && np < maxP; ++k) add (k, lev, g * std::pow (1.0f / (float) k, 1.0f / std::max (0.3f, bright)), 0.3f * (k - 1));
                }
                void square (int lev, int n, float g)
                {
                    for (int k = 1; k <= n && np < maxP; k += 2) add (k, lev, g / (float) k, 0.2f * (k - 1));
                }
            };

            explicit NineFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "b9");
                model = m == "c9" ? C9 : m == "key9" ? Key9 : m == "mel9" ? Mel9 : m == "synth9" ? Synth9 : m == "bass9" ? Bass9 : m == "string9" ? String9 : B9;
                pDry = role (*this, "direct", 0.6f); pLevel = role (*this, "level", 0.6f); pMode = role (*this, "mode", 0.0f);
                pC1 = Roles (d).index ("mod") >= 0 ? role (*this, "mod", 0.4f) : Roles (d).index ("attack") >= 0 ? role (*this, "attack", 0.5f) : role (*this, "ctrl1", 0.5f);
                pC2 = Roles (d).index ("click") >= 0 ? role (*this, "click", 0.3f) : Roles (d).index ("sustain") >= 0 ? role (*this, "sustain", 0.5f) : role (*this, "ctrl2", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 50.0f, 5000.0f, 4, 0.14f, 2);
                rot.prepare (s);
                for (auto& dl : mdl) dl.allocate ((int) (0.04 * s));
                tapeDl.allocate ((int) (0.05 * s));
                for (int k = 0; k < 3; ++k) form[k].reset();
                mono.prepare (s);
                lvlS.set (s, 20); dryS.set (s, 20); c1S.set (s, 30); c2S.set (s, 30);
                env.set (s, 1, 40);
                reset();
            }
            void reset() override
            {
                bank.reset(); rot.reset();
                for (auto& dl : mdl) dl.clear();
                tapeDl.clear();
                for (auto& b : st) b = {};
                for (auto& a : ap) a.reset();
                filt.reset(); filt2.reset(); fenvG = 0; clickEnv = 0; monoEnv = 0; lastPreset = -1; presetG = 1.0f;
                capN = -1; quietN = 0; lastQuiet = 0; anyHeld = false;
                for (int k = 0; k < 3; ++k) form[k].reset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int wanted = pMode.step (*this);
                // cambio di preset: l'uscita scende a zero in 8 ms, poi si cambia ricetta e si risale
                if (lastPreset < 0) lastPreset = wanted;
                if (wanted != lastPreset && presetG <= 0.0f)
                {
                    lastPreset = wanted;
                    for (auto& b : st) { b.env = 0.0f; b.held = false; }
                    for (auto& dl : mdl) dl.clear();
                    tapeDl.clear(); filt.reset(); filt2.reset(); rot.reset(); rot.jump();
                }
                const int preset = lastPreset;
                const float presetT = wanted == lastPreset ? 1.0f : 0.0f;
                const float presetStep = 1.0f / (0.008f * sr);
                const float c1 = c1S.next (pC1.get (*this)), c2 = c2S.next (pC2.get (*this));
                Recipe R;
                makeRecipe (R, preset, c1, c2);
                // coefficienti per blocco
                float pc[maxP];
                for (int k = 0; k < R.np; ++k)
                    pc[k] = (float) std::exp (-1.0 / (std::max (0.01f, R.decay / (1.0f + R.p[k].dec)) * sr));
                const float atkC = 1.0f / (std::max (0.001f, R.attack) * sr);
                const float relC = (float) std::exp (-1.0 / (std::max (0.005f, R.release) * sr));
                const float fdC = (float) std::exp (-1.0 / (std::max (0.01f, R.fdecay) * sr));
                const float lvl = 2.0f * taperA (pLevel.get (*this)) * R.level;
                const float dry = 2.0f * taperA (pDry.get (*this));
                for (auto& f : form) f.set (sr, 1000, R.formQ);
                for (int k = 0; k < 3; ++k) if (R.formant[k] > 0) form[k].set (sr, R.formant[k], R.formQ);
                lfo.setHz (sr, std::max (0.01f, R.modRate));
                const int nb = bank.numBands(), po = bank.perOctave();
                const int h3 = (int) std::lround (po * std::log2 (3.0f));
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    bank.push (x);
                    ++renorm;
                    float y = 0;
                    bool anyOnset = false;
                    if (R.mono) y = monoPath (R, x, c1, c2, anyOnset);
                    else
                    {
                        // 1) fondamentali: bande che non sono armoniche di una banda piu' grave e forte
                        float own[SpectralBank::maxBands];
                        for (int b = 0; b < nb; ++b) own[b] = bank.owned (b);
                        int nOn = 0;
                        for (int b = 0; b < nb; ++b)
                        {
                            auto& S = st[b];
                            float lower = 0;
                            for (int k : { po, h3, 2 * po })
                                for (int j = b - k - 1; j <= b - k + 1; ++j) if (j >= 0) lower = std::max (lower, own[j]);
                            const bool fund = own[b] > 0.003f && own[b] > 0.15f * bank.peak() && lower < 0.5f * own[b]
                                              && (b == 0 || own[b] >= own[b - 1]) && (b + 1 >= nb || own[b] >= own[b + 1]);
                            S.fs += ((fund ? 1.0f : 0.0f) - S.fs) * 0.004f;
                            if (S.fs > 0.5f) ++nOn;
                            // fasori: agganciati alla banda quando c'e' segnale, liberi nel rilascio
                            if (bank.active (b) && (S.fs > 0.1f || S.env > 0.0f) && (! R.freeze || S.lockN > 0))
                            {
                                for (int L = 0; L < 3; ++L)
                                {
                                    const Cpx nw = bank.root (b, L);
                                    S.rot[L] = cmulc (nw, S.o[L]);          // rotazione per campione (per il rilascio libero)
                                    S.o[L] = nw;
                                }
                            }
                            else if (S.env * S.amp > 2.0e-4f)
                            {
                                // rilascio: fasori liberi alla velocita' dell'ultima rotazione (rinormalizzati ogni 16 campioni)
                                const bool rn = (renorm & 15) == 0;
                                for (int L = 0; L < 3; ++L) { const Cpx o = cmul (S.o[L], S.rot[L]); S.o[L] = rn ? cnorm (o) : o; }
                            }
                            // attacco della banda
                            const float a = own[b];
                            const bool onset = S.fs > 0.5f && a > 1.5f * S.slow + 0.002f && S.refr <= 0;
                            S.slow += (a - S.slow) * 0.0015f;
                            if (S.refr > 0) --S.refr;
                            const bool gate = R.freeze ? S.held : (S.fs > 0.5f && a > 0.0015f);
                            if (R.freeze && S.lockN > 0) --S.lockN;
                            if (onset)
                            {
                                S.refr = (int) (0.06f * sr);
                                S.vel = std::clamp (a * 5.0f, 0.15f, 1.2f);
                                S.age = 0;
                                if (R.env == 1) { for (int k = 0; k < R.np; ++k) S.pe[k] = 1.0f; S.env = 1.0f; }
                                anyOnset = true;
                            }
                            switch (R.env)
                            {
                                case 0:  S.env = gate ? std::min (1.0f, S.env + atkC) : S.env * relC; S.amp = 0.11f; break;
                                case 1:  if (! gate) S.env *= relC; S.amp = 0.35f * S.vel; break;
                                case 2:
                                {
                                    const bool tapeEnd = R.tape && S.age > 8.0f * sr;     // nastro di 8 s esaurito
                                    S.env = gate && ! tapeEnd ? std::min (1.0f, S.env + atkC) : S.env * relC;
                                    S.amp = 0.25f * std::sqrt (std::max (S.vel, 0.2f));
                                    break;
                                }
                                default:
                                {
                                    const float target = gate ? std::pow (std::min (1.0f, a * 6.0f), 0.6f) : 0.0f;
                                    S.env += (target - S.env) * (target > S.env ? atkC * 4.0f : 1.0f - relC);
                                    S.amp = 0.45f;
                                    break;
                                }
                            }
                            S.age += 1.0f;
                        }
                        // STRING9: congelamento (auto: ogni nota/accordo sostituisce il precedente; manuale: nuovo
                        // accordo solo dopo almeno 1 s di silenzio, il congelato resta anche durante le pause)
                        if (R.freeze)
                        {
                            if (env.env < 0.002f) ++quietN; else { if (quietN > 0) lastQuiet = quietN; quietN = 0; }
                            if (anyOnset && capN < 0 && (! R.freezeManual || ! anyHeld || lastQuiet > (int) sr))
                            {
                                capN = (int) (0.08f * sr);
                                lastQuiet = 0;
                            }
                            if (capN >= 0 && --capN < 0)
                            {
                                anyHeld = false;
                                for (int b = 0; b < nb; ++b)
                                {
                                    auto& S = st[b];
                                    S.held = S.fs > 0.5f;
                                    S.lockN = S.held ? (int) (0.15f * sr) : 0;
                                    anyHeld = anyHeld || S.held;
                                }
                            }
                        }
                        // 2) polifonia massima: si tengono le fondamentali piu' forti
                        float thrPoly = 0;
                        if (nOn > R.poly)
                        {
                            float tmp[SpectralBank::maxBands]; int m = 0;
                            for (int b = 0; b < nb; ++b) if (st[b].fs > 0.5f) tmp[m++] = own[b];
                            std::nth_element (tmp, tmp + (m - R.poly), tmp + m);
                            thrPoly = tmp[m - R.poly];
                        }
                        // 3) sintesi delle parziali (al massimo 12 bande sonanti, le piu' forti)
                        float lv[SpectralBank::maxBands]; int nl = 0;
                        for (int b = 0; b < nb; ++b) { lv[b] = st[b].env * st[b].amp; if (lv[b] > 2.0e-4f) ++nl; }
                        float thrSyn = 2.0e-4f;
                        const int cap = std::min (12, R.poly + 4);
                        if (nl > cap)
                        {
                            float tmp[SpectralBank::maxBands]; int m = 0;
                            for (int b = 0; b < nb; ++b) if (lv[b] > 2.0e-4f) tmp[m++] = lv[b];
                            std::nth_element (tmp, tmp + (m - cap), tmp + m);
                            thrSyn = tmp[m - cap];
                        }
                        for (int b = 0; b < nb; ++b)
                        {
                            auto& S = st[b];
                            if (lv[b] < thrSyn) { if (R.env == 1) for (int k = 0; k < R.np; ++k) S.pe[k] *= pc[k]; continue; }
                            if (thrPoly > 0 && own[b] < thrPoly && S.fs > 0.5f) continue;
                            float pr[3][maxP + 1], pim[3][maxP + 1]; int have[3] = { 0, 0, 0 };
                            float acc = 0;
                            const float fc = bank.fc (b);
                            for (int k = 0; k < R.np; ++k)
                            {
                                const auto& P = R.p[k];
                                const int L = P.lev, m = std::clamp (P.num, 1, maxP);
                                const float fade = bank.nyqFade (fc * (float) m / (float) (1 << L));
                                if (fade <= 0) continue;
                                if (have[L] == 0) { pr[L][1] = S.o[L].r; pim[L][1] = S.o[L].i; have[L] = 1; }
                                while (have[L] < m)
                                {
                                    const int h = have[L];
                                    pr[L][h + 1] = pr[L][h] * S.o[L].r - pim[L][h] * S.o[L].i;
                                    pim[L][h + 1] = pr[L][h] * S.o[L].i + pim[L][h] * S.o[L].r;
                                    ++have[L];
                                }
                                float g = P.gain * fade;
                                if (R.subBelow > 0.0f && L > 0 && fc > R.subBelow) g = 0.0f;
                                if (R.env == 1) { g *= S.pe[k]; S.pe[k] *= pc[k]; }
                                acc += g * pr[L][m];
                            }
                            y += S.env * S.amp * acc;
                        }
                    }
                    // click di tasto / percussione sugli attacchi
                    if (anyOnset) { clickEnv = 1.0f; fenvG = 1.0f; }
                    if (R.click > 0 && clickEnv > 1.0e-4f) { y += R.click * 0.08f * clickEnv * noise.uni(); clickEnv *= 0.995f; }
                    fenvG *= fdC;
                    if (R.noise > 0) y += R.noise * 0.02f * noise.uni() * std::min (1.0f, env.tick (x) * 30.0f);
                    else env.tick (x);
                    y = postChain (R, y);
                    presetG = presetT > presetG ? std::min (presetT, presetG + presetStep) : std::max (presetT, presetG - presetStep);
                    const float lg = lvlS.next (lvl) * presetG, dg = dryS.next (dry);
                    const float out = dg * x + lg * y;
                    if (numCh > 1 && R.mod == 4) { ch[0][i] = dg * x + lg * lastL; ch[1][i] = dg * x + lg * lastR; }
                    else for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            struct BandState
            {
                Cpx o[3], rot[3];
                float env = 0, amp = 0, vel = 0.5f, slow = 0, fs = 0, age = 0, pe[maxP] {};
                int refr = 0;
                bool held = false;                          // STRING9: banda tenuta dal congelamento
                int lockN = 0;                              // STRING9: campioni di aggancio dopo la cattura
            };

            /** Percorso monofonico (portamento): oscillatori sulla nota del banco. */
            float monoPath (const Recipe& R, float x, float c1, float c2, bool& onset)
            {
                const float f = mpitch.tick (bank) * R.monoOct;
                glideF += (f - glideF) * (1.0f - std::exp (-1.0f / (std::max (0.001f, R.glide) * sr)));
                const float e = env.tick (x);
                const bool gate = e > 0.003f;
                if (gate && ! monoGate) onset = true;
                monoGate = gate;
                monoEnv = gate ? std::min (1.0f, monoEnv + 1.0f / (0.004f * sr)) : monoEnv * 0.9995f;
                float v;
                switch (R.monoWave)
                {
                    case 1:  v = 0.5f * mono.sqr (0, glideF) + 0.5f * mono.saw (1, glideF * 0.5f); break;
                    case 2:  v = 0.6f * mono.saw (0, glideF) + 0.4f * mono.sqr (1, glideF * 0.5f, 0.5f); break;
                    default: v = 0.5f * (mono.saw (0, glideF) + mono.saw (1, glideF * 1.006f)); break;
                }
                (void) c1; (void) c2;
                return 0.3f * v * monoEnv;
            }

            /** Filtro, formanti, overdrive, modulazioni e nastro. */
            float postChain (const Recipe& R, float y)
            {
                if (R.lpf < 19000.0f || R.fenv > 0)
                {
                    const float fc = std::min (0.42f * sr, R.lpf * std::pow (2.0f, R.fenv * fenvG));
                    const float g = fastTanPi (fc, sr);
                    filt.setG (g, 1.0f / 0.54f); filt2.setG (g, 1.0f / std::max (0.54f, R.lpq));
                    y = filt2.lp (filt.lp (y));
                }
                if (R.formant[0] > 0)
                {
                    float f = 0.25f * y;
                    for (int k = 0; k < 3; ++k) if (R.formant[k] > 0) f += (k == 0 ? 1.2f : 0.8f) * form[k].bp (y);
                    y = f * 1.4f;
                }
                if (R.drive > 0) { const float g = 1.0f + 12.0f * R.drive; y = std::tanh (y * g) / std::sqrt (g); }
                if (R.ring > 0) { ringPh += 30.0f / sr; ringPh -= std::floor (ringPh); y *= 1.0f - R.ring + R.ring * std::sin (6.2831853f * ringPh); }
                const bool modOn = R.modRate > 0.011f;
                switch (modOn ? R.mod : 0)
                {
                    case 1: case 6:          // chorus (scanner) / ensemble a 3 voci
                    {
                        mdl[0].push (y);
                        const float l = lfo.tri(); lfo.step();
                        float w = mdl[0].read ((0.002f + 0.0015f * R.modDepth * (1.0f + l)) * sr);
                        if (R.mod == 6) w = (w + mdl[0].read ((0.004f + 0.002f * R.modDepth * (1.0f + lfo.tri (0.33))) * sr)
                                               + mdl[0].read ((0.006f + 0.002f * R.modDepth * (1.0f + lfo.tri (0.66))) * sr)) / 3.0f;
                        y = 0.7f * (y + w);
                        break;
                    }
                    case 2:                  // vibrato
                    {
                        mdl[0].push (y);
                        const float l = lfo.sine(); lfo.step();
                        y = mdl[0].read ((0.003f + 0.0025f * R.modDepth * (1.0f + l)) * sr);
                        break;
                    }
                    case 3:                  // tremolo
                    {
                        const float l = lfo.sine(); lfo.step();
                        y *= 1.0f - R.modDepth * (0.5f + 0.5f * l);
                        break;
                    }
                    case 4:                  // Leslie
                    {
                        rot.set (R.modRate, R.modRate * 0.9f, 0.8, 3.0, 0.55f);
                        rot.tick (y, lastL, lastR);
                        y = 0.5f * (lastL + lastR);
                        break;
                    }
                    case 5:                  // phaser
                    {
                        const float l = lfo.tri(); lfo.step();
                        const float a = apCoef (200.0f * std::pow (15.0f, 0.5f + 0.5f * l), sr);
                        float v = y; for (auto& s : ap) v = s.tick (v, a);
                        y = 0.6f * (y + v);
                        break;
                    }
                    default: break;
                }
                if (R.mod == 4 && ! modOn) { lastL = lastR = y; }
                if (R.tape)
                {
                    wow += 0.6f / sr; wow -= std::floor (wow); flut += 6.5f / sr; flut -= std::floor (flut);
                    tapeDl.push (y);
                    const float d = (0.006f + 0.0012f * std::sin (6.2831853f * wow) + 0.00015f * std::sin (6.2831853f * flut)) * sr;
                    y = tapeLp.lp (tapeDl.read (d));
                }
                return y;
            }

            void makeRecipe (Recipe& R, int pre, float c1, float c2)
            {
                tapeLp.setFast (sr, 9000.0f);
                const float modRate = c1 < 0.02f ? 0.0f : logMap (c1, 0.5f, 8.0f);   // MOD al minimo = spento
                switch (model)
                {
                    case B9:
                    {
                        static const int db[9][9] = { { 8,8,8,8,8,8,8,8,8 }, { 8,8,8,0,0,0,0,0,0 }, { 8,8,8,8,0,0,0,0,0 }, { 8,8,8,0,0,0,0,0,0 },
                                                      { 8,8,0,0,0,0,0,0,0 }, { 8,0,8,8,0,8,0,0,8 }, { 8,0,8,6,4,5,0,3,4 }, { 0,0,8,6,6,5,0,0,0 },
                                                      { 0,0,8,7,0,0,0,0,0 } };
                        R.drawbars (db[pre], 0.4f);
                        R.env = 0; R.mod = pre <= 5 ? 1 : pre == 7 ? 2 : 3; R.modRate = modRate; R.click = c2;
                        if (pre == 3) R.drive = 0.35f;
                        if (pre == 4) { R.add (1, 2, 0.25f); R.lpf = 2500; }
                        if (pre == 5) { R.add (3, 0, 0.3f * c2); R.add (5, 0, 0.2f * c2); R.click = 0.3f * c2; }
                        if (pre == 6) { R.modDepth = c2; R.click = 0; R.attack = 0.04f; R.release = 0.3f; }
                        if (pre == 7) { R.p[0].gain *= 1.0f; R.square (0, 5, 0.25f); R.modDepth = c2; R.click = 0; }
                        if (pre == 8) { R.add (4, 0, 0.5f * c2, 4.0f); R.add (6, 0, 0.3f * c2, 6.0f); R.add (8, 0, 0.2f * c2, 8.0f); R.env = 1; R.decay = 6.0f; R.click = 0; R.modDepth = 0.4f; }
                        break;
                    }
                    case C9:
                    {
                        R.env = 0; R.modRate = modRate; R.mod = 1; R.click = 0;
                        switch (pre)
                        {
                            case 0: { static const int d[9] = { 8,8,8,0,0,0,0,0,0 }; R.drawbars (d, 0.4f); R.click = c2; break; }
                            case 1: { static const int d[9] = { 0,0,8,8,0,0,0,0,0 }; R.drawbars (d, 0.4f); R.add (3, 1, 0.4f * c2); R.add (1, 1, 0.4f * c2); break; }
                            case 2: R.square (0, 9, 0.35f + 0.2f * c2); R.add (2, 0, 0.15f * c2); R.mod = 2; R.modDepth = 0.6f; break;   // combo a transistor
                            case 3: R.saw (0, 6, 0.3f, 0.7f); R.add (2, 0, 0.15f); R.env = 2; R.attack = 0.05f + 1.5f * c1; R.release = 0.2f + 3.0f * c2;
                                    R.mod = 6; R.modRate = 0.7f; break;
                            case 4: { static const int d[9] = { 8,8,8,0,0,0,0,0,0 }, d2[9] = { 8,8,8,8,8,8,8,8,8 }; R.drawbars (c2 > 0.5f ? d2 : d, 0.4f); R.drive = 0.55f; break; }
                            case 5: R.add (1, 0, 0.5f); R.add (2, 0, 0.12f); R.add (3, 0, 0.04f); R.noise = 0.6f; R.env = 2; R.attack = 0.08f; R.release = 0.4f;
                                    R.tape = true; R.mod = 2; R.modDepth = c2; break;
                            case 6: { static const int d[9] = { 8,0,8,8,0,0,0,0,0 }; R.drawbars (d, 0.4f); R.add (3, 0, 0.3f * c2); R.add (5, 0, 0.2f * c2); R.drive = 0.15f; break; }
                            case 7: { static const int d[9] = { 0,0,8,8,8,0,0,0,0 }; R.drawbars (d, 0.4f); R.click = c2; break; }
                            default:  // Clavioline: impulso stretto (tutte le armoniche) in un passa-banda nasale
                                for (int k = 1; k <= 12; ++k) R.add (k, 0, 0.3f * (0.6f + 0.4f * c2) * std::abs (std::sin (pi * k * 0.12f)) / (pi * k * 0.12f));
                                R.formant[0] = 1100; R.formant[1] = 2600; R.formQ = 3; R.mod = 2; R.modDepth = 0.5f; R.modRate = std::max (modRate, 5.5f); break;
                        }
                        break;
                    }
                    case Key9:
                    {
                        R.env = 1; R.release = 0.12f;
                        switch (pre)
                        {
                            case 0: R.add (1, 0, 0.8f, 0); R.add (2, 0, 0.3f, 1); R.add (3, 0, 0.08f, 3); R.add (1, 1, 0.4f * c1, 0); R.add (7, 0, 0.35f * c2, 30);
                                    R.decay = 3.0f; break;                                                  // Rhodes: CTRL1 bassi, CTRL2 tine
                            case 1: R.add (1, 0, 0.7f); R.add (2, 0, 0.4f, 1); R.add (3, 0, 0.35f, 2); R.add (4, 0, 0.15f, 3); R.add (5, 0, 0.12f, 4);
                                    R.drive = 0.3f; R.decay = 2.0f; R.mod = 3; R.modDepth = c1; R.modRate = logMap (c2, 2.0f, 9.0f); break;
                            case 2: R.add (1, 0, 0.8f); R.add (2, 0, 0.25f, 1); R.add (1, 1, 0.5f * c1); R.add (7, 0, 0.3f * c1, 30); R.decay = 3.0f;
                                    R.mod = 5; R.modRate = logMap (c2, 0.1f, 4.0f); break;
                            case 3: R.add (1, 0, 0.8f, 0); R.add (4, 0, 0.35f, 8); R.add (10, 0, 0.12f, 20); R.decay = 1.0f;          // marimba
                                    R.mod = 1; R.modDepth = c1; R.modRate = logMap (c2, 0.2f, 4.0f); break;
                            case 4: for (int k = 1; k <= 8; ++k) R.add (k, 0, 0.7f * std::pow ((float) k, -1.3f), 0.8f * (k - 1));
                                    R.decay = 4.0f; R.mod = 3; R.modDepth = c1 * 0.8f; R.modRate = logMap (c2, 2.0f, 9.0f); break;
                            case 5: for (int k = 1; k <= 6; ++k) R.add (k, 0, 0.6f * std::pow ((float) k, -1.2f), 0.8f * (k - 1));
                                    R.add (2, 0, 0.3f, 1); R.decay = 3.0f; R.mod = 6; R.modDepth = c1; R.modRate = logMap (c2, 0.2f, 3.0f); break;
                            case 6: R.add (1, 0, 0.8f, 0); R.add (4, 0, 0.3f * (0.3f + c1), 6); R.add (10, 0, 0.1f * (0.3f + c1), 15);            // vibrafono
                                    R.decay = 5.0f; R.mod = 3; R.modDepth = 0.45f; R.modRate = logMap (c2, 1.0f, 9.0f); break;
                            case 7: { static const int d[9] = { 8,8,8,0,0,0,0,0,0 }; R.drawbars (d, 0.4f); R.add (4, 0, 0.3f * c1); R.add (8, 0, 0.2f * c1);
                                    R.env = 0; R.mod = 4; R.modRate = c2 < 0.5f ? 0.8f : 6.7f; break; }
                            default: R.add (1, 0, 0.7f, 0); R.add (2, 0, 0.45f, 1); R.add (3, 0, 0.25f, 2); R.add (4, 0, 0.12f, 3);    // steel drum
                                    R.decay = 1.3f; R.mod = 1; R.modDepth = c1; R.modRate = logMap (c2, 0.2f, 4.0f); break;
                        }
                        break;
                    }
                    case Mel9:
                    {
                        R.env = 2; R.tape = true; R.poly = 5;
                        R.attack = logMap (c1, 0.02f, 1.2f); R.release = logMap (c2, 0.08f, 4.0f);
                        switch (pre)
                        {
                            case 0: R.saw (0, 10, 0.35f, 0.9f); R.formant[0] = 500; R.formant[1] = 1500; R.mod = 6; R.modRate = 0.5f; R.modDepth = 0.3f; break;
                            case 1: R.saw (1, 12, 0.4f, 1.0f); R.formant[0] = 300; R.formant[1] = 900; R.formQ = 4; break;
                            case 2: R.saw (0, 12, 0.35f, 0.9f); R.formant[0] = 1000; R.formant[1] = 2500; R.mod = 6; R.modRate = 0.6f; R.modDepth = 0.5f; break;
                            case 3: R.add (1, 0, 0.6f); R.add (2, 0, 0.12f); R.add (3, 0, 0.04f); R.noise = 0.8f; R.mod = 2; R.modRate = 5.0f; R.modDepth = 0.25f; break;
                            case 4: R.add (1, 0, 0.5f); R.add (3, 0, 0.3f); R.add (5, 0, 0.17f); R.add (7, 0, 0.1f); R.add (2, 0, 0.05f); R.add (9, 0, 0.05f); break;
                            case 5: R.saw (0, 10, 0.35f, 1.1f); R.formant[0] = 600; R.formant[1] = 1800; R.drive = 0.15f; break;
                            case 6: R.saw (0, 12, 0.35f, 1.3f); R.fenv = 1.5f; R.lpf = 1200; R.fdecay = 0.25f; break;
                            case 7: R.saw (1, 12, 0.35f, 1.0f); R.formant[0] = 700; R.formant[1] = 1100; R.formant[2] = 2450; R.formQ = 6;
                                    R.mod = 6; R.modRate = 0.4f; R.modDepth = 0.4f; break;
                            default: R.saw (0, 12, 0.3f, 1.0f); R.formant[0] = 850; R.formant[1] = 1200; R.formant[2] = 2800; R.formQ = 6;
                                    R.mod = 6; R.modRate = 0.45f; R.modDepth = 0.4f; break;
                        }
                        break;
                    }
                    case Synth9:
                    {
                        R.env = 3; R.attack = 0.003f; R.release = 0.2f;
                        static const int oct[4][2] = { { 1, 1 }, { 1, 0 }, { 2, 0 }, { 4, 0 } };   // -1, 0, +1, +2 ottave (n, L)
                        const int o4 = std::clamp ((int) (c2 * 3.99f), 0, 3);
                        switch (pre)
                        {
                            case 0: R.saw (oct[o4][1], 8 / oct[o4][0] + 4, 0.3f); R.lpf = logMap (c1, 300.0f, 12000.0f); R.lpq = 1.2f; R.mod = 1; R.modRate = 0.6f; break;
                            case 1:
                            {
                                static const int iv[8][2] = { { 1, 0 }, { 5, 2 }, { 3, 1 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 1, 1 }, { 3, 2 } };
                                const int k = std::clamp ((int) (c2 * 7.99f), 0, 7);
                                R.saw (0, 8, 0.25f);
                                R.add (iv[k][0], iv[k][1], 0.25f); R.add (iv[k][0] * 2, iv[k][1], 0.12f);
                                R.lpf = 500; R.fenv = 4.0f * c1; R.fdecay = 0.35f; R.lpq = 1.5f; break;
                            }
                            case 2: for (int k = 1; k <= 8; ++k) R.add (k, 0, 0.3f * std::pow ((float) k, -2.0f + 1.5f * c1)); R.mod = 2; R.modRate = 5.5f; R.modDepth = c2; break;
                            case 3: R.mono = true; R.monoWave = 0; R.glide = 0.005f + 0.4f * c2; R.lpf = 2500; R.lpq = 1.5f; R.level = 0.6f + 0.6f * c1; break;
                            case 4: R.square (oct[o4][1], 9, 0.35f); R.lpf = 400; R.fenv = 5.0f * c1; R.fdecay = 0.3f; R.lpq = 2.5f; break;
                            case 5: R.mono = true; R.monoWave = 2; R.monoOct = (float) oct[o4][0] / (float) (1 << oct[o4][1]); R.lpf = logMap (c1, 400.0f, 10000.0f); R.lpq = 1.8f; break;
                            case 6:
                            {
                                static const float oc3[3] = { 0.5f, 0.25f, 1.0f };
                                R.mono = true; R.monoWave = 1; R.monoOct = oc3[std::clamp ((int) (c2 * 2.99f), 0, 2)]; R.lpf = 250; R.fenv = 4.0f * c1; R.fdecay = 0.25f; R.lpq = 2.0f;
                                break;
                            }
                            case 7: R.saw (0, 10, 0.3f, 0.8f); R.add (2, 0, 0.15f); R.env = 2; R.attack = 0.01f + 0.8f * c2; R.release = 0.5f;
                                    R.lpf = logMap (c1, 800.0f, 12000.0f); R.mod = 6; R.modRate = 0.8f; R.modDepth = 0.7f; break;
                            default: R.saw (0, 9, 0.3f); R.square (0, 9, 0.15f); R.lpf = logMap (c1, 400.0f, 10000.0f); R.mod = 1; R.modRate = 0.3f + 3.0f * c2; R.modDepth = 0.6f; break;
                        }
                        break;
                    }
                    case Bass9:
                    {
                        R.env = 1; R.decay = 1.6f; R.release = 0.08f; R.poly = 4;
                        const int L = c1 < 0.5f ? 1 : 2;     // -1 / -2 ottave dove previsto da CTRL1
                        switch (pre)
                        {
                            case 0: R.add (1, 1, 0.7f * (1.0f - c1)); R.add (1, 2, 0.7f * c1); R.add (2, 1, 0.3f, 1); R.add (3, 1, 0.15f, 2); R.add (4, 1, 0.08f, 3);
                                    R.lpf = logMap (c2, 300.0f, 5000.0f); break;
                            case 1:
                            {
                                // pitch -12..0 semitoni: con il banco si usano i rapporti n/4 piu' vicini (8 posizioni)
                                static const int nq[8] = { 2, 2, 3, 3, 3, 4, 4, 4 };
                                static const int lq[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
                                const int k = std::clamp ((int) (c1 * 7.99f), 0, 7);
                                R.add (nq[k], lq[k], 0.7f); R.add (nq[k] * 2, lq[k], 0.25f, 1);
                                if (c2 < 0.67f) { R.mod = 3; R.modDepth = c2 / 0.67f; R.modRate = 5.0f; } else R.ring = (c2 - 0.67f) / 0.33f;
                                break;
                            }
                            case 2: R.add (1, 1, 0.7f); R.add (2, 1, 0.35f); R.add (3, 1, 0.2f); R.decay = 3.0f; R.formant[0] = 500 + 600 * c1; R.formQ = 3; R.drive = 0.2f * c1;
                                    R.mod = 1; R.modRate = c2 > 0.02f ? 0.8f : 0; R.modDepth = c2; break;
                            case 3: R.saw (L, 10, 0.35f); if (c1 > 0.75f) R.add (3, 2, 0.3f); R.lpf = 250; R.fenv = 4.0f * c2; R.fdecay = 0.3f; R.lpq = 2.0f; R.env = 3; break;
                            case 4: R.add (1, 1, 0.7f); for (int k = 2; k <= 6; ++k) R.add (k, 1, 0.4f * c1 / (float) k, 1.5f * (1.0f - c2) * k); R.decay = 0.8f + 3.0f * c2; break;
                            case 5: R.saw (L, 10, 0.3f, 0.8f); R.env = 2; R.attack = 0.02f + 0.6f * c2; R.release = 0.3f; R.formant[0] = 400; R.formant[1] = 1200; R.formQ = 3; break;
                            case 6: R.add (1, 1, 0.7f); R.add (2, 1, 0.3f); R.add (3, 1, 0.15f); R.lpf = 400; R.fenv = 3.0f * c2; R.fdecay = 0.15f; R.lpq = 3.0f; R.env = 3; break;
                            case 7: R.square (1, 9, 0.35f); R.lpf = 200; R.fenv = 5.0f * c1; R.fdecay = logMap (c2, 0.08f, 0.6f); R.lpq = 6.0f; R.env = 3; break;
                            default: R.square (L, 9, 0.35f); R.lpf = logMap (c2, 150.0f, 4000.0f); R.lpq = 0.9f; R.env = 3; break;
                        }
                        break;
                    }
                    case String9:
                    {
                        // archi: dente di sega come serie di parziali, attacco d'arco (swell), ensemble; CTRL secondo il manuale STRING9
                        R.env = 2; R.attack = 0.12f; R.release = logMap (c2, 0.15f, 6.0f); R.poly = 6; R.level = 1.8f;
                        R.lpf = logMap (c1, 900.0f, 12000.0f); R.lpq = 0.6f;
                        R.mod = 6; R.modRate = 0.55f; R.modDepth = 0.45f;
                        switch (pre)
                        {
                            case 0: R.saw (0, 8, 0.27f, 0.85f); R.add (1, 1, 0.3f); R.subBelow = 250.0f; break;               // SYMPHONIC: ottava sotto sulle corde gravi
                            case 1:                                                                                              // JUNE-O: CTRL 2 = 5 ottave
                            {
                                static const int on[5] = { 1, 1, 1, 2, 4 }, ol[5] = { 2, 1, 0, 0, 0 };
                                const int o = std::clamp ((int) (c2 * 4.99f), 0, 4);
                                for (int k = 1; k <= 8 && k * on[o] <= maxP; ++k) R.add (k * on[o], ol[o], 0.28f / std::pow ((float) k, 1.1f), 0.3f * (k - 1));
                                R.release = 0.6f; R.mod = 1; R.modRate = 0.5f; R.modDepth = 0.7f;
                                break;
                            }
                            case 2: R.saw (0, 8, 0.3f, 0.8f); R.release = 0.5f; R.mod = 2; R.modRate = c2 < 0.02f ? 0.0f : 5.5f; R.modDepth = c2; break;   // PCM: CTRL 2 = vibrato
                            case 3: R.saw (0, 8, 0.3f, 0.7f); R.tape = true; R.noise = 0.3f; R.attack = 0.09f; R.formant[0] = 900; R.formant[1] = 2400; R.formQ = 2.5f; break;  // FLOPPY
                            case 4: R.saw (0, 10, 0.28f, 1.0f); if (c1 > 0.02f) { R.mod = 5; R.modRate = logMap (c1, 0.1f, 4.0f); } R.lpf = 7000; break;  // AARP: CTRL 1 = phaser
                            case 5:                                                                                              // CREWMAN: inviluppo del filtro
                            {
                                R.saw (0, 10, 0.28f, 1.2f); R.square (0, 5, 0.1f); R.lpf = 1400; R.lpq = 1.2f;
                                if (c1 < 0.5f) { R.fenv = -3.0f; R.fdecay = logMap (c1 / 0.5f, 2.0f, 0.08f); }       // meta' sinistra: apertura lenta -> veloce
                                else { R.fenv = 3.5f; R.fdecay = logMap ((c1 - 0.5f) / 0.5f, 2.0f, 0.08f); }         // meta' destra: chiusura lenta -> veloce
                                break;
                            }
                            case 6: R.saw (0, 8, 0.27f, 0.85f); R.add (1, 1, 0.3f); R.subBelow = 250.0f; R.freeze = true; R.freezeManual = c2 >= 0.5f; R.release = 1.2f; break;
                            case 7: for (int k = 1; k <= 8; ++k) R.add (k, 0, 0.28f / std::pow ((float) k, 1.1f), 0.3f * (k - 1));
                                    R.mod = 1; R.modRate = 0.5f; R.modDepth = 0.7f; R.freeze = true; R.freezeManual = c2 >= 0.5f; R.release = 1.2f; break;
                            default:                                                                                             // VOX FREEZE: coro e archi, phaser
                                R.saw (0, 10, 0.24f, 0.9f); R.formant[0] = 700; R.formant[1] = 1100; R.formant[2] = 2450; R.formQ = 5;
                                R.lpf = 9000; R.mod = c1 > 0.02f ? 5 : 6; R.modRate = c1 > 0.02f ? logMap (c1, 0.1f, 4.0f) : 0.5f;
                                R.freeze = true; R.freezeManual = c2 >= 0.5f; R.release = 1.2f; break;
                        }
                        break;
                    }
                }
            }

            Config cfg;
            Model model = B9;
            float sr = 48000, fenvG = 0, clickEnv = 0, monoEnv = 0, glideF = 110, lastL = 0, lastR = 0, wow = 0, flut = 0, ringPh = 0;
            bool monoGate = false;
            int lastPreset = -1;
            float presetG = 1.0f;
            int capN = -1, quietN = 0, lastQuiet = 0;   // STRING9: congelamento
            bool anyHeld = false;
            unsigned renorm = 0;
            RoleParam pDry, pLevel, pMode, pC1, pC2;
            SpectralBank bank;
            BandState st[SpectralBank::maxBands];
            Rotary rot;
            DelayLine mdl[1], tapeDl;
            Allpass1 ap[6];
            Svf filt, filt2, form[3];
            OnePole tapeLp;
            Lfo lfo;
            Noise noise;
            Envelope env;
            MonoVoice mono;
            MonoPitch mpitch;
            Smooth lvlS, dryS, c1S, c2S;
        };

        //==============================================================================
        /** Mono Synth: 11 tipi. */
        class MonoSynthFx final : public Effect
        {
        public:
            explicit MonoSynthFx (const ModelDef& d) : Effect (d)
            {
                pDry = role (*this, "direct", 0.5f); pLevel = role (*this, "level", 0.6f); pSens = role (*this, "sens", 0.5f);
                pCtrl = role (*this, "ctrl", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 50.0f, 2500.0f, 4, 0.14f, 0);
                v.prepare (s);
                fol.set (s, 1, 60);
                lvlS.set (s, 20); dryS.set (s, 20); ctlS.set (s, 30);
                reset();
            }
            void reset() override { bank.reset(); env = 0; fenv = 0; gate = false; freq = 110; v.f1.reset(); v.f2.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int wanted = pMode.step (*this);
                if (curType < 0) curType = wanted;
                if (wanted != curType && typeG <= 0.0f) { curType = wanted; v.f1.reset(); v.f2.reset(); }
                const int type = curType;
                const float typeT = wanted == curType ? 1.0f : 0.0f, typeStep = 1.0f / (0.008f * sr);
                const float sens = logMap (pSens.get (*this), 0.3f, 30.0f);
                const float lvl = 2.0f * taperA (pLevel.get (*this)), dry = 2.0f * taperA (pDry.get (*this));
                // tempi per tipo
                static const float atk[11] = { 0.003f, 0.005f, 0.004f, 0.004f, 0.003f, 0.002f, 0.004f, 0.002f, 0.4f, 0.03f, 0.003f };
                static const float rel[11] = { 0.15f, 0.3f, 0.12f, 0.2f, 0.12f, 0.1f, 0.2f, 0.25f, 1.2f, 0.6f, 0.15f };
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    bank.push (x);
                    const float p = mp.tick (bank);
                    const float ctrl = ctlS.next (pCtrl.get (*this));
                    const float e = fol.tick (x) * sens;
                    const bool on = gate ? e > 0.01f : e > 0.03f;
                    if (on && ! gate) { fenv = 1.0f; accent = std::min (1.5f, e * 10.0f); }
                    gate = on;
                    const float attackS = type == 8 ? 0.02f + 1.5f * ctrl : atk[type];
                    env = gate ? std::min (1.0f, env + 1.0f / (attackS * sr)) : env * (float) std::exp (-1.0 / (rel[type] * sr));
                    fenv *= (float) std::exp (-1.0 / ((type == 5 ? 0.18f : 0.35f) * sr));
                    const float glide = type == 9 ? 0.005f + 0.5f * ctrl : 0.004f;
                    freq += (p - freq) * (1.0f - std::exp (-1.0f / (glide * sr)));
                    float o = 0, fc = 3000, q = 0.8f;
                    switch (type)
                    {
                        case 0:  o = 0.5f * v.saw (0, freq) + 0.4f * v.sqr (1, freq * 2.0f); fc = logMap (ctrl, 300.0f, 9000.0f) * (1.0f + 2.0f * fenv); q = 1.4f; break;   // NU WAVE
                        case 1:  { const float dt = 1.0f + 0.03f * ctrl; o = 0.35f * (v.saw (0, freq) + v.saw (1, freq * dt) + v.saw (2, freq / dt)); fc = 5000; break; }  // UNISON
                        case 2:  o = 0.6f * v.sqr (0, freq) + 0.25f * v.noise.uni() * fenv; fc = 600.0f * std::pow (2.0f, 4.0f * ctrl * fenv); q = 1.2f; break;            // BLAST
                        case 3:  o = 0.45f * (v.saw (0, freq) + v.saw (1, freq * std::pow (2.0f, std::round (ctrl * 12.0f) / 12.0f))); fc = 4000; break;                   // TWIN
                        case 4:  o = 0.55f * v.saw (0, freq * 0.5f) + 0.45f * v.sqr (1, freq * 0.25f); fc = logMap (ctrl, 150.0f, 3000.0f); q = 1.1f; break;               // BASS
                        case 5:  o = 0.6f * v.saw (0, freq * 0.5f); fc = 180.0f * std::pow (2.0f, (1.0f + 5.0f * ctrl) * fenv * accent); q = 7.0f; break;                  // XOX
                        case 6:  { v.lfoPh += logMap (ctrl, 0.5f, 12.0f) / sr; v.lfoPh -= std::floor (v.lfoPh);                                                          // WUB
                                   o = 0.55f * v.saw (0, freq * 0.5f) + 0.3f * v.sqr (1, freq * 0.5f * 1.005f);
                                   fc = 150.0f * std::pow (25.0f, 0.5f + 0.5f * std::sin (6.2831853f * v.lfoPh)); q = 3.0f; break; }
                        case 7:  { v.arpPh += logMap (ctrl, 2.0f, 16.0f) / sr; v.arpPh -= std::floor (v.arpPh);                                                          // TINKER
                                   const float m = v.arpPh < 0.5f ? 1.0f : 2.0f; o = 0.5f * v.sqr (0, freq * m, 0.3f); fc = 6000; break; }
                        case 8:  o = 0.3f * (v.saw (0, freq * 0.5f) + v.saw (1, freq * 0.5f * 1.008f) + v.saw (2, freq * 0.997f)); fc = 1800; q = 0.9f; break;               // LAIR
                        case 9:  { v.lfoPh += 5.5f / sr; v.lfoPh -= std::floor (v.lfoPh);                                                                                   // GHOST
                                   const float f = freq * (1.0f + 0.006f * std::sin (6.2831853f * v.lfoPh));
                                   v.ph[0] += f / sr; v.ph[0] -= std::floor (v.ph[0]); o = 0.7f * std::sin (6.2831853f * v.ph[0]); fc = 8000; break; }
                        default: { const float s = v.saw (0, freq); const float g = 1.0f + 15.0f * ctrl; o = 0.5f * std::tanh (g * (s + 0.3f * v.sqr (1, freq * 2.0f))) ; fc = 7000; break; } // BLISTER
                    }
                    typeG = typeT > typeG ? std::min (typeT, typeG + typeStep) : std::max (typeT, typeG - typeStep);
                    const float y = v.lp4 (o, fc, q) * env * 0.22f * typeG;
                    const float out = dryS.next (dry) * x + lvlS.next (lvl) * y;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            float sr = 48000, env = 0, fenv = 0, freq = 110, accent = 1, typeG = 1;
            int curType = -1;
            bool gate = false;
            RoleParam pDry, pLevel, pSens, pCtrl, pMode;
            SpectralBank bank;
            MonoPitch mp;
            MonoVoice v;
            Envelope fol;
            Smooth lvlS, dryS, ctlS;
        };

        //==============================================================================
        /** Bass Mono Synth (tappa 3B): synth monofonico per basso, 11 tipi. Nota dal banco di filtri
            (30-1500 Hz), oscillatori polyBLEP fino a 6, filtro a 4 poli con inviluppo o LFO. */
        class BassMonoFx final : public Effect
        {
        public:
            explicit BassMonoFx (const ModelDef& d) : Effect (d)
            {
                pDry = role (*this, "direct", 0.5f); pLevel = role (*this, "level", 0.6f); pSens = role (*this, "sens", 0.5f);
                pCtrl = role (*this, "ctrl", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 30.0f, 1500.0f, 4, 0.14f, 0);
                fol.set (s, 1, 60);
                lvlS.set (s, 20); dryS.set (s, 20); ctlS.set (s, 30);
                reset();
            }
            void reset() override { bank.reset(); env = 0; fenv = 0; fstage = 0; gate = false; freq = 55; f1.reset(); f2.reset(); for (auto& p : ph) p = 0; lfo = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int wanted = pMode.step (*this);
                if (curType < 0) curType = wanted;
                if (wanted != curType && typeG <= 0.0f) { curType = wanted; f1.reset(); f2.reset(); }
                const int type = std::clamp (curType, 0, 10);
                const float typeT = wanted == curType ? 1.0f : 0.0f, typeStep = 1.0f / (0.008f * sr);
                const float sensK = pSens.get (*this);
                const float sens = logMap (sensK, 0.3f, 30.0f);
                const float lvl = 2.0f * taperA (pLevel.get (*this)), dry = 2.0f * taperA (pDry.get (*this));
                static const float rel[11] = { 0.25f, 0.3f, 0.12f, 0.25f, 0.2f, 0.1f, 0.25f, 0.3f, 0.25f, 0.3f, 0.6f };
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    bank.push (x);
                    const float p = mp.tick (bank);
                    const float c = ctlS.next (pCtrl.get (*this));
                    const float e = fol.tick (x) * sens;
                    const bool on = gate ? e > 0.01f : e > 0.03f;
                    if (on && ! gate) { fstage = 1; accent = std::min (1.5f, e * 10.0f); }
                    gate = on;
                    env = gate ? std::min (1.0f, env + 1.0f / (0.003f * sr)) : env * (float) std::exp (-1.0 / (rel[type] * sr));
                    // inviluppo del filtro: attacco/decadimento per tipo (CTRL dove indicato dal manuale)
                    float fa = 0.002f, fd = 0.35f;
                    switch (type)
                    {
                        case 0: case 8: fa = logMap (c, 0.003f, 0.25f); fd = logMap (c, 0.08f, 1.5f); break;   // LASER, TWIN
                        case 1: case 3: case 5: case 7: fd = logMap (c, 0.05f, 1.5f); break;                   // X-FADE, COSMIC, GROWL, UNISON
                        case 2: fd = 0.18f; break;                                                              // ACID
                        default: break;
                    }
                    if (fstage == 1) { fenv += 1.0f / (fa * sr); if (fenv >= 1.0f) { fenv = 1.0f; fstage = 2; } }
                    else fenv *= (float) std::exp (-1.0 / (fd * sr));
                    freq += (p - freq) * (1.0f - std::exp (-1.0f / (0.004f * sr)));
                    float o = 0, fc = 1500, q = 0.8f;
                    switch (type)
                    {
                        case 0: o = 0.5f * saw (0, freq) + 0.4f * sqr (1, freq * 0.5f); fc = 120.0f * std::pow (2.0f, 5.5f * fenv); q = 1.6f; break;                          // LASER
                        case 1: o = 0.3f * (saw (0, freq) + saw (1, freq * 1.007f) + sqr (2, freq * 0.5f)) + 0.6f * x;                                                          // X-FADE
                                fc = 150.0f * std::pow (2.0f, 6.0f * fenv * std::min (1.0f, 0.3f + sensK)); q = 1.2f; break;
                        case 2: o = 0.6f * saw (0, freq); fc = 110.0f * std::pow (2.0f, (1.5f + 4.0f * c) * fenv * accent); q = 2.0f + 8.0f * c; break;                         // ACID
                        case 3: { lfo += 5.5f / sr; lfo -= std::floor (lfo); const float f = freq * (1.0f + 0.006f * std::sin (6.2831853f * lfo));                              // COSMIC
                                  o = 0.45f * saw (0, f) + 0.35f * sqr (1, f * 2.0f, 0.3f); fc = 900.0f * std::pow (2.0f, 3.0f * fenv); q = 1.1f; break; }
                        case 4: { ph[0] += freq / sr; ph[0] -= std::floor (ph[0]); ph[1] += 0.5f * freq / sr; ph[1] -= std::floor (ph[1]);                                     // SUB
                                  const float tri = ph[1] < 0.5f ? 4.0f * ph[1] - 1.0f : 3.0f - 4.0f * ph[1];
                                  o = 0.55f * std::sin (6.2831853f * ph[0]) + 0.8f * c * tri; fc = 800.0f; q = 0.7f; break; }
                        case 5: o = 0.45f * sqr (0, freq) + 0.35f * saw (1, freq); fc = 100.0f * std::pow (2.0f, (2.0f + 5.0f * sensK) * fenv); q = 1.5f;                      // GROWL
                                o *= 0.3f + 0.7f * fenv; break;
                        case 6: { lfo += logMap (c, 0.5f, 12.0f) / sr; lfo -= std::floor (lfo);                                                                                 // WUB
                                  o = 0.5f * saw (0, freq) + 0.3f * sqr (1, freq * 1.005f); fc = 120.0f * std::pow (25.0f, 0.5f + 0.5f * std::sin (6.2831853f * lfo)); q = 3.0f; break; }
                        case 7: { static const float dt[5] = { 1.0f, 1.0069f, 0.9931f, 1.0139f, 0.9862f };                                                                     // UNISON
                                  for (int k = 0; k < 5; ++k) { o += 0.2f * saw (k, freq * dt[k]); }
                                  fc = 200.0f * std::pow (2.0f, 5.0f * fenv); q = 1.0f; break; }
                        case 8: o = 0.4f * saw (0, freq) + 0.4f * sqr (1, freq); fc = 250.0f * std::pow (2.0f, (2.0f + 3.0f * sensK) * fenv); q = 3.5f; break;                  // TWIN
                        case 9: o = 0.3f * (saw (0, freq) + saw (1, freq * 1.004f)) + 0.3f * saw (2, freq * 2.0f); fc = logMap (c, 200.0f, 6000.0f) * (1.0f + fenv); q = 1.3f; break;   // SPECTRE
                        default: { lfo += logMap (c, 0.3f, 8.0f) / sr; lfo -= std::floor (lfo); const float w = 1.0f + 0.005f * std::sin (6.2831853f * lfo);                 // OBLIVION
                                   o = 0.3f * (saw (0, freq * w) + saw (1, freq * 1.0046f / w) + saw (2, freq * 0.9954f)); fc = 900.0f; q = 1.2f; break; }
                    }
                    typeG = typeT > typeG ? std::min (typeT, typeG + typeStep) : std::max (typeT, typeG - typeStep);
                    const float g = fastTanPi (std::clamp (fc, 30.0f, 0.42f * sr), sr);
                    f1.setG (g, 1.0f / 0.54f); f2.setG (g, 1.0f / std::max (0.54f, q));
                    const float y = f2.lp (f1.lp (o)) * env * 0.25f * typeG;
                    const float out = dryS.next (dry) * x + lvlS.next (lvl) * y;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = out;
                }
            }
        private:
            inline float saw (int k, float f) noexcept
            {
                const float dt = std::min (0.45f, f / sr);
                ph[k] += dt; ph[k] -= std::floor (ph[k]);
                return 2.0f * ph[k] - 1.0f - polyBlep (ph[k], dt);
            }
            inline float sqr (int k, float f, float pw = 0.5f) noexcept
            {
                const float dt = std::min (0.45f, f / sr);
                ph[k] += dt; ph[k] -= std::floor (ph[k]);
                float v = ph[k] < pw ? 1.0f : -1.0f;
                v += polyBlep (ph[k], dt);
                float t2 = ph[k] - pw; if (t2 < 0) t2 += 1.0f;
                return v - polyBlep (t2, dt);
            }
            float sr = 48000, env = 0, fenv = 0, freq = 55, accent = 1, typeG = 1, ph[6] {}, lfo = 0;
            int curType = -1, fstage = 0;
            bool gate = false;
            RoleParam pDry, pLevel, pSens, pCtrl, pMode;
            SpectralBank bank;
            MonoPitch mp;
            Svf f1, f2;
            Envelope fol;
            Smooth lvlS, dryS, ctlS;
        };

        //==============================================================================
        /** Ravish: sitar con corde simpatiche. */
        class SitarFx final : public Effect
        {
        public:
            static constexpr int maxStr = 13;
            explicit SitarFx (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                ns = std::clamp ((int) cfg.num ("strings", 13), 4, maxStr);
                pDry = role (*this, "direct", 0.6f); pLead = role (*this, "level", 0.6f); pSym = role (*this, "symp", 0.5f);
                pTimbre = role (*this, "timbre", 0.6f); pSTimbre = role (*this, "stimbre", 0.5f); pKey = role (*this, "key", 4.0f / 11);
                pScale = role (*this, "scale", 0.0f); pDecay = role (*this, "decay", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 60.0f, 4000.0f, 4, 0.14f, 0);
                for (auto& r : str) r.allocate ((int) (s / 50.0));
                exc.set (s, 2000, 0.7);
                lvlS.set (s, 20); dryS.set (s, 20); symS.set (s, 20);
                reset();
            }
            void reset() override
            {
                bank.reset();
                for (auto& r : str) r.clear();
                for (auto& z : lpz) z = 0;
                for (auto& b : bs) b = {};
                exc.reset();
                lastKey = -1;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int key = pKey.step (*this), scale = pScale.step (*this);
                if (key * 4 + scale != lastKey) { tune (key, scale); lastKey = key * 4 + scale; }
                // DECAY 0..9: coda delle simpatiche da ~0,5 s a ~12 s
                const float t60 = logMap (pDecay.get (*this), 0.5f, 12.0f);
                const float sTim = pSTimbre.get (*this);
                const float damp = 0.15f + 0.8f * (1.0f - sTim);
                const float lead = 2.0f * taperA (pLead.get (*this)) * 1.25f;
                const float sym = 2.0f * taperA (pSym.get (*this)) * 2.0f;           // fino a +6 dB
                const float dry = 2.0f * taperA (pDry.get (*this)) * 1.25f;
                const float timbre = pTimbre.get (*this);
                float fbs[maxStr];
                for (int k = 0; k < ns; ++k) fbs[k] = std::pow (10.0f, -3.0f / (t60 * freqs[k]));
                const int nb = bank.numBands();
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    bank.push (x);
                    // lead: per ogni banda forte, armoniche con un formante che scorre con il decadimento ('jawari')
                    float y = 0;
                    for (int b = 0; b < nb; ++b)
                    {
                        const auto& B = bank.band (b);
                        auto& S = bs[b];
                        const float a = B.active ? B.w * B.ampC : 0.0f;
                        S.peak = std::max (a, S.peak * 0.99995f);
                        if (a < 0.002f || a < 0.12f * bank.peak()) continue;
                        const float fresh = std::sqrt (a / std::max (1.0e-6f, S.peak));
                        const float centre = 1.5f + timbre * 10.0f * fresh;           // buzz piu' alto all'attacco
                        Cpx pw = B.root[0];
                        float acc = 0, gsum = 0;
                        for (int m = 1; m <= 12; ++m)
                        {
                            const float fade = bank.nyqFade (B.fc * (float) m);
                            if (fade <= 0) break;
                            const float dm = ((float) m - centre) / 2.2f;
                            const float g = (0.6f / (float) m + timbre * 0.9f * std::exp (-dm * dm)) * fade;
                            acc += g * pw.r;
                            gsum += g;
                            pw = cmul (pw, B.root[0]);
                        }
                        y += a * acc / std::max (0.5f, gsum);
                    }
                    // corde simpatiche: anelli Karplus-Strong eccitati dal segnale
                    const float e = exc.bp (x) * 0.5f + 0.15f * x;
                    float s = 0;
                    for (int k = 0; k < ns; ++k)
                    {
                        float v = str[k].read (dl[k]);
                        lpz[k] = v + damp * (lpz[k] - v);
                        v = lpz[k];
                        // ponte a contatto: leggero ronzio proporzionale al timbro
                        v = v + sTim * 0.5f * (softSat (v * 4.0f) * 0.25f - v);          // neutro a bassa ampiezza: il ronzio cresce con l'escursione
                        str[k].push (e * 2.0f * (1.0f - fbs[k]) + fbs[k] * v);      // guadagno di risonanza ~2 sulla corda accordata
                        s += v;
                    }
                    const float out = dryS.next (dry) * x + lvlS.next (lead) * y * 1.2f + symS.next (sym) * s * 0.5f;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            void tune (int key, int scale)
            {
                static const int sc[3][13] = { { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21 },
                                               { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20 },
                                               { 0, 1, 4, 5, 7, 8, 11, 12, 13, 16, 17, 19, 20 } };    // esotica: frigia dominante (Bhairav)
                const float root = 130.81f * std::pow (2.0f, (float) key / 12.0f);   // dal Do3
                for (int k = 0; k < ns; ++k)
                {
                    freqs[k] = root * std::pow (2.0f, (float) sc[std::clamp (scale, 0, 2)][k] / 12.0f);
                    dl[k] = sr / freqs[k] - 0.5f;          // -0.5: ritardo di gruppo del passa-basso d'anello
                }
            }
            struct BS { float peak = 0; };
            Config cfg;
            int ns = 13, lastKey = -1;
            float sr = 48000, freqs[maxStr] {}, dl[maxStr] {}, lpz[maxStr] {};
            RoleParam pDry, pLead, pSym, pTimbre, pSTimbre, pKey, pScale, pDecay;
            SpectralBank bank;
            BS bs[SpectralBank::maxBands];
            Ring str[maxStr];
            Svf exc;
            Smooth lvlS, dryS, symS;
        };
    }

    std::unique_ptr<Effect> makeInstrument (const ModelDef& d, const std::string& type)
    {
        if (type == "nine")      return std::make_unique<NineFx> (d);
        if (type == "monosynth") return std::make_unique<MonoSynthFx> (d);
        if (type == "sitar")     return std::make_unique<SitarFx> (d);
        if (type == "b3bassmono") return std::make_unique<BassMonoFx> (d);      // tappa 3B
        return nullptr;
    }
}
