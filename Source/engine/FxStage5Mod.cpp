/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 5 (Behringer, Cuvave/M-VAVE): chorus, flanger, phaser, tremolo, vibrato e delay BBD ("type=i5...").

    Sei tipi, configurati per pedale con i valori del circuito reale (o dell'originale di cui il
    pedale e' la copia dichiarata):
      * i5bbd     - chorus/flanger/vibrato/dimension a BBD con il clock reale (ritardo = N / 2 f_clk),
                    VCO lineare o esponenziale, LFO dei circuiti (triangolo, sinusoide, quadra,
                    rampe, sample & hold, rilassamento), pre/de-enfasi, compander, diodi, filtri
                    anti-alias e di ricostruzione, rumore della linea. Modelli: chorus (CH-1, CE-5,
                    CEB-3 e i loro cloni), flanger (BF-2/HF-2/BF-2B, MODE a retroazione negativa o
                    positiva), vibrato (VB-2 con RISE e LATCH/BYPASS), dimension (DC-2 a tasti
                    combinabili), ce1 (CE-1 con il preampli HIGH/LOW e il modo vibrato), cluster
                    (Moogerfooger MF-108M: DRIVE, MIX, 6 forme dell'LFO, gamma chorus/flanger,
                    retroazione bipolare), lovetone (Flange With No Name: REACTION e ACTION a zero
                    centrale, modi TIME/SPACE), bflanger (Mu-Tron Flanger: pedale ottico o oscillatore
                    fra START e STOP, inerzia della fotoresistenza), tyco (Tycobrahe Pedalflanger: il
                    pedale e' la velocita' in AUTO e la posizione in MANUAL).
      * i5phase   - phaser a celle passa-tutto con l'elemento reale (JFET, OTA lineare, OTA a
                    controllo esponenziale, fotoresistenza), celle fisse (PH-2), parametri per modo,
                    retroazione filtrata e limitata (anello < 1), DRIVE e OUTPUT LEVEL (MF-103),
                    uscita stereo a pettini complementari.
      * i5biphase - doppio phaser a fotoaccoppiatori (Mu-Tron Bi-Phase): due catene di 6 celle LED/LDR,
                    due generatori di sweep sinusoide/quadra, SYNC NORM/REV, ingresso di B da IN A,
                    OUT A (serie, 12 celle) o IN B, comando CV al posto del pedale ottico.
      * i5vibe    - Uni-Vibe: quattro stadi a sfasatore a transistor con LDR e condensatori sfalsati
                    (15n, 220n, 470p, 4n7), guadagni di collettore/emettitore non uguali (il "throb"),
                    lampada con inerzia termica pilotata dall'oscillatore a sfasamento; CHORUS / VIBRATO.
      * i5trem    - tremolo a VCA (TR-2: triangolo -> quadra, CV livellato), tremolo/pan a due VCA
                    (PN-2: 4 modi), tremolo con TONE.
      * i5delay   - delay analogico a BBD lungo con compander, enfasi, filtri (anche che seguono il
                    clock), retroazione filtrata, guadagno d'anello <= 1 e limitazione morbida prima di
                    rientrare nella linea; preampli LEVEL del Deluxe Memory Man, chorus/vibrato sul
                    clock, LFO a 6 forme e SHORT/LONG (MF-104M), uscite DIRECT/DELAY separate, EQ
                    HIGH/LOW sulle ripetizioni.

    I mattoni della catena BBD sono copiati da FxStage4Mod.cpp (che resta invariato).
    Tutta la memoria si alloca in prepare(); i comandi continui e i cambi di modo sono lisciati.
*/

