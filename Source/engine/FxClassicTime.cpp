/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Effetti di tempo della tappa 3:
      * mmbbd      - delay a BBD della famiglia Memory Man (catena completa: stadio
                     d'ingresso, pre-enfasi, compander, filtri anti-alias e di ricostruzione
                     descritti in config, BBD a clock reale, feedback filtrato, LFO sul clock);
      * multidelay - delay digitali multimodo (#1 Echo, Stereo Memory Man with Hazarai,
                     Canyon, Grand Canyon);
      * sixteen    - 16 Second Digital Delay: memoria fissa a 12 bit, frequenza di
                     campionamento che scende allungando il tempo;
      * classicverb- riverberi (molle dispersive, FDN, plate di Dattorro, reverse, flerb,
                     eco, tremolo, dinamico, auto-infinito, shimmer, poly, risonante);
      * stain      - distorsione + DSP (Holy Stain);
      * loop720    - looper con reverse e mezza velocita'.
*/

#include "FxClassic.h"
#include "FxVerbCore.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        inline float railClip (float x, float rail) noexcept { return rail * std::tanh (x / rail); }

        //==============================================================================
        class MmBbdDelay final : public Effect
        {
        public:
            enum Model { Mm76, Dmm, Boy, Toy, Slap };
            explicit MmBbdDelay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "dmm");
                model = m == "mm76" ? Mm76 : m == "boy" ? Boy : m == "toy" ? Toy : m == "slap" ? Slap : Dmm;
                stages = (int) cfg.num ("stages", 4096);
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? (float) dl[0] : 30.0f; dHi = dl.size() > 1 ? (float) dl[1] : 550.0f;
                linTaper = cfg.str ("taper", "log") == "lin";
                useComp = cfg.num ("comp", 1) > 0.5;
                auto ct = cfg.list ("ctau"); tauC = ct.size() > 0 ? ct[0] : 20; tauE = ct.size() > 1 ? ct[1] : tauC;
                aaSpec = cfg.str ("aa", "3000:0.8,3000:0.8"); rcSpec = cfg.str ("rc", aaSpec.c_str());
                auto pe = cfg.list ("pre"); preZ = pe.size() > 0 ? (float) pe[0] : 0; preP = pe.size() > 1 ? (float) pe[1] : 0;
                fbLp = cfg.num ("fblp", 4000); fbHp = cfg.num ("fbhp", 60);
                fbMax = (float) cfg.num ("fbmax", 1.0);
                noiseV = (float) cfg.num ("noise", 3e-4); sat = (float) cfg.num ("sat", 1.0);
                inGain = (float) cfg.num ("ingain", 1.0);
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.95f; rHi = rt.size() > 1 ? (float) rt[1] : 4.4f;
                swing = (float) cfg.num ("swing", 0.1);
                modRates = cfg.list ("modrates"); if (modRates.empty()) modRates = { 0.5 };
                times = cfg.list ("times");
                pTime = role (*this, "time", 0.5f); pFb = role (*this, "feedback", 0.35f); pBlend = role (*this, "blend", 0.45f);
                pLevel = role (*this, "level", 0.39f); pRate = role (*this, "rate", 0.0f); pDepth = role (*this, "depth", 0.25f);
                pBoost = role (*this, "boost", 0.0f); pWave = role (*this, "wave", 0.0f); pMode = role (*this, "mode", 0.0f);
                pModOn = role (*this, "modon", 1.0f); pGain = role (*this, "gain", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                bbd.allocate (stages);
                bbd.setLoss (std::min (0.35f, stages * 2.0e-5f));
                bbd.setNoise (noiseV);
                aa.configure (s, aaSpec, 3000); rc.configure (s, rcSpec, 3000);
                comp.set (s, tauC, tauE, 0.3f);
                if (preP > 0) { preF.set (s, preP); deF.set (s, preZ); }
                fbL.set (s, fbLp); fbH.set (s, fbHp);
                boostLow.set (s, 1.0 / (2.0 * juce::MathConstants<double>::pi * 10e3 * 220e-9));    // 72 Hz
                boostHigh.set (s, 1.0 / (2.0 * juce::MathConstants<double>::pi * 10e3 * 2.2e-9));   // 7,2 kHz
                lvlPole.set (s, 5800); lfoRound.set (s, 9.6); squareSlew.set (s, 40); dcIn.set (s, 20);
                dS.set (s, 60); fbS.set (s, 20); bS.set (s, 20); gS.set (s, 20); depS.set (s, 30); boostS.set (s, 6); lvlS.set (s, 25);
                reset();
            }
            void reset() override
            {
                bbd.clear(); aa.reset(); rc.reset(); comp.reset(); preF.reset(); deF.reset(); fbL.reset(); fbH.reset();
                boostLow.reset(); boostHigh.reset(); lvlPole.reset(); lfoRound.reset(); squareSlew.reset(); dcIn.reset();
                fbState = 0; curD = -1; lfo.ph = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // tempo del clock
                float dT;
                if (model == Slap)
                {
                    const int k = std::clamp (pMode.step (*this), 0, (int) std::max<size_t> (1, times.size()) - 1);
                    dT = times.empty() ? 65.0f : (float) times[(size_t) k];
                }
                else dT = linTaper ? dLo + (dHi - dLo) * pTime.get (*this) : logMap (pTime.get (*this), dLo, dHi);
                if (curD < 0) { curD = dT; dS.snap (dT); }
                // feedback (DMM: pot 10kA)
                float fbT = model == Slap ? 0.0f : (model == Dmm ? taperA (pFb.get (*this)) : pFb.get (*this)) * fbMax;
                // stadio d'ingresso
                float inG = inGain, rail = 6.0f;
                if (model == Dmm)
                {
                    lvlTarget = pLevel.get (*this);
                }
                if (model == Slap) { inG = std::pow (10.0f, pGain.get (*this)); rail = 3.8f; }
                if (model == Boy || model == Toy) rail = 3.8f;
                const bool boost = model == Mm76 && pBoost.step (*this) == 1;
                // LFO sul clock
                float rateHz = 0, depth = 0;
                bool squareW = false;
                if (model == Dmm) { rateHz = logMap (pRate.get (*this), rLo, rHi); depth = swing * pDepth.get (*this); }
                else if (model == Boy)
                {
                    rateHz = (float) modRates[(size_t) std::clamp (pMode.step (*this), 0, (int) modRates.size() - 1)];
                    depth = swing * pDepth.get (*this);
                    squareW = pWave.step (*this) == 1;
                }
                else if (model == Toy) { rateHz = (float) modRates[0]; depth = pModOn.step (*this) == 1 ? swing : 0.0f; }
                lfo.setHz (sr, std::max (0.01f, rateHz));
                const float blendT = pBlend.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    // ingresso
                    float xin;
                    // BOOST: x3,5 sui bassi (220n) e x4,5 sugli acuti (2,2n), commutato con dissolvenza
                    const float bm = boostS.next (boost ? 1.0f : 0.0f);
                    const float hiPart = x - boostHigh.lp (x);
                    xin = bm * (3.5f * x + hiPart) + (1.0f - bm) * gS.next (inG) * x;
                    if (model == Dmm)
                    {
                        const float rf = 22e3f + 1e6f * taperA (lvlS.next (lvlTarget));     // R3 22k + LEVEL 1MA (reostato)
                        xin = (rf / 100e3f) * x;                                              // x0,22 .. x10,2
                        lvlPole.setFast ((float) sr, 1.0f / (2.0f * pi * rf * 27e-12f));      // C2 27p
                        xin = lvlPole.lp (xin);
                    }
                    xin = railClip (xin, rail);
                    const float dry = (model == Boy || model == Toy) ? x : xin;
                    // clock con modulazione: il periodo varia di +-swing
                    float l = squareW ? squareSlew.lp (lfo.square()) : lfoRound.lp (lfo.tri());
                    lfo.step();
                    const float dep = depS.next (depth);
                    const float dNow = dS.next (dT) * (1.0f + dep * l);
                    const double cps = (double) stages / (2.0 * dNow * 0.001) / sr;
                    // somma ingresso + feedback, pre-enfasi, compressore, anti-alias, BBD
                    float v = xin + fbS.next (fbT) * fbState;
                    if (preP > 0) { const float lp = preF.lp (v); v = lp + (preP / preZ) * (v - lp); }
                    if (useComp) v = comp.compress (v);
                    v = aa.tick (v);
                    v = sat * softSat (v / sat);
                    float y = bbd.tick (v, cps);
                    y = rc.tick (y);
                    if (useComp) y = comp.expand (y);
                    if (preP > 0) { const float lp = deF.lp (y); y = lp + (preZ / preP) * (y - lp); }
                    y = dcIn.hp (y);
                    // anello di feedback filtrato e limitato dagli operazionali
                    fbState = 1.5f * softSat (fbH.hp (fbL.lp (y)) / 1.5f);
                    const float wet = model == Dmm ? 0.75f * y : y;
                    const float b = bS.next (blendT);
                    const float out = (1.0f - b) * dry + b * wet;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = out;
                }
            }
        private:
            Config cfg;
            Model model = Dmm;
            int stages = 4096;
            float dLo = 30, dHi = 550, fbMax = 1, noiseV = 3e-4f, sat = 1, inGain = 1, rLo = 0.95f, rHi = 4.4f, swing = 0.1f,
                  preZ = 0, preP = 0, fbState = 0, curD = -1;
            double sr = 48000, tauC = 20, tauE = 20, fbLp = 4000, fbHp = 60;
            bool linTaper = false, useComp = true;
            std::string aaSpec, rcSpec;
            std::vector<double> modRates, times;
            RoleParam pTime, pFb, pBlend, pLevel, pRate, pDepth, pBoost, pWave, pMode, pModOn, pGain;
            BbdLine bbd;
            FilterChain aa, rc;
            Compander comp;
            OnePole preF, deF, fbL, fbH, boostLow, boostHigh, lvlPole, lfoRound, squareSlew, dcIn;
            Lfo lfo;
            Smooth dS, fbS, bS, gS, depS, boostS, lvlS;
            float lvlTarget = 0.39f;
        };

        //==============================================================================
        /** Delay digitali multimodo. */
        class MultiDelay final : public Effect
        {
        public:
            enum Algo { Echo, Mod, Multi, Reverse, DmmA, Tape, Verb, Oct, Shim, SampleHold, Loop, PitchA, Drum, Doubler };
            enum Model { One, Hazarai, Canyon, Grand };

            explicit MultiDelay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "canyon");
                model = m == "one" ? One : m == "hazarai" ? Hazarai : m == "grand" ? Grand : Canyon;
                pBlend = role (*this, "blend", 0.4f); pTime = role (*this, "time", 0.4f); pFb = role (*this, "feedback", 0.35f);
                pMode = role (*this, "mode", 0.0f); pP1 = role (*this, "p1", 0.5f); pP2 = role (*this, "p2", 0.5f);
                pPing = role (*this, "pingpong", 0.0f); pDecay = role (*this, "decay", 0.0f); pFilter = role (*this, "filter", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto& l : line) l.allocate ((int) (s * 3.3));
                for (auto& f : shifter) f.prepare (s, 60);
                plate.prepare (s);
                for (int k = 0; k < 4; ++k) diffL[k].allocate ((int) (0.03 * s));
                fader.prepare (s);
                for (auto& f : lp) f.reset();
                tS.set (s, 120); bS.set (s, 20); fbS.set (s, 20); dS2.set (s, 20); p1S.set (s, 30); p2S.set (s, 30);
                wowL.setHz (s, 0.6); flut.setHz (s, 6.3); modL.setHz (s, 1.0);
                env.set (s, 2, 80);
                shBuf.assign ((size_t) (s * 3.1), 0.0f);
                reset();
            }
            void reset() override
            {
                for (auto& l : line) l.clear();
                for (auto& f : shifter) f.clear();
                plate.clear();
                for (auto& dd : diffL) dd.clear();
                for (auto& f : lp) f.reset();
                for (auto& f : hp) f.reset();
                for (auto& f : svf) f.reset();
                fb[0] = fb[1] = 0; revPos = 0; shLen = 0; shPos = 0; shState = 0; shLevel = 0; crushHold[0] = crushHold[1] = 0; crushPh = 0;
                std::fill (shBuf.begin(), shBuf.end(), 0.0f);
                curT = -1;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sel = pMode.present() ? pMode.step (*this) : 0;
                // gamma del tempo per modello/modo
                float tLo = 5, tHi = 3000;
                if (model == One) { tLo = 10; tHi = 2000; }
                if (model == Hazarai)
                {
                    static const float hi[8] = { 3000, 1000, 300, 3000, 1000, 1000, 3000, 3000 };
                    tLo = 10; tHi = hi[std::clamp (sel, 0, 7)];
                }
                if (algoOf (sel) == Doubler) { tLo = 10; tHi = 120; }
                const float tMs = logMap (pTime.get (*this), tLo, tHi);
                const float fbK = pFb.get (*this);
                const float p1T = pP1.get (*this), p2T = pP2.get (*this);
                const bool ping = (model == Grand && pPing.step (*this) == 1) || model == Hazarai;
                // legge del livello
                float wetT, dryT;
                const float bl = pBlend.get (*this);
                if (model == Canyon || model == Grand) { wetT = std::min (1.0f, bl / 0.67f); dryT = bl < 0.67f ? 1.0f : (1.0f - bl) / 0.33f; }
                else { wetT = bl; dryT = 1.0f - bl; }
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (sel, changed);
                    if (changed) { reset(); }
                    const Algo algo = algoOf (fader.current);
                    const float p1 = p1S.next (p1T), p2 = p2S.next (p2T);
                    // tempo con glissando (come i delay digitali EHX quando si gira DELAY)
                    if (curT < 0) curT = tMs;
                    curT = tS.next (tMs);
                    float d = curT * 0.001f * sr;
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float mono = 0.5f * (inL + inR);
                    float fbAmt = fbS.next (algo == Loop ? fbK : fbK * (model == One ? 0.97f : 1.0f));
                    // modulazioni di lettura
                    float dm = d;
                    if (algo == Mod)
                    {
                        const float rate = model == Grand ? logMap (p1, 0.1f, 8.0f) : 1.1f;
                        const float depthMs = model == Grand ? 6.0f * p2 : 2.0f;
                        modL.setHz (sr, rate);
                        dm = d + depthMs * 0.001f * sr * modL.sine();
                        modL.step();
                    }
                    else if (model == Hazarai && fader.current == 2)    // ECHO 300 ms + MOD
                    {
                        modL.setHz (sr, 0.9);
                        dm = d * (1.0f + 0.004f * modL.sine()); modL.step();
                    }
                    else if (algo == DmmA)
                    {
                        const float depth = model == Grand ? 0.012f * p1 : 0.004f, rate = model == Grand ? logMap (p2, 0.3f, 5.0f) : 0.95f;
                        modL.setHz (sr, rate); dm = d * (1.0f + depth * modL.tri()); modL.step();
                    }
                    else if (algo == Tape || algo == Drum)
                    {
                        const float amt = model == Grand ? p1 : 0.5f;
                        dm = d * (1.0f + amt * (0.0025f * wowL.sine() + 0.0008f * flut.sine()));
                        wowL.step(); flut.step();
                    }
                    else if (algo == Doubler)
                    {
                        modL.setHz (sr, 0.7f + 2.0f * p2);
                        dm = d * (1.0f + (model == Grand ? 0.08f * p1 : 0.05f) * modL.sine()); modL.step();
                    }
                    float wet[2] = { 0, 0 };
                    for (int c = 0; c < 2; ++c)
                    {
                        if (algo == Reverse || (model == Hazarai && fader.current == 6))
                            wet[c] = reverseRead (c, d);
                        else if (algo == Multi)
                        {
                            int taps; float shape;
                            if (model == Grand) { taps = 2 + (int) std::lround (p1 * 29.0f); shape = p2; }
                            else if (model == Hazarai) { taps = 1 + (int) std::lround (fbK * 7.0f); shape = 0.3f; }
                            else { taps = 4; shape = 0.35f; }
                            float acc = 0, norm = 0;
                            for (int k = 1; k <= taps; ++k)
                            {
                                const float pos = (float) k / (float) taps;
                                const float g = shape < 0.5f ? std::pow (1.0f - pos * 0.8f, (0.5f - shape) * 4.0f)     // decay
                                                             : std::pow (0.2f + 0.8f * pos, (shape - 0.5f) * 4.0f);    // swell
                                acc += g * line[c].read (dm * pos);
                                norm += g;
                            }
                            wet[c] = acc / std::max (1.0f, 0.5f * norm);
                            multiK[c] = std::max (1.0f, 0.5f * norm) / std::max (1.0f, norm);   // nell'anello guadagno <= 1
                            if (model == Hazarai && fader.current == 5) wet[c] = 0.5f * (wet[c] + reverseRead (c, d));
                        }
                        else if (algo == Drum)
                        {
                            static const int heads[4] = { 0xF, 0x5, 0x9, 0xA };   // combinazioni di testine
                            const int hm = heads[std::clamp ((int) (p1 * 3.99f), 0, 3)];
                            float acc = 0; int cnt = 0;
                            for (int k = 0; k < 4; ++k) if (hm >> k & 1) { acc += line[c].read (dm * (k + 1) / 4.0f); ++cnt; }
                            wet[c] = acc / std::max (1, cnt) * 1.4f;
                        }
                        else wet[c] = line[c].read (dm);
                    }
                    if (algo == Reverse || (model == Hazarai && (fader.current == 6))) ++revPos;
                    // trattamento delle ripetizioni (nel feedback)
                    float f[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        float v = wet[c];
                        switch (algo)
                        {
                            case DmmA:
                                lp[c].setFast (sr, 2600.0f); v = lp[c].lp (lp[c].lp (v) * 0.5f + v * 0.5f);
                                v = 1.2f * softSat (v / 1.2f);
                                break;
                            case Tape:
                            {
                                const float tone = model == Grand ? 2500.0f + 6000.0f * p2 : 4500.0f;
                                lp[c].setFast (sr, tone); v = lp[c].lp (v);
                                hp[c].setFast (sr, 90.0f); v = hp[c].hp (v);
                                v = 0.9f * std::tanh (v * 1.6f) / 1.6f * 1.6f / 1.4f;
                                break;
                            }
                            case Drum:
                                lp[c].setFast (sr, 2500.0f + 5000.0f * p2); v = lp[c].lp (v);
                                v = std::tanh (v * 1.8f) / 1.8f;
                                break;
                            case Oct:
                                shifter[c].setRatio (2.0f);
                                v = 0.6f * v + 0.6f * shifter[c].tick (v);
                                break;
                            case Shim:
                            {
                                shifter[c].setRatio (2.0f);
                                float s = shifter[c].tick (v);
                                v = 0.5f * v + 0.7f * s;
                                break;
                            }
                            case PitchA:
                            {
                                const float semis = std::round ((p1 - 0.5f) * 24.0f);
                                shifter[c].setRatio (std::pow (2.0f, semis / 12.0f));
                                const float s = shifter[c].tick (v);
                                v = (1.0f - p2) * v + p2 * s * 1.2f;
                                break;
                            }
                            case Echo:
                                if (model == Grand)
                                {
                                    // MINI 1: filtro (sx passa-basso, dx passa-alto); MINI 2: bit crusher fino a 2 bit / 220 Hz
                                    if (p1 < 0.48f) { lp[c].setFast (sr, logMap (p1 / 0.48f, 400.0f, 20000.0f)); v = lp[c].lp (v); }
                                    else if (p1 > 0.52f) { hp[c].setFast (sr, logMap ((p1 - 0.52f) / 0.48f, 20.0f, 3000.0f)); v = hp[c].hp (v); }
                                    if (p2 > 0.02f)
                                    {
                                        const float bits = 24.0f - 22.0f * p2;
                                        const float q = std::pow (2.0f, bits - 1.0f);
                                        const float fsC = logMap (p2, sr * 0.5f, 220.0f);
                                        if (c == 0) { crushPh += fsC / sr; }
                                        if (crushPh >= 1.0f) { crushHold[c] = std::round (v * q) / q; if (c == 1) crushPh -= std::floor (crushPh); }
                                        v = crushHold[c];
                                    }
                                }
                                break;
                            default: break;
                        }
                        if (model == Hazarai)
                        {
                            // FILTER: a sinistra passa-basso, a destra passa-alto; DECAY: diffusione nel feedback
                            const float fk = pFilter.get (*this);
                            if (fk < 0.47f) { lp[c].setFast (sr, logMap (fk / 0.47f, 500.0f, 18000.0f)); v = lp[c].lp (v); }
                            else if (fk > 0.53f) { hp[c].setFast (sr, logMap ((fk - 0.53f) / 0.47f, 30.0f, 2500.0f)); v = hp[c].hp (v); }
                            const float dec = pDecay.get (*this);
                            if (dec > 0.01f)
                            {
                                static const float dl[2][2] = { { 0.0113f, 0.0167f }, { 0.0131f, 0.0191f } };
                                for (int k = 0; k < 2; ++k)
                                {
                                    auto& L = diffL[c * 2 + k];
                                    const float g = 0.75f * dec;
                                    const float dd = L.read (dl[c][k] * sr);
                                    const float y = -g * v + dd;
                                    L.push (v + g * y);
                                    v = y;
                                }
                            }
                        }
                        // anello di feedback sempre stabile: il guadagno dei rami (ottava, shimmer, pitch, multitap)
                        // e' riportato a <= 1 e la ripetizione e' limitata in modo morbido (come gli operazionali)
                        float loopK = 1.0f;
                        if (algo == Oct) loopK = 1.0f / 1.2f;
                        else if (algo == Shim) loopK = 1.0f / 1.2f;
                        else if (algo == PitchA) loopK = 1.0f / std::max (1.0f, (1.0f - p2) + 1.2f * p2);
                        else if (algo == Multi) loopK = multiK[c];
                        f[c] = v * loopK;
                        wet[c] = algo == Oct || algo == Shim || algo == PitchA || model == Hazarai ? v : wet[c];
                    }
                    if (algo == Tape || algo == DmmA || algo == Drum) { wet[0] = f[0]; wet[1] = f[1]; }
                    // ingresso nelle linee
                    float wIn0 = mono, wIn1 = mono;
                    if (numCh > 1 && ! ping) { wIn0 = inL; wIn1 = inR; }
                    if (model == Hazarai && fader.current == 7 && fbK > 0.95f) { wIn0 = wIn1 = 0; fbAmt = 1.0f; }   // DEJA LOOP congelato
                    if (algo == Doubler) fbAmt = 0;
                    if (algo == SampleHold)
                    {
                        processSampleHold (mono, d, fbK, wet);
                        fbAmt = 0;
                    }
                    // ripetizioni limitate in modo morbido prima di rientrare nella linea (mai divergenze)
                    auto loopSat = [] (float x) { return 2.0f * softSat (x * 0.5f); };
                    if (ping)
                    {
                        line[0].push (wIn0 + fbAmt * loopSat (f[1]));
                        line[1].push (fbAmt * loopSat (f[0]));
                    }
                    else
                    {
                        line[0].push (wIn0 + fbAmt * loopSat (f[0]));
                        line[1].push (wIn1 + fbAmt * loopSat (f[1]));
                    }
                    // plate dopo l'eco (VERB / REVERB)
                    if (algo == Verb)
                    {
                        float rl, rr;
                        plate.set (model == Grand ? 1.0f + 6.0f * p2 : 2.5f, 7000.0f, 1.0f, 8.0f);
                        plate.tick (0.5f * (wet[0] + wet[1]) + 0.5f * mono, rl, rr);
                        const float amt = model == Grand ? p1 : 0.5f;
                        wet[0] += amt * rl; wet[1] += amt * rr;
                    }
                    if (algo == Shim)
                    {
                        float rl, rr;
                        plate.set (4.0f, 9000.0f, 1.2f, 10.0f);
                        plate.tick (0.5f * (wet[0] + wet[1]), rl, rr);
                        wet[0] = 0.6f * wet[0] + 0.6f * rl; wet[1] = 0.6f * wet[1] + 0.6f * rr;
                    }
                    const float wg = bS.next (wetT) * fg;
                    const float dg = dS2.next (dryT);
                    if (numCh > 1)
                    {
                        ch[0][i] = dg * inL + wg * wet[0];
                        ch[1][i] = dg * inR + wg * (ping || numCh > 1 ? wet[1] : wet[0]);
                    }
                    else ch[0][i] = dg * inL + wg * (ping ? 0.5f * (wet[0] + wet[1]) : wet[0]);
                }
            }
        private:
            Algo algoOf (int sel) const noexcept
            {
                switch (model)
                {
                    case One: return Echo;
                    case Hazarai:
                    {
                        static const Algo a[8] = { Echo, Echo, Echo, Multi, Multi, Multi, Reverse, Loop };
                        return a[std::clamp (sel, 0, 7)];
                    }
                    case Canyon:
                    {
                        static const Algo a[11] = { Echo, Mod, Multi, Reverse, DmmA, Tape, Verb, Oct, Shim, SampleHold, Loop };
                        return a[std::clamp (sel, 0, 10)];
                    }
                    case Grand:
                    {
                        static const Algo a[12] = { Echo, Mod, Multi, Reverse, DmmA, Tape, Verb, PitchA, Shim, SampleHold, Drum, Doubler };
                        return a[std::clamp (sel, 0, 11)];
                    }
                }
                return Echo;
            }
            /** Lettura al contrario per segmenti lunghi quanto il ritardo, con due testine incrociate. */
            inline float reverseRead (int c, float d) const noexcept
            {
                const float seg = std::max (256.0f, d);
                float acc = 0;
                for (int k = 0; k < 2; ++k)
                {
                    float pos = std::fmod ((float) revPos + 0.5f * seg * (float) k, seg);
                    const float win = std::sin (pi * pos / seg);
                    acc += win * win * line[c].read (2.0f * pos + 1.0f);
                }
                return acc;
            }
            /** Sample & hold: all'attacco (soglia = FEEDBACK) cattura un frammento lungo DELAY e lo ripete. */
            void processSampleHold (float x, float d, float sens, float* wet) noexcept
            {
                const float e = env.tick (x);
                const float thr = 0.002f + 0.1f * (1.0f - sens) * (1.0f - sens);
                const int len = std::clamp ((int) d, 64, (int) shBuf.size() - 1);
                if (shState == 0 && e > thr && e > 1.6f * lastEnv) { shState = 1; shPos = 0; shLen = len; }
                lastEnv = 0.999f * lastEnv + 0.001f * e;
                if (shState == 1)
                {
                    shBuf[(size_t) shPos] = x;
                    if (++shPos >= shLen) { shState = 2; shPos = 0; shLevel = 1.0f; }
                }
                float y = 0;
                if (shState == 2)
                {
                    const float edge = std::min (1.0f, std::min ((float) shPos, (float) (shLen - shPos)) / 64.0f);
                    y = shBuf[(size_t) shPos] * shLevel * edge;
                    if (++shPos >= shLen) { shPos = 0; shLevel *= 0.85f; }
                    if (shLevel < 0.02f) shState = 0;
                    if (e > thr * 3.0f && e > 2.5f * lastEnv) shState = 0;     // nuova nota: ricattura
                }
                wet[0] = wet[1] = y;
            }

            Config cfg;
            Model model = Canyon;
            float sr = 48000, fb[2] {}, curT = -1, crushHold[2] {}, crushPh = 0, shLevel = 0, lastEnv = 0, multiK[2] { 1.0f, 1.0f };
            long long revPos = 0;
            int shLen = 0, shPos = 0, shState = 0;
            RoleParam pBlend, pTime, pFb, pMode, pP1, pP2, pPing, pDecay, pFilter;
            DelayLine line[2];
            Ring diffL[4];
            GrainShifter shifter[2];
            Plate plate;
            OnePole lp[2], hp[2];
            Svf svf[2];
            Lfo wowL, flut, modL;
            Envelope env;
            std::vector<float> shBuf;
            struct Fader { int current = -1, pending = -1; float g = 1, step = 0.003f;
                void prepare (double s) { step = (float) (1.0 / (0.01 * s)); }
                float tick (int w, bool& ch) { ch = false; if (current < 0) { current = w; ch = true; }
                    if (w != current) pending = w;
                    if (pending >= 0) { g -= step; if (g <= 0) { g = 0; current = pending; pending = -1; ch = true; } }
                    else if (g < 1) g = std::min (1.0f, g + step);
                    return g; } } fader;
            Smooth tS, bS, fbS, dS2, p1S, p2S;
        };

        //==============================================================================
        /** 16 Second Digital Delay. */
        class SixteenSecond final : public Effect
        {
        public:
            explicit SixteenSecond (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                mem = std::max (4096, (int) cfg.num ("mem", 87381));
                fsMax = (float) cfg.num ("fsmax", 32000);
                bits = (float) cfg.num ("bits", 12);
                maxS = (float) cfg.num ("maxs", 16); minS = (float) cfg.num ("mins", 0.03);
                pGain = role (*this, "gain", 0.5f); pTime = role (*this, "time", 0.4f); pFine = role (*this, "fine", 0.5f);
                pRate = role (*this, "rate", 0.3f); pDepth = role (*this, "depth", 0.0f); pFb = role (*this, "feedback", 0.35f);
                pClick = role (*this, "click", 0.0f); pLevel = role (*this, "level", 0.6f); pDry = role (*this, "dry", 0.8f);
                pRev = role (*this, "reverse", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                ram.assign ((size_t) mem, 0.0f);
                comp.set (s, 12, 12, 0.25f);
                tS.set (s, 150); fsS.set (s, 50); drS.set (s, 20); weS.set (s, 20); giS.set (s, 20);
                reset();
            }
            void reset() override
            {
                std::fill (ram.begin(), ram.end(), 0.0f);
                for (auto& f : aaIn) f.reset();
                for (auto& f : aaOut) f.reset();
                comp.reset(); wpos = 0; phase = 0; held = 0; out = 0; clickN = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float coarse = logMap (pTime.get (*this), minS, maxS);
                const float delayT = coarse * (0.9f + 0.2f * pFine.get (*this));
                const float gIn = logMap (pGain.get (*this), 0.25f, 4.0f);
                const float fb = std::min (1.0f, pFb.get (*this) * 1.02f);
                const float wetG = 2.0f * taperA (pLevel.get (*this)), dryG = 2.0f * taperA (pDry.get (*this));
                const float clickG = pClick.get (*this) * 0.3f;
                const bool rev = pRev.step (*this) == 1;
                lfo.setHz (sr, logMap (pRate.get (*this), 0.05f, 8.0f));
                const float depth = pDepth.get (*this) * 0.06f;
                const float q = std::pow (2.0f, bits - 1.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float dT = tS.next (delayT);
                    // memoria usata e frequenza di campionamento virtuale
                    const int used = std::clamp ((int) (dT * fsMax), 256, mem);
                    float fsV = (float) used / dT;
                    fsV *= 1.0f + depth * lfo.tri();
                    lfo.step();
                    fsV = fsS.next (fsV);
                    // filtri a capacita' commutate che seguono il clock (ellittico 7 poli ~ 2 x Butterworth del 4°)
                    const float fc = std::min (0.42f * fsV, 0.45f * sr);
                    if (std::abs (fc - lastFc) > 0.01f * lastFc) { setFilters (fc); lastFc = fc; }
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    const float gi = giS.next (gIn);
                    float a = aaIn[1].lp (aaIn[0].lp (std::clamp (x * gi, -1.0f, 1.0f)));     // convertitore a +-1 V
                    phase += fsV / sr;
                    while (phase >= 1.0f)
                    {
                        phase -= 1.0f;
                        if (wpos >= used) wpos = 0;
                        const int rp = rev ? (used - 1 - wpos) : wpos;
                        const float stored = ram[(size_t) rp];
                        out = comp.expand (stored);
                        if (wpos == 0) clickN = (int) (0.004f * sr);
                        // scrittura: ingresso + feedback, compresso e quantizzato a 12 bit
                        const float wv = std::clamp (comp.compress (a + fb * out), -1.0f, 1.0f);
                        ram[(size_t) wpos] = std::round (wv * q) / q;
                        ++wpos;
                    }
                    float y = aaOut[1].lp (aaOut[0].lp (out));
                    float clk = 0;
                    if (clickN > 0) { clk = clickG * std::sin (6.2831853f * 2000.0f * (float) clickN / sr); --clickN; }
                    const float o = drS.next (dryG) * x + weS.next (wetG) * y / std::max (0.25f, gi) + clk;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = o;
                }
            }
        private:
            void setFilters (float fc)
            {
                aaIn[0].set (sr, fc, 0.5412); aaIn[1].set (sr, fc, 1.3066);
                aaOut[0].set (sr, fc, 0.5412); aaOut[1].set (sr, fc, 1.3066);
            }
            Config cfg;
            int mem = 87381, wpos = 0, clickN = 0;
            float sr = 48000, fsMax = 32000, bits = 12, maxS = 16, minS = 0.03f, phase = 0, held = 0, out = 0, lastFc = 1;
            std::vector<float> ram;
            RoleParam pGain, pTime, pFine, pRate, pDepth, pFb, pClick, pLevel, pDry, pRev;
            Svf aaIn[2], aaOut[2];
            Compander comp;
            Lfo lfo;
            Smooth tS, fsS, drS, weS, giS;
        };

        //==============================================================================
        class ClassicVerb final : public Effect
        {
        public:
            enum Model { Holy, HolyPlus, HolyMax, Cathedral, Oceans11, Oceans12 };
            explicit ClassicVerb (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "holy");
                model = m == "holyplus" ? HolyPlus : m == "holymax" ? HolyMax : m == "cathedral" ? Cathedral
                      : m == "oceans11" ? Oceans11 : m == "oceans12" ? Oceans12 : Holy;
                pBlend = role (*this, "blend", 0.4f); pTime = role (*this, "time", 0.5f); pTone = role (*this, "tone", 0.5f);
                pMode = role (*this, "mode", 0.0f); pAmount = role (*this, "amount", 0.6f); pFb = role (*this, "feedback", 0.0f);
                pPre = role (*this, "predelay", 0.0f); pVar = role (*this, "variant", 0.0f); pP1 = role (*this, "p1", 0.5f);
                pP2 = role (*this, "p2", 0.5f);
            }
            void prepare (double s, int) override { sr = (float) s; core.prepare (s); fader.prepare (s); wS.set (s, 20); dS.set (s, 20); reset(); }
            void reset() override { core.clear(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sel = pMode.step (*this), var = std::clamp (pVar.present() ? pVar.step (*this) : 0, 0, 2);
                const float bl = pBlend.get (*this), tk = pTime.get (*this), tone = pTone.get (*this), am = pAmount.get (*this);
                VerbCore::Algo a = VerbCore::Hall;
                float t60 = 2.5f, toneHz = 7000, preS = 0, preFb = 0, pa = 0.5f, pb = 0.5f;
                float wet = 0.5f, dry = 1.0f;
                switch (model)
                {
                    case Holy:
                    {
                        static const VerbCore::Algo al[3] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Flerb };
                        a = al[std::clamp (sel, 0, 2)];
                        t60 = a == VerbCore::GSpring ? 2.2f : a == VerbCore::Hall ? 3.5f : 2.6f;
                        toneHz = a == VerbCore::GSpring ? 4200.0f : 6500.0f; pa = 0.1f; pb = 0.5f;
                        // sottile fino a ore 12, poi molto piu' bagnato
                        wet = bl <= 0.5f ? 0.35f * (bl / 0.5f) * (bl / 0.5f) : 0.35f + 0.85f * (bl - 0.5f) / 0.5f;
                        dry = bl <= 0.5f ? 1.0f : 1.0f - 0.5f * (bl - 0.5f) / 0.5f;
                        break;
                    }
                    case HolyPlus:
                    {
                        static const VerbCore::Algo al[4] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Room, VerbCore::Flerb };
                        a = al[std::clamp (sel, 0, 3)];
                        if (a == VerbCore::GSpring) { t60 = logMap (am, 0.6f, 5.0f); toneHz = 4200; }
                        else if (a == VerbCore::Hall) t60 = logMap (am, 0.8f, 8.0f);
                        else if (a == VerbCore::Room) { t60 = 1.1f; toneHz = logMap (1.0f - am, 1500.0f, 11000.0f); }
                        else { t60 = 2.6f; pa = am; pb = am; }
                        wet = 1.3f * bl; dry = 1.0f - bl;
                        break;
                    }
                    case HolyMax:
                    {
                        static const VerbCore::Algo al[4] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Plate_, VerbCore::Reverse };
                        a = al[std::clamp (sel, 0, 3)];
                        if (a == VerbCore::GSpring) { t60 = logMap (tk, 0.6f, 6.0f); toneHz = 4200; }
                        else if (a == VerbCore::Hall) t60 = logMap (tk, 1.0f, 12.0f);
                        else if (a == VerbCore::Plate_) t60 = tk > 0.97f ? 100.0f : logMap (tk / 0.97f, 0.5f, 32.0f);
                        else { t60 = 2.0f; pa = logMap (tk, 0.05f, 1.0f); }
                        // potenza costante
                        wet = 1.3f * std::sin (bl * 1.5707963f); dry = std::cos (bl * 1.5707963f);
                        break;
                    }
                    case Cathedral:
                    {
                        static const VerbCore::Algo al[8] = { VerbCore::GSpring, VerbCore::ASpring, VerbCore::Hall, VerbCore::Room,
                                                              VerbCore::Plate_, VerbCore::Reverse, VerbCore::Flerb, VerbCore::EchoV };
                        a = al[std::clamp (sel, 0, 7)];
                        t60 = tk > 0.97f ? 100.0f : logMap (tk / 0.97f, 0.3f, 20.0f);
                        if (a == VerbCore::Reverse) pa = logMap (tk, 0.08f, 1.5f), t60 = 1.5f;
                        toneHz = logMap (tone, 1200.0f, 14000.0f);
                        preS = 2.0f * pPre.get (*this) * pPre.get (*this);
                        preFb = 0.95f * pFb.get (*this);
                        pa = a == VerbCore::Reverse ? pa : 0.3f; pb = 0.5f;
                        wet = 1.3f * bl; dry = 1.0f - 0.6f * bl;
                        break;
                    }
                    case Oceans11:
                    {
                        static const VerbCore::Algo al[11] = { VerbCore::Hall, VerbCore::Spring6G15, VerbCore::Plate_, VerbCore::Reverse,
                                                               VerbCore::EchoV, VerbCore::Trem, VerbCore::ModV, VerbCore::Dyna,
                                                               VerbCore::AutoInf, VerbCore::Shim, VerbCore::Poly };
                        a = al[std::clamp (sel, 0, 10)];
                        t60 = tk > 0.97f ? 100.0f : logMap (tk / 0.97f, 0.4f, 15.0f);
                        toneHz = logMap (tone, 1500.0f, 14000.0f);
                        if (a == VerbCore::Spring6G15) { pa = 0.3f + 0.35f * var; t60 = logMap (tk, 0.8f, 4.0f); }
                        if (a == VerbCore::Reverse) { pa = logMap (tk, 0.1f, 1.5f); t60 = 2.0f; }
                        if (a == VerbCore::EchoV) { static const float et[3] = { 0.4f, 0.3f, 0.2f }; preS = et[var]; preFb = 0.4f; }
                        if (a == VerbCore::Trem) { pa = -1.0f; pb = 0.8f; }
                        if (a == VerbCore::Dyna) pa = 0.4f;
                        if (a == VerbCore::AutoInf) pa = 0.3f + 0.4f * var;
                        if (a == VerbCore::Shim) pa = 0.6f;
                        wet = 1.4f * std::pow (bl, 1.3f); dry = 1.0f;
                        break;
                    }
                    case Oceans12:
                    {
                        static const VerbCore::Algo al[12] = { VerbCore::Hall, VerbCore::Spring6G15, VerbCore::Plate_, VerbCore::Reverse,
                                                               VerbCore::EchoV, VerbCore::Trem, VerbCore::ModV, VerbCore::Dyna,
                                                               VerbCore::AutoInf, VerbCore::Shim, VerbCore::Poly, VerbCore::Resonant };
                        a = al[std::clamp (sel, 0, 11)];
                        const float p1 = pP1.get (*this), p2 = pP2.get (*this);
                        t60 = tk > 0.97f ? 100.0f : logMap (tk / 0.97f, 0.3f, 15.0f);
                        toneHz = logMap (tone, 1500.0f, 14000.0f);
                        preS = pPre.get (*this);
                        pa = p1; pb = p2;
                        if (a == VerbCore::Hall) { a = p1 < 0.5f ? VerbCore::Room : VerbCore::Hall; }
                        if (a == VerbCore::Reverse) { pa = logMap (p1, 0.1f, 1.5f); }
                        if (a == VerbCore::EchoV) { preS = logMap (p1, 0.05f, 1.0f); preFb = 0.9f * p2; }
                        if (a == VerbCore::Trem) { pa = p1; pb = p2; }
                        wet = 1.4f * std::pow (bl, 1.3f); dry = 1.0f;
                        break;
                    }
                }
                // decadimento lisciato (scala logaritmica, ~40 ms): niente salti di guadagno nel serbatoio
                const float lt = std::log (std::max (0.05f, t60));
                logT = logT < -50.0f ? lt : lt + 0.82f * (logT - lt);
                t60 = std::exp (logT);
                int dynVar = var;
                if (model == Oceans12 && a == VerbCore::Dyna) { dynVar = std::clamp ((int) (pP1.get (*this) * 2.99f), 0, 2); pa = pP2.get (*this); }
                bool changed;
                for (int i = 0; i < n; ++i)
                {
                    const int key = (int) a * 4 + var;
                    const float fg = fader.tick (key, changed);
                    if (changed) core.clear();
                    // i parametri nuovi valgono solo quando il motore corrente e' quello scelto (non durante la dissolvenza)
                    if ((i == 0 && fader.current == key) || changed)
                        if (fader.current == key) core.set (a, t60, toneHz, preS, preFb, pa, pb, a == VerbCore::Dyna ? dynVar : var);
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float wl, wr;
                    core.tick (0.5f * (inL + inR), wl, wr);
                    const float w = wS.next (wet) * fg, dg = dS.next (dry);
                    if (numCh > 1) { ch[0][i] = dg * inL + w * wl; ch[1][i] = dg * inR + w * wr; }
                    else ch[0][i] = dg * inL + w * 0.5f * (wl + wr);
                }
            }
        private:
            struct Fader { int current = -1, pending = -1; float g = 1, step = 0.003f;
                void prepare (double s) { step = (float) (1.0 / (0.012 * s)); }
                float tick (int w, bool& ch) { ch = false; if (current < 0) { current = w; ch = true; }
                    if (w != current) pending = w;
                    if (pending >= 0) { g -= step; if (g <= 0) { g = 0; current = pending; pending = -1; ch = true; } }
                    else if (g < 1) g = std::min (1.0f, g + step);
                    return g; } } fader;
            Config cfg;
            Model model = Holy;
            float sr = 48000;
            RoleParam pBlend, pTime, pTone, pMode, pAmount, pFb, pPre, pVar, pP1, pP2;
            float logT = -100.0f;
            VerbCore core;
            Smooth wS, dS;
        };

        //==============================================================================
        /** Holy Stain: distorsione analogica + DSP (room, hall, pitch, tremolo). */
        class StainFx final : public Effect
        {
        public:
            explicit StainFx (const ModelDef& d) : Effect (d)
            {
                pMix = role (*this, "mix", 0.5f); pAmount = role (*this, "amount", 0.5f); pLevel = role (*this, "level", 0.5f);
                pTone = role (*this, "tone", 0.5f); pColor = role (*this, "color", 0.0f); pDirt = role (*this, "dirt", 0.0f);
                pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                core.prepare (s);
                shifter.prepare (s, 60);
                inHp.set (s, 60); tiltLp.set (s, 800); colLp.set (s, 1800); colHp.set (s, 700); warm.set (s, 700, 1.0); warmLp.set (s, 4500);
                fuzzLp.set (s, 6000);
                mixS.set (s, 20); lvlS.set (s, 20); amtS.set (s, 50); tiltS.set (s, 25); for (auto& c : cS) c.set (s, 10);
                fader.prepare (s);
                reset();
            }
            void reset() override { core.clear(); shifter.clear(); inHp.reset(); tiltLp.reset(); colLp.reset(); colHp.reset(); warm.reset(); warmLp.reset(); fuzzLp.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int dirt = pDirt.step (*this), color = pColor.step (*this), mode = pMode.step (*this);
                const float am = pAmount.get (*this);
                const float tilt = (pTone.get (*this) - 0.5f) * 2.0f;          // -1 bassi .. +1 acuti
                const float lvl = std::pow (pLevel.get (*this) / 0.5f, 1.5f);
                const float mixT = pMix.get (*this);
                VerbCore::Algo a = mode == 1 ? VerbCore::Hall : VerbCore::Room;
                float t60 = mode == 1 ? logMap (am, 1.0f, 8.0f) : logMap (am, 0.3f, 2.5f);
                if (mode >= 2) t60 = 1.2f;
                {
                    const float lt = std::log (t60);
                    logT = logT < -50.0f ? lt : lt + 0.82f * (logT - lt);      // decadimento lisciato a blocchi
                    t60 = std::exp (logT);
                }
                const float preS = mode == 1 ? 0.045f : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (mode, changed);
                    if (changed) { core.clear(); shifter.clear(); }
                    if (fader.current == mode && (i == 0 || changed)) core.set (mode == 3 ? VerbCore::Room : a, t60, 7000.0f, preS, 0.0f, 0.5f, 0.5f, 0);
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    // sezione di distorsione
                    float v = inHp.hp (x);
                    if (dirt == 1)                   // FUZZ: guadagno alto, taglio duro asimmetrico
                    {
                        const float z = 60.0f * v;
                        v = (z > 0 ? std::min (z, 1.0f + 0.1f * std::tanh (z - 1.0f)) : std::max (z, -0.7f - 0.1f * std::tanh (-z - 0.7f))) * 0.5f;
                        v = std::tanh (v * 1.5f) * 0.45f;
                        v = fuzzLp.lp (v);
                    }
                    else if (dirt == 2)              // DRIVE: morbido
                        v = 0.5f * std::tanh (12.0f * v) / std::tanh (12.0f * 0.4f);
                    // COLOR
                    // COLOR: le tre reti sempre calcolate e incrociate (commutazione senza click)
                    const float lpB = colHp.lp (v);
                    const float cB = (lpB + 1.8f * (v - lpB)) * 0.75f, cD = colLp.lp (v), cW = warmLp.lp (v + 0.8f * warm.bp (v));
                    const float k0 = cS[0].next (color == 0 ? 1.0f : 0.0f), k1 = cS[1].next (color == 1 ? 1.0f : 0.0f), k2 = cS[2].next (color == 2 ? 1.0f : 0.0f);
                    v = k0 * cB + k1 * cD + k2 * cW;
                    // TONE a bilanciere intorno a 800 Hz (+-10 dB)
                    const float tl = tiltS.next (tilt);
                    const float lp = tiltLp.lp (v), hp = v - lp;
                    v = lp * std::pow (10.0f, -0.5f * tl) + hp * std::pow (10.0f, 0.5f * tl);
                    v *= lvlS.next (lvl);
                    // DSP
                    float dsp;
                    const int cur = fader.current;                 // l'effetto in uscita durante la dissolvenza
                    if (cur == 2)
                    {
                        const float semis = -5.0f + 9.0f * amtS.next (am);      // da -4a giusta a +3a maggiore, continuo
                        shifter.setRatio (std::pow (2.0f, semis / 12.0f));
                        dsp = shifter.tick (v);
                    }
                    else
                    {
                        float wl, wr;
                        core.tick (v, wl, wr);
                        dsp = v + 0.9f * (wl + wr);
                        if (cur == 3)
                        {
                            trem.setHz (sr, logMap (amtS.next (am), 1.0f, 10.0f));
                            dsp *= 1.0f - 0.9f * (0.5f + 0.5f * trem.sine());
                            trem.step();
                        }
                    }
                    const float m = mixS.next (mixT);
                    const float y = (1.0f - m) * v + m * dsp * fg;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }
        private:
            struct Fader { int current = -1, pending = -1; float g = 1, step = 0.003f;
                void prepare (double s) { step = (float) (1.0 / (0.02 * s)); }
                float tick (int w, bool& ch) { ch = false; if (current < 0) { current = w; ch = true; }
                    if (w != current) pending = w;
                    if (pending >= 0) { g -= step; if (g <= 0) { g = 0; current = pending; pending = -1; ch = true; } }
                    else if (g < 1) g = std::min (1.0f, g + step);
                    return g; } } fader;
            float logT = -100.0f;
            float sr = 48000;
            RoleParam pMix, pAmount, pLevel, pTone, pColor, pDirt, pMode;
            VerbCore core;
            GrainShifter shifter;
            OnePole inHp, tiltLp, colLp, colHp, warmLp, fuzzLp;
            Svf warm;
            Lfo trem;
            Smooth mixS, lvlS, amtS, tiltS, cS[3];
        };

        //==============================================================================
        /** 720 Stereo Looper: overdub, undo, reverse e mezza velocita'. */
        class Looper720 final : public Effect
        {
        public:
            enum State { Empty, Recording, Playing, Overdubbing, Stopped };
            enum Action { Pedal = 0, Stop = 1, Clear = 2, Undo = 3 };
            explicit Looper720 (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                maxSeconds = cfg.num ("maxs", 90);
                pLevel = role (*this, "level", 0.5f); pPlay = role (*this, "play", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                const size_t len = (size_t) (maxSeconds * sr);
                for (auto& b : buf) for (auto& c : b) c.assign (len, 0.0f);
                stamp.assign (len, 0);
                reset();
            }
            void reset() override { state = Empty; length = 0; rp = 0; wp = 0; cur = 0; copied = 0; hasUndo = false; gen = 1; }
            void trigger (int action) override { pending.store (action + 1); }
            float readout (int i) const override { return i == 0 ? (float) stateOut.load() : i == 1 ? posOut.load() : lenOut.load(); }
            void process (float* const* ch, int numCh, int n) override
            {
                if (const int a = pending.exchange (0); a > 0) handle (a - 1);
                const float lvl = taperA (pLevel.get (*this)) * 2.0f;
                const int pm = pPlay.step (*this);
                const double speed = (pm == 2 || pm == 3) ? 0.5 : 1.0;
                const bool rev = pm == 1 || pm == 3;
                const size_t cap = buf[0][0].size();
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const float in = ch[std::min (c, numCh - 1)][i];
                        float out = 0;
                        if (state == Recording && wp < cap) buf[cur][c][wp] = in;
                        else if (length > 0 && (state == Playing || state == Overdubbing))
                        {
                            const size_t k = (size_t) rp % length;
                            const size_t k1 = (k + (rev ? length - 1 : 1)) % length;
                            const float f = (float) (rp - std::floor (rp));
                            auto& loop = buf[cur][c];
                            out = loop[k] + (loop[k1] - loop[k]) * f;
                            if (state == Overdubbing)
                            {
                                backup (k);
                                loop[k] = loop[k] * 0.97f + in;
                            }
                            else if (hasUndo) backup (k);
                        }
                        if (c < numCh) ch[c][i] = in + out * lvl;
                    }
                    if (state == Recording) { if (++wp >= cap) { length = cap; rp = 0; state = Playing; } }
                    else if ((state == Playing || state == Overdubbing) && length > 0)
                    {
                        rp += rev ? -speed : speed;
                        if (rp < 0) rp += (double) length;
                        if (rp >= (double) length) rp -= (double) length;
                    }
                }
                stateOut.store ((int) state);
                posOut.store (length > 0 ? (float) (rp / (double) length) : 0.0f);
                lenOut.store ((float) ((state == Recording ? wp : length) / sr));
            }
        private:
            /** Copia nel buffer di annullamento il contenuto precedente della cella (una volta per sessione). */
            inline void backup (size_t k) noexcept
            {
                if (stamp[k] == gen) return;
                stamp[k] = gen;
                buf[cur ^ 1][0][k] = buf[cur][0][k];
                buf[cur ^ 1][1][k] = buf[cur][1][k];
                ++copied;
            }
            void startUndoSession()
            {
                if (++gen == 0) { std::fill (stamp.begin(), stamp.end(), 0); gen = 1; }   // raro: 65535 sessioni
                copied = 0; hasUndo = true;
            }
            void handle (int a)
            {
                switch (a)
                {
                    case Pedal:
                        if (state == Empty || (state == Stopped && length == 0)) { state = Recording; wp = 0; hasUndo = false; }
                        else if (state == Recording) { length = wp; rp = 0; state = Playing; }
                        else if (state == Playing) { startUndoSession(); state = Overdubbing; }
                        else if (state == Overdubbing) state = Playing;
                        else if (state == Stopped) { rp = 0; state = Playing; }
                        break;
                    case Stop: if (state != Empty) { state = length > 0 ? Stopped : Empty; rp = 0; } break;
                    case Clear: state = Empty; length = 0; rp = 0; hasUndo = false; break;
                    case Undo:
                        // annullamento possibile dopo un giro completo dall'inizio della sovraincisione
                        if (hasUndo && copied >= length) { cur ^= 1; hasUndo = false; if (state == Overdubbing) state = Playing; }
                        break;
                    default: break;
                }
            }
            Config cfg;
            double maxSeconds = 90, sr = 48000, rp = 0;
            RoleParam pLevel, pPlay;
            std::vector<float> buf[2][2];
            std::vector<uint16_t> stamp;
            uint16_t gen = 1;
            size_t length = 0, wp = 0, copied = 0;
            int cur = 0;
            State state = Empty;
            bool hasUndo = false;
            std::atomic<int> pending { 0 }, stateOut { 0 };
            std::atomic<float> posOut { 0 }, lenOut { 0 };
        };
    }

    std::unique_ptr<Effect> makeClassicTime (const ModelDef& d, const std::string& type)
    {
        if (type == "mmbbd")       return std::make_unique<MmBbdDelay> (d);
        if (type == "multidelay")  return std::make_unique<MultiDelay> (d);
        if (type == "sixteen")     return std::make_unique<SixteenSecond> (d);
        if (type == "classicverb") return std::make_unique<ClassicVerb> (d);
        if (type == "stain")       return std::make_unique<StainFx> (d);
        if (type == "loop720")     return std::make_unique<Looper720> (d);
        return nullptr;
    }
}
