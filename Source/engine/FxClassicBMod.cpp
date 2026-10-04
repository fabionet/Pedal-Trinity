/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Modulazioni della tappa 3B:
      * b3chorus   - chorus/vibrato a BBD con catena completa (EVH 5150 Chorus con INTENSITY
                     unica e attenuatori di livello, Bass Clone con crossover ed EQ, Stereo Clone
                     Theory a uscite in controfase, Eddy con forma d'onda e inviluppo);
      * b3bbdmulti - due linee BBD multimodo (Echoflanger: filter matrix, flange, chorus,
                     slapback e combinazioni; Stereo Polychorus con filtro che segue il clock);
      * b3mistress - flanger/chorus digitali con Filter Matrix (Stereo Electric Mistress, Neo Mistress);
      * b3hoax     - Flanger Hoax: due phaser (fisso e spazzato) seguiti da due linee di
                     ritardo modulate con fasi selezionabili, feedback dal wet o dal ramo spazzato;
      * b3wiggler  - tremolo/vibrato a valvole con 4 voicing di vibrato;
      * b3pulsar   - tremolo/panner a forma variabile (Nano Pulsar) e Super Pulsar con fase del
                     canale destro, ritmi e tre gamme di velocita';
      * b3modrex   - quattro modulatori sincronizzati sul tempo (FILTER -> MOD -> TREM -> PAN);
      * b3polyphase- phaser ottico stereo con LFO, inviluppo, START/STOP;
      * b3badstone1- Bad Stone V1 a 6 JFET con feedback (BLEND) e COLOR (solo wet).
*/

#include "FxClassicB.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        //==============================================================================
        /** Chorus/vibrato a BBD. */
        class B3Chorus final : public Effect
        {
        public:
            enum Model { Dc30, BassClone, StereoTheory, Eddy };
            explicit B3Chorus (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "dc30");
                model = m == "bassclone" ? BassClone : m == "sct" ? StereoTheory : m == "eddy" ? Eddy : Dc30;
                stages = (int) cfg.num ("stages", 1024);
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? (float) dl[0] : 3.0f; dHi = dl.size() > 1 ? (float) dl[1] : 12.0f;
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 8.0f;
                aa = cfg.num ("aa", 8000); noiseV = (float) cfg.num ("noise", 0.0003); sat = (float) cfg.num ("sat", 1.2);
                comp = cfg.num ("comp", 0) > 0.5; pre = cfg.num ("pre", 0) > 0.5;
                xover = cfg.num ("xover", 220); bassHz = cfg.num ("bass", 120); trebHz = cfg.num ("treble", 2500);
                pLevel = role (*this, "level", 0.8f); pTone = role (*this, "tone", 0.5f); pDepth = role (*this, "depth", 0.5f);
                pRate = role (*this, "rate", 0.4f); pInLvl = role (*this, "inlvl", 0.0f); pOutLvl = role (*this, "outlvl", 0.0f);
                pBass = role (*this, "bass", 0.5f); pTreble = role (*this, "treble", 0.5f); pXover = role (*this, "xover", 0.0f);
                pMode = role (*this, "mode", 0.0f); pEnvMode = role (*this, "envmode", 0.0f); pShape = role (*this, "shape", 0.5f);
                pEnv = role (*this, "env", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& b : bbd) b.prepare (s, stages, aa, noiseV, comp, pre, sat);
                if (model == Dc30) { shelf[0].prepare (s, SmoothEq::HighShelf, 6000, 0.7); shelf[1].prepare (s, SmoothEq::HighShelf, 6000, 0.7); }
                else { shelf[0].prepare (s, SmoothEq::LowShelf, bassHz, 0.7); shelf[1].prepare (s, SmoothEq::HighShelf, trebHz, 0.7); shelf[2].prepare (s, SmoothEq::HighShelf, trebHz, 0.7); }
                tLoS.set (s, 20); tHiS.set (s, 20);
                env.set (s, 3, 120);
                for (auto* sm : { &lvlS, &inS, &outS, &wetS, &dryS, &toneS, &bassS, &trebS, &xoS, &envS }) sm->set (s, 20);
                posS[0].set (s, 4); posS[1].set (s, 4); rateS.set (s, 40); depS.set (s, 30);
                reset();
            }
            void reset() override
            {
                for (auto& b : bbd) b.reset();
                for (auto& f : shelf) f.reset();
                hp.reset(); hp2.reset(); lfo.ph = 0.25;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // comandi per blocco
                float lvl = 2.0f * taperA (pLevel.get (*this));
                float inG = 1.0f, outG = 1.0f, dryT = 0.5f, wetT = 0.5f;
                float rateHz = logMap (pRate.get (*this), rLo, rHi), swingMs = 0.0f, centreMs = std::sqrt (dLo * dHi);
                const int mode = pMode.step (*this);
                if (model == Dc30)
                {
                    const float k = pDepth.get (*this);                                  // INTENSITY: velocita' e profondita' insieme
                    rateHz = logMap (k, rLo, rHi);
                    swingMs = 2.0f - 1.3f * k;                                           // +-2 ms lento .. +-0,7 ms veloce
                    centreMs = 0.5f * (dLo + dHi);
                    static const float inDb[3] = { 0.0f, 15.0f, 30.0f };                 // -20 / -35 / -50 dB di sensibilita'
                    inG = std::pow (10.0f, inDb[std::clamp (pInLvl.step (*this), 0, 2)] / 20.0f);
                    outG = pOutLvl.step (*this) == 1 ? 0.1778f : 1.0f;                   // -35 dB: 15 dB sotto
                    const float t = pTone.get (*this);
                    toneDb = t < 0.5f ? -16.0f * (1.0f - 2.0f * t) : 12.0f * (2.0f * t - 1.0f);
                    dryT = 0.6f; wetT = 0.6f;
                }
                else if (model == BassClone)
                {
                    swingMs = 0.5f * (dHi - dLo) * pDepth.get (*this);
                    lvl = 1.0f; dryT = 0.55f; wetT = 0.5f;
                    bassDb = (pBass.get (*this) - 0.5f) * 24.0f; trebDb = (pTreble.get (*this) - 0.5f) * 24.0f;
                    hp.set (sr, xover, 0.707); hp2.set (sr, xover * 0.9, 0.6);
                }
                else if (model == StereoTheory)
                {
                    const float dep = mode == 0 ? 0.55f : pDepth.get (*this);           // CHR 1: profondita' preimpostata
                    swingMs = 0.5f * (dHi - dLo) * dep;
                    lvl = 1.0f;
                    if (mode == 2) { dryT = 0.0f; wetT = 1.0f; } else { dryT = 0.55f; wetT = 0.55f; }
                }
                else // Eddy
                {
                    const bool chorus = mode == 1;
                    swingMs = 0.5f * (dHi - dLo) * pDepth.get (*this);
                    if (chorus) { dryT = 0.6f; wetT = 0.6f; } else { dryT = 0.0f; wetT = 1.0f; }
                    lvl = 2.0f * taperA (pLevel.get (*this)) / 0.96f;
                    const float t = (pTone.get (*this) - 0.5f) * 2.0f;                  // tilt +-8 dB a 1,2 kHz sul wet
                    tiltLo = std::pow (10.0f, -0.4f * t); tiltHi = std::pow (10.0f, 0.4f * t);
                    tilt.set (sr, 1200);
                }
                const float shape = pShape.get (*this);
                const float envK = (pEnv.get (*this) - 0.5f) * 2.0f;
                const int envMode = pEnvMode.step (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float x = 0.5f * (inL + inR) * inS.next (inG);
                    if (model == Dc30) x = 3.0f * softSat (x / 3.0f);                    // preamplificatore d'ingresso (binari)
                    // inviluppo (Eddy): suonando forte cresce (o cala) rate/depth
                    float rMul = 1.0f, dMul = 1.0f;
                    if (model == Eddy)
                    {
                        const float e = envS.next (envK * std::min (1.0f, env.tick (x) * 8.0f));
                        if (envMode != 1) rMul = std::exp2 (2.0f * e);
                        if (envMode != 0) dMul = std::clamp (1.0f + e, 0.0f, 2.0f);
                    }
                    const float hz = rateS.next (rateHz) * rMul;
                    lfo.setHz (sr, hz);
                    float l0, l1;
                    if (model == Eddy)
                    {
                        // SHAPE: al centro sinusoide simmetrica, ai lati forme asimmetriche (fronte rapido)
                        const float sk = 0.5f + 0.47f * (shape - 0.5f) * 2.0f;
                        l0 = std::sin (1.5707963f * lfo.skew (sk)); l1 = l0;
                    }
                    else { l0 = lfo.tri(); l1 = lfo.tri (0.5); }
                    lfo.step();
                    const float sw = depS.next (swingMs) * dMul;
                    const float d0 = posS[0].next (centreMs + sw * l0), d1 = posS[1].next (centreMs + sw * l1);
                    float v = x;
                    if (model == BassClone)
                    {
                        const float xo = xoS.next (pXover.step (*this) == 1 ? 1.0f : 0.0f);   // X-OVER: niente bassi nel ramo modulato
                        v = x + xo * (hp2.hp (hp.hp (x)) - x);
                    }
                    float w0 = bbd[0].tick (v, std::clamp (d0, dLo, dHi));
                    float w1 = (numCh > 1 && (model == Dc30 || model == StereoTheory)) ? 0.0f : w0;
                    if (model == Eddy) w0 = tilt.tick (w0, tLoS.next (tiltLo), tHiS.next (tiltHi)), w1 = w0;
                    (void) d1;
                    const float dg = dryS.next (dryT), wg = wetS.next (wetT), g = lvlS.next (lvl), og = outS.next (outG);
                    float dryX = x;
                    if (model == BassClone) dryX = shelf[0].tick (x, bassDb);            // BASS solo sul dry
                    float L = dg * dryX + wg * w0, R;
                    if (numCh > 1 && (model == Dc30 || model == StereoTheory)) R = dg * dryX - wg * w0;   // seconda uscita in controfase
                    else R = dg * dryX + wg * w1;
                    if (model == Dc30) { L = shelf[0].tick (L, toneDb); R = numCh > 1 ? shelf[1].tick (R, toneDb) : L; }
                    if (model == BassClone) { L = shelf[1].tick (L, trebDb); R = numCh > 1 ? shelf[2].tick (R, trebDb) : L; }
                    L = outRail (L * g * og); R = outRail (R * g * og);
                    if (numCh > 1) { ch[0][i] = L; ch[1][i] = R; }
                    else ch[0][i] = L;
                }
            }
        private:
            Config cfg;
            Model model = Dc30;
            int stages = 1024;
            float sr = 48000, dLo = 3, dHi = 12, rLo = 0.1f, rHi = 8, noiseV = 3e-4f, sat = 1.2f, toneDb = 0, bassDb = 0, trebDb = 0,
                  tiltLo = 1, tiltHi = 1;
            double aa = 8000, xover = 220, bassHz = 120, trebHz = 2500;
            bool comp = false, pre = false;
            RoleParam pLevel, pTone, pDepth, pRate, pInLvl, pOutLvl, pBass, pTreble, pXover, pMode, pEnvMode, pShape, pEnv;
            BbdChain bbd[1];
            SmoothEq shelf[3];
            Smooth tLoS, tHiS;
            Svf hp, hp2;
            Tilt tilt;
            Envelope env;
            Lfo lfo;
            Smooth lvlS, inS, outS, wetS, dryS, toneS, bassS, trebS, xoS, envS, posS[2], rateS, depS;
        };

        //==============================================================================
        /** Due linee BBD multimodo: Echoflanger (Adrian Belew) e Stereo Polychorus. */
        class B3BbdMulti final : public Effect
        {
        public:
            enum Model { Echoflanger, StPolychorus };
            enum Mode { Matrix, Flange, ChoFlange, Chorus, Slap, SlapMatrix, Double };
            explicit B3BbdMulti (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                model = cfg.str ("model", "echofl") == "stpoly" ? StPolychorus : Echoflanger;
                pVol = role (*this, "level", 0.6f); pFb = role (*this, "res", 0.4f); pRate = role (*this, "rate", 0.3f);
                pWidth = role (*this, "depth", 0.5f); pTune = role (*this, "manual", 0.5f); pMode = role (*this, "mode", 0.0f);
                pBlend = role (*this, "blend", 1.0f); pSweepF = role (*this, "sweepf", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                if (model == Echoflanger)
                {
                    lineA.prepare (s, 1024, 9000, 0.0004f, true, false, 1.2f);     // SAD1024 (stima) con compander
                    lineB.prepare (s, 1024, 9000, 0.0004f, true, false, 1.2f);
                }
                else
                {
                    lineA.prepare (s, 256, 12000, 0.0003f, true, false, 1.3f);     // MN3009 256 stadi: flange
                    lineB.prepare (s, 2048, 8000, 0.0004f, true, false, 1.3f);     // MN3008 2048 stadi: chorus / double track
                }
                fader.prepare (s, 15);
                for (auto* sm : { &volS, &fbS, &blS }) sm->set (s, 20);
                pA.set (s, 40); pB.set (s, 6); rateS.set (s, 40);
                reset();
            }
            void reset() override { lineA.reset(); lineB.reset(); fbA = fbB = 0; lfo.ph = 0.25; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sel = pMode.step (*this);
                Mode want;
                if (model == Echoflanger)
                {
                    static const Mode m[6] = { Matrix, Flange, ChoFlange, Chorus, Slap, SlapMatrix };
                    want = m[std::clamp (sel, 0, 5)];
                }
                else
                {
                    static const Mode m[4] = { Matrix, Flange, Chorus, Double };
                    want = m[std::clamp (sel, 0, 3)];
                }
                const float rate = logMap (pRate.get (*this), 0.1f, 10.0f), width = pWidth.get (*this), tuneT = pTune.get (*this);
                const float fbK = pFb.get (*this);
                const float vol = model == Echoflanger ? volKnob (pVol.get (*this), 0.6f, 2.5f) : 1.0f;
                const bool blendOn = model == StPolychorus || pBlend.step (*this) == 1;
                const bool sweepFilter = model == StPolychorus && pSweepF.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick ((int) want, changed);
                    if (changed) { lineA.reset(); lineB.reset(); fbA = fbB = 0; }
                    const Mode m = (Mode) fader.current;
                    lfo.setHz (sr, rateS.next (rate));
                    const float l = lfo.tri(), lr = lfo.tri (0.5);
                    lfo.step();
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    const float tune = pA.next (tuneT);
                    // ritardi della modalita' (ms)
                    auto flangeMs = [&] (float lv) { return 0.4f * std::pow (25.0f, std::clamp ((1.0f - tune) + 0.5f * width * lv, 0.0f, 1.0f)); };
                    auto chorusMs = [&] (float lv) { return (model == Echoflanger ? 6.0f : 5.0f) * std::pow (4.0f, std::clamp (tune + 0.25f * width * lv, 0.0f, 1.0f)); };
                    auto slapMs = [&] (float lv) { return (model == Echoflanger ? 40.0f : 30.0f) * std::pow (model == Echoflanger ? 4.0f : 3.3f, tune) * (1.0f + 0.01f * width * lv); };
                    float dA = 1, dB = 1, fA = 0, fB = 0;
                    const float fb = fbS.next (fbK);
                    switch (m)
                    {
                        case Matrix:     dA = flangeMs (0); dB = flangeMs (0) * 1.03f; fA = fB = 0.92f * fb; break;
                        case Flange:     dA = flangeMs (l); dB = flangeMs (lr); fA = fB = 0.92f * fb; break;
                        case ChoFlange:  dA = flangeMs (l); dB = chorusMs (lr); fA = 0.92f * fb; fB = 0.3f * fb; break;
                        case Chorus:     dA = chorusMs (l); dB = chorusMs (lr); fA = fB = 0.35f * fb; break;
                        case Slap:       dA = slapMs (l); dB = slapMs (lr) * 1.01f; fA = fB = 0.88f * fb; break;
                        case SlapMatrix: dA = flangeMs (0); dB = slapMs (lr); fA = 0.92f * fb; fB = 0.88f * fb; break;
                        case Double:     dA = 30.0f * std::pow (3.3f, tune) * (1.0f + 0.02f * width * l); dB = dA * 1.02f; fA = fB = 0.6f * fb; break;
                    }
                    // la sezione flange usa la linea corta, chorus/slap/double quella lunga (Polychorus)
                    auto& LA = (model == StPolychorus && (m == Chorus || m == Double)) ? lineB : lineA;
                    // filtri anti-alias/ricostruzione: seguono il clock del BBD (0,4 x f_clock) dove serve
                    auto track = [] (BbdChain& L, float dMs, double base) { L.setFilter (std::clamp (0.4 * L.numStages() / (2.0 * dMs * 0.001), 900.0, base)); };
                    if (model == Echoflanger) { track (lineA, dA, 9000.0); track (lineB, dB, 9000.0); }
                    else
                    {
                        // SWEEP FILTER: anche la linea corta (flange) ha il passa-basso che segue il clock, piu' 'liquido'
                        if (sweepFilter) lineA.setFilter (std::clamp (0.22 * 256.0 / (2.0 * dA * 0.001), 1500.0, 15000.0));
                        else lineA.setFilter (12000.0);
                        track (lineB, dA, 8000.0);
                    }
                    float a, b;
                    if (model == StPolychorus)
                    {
                        a = LA.tick (x + loopSat (fA * fbA), dA);
                        b = a; fbA = a;
                    }
                    else
                    {
                        a = lineA.tick (x + loopSat (fA * fbA), dA); fbA = a;
                        b = lineB.tick (x + loopSat (fB * fbB), dB); fbB = b;
                    }
                    const bool combined = m == ChoFlange || m == SlapMatrix;
                    float wL, wR;
                    if (model == StPolychorus) { wL = a; wR = -a; }                        // OUTPUT 2 in controfase
                    else if (combined) { wL = 0.7f * (a + b); wR = 0.7f * (a - b); }
                    else { wL = a; wR = b; }
                    const float bl = blS.next (blendOn ? 1.0f : 0.0f);
                    const float dryG = bl * 0.7f, wetG = (0.7f + 0.3f * (1.0f - bl)) * fg;
                    const float g = volS.next (vol);
                    if (numCh > 1)
                    {
                        ch[0][i] = outRail (g * (dryG * inL + wetG * wL));
                        ch[1][i] = outRail (g * (dryG * inR + wetG * wR));
                    }
                    else ch[0][i] = outRail (g * (dryG * inL + wetG * (combined ? 0.7f * (a + b) : a)));
                }
            }
        private:
            Config cfg;
            Model model = Echoflanger;
            float sr = 48000, fbA = 0, fbB = 0;
            RoleParam pVol, pFb, pRate, pWidth, pTune, pMode, pBlend, pSweepF;
            BbdChain lineA, lineB;
            XFader fader;
            Lfo lfo;
            Smooth volS, fbS, blS, pA, pB, rateS;
        };

        //==============================================================================
        /** Flanger/chorus digitali con Filter Matrix. */
        class B3Mistress final : public Effect
        {
        public:
            explicit B3Mistress (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                neo = cfg.str ("model", "stereo") == "neo";
                matrixAt = (float) cfg.num ("matrix", neo ? 0.42 : 0.33);
                pRate = role (*this, "rate", 0.5f); pFl = role (*this, "flanger", 0.6f); pCh = role (*this, "chorus", 0.0f);
                pFb = role (*this, "res", 0.5f);
            }
            void prepare (double s, int) override { eng.prepare (s); flS.set (s, 20); chS.set (s, 20); fbS.set (s, 20); reset(); }
            void reset() override { eng.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float rk = pRate.get (*this);
                float fl, cho, fbT, sweep;
                if (neo)
                {
                    const float f = pFb.get (*this);
                    fl = 1.0f; cho = 0.0f; fbT = 0.93f * f; sweep = 0.45f + 0.55f * f;     // FEEDBACK allarga anche l'LFO
                }
                else { fl = pFl.get (*this); cho = pCh.get (*this); fbT = 0.8f; sweep = 1.0f; }
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    float l, r;
                    eng.tick (x, rk, matrixAt, flS.next (fl), chS.next (cho), fbS.next (fbT), sweep, l, r);
                    if (numCh > 1) { ch[0][i] = l; ch[1][i] = r; }
                    else ch[0][i] = l;
                }
            }
        private:
            Config cfg;
            bool neo = false;
            float matrixAt = 0.33f;
            RoleParam pRate, pFl, pCh, pFb;
            MistressEngine eng;
            Smooth flS, chS, fbS;
        };

        //==============================================================================
        /** Flanger Hoax: phaser fisso + phaser spazzato, ciascuno con la propria linea di ritardo. */
        class B3Hoax final : public Effect
        {
        public:
            explicit B3Hoax (const ModelDef& d) : Effect (d)
            {
                pBlend = role (*this, "blend", 0.5f); pFb = role (*this, "feedback", 0.3f); pFbSrc = role (*this, "fbsource", 0.5f);
                pAmount = role (*this, "amount", 0.5f); pResp = role (*this, "response", 0.0f); pModMode = role (*this, "modmode", 0.0f);
                pModRate = role (*this, "modrate", 0.3f); pDlyMode = role (*this, "dlymode", 0.0f); pFixAmt = role (*this, "fixamt", 0.3f);
                pInvert = role (*this, "invert", 0.0f); pSwpAmt = role (*this, "swpamt", 0.3f); pByp = role (*this, "phbypass", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                lineF.prepare (s, 1024, 10000, 0.0003f, true, false, 1.3f);
                lineS.prepare (s, 1024, 10000, 0.0003f, true, false, 1.3f);
                fixA = apCoef (1000.0f, sr);
                for (auto* sm : { &blS, &fbS, &amtS, &fxS, &swS, &invS, &bpF, &bpS, &srcW, &srcS }) sm->set (s, 20);
                dF.set (s, 3); dS.set (s, 3); rateS.set (s, 30);
                reset();
            }
            void reset() override { lineF.reset(); lineS.reset(); for (auto& a : apF) a.reset(); for (auto& a : apS) a.reset(); fbW = fbSw = 0; ph = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float rate = logMap (pModRate.get (*this), 0.07f, 220.0f);
                static const float modPh[5] = { 0.0f, 0.25f, 0.5f, 0.75f, -1.0f };             // fase della modulante del phaser (DC = ferma)
                const float phSw = modPh[std::clamp (pModMode.step (*this), 0, 4)];
                static const float dPh[5][2] = { { 0.0f, 0.5f }, { -1.0f, 0.5f }, { 0.0f, -1.0f }, { 0.25f, 0.5f }, { -1.0f, -1.0f } };
                const auto& dp = dPh[std::clamp (pDlyMode.step (*this), 0, 4)];
                const bool logResp = pResp.step (*this) == 1;
                const int src = pFbSrc.step (*this);                                              // 0 WET, 1 OFF, 2 SWPT
                const int byp = pByp.step (*this);                                                // 0 nessuno, 1 fisso, 2 spazzato, 3 entrambi
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    ph += rateS.next (rate) / sr; ph -= std::floor (ph);
                    auto mod = [this] (float off) { return off < 0 ? 0.0f : std::sin (6.2831853f * (float) (ph + off)); };
                    // phaser spazzato: 6 all-pass, sfasamento a 1 kHz da 240 a 990 gradi
                    const float am = amtS.next (pAmount.get (*this));
                    const float m = phSw < 0 ? 0.0f : mod (phSw);
                    float pos = std::clamp (am * (0.5f + 0.5f * m), 0.0f, 1.0f);
                    if (! logResp) pos = std::sqrt (pos);                                          // LIN: piu' tempo vicino agli estremi alti
                    // 6 stadi: fase totale 6*2*atan(f/fc); fc da 3,3 kHz (240 gradi a 1 kHz) a 140 Hz (990 gradi)
                    const float fc = 3300.0f * std::pow (140.0f / 3300.0f, pos);
                    const float aS = apCoef (fc, sr);
                    const float fbIn = src == 0 ? fbW : src == 2 ? fbSw : 0.0f;
                    const float in = x + loopSat (0.8f * fbS.next (pFb.get (*this)) * fbIn);
                    float vF = in, vS = in;
                    for (auto& a : apF) vF = a.tick (vF, fixA);                                     // 3 stadi fissi a 1 kHz (270 gradi): 240 a ~880 Hz
                    for (auto& a : apS) vS = a.tick (vS, aS);
                    const float kF = bpF.next (byp == 1 || byp == 3 ? 1.0f : 0.0f), kS = bpS.next (byp == 2 || byp == 3 ? 1.0f : 0.0f);
                    vF = kF * in + (1.0f - kF) * vF;
                    vS = kS * in + (1.0f - kS) * vS;
                    // linee di ritardo 1..11 ms: DC = ritardo fisso dall'AMOUNT, altrimenti modulazione intorno a 6 ms
                    const float fa = fxS.next (pFixAmt.get (*this)), sa = swS.next (pSwpAmt.get (*this));
                    const float dFix = dF.next (dp[0] < 0 ? 1.0f + 10.0f * fa : 6.0f + 5.0f * fa * mod (dp[0]));
                    const float dSw = dS.next (dp[1] < 0 ? 1.0f + 10.0f * sa : 6.0f + 5.0f * sa * mod (dp[1]));
                    float yF = lineF.tick (vF, dFix), yS = lineS.tick (vS, dSw);
                    yF *= 1.0f - 2.0f * invS.next (pInvert.step (*this) == 1 ? 1.0f : 0.0f);         // INVERT: +180 gradi
                    const float wet = 0.6f * (yF + yS);
                    fbW = wet; fbSw = yS;
                    const float b = blS.next (pBlend.get (*this));
                    const float y = (1.0f - b) * x + b * wet;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }
        private:
            float sr = 48000, fixA = 0, fbW = 0, fbSw = 0;
            double ph = 0;
            RoleParam pBlend, pFb, pFbSrc, pAmount, pResp, pModMode, pModRate, pDlyMode, pFixAmt, pInvert, pSwpAmt, pByp;
            BbdChain lineF, lineS;
            Allpass1 apF[3], apS[6];
            Smooth blS, fbS, amtS, fxS, swS, invS, bpF, bpS, srcW, srcS, dF, dS, rateS;
        };

        //==============================================================================
        /** Wiggler: tremolo e vibrato a valvole. */
        class B3Wiggler final : public Effect
        {
        public:
            explicit B3Wiggler (const ModelDef& d) : Effect (d)
            {
                pRate = role (*this, "rate", 0.4f); pDepth = role (*this, "depth", 0.5f); pLevel = role (*this, "level", 0.6f);
                pMode = role (*this, "mode", 0.0f); pVib = role (*this, "vibmode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                dl.allocate ((int) (0.03 * s));
                fader.prepare (s, 15);
                inHp.set (s, 30); outLp.set (s, 9000); cplHp.set (s, 25);
                lvlS.set (s, 20); depS.set (s, 20); rateS.set (s, 40); posS.set (s, 2);
                reset();
            }
            void reset() override { dl.clear(); for (auto& a : ap) a.reset(); inHp.reset(); outLp.reset(); cplHp.reset(); lfo.ph = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int want = pMode.step (*this) == 0 ? 0 : 1 + std::clamp (pVib.step (*this), 0, 3);
                const float lvl = 2.0f * taperA (pLevel.get (*this)) / 0.4f;
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (want, changed);
                    if (changed) { dl.clear(); for (auto& a : ap) a.reset(); }
                    const float x = monoIn (ch, numCh, i);
                    // primo triodo: guadagno ~ 8, asimmetrico (polarizzazione a catodo), compensato
                    const float z = 3.0f * inHp.hp (x);
                    const float t1 = (std::tanh (z + 0.25f) - 0.2449f) / 3.0f * 1.15f;
                    const float hz = rateS.next (logMap (pRate.get (*this), 0.5f, 12.0f));
                    lfo.setHz (sr, hz);
                    const float s = lfo.sine(), tr = lfo.tri();
                    lfo.step();
                    const float dep = depS.next (pDepth.get (*this));
                    float y;
                    dl.push (t1);
                    switch (fader.current)
                    {
                        case 0:   // tremolo a polarizzazione: inviluppo morbido un po' asimmetrico
                        {
                            const float u = 0.5f + 0.5f * s;
                            y = t1 * (1.0f - dep * (0.15f + 0.85f * u * u));
                            break;
                        }
                        case 1:   // LOOZ: vibrato profondo e morbido con un filo di ampiezza
                            y = dl.read (posS.next ((0.004f + 0.0032f * dep * s) * sr)) * (1.0f - 0.08f * dep * (0.5f + 0.5f * s));
                            break;
                        case 2:   // HAMM: scanner a 9 prese (linea LC) spazzato a triangolo
                        {
                            const float scan = (0.5f + 0.5f * tr) * 8.0f * (0.3f + 0.7f * dep);
                            const int k = std::clamp ((int) scan, 0, 7); const float f = scan - (float) k;
                            const float tap = 0.00011f * sr;
                            y = (1.0f - f) * dl.read (2.0f + tap * (float) k) + f * dl.read (2.0f + tap * (float) (k + 1));
                            y = 0.75f * y + 0.25f * dl.read (2.0f + tap * 4.0f * dep);
                            break;
                        }
                        case 3:   // ACEY: vibrato a sfasamento (4 all-pass spazzati, solo wet)
                        {
                            const float a = apCoef (300.0f * std::pow (10.0f, 0.5f + 0.5f * dep * s), sr);
                            float v = t1; for (auto& st : ap) v = st.tick (v, a);
                            y = v;
                            break;
                        }
                        default:  // WURL: vibrato con forte modulazione d'ampiezza in fase
                            y = dl.read (posS.next ((0.003f + 0.0018f * dep * s) * sr)) * (1.0f - 0.35f * dep * (0.5f - 0.5f * s));
                            break;
                    }
                    // secondo triodo e accoppiamento d'uscita
                    y = cplHp.hp (outLp.lp (std::tanh (1.6f * y) / 1.6f));
                    y *= lvlS.next (lvl) * fg;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }
        private:
            float sr = 48000;
            RoleParam pRate, pDepth, pLevel, pMode, pVib;
            DelayLine dl;
            Allpass1 ap[4];
            OnePole inHp, outLp, cplHp;
            Lfo lfo;
            XFader fader;
            Smooth lvlS, depS, rateS, posS;
        };

        //==============================================================================
        /** Nano Pulsar e Super Pulsar. */
        class B3Pulsar final : public Effect
        {
        public:
            explicit B3Pulsar (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                super = cfg.str ("model", "nano") == "super";
                pVol = role (*this, "level", 0.5f); pDepth = role (*this, "depth", 0.6f); pShape = role (*this, "shape", 0.5f);
                pRate = role (*this, "rate", 0.4f); pWave = role (*this, "wave", 0.0f); pPhase = role (*this, "phase", 0.5f);
                pRange = role (*this, "range", 0.5f); pInv = role (*this, "invert", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& e : edge) e.set (s, 150);
                for (auto* sm : { &shS, &dS, &wS, &vS, &phS, &invS }) sm->set (s, 20);
                rateS.set (s, 30);
                reset();
            }
            void reset() override { ph = 0; for (auto& e : edge) e.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // VOL: tutto a sinistra = muto, fino a +12 dB
                const float vk = pVol.get (*this);
                const float vol = vk < 0.01f ? 0.0f : 4.0f * taperA (vk) / taperA (1.0f);
                float hz;
                if (super)
                {
                    static const float lo[3] = { 0.0625f, 0.5f, 4.0f }, hi[3] = { 3.5f, 28.0f, 230.0f };   // SLOW / MEDIUM / FAST
                    const int r = std::clamp (pRange.step (*this), 0, 2);
                    hz = logMap (pRate.get (*this), lo[r], hi[r]);
                }
                else hz = logMap (pRate.get (*this), 0.05f, 30.0f);
                // DEPTH: profondita' piena a ore 1 (0,6 della corsa), oltre il guadagno attraversa lo zero
                const float dk = pDepth.get (*this);
                const float dT = super ? dk : (dk <= 0.6f ? dk / 0.6f : 1.0f + (dk - 0.6f) / 0.4f);
                const float waveK = pWave.get (*this);
                const bool inv = pInv.step (*this) == 1;
                const bool square = ! super && pWave.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    ph += rateS.next (hz) / sr; ph -= std::floor (ph);
                    const float sh = shS.next (pShape.get (*this)), depth = dS.next (dT);
                    float uA, uB;
                    if (super)
                    {
                        const float rph = phS.next (pPhase.get (*this));                    // fase del canale destro 0..360 gradi
                        const float wk = wS.next (waveK);
                        uA = superWave ((float) ph, wk, sh, 0);
                        uB = superWave ((float) (ph + rph - std::floor (ph + rph)), wk, sh, 1);
                        const float iv = invS.next (inv ? 1.0f : 0.0f);                            // WAVE INVERT senza salti
                        uA = (1.0f - iv) * uA + iv * (1.0f - uA); uB = (1.0f - iv) * uB + iv * (1.0f - uB);
                    }
                    else
                    {
                        const float duty = 0.08f + 0.84f * sh;
                        const float p = (float) ph;
                        const float uq = p < duty ? 1.0f : 0.0f;
                        const float skw = std::clamp (1.0f - sh, 0.001f, 0.999f);
                        const float ut = 0.5f + 0.5f * (p < skw ? -1.0f + 2.0f * p / skw : 1.0f - 2.0f * (p - skw) / (1.0f - skw));
                        const float ws = wS.next (square ? 1.0f : 0.0f);
                        uA = ws * edge[0].lp (uq) + (1.0f - ws) * ut;
                        uB = ws * edge[1].lp (1.0f - uq) + (1.0f - ws) * (1.0f - ut);
                    }
                    const float gA = (1.0f - depth * uA) * vS.next (vol), gB = (1.0f - depth * uB) * vS.y;
                    if (numCh > 1)
                    {
                        if (super) { ch[0][i] = outRail (ch[0][i] * gA); ch[1][i] = outRail (ch[1][i] * gB); }   // ingressi stereo
                        else { const float x = 0.5f * (ch[0][i] + ch[1][i]); ch[0][i] = outRail (x * gA); ch[1][i] = outRail (x * gB); }
                    }
                    else ch[0][i] = outRail (ch[0][i] * gA);
                }
            }
        private:
            /** WAVE: meta' sinistra SINE -> TRIANGLE -> PULSE (SHAPE = simmetria/larghezza);
                meta' destra RHYTHM: SHAPE sceglie uno di 9 ritmi a 8 passi, WAVE la lunghezza dell'impulso. */
            inline float superWave (float p, float wk, float sh, int c) noexcept
            {
                if (wk < 0.5f)
                {
                    const float w = wk / 0.5f;                                               // 0 sinusoide, 0.5 triangolo, 1 impulso
                    const float skw = std::clamp (0.5f + 0.45f * (sh - 0.5f) * 2.0f, 0.05f, 0.95f);
                    const float tri = p < skw ? p / skw : 1.0f - (p - skw) / (1.0f - skw);      // 0..1
                    const float sine = 0.5f - 0.5f * std::cos (pi * tri);
                    const float pul = edge[c].lp (p < 0.08f + 0.84f * sh ? 1.0f : 0.0f);
                    return w < 0.5f ? sine + (tri - sine) * (w / 0.5f) : tri + (pul - tri) * ((w - 0.5f) / 0.5f);
                }
                static const uint8_t rhythms[9] = { 0xFF, 0xAA, 0xEE, 0xB6, 0x92, 0xDA, 0x88, 0xF6, 0xA9 };
                const int r = std::clamp ((int) (sh * 8.99f), 0, 8);
                const float steps = p * 8.0f; const int st = std::min (7, (int) steps);
                const float frac = steps - (float) st;
                const float len = 0.15f + 0.8f * (wk - 0.5f) / 0.5f;
                const float on = ((rhythms[r] >> (7 - st)) & 1) && frac < len ? 1.0f : 0.0f;
                return 1.0f - edge[c].lp (on);                                              // ritmo: i colpi aprono il volume
            }
            Config cfg;
            bool super = false;
            float sr = 48000;
            double ph = 0;
            RoleParam pVol, pDepth, pShape, pRate, pWave, pPhase, pRange, pInv;
            OnePole edge[2];
            Smooth shS, dS, wS, vS, phS, rateS, invS;
        };

        //==============================================================================
        /** Mod Rex: quattro modulatori sincronizzati sul tempo. */
        class B3ModRex final : public Effect
        {
        public:
            explicit B3ModRex (const ModelDef& d) : Effect (d)
            {
                pVol = role (*this, "level", 0.6f); pTempo = role (*this, "tempo", 0.5f);
                pModDiv = role (*this, "moddiv", 4.0f / 9); pModType = role (*this, "modtype", 0.0f); pModDepth = role (*this, "moddepth", 0.5f);
                pModFb = role (*this, "modfb", 0.3f); pTremDiv = role (*this, "tremdiv", 0.0f); pTremDepth = role (*this, "tremdepth", 0.6f);
                pPanDiv = role (*this, "pandiv", 0.0f); pFiltDiv = role (*this, "filtdiv", 0.0f); pFiltMode = role (*this, "filtmode", 0.0f);
                pFiltDepth = role (*this, "filtdepth", 0.6f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& l : dl) l.allocate ((int) (0.03 * s));
                for (auto* sm : { &volS, &mdS, &fbS, &tdS, &fdS }) sm->set (s, 20);
                for (auto& sm : secG) sm.set (s, 15);
                for (auto& sm : fmS) sm.set (s, 15);
                for (auto& sm : fTopS) sm.set (s, 20);
                for (auto& sr2 : lS) for (auto& sm : sr2) sm.set (s, 3);
                for (auto& sm : dS) sm.set (s, 3);
                bpmS.set (s, 80);
                fader.prepare (s, 15);
                reset();
            }
            void reset() override
            {
                for (auto& l : dl) l.clear();
                for (auto& c : ap) for (auto& a : c) a.reset();
                for (auto& f : flt) f.reset();
                fb[0] = fb[1] = 0; beat = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float bpm = logMap (pTempo.get (*this), 10.0f, 500.0f) * 1.0f;
                // suddivisioni (in battiti): 0 = sezione spenta
                static const float beats[10] = { 0, 4.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };
                const int dv[4] = { pModDiv.step (*this), pTremDiv.step (*this), pPanDiv.step (*this), pFiltDiv.step (*this) };
                const int modType = std::clamp (pModType.step (*this), 0, 3), fMode = std::clamp (pFiltMode.step (*this), 0, 2);
                const float vol = volKnob (pVol.get (*this), 0.6f, 2.5f);
                const float fTop = logMap (pFiltDepth.get (*this), 300.0f, 12000.0f);
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (modType, changed);
                    if (changed) { for (auto& l : dl) l.clear(); for (auto& c : ap) for (auto& a : c) a.reset(); fb[0] = fb[1] = 0; }
                    beat += (double) bpmS.next (bpm) / 60.0 / sr;
                    if (beat > 1.0e6) beat -= 1.0e6;                                           // multiplo di tutte le suddivisioni (x 12)
                    float lfoV[4][2];
                    for (int s = 0; s < 4; ++s)
                    {
                        const float g = secG[s].next (dv[s] > 0 ? 1.0f : 0.0f);
                        float lv[2] = { 0.5f, 0.5f };
                        if (dv[s] > 0)
                        {
                            const double p = beat / (double) beats[std::clamp (dv[s], 1, 9)];
                            for (int c = 0; c < 2; ++c)
                            {
                                double q = p + (c == 1 && s == 0 ? 0.5 : 0.0);                  // R INV sulla sezione MOD
                                q -= std::floor (q);
                                lv[c] = (float) (q < 0.5 ? 2.0 * q : 2.0 - 2.0 * q);            // triangolo 0..1
                            }
                        }
                        // LFO lisciato 3 ms: i cambi di suddivisione (salto di fase) non fanno click
                        lfoV[s][0] = lS[s][0].next (lv[0] * g + 0.5f * (1.0f - g)); lfoV[s][1] = lS[s][1].next (lv[1] * g + 0.5f * (1.0f - g));
                        secOn[s] = g;
                    }
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float v[2] = { inL, inR };
                    // FILTER (i tre modi incrociati in 15 ms)
                    for (int k = 0; k < 3; ++k) fmS[k].next (fMode == k ? 1.0f : 0.0f);
                    if (secOn[3] > 0.0001f)
                        for (int c = 0; c < 2; ++c)
                        {
                            const float fc = 120.0f * std::pow (fTopS[c].next (fTop) / 120.0f, lfoV[3][c]);
                            float lo, bp, hi;
                            flt[c].setG (fastTanPi (fc, sr), 1.0f / 2.2f);
                            flt[c].tick (v[c], lo, bp, hi);
                            const float f = fmS[0].y * lo + fmS[1].y * hi + fmS[2].y * 1.6f * bp;
                            v[c] = secOn[3] * f + (1.0f - secOn[3]) * v[c];
                        }
                    // MOD
                    if (secOn[0] > 0.0001f)
                    {
                        const float dep = mdS.next (pModDepth.get (*this)), fbk = fbS.next (pModFb.get (*this));
                        for (int c = 0; c < 2; ++c)
                        {
                            const float m = lfoV[0][c];
                            float y;
                            switch (fader.current)
                            {
                                case 0:  dl[c].push (v[c]); y = dl[c].read (dS[c].next ((0.002f + 0.006f * dep * m) * sr)); break;                       // VIB
                                case 1:  dl[c].push (v[c] + loopSat (0.9f * fbk * fb[c])); fb[c] = dl[c].read (dS[c].next ((0.0003f + 0.006f * dep * m) * sr));  // FLN
                                         y = 0.65f * (v[c] + fb[c]); break;
                                case 2:  dl[c].push (v[c]); y = 0.7f * (v[c] + dl[c].read (dS[c].next ((0.008f + 0.008f * dep * m) * sr))); break;        // CHR
                                default:                                                                                                                 // PHS
                                {
                                    const float a = apCoef (150.0f * std::pow (30.0f, dep * m), sr);
                                    float w = v[c] + loopSat (0.8f * fbk * fb[c]);
                                    for (auto& st : ap[c]) w = st.tick (w, a);
                                    fb[c] = w; y = 0.65f * (v[c] + w);
                                    break;
                                }
                            }
                            v[c] = secOn[0] * fg * y + (1.0f - secOn[0] * fg) * v[c];
                        }
                    }
                    // TREM
                    const float td = tdS.next (pTremDepth.get (*this));
                    for (int c = 0; c < 2; ++c) v[c] *= 1.0f - secOn[1] * td * lfoV[1][0];
                    // PAN (in mono agisce come un secondo tremolo con la forma invertita)
                    const float pn = lfoV[2][0];
                    float outL, outR;
                    if (numCh > 1)
                    {
                        const float gl = std::cos (1.5707963f * pn), gr = std::sin (1.5707963f * pn);
                        outL = (1.0f - secOn[2]) * v[0] + secOn[2] * 1.4142f * gl * v[0];
                        outR = (1.0f - secOn[2]) * v[1] + secOn[2] * 1.4142f * gr * v[1];
                    }
                    else { outL = v[0] * (1.0f - secOn[2] * (1.0f - pn)); outR = outL; }
                    const float g = volS.next (vol);
                    ch[0][i] = outRail (outL * g);
                    if (numCh > 1) ch[1][i] = outRail (outR * g);
                }
            }
        private:
            float sr = 48000, fb[2] {}, secOn[4] {};
            double beat = 0;
            RoleParam pVol, pTempo, pModDiv, pModType, pModDepth, pModFb, pTremDiv, pTremDepth, pPanDiv, pFiltDiv, pFiltMode, pFiltDepth;
            DelayLine dl[2];
            Allpass1 ap[2][6];
            Svf flt[2];
            XFader fader;
            Smooth volS, mdS, fbS, tdS, fdS, secG[4], dS[2], bpmS, fmS[3], lS[4][2], fTopS[2];
        };

        //==============================================================================
        /** Stereo Polyphase: phaser a fotoaccoppiatori con LFO, inviluppo e limiti START/STOP. */
        class B3Polyphase final : public Effect
        {
        public:
            explicit B3Polyphase (const ModelDef& d) : Effect (d)
            {
                pFb = role (*this, "res", 0.4f); pMode = role (*this, "mode", 0.5f); pGain = role (*this, "sens", 0.5f);
                pRate = role (*this, "rate", 0.4f); pStart = role (*this, "start", 0.0f); pStop = role (*this, "stop", 1.0f);
                pSw = role (*this, "speed", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& l : ldr) l.set (s, 5, 45, 3000, 120000, 0.8f);
                fbS.set (s, 20); stS.set (s, 30); spS.set (s, 30); sqLp.set (s, 25);
                reset();
            }
            void reset() override { for (auto& c : ap) for (auto& a : c) a.reset(); fb[0] = fb[1] = 0; e = 0; lfo.ph = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = pMode.step (*this);                       // 0 ENV, 1 LFO, 2 EXP
                const bool alt = pSw.step (*this) == 1;                   // giu': SLOW (ENV) / quadra (LFO)
                lfo.setHz (sr, logMap (pRate.get (*this), 0.05f, 12.0f));
                const float sens = 30.0f * taperA (pGain.get (*this));
                const float aE = (float) std::exp (-1.0 / ((alt ? 0.030 : 0.004) * sr)), rE = (float) std::exp (-1.0 / ((alt ? 0.6 : 0.12) * sr));
                const float fbT = 0.85f * pFb.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    float p;
                    if (mode == 0)
                    {
                        const float a = std::abs (x) * sens;
                        e = a > e ? a + aE * (e - a) : a + rE * (e - a);
                        p = std::clamp (e, 0.0f, 1.0f);
                    }
                    else if (mode == 1) p = alt ? sqLp.lp (lfo.square() > 0 ? 1.0f : 0.0f) : 0.5f + 0.5f * lfo.tri();
                    else p = 0.5f;                                          // EXP senza pedale: a meta' fra START e STOP
                    lfo.step();
                    const float st = stS.next (pStart.get (*this)), sp = spS.next (pStop.get (*this));
                    const float k = fbS.next (fbT);
                    float out[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        const float pc = c == 0 ? p : 1.0f - p;              // OUTPUT 2: escursione complementare
                        const float light = st + (sp - st) * pc;             // START > STOP inverte il verso
                        const float R = ldr[c].tick (light);
                        const float a = apCoef (1.0f / (2.0f * pi * R * 15e-9f), sr);
                        float v = x + loopSat (k * fb[c]);
                        for (auto& s : ap[c]) v = s.tick (v, a);
                        fb[c] = v;
                        out[c] = 0.55f * (x + v) * 1.1f;
                    }
                    if (numCh > 1) { ch[0][i] = out[0]; ch[1][i] = out[1]; }
                    else ch[0][i] = out[0];
                }
            }
        private:
            float sr = 48000, fb[2] {}, e = 0;
            RoleParam pFb, pMode, pGain, pRate, pStart, pStop, pSw;
            Allpass1 ap[2][6];
            Ldr ldr[2];
            Lfo lfo;
            OnePole sqLp;
            Smooth fbS, stS, spS;
        };

        //==============================================================================
        /** Bad Stone V1: 6 all-pass a JFET con feedback e COLOR (solo effetto). */
        class B3BadStone1 final : public Effect
        {
        public:
            explicit B3BadStone1 (const ModelDef& d) : Effect (d)
            {
                pBlend = role (*this, "res", 0.4f); pRate = role (*this, "rate", 0.3f); pColor = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override { sr = (float) s; fbS.set (s, 20); colS.set (s, 15); rateS.set (s, 40); reset(); }
            void reset() override { for (auto& a : ap) a.reset(); fb = 0; lfo.ph = 0.25; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float fbT = 0.82f * pBlend.get (*this);
                const float colT = pColor.step (*this) == 1 ? 1.0f : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    lfo.setHz (sr, rateS.next (logMap (pRate.get (*this), 0.2f, 32.0f)));
                    // JFET in parallelo a 47k, 22n: fc 154 Hz a JFET spento, ~5 kHz in piena conduzione
                    const float u = 0.03f + 0.97f * (0.5f + 0.5f * lfo.tri());
                    lfo.step();
                    const float rds = 1500.0f / u, R = 47000.0f * rds / (47000.0f + rds);
                    const float a = apCoef (1.0f / (2.0f * pi * R * 22e-9f), sr);
                    const float x = monoIn (ch, numCh, i);
                    float v = x + loopSat (fbS.next (fbT) * fb);
                    for (auto& s : ap) v = s.tick (v, a);
                    fb = v;
                    const float c = colS.next (colT);
                    const float y = (1.0f - c) * 0.5f * (x + v) * 1.2f + c * v;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = y;
                }
            }
        private:
            float sr = 48000, fb = 0;
            RoleParam pBlend, pRate, pColor;
            Allpass1 ap[6];
            Lfo lfo;
            Smooth fbS, colS, rateS;
        };
    }

    std::unique_ptr<Effect> makeB3Mod (const ModelDef& d, const std::string& type)
    {
        if (type == "b3chorus")    return std::make_unique<B3Chorus> (d);
        if (type == "b3bbdmulti")  return std::make_unique<B3BbdMulti> (d);
        if (type == "b3mistress")  return std::make_unique<B3Mistress> (d);
        if (type == "b3hoax")      return std::make_unique<B3Hoax> (d);
        if (type == "b3wiggler")   return std::make_unique<B3Wiggler> (d);
        if (type == "b3pulsar")    return std::make_unique<B3Pulsar> (d);
        if (type == "b3modrex")    return std::make_unique<B3ModRex> (d);
        if (type == "b3polyphase") return std::make_unique<B3Polyphase> (d);
        if (type == "b3badstone1") return std::make_unique<B3BadStone1> (d);
        return nullptr;
    }
}
