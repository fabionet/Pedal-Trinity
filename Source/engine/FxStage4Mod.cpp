/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 4 (Ibanez): phaser, chorus, flanger, tremolo/pan e delay BBD ("type=i4m...").

    Quattro tipi, configurati per pedale con i valori del circuito reale:
      * i4mphase - phaser a celle passa-tutto del 1° ordine con l'elemento variabile vero:
                   JFET (conduttanza lineare nella tensione di gate, in parallelo alla R fissa
                   della cella: fc = (1/R + u/Ron) / 2 pi C), fotoresistenza CdS illuminata dal
                   LED/lampada (inerzia asimmetrica della LDR) oppure OTA (fc lineare con la
                   corrente). Numero di celle per posizione del selettore, miscela in fase o
                   invertita, retroazione filtrata e limitata (guadagno d'anello < 1), LFO
                   triangolare / sinusoidale / a rilassamento RC / quadro / audio, rete RC del
                   gate che smussa i salti. Varianti: sezione "jet" del Jetlyzer (risonatore a
                   banda stretta aperto dall'inviluppo tramite fotoaccoppiatore) e panner
                   stereo del Flying Pan (secondo LFO, selettore L/STEREO/R).
      * i4mbbd   - chorus/flanger/vibrato a BBD con il clock reale: ritardo = N / (2 f_clk),
                   VCO lineare (convertitore tensione-corrente) o esponenziale, uno o due LFO
                   sommati, pre/de-enfasi, compander, diodi limitatori, filtri anti-alias e di
                   ricostruzione dello schema, rumore della linea, gate del ramo ritardato,
                   uscite mono / diretto+effetto e diretto-effetto / effetto+diretto separati.
                   Modelli speciali: doppio chorus a due sezioni (PC10), Airplane Flanger con il
                   modo TAKEOFF, doppio flanger con INTENSE/FILTER (FL99), rotary chorus (RC99),
                   chorus a doppio sweep con 4 modi (EC40).
      * i4mdelay - delay analogico a BBD lungo (MN3005/MN3205/V3208...) con compander,
                   enfasi, filtri, retroazione filtrata e limitata prima di rientrare nella
                   linea, uscita DRY separata; ES2 (modulazione e autooscillazione limitata) ed
                   ES3 ibrido (linea BBD o digitale con dissolvenza, INST/LINE).
      * i4mtrem  - tremolo a VCA (BA6110) con triangolo arrotondato dai diodi, tremolo a forma
                   variabile (triangolo -> quadra), auto-pan stereo a due attenuatori in
                   controfase.

    Tutta la memoria si alloca in prepare(); i comandi continui sono lisciati (niente click).
*/

#include "FxStage4.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        //==============================================================================
        /** Mensola di pre-enfasi del 1° ordine (zero fz, polo fp) e de-enfasi inversa esatta. */
        struct Emphasis
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

        /** Cascata di filtri passa-basso (SVF di 2° ordine o RC) con frequenze scalabili campione per
            campione (filtri che seguono il clock nei delay a microcontrollore). */
        struct LpChain
        {
            static constexpr int maxN = 4;
            Svf svf[maxN];
            OnePole rc[maxN];
            float f[maxN] {}, k[maxN] {};
            bool isRc[maxN] {};
            int n = 0;
            float curScale = -1.0f, sr = 48000;
            /** "f:q,f:q,..." (q = 0 -> polo RC). */
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
            /** Massimo del modulo della cascata analogica (picchi delle celle con Q > 0,707), fuori dal thread audio. */
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

        /** Compander NE570/SA571 nella topologia reale: il compressore ha la cella di guadagno nella
            retroazione e il raddrizzatore sulla SUA uscita (g = ref / env(uscita)), l'espansore misura il
            proprio ingresso (g = env(ingresso) / ref). Le due rilevazioni vedono lo stesso segnale (prima e
            dopo la linea): i guadagni si annullano anche nei transitori, cosi' la retroazione del delay
            resta sotto l'unita'. 2:1 / 1:2 a regime, guadagno massimo del compressore 30 (29,5 dB). */
        struct Compander570
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

        /** Diodi al silicio verso massa / in retroazione: limitazione morbida a +-v. */
        inline float diodeClamp (float x, float v) noexcept { return v * std::tanh (x / v); }

        //==============================================================================
        /** Catena BBD configurabile: enfasi -> compressore -> diodi -> anti-alias -> BBD (clock
            reale) -> ricostruzione -> espansore -> de-enfasi -> blocco della continua. */
        class I4Line
        {
        public:
            struct Spec
            {
                int stages = 1024;
                std::string aa = "9000:0.707", rc = "9000:0.5412,9000:1.3066";
                double emZ = 0, emP = 0, ctauC = 10, ctauE = 10, trackK = 0, dcHz = 20;
                bool comp = false;
                float clamp = 0, sat = 1.5f, noise = 2.0e-4f, loss = 6.0e-5f;

                void read (const Config& c, const char* prefix = "")
                {
                    auto key = [&] (const char* k) { static thread_local std::string s; s = std::string (prefix) + k; return s.c_str(); };
                    stages = (int) c.num (key ("stages"), stages);
                    aa = c.str (key ("aa"), aa.c_str());
                    rc = c.str (key ("rc"), rc.c_str());
                    auto em = c.list (key ("emph"));
                    if (em.size() >= 2) { emZ = em[0]; emP = em[1]; }
                    comp = c.num (key ("comp"), comp ? 1 : 0) > 0.5;
                    auto ct = c.list (key ("ctau"));
                    if (ct.size() >= 1) ctauC = ct[0];
                    ctauE = ct.size() >= 2 ? ct[1] : ctauC;
                    clamp = (float) c.num (key ("clamp"), clamp);
                    sat = (float) c.num (key ("sat"), sat);
                    noise = (float) c.num (key ("noise"), noise);
                    loss = (float) c.num (key ("loss"), loss);
                    trackK = c.num (key ("track"), trackK);
                    dcHz = c.num (key ("dc"), dcHz);
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
                peakG = (float) (aa.peak() * rc.peak());
                reset();
            }
            void reset() { bbd.clear(); aa.reset(); rc.reset(); em.reset(); comp.reset(); dc.reset(); }
            int numStages() const noexcept { return stages; }
            /** Guadagno massimo della catena (filtri risonanti): la retroazione si divide per questo valore. */
            float peakGain() const noexcept { return peakG; }
            double clockFor (double ms) const noexcept { return (double) stages / (2.0 * std::max (0.02, ms) * 0.001); }

            /** Un campione con il clock clkHz. */
            inline float tick (float x, double clkHz) noexcept
            {
                if (spec.trackK > 0)
                {
                    // filtri riprogettati dal microcontrollore insieme al clock: taglio <= k * f_clk
                    const float s = std::min (1.0f, (float) (spec.trackK * clkHz) / aa.first());
                    aa.setScale (s); rc.setScale (s);
                }
                float v = em.emph (x);
                if (spec.comp) v = comp.compress (v);
                if (spec.clamp > 0) v = diodeClamp (v, spec.clamp);
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
            LpChain aa, rc;
            Emphasis em;
            Compander570 comp;
            OnePole dc;
        };

        /** LFO con le forme dei circuiti: triangolo (integratore + comparatore), sinusoide (oscillatore a
            sfasamento), rilassamento RC (lati curvi), quadra (uscita del comparatore). */
        struct I4Lfo
        {
            enum Shape { Tri, Sine, Relax, Square, Rounded };
            Lfo lfo;
            RelaxOsc relax;
            Shape shape = Tri;
            float relaxH = 0.3f;
            static Shape parse (const std::string& s)
            {
                return s == "sine" ? Sine : s == "relax" ? Relax : s == "square" ? Square : s == "round" ? Rounded : Tri;
            }
            void reset (double ph = 0.25) { lfo.ph = ph; relax.v = 0.5f; relax.up = true; }
            inline float tick (double sr, double hz, Shape sh) noexcept
            {
                float y;
                switch (sh)
                {
                    case Sine:    lfo.setHz (sr, hz); y = lfo.sine(); lfo.step(); break;
                    case Square:  lfo.setHz (sr, hz); y = lfo.square(); lfo.step(); break;
                    case Relax:   relax.set (sr, hz, relaxH); y = relax.tick(); break;
                    case Rounded: lfo.setHz (sr, hz); y = std::sin (1.5707963f * lfo.tri()); lfo.step(); break;
                    case Tri:
                    default:      lfo.setHz (sr, hz); y = lfo.tri(); lfo.step(); break;
                }
                return y;
            }
        };

        /** Lista di stringhe separate da virgole (fuori dal thread audio). */
        std::vector<std::string> strList (const std::string& s)
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

        //==============================================================================
        /** Phaser a celle passa-tutto con l'elemento variabile reale. */
        class I4Phaser final : public Effect
        {
        public:
            enum Cell { Jfet, Ldr, Ota };
            static constexpr int maxStages = 12;

            explicit I4Phaser (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto c = cfg.str ("cell", "jfet");
                cell = c == "ldr" ? Ldr : c == "ota" ? Ota : Jfet;
                stageList = cfg.list ("stages"); if (stageList.empty()) stageList = { 4 };
                invList = cfg.list ("inv"); if (invList.empty()) invList = { 0 };
                cap = cfg.num ("c", 47e-9); rFix = cfg.num ("r", 30000); ron = cfg.num ("ron", 1500);
                auto f = cfg.list ("f"); fLo = f.size() > 0 ? f[0] : 60; fHi = f.size() > 1 ? f[1] : 5000;
                rpar = cfg.num ("rpar", 100000); rmin = cfg.num ("rmin", 4000); rmax = cfg.num ("rmax", 1.0e6);
                ldrA = cfg.num ("la", 8); ldrR = cfg.num ("lr", 60);
                auto u = cfg.list ("u"); uLo = u.size() > 0 ? (float) u[0] : 0.03f; uHi = u.size() > 1 ? (float) u[1] : 1.0f;
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.1f; rHi = rt.size() > 1 ? (float) rt[1] : 8.0f;
                auto r2 = cfg.list ("rate2"); r2Lo = r2.size() > 0 ? (float) r2[0] : rLo; r2Hi = r2.size() > 1 ? (float) r2[1] : rHi;
                auto pr = cfg.list ("prate"); pLo = pr.size() > 0 ? (float) pr[0] : 0.25f; pHi = pr.size() > 1 ? (float) pr[1] : 8.0f;
                for (auto& w : strList (cfg.str ("waves", cfg.str ("lfo", "tri").c_str()))) waves.push_back (I4Lfo::parse (w));
                auto wm = cfg.list ("wmul"); for (auto v : wm) waveMul.push_back ((float) v);
                while (waveMul.size() < waves.size()) waveMul.push_back (1.0f);
                lfo.relaxH = (float) cfg.num ("relax", 0.3);
                fbMax = (float) cfg.num ("fb", 0.0); fbSign = (float) cfg.num ("fbsign", 1); fbBip = cfg.num ("fbbip", 0) > 0.5;
                fbHp = cfg.num ("fbhp", 0); fbLp = cfg.num ("fblp", 0);
                auto mx = cfg.list ("mix"); mixD = mx.size() > 0 ? (float) mx[0] : 0.5f; mixW = mx.size() > 1 ? (float) mx[1] : 0.5f;
                gain = (float) cfg.num ("gain", 1.25); wetHp = cfg.num ("wethp", 0); wetLp = cfg.num ("wetlp", 0);
                inGain = (float) cfg.num ("ingain", 1.0); rail = (float) cfg.num ("rail", 3.5);
                spread = (float) cfg.num ("spread", 0.02); gateMs = cfg.num ("gate", 2.0);
                jet = cfg.num ("jet", 0) > 0.5; pan = cfg.num ("pan", 0) > 0.5;
                auto jf = cfg.list ("jetf"); jLo = jf.size() > 0 ? (float) jf[0] : 250.0f; jHi = jf.size() > 1 ? (float) jf[1] : 2500.0f;
                jetQ = (float) cfg.num ("jetq", 10);
                pRate = role (*this, "rate", 0.35f);
                pDepth = role (*this, "depth", 1.0f);
                pRes = role (*this, "res", fbBip ? 0.5f : 0.0f);
                pLevel = role (*this, "level", 0.5f);
                pManual = role (*this, "manual", 0.5f);
                pStages = role (*this, "stages", 0.0f);
                pWave = role (*this, "wave", 0.0f);
                pRange = role (*this, "range", 0.0f);
                pSelect = role (*this, "select", 1.0f);
                pPanRate = role (*this, "rate2", 0.4f);
                pJetMix = role (*this, "jetmix", 0.5f);
                pJetTone = role (*this, "jettone", 0.5f);
                pJet = role (*this, "jet", 1.0f);
            }

            void prepare (double s, int) override
            {
                sr = (float) s;
                for (auto* sm : { &fbS, &lvlS, &invS, &jetS, &jetGS, &selLS, &selRS }) sm->set (s, 20);
                rateS.set (s, 40); prateS.set (s, 40); manS.set (s, 25); depS.set (s, 25);
                uS.set (s, gateMs);
                ldr.set (s, ldrA, ldrR, rmin, rmax, 0.75f);
                if (fbHp > 0) fbHpF.set (s, fbHp);
                if (fbLp > 0) fbLpF.set (s, fbLp);
                if (wetHp > 0) wetHpF.set (s, wetHp);
                if (wetLp > 0) wetLpF.set (s, wetLp);
                stageX.prepare (s, 10);
                env.set (s, 4, 140);
                reset();
            }
            void reset() override
            {
                for (auto& a : ap) a.reset();
                fbState = 0; fbHpF.reset(); fbLpF.reset(); wetHpF.reset(); wetLpF.reset(); jetBp.reset();
                lfo.reset (0.25); panLfo.reset (0.0);
                env.env = 0;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int sel = pStages.present() ? std::clamp (pStages.step (*this), 0, (int) stageList.size() - 1) : 0;
                const int wantStages = (int) std::clamp (stageList[(size_t) sel], 2.0, (double) maxStages);
                const float invT = invList[(size_t) std::min (sel, (int) invList.size() - 1)] > 0.5 ? 1.0f : 0.0f;
                const int wsel = pWave.present() ? std::clamp (pWave.step (*this), 0, (int) waves.size() - 1) : 0;
                const I4Lfo::Shape shape = waves.empty() ? I4Lfo::Tri : waves[(size_t) wsel];
                const float wmul = waveMul.empty() ? 1.0f : waveMul[(size_t) wsel];
                const bool fast = pRange.present() && pRange.step (*this) == 1;
                const float hzT = logMap (pRate.get (*this), fast ? r2Lo : rLo, fast ? r2Hi : rHi) * wmul;
                const float depthT = pDepth.get (*this);
                float fbT = pRes.get (*this);
                fbT = fbBip ? (fbT - 0.5f) * 2.0f * fbMax : fbT * fbMax * fbSign;
                const float lvlT = pLevel.present() ? 2.0f * pLevel.get (*this) : 1.0f;
                const float manT = pManual.present() ? taperA (pManual.get (*this)) : -1.0f;
                // Flying Pan: selettore LEFT / STEREO / RIGHT del segnale sfasato
                const int ps = pan ? std::clamp (pSelect.step (*this), 0, 2) : 1;
                const float selLT = ps != 2 ? 1.0f : 0.0f, selRT = ps != 0 ? 1.0f : 0.0f;
                const float pHz = logMap (pPanRate.get (*this), pLo, pHi);
                // Jetlyzer: risonatore del "jet"
                const float jetOn = jet && pJet.step (*this) == 1 ? 1.0f : 0.0f;
                const float jfT = jet ? logMap (1.0f - pJetTone.get (*this), jLo, jHi) : 1000.0f;   // 5k C: a destra piu' grave
                const float uMid = 0.5f * (uLo + uHi), uHalf = 0.5f * (uHi - uLo);

                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float xg = stageX.tick (wantStages, changed);
                    if (changed) nst = stageX.current;
                    const float l = lfo.tick (sr, rateS.next (hzT), shape);
                    const float dep = depS.next (depthT);
                    float u;
                    if (manT >= 0) u = manS.next (manT) + 0.5f * dep * l;           // OTA con MANUAL (PM7)
                    else u = uMid + uHalf * dep * l;
                    u = uS.next (std::clamp (u, 0.0f, 1.0f));                       // rete RC del gate / del pilota

                    // frequenza d'angolo della cella
                    float fc;
                    if (cell == Jfet) fc = (float) ((1.0 / rFix + (double) u / ron) / (2.0 * juce::MathConstants<double>::pi * cap));
                    else if (cell == Ota) fc = (float) (fLo + (fHi - fLo) * (double) u);
                    else
                    {
                        const float rl = ldr.tick (u);                              // luce del LED -> resistenza CdS
                        const double r = (double) rl * rpar / ((double) rl + rpar);
                        fc = (float) (1.0 / (2.0 * juce::MathConstants<double>::pi * r * cap));
                    }

                    const float x = (numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]) * inGain;
                    float fbv = fbState;
                    if (fbHp > 0) fbv = fbHpF.hp (fbv);
                    if (fbLp > 0) fbv = fbLpF.lp (fbv);
                    float v = x + fbS.next (fbT) * loopSat (fbv);
                    v = rail * softSat (v / rail);                                  // binari degli operazionali
                    for (int s = 0; s < nst; ++s)
                    {
                        static const float dev[maxStages] = { -1.0f, 0.6f, -0.3f, 0.9f, -0.7f, 0.2f, 0.5f, -0.9f, 0.1f, -0.4f, 0.8f, -0.2f };
                        v = ap[s].tick (v, apCoef (fc * (1.0f + spread * dev[s]), sr));
                    }
                    fbState = v;
                    float w = v;
                    if (wetHp > 0) w = wetHpF.hp (w);
                    if (wetLp > 0) w = wetLpF.lp (w);
                    w *= xg;                                                        // cambio del numero di celle: dissolvenza
                    const float inv = invS.next (invT);
                    const float wl = mixW * lvlS.next (lvlT) * (1.0f - 2.0f * inv);
                    float y = gain * (mixD * x + wl * w) / inGain;
                    if (jet)
                    {
                        const float jf = jetS.next (jfT);                           // frequenza lisciata: niente salti dei coefficienti
                        if (std::abs (jf - jetCur) > 0.002f * jetCur) { jetCur = jf; jetBp.setG (fastTanPi (jf, sr), 1.0f / jetQ); }
                        const float e = std::min (1.0f, env.tick (x) * 6.0f);       // LED del fotoaccoppiatore dall'inviluppo
                        const float jg = jetGS.next (jetOn * pJetMix.get (*this)) * e;
                        y += jg * jetBp.bp (w);
                    }
                    if (pan && numCh > 1)
                    {
                        const float pl = panLfo.tick (sr, prateS.next (pHz), I4Lfo::Sine);
                        const float th = 0.7853982f * (1.0f + 0.95f * pl);              // attenuatori in controfase (potenza costante)
                        const float gl = std::sin (th), gr = std::cos (th);
                        const float dryOut = x / inGain;
                        const float sl = selLS.next (selLT), srr = selRS.next (selRT);
                        ch[0][i] = gl * (sl * y + (1.0f - sl) * dryOut);
                        ch[1][i] = gr * (srr * y + (1.0f - srr) * dryOut);
                        continue;
                    }
                    if (pan)
                    {
                        // uscita mono (solo OUTPUT L): tremolo del panner
                        const float pl = panLfo.tick (sr, prateS.next (pHz), I4Lfo::Sine);
                        const float sl = selLS.next (selLT);
                        y = std::sin (0.7853982f * (1.0f + 0.95f * pl)) * (sl * y + (1.0f - sl) * x / inGain);
                    }
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }

        private:
            Config cfg;
            Cell cell = Jfet;
            std::vector<double> stageList, invList;
            std::vector<I4Lfo::Shape> waves;
            std::vector<float> waveMul;
            double cap = 47e-9, rFix = 30000, ron = 1500, fLo = 60, fHi = 5000, rpar = 1e5, rmin = 4000, rmax = 1e6, ldrA = 8, ldrR = 60,
                   fbHp = 0, fbLp = 0, wetHp = 0, wetLp = 0, gateMs = 2;
            float sr = 48000, uLo = 0.03f, uHi = 1, rLo = 0.1f, rHi = 8, r2Lo = 0.1f, r2Hi = 8, pLo = 0.25f, pHi = 8, fbMax = 0, fbSign = 1,
                  mixD = 0.5f, mixW = 0.5f, gain = 1.25f, inGain = 1, rail = 3.5f, spread = 0.02f, jLo = 250, jHi = 2500, jetQ = 10, jetCur = -1, fbState = 0;
            bool fbBip = false, jet = false, pan = false;
            int nst = 4;
            RoleParam pRate, pDepth, pRes, pLevel, pManual, pStages, pWave, pRange, pSelect, pPanRate, pJetMix, pJetTone, pJet;
            Allpass1 ap[maxStages];
            I4Lfo lfo, panLfo;
            cx::Ldr ldr;
            OnePole fbHpF, fbLpF, wetHpF, wetLpF;
            Svf jetBp;
            Envelope env;
            XFader stageX;
            Smooth rateS, prateS, manS, depS, uS, fbS, lvlS, invS, jetS, jetGS, selLS, selRS;
        };

        //==============================================================================
        /** Modulazioni a BBD (chorus, flanger, vibrato) con il clock reale. */
        class I4Bbd final : public Effect
        {
        public:
            enum Model { Chorus, Flanger, Dual, Af2, Fl99, Rc99, Ec40 };
            enum Out { Mono, Inv, DryR, Cs505 };

            explicit I4Bbd (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "chorus");
                model = m == "flanger" ? Flanger : m == "dual" ? Dual : m == "af2" ? Af2 : m == "fl99" ? Fl99 : m == "rc99" ? Rc99
                      : m == "ec40" ? Ec40 : Chorus;
                const auto o = cfg.str ("out", "mono");
                out = o == "inv" ? Inv : o == "dryr" ? DryR : o == "cs505" ? Cs505 : Mono;
                specA.read (cfg); specB = specA; specB.read (cfg, "b");
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? dl[0] : 3.0; dHi = dl.size() > 1 ? dl[1] : 9.0;
                auto dl2 = cfg.list ("bdly"); bLo = dl2.size() > 0 ? dl2[0] : dLo; bHi = dl2.size() > 1 ? dl2[1] : dHi;
                expVco = cfg.str ("vco", model == Flanger || model == Af2 || model == Fl99 ? "lin" : "exp") == "exp";
                ctr = (float) cfg.num ("ctr", 0.5);
                swing = (float) cfg.num ("swing", 0.5); swing2 = (float) cfg.num ("swing2", swing);
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.3f; rHi = rt.size() > 1 ? (float) rt[1] : 3.0f;
                auto r2 = cfg.list ("rate2"); r2Lo = r2.size() > 0 ? (float) r2[0] : rLo; r2Hi = r2.size() > 1 ? (float) r2[1] : rHi;
                shape = I4Lfo::parse (cfg.str ("lfo", "tri"));
                fbMax = (float) cfg.num ("fbmax", 0.0); fbSign = (float) cfg.num ("fbsign", 1.0);
                auto mx = cfg.list ("mix"); mixD = mx.size() > 0 ? (float) mx[0] : 0.6f; mixW = mx.size() > 1 ? (float) mx[1] : 0.6f;
                gateThr = (float) cfg.num ("gate", 0.0);
                wetHp = cfg.num ("wethp", 0);
                outMax = (float) cfg.num ("outmax", 2.0);
                pRate = role (*this, "rate", 0.4f); pDepth = role (*this, "depth", 0.5f);
                pRate2 = role (*this, "rate2", 0.4f); pDepth2 = role (*this, "depth2", 0.0f);
                pManual = role (*this, "manual", 0.5f); pRes = role (*this, "res", 0.0f);
                pLevel = role (*this, "level", 0.5f); pOutLvl = role (*this, "outlvl", 0.5f);
                pMode = role (*this, "mode", 0.0f); pFilter = role (*this, "filter", 0.0f);
                pIntense = role (*this, "intense", 0.0f); pTake = role (*this, "rate3", 0.5f);
            }

            void prepare (double s, int) override
            {
                sr = s;
                lineA.prepare (s, specA);
                lineB.prepare (s, specB);
                for (auto* sm : { &fbS, &lvlS, &outS, &gA, &gB, &dryS, &wetS, &rotS }) sm->set (s, 25);
                rateS.set (s, 40); rate2S.set (s, 40); rate3S.set (s, 40); manS.set (s, 30); depS.set (s, 30); dep2S.set (s, 30);
                posA.set (s, 1.5); posB.set (s, 1.5);
                gateEnv.set (s, 1.0, 30.0); gateS.set (s, 60);
                if (wetHp > 0) { whA.set (s, wetHp); whB.set (s, wetHp); }
                reset();
            }
            void reset() override
            {
                lineA.reset(); lineB.reset(); fbA = fbB = 0;
                lfo1.reset (0.25); lfo2.reset (0.6); lfo3.reset (0.0);
                gateOpen = 0; gateEnv.env = 0; whA.reset(); whB.reset();
            }

            /** Posizione 0..1 (0 = ritardo minimo) -> clock della linea. */
            inline double clockAt (const I4Line& L, double lo, double hi, float p) const noexcept
            {
                p = std::clamp (p, 0.0f, 1.0f);
                if (expVco) return L.clockFor (lo * std::pow (hi / lo, (double) p));
                const double fmax = L.clockFor (lo), fmin = L.clockFor (hi);
                return fmax - (fmax - fmin) * (double) p;                          // VCO lineare: ritardo iperbolico
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int mode = pMode.present() ? pMode.step (*this) : 0;
                const float hz1T = logMap (pRate.get (*this), rLo, rHi);
                const float hz2T = logMap (pRate2.get (*this), r2Lo, r2Hi);
                const float dep1T = pDepth.get (*this), dep2T = pDepth2.present() ? pDepth2.get (*this) : 0.0f;
                const float manT = pManual.present() ? pManual.get (*this) : ctr;
                float fbT = pRes.get (*this) * fbMax * fbSign;
                float wetT = pLevel.present() ? 2.0f * pLevel.get (*this) * mixW : mixW;
                float dryT = mixD;
                const float outT = pOutLvl.present() ? volKnob (pOutLvl.get (*this), 0.5f, 2.0f) : 1.0f;
                // sezioni A/B (PC10), modi FL99/AF2/EC40
                float gAT = 1.0f, gBT = 0.0f;
                bool freeze = false, intense = true, takeoff = false;
                float hz3T = 0;
                int ecMode = 0;
                switch (model)
                {
                    case Dual:   gAT = mode == 2 ? 0.0f : 1.0f; gBT = mode == 0 ? 0.0f : 1.0f; fbT = pRes.get (*this) * fbMax; break;
                    case Fl99:   freeze = pFilter.step (*this) == 1; intense = pIntense.step (*this) == 1; gBT = 1.0f; break;
                    case Af2:
                        takeoff = mode == 1;
                        hz3T = logMap (pTake.get (*this), 0.04f, 1.5f);
                        if (takeoff) fbT = std::min (fbMax, fbT + 0.25f * fbMax);
                        break;
                    case Ec40:   ecMode = std::clamp (mode, 0, 3); break;
                    case Chorus: case Flanger: case Rc99: default: break;
                }
                const float rotT = model == Rc99 ? std::clamp ((pRate.get (*this) - 0.6f) * 2.5f, 0.0f, 1.0f) : 0.0f;
                // FL99: INTENSE spazza tutta la gamma, NORMAL solo la parte bassa-media (ritardi lunghi)
                const float pMin = (model == Fl99 && ! intense) ? 0.45f : 0.0f;

                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    const float hz1 = rateS.next (hz1T), hz2 = rate2S.next (hz2T);
                    float l1 = freeze ? 0.0f : lfo1.tick (sr, hz1, shape);
                    float l2 = 0.0f;
                    if (model == Fl99) l2 = -l1;                                    // seconda linea in controfase
                    else if (pRate2.present()) l2 = lfo2.tick (sr, hz2, shape);
                    const float d1 = depS.next (dep1T), d2 = dep2S.next (dep2T), man = manS.next (manT);
                    float pA = 0.5f, pB = 0.5f;
                    double loA = dLo, hiA = dHi, loB = bLo, hiB = bHi;
                    switch (model)
                    {
                        case Chorus: case Rc99:
                            // D-TIME sposta il centro; le due sezioni d'onda (BC9/TC10) si sommano sullo stesso clock
                            pA = man + swing * d1 * l1 + swing2 * d2 * l2; break;
                        case Flanger:
                            pA = man + swing * d1 * l1; break;
                        case Dual:
                            pA = 0.5f + swing * d1 * l1;                                // sezione A: chorus 3,2-8,5 ms
                            pB = man + swing2 * d2 * l2; break;                         // sezione B: DELAY TIME + WIDTH B
                        case Fl99:
                        {
                            const float c = pMin + (1.0f - pMin) * man;
                            pA = c + swing * d1 * (1.0f - pMin) * l1;
                            pB = c + swing * d1 * (1.0f - pMin) * l2;
                            break;
                        }
                        case Af2:
                        {
                            const float c = 1.0f - man;                                // MANUAL a destra = ritardo corto
                            if (takeoff)
                            {
                                const float dive = lfo3.lfo.skew (0.9f);               // lenta salita del ritardo, ritorno rapido
                                lfo3.lfo.setHz (sr, rate3S.next (hz3T)); lfo3.lfo.step();
                                pA = c + swing * d1 * (0.5f * l1) + 0.45f * d1 * dive;
                            }
                            else pA = c + swing * d1 * l1;
                            break;
                        }
                        case Ec40:
                        {
                            const float deep = ecMode == 2 ? 2.0f : 1.0f;
                            pA = man + swing * d1 * deep * l1 + (ecMode == 1 || ecMode == 3 ? swing2 * d1 * l2 : 0.0f);
                            break;
                        }
                        default: break;
                    }
                    pA = posA.next (std::clamp (pA, 0.0f, 1.0f));
                    pB = posB.next (std::clamp (pB, 0.0f, 1.0f));

                    // gate del ramo ritardato (JFET comandato dall'inviluppo dell'ingresso)
                    float gate = 1.0f;
                    if (gateThr > 0)
                    {
                        const float e = gateEnv.tick (x);
                        if (e > gateThr * 1.5f) gateOpen = 1.0f; else if (e < gateThr) gateOpen = 0.0f;
                        gate = gateS.next (gateOpen);
                    }
                    const float fb = fbS.next (fbT) / lineA.peakGain();
                    const float a = lineA.tick (x + (model == Dual ? 0.0f : loopSat (fb * fbA)), clockAt (lineA, loA, hiA, pA));
                    fbA = a;
                    float b = 0.0f;
                    const float gb = gB.next (gBT);
                    if (model == Dual || model == Fl99)
                    {
                        b = lineB.tick (x + loopSat (fb * fbB), clockAt (lineB, model == Dual ? loB : loA, model == Dual ? hiB : hiA, pB));
                        fbB = b;
                    }
                    float wA = a, wB = b;
                    if (wetHp > 0) { wA = whA.hp (wA); wB = whB.hp (wB); }
                    float wet;
                    const float ga = gA.next (gAT);
                    if (model == Dual) wet = ga * wA + gb * wB;                     // A, A+B, B (FLANGE solo sulla sezione B)
                    else if (model == Fl99) wet = 0.5f * (wA + wB);
                    else wet = wA;
                    wet *= gate * wetS.next (wetT);
                    const float dry = dryS.next (dryT);
                    const float og = outS.next (outT);
                    float L, R;
                    switch (out)
                    {
                        case Inv:  L = dry * x + wet; R = dry * x - wet; break;       // uscita normale e invertita
                        case Cs505:
                            if (numCh > 1) { L = wet; R = x; }                       // due jack: 1 = solo chorus, 2 = diretto
                            else { L = R = dry * x + wet; }
                            break;
                        case DryR: L = dry * x + wet; R = x; break;
                        case Mono: default: L = R = dry * x + wet; break;
                    }
                    if (model == Rc99)
                    {
                        // zona "rotary": ampiezza in controfase sulle due uscite, piu' marcata ad alta velocita'
                        const float rk = rotS.next (rotT);
                        const float am = 0.35f * rk * lfo1.lfo.sine();
                        L *= 1.0f + am; R *= 1.0f - am;
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
            I4Line::Spec specA, specB;
            I4Line lineA, lineB;
            double sr = 48000, dLo = 3, dHi = 9, bLo = 3, bHi = 9, wetHp = 0;
            bool expVco = true;
            float ctr = 0.5f, swing = 0.5f, swing2 = 0.5f, rLo = 0.3f, rHi = 3, r2Lo = 0.3f, r2Hi = 3, fbMax = 0, fbSign = 1, mixD = 0.6f, mixW = 0.6f,
                  gateThr = 0, gateOpen = 0, outMax = 2.0f, fbA = 0, fbB = 0;
            I4Lfo::Shape shape = I4Lfo::Tri;
            RoleParam pRate, pDepth, pRate2, pDepth2, pManual, pRes, pLevel, pOutLvl, pMode, pFilter, pIntense, pTake;
            I4Lfo lfo1, lfo2, lfo3;
            Envelope gateEnv;
            OnePole whA, whB;
            Smooth fbS, lvlS, outS, gA, gB, dryS, wetS, rotS, rateS, rate2S, rate3S, manS, depS, dep2S, posA, posB, gateS;
        };

        //==============================================================================
        /** Delay analogico a BBD lungo (con ES2/ES3). */
        class I4Delay final : public Effect
        {
        public:
            enum MixLaw { Level, Blend, Mix };
            explicit I4Delay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                spec.read (cfg);
                auto dl = cfg.list ("dly"); dLo = dl.size() > 0 ? (float) dl[0] : 10.0f; dHi = dl.size() > 1 ? (float) dl[1] : 300.0f;
                auto dd = cfg.list ("ddly"); ddLo = dd.size() > 0 ? (float) dd[0] : dLo; ddHi = dd.size() > 1 ? (float) dd[1] : dHi;
                fbMax = (float) cfg.num ("fbmax", 0.95);
                fbHp = cfg.num ("fbhp", 0); fbLp = cfg.num ("fblp", 0);
                const auto law = cfg.str ("law", "level");
                mixLaw = law == "blend" ? Blend : law == "mix" ? Mix : Level;
                dryR = cfg.num ("dryr", 0) > 0.5;
                auto mr = cfg.list ("modrate"); mLo = mr.size() > 0 ? (float) mr[0] : 1.2f; mHi = mr.size() > 1 ? (float) mr[1] : mLo;
                modDepth = (float) cfg.num ("moddepth", 0.01);
                oscGain = (float) cfg.num ("osc", 1.12);
                digLp = cfg.num ("diglp", 9000);
                timeMs = cfg.num ("timems", 140);
                inGain = (float) cfg.num ("ingain", 1.0);
                pTime = role (*this, "time", 0.5f); pFb = role (*this, "feedback", 0.3f);
                pLevel = role (*this, "level", 0.5f); pDepth = role (*this, "depth", 0.0f);
                pMod = role (*this, "mod", 1.0f); pOsc = role (*this, "osc", 0.0f);
                pRate = role (*this, "rate", 0.4f); pDig = role (*this, "digital", 0.0f); pInst = role (*this, "inst", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                line.prepare (s, spec);
                if (pDig.present()) ring.allocate ((int) (s * (ddHi * 1.05 + 50.0) * 0.001));
                digF1.set (s, digLp, 0.5412); digF2.set (s, digLp, 1.3066);
                if (fbHp > 0) fbHpF.set (s, fbHp);
                if (fbLp > 0) fbLpF.set (s, fbLp);
                timeS.set (s, timeMs); tdS.set (s, timeMs); fbS.set (s, 30); lvlS.set (s, 25); depS.set (s, 30); rateS.set (s, 40); oscS.set (s, 15);
                instS.set (s, 25); digS.set (s, 25);
                reset();
            }
            void reset() override
            {
                line.reset(); ring.clear(); digF1.reset(); digF2.reset(); fbHpF.reset(); fbLpF.reset(); fbState = fbDig = 0; lfo.reset (0.0);
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const bool hasDig = pDig.present();
                const float digT = hasDig && pDig.step (*this) == 1 ? 1.0f : 0.0f;
                const float tT = logMap (pTime.get (*this), dLo, dHi), tdT = logMap (pTime.get (*this), ddLo, ddHi);
                const bool osc = pOsc.present() && pOsc.step (*this) == 1;
                // REPEAT/FEEDBACK: guadagno d'anello <= fbmax (< 1); OSCILLATION: piccolo segnale > 1 ma limitato
                const float fbT = osc ? oscGain : fbMax * pFb.get (*this);
                const float lvl = pLevel.get (*this);
                const bool modOn = pDepth.present() && (! pMod.present() || pMod.step (*this) == 1);
                const float depT = modOn ? pDepth.get (*this) : 0.0f;
                const float mHz = pRate.present() ? logMap (pRate.get (*this), mLo, mHi) : mLo;
                const float instT = pInst.present() && pInst.step (*this) == 1 ? 0.316f : 1.0f;   // LINE: -10 dB in ingresso
                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    const float ig = instS.next (instT) * inGain;
                    const float l = lfo.tick (sr, rateS.next (mHz), I4Lfo::Tri);
                    const float mod = 1.0f + modDepth * depS.next (depT) * l;
                    const float k = oscS.next (fbT);
                    // linea BBD (sempre in funzione: il passaggio al digitale e' una dissolvenza senza buchi)
                    float fbv = fbState;
                    if (fbHp > 0) fbv = fbHpF.hp (fbv);
                    if (fbLp > 0) fbv = fbLpF.lp (fbv);
                    const float vin = x * ig + loopSat (k / line.peakGain() * fbv);   // limitazione morbida prima della linea
                    const float ya = line.tick (vin, line.clockFor (timeS.next (tT) * mod));
                    fbState = ya;
                    float y = ya;
                    if (hasDig)
                    {
                        // ES3 DIGITAL: ritardo digitale pulito, anello separato
                        ring.push (x * ig + loopSat (k * fbDig));
                        const float yd = digF2.lp (digF1.lp (ring.read (std::max (1.0f, tdS.next (tdT) * mod * 0.001f * (float) sr))));
                        fbDig = yd;
                        const float m = digS.next (digT);
                        y = ya + m * (yd - ya);
                    }
                    const float wet = y / ig;
                    const float lv = lvlS.next (lvl);
                    float dryG = 1.0f, wetG;
                    switch (mixLaw)
                    {
                        case Blend: dryG = 1.0f - 0.5f * lv; wetG = lv; break;     // BLEND: da solo diretto a eco prevalente
                        case Mix:   wetG = lv; break;                               // MIX 1:0 -> 1:1
                        case Level: default: wetG = 1.1f * lv; break;               // DELAY LEVEL sommato al diretto
                    }
                    const float o = outRail (dryG * x + wetG * wet);
                    ch[0][i] = o;
                    if (numCh > 1) ch[1][i] = dryR ? x : o;                          // DRY OUTPUT
                }
            }
        private:
            Config cfg;
            I4Line::Spec spec;
            I4Line line;
            Ring ring;
            Svf digF1, digF2;
            OnePole fbHpF, fbLpF;
            double sr = 48000, fbHp = 0, fbLp = 0, digLp = 9000, timeMs = 140;
            float dLo = 10, dHi = 300, ddLo = 10, ddHi = 300, fbMax = 0.95f, mLo = 1.2f, mHi = 1.2f, modDepth = 0.01f, oscGain = 1.12f,
                  inGain = 1, fbState = 0, fbDig = 0;
            MixLaw mixLaw = Level;
            bool dryR = false;
            RoleParam pTime, pFb, pLevel, pDepth, pMod, pOsc, pRate, pDig, pInst;
            I4Lfo lfo;
            Smooth timeS, tdS, fbS, lvlS, depS, rateS, oscS, instS, digS;
        };

        //==============================================================================
        /** Tremolo a VCA, tremolo a forma variabile, auto-pan stereo. */
        class I4Trem final : public Effect
        {
        public:
            enum Model { Vca, Wave, Pan };
            explicit I4Trem (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("model", "vca");
                model = m == "pan" ? Pan : m == "wave" ? Wave : Vca;
                auto rt = cfg.list ("rate"); rLo = rt.size() > 0 ? (float) rt[0] : 0.6f; rHi = rt.size() > 1 ? (float) rt[1] : 12.0f;
                auto lv = cfg.list ("lvl"); lvUnity = lv.size() > 0 ? (float) lv[0] : 0.6f; lvMax = lv.size() > 1 ? (float) lv[1] : 1.6f;
                panDepth = (float) cfg.num ("pdepth", 0.95);
                pRate = role (*this, "rate", 0.5f); pDepth = role (*this, "depth", 0.7f);
                pWave = role (*this, "wave", 0.0f); pLevel = role (*this, "level", lvUnity);
            }
            void prepare (double s, int) override { sr = s; rateS.set (s, 40); depS.set (s, 25); lvS.set (s, 25); wvS.set (s, 25); gS.set (s, 1.0); reset(); }
            void reset() override { lfo.reset (0.25); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float hzT = logMap (pRate.get (*this), rLo, rHi);
                const float depT = model == Pan ? panDepth : pDepth.get (*this);
                const float lvT = pLevel.present() ? volKnob (pLevel.get (*this), lvUnity, lvMax) : 1.0f;
                const float wvT = pWave.get (*this);
                for (int i = 0; i < n; ++i)
                {
                    const float hz = rateS.next (hzT), dep = depS.next (depT), lv = lvS.next (lvT);
                    float s;
                    if (model == Vca) s = lfo.tick (sr, hz, I4Lfo::Rounded);         // triangolo arrotondato dai diodi D1-D4
                    else if (model == Wave)
                    {
                        const float tri = lfo.tick (sr, hz, I4Lfo::Tri);
                        const float k = 1.0f + 14.0f * wvS.next (wvT);                // WAVE: triangolo -> quadra
                        s = std::tanh (k * tri) / std::tanh (k);
                    }
                    else s = lfo.tick (sr, hz, I4Lfo::Sine);
                    if (model == Pan)
                    {
                        // due attenuatori in controfase a potenza costante: 0 dB agli estremi, -3 dB al centro
                        const float th = gS.next (0.7853982f * (1.0f + dep * s));
                        const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                        ch[0][i] = std::sin (th) * x;                                    // uscita A
                        if (numCh > 1) ch[1][i] = std::cos (th) * x;                    // uscita B in controfase
                        continue;
                    }
                    const float g = gS.next (1.0f - dep * (0.5f + 0.5f * s)) * lv;
                    for (int c = 0; c < numCh; ++c) ch[c][i] = outRail (ch[c][i] * g);
                }
            }
        private:
            Config cfg;
            Model model = Vca;
            double sr = 48000;
            float rLo = 0.6f, rHi = 12, lvUnity = 0.6f, lvMax = 1.6f, panDepth = 0.95f;
            RoleParam pRate, pDepth, pWave, pLevel;
            I4Lfo lfo;
            Smooth rateS, depS, lvS, wvS, gS;
        };
    }

    std::unique_ptr<Effect> makeI4Mod (const ModelDef& d, const std::string& type)
    {
        if (type == "i4mphase") return std::make_unique<I4Phaser> (d);
        if (type == "i4mbbd")   return std::make_unique<I4Bbd> (d);
        if (type == "i4mdelay") return std::make_unique<I4Delay> (d);
        if (type == "i4mtrem")  return std::make_unique<I4Trem> (d);
        return nullptr;
    }
}