#include "FxStage5.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        //==============================================================================
        /** Mensola di pre-enfasi del 1° ordine (zero fz, polo fp) e de-enfasi inversa esatta. */
        struct Emphasis5
        {
            OnePole pre, de;
            float g = 1.0f;
            bool on = false;
            void set (double sr, double fz, double fp)
            {
                on = fz > 0 && fp > fz;
                if (! on) return;
                g = (float) (fp / fz);
                pre.set (sr, fp); de.set (sr, fz);
            }
            void reset() { pre.reset(); de.reset(); }
            inline float emph (float x) noexcept { if (! on) return x; const float l = pre.lp (x); return l + g * (x - l); }
            inline float deemph (float x) noexcept { if (! on) return x; const float l = de.lp (x); return l + (x - l) / g; }
        };

        /** Cascata di passa-basso (SVF di 2° ordine o RC) scalabile campione per campione. */
        struct LpChain5
        {
            static constexpr int maxN = 4;
            Svf svf[maxN];
            OnePole rc[maxN];
            float f[maxN] {}, k[maxN] {};
            bool isRc[maxN] {};
            int n = 0;
            float curScale = -1.0f, sr = 48000;
            void configure (double s, const std::string& spec)
            {
                sr = (float) s; n = 0;
                size_t i = 0;
                while (i < spec.size() && n < maxN)
                {
                    auto j = spec.find (',', i); if (j == std::string::npos) j = spec.size();
                    const auto tok = spec.substr (i, j - i);
                    const auto c = tok.find (':');
                    const double fr = std::atof (tok.c_str());
                    const double q = c == std::string::npos ? 0.0 : std::atof (tok.c_str() + c + 1);
                    if (fr > 0)
                    {
                        isRc[n] = q <= 0.0;
                        f[n] = (float) fr;
                        k[n] = q > 0 ? (float) (1.0 / q) : 0.0f;
                        ++n;
                    }
                    i = j + 1;
                }
                curScale = -1.0f;
                setScale (1.0f);
            }
            float first() const noexcept { return n > 0 ? f[0] : 20000.0f; }
            double peak() const
            {
                double best = 1.0;
                for (double fr = 10.0; fr < 40000.0; fr *= 1.01)
                {
                    double g = 1.0;
                    for (int m = 0; m < n; ++m)
                    {
                        const double x = fr / f[m];
                        if (isRc[m]) g /= std::sqrt (1.0 + x * x);
                        else { const double re = 1.0 - x * x, im = x * k[m]; g /= std::sqrt (re * re + im * im); }
                    }
                    best = std::max (best, g);
                }
                return best;
            }
            inline void setScale (float s) noexcept
            {
                if (std::abs (s - curScale) < 0.003f * curScale) return;
                curScale = s;
                for (int m = 0; m < n; ++m)
                {
                    const float fr = std::clamp (f[m] * s, 20.0f, 0.45f * sr);
                    if (isRc[m]) rc[m].setFast (sr, fr);
                    else svf[m].setG (fastTanPi (fr, sr), k[m]);
                }
            }
            void reset() { for (auto& v : svf) v.reset(); for (auto& r : rc) r.reset(); }
            inline float tick (float x) noexcept
            {
                for (int m = 0; m < n; ++m) x = isRc[m] ? rc[m].lp (x) : svf[m].lp (x);
                return x;
            }
        };

        /** Compander NE570/SA571 nella topologia reale (rivelatore del compressore sulla sua uscita). */
        struct Compander5
        {
            float envC = 0, envE = 0, aC = 0.99f, aE = 0.99f, rr = 0.19f, floorC = 0.006f;
            void set (double sr, double tauCms, double tauEms, float ref = 0.3f)
            {
                aC = (float) std::exp (-1.0 / (tauCms * 0.001 * sr));
                aE = (float) std::exp (-1.0 / (tauEms * 0.001 * sr));
                rr = 0.6366f * ref; floorC = rr / 30.0f;
            }
            void reset() { envC = envE = 0; }
            inline float compress (float x) noexcept
            {
                const float y = x * rr / std::max (envC, floorC);
                envC = std::abs (y) + aC * (envC - std::abs (y));
                return y;
            }
            inline float expand (float y) noexcept
            {
                envE = std::abs (y) + aE * (envE - std::abs (y));
                return y * std::max (envE, floorC) / rr;
            }
        };

        inline float diodeClamp5 (float x, float v) noexcept { return v * std::tanh (x / v); }

        /** Lista di stringhe separate da virgole (fuori dal thread audio). */
        std::vector<std::string> strList5 (const std::string& s)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find (',', i); if (j == std::string::npos) j = s.size();
                if (j > i) out.push_back (s.substr (i, j - i));
                i = j + 1;
            }
            return out;
        }
        /** Coppia "lo,hi" della config con valori di riserva. */
        void pair5 (const Config& c, const char* key, float& lo, float& hi)
        {
            auto v = c.list (key);
            if (v.size() > 0) lo = (float) v[0];
            if (v.size() > 1) hi = (float) v[1];
        }

        //==============================================================================
        /** Catena BBD: enfasi -> compressore -> diodi -> anti-alias -> BBD (clock reale) ->
            ricostruzione -> espansore -> de-enfasi -> blocco della continua. */
        class Line5
        {
        public:
            struct Spec
            {
                int stages = 1024;
                std::string aa = "9000:0.707", rc = "9000:0.5412,9000:1.3066";
                double emZ = 0, emP = 0, ctauC = 10, ctauE = 10, trackK = 0, dcHz = 20;
                bool comp = false;
                float clamp = 0, sat = 1.5f, noise = 2.0e-4f, loss = 6.0e-5f;

                void read (const Config& c)
                {
                    stages = (int) c.num ("stages", stages);
                    aa = c.str ("aa", aa.c_str());
                    rc = c.str ("rc", rc.c_str());
                    auto em = c.list ("emph");
                    if (em.size() >= 2) { emZ = em[0]; emP = em[1]; }
                    comp = c.num ("comp", comp ? 1 : 0) > 0.5;
                    auto ct = c.list ("ctau");
                    if (ct.size() >= 1) ctauC = ct[0];
                    ctauE = ct.size() >= 2 ? ct[1] : ctauC;
                    clamp = (float) c.num ("clamp", clamp);
                    sat = (float) c.num ("sat", sat);
                    noise = (float) c.num ("noise", noise);
                    loss = (float) c.num ("loss", loss);
                    trackK = c.num ("track", trackK);
                    dcHz = c.num ("dc", dcHz);
                }
            };

            void prepare (double s, const Spec& sp)
            {
                sr = s; spec = sp;
                stages = std::max (8, sp.stages);
                bbd.allocate (stages);
                bbd.setLoss (std::min (0.35f, (float) stages * sp.loss));
                bbd.setNoise (sp.noise);
                aa.configure (s, sp.aa);
                rc.configure (s, sp.rc);
                em.set (s, sp.emZ, sp.emP);
                comp.set (s, sp.ctauC, sp.ctauE, 0.3f);
                dc.set (s, sp.dcHz);
                // con il compander il guadagno dei filtri fra compressore ed espansore conta al quadrato
                // (2:1 poi 1:2): la retroazione si normalizza sul massimo di |H|^2
                const double pk = aa.peak() * rc.peak();
                peakG = (float) (sp.comp ? pk * pk : pk);
                reset();
            }
            void reset() { bbd.clear(); aa.reset(); rc.reset(); em.reset(); comp.reset(); dc.reset(); }
            float peakGain() const noexcept { return peakG; }
            double clockFor (double ms) const noexcept { return (double) stages / (2.0 * std::max (0.02, ms) * 0.001); }
            /** Fattore di scala dei filtri (taglio <= k * f_clk) senza passare dalla linea. */
            inline float scaleFor (double clkHz) const noexcept
            {
                return spec.trackK > 0 ? std::min (1.0f, (float) (spec.trackK * clkHz) / aa.first()) : 1.0f;
            }

            inline float tick (float x, double clkHz) noexcept
            {
                if (spec.trackK > 0)
                {
                    const float s = scaleFor (clkHz);
                    aa.setScale (s); rc.setScale (s);
                }
                float v = em.emph (x);
                if (spec.comp) v = comp.compress (v);
                if (spec.clamp > 0) v = diodeClamp5 (v, spec.clamp);
                v = aa.tick (v);
                v = spec.sat * softSat (v / spec.sat);
                float y = bbd.tick (v, clkHz / sr);
                y = rc.tick (y);
                if (spec.comp) y = comp.expand (y);
                y = em.deemph (y);
                return dc.hp (y);
            }
        private:
            double sr = 48000;
            int stages = 1024;
            float peakG = 1.0f;
            Spec spec;
            BbdLine bbd;
            LpChain5 aa, rc;
            Emphasis5 em;
            Compander5 comp;
            OnePole dc;
        };

        //==============================================================================
        /** LFO con le forme dei circuiti; l'uscita passa da un polo (fronti della quadra, salti
            della rampa e del sample & hold arrotondati come dalla rete RC del pilota). */
        struct Lfo5
        {
            enum Shape { Tri, Sine, Relax, Square, Rounded, RampUp, RampDown, SampleHold };
            Lfo lfo;
            RelaxOsc relax;
            Noise noise;
            OnePole edge;
            float held = 0, relaxH = 0.3f, edgeHz = 80, sr = 48000;
            static Shape parse (const std::string& s)
            {
                return s == "sine" ? Sine : s == "relax" ? Relax : s == "square" ? Square : s == "round" ? Rounded
                     : s == "ramp" ? RampUp : s == "saw" ? RampDown : s == "sh" ? SampleHold : Tri;
            }
            void prepare (double s, double eHz) { sr = (float) s; edgeHz = (float) eHz; edge.set (s, eHz); }
            void reset (double ph = 0.25) { lfo.ph = ph; relax.v = 0.5f; relax.up = true; edge.reset(); held = 0; }
            inline float raw (double hz, Shape sh) noexcept
            {
                float y;
                const double before = lfo.ph;
                switch (sh)
                {
                    case Sine:       lfo.setHz (sr, hz); y = lfo.sine(); lfo.step(); break;
                    case Square:     lfo.setHz (sr, hz); y = lfo.square(); lfo.step(); break;
                    case Relax:      relax.set (sr, hz, relaxH); y = relax.tick(); break;
                    case Rounded:    lfo.setHz (sr, hz); y = std::sin (1.5707963f * lfo.tri()); lfo.step(); break;
                    case RampUp:     lfo.setHz (sr, hz); y = lfo.saw(); lfo.step(); break;
                    case RampDown:   lfo.setHz (sr, hz); y = -lfo.saw(); lfo.step(); break;
                    case SampleHold: lfo.setHz (sr, hz); lfo.step(); if (lfo.ph < before) held = noise.uni(); y = held; break;
                    case Tri:
                    default:         lfo.setHz (sr, hz); y = lfo.tri(); lfo.step(); break;
                }
                return y;
            }
            inline float tick (double hz, Shape sh) noexcept { return edge.lp (raw (hz, sh)); }
        };

        //==============================================================================
        /** Modulazioni a BBD con il clock reale. */
        class I5Bbd final : public Effect
        {
        public:
            enum Model { Chorus, Flanger, Vibrato, Dimension, Ce1, Cluster, Lovetone, BFlanger, Tyco };
            enum Out { Mono, Inv, DryR, WetR, DryL };

            explicit I5Bbd (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "chorus");
                model = m == "flanger" ? Flanger : m == "vibrato" ? Vibrato : m == "dimension" ? Dimension : m == "ce1" ? Ce1
                      : m == "cluster" ? Cluster : m == "lovetone" ? Lovetone : m == "bflanger" ? BFlanger : m == "tyco" ? Tyco : Chorus;
                const auto o = cfg.str ("out", "mono");
                out = o == "inv" ? Inv : o == "dryr" ? DryR : o == "wetr" ? WetR : o == "dryl" ? DryL : Mono;
                spec.read (cfg);
                pair5 (cfg, "dly", dLo, dHi);
                d2Lo = dLo; d2Hi = dHi; pair5 (cfg, "dly2", d2Lo, d2Hi);
                expVco = cfg.str ("vco", "exp") == "exp";
                ctr = (float) cfg.num ("ctr", 0.5); swing = (float) cfg.num ("swing", 0.5);
                pair5 (cfg, "rate", rLo, rHi);
                r2Lo = rLo; r2Hi = rHi; pair5 (cfg, "rate2", r2Lo, r2Hi);
                shape = Lfo5::parse (cfg.str ("lfo", "tri"));
                for (auto& w : strList5 (cfg.str ("waves", ""))) waves.push_back (Lfo5::parse (w));
                relaxH = (float) cfg.num ("relax", 0.3);
                edgeHz = cfg.num ("edge", 80);
                fbMax = (float) cfg.num ("fbmax", 0.0); fbSign = (float) cfg.num ("fbsign", 1.0);
                fbBip = cfg.num ("fbbip", 0) > 0.5;
                resHp = cfg.num ("reshp", 0);
                pair5 (cfg, "mix", mixD, mixW);
                wetHp = cfg.num ("wethp", 0); wetLp = cfg.num ("wetlp", 0);
                pair5 (cfg, "tone", toneLo, toneHi);
                pair5 (cfg, "high", highLo, highHi);
                pair5 (cfg, "low", lowLo, lowHi);
                lowRev = cfg.num ("lowrev", 0) > 0.5;
                pair5 (cfg, "rise", riseLo, riseHi);
                pair5 (cfg, "outl", outUnity, outMaxG);
                pair5 (cfg, "drive", drLo, drHi);
                pair5 (cfg, "pre", preLo, preHi);
                preSat = (float) cfg.num ("presat", 2.0);
                outMax = (float) cfg.num ("outmax", 2.0);
                ldrMs = cfg.list ("ldr");
                modes = cfg.list ("modes");
                pRate = role (*this, "rate", 0.4f); pDepth = role (*this, "depth", 0.5f);
                pManual = role (*this, "manual", ctr); pRes = role (*this, "res", fbBip ? 0.5f : 0.0f);
                pLevel = role (*this, "level", 0.5f); pOutLvl = role (*this, "outlvl", outUnity);
                pMode = role (*this, "mode", 0.0f); pTone = role (*this, "tone", 0.5f);
                pHigh = role (*this, "high", 1.0f); pLow = role (*this, "low", 0.0f);
                pRise = role (*this, "rise", 0.3f); pLatch = role (*this, "latch", 1.0f);
                pFbPol = role (*this, "fbpol", 1.0f); pWave = role (*this, "wave", 0.0f);
                pRange = role (*this, "range", 0.0f); pDrive = role (*this, "drive", 0.35f);
                pMix = role (*this, "mix", 0.5f); pVDepth = role (*this, "vdepth", 0.5f); pVRate = role (*this, "vrate", 0.4f);
                pInput = role (*this, "input", 0.0f); pBtn12 = role (*this, "btn12", 1.0f / 3.0f); pBtn34 = role (*this, "btn34", 0.0f);
                pPedal = role (*this, "pedal", 0.5f); pStart = role (*this, "start", 0.2f); pStop = role (*this, "stop", 0.8f);
                pAction = role (*this, "action", 0.75f); pGain = role (*this, "gain", 0.5f); pSpace = role (*this, "space", 0.0f);
                pStereo = role (*this, "stereo", 1.0f); pPedSw = role (*this, "pedsw", 0.0f);
            }

            void prepare (double s, int) override
            {
                sr = s;
                lineA.prepare (s, spec);
                lineB.prepare (s, spec);
                for (auto* sm : { &fbS, &wetS, &dryS, &outS, &polS, &latchS, &inS, &vibS, &drS, &spcS, &stS, &mixS, &actS })
                    sm->set (s, 25);
                rateS.set (s, 40); manS.set (s, 30); depS.set (s, 30); toneS.set (s, 30); highS.set (s, 30); lowS.set (s, 30);
                rangeS.set (s, 40); startS.set (s, 30); stopS.set (s, 30); pedS.set (s, 30);
                posA.set (s, 1.5); posB.set (s, 1.5);
                lfo1.prepare (s, edgeHz); lfo2.prepare (s, edgeHz);
                lfo1.relaxH = lfo2.relaxH = relaxH;
                if (wetHp > 0) { whA.set (s, wetHp); whB.set (s, wetHp); }
                if (wetLp > 0) { wlA.set (s, wetLp); wlB.set (s, wetLp); }
                if (resHp > 0) { rhA.set (s, resHp); rhB.set (s, resHp); }
                if (ldrMs.size() >= 2) ldr.set (s, ldrMs[0], ldrMs[1], 1.0, std::exp (1.0), 1.0f);
                riseK = 0; curTone = curHigh = curLow = -1;
                reset();
            }
            void reset() override
            {
                lineA.reset(); lineB.reset(); fbA = fbB = 0;
                lfo1.reset (0.25); lfo2.reset (0.75);
                for (auto* f : { &whA, &whB, &wlA, &wlB, &rhA, &rhB, &hiA, &hiB }) f->reset();
                toneA.reset(); toneB.reset(); lowA.reset(); lowB.reset();
                riseGain = riseMs() > 0 ? 0.0f : 1.0f; wasBypass = false;
            }

            float riseMs() const noexcept { return pRise.present() && riseHi > 0 ? logMap (pRise.get (*this), riseLo, riseHi) : 0.0f; }

            /** Posizione 0..1 (0 = ritardo minimo) -> clock della linea. */
            inline double clockAt (const Line5& L, double lo, double hi, float p) const noexcept
            {
                p = std::clamp (p, 0.0f, 1.0f);
                if (expVco) return L.clockFor (lo * std::pow (hi / lo, (double) p));
                const double fmax = L.clockFor (lo), fmin = L.clockFor (hi);
                return fmax - (fmax - fmin) * (double) p;                           // VCO lineare: ritardo iperbolico
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = pMode.present() ? pMode.step (*this) : 0;
                float hzT = logMap (pRate.get (*this), rLo, rHi);
                float depT = pDepth.get (*this);
                const float manT = pManual.present() ? pManual.get (*this) : ctr;
                // retroazione: unipolare (RES/REGEN) o bipolare a zero centrale (REACTION, FEEDBACK Moog)
                float fbT = fbBip ? (pRes.get (*this) - 0.5f) * 2.0f * fbMax : pRes.get (*this) * fbMax * fbSign;
                float wetT = pLevel.present() ? 2.0f * pLevel.get (*this) * mixW : mixW;
                float dryT = mixD;
                const float polT = pFbPol.present() && pFbPol.step (*this) == 0 ? -1.0f : 1.0f;   // UF300: MODE I negativo
                Lfo5::Shape sh = shape;
                if (pWave.present() && ! waves.empty()) sh = waves[(size_t) std::clamp (pWave.step (*this), 0, (int) waves.size() - 1)];
                bool longRange = pRange.present() && pRange.step (*this) == 1;
                float inT = 1.0f, latchT = 1.0f, vibT = model == Vibrato ? 1.0f : 0.0f, spcT = 0.0f, stT = 1.0f;
                float outT = pOutLvl.present() ? volKnob (pOutLvl.get (*this), outUnity, outMaxG) : 1.0f;
                float preG = 1.0f;
                Lfo5::Shape sh2 = sh;
                switch (model)
                {
                    case Vibrato:
                    {
                        // VB-2/UV300: BYPASS lascia il segnale diretto; uscendo dal BYPASS la profondita' risale col RISE
                        const bool byp = pLatch.present() && pLatch.step (*this) == 1;
                        latchT = byp ? 0.0f : 1.0f;
                        if (byp) wasBypass = true;
                        else if (wasBypass) { wasBypass = false; riseGain = 0.0f; }
                        break;
                    }
                    case Dimension:
                    {
                        // tasti 1..4 combinabili: le reti RC dei tasti premuti si sommano (velocita' = somma, profondita'
                        // = somma quadratica limitata); nessun tasto = solo diretto
                        const int b12 = pBtn12.present() ? pBtn12.step (*this) : 1, b34 = pBtn34.present() ? pBtn34.step (*this) : 0;
                        const bool on[4] = { b12 == 1 || b12 == 3, b12 == 2 || b12 == 3, b34 == 1 || b34 == 3, b34 == 2 || b34 == 3 };
                        float rs = 0, ds = 0; int cnt = 0;
                        if (pMode.present() && ! pBtn12.present())
                        {
                            const int k = std::clamp (mode, 0, (int) modes.size() / 2 - 1);
                            rs = (float) modes[(size_t) (2 * k)]; ds = (float) (modes[(size_t) (2 * k + 1)] * modes[(size_t) (2 * k + 1)]); cnt = 1;
                        }
                        else
                            for (int k = 0; k < 4 && 2 * k + 1 < (int) modes.size(); ++k)
                                if (on[k]) { rs += (float) modes[(size_t) (2 * k)]; ds += (float) (modes[(size_t) (2 * k + 1)] * modes[(size_t) (2 * k + 1)]); ++cnt; }
                        if (cnt > 0) { hzT = rs; depT = std::min (1.4f, std::sqrt (ds)); }
                        else { hzT = 0.3f; depT = 0.0f; wetT = 0.0f; }
                        break;
                    }
                    case Ce1:
                    {
                        // CE-1: CHORUS = velocita' quasi fissa e INTENSITY sulla profondita'; VIBRATO = solo effetto con
                        // V.DEPTH e V.RATE. Preampli d'ingresso: INPUT HIGH/LOW e LEVEL
                        const bool vib = mode == 1;
                        vibT = vib ? 1.0f : 0.0f;
                        if (vib) { hzT = logMap (pVRate.get (*this), r2Lo, r2Hi); depT = pVDepth.get (*this); }
                        else hzT = logMap (pDepth.get (*this), rLo, rHi);
                        inT = pInput.present() && pInput.step (*this) == 0 ? preHi : preLo;
                        preG = volKnob (pLevel.get (*this), 0.7f, 1.6f);
                        wetT = mixW;
                        break;
                    }
                    case Cluster:
                    {
                        // MF-108M: DRIVE d'ingresso, MIX diretto/effetto, gamma CHORUS/FLANGER (toggle = range)
                        const float mx = pMix.get (*this);
                        dryT = 1.0f - mx; wetT = mx;
                        inT = logMap (pDrive.get (*this), drLo, drHi);
                        break;
                    }
                    case Lovetone:
                    {
                        // I/P GAIN, ACTION a zero centrale (mix del ritardo in fase o in controfase), modi TIME/SPACE
                        inT = logMap (pGain.get (*this), drLo, drHi);
                        const int sp = pSpace.present() ? pSpace.step (*this) : 0;                // 0 TIME, 1 SPACE, 2 TIME+SPACE
                        wetT = (sp == 1 ? 0.0f : 1.0f) * (pAction.get (*this) - 0.5f) * 2.0f * mixW;
                        spcT = sp >= 1 ? 1.0f : 0.0f;
                        if (sp == 1) fbT = 0.0f;
                        stT = pStereo.present() && pStereo.step (*this) == 0 ? 0.0f : 1.0f;
                        const int lw = pWave.present() ? std::clamp (pWave.step (*this), 0, 3) : 0;
                        sh = (lw & 2) ? Lfo5::Square : Lfo5::Tri;        // forma per TIME
                        sh2 = (lw & 1) ? Lfo5::Square : Lfo5::Tri;       // forma per SPACE
                        break;
                    }
                    case Tyco:
                        // AUTO: il pedale e' la velocita' dell'LFO; MANUAL: il pedale e' la posizione
                        if (pPedSw.present() && pPedSw.step (*this) == 1) { hzT = 0.5f; }
                        else hzT = logMap (pPedal.get (*this), rLo, rHi);
                        break;
                    case Chorus: case Flanger: case BFlanger: default: break;
                }
                const bool tycoManual = model == Tyco && pPedSw.present() && pPedSw.step (*this) == 1;
                const bool bfPedal = model == BFlanger && pPedSw.present() && pPedSw.step (*this) == 1;
                const float rangeT = longRange ? 1.0f : 0.0f;
                const float riseCoef = riseMs() > 0 ? 1.0f - std::exp (-1.0f / (riseMs() * 0.001f * (float) sr)) : 1.0f;
                // filtri del ramo d'effetto (tagli lisciati: niente salti dei coefficienti)
                const float toneT = pTone.present() && toneHi > 0 ? logMap (pTone.get (*this), toneLo, toneHi) : -1.0f;
                const float highT = pHigh.present() && highHi > 0 ? logMap (pHigh.get (*this), highLo, highHi) : -1.0f;
                const float lowT = pLow.present() && lowHi > 0 ? logMap (lowRev ? 1.0f - pLow.get (*this) : pLow.get (*this), lowLo, lowHi) : -1.0f;
                const float fsr = (float) sr;

                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float x = 0.5f * (inL + inR);
                    // ingresso: DRIVE / I/P GAIN / preampli del CE-1
                    const float ig = inS.next (inT);
                    if (model == Ce1)
                    {
                        float v = ig * x;
                        v = preSat * softSat ((v + 0.08f * v * v) / preSat);       // stadio a transistor: lieve asimmetria
                        x = v * drS.next (preG);
                    }
                    else if (model == Cluster || model == Lovetone)
                        x = preSat * softSat (ig * x / preSat);
                    const float hz = rateS.next (hzT);
                    float l1 = lfo1.tick (hz, sh);
                    float l2 = 0.0f;
                    if (model == Lovetone) l2 = lfo2.tick (hz, sh2);
                    const float dep = depS.next (depT), man = manS.next (manT);
                    if (model == Vibrato || riseGain < 1.0f) riseGain += (1.0f - riseGain) * riseCoef;
                    float pA = 0.5f, pB = 0.5f;
                    switch (model)
                    {
                        case Dimension:
                            pA = 0.5f + swing * dep * l1; pB = 0.5f - swing * dep * l1; break;   // due linee in controfase
                        case Vibrato:
                            pA = ctr + swing * dep * riseGain * l1; break;
                        case Flanger: case Cluster: case Lovetone:
                            pA = man + swing * dep * l1; break;
                        case BFlanger:
                        {
                            // l'oscillatore (o il pedale) accende il LED della fotoresistenza che regola il VCO:
                            // discesa veloce e risalita lenta della LDR (dominio logaritmico)
                            const float st = startS.next (pStart.get (*this)), sp = stopS.next (pStop.get (*this));
                            const float u = bfPedal ? pedS.next (pPedal.get (*this)) : 0.5f + 0.5f * l1;
                            float q = std::clamp (u, 0.0f, 1.0f);
                            if (ldrMs.size() >= 2) q = 1.0f - (float) std::log (ldr.tick (q));         // log R normalizzato: 1 = LED acceso
                            pA = st + (sp - st) * q;
                            break;
                        }
                        case Tyco:
                            pA = tycoManual ? 0.5f + (pedS.next (pPedal.get (*this)) - 0.5f) * (0.3f + 0.7f * dep) : ctr + swing * dep * l1;
                            break;
                        case Chorus: case Ce1: default:
                            pA = man + swing * dep * l1; break;
                    }
                    pA = posA.next (std::clamp (pA, 0.0f, 1.0f));
                    pB = posB.next (std::clamp (pB, 0.0f, 1.0f));
                    // gamma CHORUS/FLANGER: gli estremi del ritardo scivolano in ~40 ms (niente salto del clock)
                    const float rg = rangeS.next (rangeT);
                    const double dMinA = rg <= 0.0f ? dLo : dLo * std::pow ((double) d2Lo / dLo, (double) rg);
                    const double dMaxA = rg <= 0.0f ? dHi : dHi * std::pow ((double) d2Hi / dHi, (double) rg);

                    const float pol = polS.next (polT);
                    const float fb = fbS.next (fbT) * pol / lineA.peakGain();
                    float fa = fbA;
                    if (resHp > 0) fa = rhA.hp (fa);
                    const float a = lineA.tick (x + loopSat (fb * fa), clockAt (lineA, dMinA, dMaxA, pA));
                    fbA = a;
                    float b = 0.0f;
                    if (model == Dimension)
                    {
                        b = lineB.tick (x, clockAt (lineB, dMinA, dMaxA, pB));
                        fbB = b;
                    }
                    float wA = a, wB = b;
                    if (wetHp > 0) { wA = whA.hp (wA); wB = whB.hp (wB); }
                    if (wetLp > 0) { wA = wlA.lp (wA); wB = wlB.lp (wB); }
                    if (toneT > 0)
                    {
                        const float f = toneS.next (toneT);
                        if (std::abs (f - curTone) > 0.002f * curTone) { curTone = f; const float g = fastTanPi (std::min (f, 0.45f * fsr), fsr); toneA.setG (g, 1.4142f); toneB.setG (g, 1.4142f); }
                        wA = toneA.lp (wA); wB = toneB.lp (wB);
                    }
                    if (highT > 0)
                    {
                        const float f = highS.next (highT);
                        if (std::abs (f - curHigh) > 0.002f * curHigh) { curHigh = f; hiA.setFast (fsr, f); hiB.setFast (fsr, f); }
                        wA = hiA.lp (wA); wB = hiB.lp (wB);
                    }
                    if (lowT > 0)
                    {
                        const float f = lowS.next (lowT);
                        if (std::abs (f - curLow) > 0.002f * curLow) { curLow = f; const float g = fastTanPi (f, fsr); lowA.setG (g, 1.4142f); lowB.setG (g, 1.4142f); }
                        wA = lowA.hp (wA); wB = lowB.hp (wB);
                    }
                    const float wg = wetS.next (wetT), dg = dryS.next (dryT), og = outS.next (outT);
                    float L, R;
                    switch (model)
                    {
                        case Dimension:
                        {
                            L = dg * x + wg * (wA - 0.6f * wB) * 0.7f;
                            R = dg * x + wg * (wB - 0.6f * wA) * 0.7f;
                            break;
                        }
                        case Vibrato:
                        {
                            const float lt = latchS.next (latchT);                    // BYPASS: diretto
                            L = R = lt * wA * wg + (1.0f - lt) * x;
                            break;
                        }
                        case Ce1:
                        {
                            const float v = vibS.next (vibT);
                            const float mono = (1.0f - v) * (dg * x + wg * wA) + v * wA * 1.2f;
                            if (out == DryL && numCh > 1) { L = (1.0f - v) * x + v * wA * 1.2f; R = (1.0f - v) * wg * wA * 2.0f + v * wA * 1.2f; }
                            else L = R = mono;
                            break;
                        }
                        case Lovetone:
                        {
                            const float mono = dg * x + wg * wA;
                            // SPACE: tremolo/pan fra le due uscite con la seconda forma d'onda
                            const float s = spcS.next (spcT) * dep;
                            const float gS = 1.0f - s * (0.5f + 0.5f * l2), gT = 1.0f - s * (0.5f - 0.5f * l2);
                            const float st = stS.next (stT);
                            L = mono * gS;
                            R = st * mono * gT + (1.0f - st) * L;
                            break;
                        }
                        case Chorus: case Flanger: case Cluster: case BFlanger: case Tyco: default:
                        {
                            const float w = wg * wA * pol;                              // MODE I: ritardato invertito nel mix
                            switch (out)
                            {
                                case Inv:  L = dg * x + w; R = dg * x - w; break;
                                case DryR: L = dg * x + w; R = x; break;
                                case WetR: L = dg * x + w; R = w; break;
                                case Mono: case DryL: default: L = R = dg * x + w; break;
                            }
                            if (model == Vibrato) L = R = w;
                            break;
                        }
                    }
                    L = outMax * softSat (L * og / outMax);
                    R = outMax * softSat (R * og / outMax);
                    ch[0][i] = L;
                    if (numCh > 1) ch[1][i] = R;
                }
            }

        private:
            Config cfg;
            Model model = Chorus;
            Out out = Mono;
            Line5::Spec spec;
            Line5 lineA, lineB;
            double sr = 48000, wetHp = 0, wetLp = 0, resHp = 0, edgeHz = 80;
            float dLo = 3, dHi = 9, d2Lo = 3, d2Hi = 9;
            bool expVco = true, fbBip = false, lowRev = false, wasBypass = false;
            float ctr = 0.5f, swing = 0.5f, rLo = 0.3f, rHi = 3, r2Lo = 0.3f, r2Hi = 3, fbMax = 0, fbSign = 1, mixD = 0.6f, mixW = 0.6f,
                  relaxH = 0.3f, toneLo = 0, toneHi = 0, highLo = 0, highHi = 0, lowLo = 0, lowHi = 0, riseLo = 0, riseHi = 0,
                  outUnity = 0.5f, outMaxG = 2.0f, drLo = 1, drHi = 1, preLo = 1, preHi = 1, preSat = 2.0f, outMax = 2.0f,
                  fbA = 0, fbB = 0, riseGain = 1, riseK = 0, curTone = -1, curHigh = -1, curLow = -1;
            std::vector<double> ldrMs, modes;
            Lfo5::Shape shape = Lfo5::Tri;
            std::vector<Lfo5::Shape> waves;
            RoleParam pRate, pDepth, pManual, pRes, pLevel, pOutLvl, pMode, pTone, pHigh, pLow, pRise, pLatch, pFbPol, pWave, pRange,
                      pDrive, pMix, pVDepth, pVRate, pInput, pBtn12, pBtn34, pPedal, pStart, pStop, pAction, pGain, pSpace, pStereo, pPedSw;
            Lfo5 lfo1, lfo2;
            cx::Ldr ldr;
            OnePole whA, whB, wlA, wlB, rhA, rhB, hiA, hiB;
            Svf toneA, toneB, lowA, lowB;
            Smooth fbS, wetS, dryS, outS, polS, latchS, inS, vibS, drS, spcS, stS, mixS, actS, rateS, manS, depS, toneS, highS, lowS,
                   rangeS, startS, stopS, pedS, posA, posB;
        };

        //==============================================================================
        /** Phaser a celle passa-tutto con l'elemento variabile reale (copia estesa del phaser della tappa 4). */
        class I5Phaser final : public Effect
        {
        public:
            enum Cell { Jfet, Ldr, Ota, Expo };
            static constexpr int maxStages = 16;

            explicit I5Phaser (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto c = cfg.str ("cell", "jfet");
                cell = c == "ldr" ? Ldr : c == "ota" ? Ota : c == "exp" ? Expo : Jfet;
                stageList = cfg.list ("stages"); if (stageList.empty()) stageList = { 4 };
                fixList = cfg.list ("fixn");
                fixHz = (float) cfg.num ("fixf", 1000);
                spanList = cfg.list ("span"); fbmList = cfg.list ("fbm");
                cap = cfg.num ("c", 47e-9); rFix = cfg.num ("r", 30000); ron = cfg.num ("ron", 1500);
                auto f = cfg.list ("f"); fLo = f.size() > 0 ? f[0] : 60; fHi = f.size() > 1 ? f[1] : 5000;
                rpar = cfg.num ("rpar", 100000); rmin = cfg.num ("rmin", 4000); rmax = cfg.num ("rmax", 1.0e6);
                ldrA = cfg.num ("la", 8); ldrR = cfg.num ("lr", 60);
                pair5 (cfg, "u", uLo, uHi);
                pair5 (cfg, "rate", rLo, rHi);
                shape = Lfo5::parse (cfg.str ("lfo", "tri"));
                relaxH = (float) cfg.num ("relax", 0.3);
                fbMax = (float) cfg.num ("fb", 0.0); fbSign = (float) cfg.num ("fbsign", 1);
                fbHp = cfg.num ("fbhp", 0); fbLp = cfg.num ("fblp", 0);
                pair5 (cfg, "mix", mixD, mixW);
                gain = (float) cfg.num ("gain", 1.25); wetHp = cfg.num ("wethp", 0);
                rail = (float) cfg.num ("rail", 3.5);
                spread = (float) cfg.num ("spread", 0.02); gateMs = cfg.num ("gate", 2.0);
                manLin = cfg.str ("mantaper", "a") == "lin";
                stereoOut = cfg.num ("stereo", 0) > 0.5;
                pair5 (cfg, "outl", outUnity, outMaxG);
                pair5 (cfg, "drive", drLo, drHi);
                drSat = (float) cfg.num ("drsat", 1.5);
                pRate = role (*this, "rate", 0.35f);
                pDepth = role (*this, "depth", 1.0f);
                pRes = role (*this, "res", 0.0f);
                pManual = role (*this, "manual", 0.5f);
                pStages = role (*this, "stages", 0.0f);
                pOutLvl = role (*this, "outlvl", outUnity);
                pDrive = role (*this, "drive", 0.35f);
            }

            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto* sm : { &fbS, &outS, &drS, &spanS, &fixS }) sm->set (s, 20);
                rateS.set (s, 40); manS.set (s, 25); depS.set (s, 25);
                uS.set (s, gateMs);
                ldr.set (s, ldrA, ldrR, rmin, rmax, 0.75f);
                if (fbHp > 0) fbHpF.set (s, fbHp);
                if (fbLp > 0) fbLpF.set (s, fbLp);
                if (wetHp > 0) wetHpF.set (s, wetHp);
                lfo.prepare (s, 200.0); lfo.relaxH = relaxH;
                stageX.prepare (s, 10);
                fixA = apCoef (fixHz, sr);
                reset();
            }
            void reset() override
            {
                for (auto& a : ap) a.reset();
                for (auto& a : fx) a.reset();
                fbState = 0; fbHpF.reset(); fbLpF.reset(); wetHpF.reset();
                lfo.reset (0.25);
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sel = pStages.present() ? std::clamp (pStages.step (*this), 0, (int) stageList.size() - 1) : 0;
                const int wantStages = (int) std::clamp (stageList[(size_t) sel], 2.0, (double) maxStages);
                const float fixT = fixList.empty() ? 0.0f : (float) fixList[(size_t) std::min (sel, (int) fixList.size() - 1)];
                const float spanT = spanList.empty() ? 1.0f : (float) spanList[(size_t) std::min (sel, (int) spanList.size() - 1)];
                const float fbm = fbmList.empty() ? 1.0f : (float) fbmList[(size_t) std::min (sel, (int) fbmList.size() - 1)];
                const float hzT = logMap (pRate.get (*this), rLo, rHi);
                const float depthT = pDepth.get (*this);
                const float fbT = std::min (0.95f, pRes.get (*this) * fbMax * fbm) * fbSign;
                const float manT = pManual.present() ? (manLin ? pManual.get (*this) : taperA (pManual.get (*this))) : -1.0f;
                const float outT = pOutLvl.present() ? volKnob (pOutLvl.get (*this), outUnity, outMaxG) : 1.0f;
                const float drT = pDrive.present() ? logMap (pDrive.get (*this), drLo, drHi) : 1.0f;
                const float uMid = 0.5f * (uLo + uHi), uHalf = 0.5f * (uHi - uLo);

                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float xg = stageX.tick (wantStages, changed);
                    if (changed) nst = stageX.current;
                    const float l = lfo.tick (rateS.next (hzT), shape);
                    const float dep = depS.next (depthT) * spanS.next (spanT);
                    float u;
                    if (manT >= 0) u = manS.next (manT) + 0.5f * dep * l;
                    else u = uMid + uHalf * dep * l;
                    u = uS.next (std::clamp (u, 0.0f, 1.0f));

                    float fc;
                    if (cell == Jfet) fc = (float) ((1.0 / rFix + (double) u / ron) / (2.0 * juce::MathConstants<double>::pi * cap));
                    else if (cell == Ota) fc = (float) (fLo + (fHi - fLo) * (double) u);
                    else if (cell == Expo) fc = (float) (fLo * std::pow (fHi / fLo, (double) u));
                    else
                    {
                        const float rl = ldr.tick (u);
                        const double r = (double) rl * rpar / ((double) rl + rpar);
                        fc = (float) (1.0 / (2.0 * juce::MathConstants<double>::pi * r * cap));
                    }

                    float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    if (pDrive.present()) x = drSat * softSat (drS.next (drT) * x / drSat);   // DRIVE: stadio d'ingresso
                    float fbv = fbState;
                    if (fbHp > 0) fbv = fbHpF.hp (fbv);
                    if (fbLp > 0) fbv = fbLpF.lp (fbv);
                    float v = x + fbS.next (fbT) * loopSat (fbv);
                    v = rail * softSat (v / rail);
                    for (int s = 0; s < nst; ++s)
                    {
                        static const float dev[maxStages] = { -1.0f, 0.6f, -0.3f, 0.9f, -0.7f, 0.2f, 0.5f, -0.9f, 0.1f, -0.4f, 0.8f, -0.2f,
                                                              0.3f, -0.6f, 0.7f, -0.1f };
                        v = ap[s].tick (v, apCoef (fc * (1.0f + spread * dev[s]), sr));
                    }
                    // celle fisse (PH-2): entrano con dissolvenza quando cambia il modo
                    const float fxg = fixS.next (fixT);
                    if (fxg > 0.0f || fixT > 0.0f)
                    {
                        float vf = v;
                        const int nf = (int) std::ceil (std::max (fxg, fixT));
                        for (int s = 0; s < std::min (nf, 8); ++s) vf = fx[s].tick (vf, fixA);
                        const float k = std::clamp (fxg / std::max (1.0f, (float) nf), 0.0f, 1.0f);
                        v = v + k * (vf - v);
                    }
                    fbState = v;
                    float w = v;
                    if (wetHp > 0) w = wetHpF.hp (w);
                    w *= xg;
                    const float og = outS.next (outT);
                    const float yL = gain * (mixD * x + mixW * w) * og;
                    if (stereoOut && numCh > 1)
                    {
                        ch[0][i] = outRail (yL);
                        ch[1][i] = outRail (gain * (mixD * x - mixW * w) * og);         // pettine complementare
                        continue;
                    }
                    const float y = outRail (yL);
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }

        private:
            Config cfg;
            Cell cell = Jfet;
            std::vector<double> stageList, fixList, spanList, fbmList;
            double cap = 47e-9, rFix = 30000, ron = 1500, fLo = 60, fHi = 5000, rpar = 1e5, rmin = 4000, rmax = 1e6, ldrA = 8, ldrR = 60,
                   fbHp = 0, fbLp = 0, wetHp = 0, gateMs = 2;
            float sr = 48000, uLo = 0.03f, uHi = 1, rLo = 0.1f, rHi = 8, fbMax = 0, fbSign = 1, mixD = 0.5f, mixW = 0.5f, gain = 1.25f,
                  rail = 3.5f, spread = 0.02f, relaxH = 0.3f, fixHz = 1000, fixA = 0, outUnity = 0.5f, outMaxG = 2.0f, drLo = 1, drHi = 1,
                  drSat = 1.5f, fbState = 0;
            bool manLin = false, stereoOut = false;
            int nst = 4;
            Lfo5::Shape shape = Lfo5::Tri;
            RoleParam pRate, pDepth, pRes, pManual, pStages, pOutLvl, pDrive;
            Allpass1 ap[maxStages], fx[8];
            Lfo5 lfo;
            cx::Ldr ldr;
            OnePole fbHpF, fbLpF, wetHpF;
            XFader stageX;
            Smooth rateS, manS, depS, uS, fbS, outS, drS, spanS, fixS;
        };

        //==============================================================================
        /** Doppio phaser a fotoaccoppiatori (Bi-Phase / Dual-Phase). */
        class I5BiPhase final : public Effect
        {
        public:
            static constexpr int cells = 6;
            explicit I5BiPhase (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                cap = cfg.num ("c", 15e-9); rmin = cfg.num ("rmin", 1500); rmax = cfg.num ("rmax", 200000);
                ldrA = cfg.num ("la", 6); ldrR = cfg.num ("lr", 60);
                pair5 (cfg, "rate", rLo, rHi);
                fbMax = (float) cfg.num ("fb", 0.8);
                gain = (float) cfg.num ("gain", 1.25);
                pRate1 = role (*this, "rate", 0.3f); pRate2 = role (*this, "rate2", 0.4f);
                pDepA = role (*this, "depth", 0.8f); pFbA = role (*this, "res", 0.3f);
                pDepB = role (*this, "depth2", 0.8f); pFbB = role (*this, "res2", 0.3f);
                pShape = role (*this, "wave", 0.0f); pInB = role (*this, "routing", 0.0f);
                pSwB = role (*this, "sweepb", 0.0f); pCv = role (*this, "cv", 0.5f); pSync = role (*this, "sync", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                static const float spread[cells] = { 1.0f, 1.08f, 0.94f, 1.05f, 0.97f, 1.1f };   // tolleranze delle LDR
                for (int k = 0; k < cells; ++k)
                {
                    lA[k].set (s, ldrA * spread[k], ldrR * spread[k], rmin * spread[k], rmax * spread[k], 0.7f);
                    lB[k].set (s, ldrA * spread[cells - 1 - k], ldrR * spread[cells - 1 - k], rmin * spread[cells - 1 - k], rmax * spread[cells - 1 - k], 0.7f);
                }
                for (auto* sm : { &fbAS, &fbBS, &dAS, &dBS, &serS, &parS, &cvS, &revS, &srcS }) sm->set (s, 25);
                r1S.set (s, 40); r2S.set (s, 40);
                g1.prepare (s, 120.0); g2.prepare (s, 120.0);
                reset();
            }
            void reset() override
            {
                for (auto& a : apA) a.reset();
                for (auto& a : apB) a.reset();
                fbA = fbB = 0; g1.reset (0.25); g2.reset (0.6);
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int shp = pShape.present() ? std::clamp (pShape.step (*this), 0, 3) : 0;
                const auto s1 = (shp & 2) ? Lfo5::Square : Lfo5::Sine, s2 = (shp & 1) ? Lfo5::Square : Lfo5::Sine;
                const float h1 = logMap (pRate1.get (*this), rLo, rHi), h2 = logMap (pRate2.get (*this), rLo, rHi);
                const int rt = pInB.present() ? std::clamp (pInB.step (*this), 0, 2) : 0;                // IN A, OUT A, IN B
                const int sb = pSwB.present() ? std::clamp (pSwB.step (*this), 0, 2) : 0;                // GEN 1, GEN 2, CV
                const float revT = pSync.present() && pSync.step (*this) == 1 ? -1.0f : 1.0f;
                const float serT = rt == 1 ? 1.0f : 0.0f, parT = rt == 2 ? 1.0f : 0.0f;
                const float dAT = pDepA.get (*this), dBT = pDepB.get (*this);
                const float fAT = pFbA.get (*this) * fbMax, fBT = pFbB.get (*this) * fbMax;
                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float l1 = g1.tick (r1S.next (h1), s1), l2 = g2.tick (r2S.next (h2), s2);
                    const float cv = cvS.next (pCv.get (*this));
                    const float rev = revS.next (revT);
                    const float dA = dAS.next (dAT), dB = dBS.next (dBT);
                    // luce dei LED: centro del pannello +- DEPTH; B segue GEN 1 (anche invertito), GEN 2 o il CV
                    const float lightA = 0.5f + 0.5f * dA * l1;
                    const float srcB1 = sb == 0 ? 1.0f : 0.0f, srcB2 = sb == 1 ? 1.0f : 0.0f;
                    const float lightB = sb == 2 ? cv : 0.5f + 0.5f * dB * (srcB1 * rev * l1 + srcB2 * l2);
                    // phasor A
                    const float par = parS.next (parT), ser = serS.next (serT);
                    const float mono = 0.5f * (inL + inR);
                    const float xa = mono + par * (inL - mono);                                  // IN B: A prende solo IN A
                    float v = xa + fbAS.next (fAT) * loopSat (fbA);
                    for (int k = 0; k < cells; ++k)
                    {
                        const double r = lA[k].tick (lightA);
                        v = apA[k].tick (v, apCoef ((float) (1.0 / (2.0 * juce::MathConstants<double>::pi * r * cap)), sr));
                    }
                    fbA = v;
                    const float outA = 0.5f * (xa + v) * gain;
                    // phasor B: ingresso da IN A, OUT A (serie) o IN B
                    const float xbIn = (1.0f - par) * xa + par * (numCh > 1 ? inR : mono);
                    const float xb = (1.0f - ser) * xbIn + ser * outA;
                    float w = xb + fbBS.next (fBT) * loopSat (fbB);
                    for (int k = 0; k < cells; ++k)
                    {
                        const double r = lB[k].tick (std::clamp (lightB, 0.0f, 1.0f));
                        w = apB[k].tick (w, apCoef ((float) (1.0 / (2.0 * juce::MathConstants<double>::pi * r * cap)), sr));
                    }
                    fbB = w;
                    const float outB = 0.5f * (xb + w) * gain;
                    if (numCh > 1) { ch[0][i] = outRail (outA); ch[1][i] = outRail (outB); }
                    else ch[0][i] = outRail ((1.0f - ser) * 0.5f * (outA + outB) + ser * outB);   // mono: somma, o OUT B in serie
                }
            }
        private:
            Config cfg;
            double cap = 15e-9, rmin = 1500, rmax = 2e5, ldrA = 6, ldrR = 60;
            float sr = 48000, rLo = 0.1f, rHi = 18, fbMax = 0.8f, gain = 1.25f, fbA = 0, fbB = 0;
            RoleParam pRate1, pRate2, pDepA, pFbA, pDepB, pFbB, pShape, pInB, pSwB, pCv, pSync;
            Allpass1 apA[cells], apB[cells];
            cx::Ldr lA[cells], lB[cells];
            Lfo5 g1, g2;
            Smooth fbAS, fbBS, dAS, dBS, serS, parS, cvS, revS, srcS, r1S, r2S;
        };

        //==============================================================================
        /** Uni-Vibe: stadi a sfasatore con LDR, lampada con inerzia termica. */
        class I5Vibe final : public Effect
        {
        public:
            explicit I5Vibe (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto cs = cfg.list ("caps");
                if (cs.size() < 4) cs = { 15e-9, 220e-9, 470e-12, 4.7e-9 };
                for (int k = 0; k < 4; ++k) caps[k] = (float) cs[(size_t) k];
                rmin = cfg.num ("rmin", 4000); rmax = cfg.num ("rmax", 600000);
                pair5 (cfg, "rate", rLo, rHi);
                lampMs = cfg.num ("lamp", 30); atk = cfg.num ("ldra", 5); rel = cfg.num ("ldrr", 60);
                gC = (float) cfg.num ("gc", 0.92); gE = (float) cfg.num ("ge", 1.0);
                bias = (float) cfg.num ("bias", 0.25);
                pair5 (cfg, "outl", outUnity, outMaxG);
                preG = (float) cfg.num ("pre", 1.4);
                pLevel = role (*this, "outlvl", outUnity); pDepth = role (*this, "depth", 0.6f);
                pRate = role (*this, "rate", 0.4f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                static const float spread[4] = { 1.0f, 1.12f, 0.9f, 1.05f };
                for (int k = 0; k < 4; ++k) ldr[k].set (s, atk * spread[k], rel * spread[k], rmin * spread[k], rmax * spread[k], 0.75f);
                lampA = (float) std::exp (-1.0 / (lampMs * 0.001 * s));
                volS.set (s, 20); wetS.set (s, 15); depS.set (s, 30); rateS.set (s, 40);
                reset();
            }
            void reset() override { for (auto& p : st) p.reset(); lamp = 0; lfo.ph = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float hzT = logMap (pRate.get (*this), rLo, rHi);
                const float depT = pDepth.get (*this);
                const float vol = pLevel.present() ? volKnob (pLevel.get (*this), outUnity, outMaxG) : 1.0f;
                const bool vib = pMode.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    lfo.setHz (sr, rateS.next (hzT));
                    // oscillatore a sfasamento a tre transistor: sinusoide con seconda armonica
                    const float s = lfo.sine() + 0.12f * lfo.sine (0.25) * lfo.sine();
                    lfo.step();
                    const float depth = depS.next (depT);
                    // INTENSITY: ampiezza del pilota della lampada attorno alla polarizzazione; la lampada scalda col quadrato
                    const float drive = std::max (0.0f, bias * (1.0f - depth) + depth * (0.5f + 0.5f * s));
                    lamp = drive * drive + lampA * (lamp - drive * drive);
                    const float x = (numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]) * preG;
                    float v = x;
                    const float light = std::sqrt (std::max (0.0f, lamp));
                    for (int k = 0; k < 4; ++k)
                    {
                        const float R = ldr[k].tick (light);
                        st[k].setFast (sr, 1.0f / (2.0f * pi * R * caps[k]));
                        // sfasatore: emettitore (in fase) sulla LDR, collettore (invertito) sul condensatore
                        const float lp = st[k].lp (v);
                        v = gE * lp - gC * (v - lp);
                    }
                    const float w = wetS.next (vib ? 1.0f : 0.5f);
                    const float y = volS.next (vol) * ((1.0f - w) * x + w * v) * (1.4f - 0.8f * (w - 0.5f)) / preG;
                    for (int cc = 0; cc < numCh; ++cc) ch[cc][i] = outRail (y);
                }
            }
        private:
            Config cfg;
            float caps[4] {}, sr = 48000, rLo = 0.7f, rHi = 11, lampA = 0.99f, lamp = 0, gC = 0.92f, gE = 1, bias = 0.25f,
                  outUnity = 0.6f, outMaxG = 2.0f, preG = 1.4f;
            double rmin = 4000, rmax = 6e5, lampMs = 30, atk = 5, rel = 60;
            RoleParam pLevel, pDepth, pRate, pMode;
            OnePole st[4];
            cx::Ldr ldr[4];
            Lfo lfo;
            Smooth volS, wetS, depS, rateS;
        };

        //==============================================================================
        /** Tremolo a VCA (TR-2), tremolo/pan a due VCA (PN-2), tremolo con TONE. */
        class I5Trem final : public Effect
        {
        public:
            enum Model { Vca, Pan, Tone };
            explicit I5Trem (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "vca");
                model = m == "pan" ? Pan : m == "tone" ? Tone : Vca;
                pair5 (cfg, "rate", rLo, rHi);
                cvHz = cfg.num ("cv", 16);
                pair5 (cfg, "tone", toneLo, toneHi);
                shape = Lfo5::parse (cfg.str ("lfo", "tri"));
                for (auto& t : strList5 (cfg.str ("modes", "tt,ts,pt,ps"))) modeList.push_back (t);
                pRate = role (*this, "rate", 0.5f); pDepth = role (*this, "depth", 0.7f);
                pWave = role (*this, "wave", 0.0f); pMode = role (*this, "mode", 0.0f); pTone = role (*this, "tone", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                rateS.set (s, 40); depS.set (s, 25); wvS.set (s, 25); panS.set (s, 30); sqS.set (s, 30); toneS.set (s, 30);
                cvA.set (s, cvHz); cvB.set (s, cvHz);
                lfo.prepare (s, 1000.0);
                reset();
            }
            void reset() override { lfo.reset (0.25); cvA.reset(); cvB.reset(); toneF.reset(); toneG.reset(); curTone = -1; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float hzT = logMap (pRate.get (*this), rLo, rHi);
                const float depT = pDepth.get (*this);
                const float wvT = pWave.get (*this);
                float panT = 0.0f, sqT = 0.0f;
                if (model == Pan)
                {
                    const int k = std::clamp (pMode.step (*this), 0, (int) modeList.size() - 1);
                    const auto& md = modeList[(size_t) k];
                    panT = md.size() > 0 && md[0] == 'p' ? 1.0f : 0.0f;
                    sqT = md.size() > 1 && md[1] == 's' ? 1.0f : 0.0f;
                }
                const float toneT = model == Tone ? logMap (pTone.get (*this), toneLo, toneHi) : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float hz = rateS.next (hzT), dep = depS.next (depT);
                    const float tri = lfo.tick (hz, shape == Lfo5::Sine ? Lfo5::Sine : shape == Lfo5::Rounded ? Lfo5::Rounded : Lfo5::Tri);
                    float s;
                    if (model == Vca)
                    {
                        const float k = 1.0f + 14.0f * wvS.next (wvT);                 // WAVE: triangolo -> quadra
                        s = std::tanh (k * tri) / std::tanh (k);
                    }
                    else if (model == Pan)
                    {
                        const float q = sqS.next (sqT);
                        const float sq = std::tanh (12.0f * tri) / std::tanh (12.0f);
                        s = (1.0f - q) * tri + q * sq;
                    }
                    else s = tri;
                    // CV del VCA livellato (spigoli della quadra arrotondati)
                    const float gA = cvA.lp (1.0f - dep * (0.5f + 0.5f * s));
                    const float pn = panS.next (panT);
                    const float gB = cvB.lp (1.0f - dep * (0.5f + 0.5f * (s * (1.0f - 2.0f * pn))));   // pan: VCA B in controfase
                    if (model == Tone)
                    {
                        const float f = toneS.next (toneT);
                        if (std::abs (f - curTone) > 0.002f * curTone) { curTone = f; toneF.setFast (sr, f); toneG.setFast (sr, f); }
                    }
                    if (numCh > 1)
                    {
                        float l = ch[0][i] * gA, r = ch[1][i] * gB;
                        if (model == Pan) { const float x = 0.5f * (ch[0][i] + ch[1][i]); l = x * gA; r = x * gB; }
                        if (model == Tone) { l = toneF.lp (l); r = toneG.lp (r); }
                        ch[0][i] = outRail (l); ch[1][i] = outRail (r);
                    }
                    else
                    {
                        float y = ch[0][i] * gA;
                        if (model == Tone) y = toneF.lp (y);
                        ch[0][i] = outRail (y);
                    }
                }
            }
        private:
            Config cfg;
            Model model = Vca;
            float sr = 48000, rLo = 1.1f, rHi = 11.1f, toneLo = 1500, toneHi = 15000, curTone = -1;
            double cvHz = 16;
            Lfo5::Shape shape = Lfo5::Tri;
            std::vector<std::string> modeList;
            RoleParam pRate, pDepth, pWave, pMode, pTone;
            Lfo5 lfo;
            OnePole cvA, cvB, toneF, toneG;
            Smooth rateS, depS, wvS, panS, sqS, toneS;
        };

        //==============================================================================
        /** Delay analogico a BBD lungo. */
        class I5Delay final : public Effect
        {
        public:
            enum MixLaw { Level, Blend, Mix, Cross, Dmm };
            enum Out { Mono, DirOut, DryR, WetR };
            explicit I5Delay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                spec.read (cfg);
                pair5 (cfg, "dly", dLo, dHi);
                fbMax = (float) cfg.num ("fbmax", 0.95);
                fbKnee = (float) cfg.num ("fbknee", 1.0);
                fbHp = cfg.num ("fbhp", 0); fbLp = cfg.num ("fblp", 0);
                const auto law = cfg.str ("law", "level");
                mixLaw = law == "blend" ? Blend : law == "mix" ? Mix : law == "cross" ? Cross : law == "dmm" ? Dmm : Level;
                const auto o = cfg.str ("out", "mono");
                out = o == "dirout" ? DirOut : o == "dryr" ? DryR : o == "wetr" ? WetR : Mono;
                linTaper = cfg.str ("taper", "log") == "lin";
                pair5 (cfg, "modrate", mLo, mHi);
                vibRate = (float) cfg.num ("vibrate", 4.4);
                modDepth = (float) cfg.num ("moddepth", 0.01);
                vibDepth = (float) cfg.num ("vibdepth", modDepth);
                shape = Lfo5::parse (cfg.str ("lfo", "tri"));
                for (auto& w : strList5 (cfg.str ("waves", ""))) waves.push_back (Lfo5::parse (w));
                lfoRound = cfg.num ("lforound", 0);
                timeMs = cfg.num ("timems", 140);
                longK = (float) cfg.num ("long", 2.0);
                pair5 (cfg, "drive", drLo, drHi);
                drSat = (float) cfg.num ("drsat", 2.0);
                pair5 (cfg, "outl", outUnity, outMaxG);
                dmmPre = cfg.num ("dmmpre", 0) > 0.5;
                eqHi = (float) cfg.num ("eqhi", 0); eqLo = (float) cfg.num ("eqlo", 0); eqDb = (float) cfg.num ("eqdb", 12);
                wetGain = (float) cfg.num ("wet", 1.0);
                pTime = role (*this, "time", 0.5f); pFb = role (*this, "feedback", 0.3f);
                pLevel = role (*this, "level", 0.5f); pDepth = role (*this, "depth", 0.0f);
                pRate = role (*this, "rate", 0.4f); pWave = role (*this, "wave", 0.0f); pRange = role (*this, "range", 0.0f);
                pDrive = role (*this, "drive", 0.35f); pOutLvl = role (*this, "outlvl", outUnity); pMode = role (*this, "mode", 0.0f);
                pPre = role (*this, "prelvl", 0.39f); pHigh = role (*this, "high", 0.5f); pLow = role (*this, "low", 0.5f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                line.prepare (s, spec);
                if (fbHp > 0) fbHpF.set (s, fbHp);
                if (fbLp > 0) fbLpF.set (s, fbLp);
                timeS.set (s, timeMs); fbS.set (s, 30); lvlS.set (s, 25); depS.set (s, 30); rateS.set (s, 40); outS.set (s, 25);
                drS.set (s, 25); preS.set (s, 25); hiS.set (s, 30); loS.set (s, 30);
                lfo.prepare (s, lfoRound > 0 ? lfoRound : 80.0);
                lvlPole.set (s, 5800);
                if (eqHi > 0) { hiSh.set (s, eqHi); }
                if (eqLo > 0) { loSh.set (s, eqLo); }
                reset();
            }
            void reset() override
            {
                line.reset(); fbHpF.reset(); fbLpF.reset(); fbState = 0; lfo.reset (0.0); lvlPole.reset(); hiSh.reset(); loSh.reset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool lng = pRange.present() && pRange.step (*this) == 1;
                const float tk = pTime.get (*this);
                const float t0 = linTaper ? dLo + (dHi - dLo) * tk : logMap (tk, dLo, dHi);
                const float tT = lng ? t0 * longK : t0;
                // FEEDBACK: guadagno d'anello <= fbmax (<= 1); 'fbknee' = punto della corsa in cui si arriva al massimo
                const float fk = std::min (1.0f, pFb.get (*this) / fbKnee);
                const float fbT = fbMax * (dmmPre ? taperA (fk) : fk);
                const float lvl = pLevel.get (*this);
                // modulazione del clock
                Lfo5::Shape sh = shape;
                if (pWave.present() && ! waves.empty()) sh = waves[(size_t) std::clamp (pWave.step (*this), 0, (int) waves.size() - 1)];
                float depT = pDepth.present() ? pDepth.get (*this) * modDepth : 0.0f;
                float mHz = pRate.present() ? logMap (pRate.get (*this), mLo, mHi) : mLo;
                if (pMode.present())
                {
                    // VM1: CHORUS = LFO lento (~1 Hz), VIBRATO = veloce e piu' profondo; il pomello e' l'intensita'
                    const bool vib = pMode.step (*this) == 1;
                    mHz = vib ? vibRate : mLo;
                    depT = pDepth.get (*this) * (vib ? vibDepth : modDepth);
                }
                const float outT = pOutLvl.present() ? volKnob (pOutLvl.get (*this), outUnity, outMaxG) : 1.0f;
                const float drT = pDrive.present() ? logMap (pDrive.get (*this), drLo, drHi) : 1.0f;
                const float preT = pPre.get (*this);
                const float hiT = pHigh.present() ? (pHigh.get (*this) - 0.5f) * 2.0f * eqDb : 0.0f;
                const float loT = pLow.present() ? (pLow.get (*this) - 0.5f) * 2.0f * eqDb : 0.0f;
                const float fsr = (float) sr;
                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x0 = 0.5f * (inL + inR);
                    float x = x0;
                    if (dmmPre)
                    {
                        // Deluxe Memory Man: LEVEL = reostato 1MA + 22k nella retroazione dell'operazionale (x0,22..x10,2)
                        const float rf = 22e3f + 1e6f * taperA (preS.next (preT));
                        lvlPole.setFast (fsr, 1.0f / (2.0f * pi * rf * 27e-12f));
                        x = 6.0f * softSat (lvlPole.lp ((rf / 100e3f) * x0) / 6.0f);
                    }
                    if (pDrive.present()) x = drSat * softSat (drS.next (drT) * x / drSat);
                    const float l = lfo.tick (rateS.next (mHz), sh);
                    const float mod = 1.0f + depS.next (depT) * l;
                    const float k = fbS.next (fbT);
                    float fbv = fbState;
                    if (fbHp > 0) fbv = fbHpF.hp (fbv);
                    if (fbLp > 0) fbv = fbLpF.lp (fbv);
                    const float vin = x + loopSat (k / line.peakGain() * fbv);          // limitazione morbida prima della linea
                    const float ya = line.tick (vin, line.clockFor (timeS.next (tT) * mod));
                    fbState = ya;
                    float wet = ya * wetGain;
                    if (eqHi > 0)
                    {
                        // EQ a mensole sulle ripetizioni (HIGH, LOW: centro = piatto)
                        const float gh = std::pow (10.0f, hiS.next (hiT) / 20.0f), gl = std::pow (10.0f, loS.next (loT) / 20.0f);
                        const float lpH = hiSh.lp (wet);
                        wet = lpH + gh * (wet - lpH);
                        const float lpL = loSh.lp (wet);
                        wet = gl * lpL + (wet - lpL);
                    }
                    const float lv = lvlS.next (lvl);
                    float dryG = 1.0f, wetG;
                    switch (mixLaw)
                    {
                        case Blend: dryG = 1.0f - 0.5f * lv; wetG = lv; break;
                        case Mix:   wetG = lv; break;
                        case Cross: dryG = 1.0f - lv; wetG = lv; break;
                        case Dmm:   dryG = 1.0f - lv; wetG = 0.75f * lv; break;
                        case Level: default: wetG = 1.1f * lv; break;
                    }
                    const float og = outS.next (outT);
                    const float o = outRail ((dryG * x + wetG * wet) * og);
                    float L = o, R = o;
                    if (numCh > 1)
                    {
                        switch (out)
                        {
                            case DirOut: L = outRail (wetG * wet * og); R = x0; break;   // VD400: con DIR OUT collegata, OUT = solo eco
                            case DryR:   R = x0; break;
                            case WetR:   R = outRail (wet * og); break;                 // DELAY OUT: solo ritardato
                            case Mono: default: break;
                        }
                    }
                    ch[0][i] = L;
                    if (numCh > 1) ch[1][i] = R;
                }
            }
        private:
            Config cfg;
            Line5::Spec spec;
            Line5 line;
            OnePole fbHpF, fbLpF, lvlPole, hiSh, loSh;
            double sr = 48000, fbHp = 0, fbLp = 0, timeMs = 140, lfoRound = 0;
            float dLo = 10, dHi = 300, fbMax = 0.95f, fbKnee = 1.0f, mLo = 1.0f, mHi = 1.0f, vibRate = 4.4f, modDepth = 0.01f, vibDepth = 0.01f,
                  longK = 2.0f, drLo = 1, drHi = 1, drSat = 2.0f, outUnity = 0.5f, outMaxG = 2.0f, eqHi = 0, eqLo = 0, eqDb = 12, wetGain = 1,
                  fbState = 0;
            bool linTaper = false, dmmPre = false;
            MixLaw mixLaw = Level;
            Out out = Mono;
            Lfo5::Shape shape = Lfo5::Tri;
            std::vector<Lfo5::Shape> waves;
            RoleParam pTime, pFb, pLevel, pDepth, pRate, pWave, pRange, pDrive, pOutLvl, pMode, pPre, pHigh, pLow;
            Lfo5 lfo;
            Smooth timeS, fbS, lvlS, depS, rateS, outS, drS, preS, hiS, loS;
        };
    }

    std::unique_ptr<Effect> makeI5Mod (const ModelDef& d, const std::string& type)
    {
        if (type == "i5bbd")     return std::make_unique<I5Bbd> (d);
        if (type == "i5phase")   return std::make_unique<I5Phaser> (d);
        if (type == "i5biphase") return std::make_unique<I5BiPhase> (d);
        if (type == "i5vibe")    return std::make_unique<I5Vibe> (d);
        if (type == "i5trem")    return std::make_unique<I5Trem> (d);
        if (type == "i5delay")   return std::make_unique<I5Delay> (d);
        return nullptr;
    }
}
