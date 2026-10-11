/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 5 (Behringer, Cuvave/M-VAVE): compressori, EQ, filtri, wah, volume, utility e synth ("type=i5...").

    Molti pedali di questo gruppo sono cloni di modelli gia' a catalogo e usano direttamente le famiglie esistenti o i
    tipi della tappa 4 (i4ross, i4net...). Qui ci sono solo i tipi nuovi; le opzioni sono "chiave=valore" e i valori
    sono espressioni dei comandi con la sintassi della famiglia Circuit (pot, potr, taper, lin, log, sw, par, inv, div):

      i5gate    noise gate / riduttore a VCA con soglia, decadimento, modo MUTE e riduzione selettiva in frequenza
                (NR300 come l'NS-2 ma a guadagno unitario, IG9 Intelligate tipo Smart Gate, NOISE GATE Cuvave).
      i5vca     volume a VCA in dB lineari (FCV100: attenuazione 0-70 dB, MIN VOL, modo CV).
      i5ab      selettore A/B attivo con i livelli (AB100) o doppio commutatore a rele' (AB200, mode=dual).
      i5smooth  un sotto-effetto di una famiglia esistente (stessa config del modello di partenza) con i comandi
                levigati e le dissolvenze sui selettori: i cloni dei pedali che con la famiglia originale fanno click.
      i5auto    come i5smooth con l'accensione automatica dal bilanciere, il ritardo di spegnimento e il ritardo del
                sensore ottico di posizione (HB01 Hellbabe).
      i5acou    simulatore acustico a 4 modi (algoritmo della famiglia Acoustic con i modi davvero separati, AM300).
      i5para    wah a due picchi con "dive bomb" a fine corsa (Parapedal).
      i5envf    filtro d'inviluppo a variabili di stato con fotoresistenza (Mu-Tron III) o controllo diretto (Meatball).
      i5ladder  filtro passa-basso a scala a transistor 2/4 poli con inseguitore d'inviluppo (Moogerfooger MF-101).
      i5ring    modulatore ad anello con portante/VCO e LFO (MF-102, Ring Stinger con il fuzz d'ottava).
      i5murf    banco di 8 filtri risonanti animati da sequenze (MuRF).
      i5freqbox VCO con sincronizzazione e FM dal segnale d'ingresso e VCA d'inviluppo (FreqBox).

    Tutta la memoria e' allocata nel costruttore o in prepare(); nessuna allocazione nel thread audio. I comandi
    continui sono levigati (due poli), i selettori fanno dissolvenze: niente click.
*/

#include "FxStage5.h"
#include "FxCommon.h"
#include "Families.h"
#include "Circuit.h"
#include <array>
#include <map>

namespace pt::engine
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        //==============================================================================
        // Testo della config (come FxStage4Dyn.cpp)
        std::string trimmed (const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace ((unsigned char) s[a])) ++a;
            while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
            return s.substr (a, b - a);
        }

        std::vector<std::string> splitBy (const std::string& s, const std::string& sep)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (true)
            {
                const auto j = s.find (sep, i);
                out.push_back (s.substr (i, j == std::string::npos ? std::string::npos : j - i));
                if (j == std::string::npos) break;
                i = j + sep.size();
            }
            return out;
        }

        /** Opzioni "chiave=valore" separate da spazi o ';'. */
        struct Opts
        {
            std::map<std::string, std::string> opts;
            explicit Opts (const std::string& text)
            {
                size_t i = 0;
                while (i < text.size())
                {
                    while (i < text.size() && (std::isspace ((unsigned char) text[i]) || text[i] == ';')) ++i;
                    size_t j = i;
                    while (j < text.size() && ! std::isspace ((unsigned char) text[j]) && text[j] != ';') ++j;
                    const auto tok = text.substr (i, j - i);
                    if (const auto eq = tok.find ('='); eq != std::string::npos) opts[tok.substr (0, eq)] = tok.substr (eq + 1);
                    i = j;
                }
            }
            bool has (const char* k) const { return opts.count (k) > 0; }
            std::string str (const char* k, const char* fb = "") const { auto it = opts.find (k); return it == opts.end() ? fb : it->second; }
            static double si (const std::string& t)
            {
                bool ok = false;
                const double v = Expr::parseNumber (t, ok);
                return ok ? v : std::atof (t.c_str());
            }
            double num (const char* k, double fb) const { auto it = opts.find (k); return it == opts.end() ? fb : si (it->second); }
            std::vector<double> list (const char* k) const
            {
                std::vector<double> out;
                auto it = opts.find (k);
                if (it == opts.end()) return out;
                for (const auto& t : splitBy (it->second, ",")) if (! t.empty()) out.push_back (si (t));
                return out;
            }
        };

        /** Valore come espressione dei comandi (sintassi della famiglia Circuit). */
        struct Val
        {
            Expr e;
            bool expr = false;
            double c = 0;
            void set (const std::string& text, double fallback)
            {
                expr = false; c = fallback;
                if (text.empty()) return;
                std::string err;
                e = Expr::parse (text, err);
                if (! err.empty()) return;
                if (e.isConst()) { float z[maxControls] {}; ModelDef none {}; c = e.eval (z, none); }
                else expr = true;
            }
            double operator() (const float* p, const ModelDef& d) const { return expr ? e.eval (p, d) : c; }
        };

        Val val (const Opts& o, const char* key, double fallback) { Val v; v.set (o.str (key), fallback); return v; }

        /** Comandi levigati da due poli in cascata (aggiornati ogni 'sub' campioni); selettori e levette scattano subito. */
        struct Smooth
        {
            float v[maxControls] {}, a[maxControls] {};
            float k = 0.1f;
            void setTime (double updatesPerSecond, double tauSeconds)
            {
                k = (float) (1.0 - std::exp (-1.0 / std::max (1e-6, tauSeconds * updatesPerSecond)));
            }
            void snap (const Effect& e) { for (int i = 0; i < e.def.numControls; ++i) v[i] = a[i] = e.p (i); }
            bool tick (const Effect& e)
            {
                bool ch = false;
                for (int i = 0; i < e.def.numControls; ++i)
                {
                    const float t = e.p (i);
                    if (e.def.controls[i].steps >= 2) { if (std::abs (v[i] - t) > 0.0f) { v[i] = a[i] = t; ch = true; } continue; }
                    if (std::abs (t - a[i]) > 1e-6f || std::abs (t - v[i]) > 1e-6f)
                    {
                        a[i] += k * (t - a[i]);
                        const float nv = v[i] + k * (a[i] - v[i]);
                        if (std::abs (nv - v[i]) > 0.0f) { v[i] = nv; ch = true; }
                    }
                    else if (std::abs (v[i] - t) > 0.0f) { v[i] = a[i] = t; ch = true; }
                }
                return ch;
            }
        };

        /** Lisciatura a due poli di un singolo valore, campione per campione. */
        struct Sm2
        {
            double a = 0, b = 0, k = 0.01;
            bool init = false;
            void setTime (double fs, double ms) { k = 1.0 - std::exp (-1.0 / std::max (1e-3, ms * 0.001 * fs * 0.5)); }
            void snap (double v) { a = b = v; init = true; }
            inline double next (double t) noexcept
            {
                if (! init) snap (t);
                a += k * (t - a); b += k * (a - b);
                return b;
            }
        };

        inline double softRail (double x, double v) noexcept
        {
            if (v <= 0) return x;
            const double a = x / v, a2 = a * a, a4 = a2 * a2;
            return x / std::sqrt (std::sqrt (std::sqrt (1.0 + a4 * a4)));     // ginocchio dell'op-amp ai binari
        }

        inline double fastTan (double w) noexcept      // tan(w) per 0 <= w < ~1.4 (Pade' [5/4] corretto)
        {
            const double w2 = w * w;
            return w * (945.0 - 105.0 * w2 + w2 * w2) / (945.0 - 420.0 * w2 + 15.0 * w2 * w2);
        }

        inline double gOf (double f, double fs) noexcept { return fastTan (kPi * std::clamp (f, 5.0, 0.42 * fs) / fs); }

        /** Filtro a variabili di stato TPT (Zavalishin) in double, per canale, con saturazione morbida dello stato
            passa-banda (op-amp / OTA ai binari) e coefficienti impostabili campione per campione. */
        struct Svf
        {
            double ic1[2] {}, ic2[2] {};
            void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
            inline void tick (int c, double x, double g, double k, double sat, double& lp, double& bp, double& hp) noexcept
            {
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const double v3 = x - ic2[c];
                double v1 = a1 * ic1[c] + a2 * v3;
                const double v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                if (sat > 0) v1 = sat * std::tanh (v1 / sat);
                ic1[c] = 2 * v1 - ic1[c];
                ic2[c] = 2 * v2 - ic2[c];
                lp = v2; bp = v1; hp = x - k * v1 - v2;
            }
        };

        /** Polo singolo TPT (passa-basso; passa-alto = x - lp). */
        struct Pole
        {
            double s[2] {};
            void reset() { s[0] = s[1] = 0; }
            inline double lp (int c, double x, double G) noexcept { const double v = (x - s[c]) * G; const double y = v + s[c]; s[c] = y + v; return y; }
        };
        inline double poleG (double f, double fs) noexcept { const double g = std::tan (kPi * std::clamp (f, 1.0, 0.45 * fs) / fs); return g / (1.0 + g); }

        /** Sovracampionamento opzionale (2x o 4x) con il filtro IIR a mezza banda di JUCE. */
        struct Over
        {
            std::unique_ptr<juce::dsp::Oversampling<float>> os;
            int log2 = 0;
            void prepare (int factorLog2, int maxBlock)
            {
                log2 = factorLog2;
                if (log2 > 0)
                {
                    os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) log2,
                             juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
                    os->initProcessing ((size_t) std::max (1, maxBlock));
                }
                else os.reset();
            }
            void reset() { if (os) os->reset(); }
        };

        int osOf (const Opts& o, double fb) { return std::clamp ((int) std::lround (std::log2 (std::max (1.0, o.num ("os", fb)))), 0, 2); }
        inline double dbg (double db) noexcept { return std::exp (db * 0.11512925464970229); }

        //==============================================================================
        /** i5gate: noise gate a VCA. Opzioni: thr (dBFS, espr), dec (ms, espr: rilascio del VCA), mute (espr > 0.5 =
            uscita muta), band (espr 0 FULL / 1 MID / 2 HIGH: la riduzione agisce solo sopra xo=f1,f2 Hz), atk (ms),
            hold (ms), hyst (dB), det=attacco,rilascio del rivelatore (ms), adapt=0|1 (rilascio adattivo: piu' lungo dopo
            le note tenute, tipo Smart Gate), floor (dB, attenuazione massima), out (dB). */
        class Gate5 final : public Effect
        {
        public:
            explicit Gate5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                thr = val (o, "thr", -60); dec = val (o, "dec", 100); mute = val (o, "mute", 0); band = val (o, "band", 0);
                auto xo = o.list ("xo"); xo1 = xo.size() > 0 ? xo[0] : 250; xo2 = xo.size() > 1 ? xo[1] : 2500;
                atk = o.num ("atk", 0.5); hold = o.num ("hold", 10); hyst = o.num ("hyst", 6);
                auto dt = o.list ("det"); detA = dt.size() > 0 ? dt[0] : 0.2; detR = dt.size() > 1 ? dt[1] : 15;
                adapt = o.num ("adapt", 0) > 0.5; floorDb = o.num ("floor", -90); outDb = o.num ("out", 0);
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / sub, 0.006); sm.snap (*this);
                eA = std::exp (-1.0 / (detA * 0.001 * fs)); eR = std::exp (-1.0 / (detR * 0.001 * fs));
                gA = std::exp (-1.0 / (atk * 0.001 * fs));
                muteStep = 1.0 / (0.005 * fs);
                wK = 1.0 - std::exp (-1.0 / (0.010 * fs));
                G1 = poleG (xo1, fs); G2 = poleG (xo2, fs);
                outG = dbg (outDb); floorG = dbg (floorDb);
                update();
                const int b = std::clamp ((int) std::lround (band (sm.v, def)), 0, 2);
                for (int k = 0; k < 3; ++k) w[k] = k == b ? 1.0 : 0.0;
                muteG = mute (sm.v, def) > 0.5 ? 0.0 : 1.0;
                reset();
            }
            void reset() override { env = 0; g = 0; open = false; held = 0; openTime = 0; lp1.reset(); lp2.reset(); }
            float readout (int i) const override { return i == 0 ? (float) g : (float) env; }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += sub)
                {
                    const int len = std::min (sub, n - i0);
                    if (sm.tick (*this)) update();
                    for (int k = 0; k < len; ++k)
                    {
                        double a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, (double) std::abs (ch[c][i0 + k]));
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        if (env > thLin) { open = true; held = holdN; openTime += 1.0 / fs; }
                        else if (env < tcLin) { if (held > 0) --held; else if (open) { open = false; relNow = adapt ? relA() : gR; } }
                        if (open) relNow = gR;
                        const double t = open ? 1.0 : 0.0;
                        g = t > g ? t + gA * (g - t) : t + relNow * (g - t);
                        if (! open && g < 1e-3) openTime = 0;
                        muteG = muteT < muteG ? std::max (muteT, muteG - muteStep) : std::min (muteT, muteG + muteStep);
                        for (int b = 0; b < 3; ++b) w[b] += ((b == bandT ? 1.0 : 0.0) - w[b]) * wK;
                        const double ge = floorG + (1.0 - floorG) * g;
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double x = ch[c][i0 + k];
                            const double l1 = lp1.lp (c, x, G1), l2 = lp2.lp (c, x, G2);
                            const double y = w[0] * ge * x + w[1] * (l1 + ge * (x - l1)) + w[2] * (l2 + ge * (x - l2));
                            ch[c][i0 + k] = (float) (y * muteG * outG);
                        }
                    }
                }
            }
        private:
            void update()
            {
                thLin = dbg (thr (sm.v, def)); tcLin = thLin * dbg (-hyst);
                gR = std::exp (-1.0 / (std::max (1.0, dec (sm.v, def)) * 0.001 * fs));
                if (! adapt) relNow = gR;
                muteT = mute (sm.v, def) > 0.5 ? 0.0 : 1.0;
                bandT = std::clamp ((int) std::lround (band (sm.v, def)), 0, 2);
                holdN = (int) (hold * 0.001 * fs);
            }
            /** Rilascio adattivo: note brevi -> chiusura rapida (1/4 del tempo base), note tenute -> fino a 3 volte. */
            double relA() const
            {
                const double base = std::max (1.0, dec (sm.v, def)) * (0.25 + 1.8 * std::min (1.5, openTime));
                return std::exp (-1.0 / (base * 0.001 * fs));
            }
            Val thr, dec, mute, band;
            double xo1 = 250, xo2 = 2500, atk = 0.5, hold = 10, hyst = 6, detA = 0.2, detR = 15, floorDb = -90, outDb = 0;
            bool adapt = false, open = false;
            double fs = 48000, eA = 0, eR = 0, gA = 0, gR = 0.999, relNow = 0.999, env = 0, g = 0, thLin = 1e-3, tcLin = 5e-4;
            double muteG = 1, muteT = 1, muteStep = 0.004, w[3] { 1, 0, 0 }, wK = 0.002, G1 = 0.1, G2 = 0.2, outG = 1, floorG = 0, openTime = 0;
            int bandT = 0, held = 0, holdN = 480;
            static constexpr int sub = 16;
            Smooth sm;
            Pole lp1, lp2;
        };

        //==============================================================================
        /** i5vca: volume a VCA (FCV100). Ruoli volume (bilanciere), min (MIN VOL), mode (NORM / CV). Guadagno in dB
            lineare con la corsa: g = floor * (1 - pedale), floor = -range * (1 - MIN VOL). In CV l'uscita 2 non porta
            audio: con la catena stereo il canale destro diventa una copia del sinistro (un solo canale VCA). */
        class VcaVol5 final : public Effect
        {
        public:
            explicit VcaVol5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                pVol = role (*this, "volume", 1.0f); pMin = role (*this, "min", 0.0f); pMode = role (*this, "mode", 0.0f);
                range = o.num ("range", 70); cvStep = o.num ("cvstep", 1);
            }
            void prepare (double s, int) override
            {
                fs = s; gdb.setTime (fs, 15.0); gdb.snap (target());
                xK = 1.0 - std::exp (-1.0 / (0.010 * fs));
                cv = pMode.step (*this) == (int) cvStep ? 1.0 : 0.0;
            }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const double t = target(), cvT = pMode.step (*this) == (int) cvStep ? 1.0 : 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double gd = gdb.next (t);
                    const double gl = gd <= -range + 0.05 ? 0.0 : dbg (gd);
                    cv += (cvT - cv) * xK;
                    const float l = (float) (ch[0][i] * gl);
                    if (numCh > 1) ch[1][i] = (float) ((1.0 - cv) * ch[1][i] * gl + cv * l);
                    ch[0][i] = l;
                }
            }
        private:
            double target() const
            {
                const double v = std::clamp ((double) pVol.get (*this), 0.0, 1.0), mn = std::clamp ((double) pMin.get (*this), 0.0, 1.0);
                return -range * (1.0 - mn) * (1.0 - v);
            }
            RoleParam pVol, pMin, pMode;
            double range = 70, cvStep = 1, fs = 48000, xK = 0.002, cv = 0;
            Sm2 gdb;
        };

        //==============================================================================
        /** i5ab: selettore A/B. mode=sel (AB100): ruoli direction (0 = IN 1 -> OUT A / OUT B, 1 = IN 1 / IN 2 -> OUT A),
            levelA, levelB (guadagno gmax*x^2: unitario a ~0.71), select (A/B). Nel plugin OUT A / IN 1 = canale sinistro,
            OUT B / IN 2 = destro (come il Router AB-2); con la catena mono resta il livello del ramo scelto.
            mode=dual (AB200): ruoli select, select2: ciascun canale (sinistro = 1, destro = 2) in B esce dall'altra
            uscita (rele' passivi, guadagno unitario; dissolvenza di 3 ms al posto del rimbalzo del contatto). */
        class AbSel5 final : public Effect
        {
        public:
            explicit AbSel5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                dual = o.str ("mode", "sel") == "dual";
                pDir = role (*this, "direction", 0.0f); pA = role (*this, "levelA", 0.71f); pB = role (*this, "levelB", 0.71f);
                pSel = role (*this, "select", 0.0f); pSel2 = role (*this, "select2", 0.0f);
                gmax = o.num ("gmax", 2.0); fadeMs = o.num ("fade", dual ? 3.0 : 10.0);
            }
            void prepare (double s, int) override
            {
                fs = s;
                for (auto& x : sg) x.setTime (fs, fadeMs);
                for (int k = 0; k < 5; ++k) sg[k].snap (tgt (k));
            }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                double t[5];
                for (int k = 0; k < 5; ++k) t[k] = tgt (k);
                for (int i = 0; i < n; ++i)
                {
                    double v[5];
                    for (int k = 0; k < 5; ++k) v[k] = sg[k].next (t[k]);
                    if (dual)
                    {
                        if (numCh < 2) continue;
                        const double l = ch[0][i], r = ch[1][i];
                        ch[0][i] = (float) ((1.0 - v[0]) * l + v[1] * r);
                        ch[1][i] = (float) (v[0] * l + (1.0 - v[1]) * r);
                        continue;
                    }
                    // v0 = B scelto, v1 = modo 2 ingressi, v2 = livello A, v3 = livello B
                    if (numCh < 2) { ch[0][i] = (float) (ch[0][i] * ((1.0 - v[0]) * v[2] + v[0] * (v[1] * v[2] + (1.0 - v[1]) * v[3]))); continue; }
                    const double l = ch[0][i], r = ch[1][i], m = 0.5 * (l + r);
                    // IN 1 -> OUT A / OUT B
                    const double sA = m * v[2] * (1.0 - v[0]), sB = m * v[3] * v[0];
                    // IN 1 / IN 2 -> OUT A (uscita mono su entrambi i canali)
                    const double mg = ((1.0 - v[0]) * l + v[0] * r) * v[2];
                    ch[0][i] = (float) ((1.0 - v[1]) * sA + v[1] * mg);
                    ch[1][i] = (float) ((1.0 - v[1]) * sB + v[1] * mg);
                }
            }
        private:
            double tgt (int k) const
            {
                if (dual) return k == 0 ? (pSel.step (*this) > 0 ? 1.0 : 0.0) : k == 1 ? (pSel2.step (*this) > 0 ? 1.0 : 0.0) : 0.0;
                switch (k)
                {
                    case 0: return pSel.step (*this) > 0 ? 1.0 : 0.0;
                    case 1: return pDir.step (*this) > 0 ? 1.0 : 0.0;
                    case 2: { const double x = pA.get (*this); return gmax * x * x; }
                    case 3: { const double x = pB.get (*this); return gmax * x * x; }
                    default: return 0.0;
                }
            }
            bool dual = false;
            RoleParam pDir, pA, pB, pSel, pSel2;
            double gmax = 2, fadeMs = 10, fs = 48000;
            Sm2 sg[5];
        };

        //==============================================================================
        /** i5smooth / i5auto: un sotto-effetto di una famiglia esistente con gli stessi comandi, reso senza click:
            "type=i5smooth tau=ms chunk=campioni dip=ms reprep=0|1 || Famiglia: config" (nella config del sotto-effetto
            "subtype=" diventa "type="). Il sotto-effetto lavora a pezzi di 'chunk' campioni e riceve i comandi continui
            levigati da due poli (tau); quando cambia un selettore o una levetta l'uscita passa al segnale diretto con una
            dissolvenza a coseno rialzato (dip ms), il selettore scatta e l'effetto ritorna. reprep=1: reset() richiama
            anche prepare() del sotto-effetto (solo per le famiglie che non allocano in prepare: Volume, Compressor,
            SlowGear) cosi' lo stato riparte identico. Con "pedal=i" (i5auto) l'effetto si accende quando il bilanciere
            lascia il tallone (piede appoggiato) e si spegne dopo 'delay' secondi al tallone (dissolvenza di 10 ms);
            'lag=on,off' (ms) e' il ritardo del sensore ottico di posizione (LED + fotocellula) sul bilanciere. */
        class Wrap5 final : public Effect
        {
        public:
            explicit Wrap5 (const ModelDef& d) : Effect (d)
            {
                const auto parts = splitBy (d.config != nullptr ? d.config : "", "||");
                const Opts head (parts.empty() ? "" : parts[0]);
                pedal = (int) head.num ("pedal", -1); thr = head.num ("thr", 0.02); delay = val (head, "delay", 1.0);
                auto lg = head.list ("lag"); lagOn = lg.size() > 0 ? lg[0] : 0.0; lagOff = lg.size() > 1 ? lg[1] : lagOn;
                tauMs = head.num ("tau", 20.0); chunk = std::clamp ((int) head.num ("chunk", 8), 1, 64); dipMs = head.num ("dip", 8.0);
                reprep = head.num ("reprep", 0) > 0.5;
                subDef = d;
                if (parts.size() > 1)
                {
                    const auto colon = parts[1].find (':');
                    if (colon != std::string::npos)
                    {
                        const auto fam = trimmed (parts[1].substr (0, colon));
                        cfg = trimmed (parts[1].substr (colon + 1));
                        for (size_t pos = cfg.find ("subtype="); pos != std::string::npos; pos = cfg.find ("subtype=", pos)) cfg.replace (pos, 8, "type=");
                        static const std::pair<const char*, Family> map[] = {
                            { "Circuit", Family::Circuit }, { "Wah", Family::Wah }, { "Compressor", Family::Compressor },
                            { "GraphicEQ", Family::GraphicEQ }, { "ParametricEQ", Family::ParametricEQ }, { "Volume", Family::Volume },
                            { "SlowGear", Family::SlowGear }, { "Acoustic", Family::Acoustic }, { "NoiseGate", Family::NoiseGate } };
                        for (const auto& m : map) if (fam == m.first) subDef.family = m.second;
                    }
                }
                subDef.config = cfg.c_str();
                fx = createEffect (subDef);
            }
            void prepare (double s, int maxBlock) override
            {
                fs = s; maxN = std::max (1, maxBlock); mb = maxBlock;
                for (auto& b : buf) b.assign ((size_t) maxN, 0.0f);
                kS = 1.0 - std::exp (-(double) chunk / (std::max (0.5, 0.5 * tauMs) * 0.001 * fs));
                dipStep = 1.0 / std::max (1.0, dipMs * 0.001 * fs);
                snap();
                fx->prepare (s, maxBlock);
                on = pedalDown(); wet = on ? 1.0 : 0.0; offT = 0;
            }
            void reset() override
            {
                snap();
                if (reprep) fx->prepare (fs, mb);
                fx->reset();
                on = pedalDown(); wet = on ? 1.0 : 0.0; offT = 0;
            }
            float readout (int i) const override { return i == 1 ? (float) wet : i == 0 ? (float) lagS : fx->readout (i - 2); }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                const double step = 1.0 / (0.010 * fs);
                for (int i0 = 0; i0 < n; )
                {
                    const int len = std::min ({ chunk, maxN, n - i0 });
                    controls (len);
                    float* b[2] = { buf[0].data(), buf[1].data() };
                    for (int c = 0; c < numCh; ++c) std::copy (ch[c] + i0, ch[c] + i0 + len, b[c]);
                    fx->process (b, numCh, len);
                    if (pedal >= 0)
                    {
                        if (pedalDown()) { on = true; offT = 0; }
                        else if (on) { offT += len / fs; if (offT >= std::max (0.0, delay (cv, def))) on = false; }
                    }
                    const double wt = on ? 1.0 : 0.0;
                    for (int k = 0; k < len; ++k)
                    {
                        wet = wt > wet ? std::min (wt, wet + step) : std::max (wt, wet - step);
                        // selettori: dissolvenza verso il diretto, scatto a meta', ritorno
                        if (pendingDip) { dipU -= dipStep; if (dipU <= 0) { dipU = 0; pendingDip = false; applySteps(); } }
                        else if (dipU < 1) dipU = std::min (1.0, dipU + dipStep);
                        const double w = wet * (0.5 - 0.5 * std::cos (kPi * dipU));
                        for (int c = 0; c < numCh; ++c) ch[c][i0 + k] += (float) (w * (b[c][k] - ch[c][i0 + k]));
                    }
                    i0 += len;
                }
            }
        private:
            bool pedalDown() const { return pedal < 0 || pedal >= def.numControls || p (pedal) > thr; }
            void snap()
            {
                for (int i = 0; i < def.numControls; ++i) { cv[i] = ca[i] = p (i); fx->params[i].store (cv[i], std::memory_order_relaxed); }
                lagS = pedal >= 0 && pedal < def.numControls ? p (pedal) : 0.0;
                if (pedal >= 0 && pedal < def.numControls) fx->params[pedal].store ((float) lagS, std::memory_order_relaxed);
                pendingDip = false; dipU = 1;
            }
            /** Comandi levigati verso il sotto-effetto; un selettore cambiato fa partire la dissolvenza. */
            void controls (int len)
            {
                for (int i = 0; i < def.numControls; ++i)
                {
                    const float t = p (i);
                    if (def.controls[i].steps >= 2)
                    {
                        if (std::abs (t - cv[i]) > 0.0f && ! pendingDip) pendingDip = true;
                        continue;
                    }
                    if (std::abs (t - ca[i]) > 1e-6f || std::abs (t - cv[i]) > 1e-6f)
                    {
                        ca[i] += (float) kS * (t - ca[i]);
                        cv[i] += (float) kS * (ca[i] - cv[i]);
                    }
                    else cv[i] = ca[i] = t;
                    float v = cv[i];
                    if (i == pedal)
                    {
                        const double ms = v > lagS ? lagOn : lagOff;
                        lagS = ms <= 0 ? v : v + (lagS - v) * std::exp (-len / (ms * 0.001 * fs));
                        if (std::abs (lagS - v) < 1e-5) lagS = v;
                        v = (float) lagS;
                    }
                    fx->params[i].store (v, std::memory_order_relaxed);
                }
            }
            void applySteps()
            {
                for (int i = 0; i < def.numControls; ++i)
                    if (def.controls[i].steps >= 2) { cv[i] = ca[i] = p (i); fx->params[i].store (cv[i], std::memory_order_relaxed); }
            }
            ModelDef subDef {};
            std::string cfg;
            std::unique_ptr<Effect> fx;
            int pedal = -1, maxN = 512, mb = 512, chunk = 8;
            double thr = 0.02, lagOn = 0, lagOff = 0, lagS = 0, fs = 48000, wet = 1, offT = 0, tauMs = 20, dipMs = 8, kS = 0.1;
            double dipStep = 0.003, dipU = 1;
            bool on = true, reprep = false, pendingDip = false;
            float cv[maxControls] {}, ca[maxControls] {};
            Val delay;
            std::vector<float> buf[2];
        };

        //==============================================================================
        /** Biquad RBJ in double con lo stato per canale. */
        struct BQ
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
            void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
            void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
            {
                b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
            }
            void peak (double fs, double f, double q, double db)
            {
                const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::clamp (f, 10.0, fs * 0.45) / fs;
                const double al = std::sin (w) / (2 * std::max (0.05, q)), c = std::cos (w);
                set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
            }
            void highShelf (double fs, double f, double q, double db)
            {
                const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::clamp (f, 10.0, fs * 0.45) / fs;
                const double c = std::cos (w), al = std::sin (w) / (2 * q), sq = 2 * std::sqrt (A) * al;
                set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
            }
            inline double tick (int ch, double x) noexcept
            {
                const double y = b0 * x + z1[ch];
                z1[ch] = b1 * x - a1 * y + z2[ch];
                z2[ch] = b2 * x - a2 * y;
                return y;
            }
        };

        /** i5acou: simulatore acustico (algoritmo della famiglia Acoustic: risonanze del corpo per modo scalate da BODY,
            taglio del "quack" dei pick-up a cut Hz, mensola acuta TOP +-12 dB a top Hz, LEVEL curva A) con i modi
            separati da '/' (la famiglia originale li separa con ';', che la sua config spezza), comandi levigati e
            dissolvenza di 15 ms fra i due banchi di filtri quando cambia il modo. Ruoli top body level mode;
            opzioni modes=f:g:q,f:g:q/... top cut lvl (guadagno del LEVEL al massimo). */
        class Acou5 final : public Effect
        {
        public:
            explicit Acou5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                const auto ms = splitBy (o.str ("modes", "110:6:1.2,220:3:1.5,3000:4:0.8"), "/");
                for (const auto& m : ms)
                {
                    if (numModes >= 4) break;
                    int k = 0;
                    for (const auto& t : splitBy (m, ","))
                    {
                        if (k >= 4) break;
                        float f = 100, g = 0, q = 1;
                        if (std::sscanf (t.c_str(), "%f:%f:%f", &f, &g, &q) >= 2) res[numModes][k++] = { f, g, q };
                    }
                    cnt[numModes++] = k;
                }
                topHz = o.num ("top", 5000); cutHz = o.num ("cut", 600); lvlMax = o.num ("lvl", 2.0);
                pTop = role (*this, "top", 0.5f); pBody = role (*this, "body", 0.5f); pLevel = role (*this, "level", 0.5f); pMode = role (*this, "mode", 0.0f);
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / sub, 0.008); sm.snap (*this);
                cur = std::clamp (pMode.step (*this), 0, std::max (0, numModes - 1)); alt = cur; xf = 0;
                xfStep = 1.0 / (0.015 * fs);
                design (bank[0], cur); design (bank[1], cur);
                gl = lvl();
                reset();
            }
            void reset() override { for (auto& b : bank) for (auto& f : b) f.reset(); xf = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += sub)
                {
                    const int len = std::min (sub, n - i0);
                    const bool moved = sm.tick (*this);
                    const int want = std::clamp (stepOf (pMode.idx), 0, std::max (0, numModes - 1));
                    if (want != cur && xf <= 0)
                    {
                        // banco 1 = modo vecchio (con il suo stato), banco 0 riparte con il modo nuovo dallo stesso stato
                        bank[1] = bank[0]; alt = cur; cur = want; design (bank[0], cur); xf = 1;
                    }
                    else if (moved) { design (bank[0], cur); if (xf > 0) design (bank[1], alt); }
                    const double g0 = gl, g1 = lvl();
                    for (int k = 0; k < len; ++k)
                    {
                        const double g = g0 + (g1 - g0) * (k + 1) / len;
                        if (xf > 0) xf = std::max (0.0, xf - xfStep);
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double x = ch[c][i0 + k];
                            double y = x;
                            for (auto& f : bank[0]) y = f.tick (c, y);
                            if (xf > 0)
                            {
                                double y2 = x;
                                for (auto& f : bank[1]) y2 = f.tick (c, y2);
                                const double w = 0.5 - 0.5 * std::cos (kPi * xf);
                                y = y + w * (y2 - y);
                            }
                            ch[c][i0 + k] = (float) (y * g);
                        }
                    }
                    gl = g1;
                }
            }
        private:
            struct Res { float f = 1000, g = 0, q = 1; };
            int stepOf (int i) const
            {
                if (i < 0) return 0;
                const int st = std::max (1, (int) def.controls[i].steps);
                return std::clamp ((int) std::lround (sm.v[i] * (st - 1)), 0, st - 1);
            }
            double lvl() const { return taperA (pLevel.present() ? sm.v[pLevel.idx] : 0.5f) * lvlMax; }
            void design (std::array<BQ, 6>& b, int m)
            {
                const double body = (pBody.present() ? sm.v[pBody.idx] : 0.5) * 2.0, top = ((pTop.present() ? sm.v[pTop.idx] : 0.5) - 0.5) * 24.0;
                for (int k = 0; k < 4; ++k)
                {
                    if (k < cnt[m]) b[(size_t) k].peak (fs, res[m][k].f, res[m][k].q, res[m][k].g * body);
                    else b[(size_t) k].peak (fs, 1000, 1, 0);
                }
                b[4].peak (fs, cutHz, 0.8, -7.0);
                b[5].highShelf (fs, topHz, 0.7, top);
            }
            Res res[4][4] {};
            int cnt[4] {}, numModes = 0, cur = 0, alt = 0;
            double topHz = 5000, cutHz = 600, lvlMax = 2, fs = 48000, xf = 0, xfStep = 0.001, gl = 0.3;
            RoleParam pTop, pBody, pLevel, pMode;
            std::array<BQ, 6> bank[2];
            static constexpr int sub = 16;
            Smooth sm;
        };

        //==============================================================================
        /** i5para: wah a due picchi (Parapedal). Ruolo freq (bilanciere). Opzioni: f=tallone,punta (Hz, legge
            esponenziale), q (Q del picco principale), g (dB del picco), r2 (rapporto del secondo picco), q2, g2 (dB
            relativi), floor (frazione del segnale diretto che resta fuori banda), out (dB d'uscita), dive=frazione di corsa al tallone
            (arresto in gomma), fdive (Hz a fine corsa), att (dB di attenuazione a fine corsa), rail (V). */
        class Para5 final : public Effect
        {
        public:
            explicit Para5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                auto f = o.list ("f"); fHeel = f.size() > 0 ? f[0] : 760; fToe = f.size() > 1 ? f[1] : 2200;
                q1 = o.num ("q", 4); gPk = dbg (o.num ("g", 18)); r2 = o.num ("r2", 2.6); q2 = o.num ("q2", 3); g2 = dbg (o.num ("g2", -6));
                fl = o.num ("floor", 0.12); dive = o.num ("dive", 0.08); fDive = o.num ("fdive", 180); attDb = o.num ("att", -10);
                rail = o.num ("rail", 3.8); outG = dbg (o.num ("out", 0.0));
                pFreq = role (*this, "freq", 0.5f);
                osLog2 = osOf (o, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                pos.setTime (fs, 8.0); pos.snap (pFreq.get (*this));
                hp.reset(); reset();
            }
            void reset() override { a.reset(); b.reset(); over.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double t = std::clamp ((double) pFreq.get (*this), 0.0, 1.0);
                const double k1 = 1.0 / q1, k2 = 1.0 / q2;
                for (int i = 0; i < N; ++i)
                {
                    const double x = pos.next (t);
                    double f, att = 1.0;
                    if (x >= dive) f = fHeel * std::pow (fToe / fHeel, (x - dive) / (1.0 - dive));
                    else
                    {
                        const double u = x / dive;                       // ultima parte della corsa: "dive bomb"
                        f = fDive * std::pow (fHeel / fDive, u);
                        att = dbg (attDb * (1.0 - u));
                    }
                    const double g1 = gOf (f, fs), gg2 = gOf (f * r2, fs);
                    for (int c = 0; c < numCh; ++c)
                    {
                        const double u = data[c][i];
                        double l, bp1, h, bp2;
                        a.tick (c, u, g1, k1, 0.0, l, bp1, h);
                        b.tick (c, u, gg2, k2, 0.0, l, bp2, h);
                        const double y = fl * u + gPk * (k1 * bp1 + g2 * k2 * bp2);
                        data[c][i] = (float) (softRail (y * att, rail) * outG);
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            double outG = 1, fHeel = 760, fToe = 2200, q1 = 4, gPk = 7.9, r2 = 2.6, q2 = 3, g2 = 0.5, fl = 0.12, dive = 0.08, fDive = 180, attDb = -10, rail = 3.8;
            RoleParam pFreq;
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            Sm2 pos;
            Svf a, b;
            Pole hp;
        };

        //==============================================================================
        /** i5envf: filtro d'inviluppo a variabili di stato. Opzioni (espressioni dei comandi):
              pre (guadagno del preamplificatore), rail (V, binari del preamp e del filtro), envg (dal segnale
              preamplificato alla corrente del LED o alla posizione 0..1), atk dec (ms, raddrizzatore + RC),
              q (Q del filtro), mode (0 LP / 1 BP / 2 HP, dissolvenza), dir (0 UP / 1 DOWN), bpn (normalizzazione del
              passa-banda: guadagno Q^(1-bpn)), sat (V, stato passa-banda), out (guadagno d'uscita), blend (0 secco..1).
            Legge della frequenza:
              opto=1 (Mu-Tron III): LED -> fotoresistenze accoppiate R = Ron * I^-gamma (ldrr=Ron,Rbuio,gamma; dinamica
                della CdS ldrt=on,off ms), f = 1 / (2 pi (rs + R||rp) c * cr), cr = espressione della gamma; in DOWN la
                corrente del LED e' imax - envg*inv;
              opto=0 (Meatball): f = flo * 2^(man + span * depth * e) (UP) o 2^(man + span * depth * (1 - e)) (DOWN),
                e = inviluppo normalizzato 0..1; flo, man, span, depth espressioni. */
        class EnvF5 final : public Effect
        {
        public:
            explicit EnvF5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                pre = val (o, "pre", 1.0); envg = val (o, "envg", 1.0); atk = val (o, "atk", 5.0); dec = val (o, "dec", 100.0);
                q = val (o, "q", 2.0); mode = val (o, "mode", 1.0); dir = val (o, "dir", 0.0); out = val (o, "out", 1.0);
                blend = val (o, "blend", 1.0); cr = val (o, "cr", 1.0);
                flo = val (o, "flo", 100.0); man = val (o, "man", 0.0); span = val (o, "span", 4.0); depth = val (o, "depth", 1.0);
                rail = o.num ("rail", 0.0); bpn = o.num ("bpn", 0.5); sat = o.num ("sat", 0.0);
                opto = o.num ("opto", 0) > 0.5;
                auto r = o.list ("ldrr"); ron = r.size() > 0 ? r[0] : 4e3; rdark = r.size() > 1 ? r[1] : 2e6; gam = r.size() > 2 ? r[2] : 0.75;
                auto t = o.list ("ldrt"); onMs = t.size() > 0 ? t[0] : 6; offMs = t.size() > 1 ? t[1] : 60;
                rs = o.num ("rs", 4.7e3); rp = o.num ("rp", 1e6); cap = o.num ("c", 10e-9); imax = o.num ("imax", 1.0);
                osLog2 = osOf (o, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                kOn = 1.0 - std::exp (-1.0 / (onMs * 0.001 * fs)); kOff = 1.0 - std::exp (-1.0 / (offMs * 0.001 * fs));
                kF = 1.0 - std::exp (-1.0 / (0.0015 * fs));
                dirK = 1.0 - std::exp (-1.0 / (0.020 * fs));
                xfK = 1.0 - std::exp (-1.0 / (0.004 * fs));
                update (true);
                dirS = dirT; for (int m = 0; m < 3; ++m) for (int c = 0; c < 2; ++c) xg[c * 3 + m] = m == modeT ? 1.0 : 0.0;
                reset();
            }
            void reset() override { svf.reset(); env = 0; logR = std::log (rdark); lf = -1; over.reset(); }
            float readout (int i) const override { return i == 0 ? (float) fNow : i == 1 ? (float) env : (float) std::exp (logR); }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    const double preA = preV, outA = outV, blA = blV, qA = qV;
                    if (sm.tick (*this)) update (false);
                    for (int k = 0; k < len; ++k)
                    {
                        const double u = (double) (k + 1) / len;
                        const double pg = preA + u * (preV - preA), og = outA + u * (outV - outA), bl = blA + u * (blV - blA);
                        const double qq = qA + u * (qV - qA), kk = 1.0 / std::max (0.3, qq), bpG = std::pow (qq, -bpn);
                        // raddrizzatore a doppia semionda sul segnale preamplificato
                        double a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, std::abs (softRail (data[c][i0 + k] * pg, rail)));
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        dirS += (dirT - dirS) * dirK;
                        double f;
                        if (opto)
                        {
                            const double iu = std::max (0.0, envgV * env), id = std::max (0.0, imax - envgV * env);
                            const double I = (1.0 - dirS) * iu + dirS * id;
                            const double tl = I > 1e-9 ? std::log (std::clamp (ron * std::pow (I, -gam), 10.0, rdark)) : std::log (rdark);
                            logR += (tl < logR ? kOn : kOff) * (tl - logR);
                            const double rl = std::exp (logR);
                            f = 1.0 / (2 * kPi * (rs + rl * rp / (rl + rp)) * cap * crV);
                        }
                        else
                        {
                            const double e = std::clamp (envgV * env, 0.0, 1.0);
                            const double oct = manV + spanV * depthV * ((1.0 - dirS) * e + dirS * (1.0 - e));
                            f = floV * std::exp2 (oct);
                        }
                        // frequenza levigata nel dominio logaritmico (1.5 ms): niente salti con RANGE e i selettori
                        const double lt = std::log (std::clamp (f, 10.0, 0.42 * fs));
                        lf = lf < -0.5 ? lt : lf + kF * (lt - lf);
                        fNow = std::exp (lf);
                        const double g = gOf (fNow, fs);
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double dry = x[k];
                            const double v = softRail (dry * pg, rail);
                            double lp, bp, hp;
                            svf.tick (c, v, g, kk, sat, lp, bp, hp);
                            for (int m = 0; m < 3; ++m) xg[c * 3 + m] += ((m == modeT ? 1.0 : 0.0) - xg[c * 3 + m]) * xfK;
                            double y = xg[c * 3] * lp + xg[c * 3 + 1] * bp * bpG + xg[c * 3 + 2] * hp;
                            y = softRail (y, rail) * og;
                            x[k] = (float) (dry + bl * (y - dry));
                        }
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update (bool first)
            {
                const auto* p = sm.v;
                preV = std::max (0.0, pre (p, def)); outV = std::max (0.0, out (p, def)); blV = std::clamp (blend (p, def), 0.0, 1.0);
                qV = std::max (0.3, q (p, def)); envgV = std::max (0.0, envg (p, def));
                eA = std::exp (-1.0 / (std::max (0.05, atk (p, def)) * 0.001 * fs));
                eR = std::exp (-1.0 / (std::max (0.5, dec (p, def)) * 0.001 * fs));
                modeT = std::clamp ((int) std::lround (mode (p, def)), 0, 2);
                dirT = dir (p, def) > 0.5 ? 1.0 : 0.0;
                crV = std::max (1e-3, cr (p, def)); floV = std::max (1.0, flo (p, def));
                manV = man (p, def); spanV = span (p, def); depthV = depth (p, def);
                (void) first;
            }
            Val pre, envg, atk, dec, q, mode, dir, out, blend, cr, flo, man, span, depth;
            double rail = 0, bpn = 0.5, sat = 0, ron = 4e3, rdark = 2e6, gam = 0.75, onMs = 6, offMs = 60, rs = 4.7e3, rp = 1e6, cap = 10e-9, imax = 1;
            bool opto = false;
            double preV = 1, outV = 1, blV = 1, qV = 2, envgV = 1, eA = 0, eR = 0, dirT = 0, dirS = 0, crV = 1, floV = 100, manV = 0, spanV = 4, depthV = 1;
            int modeT = 1;
            double env = 0, logR = 14, kOn = 0.01, kOff = 0.001, kF = 0.01, dirK = 0.001, xfK = 0.005, lf = -1, fNow = 500, xg[6] {};
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
            Svf svf;
        };

        //==============================================================================
        /** i5ladder: passa-basso a scala di transistor (Moog) a quattro poli TPT con la non linearita' della coppia
            differenziale d'ingresso (tanh) sulla somma con la retroazione; uscita a 2 o 4 poli (dissolvenza).
            Opzioni (espressioni): drive (guadagno verso la scala; vt = V di saturazione), fc (Hz), res (k, 4 =
            auto-oscillazione), poles (0 = 2 poli, 1 = 4 poli), amt (ottave d'inviluppo), envg (dal segnale alla
            posizione 0..1 dell'inviluppo), atk rel (ms), mix (0 secco..1 filtrato), out (guadagno), comp (compensazione
            del calo di livello con la risonanza: (1+k)^comp), os. */
        class Ladder5 final : public Effect
        {
        public:
            explicit Ladder5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                drive = val (o, "drive", 1.0); fc = val (o, "fc", 1000.0); res = val (o, "res", 0.0); poles = val (o, "poles", 1.0);
                amt = val (o, "amt", 0.0); envg = val (o, "envg", 4.0); atk = val (o, "atk", 3.0); rel = val (o, "rel", 100.0);
                mix = val (o, "mix", 1.0); out = val (o, "out", 1.0);
                vt = o.num ("vt", 0.5); comp = o.num ("comp", 0.5);
                osLog2 = osOf (o, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                xfK = 1.0 - std::exp (-1.0 / (0.004 * fs));
                update(); for (int i = 0; i < 6; ++i) cur[i] = tgt[i];
                pl = tgt[6];
                reset();
            }
            void reset() override { for (auto& st : lad) for (auto& v : st) v = 0; env = 0; over.reset(); }
            float readout (int i) const override { return i == 0 ? (float) fNow : (float) env; }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    double from[6]; std::copy (cur, cur + 6, from);
                    if (sm.tick (*this)) update();
                    for (int k = 0; k < len; ++k)
                    {
                        const double u = (double) (k + 1) / len;
                        for (int j = 0; j < 6; ++j) cur[j] = from[j] + u * (tgt[j] - from[j]);
                        // 0 drive, 1 log fc, 2 k, 3 amt, 4 mix, 5 out
                        double a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, std::abs ((double) data[c][i0 + k]) * cur[0]);
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        const double e = std::clamp (envgV * env, 0.0, 1.0);
                        fNow = std::min (0.42 * fs, std::exp (cur[1]) * std::exp2 (cur[3] * e));
                        const double g = gOf (fNow, fs), G = g / (1.0 + g), kk = cur[2];
                        pl += (tgt[6] - pl) * xfK;
                        const double G2 = G * G, G3 = G2 * G, G4 = G2 * G2, H = 1.0 - G;
                        const double cgain = std::pow (1.0 + kk, comp);
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double dry = x[k];
                            auto& st = lad[c];
                            const double S = G3 * H * st[0] + G2 * H * st[1] + G * H * st[2] + H * st[3];
                            const double xin = dry * cur[0] / vt;
                            const double y4e = (G4 * xin + S) / (1.0 + kk * G4);
                            const double v = std::tanh (xin - kk * y4e);
                            double y = v, y2 = 0;
                            for (int j = 0; j < 4; ++j)
                            {
                                const double w = (y - st[j]) * G, yo = w + st[j];
                                st[j] = yo + w;
                                y = yo;
                                if (j == 1) y2 = yo;
                            }
                            const double wet = (pl * y + (1.0 - pl) * y2) * vt * cgain / std::max (1e-3, cur[0]);
                            x[k] = (float) ((dry + cur[4] * (wet - dry)) * cur[5]);
                        }
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update()
            {
                const auto* p = sm.v;
                tgt[0] = std::max (1e-3, drive (p, def)); tgt[1] = std::log (std::clamp (fc (p, def), 10.0, 0.4 * fs));
                tgt[2] = std::clamp (res (p, def), 0.0, 4.5); tgt[3] = amt (p, def);
                tgt[4] = std::clamp (mix (p, def), 0.0, 1.0); tgt[5] = std::max (0.0, out (p, def));
                tgt[6] = poles (p, def) > 0.5 ? 1.0 : 0.0;
                envgV = envg (p, def);
                eA = std::exp (-1.0 / (std::max (0.05, atk (p, def)) * 0.001 * fs));
                eR = std::exp (-1.0 / (std::max (0.5, rel (p, def)) * 0.001 * fs));
            }
            Val drive, fc, res, poles, amt, envg, atk, rel, mix, out;
            double vt = 0.5, comp = 0.5, cur[6] {}, tgt[7] {}, pl = 1, xfK = 0.005, envgV = 4, eA = 0, eR = 0, env = 0, fNow = 1000;
            double lad[2][4] {};
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
        };

        //==============================================================================
        /** Oscillatore a fase con correzione polyBLEP dei salti (dente di sega, impulso). */
        inline double blep (double t, double dt) noexcept
        {
            if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        }
        /** Forme d'onda per fase ph (0..1), incremento dt, larghezza d'impulso pw: 0 seno, 1 triangolo (con simmetria
            sk 0..1: 0.5 = triangolo, ->1 rampa), 2 dente di sega discendente, 3 quadra/impulso. */
        inline void waves (double ph, double dt, double pw, double sk, double out[4]) noexcept
        {
            out[0] = std::sin (2 * kPi * ph);
            const double s = std::clamp (sk, 0.02, 0.98);
            out[1] = ph < s ? -1.0 + 2.0 * ph / s : 1.0 - 2.0 * (ph - s) / (1.0 - s);
            out[2] = (1.0 - 2.0 * ph) + blep (ph, dt);                         // discendente: salto a ph = 0 verso +1
            double sq = ph < pw ? 1.0 : -1.0;
            sq += blep (ph, dt);
            double p2 = ph - pw; if (p2 < 0) p2 += 1.0;
            sq -= blep (p2, dt);
            out[3] = sq;
        }

        /** i5ring: modulatore ad anello. Opzioni (espressioni): drive (guadagno d'ingresso, tanh con vk V), cf (Hz della
            portante/VCO), wave (0 seno, 1 triangolo, 2 dente di sega, 3 impulso; dissolvenza), rate (Hz dell'LFO), lshape
            (0 seno / triangolo con tri=1, 1 quadra), ltgt (0 frequenza, 1 larghezza d'impulso / simmetria), lamt (ottave
            o profondita' della PW), lon (0 = LFO fermo: lman espressione della posizione manuale -1..1), oct (0 anello, 1
            fuzz d'ottava a raddrizzatore con drive modulato dall'LFO), tim (TIMBRE 0..1, se timbre=1), mix (0..1), out,
            feed (frazione di portante che passa), tri=1 (LFO triangolare al posto del seno). */
        class Ring5 final : public Effect
        {
        public:
            explicit Ring5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                drive = val (o, "drive", 1.0); cf = val (o, "cf", 400.0); wave = val (o, "wave", 0.0); rate = val (o, "rate", 1.0);
                lshape = val (o, "lshape", 0.0); ltgt = val (o, "ltgt", 0.0); lamt = val (o, "lamt", 0.0); lon = val (o, "lon", 1.0);
                lman = val (o, "lman", 0.0); oct = val (o, "oct", 0.0); tim = val (o, "tim", 0.5); mix = val (o, "mix", 1.0); out = val (o, "out", 1.0);
                vk = o.num ("vk", 0.5); feed = o.num ("feed", 0.002); timbre = o.num ("timbre", 0) > 0.5; triLfo = o.num ("tri", 0) > 0.5;
                octG = o.num ("octg", 4.0);
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / sub, 0.006); sm.snap (*this);
                xfK = 1.0 - std::exp (-1.0 / (0.005 * fs));
                update(); std::copy (tgt, tgt + nT, cur);
                for (int k = 0; k < 4; ++k) wg[k] = k == waveT ? 1.0 : 0.0;
                sqS = lshapeT; octS = octT; lonS = lonT;
                reset();
            }
            void reset() override { ph = 0; lph = 0; lfoSq = 0; dcp.reset(); tl.reset(); tp.reset(); th.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += sub)
                {
                    const int len = std::min (sub, n - i0);
                    double from[nT]; std::copy (cur, cur + nT, from);
                    if (sm.tick (*this)) update();
                    for (int k = 0; k < len; ++k)
                    {
                        const double u = (double) (k + 1) / len;
                        for (int j = 0; j < nT; ++j) cur[j] = from[j] + u * (tgt[j] - from[j]);
                        // 0 drive, 1 log cf, 2 rate, 3 lamt, 4 lman, 5 mix, 6 out, 7 tim
                        sqS += (lshapeT - sqS) * xfK; octS += (octT - octS) * xfK; lonS += (lonT - lonS) * xfK;
                        for (int w = 0; w < 4; ++w) wg[w] += ((w == waveT ? 1.0 : 0.0) - wg[w]) * xfK;
                        // LFO (seno o triangolo, quadra arrotondata), fermabile: posizione manuale
                        lph += cur[2] / fs; lph -= std::floor (lph);
                        const double ls = triLfo ? (lph < 0.5 ? 4 * lph - 1 : 3 - 4 * lph) : std::sin (2 * kPi * lph);
                        const double lq = std::tanh (8.0 * std::sin (2 * kPi * lph)) / std::tanh (8.0);
                        const double lfo = lonS * ((1.0 - sqS) * ls + sqS * lq) + (1.0 - lonS) * cur[4];
                        // portante / VCO
                        const bool pwm = ltgtT > 0.5;
                        const double fm = pwm ? 0.0 : cur[3] * lfo;
                        const double f = std::clamp (std::exp (cur[1]) * std::exp2 (fm), 0.05, 0.45 * fs);
                        const double dt = f / fs;
                        ph += dt; ph -= std::floor (ph);
                        const double pwv = pwm ? std::clamp (0.5 + 0.45 * cur[3] * lfo, 0.05, 0.95) : 0.5;
                        double wv[4];
                        waves (ph, dt, pwv, pwm ? pwv : 0.5, wv);
                        const double car = wg[0] * wv[0] + wg[1] * wv[1] + wg[2] * wv[2] + wg[3] * wv[3];
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = ch[c] + i0;
                            const double dry = x[k];
                            const double v = vk * std::tanh (dry * cur[0] / vk);
                            double wet = v * car + feed * car;
                            if (octS > 1e-4)
                            {
                                // fuzz d'ottava: raddrizzatore a doppia semionda dopo un guadagno modulato dall'LFO
                                const double gq = octG * cur[0] * std::max (0.05, 1.0 + 0.8 * lfo * cur[3] * lonS);
                                const double r = vk * std::abs (std::tanh (dry * gq / vk));
                                wet = (1.0 - octS) * wet + octS * r;
                            }
                            if (timbre) wet = timbreTick (c, wet, cur[7]);
                            wet -= dcp.lp (c, wet, dcG);              // condensatore d'uscita
                            x[k] = (float) ((dry + cur[5] * (wet - dry)) * cur[6]);
                        }
                    }
                }
            }
        private:
            /** TIMBRE: agli estremi taglia gli acuti, al centro passa dai medi scavati (sotto meta') a quelli esaltati. */
            inline double timbreTick (int c, double x, double t) noexcept
            {
                const double lpF = 2500.0 * std::pow (8.0, 1.0 - std::abs (2.0 * t - 1.0));     // 2.5 kHz agli estremi, 20 kHz al centro
                const double y = tl.lp (c, x, poleG (lpF, fs));
                const double lo = tp.lp (c, y, poleG (500.0, fs));
                const double hiP = th.lp (c, y, poleG (2500.0, fs));
                const double mid = hiP - lo;                                     // banda 500 Hz - 2.5 kHz
                const double gm = dbg (24.0 * (t - 0.5));                         // -12 .. +12 dB
                return y + (gm - 1.0) * mid;
            }
            void update()
            {
                const auto* p = sm.v;
                tgt[0] = std::max (1e-3, drive (p, def)); tgt[1] = std::log (std::clamp (cf (p, def), 0.05, 0.45 * fs));
                tgt[2] = std::max (0.0, rate (p, def)); tgt[3] = lamt (p, def); tgt[4] = std::clamp (lman (p, def), -1.0, 1.0);
                tgt[5] = std::clamp (mix (p, def), 0.0, 1.0); tgt[6] = std::max (0.0, out (p, def)); tgt[7] = std::clamp (tim (p, def), 0.0, 1.0);
                waveT = std::clamp ((int) std::lround (wave (p, def)), 0, 3);
                lshapeT = lshape (p, def) > 0.5 ? 1.0 : 0.0; ltgtT = ltgt (p, def); octT = oct (p, def) > 0.5 ? 1.0 : 0.0;
                lonT = lon (p, def) > 0.5 ? 1.0 : 0.0;
                dcG = poleG (12.0, fs);
            }
            static constexpr int nT = 8;
            Val drive, cf, wave, rate, lshape, ltgt, lamt, lon, lman, oct, tim, mix, out;
            double vk = 0.5, feed = 0.002, octG = 4, cur[nT] {}, tgt[nT] {}, wg[4] {}, xfK = 0.004;
            double lshapeT = 0, ltgtT = 0, octT = 0, lonT = 1, sqS = 0, octS = 0, lonS = 1, ph = 0, lph = 0, lfoSq = 0, dcG = 0.001;
            int waveT = 0;
            bool timbre = false, triLfo = false;
            double fs = 48000;
            static constexpr int sub = 16;
            Smooth sm;
            Pole dcp, tl, tp, th;
        };

        //==============================================================================
        /** i5murf: banco di 8 passa-banda risonanti in parallelo (frequenze fisse per banco), ciascuno con il suo VCA
            animato da una sequenza di 16 passi (pattern), inviluppo attacco/decadimento per passo e LFO sinusoidale sulle
            frequenze. Opzioni: banks=f1:..:f8|f1:..:f8 (Hz), q, drive (espr, guadagno + tanh), out (espr), mix (espr),
            rate (espr, passi al secondo), env (espr 0..1: forma attacco/decadimento, 0.5 simmetrica), pat (espr, indice
            0..11), bank (espr 0/1, sceglie anche il gruppo dei pattern), lfo (espr 0/1), lfod (ottave), lfor (rapporto
            fra la frequenza dell'LFO e quella dei passi), stereo=1 (filtri dispari a sinistra, pari a destra). */
        class Murf5 final : public Effect
        {
        public:
            explicit Murf5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                const auto bk = splitBy (o.str ("banks", "110:160:240:350:525:775:1200:1800|200:300:450:675:1000:1500:2200:3400"), "|");
                for (int b = 0; b < 2; ++b)
                {
                    const auto fl = splitBy (bk[(size_t) std::min ((int) bk.size() - 1, b)], ":");
                    for (int i = 0; i < 8; ++i) freqs[b][i] = i < (int) fl.size() ? std::atof (fl[(size_t) i].c_str()) : 200.0 * std::pow (1.5, i);
                }
                qv = o.num ("q", 6.0);
                drive = val (o, "drive", 1.0); out = val (o, "out", 1.0); mix = val (o, "mix", 1.0); rate = val (o, "rate", 4.0);
                env = val (o, "env", 0.5); pat = val (o, "pat", 0.0); bank = val (o, "bank", 1.0); lfo = val (o, "lfo", 0.0);
                lfod = o.num ("lfod", 0.6); lfor = o.num ("lfor", 0.125); stereo = o.num ("stereo", 1) > 0.5; vk = o.num ("vk", 0.6);
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / sub, 0.006); sm.snap (*this);
                fK = 1.0 - std::exp (-1.0 / (0.020 * fs));
                update(); std::copy (tgt, tgt + nT, cur);
                for (int i = 0; i < 8; ++i) lf[i] = std::log (freqs[bankT][i]);
                lfoS = lfoT;
                reset();
            }
            void reset() override { svf.reset(); for (auto& v : bank8) v.reset(); for (auto& e : ev) e = 0; for (auto& a : att) a = false; phase = 1.0; step = -1; lph = 0; }
            float readout (int i) const override { return i == 0 ? (float) step : (float) ev[std::clamp (i - 1, 0, 7)]; }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += sub)
                {
                    const int len = std::min (sub, n - i0);
                    double from[nT]; std::copy (cur, cur + nT, from);
                    if (sm.tick (*this)) update();
                    const double T = 1.0 / std::max (0.05, tgt[3]);
                    const double a = std::clamp (tgt[4], 0.0, 1.0);
                    const double ta = std::max (0.002, 0.95 * T * std::pow (a, 1.6)), td = std::max (0.006, 1.4 * T * std::pow (1.0 - a, 1.3) + 0.03 * T);
                    const double kA = 1.0 - std::exp (-1.0 / (ta * fs)), kD = 1.0 - std::exp (-1.0 / (td * fs));
                    for (int k = 0; k < len; ++k)
                    {
                        const double u = (double) (k + 1) / len;
                        for (int j = 0; j < nT; ++j) cur[j] = from[j] + u * (tgt[j] - from[j]);
                        // sequenza: un passo ogni 1/rate secondi; i filtri del passo ripartono con l'attacco
                        phase += cur[3] / fs;
                        if (phase >= 1.0)
                        {
                            phase -= std::floor (phase);
                            step = (step + 1) & 15;
                            const int mask = kPatterns[bankT][patT][step];
                            for (int f = 0; f < 8; ++f) if (mask & (1 << f)) att[f] = true;
                        }
                        lfoS += (lfoT - lfoS) * fK;
                        lph += cur[3] * lfor / fs; lph -= std::floor (lph);
                        const double sw = lfoS * lfod * std::sin (2 * kPi * lph);
                        double l = 0, r = 0;
                        for (int f = 0; f < 8; ++f)
                        {
                            if (att[f]) { ev[f] += (1.02 - ev[f]) * kA; if (ev[f] >= 1.0) { ev[f] = 1.0; att[f] = false; } }
                            else ev[f] -= ev[f] * kD;
                            lf[f] += (std::log (freqs[bankT][f]) - lf[f]) * fK;
                        }
                        double gs[8];
                        for (int f = 0; f < 8; ++f) gs[f] = gOf (std::exp (lf[f] + sw * 0.6931471805599453 * (f % 2 ? -1.0 : 1.0)), fs);
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = ch[c] + i0;
                            const double dry = x[k];
                            const double v = vk * std::tanh (dry * cur[0] / vk);
                            l = r = 0;
                            for (int f = 0; f < 8; ++f)
                            {
                                double lp, bp, hp;
                                bank8[f].tick (c, v, gs[f], 1.0 / qv, 0.0, lp, bp, hp);
                                const double y = bp / qv * ev[f];
                                if (f % 2 == 0) l += y; else r += y;
                            }
                            double wet;
                            if (stereo && numCh == 2) wet = c == 0 ? 2.0 * l : 2.0 * r;
                            else wet = l + r;
                            wet *= norm;
                            x[k] = (float) ((dry + cur[2] * (wet - dry)) * cur[1]);
                        }
                    }
                }
            }
        private:
            void update()
            {
                const auto* p = sm.v;
                tgt[0] = std::max (1e-3, drive (p, def)); tgt[1] = std::max (0.0, out (p, def)); tgt[2] = std::clamp (mix (p, def), 0.0, 1.0);
                tgt[3] = std::max (0.05, rate (p, def)); tgt[4] = env (p, def);
                patT = std::clamp ((int) std::lround (pat (p, def)), 0, 11);
                bankT = std::clamp ((int) std::lround (bank (p, def)), 0, 1);
                lfoT = lfo (p, def) > 0.5 ? 1.0 : 0.0;
            }
            static constexpr int nT = 5;
            // 24 sequenze di 16 passi (maschere degli 8 filtri, bit 0 = filtro piu' basso): 12 per il banco BASS, 12 per
            // MIDS. Stime ispirate ai tipi di animazione del MuRF (scale ascendenti/discendenti, arpeggi, alternanze,
            // accordi, ritmi sincopati): le sequenze originali non sono pubblicate.
            static constexpr unsigned char kPatterns[2][12][16] = {
                {   { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 },
                    { 128, 64, 32, 16, 8, 4, 2, 1, 128, 64, 32, 16, 8, 4, 2, 1 },
                    { 1, 4, 16, 64, 2, 8, 32, 128, 1, 4, 16, 64, 2, 8, 32, 128 },
                    { 3, 0, 12, 0, 48, 0, 192, 0, 3, 0, 12, 0, 48, 0, 192, 0 },
                    { 1, 16, 2, 32, 4, 64, 8, 128, 1, 16, 2, 32, 4, 64, 8, 128 },
                    { 17, 0, 34, 0, 68, 0, 136, 0, 68, 0, 34, 0, 17, 0, 0, 0 },
                    { 1, 1, 8, 0, 2, 2, 16, 0, 4, 4, 32, 0, 8, 8, 64, 128 },
                    { 85, 0, 170, 0, 85, 0, 170, 0, 85, 0, 170, 0, 85, 170, 85, 170 },
                    { 1, 2, 1, 4, 1, 8, 1, 16, 1, 32, 1, 64, 1, 128, 1, 64 },
                    { 9, 0, 0, 36, 0, 0, 144, 0, 9, 0, 36, 0, 144, 0, 72, 0 },
                    { 255, 0, 0, 0, 129, 0, 66, 0, 36, 0, 24, 0, 36, 0, 66, 0 },
                    { 1, 3, 7, 15, 31, 63, 127, 255, 254, 252, 248, 240, 224, 192, 128, 0 } },
                {   { 128, 64, 32, 16, 8, 4, 2, 1, 2, 4, 8, 16, 32, 64, 128, 0 },
                    { 1, 8, 2, 16, 4, 32, 8, 64, 16, 128, 32, 64, 16, 8, 4, 2 },
                    { 5, 0, 10, 0, 20, 0, 40, 0, 80, 0, 160, 0, 80, 0, 40, 0 },
                    { 129, 66, 36, 24, 36, 66, 129, 0, 129, 66, 36, 24, 36, 66, 129, 0 },
                    { 16, 0, 16, 8, 0, 8, 4, 0, 4, 2, 0, 2, 1, 0, 1, 0 },
                    { 32, 4, 64, 8, 128, 16, 64, 8, 32, 4, 16, 2, 8, 1, 4, 2 },
                    { 21, 0, 0, 42, 0, 0, 84, 0, 168, 0, 0, 84, 0, 0, 42, 0 },
                    { 2, 2, 32, 32, 4, 4, 64, 64, 8, 8, 128, 128, 16, 16, 1, 1 },
                    { 136, 0, 68, 0, 34, 0, 17, 0, 34, 0, 68, 0, 136, 0, 0, 255 },
                    { 1, 0, 4, 2, 0, 8, 4, 0, 16, 8, 0, 32, 16, 0, 64, 128 },
                    { 195, 0, 60, 0, 195, 0, 60, 0, 24, 36, 66, 129, 66, 36, 24, 0 },
                    { 6, 0, 24, 0, 96, 0, 129, 0, 96, 0, 24, 0, 6, 0, 0, 0 } } };
            double freqs[2][8] {};
            double qv = 6, lfod = 0.6, lfor = 0.125, vk = 0.6, norm = 2.2;
            bool stereo = true;
            Val drive, out, mix, rate, env, pat, bank, lfo;
            double cur[nT] {}, tgt[nT] {}, fK = 0.001, lf[8] {}, ev[8] {}, phase = 1, lph = 0, lfoT = 0, lfoS = 0;
            bool att[8] {};
            int patT = 0, bankT = 0, step = -1;
            double fs = 48000;
            static constexpr int sub = 16;
            Smooth sm;
            Svf svf, bank8[8];
        };
        constexpr unsigned char Murf5::kPatterns[2][12][16];

        //==============================================================================
        /** i5freqbox: VCO con forma d'onda continua (triangolo -> rampa discendente -> quadra -> impulso stretto),
            sincronizzazione forte sul passaggio per lo zero del segnale d'ingresso (comparatore con isteresi), FM
            esponenziale dal segnale d'ingresso e inviluppo dell'ingresso sulla frequenza; l'uscita del VCO passa da un
            VCA comandato dallo stesso inviluppo. Opzioni (espressioni): drive (guadagno d'ingresso, tanh con vk V), freq
            (Hz), wave (0..1), sync (0/1), fm (ottave per volt del segnale preamplificato), envamt (ottave a inviluppo
            pieno), envg (dal segnale all'inviluppo 0..1), atk rel (ms), mix (0..1), out, os. */
        class FreqBox5 final : public Effect
        {
        public:
            explicit FreqBox5 (const ModelDef& d) : Effect (d)
            {
                const Opts o (d.config != nullptr ? d.config : "");
                drive = val (o, "drive", 1.0); freq = val (o, "freq", 200.0); wave = val (o, "wave", 0.0); sync = val (o, "sync", 0.0);
                fm = val (o, "fm", 0.0); envamt = val (o, "envamt", 0.0); envg = val (o, "envg", 4.0); atk = val (o, "atk", 2.0);
                rel = val (o, "rel", 80.0); mix = val (o, "mix", 1.0); out = val (o, "out", 1.0);
                vk = o.num ("vk", 0.5); hyst = o.num ("hyst", 0.01);
                osLog2 = osOf (o, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                xfK = 1.0 - std::exp (-1.0 / (0.005 * fs));
                update(); std::copy (tgt, tgt + nT, cur); syncS = syncT;
                reset();
            }
            void reset() override { ph = 0; env = 0; cmp = false; over.reset(); for (auto& v : dcs) v = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double dcG = poleG (8.0, fs);
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    double from[nT]; std::copy (cur, cur + nT, from);
                    if (sm.tick (*this)) update();
                    for (int k = 0; k < len; ++k)
                    {
                        const double u = (double) (k + 1) / len;
                        for (int j = 0; j < nT; ++j) cur[j] = from[j] + u * (tgt[j] - from[j]);
                        // 0 drive, 1 log freq, 2 wave, 3 fm, 4 envamt, 5 mix, 6 out
                        syncS += (syncT - syncS) * xfK;
                        double xin = 0;
                        for (int c = 0; c < numCh; ++c) xin += data[c][i0 + k];
                        xin /= numCh;
                        const double xd = vk * std::tanh (xin * cur[0] / vk);
                        const double a = std::abs (xd);
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        const double e = std::clamp (envgV * env, 0.0, 1.0);
                        const double f = std::clamp (std::exp (cur[1]) * std::exp2 (cur[3] * xd / vk + cur[4] * e), 1.0, 0.45 * fs);
                        const double dt = f / fs;
                        ph += dt;
                        bool reset = false;
                        // comparatore con isteresi: fronte di salita -> sincronizzazione del VCO
                        if (! cmp && xd > hyst) { cmp = true; reset = syncS > 0.5; }
                        else if (cmp && xd < -hyst) cmp = false;
                        if (reset) ph = 0.0;
                        ph -= std::floor (ph);
                        double wv[4];
                        const double w = std::clamp (cur[2], 0.0, 1.0) * 3.0;
                        const double pw = w > 2.0 ? 0.5 - 0.4 * (w - 2.0) : 0.5;
                        waves (ph, dt, pw, 0.5, wv);
                        // dissolvenza continua tra le forme: triangolo -> rampa -> quadra -> impulso
                        double vco;
                        if (w < 1.0) vco = (1.0 - w) * wv[1] + w * wv[2];
                        else if (w < 2.0) vco = (2.0 - w) * wv[2] + (w - 1.0) * wv[3];
                        else vco = wv[3];
                        const double fx = 0.5 * vco * e;
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double dry = x[k];
                            double y = fx;
                            dcs[c] += (y - dcs[c]) * dcG;
                            y -= dcs[c];
                            x[k] = (float) ((dry + cur[5] * (y - dry)) * cur[6]);
                        }
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update()
            {
                const auto* p = sm.v;
                tgt[0] = std::max (1e-3, drive (p, def)); tgt[1] = std::log (std::clamp (freq (p, def), 1.0, 0.4 * fs));
                tgt[2] = wave (p, def); tgt[3] = fm (p, def); tgt[4] = envamt (p, def);
                tgt[5] = std::clamp (mix (p, def), 0.0, 1.0); tgt[6] = std::max (0.0, out (p, def));
                syncT = sync (p, def) > 0.5 ? 1.0 : 0.0;
                envgV = envg (p, def);
                eA = std::exp (-1.0 / (std::max (0.05, atk (p, def)) * 0.001 * fs));
                eR = std::exp (-1.0 / (std::max (0.5, rel (p, def)) * 0.001 * fs));
            }
            static constexpr int nT = 7;
            Val drive, freq, wave, sync, fm, envamt, envg, atk, rel, mix, out;
            double vk = 0.5, hyst = 0.01, cur[nT] {}, tgt[nT] {}, xfK = 0.004, syncT = 0, syncS = 0, envgV = 4, eA = 0, eR = 0;
            double ph = 0, env = 0, dcs[2] {};
            bool cmp = false;
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
        };
    }

    std::unique_ptr<Effect> makeI5Dyn (const ModelDef& d, const std::string& type)
    {
        if (type == "i5gate")    return std::make_unique<Gate5> (d);
        if (type == "i5vca")     return std::make_unique<VcaVol5> (d);
        if (type == "i5ab")      return std::make_unique<AbSel5> (d);
        if (type == "i5auto" || type == "i5smooth") return std::make_unique<Wrap5> (d);
        if (type == "i5acou")    return std::make_unique<Acou5> (d);
        if (type == "i5para")    return std::make_unique<Para5> (d);
        if (type == "i5envf")    return std::make_unique<EnvF5> (d);
        if (type == "i5ladder")  return std::make_unique<Ladder5> (d);
        if (type == "i5ring")    return std::make_unique<Ring5> (d);
        if (type == "i5murf")    return std::make_unique<Murf5> (d);
        if (type == "i5freqbox") return std::make_unique<FreqBox5> (d);
        return nullptr;
    }
}
