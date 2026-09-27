/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Circuit.h"
#include <sstream>
#include <map>

namespace pt::engine
{
    namespace
    {
        constexpr double Vt = 0.02585;          // tensione termica a 25 °C
        constexpr double pi = 3.14159265358979323846;

        std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\n\r");
            if (a == std::string::npos) return {};
            const auto b = s.find_last_not_of (" \t\n\r");
            return s.substr (a, b - a + 1);
        }

        std::vector<std::string> split (const std::string& s, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            int depth = 0;
            for (char c : s)
            {
                if (c == '(') ++depth;
                if (c == ')') --depth;
                if (c == sep && depth == 0) { out.push_back (cur); cur.clear(); }
                else cur += c;
            }
            out.push_back (cur);
            return out;
        }

        /** Limitazione morbida ai binari di alimentazione (op-amp / transistor). */
        inline double railClip (double x, double vpos, double vneg) noexcept
        {
            const double v = x >= 0 ? vpos : vneg;
            const double a = x / v, a2 = a * a, a4 = a2 * a2;
            return x / std::sqrt (std::sqrt (std::sqrt (1.0 + a4 * a4)));   // ginocchio ~ 8° ordine
        }

        /** Corrente di una coppia di diodi antiparalleli (nUp in serie verso +, nDn verso -). */
        struct DiodePair
        {
            DiodeModel d;
            double up = 1, dn = 1;      // numero di diodi in serie per verso (0 = assente)

            inline void eval (double v, double& i, double& di) const noexcept
            {
                i = 0; di = 0;
                if (up > 0)
                {
                    const double k = 1.0 / (up * d.n * Vt);
                    const double e = std::exp (std::min (v * k, 80.0));
                    i += d.is * (e - 1.0);
                    di += d.is * k * e;
                }
                if (dn > 0)
                {
                    const double k = 1.0 / (dn * d.n * Vt);
                    const double e = std::exp (std::min (-v * k, 80.0));
                    i -= d.is * (e - 1.0);
                    di += d.is * k * e;
                }
            }
        };

