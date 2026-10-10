/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 4 (Ibanez): compressori, EQ, filtri, wah e utility ("type=i4...").

    Tipi (config = istruzioni separate da ';' con opzioni "chiave=valore"; i valori dei componenti sono
    espressioni dei comandi con la sintassi della famiglia Circuit: pot, potr, taper, lin, log, sw, par, inv, div):

      i4net    circuito lineare qualsiasi (R, C, L, op-amp ideali) risolto con l'analisi nodale e i modelli
               "compagni" trapezoidali degli elementi reattivi (come il wah a induttore di FxFilters.cpp):
               EQ grafici a gyrator, EQ parametrici, wah a filtro attivo (WH10 MFB, WD7 a variabili di stato),
               auto-wah a fotoaccoppiatore (AW5), simulazioni di cassa. Istruzioni:
                 in <nodo> | out <nodo> | R a b val | C a b val | L a b val | O piu' meno uscita (op-amp ideale)
                 W a b (fotoresistenza, vedi ldr=)
               Opzioni: gain=espr (guadagno lineare d'uscita) rail=V (escursione dell'op-amp d'uscita, morbida)
                 os=1|2 warp=Hz (frequenza della bilineare esatta) xfade=ms (dissolvenza sui selettori)
                 upd=campioni (passo di ricalcolo della rete mentre i comandi si muovono: 16, o 64 per le reti grandi)
                 ldr=pedal ldrctl=i ldrr=Rpunta,Rtallone ldrt=on,off(ms)   fotoresistenza mossa dal bilanciere
                 ldr=env ldrr=Ron,Rbuio,gamma ldrt=on,off envg=espr envth=V enva=ms envr=ms envmax=V
                                                                         fotoresistenza accesa da un LED pilotato
                                                                         dall'inviluppo dell'ingresso
                 auto=i,passo pedal=i delay=espr(s)    accensione automatica col bilanciere (WD7: AUTO SW;
                                                        auto=-2: sempre automatica, WD7JR)
      i4ross   compressore a OTA in retroazione (Ross/Dyna, Ibanez CP-835, CP9, CP10, CP5): pre-enfasi
               d'ingresso, OTA con tanh, carico RC, invertitore di fase, raddrizzatori a transistor risolti con
               Newton, condensatore del rivelatore con rilascio da VCC e attacco in serie, Iabc dall'inseguitore.
      i4opto   compressore ottico (CP-830): op-amp non invertente con la fotoresistenza in retroazione,
               rivelatore a transistor che accende il LED.
      i4vca    compressore a VCA in derivazione (TC999): OTA come resistenza variabile verso massa, stadio di
               SUSTAIN, raddrizzatore di precisione, ATTACK, invertitore della tensione di controllo.
      i4af     filtro d'inviluppo a variabili di stato (AF-201, AF9, AFL, AW7 con la distorsione prima o dopo).
      i4lofi   lo-fi (LF7): ingresso per modo, distorsione a op-amp, passa-alto e passa-basso a OTA.
      i4dnr    riduttore di rumore dinamico LM1894 (NB10).
      i4hush   HUSH (espansore verso il basso + passa-basso dinamico, EC30/EC50).
      i4gate   noise gate con soglia a due gamme (PTGATE).
      i4peq    EQ parametrico a biquad analogici (PPE1 a controllo digitale, PTEQ).
      i4vol    volume stereo con modo bilanciere (VL10);  i4pan  pan a pedale (ST-810);
      i4loop   selettore di due loop con i livelli (LS10).
      i4chain  catena di sotto-effetti delle famiglie esistenti (fuzz Circuit + wah a induttore, preamp a valvola +
               compressore, due wah in parallelo...): "type=i4chain mode=ser|par || Famiglia@on=i,passo@w=espr: config || ..."
               (nella config dei sotto-effetti "subtype=" diventa "type=").

    Tutta la memoria e' allocata nel costruttore o in prepare(); nessuna allocazione nel thread audio. I comandi
    continui sono levigati (due poli), i selettori che cambiano la rete fanno una dissolvenza di 20 ms tra la rete
    vecchia e quella nuova: niente click.
*/

#include "FxStage4.h"
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
        constexpr double kVt = 0.02585;

        //==============================================================================
        // Testo della config
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

        std::vector<std::string> words (const std::string& s)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (i < s.size())
            {
                while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i;
                size_t j = i;
                while (j < s.size() && ! std::isspace ((unsigned char) s[j])) ++j;
                if (j > i) out.push_back (s.substr (i, j - i));
                i = j;
            }
            return out;
        }

        /** Istruzioni separate da ';': le parole "chiave=valore" sono opzioni, le altre righe sono elementi. */
        struct RawConfig
        {
            std::map<std::string, std::string> opts;
            std::vector<std::vector<std::string>> lines;

            explicit RawConfig (const std::string& text)
            {
                for (const auto& st : splitBy (text, ";"))
                {
                    auto w = words (st);
                    if (w.empty()) continue;
                    if (w[0].find ('=') != std::string::npos)
                    {
                        for (const auto& t : w)
                            if (const auto eq = t.find ('='); eq != std::string::npos)
                                opts[t.substr (0, eq)] = t.substr (eq + 1);
                    }
                    else lines.push_back (w);
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

        Val val (const RawConfig& rc, const char* key, double fallback) { Val v; v.set (rc.str (key), fallback); return v; }

        /** Comandi levigati da due poli in cascata; selettori e levette scattano subito. */
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

        int stepOf (const ModelDef& d, const float* p, int i)
        {
            if (i < 0 || i >= d.numControls) return 0;
            const int s = std::max (1, (int) d.controls[i].steps);
            return std::clamp ((int) std::lround (p[i] * (s - 1)), 0, s - 1);
        }

        inline double softRail (double x, double v) noexcept
        {
            if (v <= 0) return x;
            const double a = x / v, a2 = a * a, a4 = a2 * a2;
            return x / std::sqrt (std::sqrt (std::sqrt (1.0 + a4 * a4)));     // ginocchio dell'op-amp ai binari
        }

        inline double softplus (double x, double k) noexcept { return x > 20 * k ? x : k * std::log1p (std::exp (x / k)); }

        /** Biquad RBJ in double (stato per canale). */
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
            void shelf (double fs, double f, double s, double db, bool high)
            {
                const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::clamp (f, 10.0, fs * 0.45) / fs;
                const double c = std::cos (w), al = std::sin (w) / 2 * std::sqrt ((A + 1 / A) * (1 / s - 1) + 2), sq = 2 * std::sqrt (A) * al;
                if (high) set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                               (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
                else set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                          (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
            }
            void lowpass (double fs, double f, double q)
            {
                const double w = 2 * kPi * std::clamp (f, 10.0, fs * 0.45) / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
                set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
            }
            void highpass (double fs, double f, double q)
            {
                const double w = 2 * kPi * std::clamp (f, 10.0, fs * 0.45) / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
                set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
            }
            inline double tick (int ch, double x) noexcept
            {
                const double y = b0 * x + z1[ch];
                z1[ch] = b1 * x - a1 * y + z2[ch];
                z2[ch] = b2 * x - a2 * y;
                return y;
            }
        };

        /** Passa-alto / passa-basso del primo ordine (bilineare). */
        struct OnePole
        {
            double a = 0, b = 1, x1[2] {}, y1[2] {};
            bool high = false;
            void reset() { x1[0] = x1[1] = y1[0] = y1[1] = 0; }
            void set (double fs, double f, bool hp)
            {
                high = hp;
                const double t = std::tan (kPi * std::clamp (f, 1.0, fs * 0.45) / fs);
                a = (1 - t) / (1 + t); b = high ? 1 / (1 + t) : t / (1 + t);
            }
            inline double tick (int ch, double x) noexcept
            {
                const double y = high ? b * (x - x1[ch]) + a * y1[ch] : b * (x + x1[ch]) + a * y1[ch];
                x1[ch] = x; y1[ch] = y;
                return y;
            }
        };

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

        //==============================================================================
        /** i4net: circuito lineare con op-amp ideali (analisi nodale, modelli compagni trapezoidali). */
        class NetEffect final : public Effect
        {
        public:
            explicit NetEffect (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                auto node = [this] (const std::string& n) -> int
                {
                    if (n == "0" || n == "gnd") return -1;
                    auto it = nodes.find (n);
                    if (it != nodes.end()) return it->second;
                    const int id = (int) nodes.size();
                    nodes[n] = id;
                    return id;
                };
                for (const auto& w : rc.lines)
                {
                    const std::string& k = w[0];
                    if (k == "in" && w.size() >= 2) inNode = node (w[1]);
                    else if (k == "out" && w.size() >= 2) outNode = node (w[1]);
                    else if ((k == "R" || k == "C" || k == "L") && w.size() >= 4)
                    {
                        El e; e.kind = k[0]; e.a = node (w[1]); e.b = node (w[2]);
                        e.v.set (w[3], k == "R" ? 1e3 : k == "C" ? 1e-9 : 1e-3);
                        els.push_back (e);
                    }
                    else if (k == "W" && w.size() >= 3)
                    {
                        El e; e.kind = 'W'; e.a = node (w[1]); e.b = node (w[2]);
                        els.push_back (e);
                    }
                    else if (k == "O" && w.size() >= 4)
                    {
                        El e; e.kind = 'O'; e.a = node (w[1]); e.b = node (w[2]); e.c = node (w[3]);
                        els.push_back (e);
                    }
                }
                numNodes = (int) nodes.size();
                for (int i = 0; i < (int) els.size(); ++i)
                {
                    if (els[(size_t) i].kind == 'C' || els[(size_t) i].kind == 'L') reactive.push_back (i);
                    if (els[(size_t) i].kind == 'O') ++numOps;
                }
                nB = (int) reactive.size();
                for (int k = 0; k < nB; ++k) sign.push_back (els[(size_t) reactive[(size_t) k]].kind == 'C' ? 1 : -1);
                S = numNodes + numOps + 1;
                nW = nB + 1;
                Y.assign ((size_t) (S * S), 0.0);
                Bm.assign ((size_t) (S * nW), 0.0);
                const size_t nc = (size_t) (nW * nW + nB);
                tgt.assign (nc, 0.0); cur.assign (nc, 0.0); inc.assign (nc, 0.0); alt.assign (nc, 0.0);
                for (auto& s : st) { s.v.assign ((size_t) nB, 0.0); s.i.assign ((size_t) nB, 0.0); }
                for (auto& s : sta) { s.v.assign ((size_t) nB, 0.0); s.i.assign ((size_t) nB, 0.0); }
                J.assign ((size_t) nB, 0.0);
                yv.assign ((size_t) nW, 0.0);
                lastVals.assign (els.size(), -1.0);

                gain = val (rc, "gain", 1.0);
                rail = rc.num ("rail", 0.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 1.0)))), 0, 2);
                warpHz = rc.num ("warp", 0.0);
                xfadeMs = rc.num ("xfade", 20.0);
                // fotoresistenza
                const auto ldr = rc.str ("ldr");
                ldrMode = ldr == "pedal" ? 1 : ldr == "env" ? 2 : 0;
                ldrCtl = (int) rc.num ("ldrctl", (double) role (*this, "freq").idx);
                auto r = rc.list ("ldrr");
                ldrA = r.size() > 0 ? r[0] : 27e3; ldrB = r.size() > 1 ? r[1] : 3.2e6; ldrGamma = r.size() > 2 ? r[2] : 0.8;
                auto t = rc.list ("ldrt");
                ldrOnMs = t.size() > 0 ? t[0] : 4.0; ldrOffMs = t.size() > 1 ? t[1] : 25.0;
                envG = val (rc, "envg", 1.0);
                envTh = rc.num ("envth", 0.0);
                envAtkMs = rc.num ("enva", 1.0); envRelMs = rc.num ("envr", 50.0); envMax = rc.num ("envmax", 1e9);
                // accensione automatica
                auto au = rc.list ("auto");
                autoCtl = au.size() > 0 ? (int) au[0] : -1; autoStep = au.size() > 1 ? (int) au[1] : 1;
                pedalCtl = (int) rc.num ("pedal", (double) role (*this, "freq").idx);
                delay = val (rc, "delay", 0.0);
                // validita'
                ok = inNode >= 0 && outNode >= 0 && numNodes > 0;
                stepped.fill (-1);
                // passo di aggiornamento dei coefficienti mentre i comandi si muovono: le reti grandi (EQ) ogni 64
                // campioni (sovracampionati), le piccole (wah, fotoresistenza) ogni 16; in mezzo interpolazione lineare
                sub = std::clamp ((int) rc.num ("upd", S > 24 ? 64.0 : 16.0), 4, 256);
            }

            void prepare (double s, int maxBlock) override
            {
                sr = s;
                over.prepare (osLog2, maxBlock);
                fs = sr * (double) (1 << osLog2);
                K = 2.0 * fs;
                if (warpHz > 0 && warpHz < 0.45 * fs) { const double w = 2 * kPi * warpHz; K = w / std::tan (w / (2 * fs)); }
                sm.setTime (fs / sub, 0.004);
                sm.snap (*this);
                xfadeLen = std::max (1, (int) (xfadeMs * 0.001 * fs));
                ldrK[0] = 1.0 - std::exp (-sub / (std::max (0.1, ldrOnMs) * 0.001 * fs));
                ldrK[1] = 1.0 - std::exp (-sub / (std::max (0.1, ldrOffMs) * 0.001 * fs));
                envA = std::exp (-1.0 / (std::max (0.01, envAtkMs) * 0.001 * fs));
                envR = std::exp (-1.0 / (std::max (0.01, envRelMs) * 0.001 * fs));
                logR = ldrMode == 1 ? targetLogR() : std::log (std::max (1.0, ldrB));
                env = 0;
                for (int i = 0; i < maxControls; ++i) stepped[(size_t) i] = stepOf (def, sm.v, i);
                if (ok) { solve(); std::copy (tgt.begin(), tgt.end(), cur.begin()); }
                autoOn = autoActive() ? pedalPressed() : true;
                wetG = autoOn ? 1.0 : 0.0; offTimer = 0;
                reset();
            }

            /** Diagnostica: 0 = resistenza della fotocellula (ohm), 1 = effetto inserito (accensione automatica). */
            float readout (int index) const override { return index == 0 ? (float) std::exp (logR) : (float) wetG; }

            void reset() override
            {
                for (auto& s : st) { std::fill (s.v.begin(), s.v.end(), 0.0); std::fill (s.i.begin(), s.i.end(), 0.0); }
                xleft = 0;
                over.reset();
            }

            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0 || ! ok) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double wetStep = 1.0 / (0.010 * fs);
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    // comandi, fotoresistenza, accensione automatica
                    const bool moved = sm.tick (*this);
                    bool ldrMoved = false;
                    if (ldrMode != 0)
                    {
                        const double tl = ldrMode == 1 ? targetLogR() : envLogR();
                        const double nl = logR + ldrK[tl < logR ? 0 : 1] * (tl - logR);
                        if (std::abs (nl - logR) > 1e-5) { logR = nl; ldrMoved = true; }
                    }
                    bool switched = false;
                    for (int c = 0; c < def.numControls; ++c)
                        if (def.controls[c].steps >= 2)
                        {
                            const int s = stepOf (def, sm.v, c);
                            if (s != stepped[(size_t) c]) { stepped[(size_t) c] = s; switched = true; }
                        }
                    if (switched)
                    {
                        // dissolvenza: la rete vecchia continua con il suo stato, la nuova parte dallo stesso stato
                        std::copy (cur.begin(), cur.end(), alt.begin());
                        for (int c = 0; c < 2; ++c) { sta[c].v = st[c].v; sta[c].i = st[c].i; }
                        solve();
                        std::copy (tgt.begin(), tgt.end(), cur.begin());
                        std::fill (inc.begin(), inc.end(), 0.0);
                        xleft = xfadeLen;
                    }
                    bool ramp = false;
                    if (! switched && (moved || ldrMoved))
                    {
                        solve();
                        for (size_t j = 0; j < cur.size(); ++j) inc[j] = (tgt[j] - cur[j]) / len;
                        ramp = true;
                    }
                    if (autoActive())
                    {
                        if (pedalPressed()) { autoOn = true; offTimer = 0; }
                        else if (autoOn)
                        {
                            offTimer += len / fs;
                            if (offTimer >= std::max (0.0, delay (sm.v, def))) autoOn = false;
                        }
                    }
                    else autoOn = true;
                    const double wetT = autoOn ? 1.0 : 0.0;

                    for (int k = 0; k < len; ++k)
                    {
                        if (std::abs (wetG - wetT) > 0.0) wetG = wetT > wetG ? std::min (wetT, wetG + wetStep) : std::max (wetT, wetG - wetStep);
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double u = x[k];
                            if (ldrMode == 2 && c == 0)
                            {
                                const double a = std::abs (u);
                                env = a > env ? a + envA * (env - a) : a + envR * (env - a);
                            }
                            double y = tick (cur, st[c], u);
                            if (xleft > 0)
                            {
                                const double ya = tick (alt, sta[c], u);
                                const double g = (double) xleft / xfadeLen;
                                y = y * (1.0 - g) + ya * g;
                            }
                            if (rail > 0) y = softRail (y, rail);
                            x[k] = (float) (u + wetG * (y - u));
                        }
                        if (xleft > 0) --xleft;
                        if (ramp) for (size_t j = 0; j < cur.size(); ++j) cur[j] += inc[j];
                    }
                    if (ramp) std::copy (tgt.begin(), tgt.end(), cur.begin());
                }
                if (over.os) over.os->processSamplesDown (block);
            }

        private:
            struct El { char kind = 'R'; int a = -1, b = -1, c = -1; Val v; };
            struct State { std::vector<double> v, i; };

            bool autoActive() const { return autoCtl == -2 || (autoCtl >= 0 && stepOf (def, sm.v, autoCtl) == autoStep); }
            bool pedalPressed() const { return pedalCtl < 0 || sm.v[pedalCtl] > 0.02f; }

            double targetLogR() const
            {
                const double p = ldrCtl >= 0 ? std::clamp ((double) sm.v[ldrCtl], 0.0, 1.0) : 0.5;
                return std::log (std::max (1.0, ldrA)) + (1.0 - p) * std::log (std::max (1.0, ldrB) / std::max (1.0, ldrA));
            }
            double envLogR() const
            {
                // corrente del LED (unita' relative) -> resistenza della fotocellula Ron * I^-gamma, al buio Rbuio
                const double I = std::max (0.0, std::min (envMax, envG (sm.v, def) * env) - envTh);
                const double r = I > 1e-9 ? ldrA * std::pow (I, -ldrGamma) : ldrB;
                return std::log (std::clamp (r, 1.0, std::max (1.0, ldrB)));
            }

            /** Una passo della rete: J dagli stati, y = W [J; u], aggiornamento degli stati. */
            inline double tick (const std::vector<double>& co, State& s, double u) noexcept
            {
                const double* W = co.data();
                const double* G = co.data() + nW * nW;
                for (int k = 0; k < nB; ++k)
                {
                    const double h = G[k] * s.v[(size_t) k] + s.i[(size_t) k];
                    J[(size_t) k] = sign[(size_t) k] > 0 ? h : -h;
                }
                double out = 0;
                for (int r = 0; r < nW; ++r)
                {
                    const double* row = W + r * nW;
                    double acc = row[nB] * u;
                    for (int k = 0; k < nB; ++k) acc += row[k] * J[(size_t) k];
                    if (r < nB) yv[(size_t) r] = acc; else out = acc;
                }
                for (int k = 0; k < nB; ++k)
                {
                    const double v = yv[(size_t) k];
                    s.v[(size_t) k] = v;
                    s.i[(size_t) k] = G[k] * v - J[(size_t) k];
                }
                return std::isfinite (out) ? out : 0.0;
            }

            /** Analisi nodale con i valori correnti dei comandi -> tgt (matrice W e conduttanze dei rami reattivi). */
            void solve()
            {
                std::fill (Y.begin(), Y.end(), 0.0);
                std::fill (Bm.begin(), Bm.end(), 0.0);
                auto y = [this] (int r, int c) -> double& { return Y[(size_t) (r * S + c)]; };
                auto b = [this] (int r, int c) -> double& { return Bm[(size_t) (r * nW + c)]; };
                auto stamp = [&] (int a, int bb, double g)
                {
                    if (a >= 0) y (a, a) += g;
                    if (bb >= 0) y (bb, bb) += g;
                    if (a >= 0 && bb >= 0) { y (a, bb) -= g; y (bb, a) -= g; }
                };
                int op = 0, rk = 0;
                double* G = tgt.data() + nW * nW;
                for (size_t e = 0; e < els.size(); ++e)
                {
                    const auto& el = els[e];
                    switch (el.kind)
                    {
                        case 'R': stamp (el.a, el.b, 1.0 / std::max (1e-3, el.v (sm.v, def))); break;
                        case 'W': stamp (el.a, el.b, 1.0 / std::exp (logR)); break;
                        case 'C':
                        case 'L':
                        {
                            const double v = el.v (sm.v, def);
                            const double g = el.kind == 'C' ? K * std::max (1e-15, v) : 1.0 / (K * std::max (1e-9, v));
                            stamp (el.a, el.b, g);
                            G[rk] = g;
                            if (el.a >= 0) b (el.a, rk) += 1.0;
                            if (el.b >= 0) b (el.b, rk) -= 1.0;
                            ++rk;
                            break;
                        }
                        case 'O':
                        {
                            const int row = numNodes + op++;
                            if (el.a >= 0) y (row, el.a) += 1.0;
                            if (el.b >= 0) y (row, el.b) -= 1.0;
                            if (el.c >= 0) y (el.c, row) -= 1.0;
                            break;
                        }
                        default: break;
                    }
                }
                const int srow = numNodes + numOps;
                y (srow, inNode) = 1.0;
                y (inNode, srow) -= 1.0;
                b (srow, nB) = 1.0;
                // eliminazione di Gauss con pivot parziale
                for (int k = 0; k < S; ++k)
                {
                    int piv = k;
                    for (int r = k + 1; r < S; ++r) if (std::abs (y (r, k)) > std::abs (y (piv, k))) piv = r;
                    if (std::abs (y (piv, k)) < 1e-300) { singular = true; continue; }
                    if (piv != k)
                    {
                        for (int c = 0; c < S; ++c) std::swap (y (k, c), y (piv, c));
                        for (int c = 0; c < nW; ++c) std::swap (b (k, c), b (piv, c));
                    }
                    const double inv = 1.0 / y (k, k);
                    for (int r = k + 1; r < S; ++r)
                    {
                        const double f = y (r, k) * inv;
                        if (! (std::abs (f) > 0.0)) continue;
                        for (int c = k; c < S; ++c) y (r, c) -= f * y (k, c);
                        for (int c = 0; c < nW; ++c) b (r, c) -= f * b (k, c);
                    }
                }
                for (int k = S - 1; k >= 0; --k)
                    for (int c = 0; c < nW; ++c)
                    {
                        double sum = b (k, c);
                        for (int j = k + 1; j < S; ++j) sum -= y (k, j) * b (j, c);
                        b (k, c) = std::abs (y (k, k)) > 1e-300 ? sum / y (k, k) : 0.0;
                    }
                double* W = tgt.data();
                const double gOut = gain (sm.v, def);
                for (int c = 0; c < nW; ++c)
                {
                    for (int k = 0; k < nB; ++k)
                    {
                        const auto& el = els[(size_t) reactive[(size_t) k]];
                        W[k * nW + c] = (el.a >= 0 ? b (el.a, c) : 0.0) - (el.b >= 0 ? b (el.b, c) : 0.0);
                    }
                    W[nB * nW + c] = b (outNode, c) * gOut;
                }
            }

            std::map<std::string, int> nodes;
            std::vector<El> els;
            std::vector<int> reactive, sign;
            int numNodes = 0, numOps = 0, nB = 0, S = 1, nW = 1, inNode = -1, outNode = -1;
            bool ok = false, singular = false;
            std::vector<double> Y, Bm, tgt, cur, inc, alt, J, yv, lastVals;
            State st[2], sta[2];
            Val gain, delay, envG;
            double rail = 0, warpHz = 0, xfadeMs = 20;
            int osLog2 = 0;
            Over over;
            double sr = 48000, fs = 48000, K = 96000;
            int sub = 16;
            Smooth sm;
            std::array<int, maxControls> stepped {};
            int xleft = 0, xfadeLen = 960;
            // fotoresistenza
            int ldrMode = 0, ldrCtl = -1;
            double ldrA = 27e3, ldrB = 3.2e6, ldrGamma = 0.8, ldrOnMs = 4, ldrOffMs = 25, ldrK[2] { 0.1, 0.1 }, logR = 10;
            double envTh = 0, envAtkMs = 1, envRelMs = 50, envMax = 1e9, envA = 0, envR = 0, env = 0;
            // accensione automatica
            int autoCtl = -1, autoStep = 1, pedalCtl = -1;
            bool autoOn = true;
            double offTimer = 0, wetG = 1;
        };

        //==============================================================================
        /** Raddrizzatore a transistor dei compressori Ross/Dyna: sorgente (Rs) -> condensatore di accoppiamento ->
            base con 1M e diodo di bloccaggio verso massa; giunzione base-emettitore esponenziale (Newton). */
        struct Rectifier
        {
            double rs = 10e3, cr = 10e-9, rb = 1e6, isd = 2.52e-9, nd = 1.752, isb = 1e-14 / 250.0;
            double vcap[2] {}, icap[2] {}, vb[2] {};
            double T = 1.0 / 96000.0, Gs = 0, h = 0;
            void setup (double fs) { T = 1.0 / fs; h = T / (2.0 * cr); Gs = 1.0 / (rs + h); }
            void reset() { for (int c = 0; c < 2; ++c) vcap[c] = icap[c] = vb[c] = 0; }
            /** Corrente di base (A) per la tensione di sorgente vs. */
            inline double tick (int c, double vs) noexcept
            {
                const double Js = Gs * (vcap[c] + h * icap[c]);
                double v = vb[c];
                const double nvd = nd * kVt;
                for (int it = 0; it < 8; ++it)
                {
                    const double ed = std::exp (std::clamp (-v / nvd, -60.0, 60.0));
                    const double eb = std::exp (std::clamp (v / kVt, -60.0, 60.0));
                    const double f = Gs * (vs - v) - Js - v / rb + isd * (ed - 1.0) - isb * (eb - 1.0);
                    const double df = -Gs - 1.0 / rb - isd * ed / nvd - isb * eb / kVt;
                    const double step = std::clamp (f / df, -0.5, 0.5);
                    v -= step;
                    if (std::abs (step) < 1e-7) break;
                }
                if (! std::isfinite (v)) v = 0;
                vb[c] = v;
                const double i = Gs * (vs - v) - Js;
                vcap[c] += h * (i + icap[c]);
                icap[c] = i;
                return std::max (0.0, isb * (std::exp (std::clamp (v / kVt, -60.0, 60.0)) - 1.0));
            }
        };

        /** i4ross: compressore a OTA in retroazione (Ross / Dyna Comp e i derivati Ibanez CP-835, CP9, CP10, CP5).
            Config: rin rz cz cc (rete di pre-enfasi all'ingresso differenziale: vd = vin*rin/(rin+Z), Z = rz||cz + 1/(s*cc)),
            vlin (V, 2Vt per l'OTA nudo), rl cl (carico dell'uscita dell'OTA verso VBIAS), orail (V, escursione), gsplit
            (guadagno dell'invertitore di fase), rabc (resistenza verso Iabc, espressione: SUSTAIN/THRESHOLD), vabc
            (cadute Vbe), vcc rrel cdet (rilascio: condensatore del rivelatore caricato da VCC), ratt (resistenza
            d'attacco in serie ai collettori, espressione), beta, rsc rse (resistenze di sorgente dei raddrizzatori),
            cr rb, level (guadagno d'uscita, espressione), out (dB), enh (dB, espressione) enhf (Hz), os. */
        class RossComp final : public Effect
        {
        public:
            explicit RossComp (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                rin = val (rc, "rin", 2e3); rz = val (rc, "rz", 16e3); cz = val (rc, "cz", 10e-9); cc = val (rc, "cc", 0.0);
                vlin = rc.num ("vlin", 2 * kVt);
                rl = rc.num ("rl", 150e3); cl = rc.num ("cl", 2e-9); orail = rc.num ("orail", 3.8);
                gsplit = rc.num ("gsplit", 0.98);
                rabc = val (rc, "rabc", 20e3); vabc = rc.num ("vabc", 1.2);
                vcc = rc.num ("vcc", 9.0); rrel = val (rc, "rrel", 150e3); cdet = rc.num ("cdet", 10e-6);
                ratt = val (rc, "ratt", 0.0); beta = rc.num ("beta", 250.0);
                rsc = rc.num ("rsc", 10e3); rse = rc.num ("rse", 300.0); crr = rc.num ("cr", 10e-9); rbb = rc.num ("rb", 1e6);
                level = val (rc, "level", 1.0); outDb = rc.num ("out", 0.0);
                enh = val (rc, "enh", 0.0); enhF = rc.num ("enhf", 0.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 2.0)))), 0, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                for (int k = 0; k < 2; ++k)
                {
                    rect[k].rs = k == 0 ? rsc : rse; rect[k].cr = crr; rect[k].rb = rbb; rect[k].isb = 1e-14 / beta;
                    rect[k].setup (fs);
                }
                hpOut.set (fs, 8.0, true);
                update (true);
                reset();
            }
            /** Diagnostica: 0 = tensione del rivelatore (V), 1 = Iabc (uA). */
            float readout (int index) const override
            {
                return index == 0 ? (float) vdet : (float) (softplus (vdet - vabc, 0.03) / rabcV * 1e6);
            }
            void reset() override
            {
                pre.reset(); for (auto& r : rect) r.reset(); hpOut.reset(); enhBq.reset();
                for (int c = 0; c < 2; ++c) { v6[c] = 0; iPrev[c] = 0; }
                vdet = vcc - 0.3; over.reset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double T = 1.0 / fs, a = T / (2.0 * rl * cl), bcl = T / (2.0 * cl);
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    if (sm.tick (*this)) update (false);
                    const double gStep = (gTgt - gOut) / len;
                    for (int k = 0; k < len; ++k)
                    {
                        gOut += gStep;
                        // Iabc dalla tensione del rivelatore (inseguitore + diodo d'ingresso dell'OTA, ginocchio morbido)
                        const double iabc = softplus (vdet - vabc, 0.03) / rabcV;
                        double icTot = 0;
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double vd = pre.tick (c, x[k]);
                            const double io = iabc * std::tanh (vd / vlin);
                            // carico RC dell'uscita dell'OTA (trapezoidale)
                            double v = (v6[c] * (1.0 - a) + bcl * (io + iPrev[c])) / (1.0 + a);
                            iPrev[c] = io;
                            v = softRail (v, orail);
                            v6[c] = v;
                            const double ve = gsplit * v;
                            // invertitore di fase: collettore (-) ed emettitore (+) verso i due raddrizzatori
                            if (c == 0) icTot = beta * (rect[0].tick (0, -ve) + rect[1].tick (0, ve));
                            double y = hpOut.tick (c, ve) * gOut;
                            if (enhF > 0) y = enhBq.tick (c, y);
                            x[k] = (float) y;
                        }
                        // rivelatore: attacco limitato dalla resistenza in serie ai collettori (e dalla saturazione)
                        const double icMax = std::max (0.0, vdet - 0.15) / (rattV + 40.0);
                        const double ic = icTot * icMax / (icTot + icMax + 1e-12);
                        vdet += T / cdet * ((vcc - vdet) / rrelV - ic - iabc / beta);
                        vdet = std::clamp (vdet, 0.0, vcc);
                    }
                    gOut = gTgt;
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update (bool first)
            {
                const auto* p = sm.v;
                // rete di pre-enfasi: vd/vin = rin/(rin + Z), Z = rz/(1+s rz cz) + 1/(s cc)
                const double Ri = std::max (1.0, rin (p, def)), Rz = std::max (1.0, rz (p, def)), Cz = std::max (0.0, cz (p, def));
                const double Cc = std::max (0.0, cc (p, def));
                for (auto& v : pre.bs) v = 0;
                for (auto& v : pre.as) v = 0;
                if (Cc > 0)
                {
                    pre.bs[1] = Ri * Cc; pre.bs[2] = Ri * Rz * Cz * Cc;
                    pre.as[0] = 1.0; pre.as[1] = Rz * Cc + Rz * Cz + Ri * Cc; pre.as[2] = Ri * Rz * Cz * Cc;
                }
                else
                {
                    pre.bs[0] = Ri; pre.bs[1] = Ri * Rz * Cz;
                    pre.as[0] = Ri + Rz; pre.as[1] = Ri * Rz * Cz;
                }
                pre.design (fs, 0.0);
                rabcV = std::max (100.0, rabc (p, def));
                rrelV = std::max (1e3, rrel (p, def));
                rattV = std::max (0.0, ratt (p, def));
                gTgt = std::max (0.0, level (p, def)) * std::pow (10.0, outDb / 20.0);
                if (first) gOut = gTgt;
                if (enhF > 0) enhBq.shelf (fs, enhF, 0.7, enh (p, def), true);
            }

            Val rin, rz, cz, cc, rabc, rrel, ratt, level, enh;
            double vlin = 0.0517, rl = 150e3, cl = 2e-9, orail = 3.8, gsplit = 0.98, vabc = 1.2, vcc = 9, cdet = 10e-6, beta = 250;
            double rsc = 10e3, rse = 300, crr = 10e-9, rbb = 1e6, outDb = 0, enhF = 0;
            double rabcV = 20e3, rrelV = 150e3, rattV = 0, gTgt = 1, gOut = 1, vdet = 8.7, v6[2] {}, iPrev[2] {};
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
            AnalogTF pre;
            Rectifier rect[2];
            OnePole hpOut;
            BQ enhBq;
        };

        //==============================================================================
        /** i4opto: compressore ottico (Ibanez CP-830). Op-amp non invertente: guadagno 1 + (Rf || Rldr)/(Rg + 1/(s Cg));
            il rivelatore (transistor) accende il LED della fotocellula con una costante di rilascio lunga.
            Config: rf (espressione, SUSTAIN) rg cg ldrr=Ron,Rbuio,gamma ldrt=on,off(ms) envg (guadagno del rivelatore)
            envth (V di soglia) enva envr (ms) rail (V) level (espressione) out (dB) os. */
        class OptoComp final : public Effect
        {
        public:
            explicit OptoComp (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                rf = val (rc, "rf", 250e3); rg = val (rc, "rg", 1.5e3); cg = rc.num ("cg", 1e-6);
                auto r = rc.list ("ldrr"); ron = r.size() > 0 ? r[0] : 5e3; rdark = r.size() > 1 ? r[1] : 5e6; gam = r.size() > 2 ? r[2] : 0.8;
                auto t = rc.list ("ldrt"); onMs = t.size() > 0 ? t[0] : 8; offMs = t.size() > 1 ? t[1] : 120;
                envg = rc.num ("envg", 2.0); envth = rc.num ("envth", 0.6);
                enva = rc.num ("enva", 2.0); envr = rc.num ("envr", 300.0);
                rail = rc.num ("rail", 3.0); level = val (rc, "level", 1.0); outDb = rc.num ("out", 0.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 2.0)))), 0, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                kOn = 1.0 - std::exp (-1.0 / (onMs * 0.001 * fs)); kOff = 1.0 - std::exp (-1.0 / (offMs * 0.001 * fs));
                eA = std::exp (-1.0 / (enva * 0.001 * fs)); eR = std::exp (-1.0 / (envr * 0.001 * fs));
                hpIn.set (fs, 7.0, true);
                update (true);
                reset();
            }
            void reset() override { env = 0; logR = std::log (rdark); for (int c = 0; c < 2; ++c) zc[c] = zx[c] = 0; hpIn.reset(); over.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double T = 1.0 / fs;
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    if (sm.tick (*this)) update (false);
                    const double gStep = (gTgt - gOut) / len;
                    for (int k = 0; k < len; ++k)
                    {
                        gOut += gStep;
                        // fotocellula: R = Ron * I^-gamma (dinamica asimmetrica della CdS)
                        const double I = std::max (0.0, env * envg - envth);
                        const double tl = I > 1e-9 ? std::log (std::clamp (ron * std::pow (I, -gam), 10.0, rdark)) : std::log (rdark);
                        logR += (tl < logR ? kOn : kOff) * (tl - logR);
                        const double rldr = std::exp (logR);
                        const double rfe = rfV * rldr / (rfV + rldr);
                        // guadagno: 1 + Zf/Zg con Zg = Rg + 1/(s Cg): passa-alto del primo ordine sulla parte amplificata
                        const double wc = 1.0 / (rgV * cg), tt = std::tan (std::min (wc * T / 2, 1.5));
                        const double ag = (1 - tt) / (1 + tt), bg = 1 / (1 + tt);
                        double peak = 0;
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double u = hpIn.tick (c, x[k]);
                            // parte amplificata: (Rf/Rg) * passa-alto(u) (zc = stato del passa-alto)
                            const double hp = bg * (u - zx[c]) + ag * zc[c];
                            zx[c] = u; zc[c] = hp;
                            double y = softRail (u + rfe / rgV * hp, rail);
                            peak = std::max (peak, std::abs (y));
                            x[k] = (float) (y * gOut);
                        }
                        env = peak > env ? peak + eA * (env - peak) : peak + eR * (env - peak);
                    }
                    gOut = gTgt;
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update (bool first)
            {
                rfV = std::max (10.0, rf (sm.v, def)); rgV = std::max (10.0, rg (sm.v, def));
                gTgt = std::max (0.0, level (sm.v, def)) * std::pow (10.0, outDb / 20.0);
                if (first) gOut = gTgt;
            }
            Val rf, rg, level;
            double cg = 1e-6, ron = 5e3, rdark = 5e6, gam = 0.8, onMs = 8, offMs = 120, envg = 2, envth = 0.6, enva = 2, envr = 300;
            double rail = 3, outDb = 0, rfV = 250e3, rgV = 1.5e3, gTgt = 1, gOut = 1, kOn = 0.01, kOff = 0.001, eA = 0, eR = 0;
            double env = 0, logR = 15, zc[2] {}, zx[2] {};
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
            OnePole hpIn;
        };

        //==============================================================================
        /** i4vca: compressore con l'OTA usato come resistenza variabile in derivazione (Ibanez TC999).
            Sorgente -> rser (470k) -> nodo X -> cser (100n) + Rvca verso massa, Rvca = 1/(gm * kin * gmk), gm = Iabc/2Vt;
            stadio non invertente SUSTAIN: 1 + Rf/(Rg + 1/(s Cg)) con Cf in parallelo a Rf; raddrizzatore di precisione
            (guadagno kdet) sul condensatore del rivelatore (attacco att ms, rilascio rel ms); invertitore 470k/470k;
            Iabc = (Vctl - vctl)/rctl. Config: rser cser kin gmk rf(espr) rg cg cf rail kdet vb vd att(espr, ms) rel
            rctl vctl level(espr) out(dB) os. */
        class ShuntComp final : public Effect
        {
        public:
            explicit ShuntComp (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                rser = rc.num ("rser", 470e3); cser = rc.num ("cser", 100e-9); kin = rc.num ("kin", 0.0035); gmk = rc.num ("gmk", 1.0);
                rf = val (rc, "rf", 50e3); rg = rc.num ("rg", 3e3); cg = rc.num ("cg", 1e-6); cf = rc.num ("cf", 330e-12);
                rail = rc.num ("rail", 3.8); kdet = rc.num ("kdet", 7.2); vb = rc.num ("vb", 4.5); vdd = rc.num ("vd", 0.6);
                att = val (rc, "att", 10.0); relMs = rc.num ("rel", 520.0); rctl = rc.num ("rctl", 47e3); vctl = rc.num ("vctl", 2.4);
                level = val (rc, "level", 1.0); outDb = rc.num ("out", 0.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 2.0)))), 0, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                hpG.set (fs, 1.0 / (2 * kPi * rg * cg), true);
                eR = std::exp (-1.0 / (relMs * 0.001 * fs));
                ctlLp = 1.0 - std::exp (-1.0 / (470e3 * 1e-9 * fs));
                update (true);
                reset();
            }
            void reset() override
            {
                for (int c = 0; c < 2; ++c) { vc[c] = 0; lpf[c] = 0; }
                hpG.reset(); env = 0; vctlS = 2 * vb - (vb + vdd); over.reset();
            }
            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = over.os ? over.os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const double T = 1.0 / fs;
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    if (sm.tick (*this)) update (false);
                    const double gStep = (gTgt - gOut) / len;
                    for (int k = 0; k < len; ++k)
                    {
                        gOut += gStep;
                        // tensione di controllo -> Iabc -> resistenza dell'OTA in derivazione
                        const double iabc = softplus (vctlS - vctl, 0.03) / rctl;
                        const double gm = std::max (1e-12, iabc / (2 * kVt) * kin * gmk);
                        const double rv = 1.0 / gm;
                        double peak = 0;
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            const double u = x[k];
                            const double i = (u - vc[c]) / (rser + rv);
                            const double vx = vc[c] + rv * i;
                            vc[c] += T / cser * i;
                            // SUSTAIN: parte amplificata passa-alto (Rg, Cg) e passa-basso (Rf, Cf)
                            const double hp = hpG.tick (c, vx);
                            lpf[c] += lpK * (hp - lpf[c]);
                            const double y = softRail (vx + gainV * lpf[c], rail);
                            peak = std::max (peak, std::abs (y));
                            x[k] = (float) (y * gOut);
                        }
                        // raddrizzatore di precisione e condensatore del rivelatore
                        env = peak > env ? peak + eA * (env - peak) : peak + eR * (env - peak);
                        const double v18 = std::max (0.8, vb + vdd - kdet * env);
                        const double target = 2 * vb - v18;
                        vctlS += ctlLp * (target - vctlS);
                    }
                    gOut = gTgt;
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            void update (bool first)
            {
                const double rfV = std::max (1.0, rf (sm.v, def));
                gainV = rfV / rg;
                lpK = 1.0 - std::exp (-1.0 / (rfV * cf * fs));
                eA = std::exp (-1.0 / (std::max (0.05, att (sm.v, def)) * 0.001 * fs));
                gTgt = std::max (0.0, level (sm.v, def)) * std::pow (10.0, outDb / 20.0);
                if (first) gOut = gTgt;
            }
            Val rf, att, level;
            double rser = 470e3, cser = 100e-9, kin = 0.0035, gmk = 1, rg = 3e3, cg = 1e-6, cf = 330e-12, rail = 3.8, kdet = 7.2;
            double vb = 4.5, vdd = 0.6, relMs = 520, rctl = 47e3, vctl = 2.4, outDb = 0;
            double gainV = 10, lpK = 0.5, eA = 0, eR = 0, ctlLp = 0.01, gTgt = 1, gOut = 1, env = 0, vctlS = 3.9, vc[2] {}, lpf[2] {};
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
            OnePole hpG;
        };

        //==============================================================================
        /** Filtro a variabili di stato TPT (Zavalishin) in double, con saturazione morbida dello stato passa-banda
            (OTA / op-amp ai binari). */
        struct Svf
        {
            double ic1[2] {}, ic2[2] {}, g = 0.1, k = 1, a1 = 0, a2 = 0, a3 = 0, sat = 0;
            void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
            void set (double fs, double f, double q)
            {
                g = std::tan (kPi * std::clamp (f, 10.0, fs * 0.45) / fs);
                k = 1.0 / std::max (0.3, q);
                a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
            }
            inline void tick (int c, double x, double& lp, double& bp, double& hp) noexcept
            {
                const double v3 = x - ic2[c];
                double v1 = a1 * ic1[c] + a2 * v3;
                const double v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                if (sat > 0) v1 = sat * std::tanh (v1 / sat);
                ic1[c] = 2 * v1 - ic1[c];
                ic2[c] = 2 * v2 - ic2[c];
                lp = v2; bp = v1; hp = x - k * v1 - v2;
            }
        };

        /** Distorsione a op-amp con diodi in retroazione (asinh) e passa-basso del primo ordine. */
        struct OpDist
        {
            double vk = 0.3;
            OnePole lp;
            inline double tick (int c, double x, double gain) noexcept { return lp.tick (c, vk * std::asinh (gain * x / vk)); }
        };

        int idxOf (const RawConfig& rc, const char* k) { return rc.has (k) ? (int) rc.num (k, -1) : -1; }

        /** i4af: filtro d'inviluppo (Ibanez AF-201, AF9, AFL; AW7 con la distorsione).
            Config (indici dei comandi): sens peak filt dir rng manual drive dsw level; filts=hp,bp,lp (uscita per
            posizione di 'filt', oppure filt fisso con fixed=bp) dirs=up,down ranges=200:4000,100:2000 q=lo,hi
            sg=lo,hi (guadagno dell'inviluppo) env=attacco,rilascio(ms) sat=V bpn=esponente della normalizzazione del
            passa-banda dg=lo,hi (guadagno della distorsione) vk=V dtone=Hz out=dB (guadagno interno, prima dei binari
            rail=V) os; il LEVEL e' passivo (curva A, massimo 1). */
        class EnvFilter final : public Effect
        {
        public:
            explicit EnvFilter (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                cSens = idxOf (rc, "sens"); cPeak = idxOf (rc, "peak"); cFilt = idxOf (rc, "filt"); cDir = idxOf (rc, "dir");
                cRng = idxOf (rc, "rng"); cMan = idxOf (rc, "manual"); cDrive = idxOf (rc, "drive"); cDsw = idxOf (rc, "dsw");
                cLevel = idxOf (rc, "level");
                for (const auto& t : splitBy (rc.str ("filts", "bp"), ",")) filts.push_back (t == "lp" ? 0 : t == "hp" ? 2 : 1);
                for (const auto& t : splitBy (rc.str ("dirs", "up,down"), ",")) dirs.push_back (t == "down" ? 1 : 0);
                for (const auto& t : splitBy (rc.str ("ranges", "200:4000"), ","))
                {
                    const auto p = splitBy (t, ":");
                    ranges.push_back ({ std::atof (p[0].c_str()), p.size() > 1 ? std::atof (p[1].c_str()) : 4000.0 });
                }
                auto q = rc.list ("q"); qLo = q.size() > 0 ? q[0] : 0.7; qHi = q.size() > 1 ? q[1] : 8;
                auto g = rc.list ("sg"); sgLo = g.size() > 0 ? g[0] : 2; sgHi = g.size() > 1 ? g[1] : 40;
                auto e = rc.list ("env"); atkMs = e.size() > 0 ? e[0] : 4; relMs = e.size() > 1 ? e[1] : 120;
                svf.sat = rc.num ("sat", 0.0); bpn = rc.num ("bpn", 0.5);
                auto dg = rc.list ("dg"); dgLo = dg.size() > 0 ? dg[0] : 2; dgHi = dg.size() > 1 ? dg[1] : 300;
                dist.vk = rc.num ("vk", 0.3); dtone = rc.num ("dtone", 3500);
                outDb = rc.num ("out", 0.0); rail = rc.num ("rail", 0.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 1.0)))), 0, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                eA = std::exp (-1.0 / (atkMs * 0.001 * fs)); eR = std::exp (-1.0 / (relMs * 0.001 * fs));
                dist.lp.set (fs, dtone, false);
                kF = 1.0 - std::exp (-1.0 / (0.002 * fs));
                dirK = 1.0 - std::exp (-1.0 / (0.020 * fs));
                dirS = cDir >= 0 && dirs[(size_t) std::min ((int) dirs.size() - 1, stepOf (def, sm.v, cDir))] == 1 ? 1.0 : 0.0;
                dipStep = 1.0 / (0.004 * fs);
                fSm = -1; lfS = -1;
                dmCur = cDsw >= 0 ? stepOf (def, sm.v, cDsw) : 0;
                reset();
            }
            void reset() override { svf.reset(); dist.lp.reset(); env = 0; over.reset(); for (auto& v : xg) v = 1; dipG = 1; }
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
                    sm.tick (*this);
                    const auto* p = sm.v;
                    const double sens = cSens >= 0 ? p[cSens] : 0.5;
                    const double sg = sgLo * std::pow (sgHi / sgLo, sens);
                    const double peakQ = qLo * std::pow (qHi / qLo, cPeak >= 0 ? (double) p[cPeak] : 0.5);
                    const int fsel = cFilt >= 0 ? filts[(size_t) std::min ((int) filts.size() - 1, stepOf (def, p, cFilt))] : filts[0];
                    const bool down = cDir >= 0 && dirs[(size_t) std::min ((int) dirs.size() - 1, stepOf (def, p, cDir))] == 1;
                    const auto& rg = ranges[(size_t) (cRng >= 0 ? std::min ((int) ranges.size() - 1, stepOf (def, p, cRng)) : 0)];
                    const double base = cMan >= 0 ? (double) p[cMan] : 0.0;
                    const double dgain = cDrive >= 0 ? dgLo * std::pow (dgHi / dgLo, (double) p[cDrive]) : 1.0;
                    const int dmT = cDsw >= 0 ? stepOf (def, p, cDsw) : 0;
                    const double inG = std::pow (10.0, outDb / 20.0), outG = cLevel >= 0 ? taperA (p[cLevel]) : 1.0;
                    // uscite della posizione corrente con dissolvenza quando cambia la levetta del filtro
                    const double bpG = std::pow (1.0 / peakQ, 1.0 - bpn);
                    for (int k = 0; k < len; ++k)
                    {
                        // cambio del modo di distorsione (prima/dopo/esclusa): breve pausa di 4 ms invece del salto
                        if (dmT != dmCur) { dipG -= dipStep; if (dipG <= 0) { dipG = 0; dmCur = dmT; } }
                        else if (dipG < 1) dipG = std::min (1.0, dipG + dipStep);
                        const int dmode = dmCur;
                        // inviluppo dal segnale d'ingresso (canale sinistro o somma)
                        double a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, (double) std::abs (data[c][i0 + k]));
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        const double ctl = std::clamp (sg * env, 0.0, 1.0);
                        // levetta UP/DOWN: dissolvenza di 20 ms tra le due leggi
                        dirS += ((down ? 1.0 : 0.0) - dirS) * dirK;
                        double pos = dirS * (1.0 - base) * (1.0 - ctl) + (1.0 - dirS) * (base + (1.0 - base) * ctl);
                        pos = std::clamp (pos, 0.0, 1.0);
                        // frequenza levigata (2 ms, nel dominio logaritmico): niente salti con le levette DRIVE e RANGE
                        const double lf = std::log (rg.first) + pos * std::log (rg.second / rg.first);
                        lfS = lfS < 0 ? lf : lfS + kF * (lf - lfS);
                        const double f = std::exp (lfS);
                        if (std::abs (f - fSm) > 1e-3 * f || std::abs (peakQ - qSm) > 1e-4) { svf.set (fs, f, peakQ); fSm = f; qSm = peakQ; }
                        for (int c = 0; c < numCh; ++c)
                        {
                            float* x = data[c] + i0;
                            double u = x[k];
                            if (dmode == 1) u = dist.tick (c, u, dgain);
                            double lp, bp, hp;
                            svf.tick (c, u, lp, bp, hp);
                            for (int m = 0; m < 3; ++m)
                            {
                                const double t = m == fsel ? 1.0 : 0.0;
                                xg[c * 3 + m] += (t - xg[c * 3 + m]) * xfK;
                            }
                            double y = xg[c * 3] * lp + xg[c * 3 + 1] * bp * bpG + xg[c * 3 + 2] * hp;
                            if (dmode == 2) y = dist.tick (c, y, dgain);
                            y *= inG;
                            if (rail > 0) y = softRail (y, rail);
                            x[k] = (float) (y * outG * dipG);
                        }
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            int cSens = -1, cPeak = -1, cFilt = -1, cDir = -1, cRng = -1, cMan = -1, cDrive = -1, cDsw = -1, cLevel = -1;
            std::vector<int> filts, dirs;
            std::vector<std::pair<double, double>> ranges;
            double qLo = 0.7, qHi = 8, sgLo = 2, sgHi = 40, atkMs = 4, relMs = 120, bpn = 0.5, dgLo = 2, dgHi = 300, dtone = 3500;
            double outDb = 0, rail = 0, eA = 0, eR = 0, env = 0, fSm = -1, qSm = -1, xg[6] { 1, 1, 1, 1, 1, 1 }, xfK = 0.002;
            double kF = 0.01, lfS = -1, dipG = 1, dipStep = 0.005, dirK = 0.001, dirS = 0;
            int dmCur = 0;
            int osLog2 = 0;
            Over over;
            double sr = 48000, fs = 48000;
            static constexpr int sub = 16;
            Smooth sm;
            Svf svf;
            OpDist dist;
        };

        //==============================================================================
        /** i4lofi: Ibanez LF7 Lo Fi. Config (indici): drive lo hi level mode; mgain=g0,g1,g2 (guadagno d'ingresso per
            modo GUITAR/DRUMS/MIC) dg=lo,hi lof=lo,hi hif=lo,hi (Hz, posizione 0..1 del pomello) q=Q vk=V sat=V out=dB
            (guadagno interno prima dei binari rail=V; LEVEL passivo, curva A) os. */
        class LoFi final : public Effect
        {
        public:
            explicit LoFi (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                cDrive = idxOf (rc, "drive"); cLo = idxOf (rc, "lo"); cHi = idxOf (rc, "hi"); cLevel = idxOf (rc, "level"); cMode = idxOf (rc, "mode");
                mgain = rc.list ("mgain"); if (mgain.empty()) mgain = { 1.0 };
                auto dg = rc.list ("dg"); dgLo = dg.size() > 0 ? dg[0] : 1; dgHi = dg.size() > 1 ? dg[1] : 100;
                auto lf = rc.list ("lof"); loA = lf.size() > 0 ? lf[0] : 80; loB = lf.size() > 1 ? lf[1] : 1000;
                auto hf = rc.list ("hif"); hiA = hf.size() > 0 ? hf[0] : 10000; hiB = hf.size() > 1 ? hf[1] : 1000;
                q = rc.num ("q", 0.8); dist.vk = rc.num ("vk", 0.35); sat = rc.num ("sat", 1.2); outDb = rc.num ("out", 0.0);
                rail = rc.num ("rail", 3.5);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, rc.num ("os", 2.0)))), 0, 2);
            }
            void prepare (double s, int maxBlock) override
            {
                sr = s; over.prepare (osLog2, maxBlock); fs = sr * (double) (1 << osLog2);
                sm.setTime (fs / sub, 0.006); sm.snap (*this);
                dist.lp.set (fs, 12000.0, false);
                hp.sat = sat; lp.sat = sat;
                reset();
            }
            void reset() override { hp.reset(); lp.reset(); dist.lp.reset(); over.reset(); for (auto& m : mg) m = -1; }
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
                    sm.tick (*this);
                    const auto* p = sm.v;
                    const double gIn = mgain[(size_t) std::min ((int) mgain.size() - 1, cMode >= 0 ? stepOf (def, p, cMode) : 0)];
                    const double dg = dgLo * std::pow (dgHi / dgLo, cDrive >= 0 ? (double) taperA (p[cDrive]) : 0.5);
                    const double fl = loA * std::pow (loB / loA, cLo >= 0 ? (double) p[cLo] : 0.0);
                    const double fh = hiA * std::pow (hiB / hiA, cHi >= 0 ? (double) p[cHi] : 0.0);
                    hp.set (fs, fl, q); lp.set (fs, fh, q);
                    const double inG = std::pow (10.0, outDb / 20.0), outT = cLevel >= 0 ? taperA (p[cLevel]) : 1.0;
                    for (int c = 0; c < numCh; ++c)
                    {
                        if (mg[c] < 0) mg[c] = gIn;
                        float* x = data[c] + i0;
                        for (int k = 0; k < len; ++k)
                        {
                            mg[c] += (gIn - mg[c]) * 0.002;
                            double l, b, h;
                            const double u = dist.tick (c, x[k] * mg[c], dg);
                            hp.tick (c, u, l, b, h);
                            double l2, b2, h2;
                            lp.tick (c, h, l2, b2, h2);
                            x[k] = (float) (softRail (l2 * inG, rail) * outT);
                        }
                    }
                }
                if (over.os) over.os->processSamplesDown (block);
            }
        private:
            int cDrive = -1, cLo = -1, cHi = -1, cLevel = -1, cMode = -1;
            std::vector<double> mgain;
            double dgLo = 1, dgHi = 100, loA = 80, loB = 1000, hiA = 10000, hiB = 1000, q = 0.8, sat = 1.2, outDb = 0, rail = 3.5, mg[2] { -1, -1 };
            int osLog2 = 1;
            Over over;
            double sr = 48000, fs = 96000;
            static constexpr int sub = 16;
            Smooth sm;
            Svf hp, lp;
            OpDist dist;
        };

        //==============================================================================
        /** i4dnr: riduttore di rumore dinamico (National LM1894, Ibanez NB10). Passa-basso del primo ordine la cui banda
            passante segue il contenuto di acuti: ramo di controllo passa-alto (ctlhp Hz), guadagno g (espressione),
            rivelatore di picco t=attacco,rilascio (ms), banda da bwmin (espressione: FREQUENCY) a bw=..,max Hz con mappa
            esponenziale tra thr e thr+span dB. */
        class Dnr final : public Effect
        {
        public:
            explicit Dnr (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                auto bw = rc.list ("bw"); bwMin = bw.size() > 0 ? bw[0] : 350; bwMax = bw.size() > 1 ? bw[1] : 14000;
                bwLo = val (rc, "bwmin", bwMin);
                ctlHp = rc.num ("ctlhp", 6000); g = val (rc, "g", 1.0); thrDb = rc.num ("thr", -60); span = rc.num ("span", 36);
                auto t = rc.list ("t"); atk = t.size() > 0 ? t[0] : 0.5; rel = t.size() > 1 ? t[1] : 60;
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / 16, 0.006); sm.snap (*this);
                chp.highpass (fs, ctlHp, 0.707);
                eA = std::exp (-1.0 / (atk * 0.001 * fs)); eR = std::exp (-1.0 / (rel * 0.001 * fs));
                reset();
            }
            void reset() override { chp.reset(); env = 0; for (auto& v : z) v = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += 16)
                {
                    const int len = std::min (16, n - i0);
                    sm.tick (*this);
                    const double gain = std::max (1e-6, g (sm.v, def)), lo = std::clamp (bwLo (sm.v, def), 50.0, bwMax);
                    for (int k = 0; k < len; ++k)
                    {
                        double a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, std::abs (chp.tick (c, ch[c][i0 + k])));
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        const double lvl = 20.0 * std::log10 (std::max (1e-9, env * gain));
                        const double pos = std::clamp ((lvl - thrDb) / span, 0.0, 1.0);
                        const double bw = lo * std::pow (bwMax / lo, pos);
                        const double t = std::tan (kPi * std::min (bw, 0.45 * fs) / fs), b = t / (1 + t), aa = (1 - t) / (1 + t);
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double x = ch[c][i0 + k];
                            const double y = b * (x + z[c * 2]) + aa * z[c * 2 + 1];
                            z[c * 2] = x; z[c * 2 + 1] = y;
                            ch[c][i0 + k] = (float) y;
                        }
                    }
                }
            }
        private:
            double bwMin = 350, bwMax = 14000, ctlHp = 6000, thrDb = -60, span = 36, atk = 0.5, rel = 60, fs = 48000, eA = 0, eR = 0, env = 0, z[4] {};
            Val g, bwLo;
            Smooth sm;
            BQ chp;
        };

        //==============================================================================
        /** i4hush: riduttore di rumore a singolo capo tipo HUSH (Rocktron): espansore verso il basso sotto la soglia
            (rapporto ratio) e passa-basso dinamico (lp=min,max Hz) che si chiude con il segnale debole.
            Config: thr (espressione, dBFS) ratio lp=min,max t=attacco,rilascio(ms) on (espressione 0/1, dissolvenza). */
        class Hush final : public Effect
        {
        public:
            explicit Hush (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                thr = val (rc, "thr", -60); ratio = rc.num ("ratio", 2.5); on = val (rc, "on", 1.0);
                auto lp = rc.list ("lp"); lpMin = lp.size() > 0 ? lp[0] : 1500; lpMax = lp.size() > 1 ? lp[1] : 18000;
                auto t = rc.list ("t"); atk = t.size() > 0 ? t[0] : 1; rel = t.size() > 1 ? t[1] : 90;
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / 16, 0.006); sm.snap (*this);
                eA = std::exp (-1.0 / (atk * 0.001 * fs)); eR = std::exp (-1.0 / (rel * 0.001 * fs));
                hf.highpass (fs, 3000, 0.707);
                wet = on (sm.v, def) > 0.5 ? 1.0 : 0.0;
                reset();
            }
            void reset() override { env = envH = 0; gS = 1; for (auto& v : z) v = 0; hf.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += 16)
                {
                    const int len = std::min (16, n - i0);
                    sm.tick (*this);
                    const double th = thr (sm.v, def), wetT = on (sm.v, def) > 0.5 ? 1.0 : 0.0;
                    for (int k = 0; k < len; ++k)
                    {
                        wet += std::clamp (wetT - wet, -0.002, 0.002);
                        double a = 0, ah = 0;
                        for (int c = 0; c < numCh; ++c) { a = std::max (a, (double) std::abs (ch[c][i0 + k])); ah = std::max (ah, std::abs (hf.tick (c, ch[c][i0 + k]))); }
                        env = a > env ? a + eA * (env - a) : a + eR * (env - a);
                        envH = ah > envH ? ah + eA * (envH - ah) : ah + eR * (envH - ah);
                        const double lvl = 20.0 * std::log10 (std::max (1e-9, env));
                        const double gDb = lvl < th ? (lvl - th) * (ratio - 1.0) : 0.0;
                        const double gT = std::pow (10.0, std::max (-80.0, gDb) / 20.0);
                        gS += 0.05 * (gT - gS);
                        const double lh = 20.0 * std::log10 (std::max (1e-9, envH));
                        const double pos = std::clamp ((lh - (th - 6.0)) / 30.0, 0.0, 1.0);
                        const double f = lpMin * std::pow (lpMax / lpMin, pos);
                        const double t = std::tan (kPi * std::min (f, 0.45 * fs) / fs), b = t / (1 + t), aa = (1 - t) / (1 + t);
                        for (int c = 0; c < numCh; ++c)
                        {
                            const double x = ch[c][i0 + k];
                            const double y = b * (x + z[c * 2]) + aa * z[c * 2 + 1];
                            z[c * 2] = x; z[c * 2 + 1] = y;
                            ch[c][i0 + k] = (float) (x + wet * (y * gS - x));
                        }
                    }
                }
            }
        private:
            Val thr, on;
            double ratio = 2.5, lpMin = 1500, lpMax = 18000, atk = 1, rel = 90, fs = 48000, eA = 0, eR = 0, env = 0, envH = 0, gS = 1, wet = 1, z[4] {};
            Smooth sm;
            BQ hf;
        };

        //==============================================================================
        /** i4gate: noise gate con isteresi; soglia = espressione (dBFS), attacco/rilascio (ms). */
        class Gate final : public Effect
        {
        public:
            explicit Gate (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                thr = val (rc, "thr", -60); hyst = rc.num ("hyst", 6);
                auto t = rc.list ("t"); atk = t.size() > 0 ? t[0] : 0.5; rel = t.size() > 1 ? t[1] : 60; hold = t.size() > 2 ? t[2] : 20;
            }
            void prepare (double s, int) override
            {
                fs = s; sm.setTime (fs / 16, 0.006); sm.snap (*this);
                det.set (fs, 0.2, 15.0);
                gA = std::exp (-1.0 / (atk * 0.001 * fs)); gR = std::exp (-1.0 / (rel * 0.001 * fs));
                reset();
            }
            void reset() override { g = 0; open = false; held = 0; det.env = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += 16)
                {
                    const int len = std::min (16, n - i0);
                    sm.tick (*this);
                    const double th = std::pow (10.0, thr (sm.v, def) / 20.0), tc = th * std::pow (10.0, -hyst / 20.0);
                    for (int k = 0; k < len; ++k)
                    {
                        float a = 0;
                        for (int c = 0; c < numCh; ++c) a = std::max (a, std::abs (ch[c][i0 + k]));
                        const double e = det.tick (a);
                        if (e > th) { open = true; held = (int) (hold * 0.001 * fs); }
                        else if (e < tc) { if (held > 0) --held; else open = false; }
                        const double t = open ? 1.0 : 0.0;
                        g = t > g ? t + gA * (g - t) : t + gR * (g - t);
                        for (int c = 0; c < numCh; ++c) ch[c][i0 + k] = (float) (ch[c][i0 + k] * g);
                    }
                }
            }
        private:
            Val thr;
            double hyst = 6, atk = 0.5, rel = 60, hold = 20, fs = 48000, gA = 0, gR = 0, g = 0;
            bool open = false;
            int held = 0;
            Envelope det;
            Smooth sm;
        };

        //==============================================================================
        /** i4peq: EQ parametrico a biquad (filtri analogici a variabili di stato con frequenza a scatti o continua).
            Per banda b (0..5): tb=ls|pk|hs fb=espr (Hz) nb=passi (quantizza la frequenza sul comando fcb) fcb=i flob,fhib
            gb=espr (dB) qb=espr; lvl=espr (dB); rail=V (escursione dell'op-amp d'uscita). */
        class Peq final : public Effect
        {
        public:
            explicit Peq (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                for (int b = 0; b < 6; ++b)
                {
                    const std::string i = std::to_string (b);
                    if (! rc.has (("t" + i).c_str())) break;
                    Band bd;
                    const auto t = rc.str (("t" + i).c_str());
                    bd.type = t == "ls" ? 0 : t == "hs" ? 2 : 1;
                    bd.f = val (rc, ("f" + i).c_str(), 1000); bd.g = val (rc, ("g" + i).c_str(), 0); bd.q = val (rc, ("q" + i).c_str(), 0.7);
                    bd.fc = idxOf (rc, ("fc" + i).c_str()); bd.steps = (int) rc.num (("n" + i).c_str(), 0);
                    auto fr = rc.list (("fr" + i).c_str()); bd.flo = fr.size() > 0 ? fr[0] : 100; bd.fhi = fr.size() > 1 ? fr[1] : 10000;
                    bands.push_back (bd);
                }
                lvl = val (rc, "lvl", 0.0);
                rail = rc.num ("rail", 3.5);
            }
            void prepare (double s, int) override { fs = s; sm.setTime (fs / 8, 0.006); sm.snap (*this); update(); gOut = gT; reset(); }
            void reset() override { for (auto& b : bands) b.bq.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += 8)
                {
                    const int len = std::min (8, n - i0);
                    if (sm.tick (*this)) update();
                    const double st = (gT - gOut) / len;
                    for (int k = 0; k < len; ++k)
                    {
                        gOut += st;
                        for (int c = 0; c < numCh; ++c)
                        {
                            double y = ch[c][i0 + k];
                            for (auto& b : bands) y = b.bq.tick (c, y);
                            ch[c][i0 + k] = (float) softRail (y * gOut, rail);
                        }
                    }
                    gOut = gT;
                }
            }
        private:
            struct Band { int type = 1, fc = -1, steps = 0; double flo = 100, fhi = 10000; Val f, g, q; BQ bq; };
            void update()
            {
                for (auto& b : bands)
                {
                    double f;
                    if (b.fc >= 0)
                    {
                        double x = std::clamp ((double) sm.v[b.fc], 0.0, 1.0);
                        if (b.steps >= 2) x = std::round (x * (b.steps - 1)) / (b.steps - 1);
                        f = b.flo * std::pow (b.fhi / b.flo, x);
                    }
                    else f = b.f (sm.v, def);
                    const double g = b.g (sm.v, def), q = std::max (0.1, b.q (sm.v, def));
                    if (b.type == 0) b.bq.shelf (fs, f, q, g, false);
                    else if (b.type == 2) b.bq.shelf (fs, f, q, g, true);
                    else b.bq.peak (fs, f, q, g);
                }
                gT = std::pow (10.0, lvl (sm.v, def) / 20.0);
            }
            std::vector<Band> bands;
            Val lvl;
            double fs = 48000, gT = 1, gOut = 1, rail = 3.5;
            Smooth sm;
        };

        //==============================================================================
        /** i4vol: volume stereo a pedale (Ibanez VL10): ruoli volume, mode (VOL / BAL), min (volume minimo, 0..50 %).
            VOL: A e B insieme; BAL: A sale mentre B scende. Curva del potenziometro: taper (A|B). */
        class StereoVol final : public Effect
        {
        public:
            explicit StereoVol (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                pVol = role (*this, "volume", 1.0f); pMode = role (*this, "mode", 0.0f); pMin = role (*this, "min", 0.0f);
                taper = rc.str ("taper", "A")[0]; minMax = rc.num ("minmax", 0.5);
            }
            void prepare (double s, int) override { for (auto& g : gs) g.reset (s, 0.02); first = true; }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const double v = pVol.get (*this), mn = minMax * pMin.get (*this);
                const bool bal = pMode.step (*this) > 0;
                auto curve = [this] (double x) { return Expr::applyTaper (x, taper); };
                const double a = mn + (1 - mn) * curve (v), b = bal ? mn + (1 - mn) * curve (1 - v) : a;
                gs[0].setTargetValue ((float) a); gs[1].setTargetValue ((float) b);
                if (first) { gs[0].setCurrentAndTargetValue ((float) a); gs[1].setCurrentAndTargetValue ((float) b); first = false; }
                for (int c = 0; c < std::min (numCh, 2); ++c)
                    for (int i = 0; i < n; ++i) ch[c][i] *= gs[c].getNextValue();
                if (numCh < 2) gs[1].skip (n);
            }
        private:
            RoleParam pVol, pMode, pMin;
            char taper = 'A';
            double minMax = 0.5;
            bool first = true;
            juce::SmoothedValue<float> gs[2];
        };

        /** i4pan: pan a pedale (Ibanez ST-810): legge a potenza costante (cos/sen) tra le uscite A e B; con la catena
            mono resta la sola uscita A, come il pedale con un solo amplificatore. */
        class PanPedal final : public Effect
        {
        public:
            explicit PanPedal (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                pPan = role (*this, "pan", 0.5f); law = rc.num ("law", 0.5); gain = rc.num ("gain", 1.0);
            }
            void prepare (double s, int) override { for (auto& g : gs) g.reset (s, 0.02); first = true; }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const double x = std::clamp ((double) pPan.get (*this), 0.0, 1.0);
                // law = 0.5: potenza costante; 1: lineare
                const double a = law >= 1 ? 1 - x : std::cos (x * kPi / 2), b = law >= 1 ? x : std::sin (x * kPi / 2);
                gs[0].setTargetValue ((float) (a * gain)); gs[1].setTargetValue ((float) (b * gain));
                if (first) { gs[0].setCurrentAndTargetValue ((float) (a * gain)); gs[1].setCurrentAndTargetValue ((float) (b * gain)); first = false; }
                if (numCh >= 2)
                    for (int i = 0; i < n; ++i)
                    {
                        const float m = 0.5f * (ch[0][i] + ch[1][i]);
                        ch[0][i] = m * gs[0].getNextValue(); ch[1][i] = m * gs[1].getNextValue();
                    }
                else { for (int i = 0; i < n; ++i) ch[0][i] *= gs[0].getNextValue(); gs[1].skip (n); }
            }
        private:
            RoleParam pPan;
            double law = 0.5, gain = 1;
            bool first = true;
            juce::SmoothedValue<float> gs[2];
        };

        /** i4loop: selettore di due loop con livello (Ibanez LS10): ruoli levelA, levelB (0..+6 dB: guadagno 2*x), select
            (A/B). Nel plugin il loop A e' il canale sinistro, il B il destro (come il Router LS-2); con la catena mono
            cambia solo il livello del loop scelto. */
        class LoopSel final : public Effect
        {
        public:
            explicit LoopSel (const ModelDef& d) : Effect (d)
            {
                const RawConfig rc (d.config != nullptr ? d.config : "");
                pA = role (*this, "levelA", 0.5f); pB = role (*this, "levelB", 0.5f); pSel = role (*this, "select", 0.0f);
                gmax = rc.num ("gmax", 2.0);
            }
            void prepare (double s, int) override { for (auto& g : gs) g.reset (s, 0.01); first = true; }
            void reset() override {}
            void process (float* const* ch, int numCh, int n) override
            {
                const bool b = pSel.step (*this) > 0;
                const float la = (float) (gmax * pA.get (*this)), lb = (float) (gmax * pB.get (*this));
                gs[0].setTargetValue (b ? 0.0f : la); gs[1].setTargetValue (b ? lb : 0.0f);
                if (first) { gs[0].setCurrentAndTargetValue (b ? 0.0f : la); gs[1].setCurrentAndTargetValue (b ? lb : 0.0f); first = false; }
                for (int i = 0; i < n; ++i)
                {
                    const float ga = gs[0].getNextValue(), gb = gs[1].getNextValue();
                    if (numCh >= 2) { const float v = ch[0][i] * ga + ch[1][i] * gb; ch[0][i] = v; ch[1][i] = v; }
                    else ch[0][i] *= ga + gb;
                }
            }
        private:
            RoleParam pA, pB, pSel;
            double gmax = 2;
            bool first = true;
            juce::SmoothedValue<float> gs[2];
        };

        //==============================================================================
        /** i4chain: catena di sotto-effetti delle famiglie esistenti, con gli stessi comandi del pedale.
            "type=i4chain mode=ser|par || Famiglia[@on=i,passo][@w=espr]: config || ...". In serie ogni sezione con
            "on" e' inserita solo quando il comando i e' sul passo indicato (dissolvenza di 10 ms); in parallelo le
            uscite sono sommate con i pesi w. Nella config delle sezioni "subtype=" diventa "type=". */
        class ChainFx final : public Effect
        {
        public:
            explicit ChainFx (const ModelDef& d) : Effect (d)
            {
                const auto parts = splitBy (d.config != nullptr ? d.config : "", "||");
                const RawConfig head (parts.empty() ? "" : parts[0]);
                parallel = head.str ("mode", "ser") == "par";
                for (size_t i = 1; i < parts.size() && secs.size() < 6; ++i)
                {
                    const auto colon = parts[i].find (':');
                    if (colon == std::string::npos) continue;
                    auto hdr = splitBy (trimmed (parts[i].substr (0, colon)), "@");
                    auto sec = std::make_unique<Section>();
                    sec->def = d;
                    std::string cfg = trimmed (parts[i].substr (colon + 1));
                    for (size_t pos = cfg.find ("subtype="); pos != std::string::npos; pos = cfg.find ("subtype=", pos))
                        cfg.replace (pos, 8, "type=");
                    sec->config = cfg;
                    sec->def.config = sec->config.c_str();
                    sec->def.family = familyOf (hdr[0], d.family);
                    for (size_t h = 1; h < hdr.size(); ++h)
                    {
                        if (hdr[h].rfind ("on=", 0) == 0)
                        {
                            auto v = splitBy (hdr[h].substr (3), ",");
                            sec->onCtl = std::atoi (v[0].c_str()); sec->onStep = v.size() > 1 ? std::atoi (v[1].c_str()) : 1;
                        }
                        else if (hdr[h].rfind ("w=", 0) == 0) sec->w.set (hdr[h].substr (2), 1.0);
                    }
                    secs.push_back (std::move (sec));
                }
                for (auto& s : secs) s->fx = createEffect (s->def);
            }
            void prepare (double s, int maxBlock) override
            {
                fs = s; maxN = std::max (1, maxBlock);
                for (auto& sec : secs)
                {
                    forward (*sec);
                    sec->fx->prepare (s, maxBlock);
                    for (auto& b : sec->buf) b.assign ((size_t) maxN, 0.0f);
                    sec->g = active (*sec) ? 1.0f : 0.0f;
                    sec->wS = (float) sec->w (pvals(), def);
                }
                dry[0].assign ((size_t) maxN, 0.0f); dry[1].assign ((size_t) maxN, 0.0f);
                acc[0].assign ((size_t) maxN, 0.0f); acc[1].assign ((size_t) maxN, 0.0f);
            }
            void reset() override { for (auto& sec : secs) sec->fx->reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                numCh = std::min (numCh, 2);
                for (int i0 = 0; i0 < n; i0 += maxN)
                {
                    const int len = std::min (maxN, n - i0);
                    float* io[2] = { ch[0] + i0, numCh > 1 ? ch[1] + i0 : nullptr };
                    if (parallel)
                    {
                        for (int c = 0; c < numCh; ++c) { std::copy (io[c], io[c] + len, dry[(size_t) c].begin()); std::fill (acc[(size_t) c].begin(), acc[(size_t) c].begin() + len, 0.0f); }
                        for (auto& sec : secs)
                        {
                            forward (*sec);
                            float* b[2] = { sec->buf[0].data(), sec->buf[1].data() };
                            for (int c = 0; c < numCh; ++c) std::copy (dry[(size_t) c].begin(), dry[(size_t) c].begin() + len, b[c]);
                            sec->fx->process (b, numCh, len);
                            const float wT = (float) sec->w (pvals(), def), w0 = sec->wS, st = (wT - w0) / (float) len;
                            for (int c = 0; c < numCh; ++c)
                                for (int k = 0; k < len; ++k) acc[(size_t) c][(size_t) k] += b[c][k] * (w0 + st * (float) (k + 1));
                            sec->wS = wT;
                        }
                        for (int c = 0; c < numCh; ++c) std::copy (acc[(size_t) c].begin(), acc[(size_t) c].begin() + len, io[c]);
                        continue;
                    }
                    for (auto& sec : secs)
                    {
                        forward (*sec);
                        const float target = active (*sec) ? 1.0f : 0.0f;
                        if (sec->onCtl < 0 || (sec->g >= 1.0f && target >= 1.0f))
                        {
                            sec->fx->process (io, numCh, len);
                            continue;
                        }
                        // sezione commutabile: elaborata sempre (stato caldo), mescolata con dissolvenza
                        float* b[2] = { sec->buf[0].data(), sec->buf[1].data() };
                        for (int c = 0; c < numCh; ++c) std::copy (io[c], io[c] + len, b[c]);
                        sec->fx->process (b, numCh, len);
                        const float step = 1.0f / (0.010f * (float) fs);
                        for (int k = 0; k < len; ++k)
                        {
                            sec->g = target > sec->g ? std::min (target, sec->g + step) : std::max (target, sec->g - step);
                            for (int c = 0; c < numCh; ++c) io[c][k] += sec->g * (b[c][k] - io[c][k]);
                        }
                    }
                }
            }
        private:
            struct Section
            {
                ModelDef def {};
                std::string config;
                std::unique_ptr<Effect> fx;
                int onCtl = -1, onStep = 1;
                float g = 1, wS = 1;
                Val w;
                std::vector<float> buf[2];
                Section() { w.set ("", 1.0); }
            };
            static Family familyOf (const std::string& name, Family fallback)
            {
                static const std::pair<const char*, Family> map[] = {
                    { "Circuit", Family::Circuit }, { "AmpSim", Family::AmpSim }, { "Wah", Family::Wah },
                    { "Compressor", Family::Compressor }, { "NoiseGate", Family::NoiseGate }, { "GraphicEQ", Family::GraphicEQ },
                    { "ParametricEQ", Family::ParametricEQ }, { "Volume", Family::Volume }, { "CabIR", Family::CabIR } };
                for (const auto& m : map) if (name == m.first) return m.second;
                return fallback;
            }
            const float* pvals()
            {
                for (int i = 0; i < def.numControls; ++i) pv[i] = p (i);
                return pv;
            }
            bool active (const Section& s) const
            {
                if (s.onCtl < 0) return true;
                const int st = std::clamp ((int) std::lround (p (s.onCtl) * (std::max (1, (int) def.controls[s.onCtl].steps) - 1)), 0, 64);
                return st == s.onStep;
            }
            void forward (Section& s) { for (int i = 0; i < def.numControls; ++i) s.fx->params[i].store (p (i), std::memory_order_relaxed); }

            std::vector<std::unique_ptr<Section>> secs;
            bool parallel = false;
            double fs = 48000;
            int maxN = 512;
            std::vector<float> dry[2], acc[2];
            float pv[maxControls] {};
        };
    }

    std::unique_ptr<Effect> makeI4Dyn (const ModelDef& d, const std::string& type)
    {
        if (type == "i4net")   return std::make_unique<NetEffect> (d);
        if (type == "i4ross")  return std::make_unique<RossComp> (d);
        if (type == "i4opto")  return std::make_unique<OptoComp> (d);
        if (type == "i4vca")   return std::make_unique<ShuntComp> (d);
        if (type == "i4af")    return std::make_unique<EnvFilter> (d);
        if (type == "i4lofi")  return std::make_unique<LoFi> (d);
        if (type == "i4dnr")   return std::make_unique<Dnr> (d);
        if (type == "i4hush")  return std::make_unique<Hush> (d);
        if (type == "i4gate")  return std::make_unique<Gate> (d);
        if (type == "i4peq")   return std::make_unique<Peq> (d);
        if (type == "i4vol")   return std::make_unique<StereoVol> (d);
        if (type == "i4pan")   return std::make_unique<PanPedal> (d);
        if (type == "i4loop")  return std::make_unique<LoopSel> (d);
        if (type == "i4chain") return std::make_unique<ChainFx> (d);
        return nullptr;
    }
}
