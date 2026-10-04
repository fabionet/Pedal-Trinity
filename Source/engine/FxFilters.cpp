/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Filtri: EQ grafici a gyrator, EQ parametrici, wah / filtri dinamici,
    simulatori di chitarra acustica.

    EQ grafico (config):  freqs=100,200,...  qmax=3.5  qmin=0.9  range=15
                          pre=dB (guadagno stadio d'ingresso)  lvl=15 (gamma LEVEL)
      Ogni banda e' un RLC serie (gyrator) collegato al cursore: la larghezza
      di banda si stringe all'aumentare del guadagno ("proportional Q").
    EQ parametrico:       types=ls,pk,pk,hs  f0=lo,hi f1=lo,hi ...  q=0.7,1,1,0.7  range=15
    Wah:                  type=auto|touch|lfo|pedal  f=lo,hi  q=lo,hi  filt=bp|lp
                          attack=ms release=ms rate=lo,hi
    Wah a induttore:      type=inductor + valori dei componenti (Cry Baby / Vox), vedi InductorWahEffect
    Acoustic:             modes=f:g:q|f:g:q;... (risonanze del corpo per modo)  top=Hz
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        class GraphicEQEffect : public Effect
        {
        public:
            explicit GraphicEQEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                freqs = cfg.list ("freqs");
                if (freqs.empty()) freqs = { 100, 200, 400, 800, 1600, 3200, 6400 };
                qs = cfg.list ("qs");
                qMax = cfg.num ("qmax", 3.5);
                qMin = cfg.num ("qmin", 0.9);
                range = (float) cfg.num ("range", 15);
                lvlRange = (float) cfg.num ("lvl", 15);
                for (size_t b = 0; b < freqs.size() && b < 12; ++b)
                    bands[b] = role (*this, ("b" + std::to_string (b)).c_str(), 0.5f);
                pLevel = role (*this, "level", 0.5f);
                for (auto& g : last) g = -999;
            }
            void prepare (double s, int) override { sr = s; for (auto& g : last) g = -999; reset(); }
            void reset() override { for (auto& ch : f) for (auto& b : ch) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                const int nb = (int) std::min<size_t> (freqs.size(), 12);
                for (int b = 0; b < nb; ++b)
                {
                    const float g = (bands[b].get (*this) - 0.5f) * 2.0f * range;
                    if (std::abs (g - last[b]) > 0.01f)
                    {
                        last[b] = g;
                        const double q0 = qs.size() > (size_t) b ? qs[(size_t) b] : qMax;
                        const double q = qMin + (q0 - qMin) * std::abs (g) / range;
                        for (int c = 0; c < 2; ++c) f[c][b].peak (sr, freqs[(size_t) b], q, g);
                    }
                }
                gainTarget = dbToGain ((pLevel.get (*this) - 0.5f) * 2.0f * lvlRange);
                for (int i = 0; i < n; ++i)
                {
                    gainNow += 0.002f * (gainTarget - gainNow);
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (int b = 0; b < nb; ++b) y = f[c][b].process (y);
                        ch[c][i] = y * gainNow;
                    }
                }
            }
        private:
            Config cfg;
            std::vector<double> freqs, qs;
            double qMax = 3.5, qMin = 0.9, sr = 48000;
            float range = 15, lvlRange = 15, last[12] {}, gainTarget = 1, gainNow = 1;
            RoleParam bands[12], pLevel;
            pt::dsp::Biquad f[2][12];
        };

        //==============================================================================
        class ParametricEQEffect : public Effect
        {
        public:
            explicit ParametricEQEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto t = cfg.str ("types", "ls,pk,pk,hs");
                size_t i = 0;
                while (i < t.size() && nb < 6)
                {
                    auto j = t.find (',', i); if (j == std::string::npos) j = t.size();
                    const auto k = t.substr (i, j - i);
                    types[nb] = k == "ls" ? 0 : k == "hs" ? 2 : k == "lp" ? 3 : k == "hp" ? 4 : 1;
                    auto fr = cfg.list (("f" + std::to_string (nb)).c_str());
                    fLo[nb] = fr.size() > 0 ? fr[0] : 1000; fHi[nb] = fr.size() > 1 ? fr[1] : fLo[nb];
                    const std::string idx = std::to_string (nb);
                    g[nb] = role (*this, ("g" + idx).c_str(), 0.5f);
                    fq[nb] = role (*this, ("f" + idx).c_str(), 0.5f);
                    qq[nb] = role (*this, ("q" + idx).c_str(), 0.5f);
                    ++nb; i = j + 1;
                }
                qDef = cfg.list ("q");
                range = (float) cfg.num ("range", 15);
                pLevel = role (*this, "level", 0.5f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int b = 0; b < nb; ++b)
                {
                    const double freq = fLo[b] * std::pow (fHi[b] / fLo[b], (double) fq[b].get (*this));
                    const double q0 = qDef.size() > (size_t) b ? qDef[(size_t) b] : 0.9;
                    const double q = qq[b].present() ? q0 * std::pow (8.0, (qq[b].get (*this) - 0.5) * 2.0) : q0;
                    const double gain = (g[b].get (*this) - 0.5) * 2.0 * range;
                    for (int c = 0; c < 2; ++c)
                    {
                        switch (types[b])
                        {
                            case 0: f[c][b].lowShelf (sr, freq, q, gain); break;
                            case 2: f[c][b].highShelf (sr, freq, q, gain); break;
                            case 3: f[c][b].lowPass (sr, freq, q); break;
                            case 4: f[c][b].highPass (sr, freq, q); break;
                            default: f[c][b].peak (sr, freq, q, gain); break;
                        }
                    }
                }
                const float lvl = dbToGain ((pLevel.get (*this) - 0.5f) * 2.0f * range);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (int b = 0; b < nb; ++b) y = f[c][b].process (y);
                        ch[c][i] = y * lvl;
                    }
            }
        private:
            Config cfg;
            int nb = 0, types[6] {};
            double fLo[6] {}, fHi[6] {}, sr = 48000;
            std::vector<double> qDef;
            float range = 15;
            RoleParam g[6], fq[6], qq[6], pLevel;
            pt::dsp::Biquad f[2][6];
        };

        //==============================================================================
        class WahEffect : public Effect
        {
        public:
            explicit WahEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                type = cfg.str ("type", "touch");
                auto fr = cfg.list ("f"); fLo = fr.size() > 0 ? fr[0] : 250; fHi = fr.size() > 1 ? fr[1] : 3000;
                auto q = cfg.list ("q"); qLo = q.size() > 0 ? q[0] : 1.5; qHi = q.size() > 1 ? q[1] : 12;
                lpMode = cfg.str ("filt", "bp") == "lp";
                atk = cfg.num ("attack", 4); rel = cfg.num ("release", 120);
                auto r = cfg.list ("rate"); rateLo = r.size() > 0 ? r[0] : 0.1; rateHi = r.size() > 1 ? r[1] : 8;
                pSens = role (*this, "sens", 0.5f);
                pPeak = role (*this, "peak", 0.5f);
                pFreq = role (*this, "freq", 0.3f);
                pMode = role (*this, "mode", 0.0f);
                pFilt = role (*this, "filter", lpMode ? 1.0f : 0.0f);
                pRate = role (*this, "rate", 0.3f);
                pDepth = role (*this, "depth", 0.7f);
                pDecay = role (*this, "decay", 0.5f);
                pLevel = role (*this, "level", 0.5f);
                pRange = role (*this, "range", 0.5f);
                pDrive = role (*this, "drive", 0.0f);
                pVoice = role (*this, "voice", 0.0f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { svf.reset(); env.env = 0; lfo.phase = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                const int voice = pVoice.step (*this);
                const float q = logMap (pPeak.get (*this), (float) qLo, (float) qHi) * (1.0f + 0.12f * (float) (voice % 4));
                const double rangeK = std::pow (2.0, (pRange.get (*this) - 0.5) * 1.4);    // RANGE: sposta la corsa
                const double fL = fLo * std::sqrt (rangeK), fH = fHi * rangeK;
                const float drive = 1.0f + 10.0f * pDrive.get (*this);
                const float sens = pSens.get (*this), base = pFreq.get (*this), depth = pDepth.get (*this);
                const bool down = pMode.step (*this) == 1 && type != "lfo";
                const bool lp = pFilt.present() ? pFilt.step (*this) > 0 : lpMode;
                env.set (sr, atk, logMap (pDecay.get (*this), (float) rel * 0.3f, (float) rel * 3.0f));
                lfo.setRate (sr, logMap (pRate.get (*this), (float) rateLo, (float) rateHi));
                const float outGain = pLevel.present() ? taperA (pLevel.get (*this)) * 2.0f : 1.0f;
                for (int i = 0; i < n; i += 8)
                {
                    const int len = std::min (8, n - i);
                    float e = 0;
                    for (int k = 0; k < len; ++k)
                    {
                        float x = 0;
                        for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (ch[c][i + k]));
                        e = env.tick (x);
                    }
                    float pos;
                    if (type == "pedal") pos = base;
                    else if (type == "lfo") { pos = base + depth * 0.5f * (1.0f + lfo.next (0)) * (1.0f - base); lfo.advance (len); }
                    else
                    {
                        const float drive = std::clamp (e * (2.0f + 60.0f * sens * sens), 0.0f, 1.0f);
                        pos = down ? std::clamp (base + (1.0f - base) * (1.0f - drive), 0.0f, 1.0f)
                                   : std::clamp (base + (1.0f - base) * drive, 0.0f, 1.0f);
                    }
                    svf.set (sr, fL * std::pow (fH / fL, (double) pos), q);
                    for (int k = 0; k < len; ++k)
                        for (int c = 0; c < numCh; ++c)
                        {
                            float l, b, h;
                            const float in = drive > 1.01f ? std::tanh (ch[c][i + k] * drive) / std::tanh (drive) : ch[c][i + k];
                            svf.tick (c, in, l, b, h);
                            ch[c][i + k] = (lp ? l : b * 1.6f) * outGain;
                        }
                }
            }
        private:
            Config cfg;
            std::string type;
            double fLo = 250, fHi = 3000, qLo = 1.5, qHi = 12, atk = 4, rel = 120, rateLo = 0.1, rateHi = 8, sr = 48000;
            bool lpMode = false;
            RoleParam pSens, pPeak, pFreq, pMode, pFilt, pRate, pDepth, pDecay, pLevel, pRange, pDrive, pVoice;
            SVF svf;
            Envelope env;
            LFO lfo;
        };

        //==============================================================================
        // Wah a induttore (Dunlop Cry Baby, Vox V846/V847 e derivati) modellato sul circuito.
        //
        // Topologia (Holters & Zoelzer, DAFx-11; Electrosmash GCB-95), nodi in regime di piccolo segnale:
        //   Q0 inseguitore d'ingresso (passa-alto Cin1 su ~1M)  ->  vi
        //   vi -Ri(68k)- n2 -Ci(10n)- b(Q1) ;  b -Rf(1k5)- n7 ;  Q1: collettore c (Rc 22k), emettitore e (Re 390)
        //   c -R4(470k)- n6 ;  n6 -L(500m)+Rl- n7 ;  n6 -Rq(33k)- n7 ;  n6 -C2(4u7)||R8(82k)- massa
        //   n7 -C(10n)- n12 (emettitore di Q2) ;  c -C4(220n)- n8 (uscita) ;  pot: n8 -(1-a)P- w -aP+Rt- massa
        //   w -C5(220n)- b2 ;  c -R5(470k)- b2 ;  Q2 inseguitore: b2 -> n12 (R9 10k)
        // Il condensatore C riporta su n7 una frazione 'a' dell'uscita di Q1 (invertente, guadagno ~33):
        // la capacita' vista dall'induttore e' moltiplicata per (1 + a*A) (effetto Miller) e il picco
        // scorre da ~2.2 kHz (a = 0, punta) a ~400 Hz (a = 1, tallone). Rq fissa la Q; le perdite
        // dell'induttore (Rl) abbassano il picco soprattutto al tallone.
        //
        // Rete lineare risolta con l'analisi nodale (10 nodi) e i modelli "compagni" trapezoidali
        // degli elementi reattivi con costante prewarpata sul picco; stati fisici (tensioni e correnti
        // dei rami): i coefficienti sono ricalcolati ogni 8 campioni e interpolati campione per campione,
        // i comandi continui levigati da due poli, quindi niente zipper mentre il pedale si muove (anche
        // con un'espressione MIDI a 7 bit). Q1 e' l'unico elemento non lineare: giunzione esponenziale (corrente di collettore
        // Icq*e^(vbe/Vt)) con resistenza d'emettitore e saturazione morbida del collettore verso
        // l'emettitore, risolta con Newton (una incognita) a ogni campione. Q2 e' un inseguitore lineare
        // (ibrido-pi). Punto di lavoro calcolato dai componenti. Sovracampionamento 2x (os=1|2|4).
        //
        // Config (valori con suffissi SI p n u m k M, anche 4k7):
        //   type=inductor  L=500m Rl=35 C=10n Rq=33k Rf=1.5k Ri=68k Ci=10n Rc=22k Re=390 R4=470k R5=470k
        //   R8=82k C2=4.7u C4=220n C5=220n R9=10k pot=100k Rt=0 Rload=1M hfe=1430 vcc=8.15
        //   taper=hot|A|B|C|evh|p<esp>|x:y,x:y...  travel=punta,tallone (rotazione del pot raggiunta, 0..1)
        //   hp=16 (passa-alto del buffer d'ingresso, 0 = senza buffer)  out=dB (livello d'uscita)
        //   boost=dB (stadio di boost: attivo con la levetta di ruolo "boost" se c'e')  brail=V
        //   dist=dB dlevel=dB dtone=Hz (distorsione prima del wah, attiva con la levetta "dist")  os=2
        // Ogni valore puo' essere:
        //   costante                       33k
        //   lista per selettore            10n|13n|15n|18n|24n|33n@range   (senza @: comando "voice")
        //   escursione su un comando       10k~150k@q:L   (curva B lineare, L logaritmica, A audio, C antilog)
        // Comandi (ruoli): freq = bilanciere (0 tallone, 1 punta: rotazione del pot = travel, curva = taper),
        // boost e dist = levette che inseriscono gli stadi omonimi; tutti gli altri (q, range, voice, fine,
        // level, gain...) agiscono attraverso le espressioni dei valori che li citano.
        namespace inductor
        {
            constexpr double Vt = 0.02585;

            double siNumber (std::string s, bool& ok)
            {
                ok = false;
                while (! s.empty() && s.back() == ' ') s.pop_back();
                if (s.empty()) return 0;
                for (size_t i = 1; i + 1 < s.size(); ++i)        // notazione 4k7 / 2n2 / 1M5
                    if (std::string ("pnumkMR").find (s[i]) != std::string::npos
                        && std::isdigit ((unsigned char) s[i - 1]) && std::isdigit ((unsigned char) s[i + 1]))
                    {
                        s = s.substr (0, i) + "." + s.substr (i + 1) + (s[i] == 'R' ? "" : std::string (1, s[i]));
                        break;
                    }
                double mult = 1;
                bool suffix = true;
                switch (s.back())
                {
                    case 'p': mult = 1e-12; break;
                    case 'n': mult = 1e-9; break;
                    case 'u': mult = 1e-6; break;
                    case 'm': mult = 1e-3; break;
                    case 'k': case 'K': mult = 1e3; break;
                    case 'M': mult = 1e6; break;
                    case 'R': break;
                    default: suffix = false; break;
                }
                if (suffix) s.pop_back();
                char* end = nullptr;
                const double v = std::strtod (s.c_str(), &end);
                ok = ! s.empty() && end != nullptr && *end == 0;
                return v * mult;
            }

            inline double curve (double x, char c)
            {
                x = std::clamp (x, 0.0, 1.0);
                constexpr double b = 32.11;          // stessa curva "A" del resto del progetto (15% a meta')
                switch (c)
                {
                    case 'A': return (std::pow (b, x) - 1.0) / (b - 1.0);
                    case 'C': return 1.0 - (std::pow (b, 1.0 - x) - 1.0) / (b - 1.0);
                    default:  return x;
                }
            }

            /** Valore di un componente: costante, lista per selettore o escursione su un comando. */
            struct Value
            {
                enum Kind { Const, List, Span } kind = Const;
                std::vector<double> v { 0.0 };
                int ctl = -1;
                char shape = 'B';

                static Value make (double c) { Value r; r.v = { c }; return r; }

                bool parse (const std::string& text, const ModelDef& def)
                {
                    std::string body = text, role;
                    if (const auto at = text.find ('@'); at != std::string::npos)
                    {
                        body = text.substr (0, at);
                        role = text.substr (at + 1);
                        if (const auto colon = role.find (':'); colon != std::string::npos)
                        {
                            shape = colon + 1 < role.size() ? role[colon + 1] : 'B';
                            role = role.substr (0, colon);
                        }
                    }
                    bool ok = true;
                    v.clear();
                    auto add = [&] (const std::string& t) { bool k = false; v.push_back (siNumber (t, k)); ok = ok && k; };
                    if (const auto tl = body.find ('~'); tl != std::string::npos)
                    {
                        kind = Span;
                        add (body.substr (0, tl)); add (body.substr (tl + 1));
                    }
                    else if (body.find ('|') != std::string::npos)
                    {
                        kind = List;
                        size_t i = 0;
                        while (i <= body.size())
                        {
                            auto j = body.find ('|', i); if (j == std::string::npos) j = body.size();
                            add (body.substr (i, j - i));
                            i = j + 1;
                        }
                        if (role.empty()) role = "voice";
                    }
                    else { kind = Const; add (body); }
                    if (! role.empty())
                        for (int i = 0; i < def.numControls; ++i)
                            if (def.controls[i].role != nullptr && role == def.controls[i].role) { ctl = i; break; }
                    if (! ok || v.empty()) { kind = Const; v = { 0.0 }; }
                    return ok;
                }

                double eval (const float* p, const ModelDef& def) const
                {
                    if (kind == Const || v.size() < 2) return v[0];
                    const double x = ctl >= 0 ? std::clamp ((double) p[ctl], 0.0, 1.0) : 0.0;
                    if (kind == List)
                    {
                        const int n = (int) v.size();
                        const int steps = ctl >= 0 ? (int) def.controls[ctl].steps : 0;
                        const int idx = steps >= 2 ? (int) std::lround (x * (steps - 1)) : (int) std::lround (x * (n - 1));
                        return v[(size_t) std::clamp (idx, 0, n - 1)];
                    }
                    const double lo = v[0], hi = v[1];
                    if (shape == 'L' && lo > 0 && hi > 0) return lo * std::pow (hi / lo, x);
                    return lo + (hi - lo) * curve (x, shape);
                }
            };

            /** Punto di lavoro di Q1 e Q2 (polarizzazione a retroazione di collettore). */
            struct Bias { double ic1 = 1.6e-4, vc1 = 4.4, ve1 = 0.06, ic2 = 3.7e-4; };

            Bias solveBias (double vcc, double beta, double rc, double re, double r4, double r5, double r8,
                            double rf, double rl, double r9)
            {
                constexpr double is = 20.3e-15;          // MPSA18 (Holters & Zoelzer)
                auto q2 = [&] (double vc1)
                {
                    double lo = 1e-13, hi = std::max (1e-9, vc1 / r9 + 1e-9);
                    for (int it = 0; it < 90; ++it)
                    {
                        const double m = std::sqrt (lo * hi);
                        const double f = vc1 - r5 * m / beta - (Vt * std::log (m / is) + r9 * m * (beta + 1) / beta);
                        (f > 0 ? lo : hi) = m;
                    }
                    return std::sqrt (lo * hi);
                };
                Bias b;
                auto resid = [&] (double ic1)
                {
                    const double ib1 = ic1 / beta;
                    const double vb1 = Vt * std::log (ic1 / is) + re * ic1 * (beta + 1) / beta;
                    const double v6 = vb1 + ib1 * (rf + rl);
                    const double i4 = v6 / r8 + ib1;
                    const double vc1 = v6 + r4 * i4;
                    const double ic2 = q2 (vc1);
                    b = { ic1, vc1, re * ic1 * (beta + 1) / beta, ic2 };
                    return (vcc - vc1) / rc - ic1 - i4 - ic2 / beta;
                };
                double lo = 1e-9, hi = vcc / rc;
                for (int it = 0; it < 90; ++it)
                {
                    const double m = std::sqrt (lo * hi);
                    (resid (m) > 0 ? lo : hi) = m;
                }
                resid (std::sqrt (lo * hi));
                return b;
            }

            /** Curva del potenziometro: frazione di resistenza tra cursore e massa per rotazione 0..1. */
            struct Taper
            {
                char kind = 'p';                 // A, B, C, p (potenza), t (punti)
                double expo = 2.0;
                std::vector<std::pair<double, double>> pts;

                void parse (const std::string& s)
                {
                    if (s == "A" || s == "B" || s == "C") { kind = s[0]; return; }
                    if (s == "hot" || s.empty()) { kind = 'p'; expo = 2.0; return; }    // Hot Potz (stimata: ~750 Hz a meta' corsa)
                    if (s == "evh")      // pista "consumata" del pot di EVH: piu' risoluzione a meta' corsa (stimata)
                    {
                        kind = 't'; pts = { { 0, 0 }, { 0.2, 0.012 }, { 0.4, 0.06 }, { 0.6, 0.2 }, { 0.8, 0.5 }, { 1, 1 } };
                        return;
                    }
                    if (s[0] == 'p') { kind = 'p'; expo = std::max (0.2, std::atof (s.c_str() + 1)); return; }
                    kind = 't'; pts = { { 0, 0 } };
                    size_t i = 0;
                    while (i < s.size())
                    {
                        auto j = s.find (',', i); if (j == std::string::npos) j = s.size();
                        const auto tok = s.substr (i, j - i);
                        const auto c = tok.find (':');
                        if (c != std::string::npos) pts.push_back ({ std::atof (tok.substr (0, c).c_str()), std::atof (tok.substr (c + 1).c_str()) });
                        i = j + 1;
                    }
                    pts.push_back ({ 1, 1 });
                }

                double operator() (double r) const
                {
                    r = std::clamp (r, 0.0, 1.0);
                    switch (kind)
                    {
                        case 'A': case 'B': case 'C': return curve (r, kind);
                        case 'p': return std::pow (r, expo);
                        default: break;
                    }
                    for (size_t k = 1; k < pts.size(); ++k)
                        if (r <= pts[k].first)
                        {
                            const double w = pts[k].first - pts[k - 1].first;
                            const double f = w > 0 ? (r - pts[k - 1].first) / w : 1.0;
                            return pts[k - 1].second + f * (pts[k].second - pts[k - 1].second);
                        }
                    return 1.0;
                }
            };

            inline double railClip (double x, double v) noexcept
            {
                const double a = x / v, a2 = a * a, a4 = a2 * a2;
                return x / std::sqrt (std::sqrt (std::sqrt (1.0 + a4 * a4)));
            }
        }

        class InductorWahEffect : public Effect
        {
        public:
            explicit InductorWahEffect (const ModelDef& d) : Effect (d)
            {
                const Config cfg (d.config);
                auto val = [&] (const char* key, double fallback)
                {
                    const auto s = cfg.str (key, "");
                    auto v = inductor::Value::make (fallback);
                    if (! s.empty()) v.parse (s, d);
                    return v;
                };
                L = val ("L", 0.5);   Rl = val ("Rl", 35);     C3 = val ("C", 10e-9);   Rq = val ("Rq", 33e3);
                Rf = val ("Rf", 1.5e3); Ri = val ("Ri", 68e3); Ci = val ("Ci", 10e-9);  Rc = val ("Rc", 22e3);
                Re = val ("Re", 390); R4 = val ("R4", 470e3);  R5 = val ("R5", 470e3);  R8 = val ("R8", 82e3);
                C2 = val ("C2", 4.7e-6); C4 = val ("C4", 220e-9); C5 = val ("C5", 220e-9); R9 = val ("R9", 10e3);
                Pot = val ("pot", 100e3); Rt = val ("Rt", 0);  Rload = val ("Rload", 1e6);
                Hfe = val ("hfe", 1430); Vcc = val ("vcc", 8.15);
                Out = val ("out", 0);  Boost = val ("boost", 0); Dist = val ("dist", 0); DLevel = val ("dlevel", 0);
                hasBoost = ! cfg.str ("boost", "").empty();
                hasDist = ! cfg.str ("dist", "").empty();
                taper.parse (cfg.str ("taper", "hot"));
                auto tr = cfg.list ("travel");
                travelToe = tr.size() > 0 ? std::clamp (tr[0], 0.0, 1.0) : 0.0;
                travelHeel = tr.size() > 1 ? std::clamp (tr[1], 0.0, 1.0) : 1.0;
                hpHz = cfg.num ("hp", 16.0);
                brail = cfg.num ("brail", 3.8);
                dtone = cfg.num ("dtone", 3500.0);
                osLog2 = std::clamp ((int) std::lround (std::log2 (std::max (1.0, cfg.num ("os", 2.0)))), 0, 2);
                pFreq = role (*this, "freq", 0.5f);
                pBoostSw = role (*this, "boost", 1.0f);
                pDistSw = role (*this, "dist", 1.0f);
            }

            void prepare (double s, int maxBlock) override
            {
                sr = s;
                if (osLog2 > 0)
                {
                    os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) osLog2,
                             juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
                    os->initProcessing ((size_t) std::max (1, maxBlock));
                }
                else os.reset();
                fs = sr * (double) (1 << osLog2);
                for (int i = 0; i < maxControls; ++i) sm[i] = sm1[i] = p (i);
                smoothK = 1.0 - std::exp (-(double) sub / (0.004 * fs));      // 2 x 4 ms
                biasKey = -1;
                updateCoefficients();
                std::copy (std::begin (tgt), std::end (tgt), std::begin (cur));
                const double w = 2.0 * pi * std::clamp (hpHz, 0.0, fs * 0.2) / fs;
                hpA = (1.0 - std::tan (w / 2)) / (1.0 + std::tan (w / 2));
                hpB = 1.0 / (1.0 + std::tan (w / 2));
                const double wt = std::tan (pi * std::clamp (dtone, 20.0, fs * 0.45) / fs);
                dtA = (1.0 - wt) / (1.0 + wt);
                reset();
            }

            void reset() override
            {
                for (auto& s : st) s = {};
                if (os) os->reset();
            }

            void process (float* const* ch, int numCh, int n) override
            {
                if (n <= 0) return;
                numCh = std::min (numCh, 2);
                juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
                juce::dsp::AudioBlock<float> up = os ? os->processSamplesUp (block) : block;
                const int N = (int) up.getNumSamples();
                float* data[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
                const bool distOn = hasDist && (! pDistSw.present() || pDistSw.step (*this) > 0);
                const bool boostOn = hasBoost && (! pBoostSw.present() || pBoostSw.step (*this) > 0);
                for (int i0 = 0; i0 < N; i0 += sub)
                {
                    const int len = std::min (sub, N - i0);
                    // coefficienti nuovi ogni 'sub' campioni, interpolati linearmente campione per campione:
                    // la rete cambia con continuita' come il potenziometro vero
                    const bool ramp = smoothControls();
                    if (ramp)
                        for (int j = 0; j < nC; ++j) inc[j] = (tgt[j] - cur[j]) / len;
                    for (int k = 0; k < len; ++k)
                    {
                        const double gBoost = boostOn ? cur[cBoost] : 1.0, gOut = cur[cOut];
                        for (int c = 0; c < numCh; ++c)
                        {
                            auto& s = st[c];
                            float* x = data[c] + i0;
                            double v = x[k];
                            if (distOn)     // distorsione (SW95): guadagno, diodi morbidi, passa-basso
                            {
                                const double u = v * cur[cDist] / 0.6, y = 0.6 * u / std::sqrt (1.0 + u * u);
                                s.dlp = (1.0 - dtA) * 0.5 * (y + s.dlx) + dtA * s.dlp;
                                s.dlx = y;
                                v = s.dlp * cur[cDLvl];
                            }
                            if (hpHz > 0)  // buffer d'ingresso: passa-alto del primo ordine
                            {
                                const double y = hpB * (v - s.hpx) + hpA * s.hpy;
                                s.hpx = v; s.hpy = y; v = y;
                            }
                            v = tick (s, v);
                            if (hasBoost) v = boostOn ? inductor::railClip (v * gBoost, brail) : v;
                            x[k] = (float) (v * gOut);
                        }
                        if (ramp)
                            for (int j = 0; j < nC; ++j) cur[j] += inc[j];
                    }
                    if (ramp)
                        std::copy (std::begin (tgt), std::end (tgt), std::begin (cur));
                }
                if (os) os->processSamplesDown (block);
            }

        private:
            static constexpr double pi = 3.14159265358979323846;
            static constexpr int sub = 8;                   // campioni (sovracampionati) tra due aggiornamenti
            enum { nN = 10, nB = 6, nR = 8 };               // nodi, rami reattivi, righe d'uscita
            // coefficienti in un vettore unico (per l'interpolazione): matrice W, conduttanze dei rami
            // reattivi, K*L - Rl, guadagni d'uscita / boost / distorsione
            enum { cW = 0, cG = nR * nR, cKL = cG + nB, cOut, cBoost, cDist, cDLvl, nC };
            enum Node { n2, nb, ne, nc, n6, n7, n8, nw, nb2, n12 };

            struct State
            {
                double v[nB] {}, i[nB] {};                  // tensione e corrente dei rami reattivi
                double x = 0;                               // vbe di Q1 / Vt (stima per Newton)
                double hpx = 0, hpy = 0, dlx = 0, dlp = 0;
            };

            /** Comandi continui levigati da due poli in cascata (traiettoria senza spigoli: niente zipper
                anche con un'espressione MIDI a 7 bit); selettori e levette scattano subito. */
            bool smoothControls()
            {
                bool changed = false;
                for (int i = 0; i < def.numControls; ++i)
                {
                    const float target = p (i);
                    float a1 = sm1[i], v = sm[i];
                    if (def.controls[i].steps >= 2) a1 = v = target;
                    else if (std::abs (target - a1) > 1e-6f || std::abs (target - v) > 1e-6f)
                    {
                        a1 += (float) smoothK * (target - a1);
                        v += (float) smoothK * (a1 - v);
                    }
                    else a1 = v = target;
                    sm1[i] = a1;
                    if (std::abs (v - sm[i]) > 0.0f) { sm[i] = v; changed = true; }
                }
                if (changed) updateCoefficients();
                return changed;
            }

            void updateCoefficients()
            {
                const double l = std::max (1e-4, L.eval (sm, def)), rl = std::max (0.0, Rl.eval (sm, def));
                const double c3 = std::max (1e-12, C3.eval (sm, def)), rq = std::max (100.0, Rq.eval (sm, def));
                const double rf = std::max (1.0, Rf.eval (sm, def)), ri = std::max (1.0, Ri.eval (sm, def));
                const double ci = std::max (1e-12, Ci.eval (sm, def)), rc = std::max (100.0, Rc.eval (sm, def));
                const double re = std::max (0.0, Re.eval (sm, def)), r4 = std::max (1e3, R4.eval (sm, def));
                const double r5 = std::max (1e3, R5.eval (sm, def)), r8 = std::max (1e3, R8.eval (sm, def));
                const double c2 = std::max (1e-12, C2.eval (sm, def)), c4 = std::max (1e-12, C4.eval (sm, def));
                const double c5 = std::max (1e-12, C5.eval (sm, def)), r9 = std::max (100.0, R9.eval (sm, def));
                const double pot = std::max (1e3, Pot.eval (sm, def)), rt = std::max (0.0, Rt.eval (sm, def));
                const double rload = std::max (1e3, Rload.eval (sm, def));
                beta = std::max (20.0, Hfe.eval (sm, def));
                const double vcc = std::clamp (Vcc.eval (sm, def), 3.0, 30.0);

                // punto di lavoro: ricalcolato solo se cambia (oltre lo 0.2%) un componente che lo determina
                // (la resistenza in serie all'induttore e Rf vi pesano solo con la corrente di base: trascurate)
                const double bv[8] = { vcc, beta, rc, re, r4, r5, r8, r9 };
                bool newBias = biasKey < 0;
                for (int k = 0; k < 8; ++k)
                    if (std::abs (bv[k] - biasVals[k]) > 2e-3 * std::max (1.0, std::abs (biasVals[k]))) newBias = true;
                if (newBias)
                {
                    biasKey = 1;
                    std::copy (bv, bv + 8, biasVals);
                    const auto b = inductor::solveBias (vcc, beta, rc, re, r4, r5, r8, rf, 0.0, r9);
                    icq = b.ic1;
                    gm2 = b.ic2 / inductor::Vt;
                    headroom = std::max (0.3, b.vc1 - b.ve1 - 0.15);
                }
                tgt[cDist] = std::pow (10.0, Dist.eval (sm, def) / 20.0);
                tgt[cDLvl] = std::pow (10.0, DLevel.eval (sm, def) / 20.0);
                tgt[cBoost] = std::pow (10.0, Boost.eval (sm, def) / 20.0);
                tgt[cOut] = std::pow (10.0, Out.eval (sm, def) / 20.0);

                // posizione del bilanciere -> rotazione del potenziometro -> frazione 'a' verso massa
                const double t = std::clamp ((double) sm[std::max (0, pFreq.idx)], 0.0, 1.0);
                const double rot = pFreq.present() ? travelToe + (travelHeel - travelToe) * (1.0 - t) : 0.5;
                const double a = std::clamp (taper (rot), 0.0, 1.0);
                const double rTop = std::max (1.0, (1.0 - a) * pot), rBot = std::max (1.0, a * pot + rt);
                const double rcac = 1.0 / (1.0 / rc + 1.0 / r4 + 1.0 / r5 + 1.0 / pot);
                dImax = headroom / rcac;

                // costante della trasformazione bilineare prewarpata sul picco stimato
                const double gain = rcac / (inductor::Vt / icq + re);
                const double af = rBot / (rBot + rTop);
                double w0 = 1.0 / std::sqrt (l * c3 * (1.0 + af * gain));
                w0 = std::min (w0, 0.7 * pi * fs);
                const double K = w0 / std::tan (w0 / (2.0 * fs));
                tgt[cKL] = K * l - rl;

                // matrice nodale
                double Y[nN][nN] {}, B[nN][nR] {};
                auto g = [&Y] (int a_, int b_, double gv)
                {
                    if (a_ >= 0) Y[a_][a_] += gv;
                    if (b_ >= 0) Y[b_][b_] += gv;
                    if (a_ >= 0 && b_ >= 0) { Y[a_][b_] -= gv; Y[b_][a_] -= gv; }
                };
                g (n2, -1, 1.0 / ri);          // Ri dal generatore (la sorgente va nel termine noto)
                g (nb, n7, 1.0 / rf);
                g (ne, -1, 1.0 / std::max (1.0, re));
                g (nc, -1, 1.0 / rc);
                g (nc, n6, 1.0 / r4);
                g (n6, -1, 1.0 / r8);
                g (n6, n7, 1.0 / rq);
                g (n8, nw, 1.0 / rTop);
                g (nw, -1, 1.0 / rBot);
                g (n8, -1, 1.0 / rload);
                g (nc, nb2, 1.0 / r5);
                g (nb2, n12, gm2 / beta);      // r_pi di Q2
                g (n12, -1, 1.0 / r9);
                Y[n12][nb2] -= gm2;            // Q2: gm*(vb2 - ve2) entra nell'emettitore
                Y[n12][n12] += gm2;
                const int from[nB] = { n2, n6, n6, n7, nc, nw }, to[nB] = { nb, -1, n7, n12, n8, nb2 };
                const double G[nB] = { K * ci, K * c2, 1.0 / (rl + K * l), K * c3, K * c4, K * c5 };
                for (int k = 0; k < nB; ++k) tgt[cG + k] = G[k];
                for (int k = 0; k < nB; ++k)
                {
                    g (from[k], to[k], G[k]);
                    B[from[k]][k] += 1.0;
                    if (to[k] >= 0) B[to[k]][k] -= 1.0;
                }
                B[n2][6] = 1.0 / ri;                                   // ingresso
                B[nc][7] = -1.0; B[nb][7] = -1.0 / beta; B[ne][7] = 1.0 + 1.0 / beta;   // corrente di Q1

                // eliminazione di Gauss con pivot parziale: X = Y^-1 B
                for (int k = 0; k < nN; ++k)
                {
                    int piv = k;
                    for (int r = k + 1; r < nN; ++r) if (std::abs (Y[r][k]) > std::abs (Y[piv][k])) piv = r;
                    if (piv != k)
                    {
                        for (int c = 0; c < nN; ++c) std::swap (Y[k][c], Y[piv][c]);
                        for (int c = 0; c < nR; ++c) std::swap (B[k][c], B[piv][c]);
                    }
                    const double inv = 1.0 / Y[k][k];
                    for (int r = k + 1; r < nN; ++r)
                    {
                        const double f = Y[r][k] * inv;
                        for (int c = k; c < nN; ++c) Y[r][c] -= f * Y[k][c];
                        for (int c = 0; c < nR; ++c) B[r][c] -= f * B[k][c];
                    }
                }
                for (int k = nN - 1; k >= 0; --k)
                    for (int c = 0; c < nR; ++c)
                    {
                        double sum = B[k][c];
                        for (int j = k + 1; j < nN; ++j) sum -= Y[k][j] * B[j][c];
                        B[k][c] = sum / Y[k][k];
                    }
                // righe d'uscita: tensioni dei rami reattivi, vbe di Q1, uscita n8
                for (int c = 0; c < nR; ++c)
                {
                    for (int k = 0; k < nB; ++k)
                        tgt[cW + k * nR + c] = B[from[k]][c] - (to[k] >= 0 ? B[to[k]][c] : 0.0);
                    tgt[cW + 6 * nR + c] = B[nb][c] - B[ne][c];
                    tgt[cW + 7 * nR + c] = B[n8][c];
                }
            }

            /** Un campione del circuito (tensioni in volt). */
            inline double tick (State& s, double vi) noexcept
            {
                const double* G = cur + cG;
                auto W = [this] (int r, int c) { return cur[cW + r * nR + c]; };
                double J[nB];
                for (int k = 0; k < nB; ++k) J[k] = G[k] * s.v[k] + s.i[k];
                J[2] = -G[2] * (s.v[2] + cur[cKL] * s.i[2]);          // ramo L + Rl
                double y[nR];
                for (int r = 0; r < nR; ++r)
                {
                    double acc = W (r, 6) * vi;
                    for (int k = 0; k < nB; ++k) acc += W (r, k) * J[k];
                    y[r] = acc;
                }
                // Newton su x = vbe/Vt:  Vt*x = y6 + h*I(x),  I = corrente di collettore (variazione) di Q1
                const double h = W (6, 7);
                double x = std::min (s.x, 30.0), I = 0;
                double e = std::exp (x);
                for (int it = 0; it < 16; ++it)
                {
                    double d = icq * (e - 1.0), dd = icq * e;
                    if (d > 0)          // saturazione del collettore (si avvicina all'emettitore)
                    {
                        const double z = d / dImax, z4 = (z * z) * (z * z);
                        const double q = 1.0 / std::sqrt (std::sqrt (1.0 + z4));
                        dd *= q / (1.0 + z4);
                        d *= q;
                    }
                    const double F = y[6] + h * d - inductor::Vt * x;
                    double dF = h * dd - inductor::Vt;
                    if (dF > -1e-12) dF = -inductor::Vt;
                    const double step = std::clamp (F / dF, -2.0, 2.0);
                    x = std::min (x - step, 30.0);
                    I = d - dd * step;                  // corrente nel nuovo punto (al primo ordine)
                    if (std::abs (step) < 1e-8) break;
                    // e^x aggiornato con la serie di Taylor per i passi piccoli (un solo exp per campione)
                    e = std::abs (step) < 1e-2 ? e * (1.0 - step * (1.0 - step * (0.5 - step * (1.0 / 6.0)))) : std::exp (x);
                }
                s.x = std::isfinite (x) ? x : 0.0;
                if (! std::isfinite (I)) { s = {}; return 0.0; }
                for (int k = 0; k < nB; ++k)
                {
                    const double v = y[k] + W (k, 7) * I;
                    s.v[k] = v;
                    s.i[k] = G[k] * v - J[k];
                }
                return y[7] + W (7, 7) * I;
            }

            inductor::Value L, Rl, C3, Rq, Rf, Ri, Ci, Rc, Re, R4, R5, R8, C2, C4, C5, R9, Pot, Rt, Rload, Hfe, Vcc;
            inductor::Value Out, Boost, Dist, DLevel;
            inductor::Taper taper;
            bool hasBoost = false, hasDist = false;
            double travelToe = 0, travelHeel = 1, hpHz = 16, brail = 3.8, dtone = 3500;
            int osLog2 = 1;
            RoleParam pFreq, pBoostSw, pDistSw;
            std::unique_ptr<juce::dsp::Oversampling<float>> os;
            double sr = 48000, fs = 96000, smoothK = 0.1, hpA = 1, hpB = 1, dtA = 0;
            float sm[maxControls] {}, sm1[maxControls] {};
            double biasVals[8] {}, biasKey = -1, icq = 1.6e-4, gm2 = 0.014, beta = 1430, headroom = 4, dImax = 2.6e-4;
            double tgt[nC] {}, cur[nC] {}, inc[nC] {};
            State st[2];
        };

        //==============================================================================
        class AcousticEffect : public Effect
        {
        public:
            explicit AcousticEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                // modes=f:g:q|f:g:q;f:g:q|...  (';' separa i modi, '|' le risonanze)
                const auto m = cfg.str ("modes", "110:6:1.2|220:3:1.5|3000:4:0.8");
                size_t i = 0;
                while (i <= m.size() && numModes < 4)
                {
                    auto j = m.find (';', i); if (j == std::string::npos) j = m.size();
                    const auto mode = m.substr (i, j - i);
                    size_t a = 0; int k = 0;
                    while (a <= mode.size() && k < 4)
                    {
                        auto b = mode.find ('|', a); if (b == std::string::npos) b = mode.size();
                        const auto t = mode.substr (a, b - a);
                        float f = 100, g = 0, q = 1;
                        std::sscanf (t.c_str(), "%f:%f:%f", &f, &g, &q);
                        res[numModes][k++] = { f, g, q };
                        a = b + 1;
                    }
                    resCount[numModes] = k;
                    ++numModes; i = j + 1;
                }
                topHz = cfg.num ("top", 5000);
                cutHz = cfg.num ("cut", 600);       // attenuazione del "quack" dei pick-up magnetici
                pTop = role (*this, "top", 0.5f);
                pBody = role (*this, "body", 0.5f);
                pLevel = role (*this, "level", 0.5f);
                pMode = role (*this, "mode", 0.0f);
                pReverb = role (*this, "reverb", 0.0f);
            }
            void prepare (double s, int) override { sr = s; reset(); }
            void reset() override { for (auto& c : f) for (auto& b : c) b.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                const int m = std::clamp (pMode.step (*this), 0, std::max (0, numModes - 1));
                const float body = pBody.get (*this) * 2.0f, top = (pTop.get (*this) - 0.5f) * 24.0f;
                for (int c = 0; c < 2; ++c)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        if (k < resCount[m]) f[c][k].peak (sr, res[m][k].f, res[m][k].q, res[m][k].g * body);
                        else f[c][k].peak (sr, 1000, 1, 0);
                    }
                    f[c][4].peak (sr, cutHz, 0.8, -7.0);
                    f[c][5].highShelf (sr, topHz, 0.7, top);
                }
                const float lvl = taperA (pLevel.get (*this)) * 2.0f;
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < numCh; ++c)
                    {
                        float y = ch[c][i];
                        for (auto& b : f[c]) y = b.process (y);
                        ch[c][i] = y * lvl;
                    }
            }
        private:
            struct Res { float f, g, q; };
            Config cfg;
            Res res[4][4] {};
            int resCount[4] {}, numModes = 0;
            double topHz = 5000, cutHz = 600, sr = 48000;
            RoleParam pTop, pBody, pLevel, pMode, pReverb;
            pt::dsp::Biquad f[2][6];
        };
    }

    std::unique_ptr<Effect> makeGraphicEQ (const ModelDef& d)    { return std::make_unique<GraphicEQEffect> (d); }
    std::unique_ptr<Effect> makeParametricEQ (const ModelDef& d) { return std::make_unique<ParametricEQEffect> (d); }
    std::unique_ptr<Effect> makeWah (const ModelDef& d)
    {
        if (Config (d.config).str ("type") == "inductor") return std::make_unique<InductorWahEffect> (d);
        return std::make_unique<WahEffect> (d);
    }
    std::unique_ptr<Effect> makeAcoustic (const ModelDef& d)     { return std::make_unique<AcousticEffect> (d); }
}