        DiodePair diodesFromArgs (const Stage& s, const float* p, const ModelDef& def, const std::string& name)
        {
            DiodePair dp;
            dp.d = DiodeModel::byName (name);
            dp.up = s.arg ("up", p, def, 1.0);
            dp.dn = s.arg ("dn", p, def, 1.0);
            if (s.has ("is")) dp.d.is = s.arg ("is", p, def, dp.d.is);
            if (s.has ("n"))  dp.d.n  = s.arg ("n", p, def, dp.d.n);
            return dp;
        }
    }

    //==============================================================================
    // Espressioni
    //==============================================================================
    double Expr::parseNumber (const std::string& raw, bool& ok)
    {
        std::string s = trim (raw);
        ok = false;
        if (s.empty()) return 0;
        double mult = 1.0;
        // notazione 4k7 / 2n2
        for (size_t i = 1; i + 1 < s.size(); ++i)
        {
            const char c = s[i];
            if (std::string ("pnumkMR").find (c) != std::string::npos && std::isdigit ((unsigned char) s[i - 1])
                && std::isdigit ((unsigned char) s[i + 1]))
            {
                s = s.substr (0, i) + "." + s.substr (i + 1) + (c == 'R' ? "" : std::string (1, c));
                break;
            }
        }
        const char last = s.back();
        switch (last)
        {
            case 'p': mult = 1e-12; break;
            case 'n': mult = 1e-9; break;
            case 'u': mult = 1e-6; break;
            case 'm': mult = 1e-3; break;
            case 'k': case 'K': mult = 1e3; break;
            case 'M': mult = 1e6; break;
            case 'R': mult = 1.0; break;
            default: break;
        }
        if (mult != 1.0 || last == 'R')
            s.pop_back();
        char* end = nullptr;
        const double v = std::strtod (s.c_str(), &end);
        ok = end != nullptr && *end == 0 && ! s.empty();
        return v * mult;
    }

    double Expr::applyTaper (double x, char t)
    {
        x = std::clamp (x, 0.0, 1.0);
        constexpr double b = 32.11;                     // curva "A": 15% a meta' corsa
        switch (t)
        {
            case 'A': return (std::pow (b, x) - 1.0) / (b - 1.0);
            case 'C': return 1.0 - (std::pow (b, 1.0 - x) - 1.0) / (b - 1.0);
            case 'W': return x < 0.5 ? 0.5 * applyTaper (2 * x, 'A') : 0.5 + 0.5 * applyTaper (2 * x - 1, 'C');
            default:  return x;
        }
    }

    Expr Expr::parse (const std::string& text, std::string& error)
    {
        Expr e;
        for (const auto& termText : split (text, '+'))
        {
            Term term;
            for (const auto& fRaw : split (termText, '*'))
            {
                const auto f = trim (fRaw);
                Factor fac;
                const auto paren = f.find ('(');
                if (paren == std::string::npos)
                {
                    bool ok = false;
                    fac.kind = Factor::Const;
                    fac.args.push_back (parseNumber (f, ok));
                    if (! ok) error += "numero non valido '" + f + "'; ";
                }
                else
                {
                    const auto fn = trim (f.substr (0, paren));
                    auto inner = f.substr (paren + 1);
                    if (! inner.empty() && inner.back() == ')') inner.pop_back();
                    auto parts = split (inner, ',');
                    fac.idx = std::atoi (trim (parts[0]).c_str());
                    static const std::map<std::string, Factor::Kind> kinds {
                        { "pot", Factor::Pot }, { "potr", Factor::PotR }, { "taper", Factor::Taper },
                        { "lin", Factor::Lin }, { "log", Factor::Log }, { "sw", Factor::Sw } };
                    auto it = kinds.find (fn);
                    if (it == kinds.end()) { error += "funzione sconosciuta '" + fn + "'; "; continue; }
                    fac.kind = it->second;
                    for (size_t i = 1; i < parts.size(); ++i)
                    {
                        const auto a = trim (parts[i]);
                        if ((fac.kind == Factor::Pot || fac.kind == Factor::PotR) && i == 2) { fac.taper = a.empty() ? 'B' : a[0]; continue; }
                        if (fac.kind == Factor::Taper && i == 1) { fac.taper = a.empty() ? 'B' : a[0]; continue; }
                        bool ok = false;
                        fac.args.push_back (parseNumber (a, ok));
                        if (! ok) error += "argomento non valido '" + a + "'; ";
                    }
                }
                term.push_back (fac);
            }
            e.terms.push_back (term);
        }
        return e;
    }

    bool Expr::isConst() const
    {
        for (auto& t : terms) for (auto& f : t) if (f.kind != Factor::Const) return false;
        return true;
    }

    double Expr::eval (const float* p, const ModelDef& def) const
    {
        double sum = 0;
        for (const auto& t : terms)
        {
            double prod = 1;
            for (const auto& f : t)
            {
                const double x = (f.kind == Factor::Const) ? 0.0 : (double) p[std::clamp (f.idx, 0, maxControls - 1)];
                switch (f.kind)
                {
                    case Factor::Const: prod *= f.args.empty() ? 0.0 : f.args[0]; break;
                    case Factor::Pot:   prod *= std::max (10.0, f.args[0] * applyTaper (x, f.taper)); break;
                    case Factor::PotR:  prod *= std::max (10.0, f.args[0] * (1.0 - applyTaper (x, f.taper))); break;
                    case Factor::Taper: prod *= applyTaper (x, f.taper); break;
                    case Factor::Lin:   prod *= f.args[0] + (f.args[1] - f.args[0]) * x; break;
                    case Factor::Log:   prod *= f.args[0] * std::pow (f.args[1] / f.args[0], x); break;
                    case Factor::Sw:
                    {
                        const int steps = std::max (1, (int) def.controls[std::clamp (f.idx, 0, maxControls - 1)].steps);
                        const int s = std::clamp ((int) std::lround (x * (steps - 1)), 0, (int) f.args.size() - 1);
                        prod *= f.args.empty() ? 0.0 : f.args[(size_t) s];
                        break;
                    }
                }
            }
            sum += prod;
        }
        return sum;
    }

    //==============================================================================
    // Filtro analogico -> digitale (bilineare)
    //==============================================================================
    void AnalogTF::design (double fs, double prewarpHz)
    {
        double K = 2.0 * fs;
        if (prewarpHz > 0 && prewarpHz < fs * 0.45)
        {
            const double w = 2 * pi * prewarpHz;
            K = w / std::tan (w / (2.0 * fs));
        }
        // riduzione dell'ordine se i coefficienti piu' alti sono nulli
        order = 3;
        while (order > 1 && std::abs (as[order]) < 1e-30 && std::abs (bs[order]) < 1e-30)
            --order;
        // (1+z^-1)^(N-k) (1-z^-1)^k espansi
        double B[4] {}, A[4] {};
        for (int k = 0; k <= order; ++k)
        {
            double poly[4] { 1, 0, 0, 0 };
            int deg = 0;
            auto mul = [&] (double c1)        // moltiplica per (1 + c1 z^-1)
            {
                for (int i = deg + 1; i > 0; --i) poly[i] += c1 * poly[i - 1];
                ++deg;
            };
            for (int i = 0; i < order - k; ++i) mul (1.0);
            for (int i = 0; i < k; ++i) mul (-1.0);
            const double kk = std::pow (K, k);
            for (int i = 0; i <= order; ++i)
            {
                B[i] += bs[k] * kk * poly[i];
                A[i] += as[k] * kk * poly[i];
            }
        }
        const double a0 = A[0] != 0 ? A[0] : 1.0;
        for (int i = 0; i <= order; ++i) { bz[i] = B[i] / a0; az[i] = A[i] / a0; }
    }

    DiodeModel DiodeModel::byName (const std::string& n)
    {
        // parametri tipici (Is, n) dai modelli SPICE pubblicati
        if (n == "ge")  return { 2.0e-7, 1.30 };      // 1N34A / OA90
        if (n == "led") return { 2.3e-18, 1.90 };     // LED rosso ~1.7 V
        if (n == "sch") return { 3.0e-6, 1.05 };      // Schottky BAT41/1N5817
        if (n == "mos") return { 1.0e-12, 1.30 };     // giunzione MOSFET usata come diodo
        if (n == "si2") return { 4.35e-9, 1.906 };    // 1N4148 (modello Fairchild)
        return { 2.52e-9, 1.752 };                     // 1N914 / 1S2473 / 1SS133
    }

    double Stage::arg (const char* name, const float* p, const ModelDef& def, double fallback) const
    {
        for (auto& a : args)
            if (a.first == name)
                return a.second.eval (p, def);
        return fallback;
    }

    bool Stage::has (const char* name) const
    {
        for (auto& a : args) if (a.first == name) return true;
        return false;
    }

    //==============================================================================
    // Stadi
    //==============================================================================
    namespace
    {
        /** Stadio lineare generico descritto da H(s) calcolata dai componenti. */
        class LinearStage : public Stage
        {
        public:
            using Designer = std::function<void (const LinearStage&, const float*, const ModelDef&, AnalogTF&, double&)>;
            explicit LinearStage (Designer d, bool inv = false) : designer (std::move (d)), invert (inv) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                double prewarp = 0;
                designer (*this, p, def, tf, prewarp);
                tf.design (fs, prewarp);
                vpos = arg ("rail", p, def, 0.0);
                vneg = arg ("railn", p, def, vpos);
            }
            void reset() override { tf.reset(); }
            void process (int ch, double* x, int n) override
            {
                for (int i = 0; i < n; ++i)
                {
                    double y = tf.tick (ch, x[i]);
                    if (invert) y = -y;
                    if (vpos > 0) y = railClip (y, vpos, vneg);
                    x[i] = y;
                }
            }
            AnalogTF tf;
            Designer designer;
            bool invert;
            double vpos = 0, vneg = 0;
        };

        void setTF (AnalogTF& tf, std::initializer_list<double> b, std::initializer_list<double> a)
        {
            std::fill (std::begin (tf.bs), std::end (tf.bs), 0.0);
            std::fill (std::begin (tf.as), std::end (tf.as), 0.0);
            int i = 0; for (double v : b) tf.bs[i++] = v;
            i = 0;     for (double v : a) tf.as[i++] = v;
        }

        /** Op-amp non invertente con diodi nella retroazione (Tube Screamer, OD-1, SD-1...). */
        class FeedbackClipper : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f; T = 1.0 / fs;
                rg = arg ("Rg", p, def, 4.7e3);
                cg = arg ("Cg", p, def, 0.0);
                rf = arg ("Rf", p, def, 51e3);
                cf = arg ("Cf", p, def, 0.0);
                vpos = arg ("rail", p, def, 4.0);
                vneg = arg ("railn", p, def, vpos);
                diodes = diodesFromArgs (*this, p, def, diodeName);
                gcf = cf > 0 ? 2.0 * cf / T : 0.0;
            }
            void reset() override { for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double vin = x[i];
                    // corrente nel ramo verso massa (Rg + Cg in serie)
                    double ig;
                    if (cg > 0)
                    {
                        const double k = T / (2.0 * cg);
                        const double vc = (s.vc + k * (s.ig + vin / rg)) / (1.0 + k / rg);
                        ig = (vin - vc) / rg;
                        s.vc = vc;
                    }
                    else ig = vin / rg;
                    s.ig = ig;
                    // Newton su x = vout - vin:  x/Rf + Gc*x - Ih + Id(x) = ig
                    double v = s.x;
                    for (int it = 0; it < 12; ++it)
                    {
                        double id, did;
                        diodes.eval (v, id, did);
                        const double F = v / rf + gcf * v - s.ih + id - ig;
                        const double dF = 1.0 / rf + gcf + did;
                        double step = F / dF;
                        step = std::clamp (step, -0.5, 0.5);
                        v -= step;
                        if (std::abs (step) < 1e-10) break;
                    }
                    if (gcf > 0)
                    {
                        const double ic = gcf * v - s.ih;
                        s.ih = gcf * v + ic;
                    }
                    s.x = v;
                    x[i] = railClip (vin + v, vpos, vneg);
                }
            }
            std::string diodeName = "si";
        private:
            struct State { double vc = 0, ig = 0, x = 0, ih = 0; } st[2];
            double T = 0, rg = 0, cg = 0, rf = 0, cf = 0, gcf = 0, vpos = 4, vneg = 4;
            DiodePair diodes;
        };

        /** Diodi verso massa dopo una resistenza, con condensatore in parallelo (DS-1, RAT...). */
        class ShuntClipper : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f; T = 1.0 / fs;
                r = arg ("R", p, def, 2.2e3);
                c = arg ("C", p, def, 0.0);
                bias = arg ("bias", p, def, 0.0);
                diodes = diodesFromArgs (*this, p, def, diodeName);
                gc = c > 0 ? 2.0 * c / T : 0.0;
            }
            void reset() override { for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double vin = x[i] + bias;
                    double v = s.v;
                    for (int it = 0; it < 12; ++it)
                    {
                        double id, did;
                        diodes.eval (v, id, did);
                        const double F = gc * v - s.ih - (vin - v) / r + id;
                        const double dF = gc + 1.0 / r + did;
                        double step = std::clamp (F / dF, -0.5, 0.5);
                        v -= step;
                        if (std::abs (step) < 1e-10) break;
                    }
                    if (gc > 0)
                    {
                        const double icap = gc * v - s.ih;
                        s.ih = gc * v + icap;
                    }
                    s.v = v;
                    x[i] = v;
                }
            }
            std::string diodeName = "si";
        private:
            struct State { double v = 0, ih = 0; } st[2];
            double T = 0, r = 2.2e3, c = 0, gc = 0, bias = 0;
            DiodePair diodes;
        };

        /** Stadio a transistor ad emettitore comune: guadagno + saturazione asimmetrica. */
        class TransistorStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                gain = arg ("g", p, def, 10.0);
                vp = arg ("vp", p, def, 4.0);
                vn = arg ("vn", p, def, 4.0);
                inv = arg ("inv", p, def, 1.0) > 0.5;
                soft = arg ("soft", p, def, 2.0);
            }
            void reset() override {}
            void process (int, double* x, int n) override
            {
                for (int i = 0; i < n; ++i)
                {
                    double y = gain * x[i];
                    // curva esponenziale verso il binario (ginocchio morbido tipo giunzione)
                    const double lim = y >= 0 ? vp : vn;
                    const double a = std::abs (y) / lim;
                    y = std::copysign (lim * (a / std::pow (1.0 + std::pow (a, soft), 1.0 / soft)), y);
                    x[i] = inv ? -y : y;
                }
            }
        private:
            double gain = 10, vp = 4, vn = 4, soft = 2;
            bool inv = true;
        };

        /** Guadagno / volume: y = a*x (a espressione, es. taper(0,A)*2). */
        class GainStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                const double db = arg ("db", p, def, 0.0);
                a = arg ("a", p, def, 1.0) * std::pow (10.0, db / 20.0);
            }
            void reset() override { for (auto& v : cur) v = a; }
            void process (int ch, double* x, int n) override
            {
                // rampa lineare verso il nuovo guadagno (niente click)
                double g = cur[ch];
                const double step = (a - g) / std::max (1, n);
                for (int i = 0; i < n; ++i) { g += step; x[i] *= g; }
                cur[ch] = a;
            }
        private:
            double a = 1, cur[2] { 1, 1 };
        };

        /** Salva un punto del segnale (per miscelazioni clean/drive). */
        class TapStage : public Stage
        {
        public:
            explicit TapStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float*, const ModelDef&) override { fs = f; }
            void reset() override {}
            void process (int ch, double* x, int n) override
            {
                auto& b = store[ch];
                b.assign (x, x + n);
            }
            std::vector<double>* store;
        };

        /** Miscela il segnale corrente con quello salvato da 'tap'. */
        class MixStage : public Stage
        {
        public:
            explicit MixStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                wet = arg ("wet", p, def, 1.0);
                dry = arg ("dry", p, def, 1.0 - wet);
            }
            void reset() override {}
            void process (int ch, double* x, int n) override
            {
                auto& b = store[ch];
                for (int i = 0; i < n && i < (int) b.size(); ++i)
                    x[i] = wet * x[i] + dry * b[(size_t) i];
            }
            std::vector<double>* store;
            double wet = 1, dry = 0;
        };


        /** Stadio a triodo 12AX7 a catodo comune (modello di Koren), tabulato.
            Argomenti: Rp (carico anodico), Rk (catodo), B (alimentazione), byp=1 catodo bypassato,
            mu ex kg1 kp kvb (parametri del modello; default 12AX7). Uscita invertente in volt. */
        class TriodeStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                const double rp = arg ("Rp", p, def, 100e3), rk = arg ("Rk", p, def, 1.5e3), b = arg ("B", p, def, 250.0);
                const bool byp = arg ("byp", p, def, 1.0) > 0.5;
                const double mu = arg ("mu", p, def, 100.0), ex = arg ("ex", p, def, 1.4), kg1 = arg ("kg1", p, def, 1060.0),
                             kp = arg ("kp", p, def, 600.0), kvb = arg ("kvb", p, def, 300.0);
                const double key = rp + rk * 1.37 + b + (byp ? 1 : 0) + mu;
                if (key == lastKey) return;
                lastKey = key;
                auto ip = [&] (double vgk, double vpk)
                {
                    if (vpk <= 0) return 0.0;
                    const double e1 = vpk / kp * std::log1p (std::exp (std::min (kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk)), 80.0)));
                    return e1 > 0 ? std::pow (e1, ex) / kg1 : 0.0;
                };
                // risolve la corrente anodica per una tensione di griglia (Newton su Ip)
                auto solve = [&] (double vg, double vk0, double guess)
                {
                    double i = guess;
                    for (int it = 0; it < 40; ++it)
                    {
                        const double vk = byp ? vk0 : i * rk;
                        const double vgk = vg - vk, vpk = b - i * rp - vk;
                        const double f0 = ip (vgk, vpk) - i;
                        const double h = 1e-9;
                        const double vk2 = byp ? vk0 : (i + h) * rk;
                        const double f1 = ip (vg - vk2, b - (i + h) * rp - vk2) - (i + h);
                        const double d = (f1 - f0) / h;
                        if (std::abs (d) < 1e-15) break;
                        const double step = f0 / d;
                        i = std::clamp (i - step, 0.0, b / rp);
                        if (std::abs (step) < 1e-12) break;
                    }
                    return i;
                };
                // punto di lavoro (griglia a 0 V, catodo autopolarizzato)
                double iq = 1e-3;
                for (int it = 0; it < 60; ++it) iq = 0.5 * iq + 0.5 * solve (0.0, iq * rk, iq);
                const double vkq = iq * rk;
                const double vpq = b - iq * rp;
                for (int k = 0; k < N; ++k)
                {
                    double vin = lo + (hi - lo) * k / (N - 1);
                    // conduzione di griglia: oltre il potenziale del catodo la griglia assorbe corrente
                    // (resistenza di griglia) e la tensione effettiva si "appiattisce"
                    if (vin > vkq) vin = vkq + 0.5 * std::tanh ((vin - vkq) / 0.5);
                    const double i = solve (vin, vkq, iq);
                    table[k] = (b - i * rp) - vpq;     // variazione della tensione anodica (invertente)
                }
            }
            void reset() override {}
            void process (int, double* x, int n) override
            {
                for (int i = 0; i < n; ++i)
                {
                    const double pos = (std::clamp (x[i], lo, hi) - lo) / (hi - lo) * (N - 1);
                    const int k = std::min ((int) pos, N - 2);
                    const double f = pos - k;
                    x[i] = table[k] + f * (table[k + 1] - table[k]);
                }
            }
        private:
            static constexpr int N = 4096;
            static constexpr double lo = -12.0, hi = 12.0;
            double table[N] {};
            double lastKey = -1;
        };

        /** Stadio finale push-pull con "sag" dell'alimentazione a raddrizzatore. */
        class PowerStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                v = arg ("V", p, def, 1.0);
                g = arg ("g", p, def, 1.0);
                sag = arg ("sag", p, def, 0.2);
                rel = std::exp (-1.0 / (0.08 * fs));
                atk = std::exp (-1.0 / (0.004 * fs));
            }
            void reset() override { env[0] = env[1] = 0; }
            void process (int ch, double* x, int n) override
            {
                double e = env[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double a = std::abs (x[i] * g);
                    e = a > e ? a + atk * (e - a) : a + rel * (e - a);
                    const double vv = v * (1.0 - sag * std::min (1.0, e / (v * 2.0)));
                    x[i] = vv * std::tanh (x[i] * g / vv);
                }
                env[ch] = e;
            }
        private:
            double v = 1, g = 1, sag = 0.2, rel = 0, atk = 0, env[2] {};
        };


        /** Diodi antiparalleli IN SERIE al segnale con carico RL (zona morta, es. HM-2 al germanio). */
        class SeriesDiodeStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                rl = arg ("RL", p, def, 10e3);
                diodes = diodesFromArgs (*this, p, def, diodeName);
            }
            void reset() override { for (auto& v : last) v = 0; }
            void process (int ch, double* x, int n) override
            {
                double v = last[ch];
                for (int i = 0; i < n; ++i)
                {
                    // vout = RL * Id(vin - vout)  ->  F(v) = v - RL*Id(vin - v) = 0
                    const double vin = x[i];
                    for (int it = 0; it < 16; ++it)
                    {
                        double id, did;
                        diodes.eval (vin - v, id, did);
                        const double F = v - rl * id, dF = 1.0 + rl * did;
                        const double step = std::clamp (F / dF, -0.5, 0.5);
                        v -= step;
                        if (std::abs (step) < 1e-11) break;
                    }
                    x[i] = v;
                }
                last[ch] = v;
            }
            std::string diodeName = "ge";
        private:
            double rl = 10e3, last[2] {};
            DiodePair diodes;
        };

        /** Raddrizzatore a doppia semionda (coppia differenziale del Superfuzz): genera l'ottava. */
        class RectifierStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                mix = arg ("mix", p, def, 1.0);      // 0 = diretto, 1 = solo ottava
                g = arg ("g", p, def, 1.0);
                soft = arg ("vk", p, def, 0.05);     // ginocchio della giunzione
                dcCoef = std::exp (-2.0 * 3.14159265358979 * 8.0 / fs);
            }
            void reset() override { for (auto& d : dc) d = 0; }
            void process (int ch, double* x, int n) override
            {
                for (int i = 0; i < n; ++i)
                {
                    const double v = x[i] * g;
                    const double r = std::sqrt (v * v + soft * soft) - soft;   // |v| con ginocchio morbido
                    dc[ch] = r + dcCoef * (dc[ch] - r);
                    x[i] = (1.0 - mix) * v + mix * (r - dc[ch]) * 2.0;
                }
            }
        private:
            double mix = 1, g = 1, soft = 0.05, dcCoef = 0.999, dc[2] {};
        };

        std::unique_ptr<Stage> makeStage (const std::string& type, std::vector<double>* tapBuf, std::string& err)
        {
            using LS = LinearStage;
            auto val = [] (const LS& s, const char* k, const float* p, const ModelDef& d, double fb) { return s.arg (k, p, d, fb); };

            if (type == "hpf")
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double rc = val (s, "R", p, d, 1e6) * val (s, "C", p, d, 47e-9);
                    setTF (tf, { 0, rc }, { 1, rc });
                });
            if (type == "lpf")
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double rc = val (s, "R", p, d, 10e3) * val (s, "C", p, d, 10e-9);
                    setTF (tf, { 1 }, { 1, rc });
                });
            if (type == "lpf2" || type == "hpf2" || type == "peak" || type == "lshelf" || type == "hshelf" || type == "bpf")
                return std::make_unique<LS> ([val, type] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double& pw)
                {
                    const double f0 = val (s, "f", p, d, 1000.0), q = val (s, "Q", p, d, 0.707);
                    const double w = 2 * pi * f0, A = std::pow (10.0, val (s, "g", p, d, 0.0) / 40.0);
                    pw = f0;
                    if (type == "lpf2")        setTF (tf, { w * w }, { w * w, w / q, 1 });
                    else if (type == "hpf2")   setTF (tf, { 0, 0, 1 }, { w * w, w / q, 1 });
                    else if (type == "bpf")    setTF (tf, { 0, w / q }, { w * w, w / q, 1 });
                    else if (type == "peak")   setTF (tf, { w * w, A * w / q, 1 }, { w * w, w / (A * q), 1 });
                    else if (type == "lshelf")   // prototipi analogici RBJ (guadagno in DC = g dB)
                        setTF (tf, { A * A * w * w, A * std::sqrt (A) * w / q, A }, { w * w, std::sqrt (A) * w / q, A });
                    else                         // hshelf: guadagno alle alte = g dB
                        setTF (tf, { A * w * w, A * std::sqrt (A) * w / q, A * A }, { A * w * w, std::sqrt (A) * w / q, 1 });
                });
            if (type == "opni")
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double rg = val (s, "Rg", p, d, 10e3), cg = val (s, "Cg", p, d, 0.0);
                    const double rf = val (s, "Rf", p, d, 100e3), cf = val (s, "Cf", p, d, 0.0);
                    if (cg > 0)
                        setTF (tf, { 1, rf * cf + rg * cg + rf * cg, rf * cf * rg * cg },
                                   { 1, rf * cf + rg * cg, rf * cf * rg * cg });
                    else
                        setTF (tf, { rg + rf, rg * rf * cf }, { rg, rg * rf * cf });
                });
            if (type == "opinv")
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double ri = val (s, "Ri", p, d, 10e3), ci = val (s, "Ci", p, d, 0.0);
                    const double rf = val (s, "Rf", p, d, 100e3), cf = val (s, "Cf", p, d, 0.0);
                    if (ci > 0) setTF (tf, { 0, rf * ci }, { 1, rf * cf + ri * ci, rf * cf * ri * ci });
                    else        setTF (tf, { rf }, { ri, ri * rf * cf });
                }, true);
            if (type == "tonebm")   // tono tipo Big Muff / DS-1: miscela passa-basso e passa-alto
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double a = val (s, "R1", p, d, 6.8e3) * val (s, "C1", p, d, 22e-9);
                    const double b = val (s, "R2", p, d, 6.8e3) * val (s, "C2", p, d, 10e-9);
                    const double t = std::clamp (val (s, "t", p, d, 0.5), 0.0, 1.0);
                    const double k = val (s, "k", p, d, 1.0);        // perdita d'inserzione
                    setTF (tf, { k * (1 - t), k * b, k * t * a * b }, { 1, a + b, a * b });
                });
            if (type == "fmv")      // stack Fender/Marshall/Vox (Yeh & Smith, DAFx 2006)
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double R1 = val (s, "R1", p, d, 250e3), R2 = val (s, "R2", p, d, 1e6), R3 = val (s, "R3", p, d, 25e3),
                                 R4 = val (s, "R4", p, d, 56e3), C1 = val (s, "C1", p, d, 250e-12), C2 = val (s, "C2", p, d, 20e-9),
                                 C3 = val (s, "C3", p, d, 20e-9);
                    const double t = std::clamp (val (s, "t", p, d, 0.5), 0.0, 1.0);
                    const double m = std::clamp (val (s, "m", p, d, 0.5), 0.0, 1.0);
                    const double l = std::clamp (val (s, "l", p, d, 0.5), 0.0, 1.0);
                    const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
                    const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                                    + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                                    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                                    + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
                    const double C123 = C1 * C2 * C3;
                    const double b3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4) - m * m * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                                    + m * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4) + t * C123 * R1 * R3 * R4
                                    - t * m * C123 * R1 * R3 * R4 + t * l * C123 * R1 * R2 * R4;
                    const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
                    const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                                    + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                                    + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3
                                       + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
                    const double a3 = l * m * (C123 * R1 * R2 * R3 + C123 * R2 * R3 * R4) - m * m * (C123 * R1 * R3 * R3 + C123 * R3 * R3 * R4)
                                    + m * (C123 * R3 * R3 * R4 + C123 * R1 * R3 * R3 - C123 * R1 * R3 * R4) + l * C123 * R1 * R2 * R4
                                    + C123 * R1 * R3 * R4;
                    setTF (tf, { 0, b1, b2, b3 }, { 1, a1, a2, a3 });
                });
            if (type == "tonets")   // tono attivo Tube Screamer / SD-1 (analisi nodale, 3° ordine)
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    // Vin -R7- X (C4 a massa, ingresso + dell'op-amp); X -a*P- W -(1-a)*P- V- ;
                    // W -(Rw + Cw)- massa ; uscita -(Rf || Cf)- V-
                    const double R7 = val (s, "R7", p, d, 1e3), C4 = val (s, "C4", p, d, 220e-9), P = val (s, "P", p, d, 20e3);
                    const double a = std::clamp (val (s, "t", p, d, 0.5), 0.002, 0.998);
                    const double Rw = val (s, "Rw", p, d, 220.0), Cw = val (s, "Cw", p, d, 220e-9);
                    const double Rf = val (s, "Rf", p, d, 1e3), Cf = val (s, "Cf", p, d, 0.0);
                    const double Ga = 1.0 / (a * P), Gb = 1.0 / ((1.0 - a) * P), G = Ga + Gb;
                    const double q0 = G, q1 = Cw * (G * Rw + 1.0), k = Rf * Cf, g7 = 1.0 / R7;
                    const double n0 = g7 * q0, n1 = g7 * (q1 + k * q0 + Rf * Gb * Cw), n2 = g7 * k * q1;
                    const double d0 = g7 * q0, d1 = g7 * q1 + C4 * q0 + Ga * Cw, d2 = C4 * q1;
                    setTF (tf, { n0, n1, n2 }, { d0, d1 + k * d0, d2 + k * d1, k * d2 });
                });
            if (type == "shelf1")   // R || Cs in serie, Cp verso massa: mensola passa-basso
                return std::make_unique<LS> ([val] (const LS& s, const float* p, const ModelDef& d, AnalogTF& tf, double&)
                {
                    const double r = val (s, "R", p, d, 5.6e3), cs = val (s, "Cs", p, d, 6.8e-9), cp = val (s, "Cp", p, d, 5.6e-9);
                    setTF (tf, { 1, r * cs }, { 1, r * (cs + cp) });
                });
            if (type == "fbclip") { auto s = std::make_unique<FeedbackClipper>(); return s; }
            if (type == "dclip")  { auto s = std::make_unique<ShuntClipper>(); return s; }
            if (type == "bjt")    return std::make_unique<TransistorStage>();
            if (type == "triode") return std::make_unique<TriodeStage>();
            if (type == "series") return std::make_unique<SeriesDiodeStage>();
            if (type == "rect")   return std::make_unique<RectifierStage>();
            if (type == "power")  return std::make_unique<PowerStage>();
            if (type == "gain" || type == "vol") return std::make_unique<GainStage>();
            if (type == "tap")    return std::make_unique<TapStage> (tapBuf);
            if (type == "mix")    return std::make_unique<MixStage> (tapBuf);
            if (type == "rail")
                return std::make_unique<LS> ([] (const LS&, const float*, const ModelDef&, AnalogTF& tf, double&)
                { setTF (tf, { 1 }, { 1 }); });
            err += "stadio sconosciuto '" + type + "'; ";
            return nullptr;
        }
    }

    //==============================================================================
    // CircuitEffect
    //==============================================================================
    CircuitEffect::CircuitEffect (const ModelDef& d) : Effect (d)
    {
        static_assert (maxControls >= 8);
        tapStore = std::make_unique<std::vector<double>[]> (2);
        auto* taps = tapStore.get();

        for (const auto& stmtRaw : split (d.config != nullptr ? d.config : "", ';'))
        {
            const auto stmt = trim (stmtRaw);
            if (stmt.empty()) continue;
            std::istringstream is (stmt);
            std::string type;
            is >> type;
            auto st = makeStage (type, taps, error);
            if (st == nullptr) continue;
            std::string tok, rest;
            std::getline (is, rest);
            // argomenti key=value separati da spazi (le espressioni non contengono spazi)
            std::istringstream as (rest);
            while (as >> tok)
            {
                const auto eq = tok.find ('=');
                if (eq == std::string::npos) { error += "argomento senza '=': " + tok + "; "; continue; }
                const auto key = tok.substr (0, eq), value = tok.substr (eq + 1);
                if (key == "d")
                {
                    if (auto* fb = dynamic_cast<FeedbackClipper*> (st.get())) fb->diodeName = value;
                    if (auto* sh = dynamic_cast<ShuntClipper*> (st.get())) sh->diodeName = value;
                    if (auto* se = dynamic_cast<SeriesDiodeStage*> (st.get())) se->diodeName = value;
                    continue;
                }
                st->args.emplace_back (key, Expr::parse (value, error));
            }
            stages.push_back (std::move (st));
        }
        for (int i = 0; i < maxControls; ++i)
            smoothed[i] = params[i].load();
    }

    void CircuitEffect::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        maxN = maxBlock;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlock);
        osr = sr * 4.0;
        work.assign ((size_t) maxBlock * 4 + 16, 0.0);
        for (int c = 0; c < 2; ++c) tapStore[c].reserve ((size_t) maxBlock * 4 + 16);
        first = true;
        updateStages();
        reset();
    }

    void CircuitEffect::reset()
    {
        for (auto& s : stages) s->reset();
        if (os) os->reset();
    }

    void CircuitEffect::updateStages()
    {
        for (int i = 0; i < maxControls; ++i)
        {
            const float target = params[i].load (std::memory_order_relaxed);
            smoothed[i] = first ? target : smoothed[i] + 0.35f * (target - smoothed[i]);
        }
        first = false;
        for (auto& s : stages) s->update (osr, smoothed, def);
    }

    void CircuitEffect::process (float* const* ch, int numCh, int n)
    {
        if (os == nullptr || n <= 0) return;
        bool changed = false;
        for (int i = 0; i < def.numControls; ++i)
            if (std::abs (params[i].load (std::memory_order_relaxed) - smoothed[i]) > 1e-5f) changed = true;
        if (changed) updateStages();

        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int N = (int) up.getNumSamples();
        for (int c = 0; c < numCh; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            for (int i = 0; i < N; ++i) work[(size_t) i] = d[i];
            for (auto& s : stages) s->process (c, work.data(), N);
            for (int i = 0; i < N; ++i) d[i] = (float) work[(size_t) i];
        }
        os->processSamplesDown (block);
    }
}
