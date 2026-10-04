/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Delay, riverberi, campionatore e inviluppi della tappa 3B:
      * b3memory    - delay a BBD con catena completa: Deluxe Memory Boy (LFO triangolo/quadra
                      dal DEPTH bipolare, filtro che segue il clock) e Memory Man Stereo
                      Echo/Chorus (uscite somma e differenza);
      * b3echo      - delay digitali compatti: Pico Canyon Echo (filtro passa-basso/passa-alto
                      nell'anello) e Pico ReRun (nastro: saturazione e tre livelli di flutter);
      * b3replay    - Instant Replay: campionatore a 8 bit compandati con riproduzione singola
                      (sugli attacchi) o ripetuta e PITCH;
      * b3verb      - riverberi sul motore VerbCore: Holy Grail Neo, Holier Grail (con gate),
                      Pico Oceans 3-Verb, Oceans Abyss (due motori in parallelo);
      * b3holiest   - Holiest Grail: FDN parametrico (decay, damping, diffusion) + molla e
                      pre-delay con feedback;
      * b3shimmer   - Pico Shimmer: ottave polifoniche + riverbero, shimmer, archi e glitch;
      * b3attackdecay - Attack Decay: inviluppi di volume mono o per nota (banco di filtri)
                      con fuzz Harmonix.
    Tutti gli anelli di retroazione hanno guadagno <= 1 e una limitazione morbida (loopSat);
    i riverberi "infiniti" sono serbatoi a t60 59 s con controllo automatico del livello
    d'ingresso (mai crescita illimitata).
*/

#include "FxClassicB.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        //==============================================================================
        /** Delay a BBD: Deluxe Memory Boy e Memory Man Stereo Echo/Chorus. */
        class B3Memory final : public Effect
        {
        public:
            enum Model { DeluxeBoy, StereoEcho };
            explicit B3Memory (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                model = cfg.str ("model", "dboy") == "mmstereo" ? StereoEcho : DeluxeBoy;
                stages = (int) cfg.num ("stages", 8192);
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? (float) dl[0] : 34.0f; dHi = dl.size() > 1 ? (float) dl[1] : 700.0f;
                filt = cfg.num ("filter", 4300); sat = (float) cfg.num ("sat", 0.75); fbMax = (float) cfg.num ("fbmax", 1.0);
                pTime = role (*this, "time", 0.5f); pFb = role (*this, "feedback", 0.35f); pBlend = role (*this, "blend", 0.45f);
                pGain = role (*this, "gain", 0.25f); pRate = role (*this, "rate", 0.3f); pDepth = role (*this, "depth", 0.5f);
                pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                line.prepare (s, stages, filt, 0.0003f, true, false, sat, 2.0e-5f, model == StereoEcho ? 20.0 : 20.0);
                fbLp.set (s, model == StereoEcho ? 3500 : 3000); fbHp.set (s, 70); sqLp.set (s, 40); triLp.set (s, 10);
                for (auto* sm : { &fbS, &bS, &gS, &depS }) sm->set (s, 20);
                dS.set (s, 80); rateS.set (s, 40); chS.set (s, 30);
                reset();
            }
            void reset() override { line.reset(); fbLp.reset(); fbHp.reset(); fbState = 0; lfo.ph = 0; first = true; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool chorus = model == StereoEcho && pMode.step (*this) == 1;
                float dT = logMap (pTime.get (*this), dLo, dHi);
                if (model == StereoEcho && chorus) dT = logMap (pTime.get (*this), 4.0f, 25.0f);   // CHORUS: stesso BBD a ritardi corti
                if (first) { dS.snap (dT); first = false; }
                const float fbT = (chorus ? 0.35f : fbMax) * pFb.get (*this);
                float gain = 1.0f, rateHz = 0.5f, swing = 0.0f; bool square = false;
                if (model == DeluxeBoy)
                {
                    gain = std::pow (10.0f, (-6.0f + 26.0f * pGain.get (*this)) / 20.0f);          // GAIN -6..+20 dB su dry e wet
                    rateHz = logMap (pRate.get (*this), 0.2f, 8.0f);
                    // DEPTH a scatto centrale: sinistra triangolo, destra quadra, centro spento
                    const float dk = (pDepth.get (*this) - 0.5f) * 2.0f;
                    square = dk > 0;
                    swing = std::max (0.0f, std::abs (dk) - 0.04f) / 0.96f * 0.08f;
                }
                else if (chorus) { rateHz = 0.8f; swing = 0.3f; }
                lfo.setHz (sr, rateHz);
                const float rail = model == DeluxeBoy ? 3.8f : 6.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    const float xin = rail * std::tanh (gS.next (gain) * x / rail);
                    const float l = square ? sqLp.lp (lfo.square()) : lfo.tri();
                    lfo.step();
                    const float dNow = dS.next (dT) * (1.0f + depS.next (swing) * (square ? l : triLp.lp (l)));
                    // il filtro segue il clock: sotto 0,45 x f_clock/2 i BBD lunghi diventano scuri come gli originali
                    const double fcl = (double) stages / (2.0 * dNow * 0.001);
                    line.setFilter (std::clamp (0.45 * 0.5 * fcl, 800.0, filt));
                    const float v = xin + loopSat (fbS.next (fbT) * fbState);
                    const float y = line.tick (v, dNow);
                    fbState = fbHp.hp (fbLp.lp (y));
                    const float b = bS.next (pBlend.get (*this));
                    if (model == StereoEcho && numCh > 1)
                    {
                        ch[0][i] = (1.0f - 0.5f * b) * xin + b * y;                // OUTPUT: dry + eco in fase
                        ch[1][i] = (1.0f - 0.5f * b) * xin - b * y;                // STEREO OUTPUT: in controfase
                    }
                    else
                    {
                        const float o = model == StereoEcho ? (1.0f - 0.5f * b) * xin + b * y : (1.0f - b) * xin + b * y;
                        for (int c = 0; c < numCh; ++c) ch[c][i] = o;
                    }
                }
            }
        private:
            Config cfg;
            Model model = DeluxeBoy;
            int stages = 8192;
            float sr = 48000, dLo = 34, dHi = 700, sat = 0.75f, fbMax = 1.0f, fbState = 0;
            double filt = 4300;
            bool first = true;
            RoleParam pTime, pFb, pBlend, pGain, pRate, pDepth, pMode;
            BbdChain line;
            OnePole fbLp, fbHp, sqLp, triLp;
            Lfo lfo;
            Smooth fbS, bS, gS, depS, dS, rateS, chS;
        };

        //==============================================================================
        /** Delay digitali compatti: Pico Canyon Echo e Pico ReRun. */
        class B3Echo final : public Effect
        {
        public:
            explicit B3Echo (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                tape = cfg.str ("model", "canyon") == "rerun";
                pBlend = role (*this, "blend", 0.4f); pTime = role (*this, "time", 0.45f); pFilter = role (*this, "filter", 0.5f);
                pFb = role (*this, "feedback", 0.35f); pSat = role (*this, "sat", 0.4f); pFlutter = role (*this, "flutter", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                line.allocate ((int) (s * 3.1));
                tS.set (s, 150); fbS.set (s, 20); bS.set (s, 20); fS.set (s, 30); satS.set (s, 30); flS.set (s, 50);
                wow.setHz (s, 0.55); flut.setHz (s, 6.8);
                hp.set (s, 70);
                reset();
            }
            void reset() override { line.clear(); lp.reset(); hp.reset(); hpF.reset(); fb = 0; first = true; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float tMs = logMap (pTime.get (*this), 8.0f, 3000.0f);
                if (first) { tS.snap (tMs); first = false; }
                const float fk = pFb.get (*this);
                const float fbT = fk > 0.985f ? 1.0f : fk * 0.98f;                        // al massimo: ripetizioni infinite (limitate in morbido)
                static const float flutLv[3] = { 0.25f, 0.6f, 1.2f };
                const float flT = tape ? flutLv[std::clamp (pFlutter.step (*this), 0, 2)] : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    const float d = tS.next (tMs) * 0.001f * sr;
                    float dm = d;
                    if (tape)
                    {
                        const float fa = flS.next (flT);
                        dm = d * (1.0f + fa * (0.0022f * wow.sine() + 0.0007f * flut.sine())) + fa * 0.0004f * sr * flut.sine (0.3);
                        wow.step(); flut.step();
                    }
                    float y = line.read (std::max (2.0f, dm));
                    float v = y;
                    if (tape)
                    {
                        // nastro: saturazione sulle ripetizioni, banda che si restringe con il SAT
                        const float sk = satS.next (pSat.get (*this));
                        const float L = 1.2f - 0.95f * sk;              // livello di saturazione 1,2 V .. 0,25 V
                        v = L * std::tanh (v / L);                       // guadagno unitario ai bassi livelli: anello sempre <= 1
                        lp.setFast (sr, 7000.0f - 3500.0f * sk); v = lp.lp (v);
                        v = hp.hp (v);
                    }
                    else
                    {
                        // FILTER: centro spento, sinistra passa-basso, destra passa-alto (nell'anello)
                        const float fk2 = fS.next (pFilter.get (*this));
                        if (fk2 < 0.47f) { lp.setFast (sr, logMap (fk2 / 0.47f, 500.0f, 18000.0f)); v = lp.lp (v); }
                        else if (fk2 > 0.53f) { hpF.setFast (sr, logMap ((fk2 - 0.53f) / 0.47f, 20.0f, 2500.0f)); v = hpF.hp (v); }
                        else { lp.lp (v); hpF.hp (v); }
                        y = v;
                    }
                    fb = v;
                    line.push (x + fbS.next (fbT) * loopSat (fb));
                    const float b = bS.next (pBlend.get (*this));
                    const float wet = tape ? v : y;
                    const float o = (b < 0.5f ? 1.0f : 2.0f * (1.0f - b)) * x + (b < 0.5f ? 2.0f * b : 1.0f) * wet;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = o;
                }
            }
        private:
            Config cfg;
            bool tape = false, first = true;
            float sr = 48000, fb = 0;
            RoleParam pBlend, pTime, pFilter, pFb, pSat, pFlutter;
            DelayLine line;
            OnePole lp, hp, hpF;
            Lfo wow, flut;
            Smooth tS, fbS, bS, fS, satS, flS;
        };

        //==============================================================================
        /** Instant Replay: campionatore con riproduzione singola o ripetuta. */
        class B3Replay final : public Effect
        {
        public:
            enum State { Empty = 0, Recording = 1, Playing = 2, Ready = 4 };
            explicit B3Replay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                maxS = (float) cfg.num ("maxs", 2.0); fsMem = (float) cfg.num ("fs", 16000);
                pPitch = role (*this, "pitch", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                mem.assign ((size_t) (maxS * fsMem) + 4, 0);
                aaIn.set (s, 0.42 * fsMem, 0.707); aaOut.set (s, 0.42 * fsMem, 0.707); aaIn2.set (s, 0.42 * fsMem, 1.3); aaOut2.set (s, 0.42 * fsMem, 1.3);
                onset.prepare (s, 0.01f, 0.12);
                pS.set (s, 30);
                reset();
            }
            void reset() override { state = Empty; len = 0; wpos = 0; rpos = 0; ph = 0; playing = false; aaIn.reset(); aaOut.reset(); aaIn2.reset(); aaOut2.reset(); }
            void trigger (int action) override { pending.store (action + 1); }
            float readout (int i) const override { return i == 0 ? (float) stateOut.load() : i == 1 ? posOut.load() : lenOut.load(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                if (const int a = pending.exchange (0); a > 0)
                {
                    if (a - 1 == 2) { state = Empty; len = 0; playing = false; }                 // CLEAR
                    else if (state == Recording) { len = wpos; state = len > 64 ? Ready : Empty; rpos = 0; playing = false; }
                    else { state = Recording; wpos = 0; ph = 0; playing = false; }               // RECORD: nuova registrazione
                }
                const bool repeat = pMode.step (*this) == 1;
                const float rate = std::exp2 ((pPitch.get (*this) - 0.5f) * 2.0f);             // PITCH: -1..+1 ottava
                const float step = fsMem / sr;
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    float out = 0;
                    if (state == Recording)
                    {
                        const float a = aaIn2.lp (aaIn.lp (x));
                        ph += step;
                        while (ph >= 1.0f)
                        {
                            ph -= 1.0f;
                            if (wpos < mem.size() - 2) mem[wpos++] = encode (a);
                            else { len = wpos; state = Ready; rpos = 0; break; }
                        }
                    }
                    else if (state == Ready || state == Playing)
                    {
                        const bool hit = onset.tick (x);
                        if (repeat) playing = true;
                        else if (hit) { playing = true; rpos = 0; fade = 0; }                  // SINGLE SHOT: riparte sull'attacco
                        if (playing && len > 1)
                        {
                            const size_t k = (size_t) rpos;
                            const float f = (float) (rpos - (double) k);
                            const float a = decode (mem[std::min (k, len - 1)]), b = decode (mem[std::min (k + 1, len - 1)]);
                            fade = std::min (1.0f, fade + 0.01f);
                            const float edge = std::min (1.0f, (float) ((double) len - rpos) * 0.01f);
                            out = (a + (b - a) * f) * fade * edge;
                            rpos += (double) (pS.next (rate) * step);
                            if (rpos >= (double) len) { rpos -= (double) len; if (! repeat) playing = false; else fade = 1.0f; }
                        }
                        state = playing ? Playing : Ready;
                    }
                    out = aaOut2.lp (aaOut.lp (out));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = x + out;
                }
                stateOut.store ((int) state);
                posOut.store (len > 0 ? (float) (rpos / (double) len) : 0.0f);
                lenOut.store ((float) ((state == Recording ? wpos : len) / fsMem));
            }
        private:
            /** 8 bit con compressione mu-law (stima della conversione dell'originale). */
            static inline uint8_t encode (float x) noexcept
            {
                const float v = std::clamp (x / 1.2f, -1.0f, 1.0f);
                const float m = std::copysign (std::log1p (255.0f * std::abs (v)) / std::log1p (255.0f), v);
                return (uint8_t) std::clamp ((int) std::lround (m * 127.0f) + 128, 0, 255);
            }
            static inline float decode (uint8_t q) noexcept
            {
                const float m = ((float) q - 128.0f) / 127.0f;
                return 1.2f * std::copysign ((std::pow (256.0f, std::abs (m)) - 1.0f) / 255.0f, m);
            }
            Config cfg;
            float sr = 48000, maxS = 2, fsMem = 16000, ph = 0, fade = 1;
            std::vector<uint8_t> mem;
            size_t len = 0, wpos = 0;
            double rpos = 0;
            bool playing = false;
            State state = Empty;
            RoleParam pPitch, pMode;
            Svf aaIn, aaOut, aaIn2, aaOut2;
            OnsetB onset;
            Smooth pS;
            std::atomic<int> pending { 0 }, stateOut { 0 };
            std::atomic<float> posOut { 0 }, lenOut { 0 };
        };

        //==============================================================================
        /** Controllo automatico del livello d'ingresso dei serbatoi quasi infiniti. */
        struct TankAgc
        {
            float lvl = 0, a = 0.999f;
            void prepare (double sr) { a = (float) std::exp (-1.0 / (0.15 * sr)); }
            /** Guadagno d'ingresso dato il livello d'uscita del serbatoio; on = serbatoio infinito. */
            inline float tick (float outAbs, bool on) noexcept
            {
                lvl = outAbs + a * (lvl - outAbs);
                return on ? 1.0f / (1.0f + std::max (0.0f, lvl - 0.25f) * 6.0f) : 1.0f;
            }
        };

        /** Riverberi sul motore VerbCore. */
        class B3Verb final : public Effect
        {
        public:
            enum Model { HolyNeo, Holier, ThreeVerb, Abyss };
            explicit B3Verb (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "neo");
                model = m == "holier" ? Holier : m == "3verb" ? ThreeVerb : m == "abyss" ? Abyss : HolyNeo;
                pBlend = role (*this, "blend", 0.4f); pMode = role (*this, "mode", 0.0f); pLength = role (*this, "length", 0.0f);
                pThr = role (*this, "gatethr", 0.3f); pGate = role (*this, "gate", 0.0f); pSpeed = role (*this, "gatespeed", 0.0f);
                pRev = role (*this, "gaterev", 0.0f); pTime = role (*this, "time", 0.5f); pPre = role (*this, "predelay", 0.0f);
                pTone = role (*this, "tone", 0.5f);
                static const char* ab[2][6] = { { "amix", "apre", "atime", "alow", "ahigh", "atype" }, { "bmix", "bpre", "btime", "blow", "bhigh", "btype" } };
                for (int e = 0; e < 2; ++e)
                {
                    pMix[e] = role (*this, ab[e][0], e == 0 ? 0.4f : 0.0f); pPreE[e] = role (*this, ab[e][1], 0.0f);
                    pTimeE[e] = role (*this, ab[e][2], 0.5f); pLow[e] = role (*this, ab[e][3], 0.5f); pHigh[e] = role (*this, ab[e][4], 0.5f);
                    pType[e] = role (*this, ab[e][5], 0.0f);
                }
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                for (int e = 0; e < (model == Abyss ? 2 : 1); ++e) core[e].prepare (s);
                for (int e = 0; e < 2; ++e)
                {
                    eq[e * 4 + 0].prepare (s, SmoothEq::LowShelf, 250, 0.7); eq[e * 4 + 1].prepare (s, SmoothEq::LowShelf, 250, 0.7);
                    eq[e * 4 + 2].prepare (s, SmoothEq::HighShelf, 3500, 0.7); eq[e * 4 + 3].prepare (s, SmoothEq::HighShelf, 3500, 0.7);
                }
                for (auto& f : fader) f.prepare (s, 15);
                for (auto& a : agc) a.prepare (s);
                for (auto* sm : { &wS, &dS, &gS }) sm->set (s, 20);
                for (auto& sm : mixS) sm.set (s, 20);
                env.set (s, 2, 60);
                for (int e = 0; e < 2; ++e) { preDl[e].allocate ((int) (1.05 * s)); preS[e].set (s, 150); }
                awS.set (s, 15); adS.set (s, 15);
                reset();
            }
            void reset() override
            {
                for (int e = 0; e < (model == Abyss ? 2 : 1); ++e) core[e].clear();
                for (auto& f : eq) f.reset();
                for (auto& d : preDl) d.clear();
                gate = 0; logT[0] = logT[1] = -100; toneL[0] = toneL[1] = -1;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int nCore = model == Abyss ? 2 : 1;
                VerbCore::Algo algo[2] = { VerbCore::Hall, VerbCore::Hall };
                float t60[2] = { 2.5f, 2.5f }, tone[2] = { 7000, 7000 }, pre[2] = { 0, 0 }, pa[2] = { 0.5f, 0.5f }, pb[2] = { 0.5f, 0.5f };
                int var[2] = { 0, 0 };
                bool inf[2] = { false, false };
                float wet = 0.5f, dry = 1.0f;
                const float bl = pBlend.get (*this);
                switch (model)
                {
                    case HolyNeo:
                    {
                        static const VerbCore::Algo al[3] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Plate_ };
                        algo[0] = al[std::clamp (pMode.step (*this), 0, 2)];
                        t60[0] = algo[0] == VerbCore::GSpring ? 2.2f : algo[0] == VerbCore::Hall ? 3.5f : 3.0f;
                        tone[0] = algo[0] == VerbCore::GSpring ? 4200.0f : 7000.0f;
                        wet = 1.3f * std::sin (bl * 1.5707963f); dry = std::cos (bl * 1.5707963f);     // volume costante
                        break;
                    }
                    case Holier:
                    {
                        static const VerbCore::Algo al[4] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Flerb, VerbCore::Room };
                        const int sel = std::clamp (pMode.step (*this), 0, 3);
                        algo[0] = al[sel];
                        const bool shortL = pLength.step (*this) == 1;
                        static const float lt[4][2] = { { 2.8f, 1.4f }, { 4.5f, 1.8f }, { 3.0f, 1.5f }, { 1.2f, 0.5f } };
                        t60[0] = lt[sel][shortL ? 1 : 0];
                        tone[0] = sel == 0 ? 4200.0f : 6500.0f; pa[0] = 0.1f;
                        wet = 1.3f * bl; dry = 1.0f - 0.5f * bl;
                        break;
                    }
                    case ThreeVerb:
                    {
                        static const VerbCore::Algo al[3] = { VerbCore::Hall, VerbCore::Plate_, VerbCore::Spring6G15 };
                        const int sel = std::clamp (pMode.step (*this), 0, 2);
                        algo[0] = al[sel];
                        const float tk = pTime.get (*this), pk = pPre.get (*this);
                        tone[0] = logMap (pTone.get (*this), 1500.0f, 14000.0f);
                        if (sel == 2)
                        {
                            // DELAY/SPRING: lunghezza della molla (corta / media / lunga), TIME = decadimento
                            const int len = std::clamp ((int) (pk * 2.99f), 0, 2);
                            static const float base[3] = { 1.2f, 2.2f, 3.5f };
                            t60[0] = base[len] * logMap (tk, 0.5f, 2.0f);
                            pa[0] = 0.25f + 0.25f * (float) len;
                            tone[0] = std::min (tone[0], 5000.0f);
                        }
                        else
                        {
                            inf[0] = tk > 0.97f;
                            t60[0] = inf[0] ? 59.0f : logMap (tk / 0.97f, 0.4f, 20.0f);
                            pre[0] = pk * pk;                                                  // fino a 1 s
                        }
                        wet = 1.4f * std::pow (bl, 1.2f); dry = bl < 0.5f ? 1.0f : 2.0f * (1.0f - bl);
                        break;
                    }
                    case Abyss:
                    {
                        static const VerbCore::Algo al[10] = { VerbCore::Room, VerbCore::Hall, VerbCore::Spring6G15, VerbCore::Plate_, VerbCore::Reverse,
                                                               VerbCore::Dyna, VerbCore::AutoInf, VerbCore::Shim, VerbCore::Poly, VerbCore::Resonant };
                        for (int e = 0; e < 2; ++e)
                        {
                            algo[e] = al[std::clamp (pType[e].step (*this), 0, 9)];
                            const float tk = pTimeE[e].get (*this);
                            inf[e] = tk > 0.97f && algo[e] != VerbCore::Reverse && algo[e] != VerbCore::AutoInf;
                            t60[e] = inf[e] ? 59.0f : logMap (tk / 0.97f, 0.3f, 18.0f);
                            if (algo[e] == VerbCore::Reverse) { pa[e] = logMap (tk, 0.1f, 1.5f); t60[e] = 2.0f; }
                            if (algo[e] == VerbCore::Spring6G15) { pa[e] = 0.4f; t60[e] = logMap (tk, 0.8f, 5.0f); }
                            if (algo[e] == VerbCore::AutoInf) pa[e] = tk;
                            if (algo[e] == VerbCore::Resonant) { pa[e] = 0.5f; pb[e] = 0.6f; }
                            const float pk = pPreE[e].get (*this); pre[e] = pk * pk;
                            tone[e] = 9000.0f;
                            lowDb[e] = (pLow[e].get (*this) - 0.5f) * 24.0f; highDb[e] = (pHigh[e].get (*this) - 0.5f) * 24.0f;
                        }
                        dry = 1.0f;
                        break;
                    }
                }
                for (int e = 0; e < nCore; ++e)
                {
                    const float lt = std::log (std::max (0.05f, t60[e]));
                    logT[e] = logT[e] < -50.0f ? lt : lt + 0.82f * (logT[e] - lt);
                    t60[e] = std::exp (logT[e]);
                    // tono lisciato a blocchi (scala logaritmica); il pre-delay e' una linea esterna a ritardo lisciato
                    const float tl = std::log (tone[e]);
                    toneL[e] = toneL[e] < 0.0f ? tl : tl + 0.7f * (toneL[e] - tl);
                    tone[e] = std::exp (toneL[e]);
                    preL[e] = pre[e]; pre[e] = 0.0f;
                }
                // gate (Holier Grail)
                const int gm = model == Holier ? pGate.step (*this) : 0;                       // 0 OFF, 1 solo riverbero, 2 riverbero + dry
                const float thr = 0.0005f * std::pow (200.0f, pThr.get (*this));
                const bool gRev = pRev.step (*this) == 1;
                const float gRel = (float) std::exp (-1.0 / ((pSpeed.step (*this) == 1 ? 0.06 : 0.4) * sr));
                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    float wl = 0, wr = 0;
                    for (int e = 0; e < nCore; ++e)
                    {
                        bool changed;
                        const int key = (int) algo[e] * 4 + var[e];
                        const float fg = fader[e].tick (key, changed);
                        if (changed) core[e].clear();
                        if ((i == 0 && fader[e].current == key) || changed)
                            if (fader[e].current == key) core[e].set (algo[e], t60[e], tone[e], pre[e], 0.0f, pa[e], pb[e], var[e]);
                        const float gIn = agc[e].tick (std::abs (lastOut[e]), inf[e]);
                        float l, r;
                        const float pd = preS[e].next (preL[e] * sr);
                        float xin = x * gIn;
                        if (preL[e] > 0.0005f || pd > 1.0f) { preDl[e].push (xin); xin = preDl[e].read (std::max (1.0f, pd)); }
                        else preDl[e].push (xin);
                        core[e].tick (xin, l, r);
                        lastOut[e] = 0.5f * (l + r);
                        if (model == Abyss)
                        {
                            l = eq[e * 4 + 2].tick (eq[e * 4 + 0].tick (l, lowDb[e]), highDb[e]); r = eq[e * 4 + 3].tick (eq[e * 4 + 1].tick (r, lowDb[e]), highDb[e]);
                            const float m = mixS[e].next (1.4f * std::pow (pMix[e].get (*this), 1.2f));
                            l *= m; r *= m;
                        }
                        wl += l * fg; wr += r * fg;
                    }
                    float gd = 1.0f, gw = 1.0f;
                    const float aw = awS.next (gm > 0 ? 1.0f : 0.0f), ad = adS.next (gm == 2 ? 1.0f : 0.0f);
                    if (model == Holier)
                    {
                        const float e = env.tick (x);
                        const bool open = gRev ? e < thr : e > thr;                           // REVERSE: si apre quando si smette di suonare
                        const float tgt = open ? 1.0f : 0.0f;
                        gate = tgt > gate ? tgt + 0.99f * (gate - tgt) : tgt + gRel * (gate - tgt);
                        gw = 1.0f - aw * (1.0f - gate); gd = 1.0f - ad * (1.0f - gate);      // OFF / solo riverbero / riverbero + dry, senza salti
                    }
                    const float w = (model == Abyss ? 1.0f : wS.next (wet)), dg = dS.next (dry) * gd;
                    if (numCh > 1) { ch[0][i] = outRail (dg * inL + w * gw * wl); ch[1][i] = outRail (dg * inR + w * gw * wr); }
                    else ch[0][i] = outRail (dg * inL + w * gw * 0.5f * (wl + wr));
                }
            }
        private:
            Config cfg;
            Model model = HolyNeo;
            float sr = 48000, gate = 0, logT[2] { -100, -100 }, lastOut[2] {}, lowDb[2] {}, highDb[2] {}, toneL[2] { -1, -1 }, preL[2] { -1, -1 };
            RoleParam pBlend, pMode, pLength, pThr, pGate, pSpeed, pRev, pTime, pPre, pTone, pMix[2], pPreE[2], pTimeE[2], pLow[2], pHigh[2], pType[2];
            VerbCore core[2];
            XFader fader[2];
            TankAgc agc[2];
            SmoothEq eq[8];
            Envelope env;
            DelayLine preDl[2];
            Smooth wS, dS, gS, mixS[2], preS[2], awS, adS;
        };

        //==============================================================================
        /** Holiest Grail: riverbero parametrico + molla + pre-delay con feedback. */
        class B3Holiest final : public Effect
        {
        public:
            explicit B3Holiest (const ModelDef& d) : Effect (d)
            {
                pIn = role (*this, "gain", 0.5f); pPre = role (*this, "predelay", 0.1f); pPreFb = role (*this, "prefb", 0.0f);
                pDecay = role (*this, "decay", 0.5f); pDamp = role (*this, "damping", 0.4f); pDiff = role (*this, "diffusion", 0.6f);
                pDry = role (*this, "direct", 0.8f); pSpring = role (*this, "spring", 0.0f); pRev = role (*this, "level", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                fdn.prepare (s, 2.2f); spring.prepare (s); pre.allocate ((int) (0.46 * s));
                for (auto* sm : { &inS, &pdS, &pfS, &dryS, &spS, &rvS }) sm->set (s, 30);
                pdS.set (s, 120);
                reset();
            }
            void reset() override { fdn.clear(); spring.clear(); pre.clear(); preFb = 0; logT = -100; lastP = -1; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float dk = pDecay.get (*this);
                const float lt = std::log (logMap (dk, 0.3f, 15.0f));
                logT = logT < -50.0f ? lt : lt + 0.82f * (logT - lt);
                const float damp = logMap (1.0f - pDamp.get (*this), 1500.0f, 14000.0f);
                const float diff = 0.3f + 0.45f * pDiff.get (*this);
                const float pkey = std::exp (logT) + damp * 1e-3f + diff;
                if (std::abs (pkey - lastP) > 1e-4f) { lastP = pkey; fdn.set (std::exp (logT), damp, 1.6f, 4.0f, diff); spring.set (0.8f, 4200.0f, 2, -0.6f); }
                // PRE-DELAY: 0-100 ms nella prima meta' della corsa, 100-440 ms nella seconda
                const float pk = pPre.get (*this);
                const float preMs = pk < 0.5f ? 200.0f * pk : 100.0f + 680.0f * (pk - 0.5f);
                const float gIn = logMap (pIn.get (*this), 0.25f, 4.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    const float a = 1.5f * softSat (inS.next (gIn) * x / 1.5f);              // convertitore A/D
                    pre.push (a + loopSat (0.9f * pfS.next (pPreFb.get (*this)) * preFb));
                    preFb = pre.read (std::max (1.0f, pdS.next (preMs) * 0.001f * sr));
                    float l, r, sl, srr;
                    fdn.tick (preFb, l, r);
                    spring.tick (preFb * 1.6f, sl, srr);
                    const float sp = spS.next (pSpring.get (*this)), rv = rvS.next (1.4f * pRev.get (*this)), dg = dryS.next (2.0f * taperA (pDry.get (*this)) / 0.9f);
                    const float oL = dg * x + (rv * l + sp * sl) / std::max (0.25f, inS.y);
                    const float oR = dg * x + (rv * r + sp * srr) / std::max (0.25f, inS.y);
                    if (numCh > 1) { ch[0][i] = outRail (oL); ch[1][i] = outRail (oR); }
                    else ch[0][i] = outRail (0.5f * (oL + oR));
                }
            }
        private:
            float sr = 48000, preFb = 0, logT = -100, lastP = -1;
            RoleParam pIn, pPre, pPreFb, pDecay, pDamp, pDiff, pDry, pSpring, pRev;
            Fdn fdn;
            SpringTank spring;
            DelayLine pre;
            Smooth inS, pdS, pfS, dryS, spS, rvS;
        };

        //==============================================================================
        /** Pico Shimmer: ottave polifoniche (POG) nel riverbero, archi sintetizzati e glitch delay. */
        class B3Shimmer final : public Effect
        {
        public:
            explicit B3Shimmer (const ModelDef& d) : Effect (d)
            {
                pScene = role (*this, "mode", 0.0f); pBlend = role (*this, "blend", 0.5f); pTime = role (*this, "time", 0.6f);
                pVoice = role (*this, "voice", 0.4f); pTone = role (*this, "tone", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 1);
                fdn.prepare (s, 2.0f);
                for (auto& g : sh) g.prepare (s, 60);
                glitch.allocate ((int) (1.1 * s));
                mod.allocate ((int) (0.05 * s));
                onset.prepare (s, 0.004f);
                agc.prepare (s);
                fader.prepare (s, 20);
                for (auto* sm : { &bS, &vS, &tS, &swS }) sm->set (s, 30);
                ens.allocate ((int) (0.03 * s));
                reset();
            }
            void reset() override
            {
                bank.reset(); fdn.clear(); for (auto& g : sh) g.clear(); glitch.clear(); mod.clear(); ens.clear();
                fb = 0; swell = 0; gPos = 0; gLen = 0; gAge = 0; logT = -100; lastT = -1; lastOut = 0; toneLp.reset(); fbLp.reset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int scene = std::clamp (pScene.step (*this), 0, 2);
                const float tk = pTime.get (*this);
                const bool inf = tk > 0.97f;
                const float lt = std::log (inf ? 59.0f : logMap (tk / 0.97f, 1.0f, 20.0f));
                logT = logT < -50.0f ? lt : lt + 0.82f * (logT - lt);
                const float toneHz = logMap (pTone.get (*this), 1500.0f, 14000.0f);
                if (std::abs (std::exp (logT) + toneHz * 1e-4f - lastT) > 1e-3f) { lastT = std::exp (logT) + toneHz * 1e-4f; fdn.set (std::exp (logT), toneHz, 1.7f, 8.0f, 0.7f); }
                fbLp.setFast (sr, std::min (8000.0f, toneHz));
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (scene, changed);
                    if (changed) { for (auto& g : sh) g.clear(); glitch.clear(); }
                    const float x = monoIn (ch, numCh, i);
                    bank.push (x);
                    const float vk = vS.next (pVoice.get (*this));
                    // VOICE: 0 plate pulito .. 0,5 shimmer classico .. 1 archi
                    const float shimAmt = vk < 0.5f ? vk * 2.0f : 1.0f - (vk - 0.5f) * 0.8f;
                    const float strAmt = std::max (0.0f, vk - 0.5f) * 2.0f;
                    // archi: parziali 1..4 dal banco (dente di sega) con attacco lento e ensemble
                    if (onset.tick (x)) swell = 0;
                    swell = std::min (1.0f, swell + 1.0f / (0.35f * sr));
                    float strings = 0;
                    if (strAmt > 0.001f)
                    {
                        static const int nums[4] = { 1, 2, 3, 4 }, levs[4] = { 0, 0, 0, 0 };
                        static const float gains[4] = { 0.7f, 0.35f, 0.23f, 0.17f };
                        strings = bank.voices (nums, levs, gains, 4) * kBankGain * swell * swell;
                        ens.push (strings);
                        ensPh += 0.6f / sr; ensPh -= std::floor (ensPh);
                        strings = 0.5f * strings + 0.5f * ens.read ((0.008f + 0.003f * std::sin (6.2831853f * ensPh)) * sr);
                    }
                    // ingresso del serbatoio per scena
                    float in = x;
                    if (fader.current == 2)
                    {
                        // ETHERDUST: frammenti ripetuti di lunghezza casuale (glitch delay) prima del riverbero
                        glitch.push (x);
                        if (gAge <= 0) { gLen = (0.06f + 0.25f * rnd()) * sr; gPos = (0.05f + 0.6f * rnd()) * sr; gAge = (int) (gLen * (2 + (int) (rnd() * 3))); }
                        const float ph = std::fmod ((float) gAge, gLen);
                        const float w = std::sin (pi * ph / gLen);
                        in = 0.5f * x + 0.8f * w * w * glitch.read (gPos + gLen - ph);
                        --gAge;
                    }
                    const float g = agc.tick (std::abs (lastOut), inf);
                    // shimmer: ottava sopra (e sotto in OFF-WORLD) nell'anello, guadagno d'anello <= 0,55
                    sh[0].setRatio (2.0f); sh[1].setRatio (0.5f);
                    float shim = sh[0].tick (fb);
                    if (fader.current == 1) { mod.push (fb); modPh += 0.4f / sr; modPh -= std::floor (modPh); shim = 0.6f * shim + 0.5f * sh[1].tick (mod.read ((0.01f + 0.004f * std::sin (6.2831853f * modPh)) * sr)); }
                    const float loopIn = in + 0.55f * shimAmt * loopSat (fbLp.lp (shim)) + strAmt * strings;
                    float l, r;
                    fdn.tick (loopIn * g * fg, l, r);
                    fb = 0.5f * (l + r);
                    lastOut = fb;
                    const float b = bS.next (pBlend.get (*this));
                    const float wet = 1.4f * b, dry = b < 0.5f ? 1.0f : 2.0f * (1.0f - b);
                    if (numCh > 1) { ch[0][i] = outRail (dry * ch[0][i] + wet * 1.5f * softSat (l / 1.5f)); ch[1][i] = outRail (dry * ch[1][i] + wet * 1.5f * softSat (r / 1.5f)); }
                    else ch[0][i] = outRail (dry * x + wet * 1.5f * softSat (fb / 1.5f));
                }
            }
        private:
            inline float rnd() noexcept { return 0.5f + 0.5f * noise.uni(); }
            float sr = 48000, fb = 0, swell = 0, gPos = 0, gLen = 1, logT = -100, lastT = -1, lastOut = 0, ensPh = 0, modPh = 0;
            int gAge = 0;
            RoleParam pScene, pBlend, pTime, pVoice, pTone;
            SpectralBank bank;
            Fdn fdn;
            GrainShifter sh[2];
            DelayLine glitch, mod, ens;
            OnsetB onset;
            TankAgc agc;
            XFader fader;
            Noise noise;
            OnePole toneLp, fbLp;
            Smooth bS, vS, tS, swS;
        };

        //==============================================================================
        /** Attack Decay: inviluppi di volume (mono o per nota) con fuzz Harmonix. */
        class B3AttackDecay final : public Effect
        {
        public:
            explicit B3AttackDecay (const ModelDef& d) : Effect (d)
            {
                pPoly = role (*this, "poly", 0.0f); pHarm = role (*this, "drive", 0.4f); pTone = role (*this, "tone", 0.5f);
                pHVol = role (*this, "hlevel", 0.3f); pSens = role (*this, "sens", 0.5f); pAttack = role (*this, "attack", 0.4f);
                pBlend = role (*this, "blend", 1.0f); pDecay = role (*this, "decay", 0.7f); pVol = role (*this, "level", 0.5f);
                pH = role (*this, "fuzz", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 50.0f, 6000.0f, 4, 0.14f, 0);
                det.set (s, 1, 30); slow.set (s, 50, 400);
                for (auto* sm : { &hS, &bS, &vS, &hvS, &polyS }) sm->set (s, 20);
                fuzzHp.set (s, 80); fuzzLp.set (s, 4000);
                reset();
            }
            void reset() override
            {
                bank.reset(); env = 0; gcur = 0; stage = 0; refr = 0;
                for (auto& b : bs) b = {};
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float ak = pAttack.get (*this), dk = pDecay.get (*this);
                const float atkS = ak < 0.01f ? 0.0f : logMap (ak, 0.004f, 8.0f);              // ATTACK al minimo: niente attacco
                const float decS = logMap (dk, 0.004f, 8.0f);
                const float atkStep = atkS <= 0.0f ? 1.0f : 1.0f / (atkS * sr);
                const float decC = (float) std::exp (-6.9 / (decS * sr));                          // -60 dB in DECAY
                const float dropStep = 1.0f / (0.002f * sr);
                const float thr = 0.002f + 0.1f * (1.0f - pSens.get (*this)) * (1.0f - pSens.get (*this));
                const bool poly = pPoly.step (*this) == 1;
                const float hk = pHarm.get (*this);
                const float hGain = 2.0f * std::pow (60.0f, hk);
                fuzzLp.setFast (sr, logMap (pTone.get (*this), 800.0f, 8000.0f));
                const float hOn = pH.step (*this) == 1 ? 1.0f : 0.0f;
                const float hVol = 2.0f * taperA (pHVol.get (*this));
                const float vol = 2.0f * taperA (pVol.get (*this)) / 0.3f;
                const int nb = bank.numBands();
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    // fuzz Harmonix (prima degli inviluppi): guadagno, diodi asimmetrici, tono
                    const float z = hGain * fuzzHp.hp (x);
                    float fz = z > 0 ? std::tanh (z) : std::tanh (0.7f * z) / 0.7f * 0.8f;
                    fz = fuzzLp.lp (fz) * 0.35f;
                    const float h = hS.next (hOn);
                    const float src = (1.0f - h) * x + h * fz * hvS.next (hVol);
                    float y;
                    const float pm = polyS.next (poly ? 1.0f : 0.0f);
                    float yMono = 0, yPoly = 0;
                    {
                        // inviluppo mono: attacco lineare, decadimento esponenziale, riparte a ogni nota
                        const float f = det.tick (x), s = slow.tick (x);
                        if (refr > 0) --refr;
                        if (refr == 0 && f > thr && f > 1.6f * s + 0.0008f) { stage = 3; refr = (int) (0.06f * sr); }
                        // guadagno: attacco 'a nastro' (curva quadratica), decadimento esponenziale, ripartenza con discesa in 2 ms
                        if (stage == 3) { gcur -= dropStep; if (gcur <= 0.0f) { gcur = 0.0f; env = 0.0f; stage = 1; } }
                        else if (stage == 1) { env += atkStep; if (env >= 1.0f) { env = 1.0f; stage = 2; } gcur = env * env; }
                        else if (stage == 2) { env *= decC; gcur = env; }
                        yMono = src * gcur;
                    }
                    if (pm > 0.001f)
                    {
                        // POLY: ogni banda del banco ha il proprio inviluppo (come l'HOG2)
                        bank.push (src);
                        for (int b = 0; b < nb; ++b)
                        {
                            const auto B = bank.band (b);
                            auto& S = bs[b];
                            const float a = B.active ? B.w * B.ampC : 0.0f;
                            const bool on = a > 1.5f * S.slow + 0.002f && S.refr <= 0 && a > thr * 0.5f;
                            S.slow += (a - S.slow) * 0.0015f;
                            if (S.refr > 0) --S.refr;
                            if (on) { S.stage = 3; S.refr = (int) (0.06f * sr); }
                            if (S.stage == 3) { S.g -= dropStep; if (S.g <= 0.0f) { S.g = 0.0f; S.env = 0.0f; S.stage = 1; } }
                            else if (S.stage == 1) { S.env += atkStep; if (S.env >= 1.0f) { S.env = 1.0f; S.stage = 2; } S.g = S.env * S.env; }
                            else if (S.stage == 2) { S.env *= decC; S.g = S.env; }
                            yPoly += a * B.root[0].r * S.g;
                        }
                        yPoly *= kBankGain;
                    }
                    y = (1.0f - pm) * yMono + pm * yPoly;
                    const float b = bS.next (pBlend.get (*this));
                    const float o = outRail (vS.next (vol) * ((1.0f - b) * x + b * y));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = o;
                }
            }
        private:
            struct BandEnv { float env = 0, g = 0, slow = 0; int stage = 0, refr = 0; };
            float sr = 48000, env = 0, gcur = 0;
            int stage = 0, refr = 0;
            RoleParam pPoly, pHarm, pTone, pHVol, pSens, pAttack, pBlend, pDecay, pVol, pH;
            SpectralBank bank;
            BandEnv bs[SpectralBank::maxBands];
            Envelope det, slow;
            OnePole fuzzHp, fuzzLp;
            Smooth hS, bS, vS, hvS, polyS;
        };
    }

    std::unique_ptr<Effect> makeB3Time (const ModelDef& d, const std::string& type)
    {
        if (type == "b3memory")      return std::make_unique<B3Memory> (d);
        if (type == "b3echo")        return std::make_unique<B3Echo> (d);
        if (type == "b3replay")      return std::make_unique<B3Replay> (d);
        if (type == "b3verb")        return std::make_unique<B3Verb> (d);
        if (type == "b3holiest")     return std::make_unique<B3Holiest> (d);
        if (type == "b3shimmer")     return std::make_unique<B3Shimmer> (d);
        if (type == "b3attackdecay") return std::make_unique<B3AttackDecay> (d);
        return nullptr;
    }
}
