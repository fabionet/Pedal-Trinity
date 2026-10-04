/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)
*/

#include "Circuit.h"
#include <sstream>
#include <map>
#include <complex>

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
                    if (fn == "par" || fn == "inv" || fn == "div")
                    {
                        // argomenti = espressioni complete (resistenze in parallelo, reciproci, rapporti)
                        fac.kind = fn == "par" ? Factor::Par : fn == "inv" ? Factor::Inv : Factor::Div;
                        for (const auto& a : parts) fac.sub.push_back (parse (a, error));
                        if ((fac.kind == Factor::Inv && fac.sub.size() != 1) || (fac.kind == Factor::Div && fac.sub.size() != 2))
                            error += "numero di argomenti errato in '" + fn + "'; ";
                        term.push_back (fac);
                        continue;
                    }
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
        for (auto& t : terms)
            for (auto& f : t)
            {
                if (f.kind == Factor::Par || f.kind == Factor::Inv || f.kind == Factor::Div)
                {
                    for (auto& e : f.sub) if (! e.isConst()) return false;
                }
                else if (f.kind != Factor::Const) return false;
            }
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
                    case Factor::Par:
                    {
                        double g = 0;
                        for (auto& e : f.sub) g += 1.0 / std::max (1e-30, std::abs (e.eval (p, def)));
                        prod *= g > 0 ? 1.0 / g : 0.0;
                        break;
                    }
                    case Factor::Inv:
                    {
                        const double v = f.sub.empty() ? 1.0 : f.sub[0].eval (p, def);
                        prod *= 1.0 / (std::abs (v) < 1e-30 ? std::copysign (1e-30, v) : v);
                        break;
                    }
                    case Factor::Div:
                    {
                        const double a = f.sub.size() > 0 ? f.sub[0].eval (p, def) : 0.0;
                        const double b = f.sub.size() > 1 ? f.sub[1].eval (p, def) : 1.0;
                        prod *= a / (std::abs (b) < 1e-30 ? std::copysign (1e-30, b) : b);
                        break;
                    }
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

        /** Indice della memoria "tap" (argomento n=0..maxTaps-1, default 0) -> primo dei 2 canali. */
        int tapSlot (const Stage& s, const float* p, const ModelDef& def)
        {
            const int n = (int) std::lround (s.arg ("n", p, def, 0.0));
            return 2 * std::clamp (n, 0, CircuitEffect::maxTaps - 1);
        }

        /** Salva un punto del segnale (per miscelazioni clean/drive e rami paralleli). */
        class TapStage : public Stage
        {
        public:
            explicit TapStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override { fs = f; slot = tapSlot (*this, p, def); }
            void reset() override {}
            void process (int ch, double* x, int n) override
            {
                auto& b = store[slot + ch];
                b.assign (x, x + n);
            }
            std::vector<double>* store;
            int slot = 0;
        };

        /** Miscela il segnale corrente con quello salvato da 'tap': y = wet*x + dry*tap. */
        class MixStage : public Stage
        {
        public:
            explicit MixStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                wet = arg ("wet", p, def, 1.0);
                dry = arg ("dry", p, def, 1.0 - wet);
                slot = tapSlot (*this, p, def);
            }
            void reset() override {}
            void process (int ch, double* x, int n) override
            {
                auto& b = store[slot + ch];
                for (int i = 0; i < n && i < (int) b.size(); ++i)
                    x[i] = wet * x[i] + dry * b[(size_t) i];
            }
            std::vector<double>* store;
            double wet = 1, dry = 0;
            int slot = 0;
        };

        /** Riprende il segnale salvato da 'tap n=k' (inizio di un ramo parallelo): y = a*tap. */
        class RecallStage : public Stage
        {
        public:
            explicit RecallStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                a = arg ("a", p, def, 1.0);
                slot = tapSlot (*this, p, def);
            }
            void reset() override {}
            void process (int ch, double* x, int n) override
            {
                auto& b = store[slot + ch];
                for (int i = 0; i < n; ++i)
                    x[i] = i < (int) b.size() ? a * b[(size_t) i] : 0.0;
            }
            std::vector<double>* store;
            double a = 1;
            int slot = 0;
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


        //==============================================================================
        // Stadi aggiunti nella tappa 3 (pedali analogici Electro-Harmonix / MXR)
        //==============================================================================

        /** Saturazione morbida asimmetrica verso i binari: y = x / (1 + |x/lim|^k)^(1/k); dy = derivata. */
        inline double softSat (double x, double vp, double vn, double k, int kInt, double& dy) noexcept
        {
            if (vp <= 0) { dy = 1.0; return x; }
            const double lim = x >= 0 ? vp : vn;
            const double a = std::abs (x) / lim;
            double ak, den;
            switch (kInt)       // esponenti interi frequenti senza pow()
            {
                case 2:  ak = a * a; den = std::sqrt (1.0 + ak); break;
                case 4:  { const double a2 = a * a; ak = a2 * a2; den = std::sqrt (std::sqrt (1.0 + ak)); break; }
                case 8:  { const double a2 = a * a, a4 = a2 * a2; ak = a4 * a4; den = std::sqrt (std::sqrt (std::sqrt (1.0 + ak))); break; }
                default: ak = std::pow (a, k); den = std::pow (1.0 + ak, 1.0 / k); break;
            }
            dy = 1.0 / (den * (1.0 + ak));
            return x / den;
        }

        /** Esponente del ginocchio come intero 2/4/8 se lo e' (0 = generico). */
        inline int kneeInt (double k) noexcept
        {
            for (int v : { 2, 4, 8 }) if (std::abs (k - v) < 1e-9) return v;
            return 0;
        }

        inline bool isZero (double v) noexcept { return std::fpclassify (v) == FP_ZERO; }

        /** Stadio invertente (transistor a emettitore comune con retroazione collettore-base, op-amp invertente,
            inverter CMOS) con guadagno ad anello aperto finito e saturazione morbida dentro l'anello:
            Ri in ingresso, Rf || Cf in retroazione, diodi (con Cd in serie, opzionale) in parallelo alla
            retroazione (Big Muff Pi, log amp). Risolto campione per campione (Newton, trapezi).
            Argomenti: Ri Ci (condensatore d'ingresso in serie a Ri, 0 = assente) Rf Cf Rb (resistenza verso massa del
            nodo d'ingresso: polarizzazione + base) A (guadagno ad
            anello aperto; con Ahf e fp diventa A(s) = Ahf + (A - Ahf)/(1 + s/2 pi fp)) d up dn Cd rail railn soft. */
        class InvClipper : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f; T = 1.0 / fs;
                ri = std::max (1.0, arg ("Ri", p, def, 10e3));
                rf = std::max (1.0, arg ("Rf", p, def, 100e3));
                cf = arg ("Cf", p, def, 0.0);
                cd = arg ("Cd", p, def, 0.0);
                const double rb = arg ("Rb", p, def, 0.0);
                ci = arg ("Ci", p, def, 0.0);
                gci = ci > 0 ? 2.0 * ci / T : 0.0;
                rin = ri + (gci > 0 ? 1.0 / gci : 0.0);
                gb = rb > 0 ? 1.0 / rb : 0.0;
                A = std::max (1.0, arg ("A", p, def, 1e5));
                Ahf = std::max (1.0, arg ("Ahf", p, def, A));
                const double fp = arg ("fp", p, def, 0.0);
                Gp = fp > 0 && std::abs (Ahf - A) > 1e-12 ? std::tan (pi * std::min (fp, 0.4 * fs) / fs) : 0.0;
                Gp = Gp / (1.0 + Gp);
                vp = arg ("rail", p, def, 0.0);
                vn = arg ("railn", p, def, vp);
                soft = std::max (1.0, arg ("soft", p, def, 4.0));
                softInt = kneeInt (soft);
                hasD = ! diodeName.empty() && diodeName != "none";
                if (hasD) diodes = diodesFromArgs (*this, p, def, diodeName);
                gcf = cf > 0 ? 2.0 * cf / T : 0.0;
                gcd = cd > 0 ? 2.0 * cd / T : 0.0;
            }
            void reset() override { for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                const double gr = 1.0 / rf + gcf;
                for (int i = 0; i < n; ++i)
                {
                    const double vin = x[i];
                    // guadagno ad anello aperto A(s) = Ahf + (A - Ahf) / (1 + s/wp): carico del collettore che cambia
                    // con la frequenza (ingresso dello stadio successivo, tone stack)
                    const double Aeff = Gp > 0 ? Ahf + (A - Ahf) * Gp : A;
                    const double off = Gp > 0 ? (A - Ahf) * (1.0 - Gp) * s.zp : 0.0;
                    // incognita: y = uscita dell'amplificatore prima della saturazione (scala dei volt, ben condizionata
                    // anche con A molto grande); vm = -(y + off)/A. Stima iniziale estrapolata dai campioni precedenti.
                    double y = 2.0 * s.y - s.y1, vd = 2.0 * s.vd - s.vd1, vm = 0, vo = 0, w = 0;
                    const double ia = 1.0 / Aeff;
                    for (int it = 0; it < 40; ++it)
                    {
                        double gp;
                        vo = softSat (y, vp, vn, soft, softInt, gp);
                        vm = -(y + off) * ia;
                        w = vm - vo;
                        const double dw = -ia - gp;              // dw/dy
                        double F1 = (vm - vin + (gci > 0 ? s.hci / gci : 0.0)) / rin + vm * gb + w * gr - s.hcf;
                        double J11 = -(1.0 / rin + gb) * ia + dw * gr;
                        double dy, dvd = 0;
                        if (hasD && gcd > 0)
                        {
                            double id, did;
                            diodes.eval (vd, id, did);
                            F1 += id;
                            const double F2 = w - vd - (id + s.hcd) / gcd;
                            const double J12 = did, J21 = dw, J22 = -1.0 - did / gcd;
                            const double det = J11 * J22 - J12 * J21;
                            dy = (F1 * J22 - J12 * F2) / det;
                            dvd = (J11 * F2 - J21 * F1) / det;
                        }
                        else
                        {
                            if (hasD)
                            {
                                double id, did;
                                diodes.eval (w, id, did);
                                F1 += id; J11 += did * dw;
                            }
                            dy = F1 / J11;
                        }
                        dy = std::clamp (dy, -0.5, 0.5);
                        dvd = std::clamp (dvd, -0.3, 0.3);
                        y -= dy; vd -= dvd;
                        if (std::abs (dy) < 1e-7 && std::abs (dvd) < 1e-8) break;
                    }
                    vm = -(y + off) * ia;
                    double gp;
                    vo = softSat (y, vp, vn, soft, softInt, gp);
                    if (Gp > 0) { const double v = Gp * (vm - s.zp); s.zp = v + (v + s.zp); }
                    w = vm - vo;
                    if (gcf > 0) { const double ic = gcf * w - s.hcf; s.hcf = gcf * w + ic; }
                    if (gci > 0)
                    {
                        const double ii = (vin - vm - s.hci / gci) / rin;
                        const double vc = (ii + s.hci) / gci;
                        s.hci = gci * vc + ii;
                    }
                    if (hasD && gcd > 0)
                    {
                        double id, did;
                        diodes.eval (vd, id, did);
                        const double vc = (id + s.hcd) / gcd;
                        s.hcd = gcd * vc + id;
                    }
                    s.y1 = s.y; s.vd1 = s.vd;
                    s.y = y; s.vd = vd;
                    x[i] = vo;
                }
            }
            std::string diodeName = "none";
        private:
            struct State { double y = 0, vd = 0, y1 = 0, vd1 = 0, hcf = 0, hcd = 0, hci = 0, zp = 0; } st[2];
            double T = 0, ri = 1e4, rf = 1e5, cf = 0, cd = 0, ci = 0, gci = 0, rin = 1e4, gb = 0, A = 1e5, Ahf = 1e5, Gp = 0, vp = 0, vn = 0, soft = 4, gcf = 0, gcd = 0;
            int softInt = 4;
            bool hasD = false;
            DiodePair diodes;
        };

        /** Rete lineare generica descritta per nodi (analisi nodale modificata), convertita in H(s) fino al 3° ordine:
                nodal R.a.b=33k C.b.0=10n O.p.m.o=1 E.p.m.o=guadagno OUT.w=k
            'i' = ingresso (generatore ideale), '0' = massa; O = op-amp ideale (v+ = v-), E = generatore di tensione
            controllato (vo = k (vp - vm)), OUT.nodo = uscita moltiplicata per k. I coefficienti di H(s) si ricavano
            dal determinante della matrice MNA valutato su 8 punti del cerchio |s| = 2 pi fref (DFT inversa). */
        class NodalStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                if (! parsed) parse();
                vpos = arg ("rail", p, def, 0.0);
                vneg = arg ("railn", p, def, vpos);
                if (! ok) { setUnity(); tf.design (fs); return; }
                for (auto& e : elems) e.val = args[(size_t) e.arg].second.eval (p, def);
                const double fref = arg ("fref", p, def, 1000.0);
                const double rho = 2.0 * pi * fref;
                constexpr int M = 8;
                const int N = nodes + extra;
                std::complex<double> Dk[M], Nk[M];
                std::vector<std::complex<double>> Am ((size_t) (N * N)), z ((size_t) N);
                for (int k = 0; k < M; ++k)
                {
                    const std::complex<double> s = std::polar (rho, 2.0 * pi * k / M);
                    std::fill (Am.begin(), Am.end(), std::complex<double> (0));
                    std::fill (z.begin(), z.end(), std::complex<double> (0));
                    auto at = [&] (int r, int c) -> std::complex<double>& { return Am[(size_t) (r * N + c)]; };
                    auto stampY = [&] (int a, int b, std::complex<double> y)
                    {
                        if (a >= 0) at (a, a) += y;
                        if (b >= 0) at (b, b) += y;
                        if (a >= 0 && b >= 0) { at (a, b) -= y; at (b, a) -= y; }
                    };
                    int row = nodes;
                    // generatore d'ingresso
                    at (inNode, row) += 1.0; at (row, inNode) += 1.0; z[(size_t) row] = 1.0; ++row;
                    for (auto& e : elems)
                    {
                        if (e.type == 'R') stampY (e.a, e.b, 1.0 / std::max (1e-6, e.val));
                        else if (e.type == 'C') stampY (e.a, e.b, s * e.val);
                        else if (e.type == 'O' || e.type == 'E')
                        {
                            // corrente d'uscita incognita nel nodo o; vincolo sulla riga 'row'
                            if (e.c >= 0) at (e.c, row) += 1.0;
                            if (e.type == 'O')
                            {
                                if (e.a >= 0) at (row, e.a) += 1.0;
                                if (e.b >= 0) at (row, e.b) -= 1.0;
                            }
                            else
                            {
                                if (e.c >= 0) at (row, e.c) += 1.0;
                                if (e.a >= 0) at (row, e.a) -= e.val;
                                if (e.b >= 0) at (row, e.b) += e.val;
                            }
                            ++row;
                        }
                    }
                    // eliminazione di Gauss con pivot parziale (determinante e soluzione)
                    std::complex<double> det = 1.0;
                    for (int c = 0; c < N; ++c)
                    {
                        int piv = c;
                        for (int r = c + 1; r < N; ++r) if (std::abs (at (r, c)) > std::abs (at (piv, c))) piv = r;
                        if (std::abs (at (piv, c)) < 1e-300) { det = 0; break; }
                        if (piv != c)
                        {
                            for (int j = 0; j < N; ++j) std::swap (at (c, j), at (piv, j));
                            std::swap (z[(size_t) c], z[(size_t) piv]);
                            det = -det;
                        }
                        det *= at (c, c);
                        for (int r = c + 1; r < N; ++r)
                        {
                            const auto m = at (r, c) / at (c, c);
                            if (m == std::complex<double> (0)) continue;
                            for (int j = c; j < N; ++j) at (r, j) -= m * at (c, j);
                            z[(size_t) r] -= m * z[(size_t) c];
                        }
                    }
                    std::vector<std::complex<double>> xs ((size_t) N);
                    if (det != std::complex<double> (0))
                        for (int r = N - 1; r >= 0; --r)
                        {
                            auto acc = z[(size_t) r];
                            for (int j = r + 1; j < N; ++j) acc -= at (r, j) * xs[(size_t) j];
                            xs[(size_t) r] = acc / at (r, r);
                        }
                    Dk[k] = det;
                    Nk[k] = det * xs[(size_t) outNode] * args[(size_t) outArg].second.eval (p, def);
                }
                // coefficienti dei polinomi (DFT inversa sul cerchio)
                double dc[M], nc[M];
                for (int j = 0; j < M; ++j)
                {
                    std::complex<double> sd = 0, sn = 0;
                    for (int k = 0; k < M; ++k)
                    {
                        const auto w = std::polar (1.0, -2.0 * pi * j * k / M);
                        sd += Dk[k] * w; sn += Nk[k] * w;
                    }
                    const double sc = 1.0 / (M * std::pow (rho, j));
                    dc[j] = sd.real() * sc; nc[j] = sn.real() * sc;
                }
                // pulizia dei residui numerici e semplificazione dei fattori s comuni
                double mx = 0;
                for (int j = 0; j < M; ++j) mx = std::max ({ mx, std::abs (dc[j]) * std::pow (rho, j), std::abs (nc[j]) * std::pow (rho, j) });
                for (int j = 0; j < M; ++j)
                {
                    if (std::abs (dc[j]) * std::pow (rho, j) < 1e-11 * mx) dc[j] = 0;
                    if (std::abs (nc[j]) * std::pow (rho, j) < 1e-11 * mx) nc[j] = 0;
                }
                int shift = 0;
                while (shift < M - 1 && isZero (dc[shift]) && isZero (nc[shift])) ++shift;
                setUnity();
                bool tooHigh = false;
                for (int j = shift; j < M; ++j)
                {
                    const int d = j - shift;
                    if (d <= 3) { tf.bs[d] = nc[j]; tf.as[d] = dc[j]; }
                    else if (! isZero (dc[j]) || ! isZero (nc[j])) tooHigh = true;
                }
                if (tooHigh || isZero (tf.as[0])) setUnity();     // rete non supportata (oltre il 3° ordine o senza DC)
                else
                {
                    // normalizzazione (a0 = 1): i coefficienti diventano potenze di costanti di tempo
                    const double a0 = tf.as[0];
                    for (int j = 0; j < 4; ++j) { tf.as[j] /= a0; tf.bs[j] /= a0; }
                }
                tf.design (fs);
            }
            void reset() override { tf.reset(); }
            void process (int ch, double* x, int n) override
            {
                for (int i = 0; i < n; ++i)
                {
                    double y = tf.tick (ch, x[i]);
                    if (vpos > 0) y = railClip (y, vpos, vneg);
                    x[i] = y;
                }
            }
        private:
            struct Elem { char type; int a = -1, b = -1, c = -1; int arg = 0; double val = 0; };
            void setUnity()
            {
                std::fill (std::begin (tf.bs), std::end (tf.bs), 0.0);
                std::fill (std::begin (tf.as), std::end (tf.as), 0.0);
                tf.bs[0] = tf.as[0] = 1.0;
            }
            void parse()
            {
                parsed = true;
                std::map<std::string, int> map;
                auto node = [&] (const std::string& nm) -> int
                {
                    if (nm == "0" || nm == "gnd") return -1;
                    auto it = map.find (nm);
                    if (it != map.end()) return it->second;
                    const int id = (int) map.size();
                    map[nm] = id;
                    return id;
                };
                inNode = node ("i");
                for (size_t k = 0; k < args.size(); ++k)
                {
                    const auto parts = split (args[k].first, '.');
                    const auto& t = parts[0];
                    if (t == "OUT" && parts.size() == 2) { outNode = node (parts[1]); outArg = (int) k; continue; }
                    if ((t == "R" || t == "C") && parts.size() == 3)
                    { Elem e; e.type = t[0]; e.a = node (parts[1]); e.b = node (parts[2]); e.arg = (int) k; elems.push_back (e); continue; }
                    if ((t == "O" || t == "E") && parts.size() == 4)
                    {
                        Elem e; e.type = t[0]; e.a = node (parts[1]); e.b = node (parts[2]); e.c = node (parts[3]); e.arg = (int) k;
                        elems.push_back (e); ++extra; continue;
                    }
                }
                nodes = (int) map.size();
                extra += 1;      // generatore d'ingresso
                ok = outNode >= 0 && nodes > 0 && nodes + extra <= 24;
            }
            AnalogTF tf;
            std::vector<Elem> elems;
            bool parsed = false, ok = false;
            int nodes = 0, extra = 0, inNode = 0, outNode = -1, outArg = 0;
            double vpos = 0, vneg = 0;
        };

        /** Rivelatore d'inviluppo (semionda o onda intera) con attacco/rilascio esponenziali. */
        struct EnvFollower
        {
            double a = 0, r = 0, e[2] {};
            bool half = false;
            void set (double fs, double atkMs, double relMs)
            {
                a = std::exp (-1.0 / (std::max (0.01, atkMs) * 1e-3 * fs));
                r = std::exp (-1.0 / (std::max (0.01, relMs) * 1e-3 * fs));
            }
            inline double tick (int ch, double x) noexcept
            {
                const double v = half ? std::max (0.0, x) : std::abs (x);
                double& s = e[ch];
                s = v > s ? v + a * (s - v) : v + r * (s - v);
                return s;
            }
            void reset() { e[0] = e[1] = 0; }
        };

        /** Approssimazione di tan(x) per 0 <= x < 0.7 (coefficiente dei filtri TPT a frequenza variabile). */
        inline double tanApprox (double x) noexcept
        {
            const double x2 = x * x;
            return x * (1.0 + x2 * (1.0 / 3.0 + x2 * (2.0 / 15.0 + x2 * (17.0 / 315.0))));
        }

        /** Filtro a variabili di stato TPT (Zavalishin): uscite passa-basso, passa-banda, passa-alto. */
        struct Svf
        {
            double ic1 = 0, ic2 = 0;
            inline void tick (double x, double g, double k, double& lp, double& bp, double& hp) noexcept
            {
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const double v3 = x - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
                lp = v2; bp = v1; hp = x - k * v1 - v2;
            }
            void reset() { ic1 = ic2 = 0; }
        };

        /** Rilevatore di nuova nota (soglia con isteresi sull'inviluppo) per sweep e attacchi dei synth. */
        struct NoteTrigger
        {
            EnvFollower env;
            bool armed[2] { true, true };
            double thr = 0.02;
            void set (double fs, double t) { env.set (fs, 0.5, 25.0); thr = std::max (1e-5, t); }
            inline bool tick (int ch, double x) noexcept
            {
                const double e = env.tick (ch, x);
                if (armed[ch] && e > thr) { armed[ch] = false; return true; }
                if (! armed[ch] && e < 0.5 * thr) armed[ch] = true;
                return false;
            }
            void reset() { env.reset(); armed[0] = armed[1] = true; }
        };

        /** Filtro controllato dall'inviluppo (envelope filter / auto-wah: Doctor Q, Q-Tron, Bassballs, Tube Zipper).
            Argomenti: mode (0 LP, 1 BP, 2 HP, 3 BP + diretto, 4 notch, 5 LP + BP), flo fhi (Hz), q qhi, sens (V^-1: posizione =
            sens * inviluppo), atk rel (ms), half (1 = raddrizzatore a semionda), dir (+1 su, -1 giu'), bpn (BP:
            1 = picco unitario, 0 = picco Q), trill (profondita' 0..1) trate (Hz), src (memoria tap da cui
            ricavare l'inviluppo, -1 = ingresso), drive (V: saturazione OTA degli integratori, 0 = lineare). */
        class EnvFilterStage : public Stage
        {
        public:
            explicit EnvFilterStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                mode = (int) std::lround (arg ("mode", p, def, 1.0));
                flo = std::clamp (arg ("flo", p, def, 300.0), 10.0, 0.2 * fs);
                fhi = std::clamp (arg ("fhi", p, def, 3000.0), 10.0, 0.2 * fs);
                qlo = std::max (0.3, arg ("q", p, def, 3.0));
                qhi = std::max (0.3, arg ("qhi", p, def, qlo));
                sens = arg ("sens", p, def, 4.0);
                dir = arg ("dir", p, def, 1.0);
                bpn = arg ("bpn", p, def, 1.0);
                trill = arg ("trill", p, def, 0.0);
                trate = arg ("trate", p, def, 6.0);
                drive = arg ("drive", p, def, 0.0);
                src = (int) std::lround (arg ("src", p, def, -1.0));
                env.half = arg ("half", p, def, 0.0) > 0.5;
                env.set (fs, arg ("atk", p, def, 5.0), arg ("rel", p, def, 120.0));
                lfoInc = 2.0 * pi * trate / fs;
            }
            void reset() override { env.reset(); for (auto& s : svf) s.reset(); phase = 0; cnt[0] = cnt[1] = 0; }
            void process (int ch, double* x, int n) override
            {
                const std::vector<double>* sb = (src >= 0 && src < CircuitEffect::maxTaps) ? &store[2 * src + ch] : nullptr;
                auto& sv = svf[ch];
                double ph = phase;
                for (int i = 0; i < n; ++i)
                {
                    const double det = sb != nullptr && i < (int) sb->size() ? (*sb)[(size_t) i] : x[i];
                    const double e = env.tick (ch, det);
                    if (cnt[ch]-- <= 0)
                    {
                        cnt[ch] = 8;
                        double c = std::clamp (sens * e, 0.0, 1.0);
                        if (dir < 0) c = 1.0 - c;
                        if (trill > 0) c = std::clamp (c + trill * 0.5 * (1.0 + std::sin (ph)), 0.0, 1.0);
                        const double fc = flo * std::pow (fhi / flo, c);
                        const double q = qlo * std::pow (qhi / qlo, c);
                        g[ch] = tanApprox (pi * fc / fs);
                        k[ch] = 1.0 / q;
                        kb[ch] = std::pow (k[ch], bpn);
                    }
                    ph += lfoInc;
                    double in = x[i];
                    if (drive > 0) in = drive * std::tanh (in / drive);
                    double lp, bp, hp;
                    sv.tick (in, g[ch], k[ch], lp, bp, hp);
                    const double bpo = bp * kb[ch];
                    switch (mode)
                    {
                        case 0: x[i] = lp; break;
                        case 2: x[i] = hp; break;
                        case 3: x[i] = x[i] + bpo; break;
                        case 4: x[i] = lp + hp; break;
                        case 5: x[i] = lp + bpo; break;
                        default: x[i] = bpo; break;
                    }
                }
                if (ch == 0) phase = std::fmod (ph, 2.0 * pi);
            }
        private:
            std::vector<double>* store;
            EnvFollower env;
            Svf svf[2];
            double flo = 300, fhi = 3000, qlo = 3, qhi = 3, sens = 4, dir = 1, bpn = 1, trill = 0, trate = 6, drive = 0;
            double g[2] { 0.01, 0.01 }, k[2] { 0.3, 0.3 }, kb[2] { 0.3, 0.3 }, lfoInc = 0, phase = 0;
            int mode = 1, src = -1, cnt[2] {};
        };

        /** Filtro a formanti vocali (tre passa-banda in parallelo) che si sposta tra due o tre vocali per posizione fissa
            o seguendo l'inviluppo (Talking Machine, Cock Fight TALK). Vocali: 0 OO, 1 OH, 2 AH, 3 AE, 4 EH, 5 IH, 6 EE,
            7 ER, 8 UH; 9/10 = wah (un solo picco 400 -> 2200 Hz), 11/12 = doppio filtro tipo Bassballs.
            Argomenti: v1 v2 [v3] pos sens (negativo = sweep invertito) atk rel dir q shift (fattore sulle frequenze)
            pan (0..1: panoramica sincronizzata con lo sweep sui due canali). */
        class FormantStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                v1 = (int) std::lround (arg ("v1", p, def, 0.0));
                v2 = (int) std::lround (arg ("v2", p, def, 2.0));
                v3 = (int) std::lround (arg ("v3", p, def, -1.0));
                pos = arg ("pos", p, def, 0.0);
                sens = arg ("sens", p, def, 0.0);
                dir = arg ("dir", p, def, 1.0);
                if (sens < 0) { sens = -sens; dir = -dir; }
                pan = std::clamp (arg ("pan", p, def, 0.0), 0.0, 1.0);
                q = std::max (0.5, arg ("q", p, def, 6.0));
                shift = std::max (0.25, arg ("shift", p, def, 1.0));
                env.set (fs, arg ("atk", p, def, 10.0), arg ("rel", p, def, 200.0));
            }
            void reset() override { env.reset(); for (auto& c : svf) for (auto& s : c) s.reset(); }
            void process (int ch, double* x, int n) override
            {
                static const double tab[13][6] = {
                    { 300, 870, 2240, 1.0, 0.50, 0.15 },   // OO
                    { 570, 840, 2410, 1.0, 0.70, 0.20 },   // OH
                    { 730, 1090, 2440, 1.0, 0.70, 0.25 },  // AH
                    { 660, 1720, 2410, 1.0, 0.60, 0.30 },  // AE
                    { 530, 1840, 2480, 1.0, 0.55, 0.30 },  // EH
                    { 390, 1990, 2550, 1.0, 0.50, 0.35 },  // IH
                    { 270, 2290, 3010, 1.0, 0.45, 0.35 },  // EE
                    { 490, 1350, 1690, 1.0, 0.60, 0.45 },  // ER
                    { 640, 1190, 2390, 1.0, 0.65, 0.25 },  // UH
                    { 400, 800, 1600, 1.0, 0.0, 0.0 },     // wah, tallone
                    { 2200, 4400, 8800, 1.0, 0.0, 0.0 },   // wah, punta
                    { 55, 250, 1000, 1.0, 0.9, 0.0 },      // doppio filtro Bassballs, riposo
                    { 330, 1600, 3000, 1.0, 0.9, 0.0 } };  // doppio filtro Bassballs, aperto
                auto vw = [] (int v) { return std::clamp (v, 0, 12); };
                for (int i = 0; i < n; ++i)
                {
                    const double e = env.tick (ch, x[i]);
                    if (cnt[ch]-- <= 0)
                    {
                        cnt[ch] = 8;
                        double c = std::clamp (pos + sens * e, 0.0, 1.0);
                        if (dir < 0) c = 1.0 - c;
                        if (pan > 0) gpan[ch] = 1.0 - pan * (ch == 0 ? c : 1.0 - c);
                        int a = vw (v1), b = vw (v2);
                        double t = c;
                        if (v3 >= 0) { if (c < 0.5) t = 2 * c; else { a = vw (v2); b = vw (v3); t = 2 * c - 1; } }
                        for (int k = 0; k < 3; ++k)
                        {
                            const double fk = shift * tab[a][k] * std::pow (tab[b][k] / tab[a][k], t);
                            gk[ch][k] = tanApprox (pi * std::min (fk, 0.2 * fs) / fs);
                            amp[ch][k] = tab[a][k + 3] + t * (tab[b][k + 3] - tab[a][k + 3]);
                        }
                    }
                    double y = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        double lp, bp, hp;
                        svf[ch][k].tick (x[i], gk[ch][k], 1.0 / q, lp, bp, hp);
                        y += amp[ch][k] * bp / q;
                    }
                    x[i] = pan > 0 ? y * gpan[ch] : y;
                }
            }
        private:
            EnvFollower env;
            Svf svf[2][3];
            double gk[2][3] {}, amp[2][3] {}, gpan[2] { 1, 1 }, pos = 0, sens = 0, dir = 1, q = 6, shift = 1, pan = 0;
            int v1 = 0, v2 = 2, v3 = -1, cnt[2] {};
        };

        /** Generatore di sub-ottava / squadratore: trigger di Schmitt con isteresi relativa al picco, flip-flop
            divisore (div = 1 squadra, 2 ottava sotto, 4 due ottave sotto), onda quadra modulata dall'inviluppo
            (VCA a JFET/OTA) con correzione polyBLEP delle transizioni. Argomenti: div hys atk rel thr g. */
        class SubOctaveStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                div = std::max (1, (int) std::lround (arg ("div", p, def, 2.0)));
                hys = std::clamp (arg ("hys", p, def, 0.15), 0.0, 0.9);
                thr = arg ("thr", p, def, 0.003);
                gain = arg ("g", p, def, 1.0);
                env.set (fs, arg ("atk", p, def, 2.0), arg ("rel", p, def, 60.0));
                pkDecay = std::exp (-1.0 / (0.05 * fs));
                gateCoef = std::exp (-1.0 / (0.005 * fs));
            }
            void reset() override { env.reset(); for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double v = x[i];
                    const double e = env.tick (ch, v);
                    s.pk = std::max (std::abs (v), s.pk * pkDecay);
                    const double h = hys * s.pk;
                    double next = s.sq;
                    double frac = 0;
                    bool edge = false;
                    if (! s.hi && v > h && s.pk > thr)
                    {
                        s.hi = true;
                        frac = (h - s.prev) / std::max (1e-12, v - s.prev);
                        if (div == 1) { next = 1.0; edge = true; }
                        else if (++s.count >= div / 2) { s.count = 0; next = -s.sq; edge = true; }
                    }
                    else if (s.hi && v < -h)
                    {
                        s.hi = false;
                        if (div == 1) { frac = (-h - s.prev) / std::min (-1e-12, v - s.prev); next = -1.0; edge = true; }
                    }
                    // uscita ritardata di un campione con correzione polyBLEP
                    double cur = next, prevOut = s.held;
                    if (edge)
                    {
                        const double d = std::clamp (frac, 0.0, 1.0);
                        const double jump = next - s.sq;
                        prevOut += jump * (1.0 - d) * (1.0 - d) * 0.5;
                        cur -= jump * d * d * 0.5;
                    }
                    s.sq = next;
                    const double gt = e > thr ? 1.0 : 0.0;
                    s.gate = gt + gateCoef * (s.gate - gt);
                    x[i] = prevOut * s.amp * s.gate * gain;
                    s.amp = e;
                    s.held = cur;
                    s.prev = v;
                }
            }
        private:
            struct State { double pk = 0, prev = 0, sq = 1, held = 0, amp = 0, gate = 0; bool hi = false; int count = 0; } st[2];
            EnvFollower env;
            double hys = 0.15, thr = 0.003, gain = 1, pkDecay = 0.999, gateCoef = 0.99;
            int div = 2;
        };

        /** VCF passa-basso risonante a 3 poli (tre integratori OTA con saturazione) con frequenza a rampa innescata
            dall'attacco della nota (Micro Synthesizer: START -> STOP in RATE). Argomenti: src (memoria tap con la
            chitarra per il rilevatore), trig (V), start stop (Hz), rate (ms), res (0..1), kmax, drive (V).
            In alternativa a start/stop: fc (Hz) mod (-1..1) oct (ottave): mod > 0 sale da fc/2^(oct*mod) a fc,
            mod < 0 scende da fc a fc*2^(oct*mod), mod = 0 fermo su fc (Pico Swello). */
        class SweepVcfStage : public Stage
        {
        public:
            explicit SweepVcfStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                src = (int) std::lround (arg ("src", p, def, -1.0));
                trig.set (fs, arg ("trig", p, def, 0.02));
                fStart = std::clamp (arg ("start", p, def, 2000.0), 20.0, 0.2 * fs);
                fStop = std::clamp (arg ("stop", p, def, 200.0), 20.0, 0.2 * fs);
                if (has ("fc"))
                {
                    // fc = frequenza di riferimento, mod -1..1: > 0 sweep verso l'alto fino a fc, < 0 verso il basso da fc
                    const double fc = std::clamp (arg ("fc", p, def, 1000.0), 20.0, 0.2 * fs);
                    const double m = std::clamp (arg ("mod", p, def, 0.0), -1.0, 1.0), oct = arg ("oct", p, def, 3.0);
                    fStart = std::clamp (m > 0 ? fc * std::pow (2.0, -oct * m) : fc, 20.0, 0.2 * fs);
                    fStop = std::clamp (m < 0 ? fc * std::pow (2.0, oct * m) : fc, 20.0, 0.2 * fs);
                }
                rateMs = std::max (1.0, arg ("rate", p, def, 300.0));
                kfb = std::clamp (arg ("res", p, def, 0.3), 0.0, 1.0) * arg ("kmax", p, def, 7.6);
                drive = std::max (0.01, arg ("drive", p, def, 1.0));
                comp = arg ("comp", p, def, 0.5);
            }
            void reset() override { trig.reset(); for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                const std::vector<double>* sb = (src >= 0 && src < CircuitEffect::maxTaps) ? &store[2 * src + ch] : nullptr;
                const double step = 1000.0 / (rateMs * fs);
                for (int i = 0; i < n; ++i)
                {
                    const double det = sb != nullptr && i < (int) sb->size() ? (*sb)[(size_t) i] : x[i];
                    if (trig.tick (ch, det)) s.pos = 0.0;
                    s.pos = std::min (1.0, s.pos + step);
                    if (s.cnt-- <= 0)
                    {
                        s.cnt = 8;
                        const double fc = fStart * std::pow (fStop / fStart, s.pos);
                        const double g = tanApprox (pi * fc / fs);
                        s.G = g / (1.0 + g);
                    }
                    // tre poli TPT con saturazione tanh all'ingresso di ogni OTA, retroazione con un campione di ritardo
                    double u = x[i] * (1.0 + comp * kfb) - kfb * s.y3;
                    double y = u;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double in = drive * std::tanh (y / drive);
                        const double v = s.G * (in - s.z[k]);
                        const double out = v + s.z[k];
                        s.z[k] = out + v;
                        y = out;
                    }
                    s.y3 = y;
                    x[i] = y;
                }
            }
        private:
            struct State { double z[3] {}, y3 = 0, pos = 1.0, G = 0.01; int cnt = 0; } st[2];
            std::vector<double>* store;
            NoteTrigger trig;
            double fStart = 2000, fStop = 200, rateMs = 300, kfb = 0, drive = 1, comp = 0.5;
            int src = -1;
        };

        /** VCA d'attacco ritardato (ATTACK DELAY): a ogni nuova nota il guadagno riparte da 0 e sale in 'time' ms. */
        class AttackVcaStage : public Stage
        {
        public:
            explicit AttackVcaStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                src = (int) std::lround (arg ("src", p, def, -1.0));
                trig.set (fs, arg ("trig", p, def, 0.02));
                timeMs = arg ("time", p, def, 1.0);
            }
            void reset() override { trig.reset(); g[0] = g[1] = 1.0; }
            void process (int ch, double* x, int n) override
            {
                const std::vector<double>* sb = (src >= 0 && src < CircuitEffect::maxTaps) ? &store[2 * src + ch] : nullptr;
                const double step = timeMs > 2.0 ? 1000.0 / (timeMs * fs) : 1.0;
                for (int i = 0; i < n; ++i)
                {
                    const double det = sb != nullptr && i < (int) sb->size() ? (*sb)[(size_t) i] : x[i];
                    if (trig.tick (ch, det) && timeMs > 2.0) g[ch] = 0.0;
                    g[ch] = std::min (1.0, g[ch] + step);
                    x[i] *= g[ch] * g[ch];
                }
            }
        private:
            std::vector<double>* store;
            NoteTrigger trig;
            double timeMs = 1, g[2] { 1, 1 };
            int src = -1;
        };

        /** Noise gate a soglia (EVH 5150, Deluxe Big Muff): thr in volt (0 = escluso), atk/rel in ms, isteresi 6 dB,
            floor = attenuazione a gate chiuso (dB); fdb (facoltativo) = guadagno a gate chiuso in dB con segno
            (Silencer: da +4 a -70 dB), se presente sostituisce floor. */
        class GateStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                thr = arg ("thr", p, def, 0.0);
                floorGain = std::pow (10.0, -std::abs (arg ("floor", p, def, 70.0)) / 20.0);
                if (has ("fdb")) floorGain = std::pow (10.0, std::clamp (arg ("fdb", p, def, -70.0), -120.0, 24.0) / 20.0);
                det.set (fs, 0.3, 15.0);
                atkC = std::exp (-1.0 / (std::max (0.05, arg ("atk", p, def, 1.0)) * 1e-3 * fs));
                relC = std::exp (-1.0 / (std::max (0.05, arg ("rel", p, def, 80.0)) * 1e-3 * fs));
                holdN = (int) (arg ("hold", p, def, 20.0) * 1e-3 * fs);
            }
            void reset() override { det.reset(); for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                if (thr <= 0) return;
                auto& s = st[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double e = det.tick (ch, x[i]);
                    if (e > thr) { s.open = true; s.hold = holdN; }
                    else if (e < 0.5 * thr && s.hold-- <= 0) s.open = false;
                    const double target = s.open ? 1.0 : floorGain;
                    s.g = target + (target > s.g ? atkC : relC) * (s.g - target);
                    x[i] *= s.g;
                }
            }
        private:
            struct State { double g = 1; bool open = true; int hold = 0; } st[2];
            EnvFollower det;
            double thr = 0, floorGain = 3e-4, atkC = 0, relC = 0;
            int holdN = 0;
        };

        /** Compressore/limitatore a controllo in avanti (attenuatore ottico LED/lampada, JFET): soglia thr (V), ratio,
            atk/rel (ms), lag (ms: inerzia termica della lampada, 0 = LED), knee (dB), dlp (Hz: passa-basso sul
            rivelatore, la lampada comprime meno gli acuti). */
        class CompStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                thr = std::max (1e-5, arg ("thr", p, def, 0.1));
                ratio = std::max (1.0, arg ("ratio", p, def, 4.0));
                knee = std::max (0.1, arg ("knee", p, def, 6.0));
                det.set (fs, arg ("atk", p, def, 5.0), arg ("rel", p, def, 200.0));
                const double lag = arg ("lag", p, def, 0.0);
                lagC = lag > 0.05 ? std::exp (-1.0 / (lag * 1e-3 * fs)) : 0.0;
                const double dlp = arg ("dlp", p, def, 0.0);
                dlpC = dlp > 0 ? std::exp (-2.0 * pi * dlp / fs) : 0.0;
            }
            void reset() override { det.reset(); for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                const double thrDb = 20.0 * std::log10 (thr);
                for (int i = 0; i < n; ++i)
                {
                    double d = x[i];
                    if (dlpC > 0) { s.lp = d + dlpC * (s.lp - d); d = s.lp; }
                    const double e = det.tick (ch, d);
                    if (s.cnt-- <= 0)
                    {
                        s.cnt = 4;
                        const double over = 20.0 * std::log10 (std::max (e, 1e-9)) - thrDb;
                        double gr = 0;
                        if (over > knee * 0.5) gr = over * (1.0 - 1.0 / ratio);
                        else if (over > -knee * 0.5) { const double t = over + knee * 0.5; gr = (1.0 - 1.0 / ratio) * t * t / (2.0 * knee); }
                        s.target = std::pow (10.0, -gr / 20.0);
                    }
                    s.g = s.target + lagC * (s.g - s.target);
                    x[i] *= s.g;
                }
            }
        private:
            struct State { double lp = 0, g = 1, target = 1; int cnt = 0; } st[2];
            EnvFollower det;
            double thr = 0.1, ratio = 4, knee = 6, lagC = 0, dlpC = 0;
        };

        /** Modulatore ad anello: y = x * portante (seno o triangolo, wave=0/1) di frequenza f (Hz). */
        class RingStage : public Stage
        {
        public:
            void update (double fsNew, const float* p, const ModelDef& def) override
            {
                fs = fsNew;
                inc = std::clamp (arg ("f", p, def, 440.0), 0.1, 0.25 * fs) / fs;
                tri = arg ("wave", p, def, 0.0) > 0.5;
            }
            void reset() override { ph[0] = ph[1] = 0; }
            void process (int ch, double* x, int n) override
            {
                double p = ph[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double c = tri ? 1.0 - 4.0 * std::abs (p - 0.5) : std::sin (2.0 * pi * p);
                    x[i] *= c;
                    p += inc; if (p >= 1.0) p -= 1.0;
                }
                ph[ch] = p;
            }
        private:
            double inc = 0.01, ph[2] {};
            bool tri = false;
        };

        /** Eliminatore di ronzio sincrono: stima la forma d'onda periodica alla frequenza di rete (media esponenziale
            periodo per periodo, costante tau in s) e la sottrae; mode 0 = solo armoniche dispari, 1 = tutte. */
        class HumStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                const double f0 = std::clamp (arg ("f0", p, def, 50.0), 20.0, 400.0);
                const int P = std::max (4, (int) std::lround (f / f0));
                if (P != period || std::abs (f - fs) > 1e-9)
                {
                    period = P;
                    for (auto& b : buf) b.assign ((size_t) P, 0.0);
                    idx[0] = idx[1] = 0;
                }
                fs = f;
                all = arg ("mode", p, def, 0.0) > 0.5;
                a = 1.0 / std::max (1.0, arg ("tau", p, def, 1.5) * f0);
            }
            void reset() override { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0); idx[0] = idx[1] = 0; }
            void process (int ch, double* x, int n) override
            {
                auto& b = buf[ch];
                if (b.empty()) return;
                int k = idx[ch];
                const int half = period / 2;
                for (int i = 0; i < n; ++i)
                {
                    b[(size_t) k] += a * (x[i] - b[(size_t) k]);
                    const double est = all ? b[(size_t) k] : 0.5 * (b[(size_t) k] - b[(size_t) ((k + half) % period)]);
                    x[i] -= est;
                    if (++k >= period) k = 0;
                }
                idx[ch] = k;
            }
        private:
            std::vector<double> buf[2];
            int period = 0, idx[2] {};
            double a = 0.01;
            bool all = false;
        };

        /** Sustainer "freeze": all'accensione (reset) cattura len ms di segnale e li ripete all'infinito a grani
            sovrapposti con finestra di Hann (posizioni pseudo-casuali), con dissolvenza d'ingresso fin ms.
            L'uscita contiene solo la parte congelata (il diretto si somma con tap/mix). auto=1: a ogni nuova nota
            (soglia trig in V) la cattura riparte (modo AUTO del Superego/Deep Freeze). */
        class FreezeStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                const int L = std::max (64, (int) (std::clamp (arg ("len", p, def, 160.0), 20.0, 1000.0) * 1e-3 * f));
                if (L != len || std::abs (f - fs) > 1e-9)
                {
                    len = L;
                    for (auto& s : st) { s.buf.assign ((size_t) L, 0.0); s.fill = 0; s.t = 0; s.g = 0; }
                }
                fs = f;
                const double fin = std::max (1.0, arg ("fin", p, def, 30.0));
                fadeStep = 1000.0 / (fin * fs);
                autoMode = arg ("auto", p, def, 0.0) > 0.5;
                if (autoMode) trig.set (fs, arg ("trig", p, def, 0.02));
            }
            void reset() override
            {
                unsigned seed = 12345;
                for (auto& s : st) { std::fill (s.buf.begin(), s.buf.end(), 0.0); s.fill = 0; s.t = 0; s.g = 0; s.rng = seed++; }
                trig.reset();
            }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                if (s.buf.empty()) return;
                const int G = len / 2;                  // lunghezza del grano = meta' della cattura
                for (int i = 0; i < n; ++i)
                {
                    // modo automatico: ogni nuova nota riparte la cattura (Superego / Deep Freeze AUTO)
                    if (autoMode && trig.tick (ch, x[i])) { s.fill = 0; s.t = 0; s.g = 0; }
                    if (s.fill < len)
                    {
                        s.buf[(size_t) s.fill++] = x[i];
                        x[i] = 0.0;
                        continue;
                    }
                    // due grani sfasati di G/2
                    double y = 0;
                    for (int v = 0; v < 2; ++v)
                    {
                        const int tt = (s.t + v * (G / 2)) % G;
                        if (tt == 0)
                        {
                            s.rng = s.rng * 1664525u + 1013904223u;
                            s.start[v] = (int) ((s.rng >> 8) % (unsigned) std::max (1, len - G));
                        }
                        const double w = std::sin (pi * (tt + 0.5) / G);
                        y += w * w * s.buf[(size_t) (s.start[v] + tt)];
                    }
                    s.t = (s.t + 1) % G;
                    s.g = std::min (1.0, s.g + fadeStep);
                    x[i] = y * s.g;
                }
            }
        private:
            struct State { std::vector<double> buf; int fill = 0, t = 0, start[2] {}; double g = 0; unsigned rng = 1; } st[2];
            int len = 0;
            double fadeStep = 0.001;
            bool autoMode = false;
            NoteTrigger trig;
        };

        //==============================================================================
        // Stadi aggiunti nella tappa 3B (pedali analogici Electro-Harmonix / MXR di priorita' B)
        //==============================================================================

        /** Stadio a transistor con punto di lavoro regolabile (comandi BIAS / RIP / VOLTS dei fuzz): corrente di
            collettore normalizzata u = q + x*g/vsw (q = Icq/Imax: 0.5 = centrato, verso 0 interdizione), interdizione
            morbida (softplus con ginocchio s) e saturazione morbida verso 1 (esponente k). Uscita = variazione della
            tensione di collettore (vsw = escursione totale in volt), invertente salvo inv=0. Con q basso i segnali
            piccoli non passano (fuzz 'strozzato'/gated, sputter) e la forma d'onda si raddrizza (pseudo-ottava). */
        class BiasStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                g = arg ("g", p, def, 20.0);
                vsw = std::max (0.05, arg ("vsw", p, def, 8.0));
                q = arg ("q", p, def, 0.5);
                s = std::max (1e-4, arg ("s", p, def, 0.04));
                k = std::max (1.0, arg ("k", p, def, 4.0));
                kInt = kneeInt (k);
                inv = arg ("inv", p, def, 1.0) > 0.5;
                u0 = curr (q);
            }
            void reset() override {}
            void process (int, double* x, int n) override
            {
                const double kx = g / vsw;
                for (int i = 0; i < n; ++i)
                {
                    const double d = (curr (q + x[i] * kx) - u0) * vsw;
                    x[i] = inv ? -d : d;
                }
            }
        private:
            inline double curr (double ul) const noexcept
            {
                const double a = ul / s;
                const double uc = a > 30.0 ? ul : s * std::log1p (std::exp (std::max (a, -60.0)));   // interdizione morbida
                double dy;
                return softSat (uc, 1.0, 1.0, k, kInt, dy);                                         // saturazione
            }
            double g = 20, vsw = 8, q = 0.5, s = 0.04, k = 4, u0 = 0.5;
            int kInt = 4;
            bool inv = true;
        };

        /** VCA comandato dall'inviluppo (DYNAMICS del Graphic Fuzz, espansore dello Steel Leather): guadagno
            gv = clamp((e/ref)^pw, gmin, gmax), g = 1 - depth + depth*gv; e = inviluppo (attacco atk, rilascio rel in ms)
            della memoria tap 'src' (-1 = ingresso dello stadio), raddrizzato a onda intera. */
        class EnvVcaStage : public Stage
        {
        public:
            explicit EnvVcaStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                src = (int) std::lround (arg ("src", p, def, -1.0));
                env.set (fs, arg ("atk", p, def, 3.0), arg ("rel", p, def, 200.0));
                ref = std::max (1e-6, arg ("ref", p, def, 0.1));
                pw = arg ("pw", p, def, 1.0);
                gmax = arg ("gmax", p, def, 4.0);
                gmin = arg ("gmin", p, def, 0.0);
                depth = std::clamp (arg ("depth", p, def, 1.0), 0.0, 1.0);
            }
            void reset() override { env.reset(); for (auto& v : gain) v = 1.0 - depth; for (auto& c : cnt) c = 0; }
            void process (int ch, double* x, int n) override
            {
                const std::vector<double>* sb = (src >= 0 && src < CircuitEffect::maxTaps) ? &store[2 * src + ch] : nullptr;
                for (int i = 0; i < n; ++i)
                {
                    const double d = sb != nullptr && i < (int) sb->size() ? (*sb)[(size_t) i] : x[i];
                    const double e = env.tick (ch, d);
                    if (cnt[ch]-- <= 0)
                    {
                        cnt[ch] = 4;
                        const double r = e / ref;
                        const double gv = std::clamp (pw == 1.0 ? r : std::pow (std::max (r, 1e-9), pw), gmin, gmax);
                        gain[ch] = 1.0 - depth + depth * gv;
                    }
                    x[i] *= gain[ch];
                }
            }
        private:
            std::vector<double>* store;
            EnvFollower env;
            double ref = 0.1, pw = 1, gmax = 4, gmin = 0, depth = 1, gain[2] { 1, 1 };
            int src = -1, cnt[2] {};
        };

        /** Riduttore di frequenza di campionamento e di risoluzione (bit crusher, Mainframe): campiona e tiene il
            segnale a 'rate' Hz (senza filtro anti-aliasing, come il pedale) e lo quantizza a 'bits' bit (anche
            frazionari) sulla scala +-fs volt con un livello a zero (a pochi bit i segnali deboli spariscono: 'effetto
            gate' dei livelli fissi del Mainframe); env=1: la scala segue l'inviluppo del segnale (1.2 x picco, attacco 1 ms,
            rilascio 60 ms), cioe' la stessa risoluzione relativa per segnali forti e deboli. */
        class CrushStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                inc = std::clamp (arg ("rate", p, def, 48000.0), 10.0, fs) / fs;
                const double bits = std::clamp (arg ("bits", p, def, 24.0), 1.0, 24.0);
                full = std::max (1e-3, arg ("fs", p, def, 1.0));
                levels = std::pow (2.0, bits);
                step = 2.0 * full / levels;
                lim = full;
                envMode = arg ("env", p, def, 0.0) > 0.5;
                if (envMode) env.set (fs, 1.0, 60.0);
            }
            void reset() override { for (auto& s : st) s = {}; env.reset(); }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double e = envMode ? env.tick (ch, x[i]) : 0.0;
                    s.acc += inc;
                    if (s.acc >= 1.0)
                    {
                        s.acc -= 1.0;
                        if (envMode)
                        {
                            // scala di quantizzazione che segue l'inviluppo: stessa risoluzione per segnali forti e deboli
                            const double fe = std::clamp (1.2 * e, 1e-4, full);
                            step = 2.0 * fe / levels;
                            lim = fe;
                        }
                        const double v = std::clamp (x[i], -lim, lim);
                        s.hold = std::round (v / step) * step;     // quantizzatore con lo zero (mid-tread): a pochi bit gate
                    }
                    x[i] = s.hold;
                }
            }
        private:
            struct State { double acc = 1.0, hold = 0; } st[2];
            double inc = 1, step = 1e-7, lim = 1, full = 1, levels = 2;
            bool envMode = false;
            EnvFollower env;
        };

        /** Filtro passa-basso risonante a 4 poli (OTA con saturazione tanh) modulato da un LFO (Blurst): la frequenza
            percorre in scala logaritmica la finestra [fmin..fmax] larga 'span' (0..1 della gamma) e posizionata da
            'center' (0 = in basso, 1 = in alto); LFO a 'rate' Hz di forma shape (0 triangolo, 1 dente di sega salente,
            2 discendente, 3 seno); res 0..1 (kmax = retroazione massima, 4 = auto-oscillazione), drive (V). */
        class LfoVcfStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                fmin = std::clamp (arg ("fmin", p, def, 80.0), 10.0, 0.2 * fs);
                fmax = std::clamp (arg ("fmax", p, def, 5000.0), fmin, 0.2 * fs);
                span = std::clamp (arg ("span", p, def, 1.0), 0.0, 1.0);
                center = std::clamp (arg ("center", p, def, 0.5), 0.0, 1.0);
                inc = std::clamp (arg ("rate", p, def, 1.0), 0.005, 50.0) / fs;
                shape = (int) std::lround (arg ("shape", p, def, 0.0));
                kfb = std::clamp (arg ("res", p, def, 0.3), 0.0, 1.0) * arg ("kmax", p, def, 4.0);
                drive = std::max (0.01, arg ("drive", p, def, 1.0));
            }
            void reset() override { for (auto& s : st) s = {}; }
            void process (int ch, double* x, int n) override
            {
                auto& s = st[ch];
                const double lo = center * (1.0 - span);
                for (int i = 0; i < n; ++i)
                {
                    if (s.cnt-- <= 0)
                    {
                        s.cnt = 8;
                        double v;
                        switch (shape)
                        {
                            case 1:  v = s.ph; break;
                            case 2:  v = 1.0 - s.ph; break;
                            case 3:  v = 0.5 - 0.5 * std::cos (2.0 * pi * s.ph); break;
                            default: v = 1.0 - std::abs (2.0 * s.ph - 1.0); break;
                        }
                        const double fc = fmin * std::pow (fmax / fmin, lo + span * v);
                        const double gg = tanApprox (pi * std::min (fc, 0.2 * fs) / fs);
                        s.G = gg / (1.0 + gg);
                    }
                    s.ph += inc; if (s.ph >= 1.0) s.ph -= 1.0;
                    double y = x[i] - kfb * s.y4;
                    for (int k = 0; k < 4; ++k)
                    {
                        const double in = drive * std::tanh (y / drive);
                        const double v = s.G * (in - s.z[k]);
                        const double out = v + s.z[k];
                        s.z[k] = out + v;
                        y = out;
                    }
                    s.y4 = y;
                    x[i] = y;
                }
            }
        private:
            struct State { double z[4] {}, y4 = 0, ph = 0, G = 0.01; int cnt = 0; } st[2];
            double fmin = 80, fmax = 5000, span = 1, center = 0.5, inc = 1e-5, kfb = 0, drive = 1;
            int shape = 0;
        };

        /** Guadagno per canale (pedali stereo a due canali indipendenti, instradamento A/B): canale sinistro x l,
            destro x r, con rampa lineare a ogni blocco. */
        class ChannelGainStage : public Stage
        {
        public:
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                a[0] = arg ("l", p, def, 1.0);
                a[1] = arg ("r", p, def, a[0]);
            }
            void reset() override { cur[0] = a[0]; cur[1] = a[1]; }
            void process (int ch, double* x, int n) override
            {
                const int c = std::clamp (ch, 0, 1);
                double g = cur[c];
                const double step = (a[c] - g) / std::max (1, n);
                for (int i = 0; i < n; ++i) { g += step; x[i] *= g; }
                cur[c] = a[c];
            }
        private:
            double a[2] { 1, 1 }, cur[2] { 1, 1 };
        };

        /** Pan / blend / volume stereo (Next Step Pan): legge gli ingressi di entrambi i canali (memoria riservata
            riempita da CircuitEffect) e ignora il segnale corrente. mode 0 = volume mono (ingresso sinistro), 1 = blend
            (punta = solo ingresso sinistro, tallone = solo destro), 2 = pan (punta = solo uscita sinistra, tallone = solo
            destra), 3 = volume stereo; pos 0..1 (tallone..punta), g = volume; leggi a potenza costante. */
        class PanStage : public Stage
        {
        public:
            explicit PanStage (std::vector<double>* buf) : store (buf) {}
            void update (double f, const float* p, const ModelDef& def) override
            {
                fs = f;
                mode = std::clamp ((int) std::lround (arg ("mode", p, def, 2.0)), 0, 3);
                const double pos = std::clamp (arg ("pos", p, def, 0.5), 0.0, 1.0);
                const double v = arg ("g", p, def, 1.0);
                const double gl = std::sin (0.5 * pi * pos), gr = std::cos (0.5 * pi * pos);
                for (int c = 0; c < 2; ++c) { tgtL[c] = 0; tgtR[c] = 0; }
                switch (mode)
                {
                    case 0:  tgtL[0] = tgtL[1] = v; break;                          // volume mono: ingresso sinistro
                    case 1:  tgtL[0] = tgtL[1] = gl; tgtR[0] = tgtR[1] = gr; break;  // blend dei due ingressi
                    case 2:  tgtL[0] = gl; tgtL[1] = gr; break;                    // pan dell'ingresso sinistro
                    default: tgtL[0] = v; tgtR[1] = v; break;                      // volume stereo
                }
            }
            void reset() override { for (int c = 0; c < 2; ++c) { curL[c] = tgtL[c]; curR[c] = tgtR[c]; } }
            void process (int ch, double* x, int n) override
            {
                const int c = std::clamp (ch, 0, 1);
                const auto& inL = store[0];
                const auto& inR = store[1];
                double gL = curL[c], gR = curR[c];
                const double sL = (tgtL[c] - gL) / std::max (1, n), sR = (tgtR[c] - gR) / std::max (1, n);
                for (int i = 0; i < n; ++i)
                {
                    gL += sL; gR += sR;
                    const double l = i < (int) inL.size() ? inL[(size_t) i] : 0.0;
                    const double r = i < (int) inR.size() ? inR[(size_t) i] : l;
                    x[i] = gL * l + gR * r;
                }
                curL[c] = tgtL[c]; curR[c] = tgtR[c];
            }
        private:
            std::vector<double>* store;          // store[0] / store[1] = ingressi sinistro / destro
            int mode = 2;
            double tgtL[2] {}, tgtR[2] {}, curL[2] {}, curR[2] {};
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
            if (type == "recall") return std::make_unique<RecallStage> (tapBuf);
            if (type == "fbinv")  return std::make_unique<InvClipper>();
            if (type == "nodal")  return std::make_unique<NodalStage>();
            if (type == "envf")   return std::make_unique<EnvFilterStage> (tapBuf);
            if (type == "formant") return std::make_unique<FormantStage>();
            if (type == "subocta") return std::make_unique<SubOctaveStage>();
            if (type == "sweep3") return std::make_unique<SweepVcfStage> (tapBuf);
            if (type == "atkvca") return std::make_unique<AttackVcaStage> (tapBuf);
            if (type == "gate")   return std::make_unique<GateStage>();
            if (type == "comp")   return std::make_unique<CompStage>();
            if (type == "ring")   return std::make_unique<RingStage>();
            if (type == "hum")    return std::make_unique<HumStage>();
            if (type == "freeze") return std::make_unique<FreezeStage>();
            if (type == "cebias") return std::make_unique<BiasStage>();
            if (type == "envvca") return std::make_unique<EnvVcaStage> (tapBuf);
            if (type == "crush")  return std::make_unique<CrushStage>();
            if (type == "lfovcf") return std::make_unique<LfoVcfStage>();
            if (type == "chgain") return std::make_unique<ChannelGainStage>();
            if (type == "pan")    return std::make_unique<PanStage> (tapBuf + 2 * CircuitEffect::maxTaps);
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
        // 2 x maxTaps memorie 'tap' + 2 memorie riservate agli ingressi dei due canali (stadio 'pan')
        tapStore = std::make_unique<std::vector<double>[]> (2 * maxTaps + 2);
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
                    if (auto* iv = dynamic_cast<InvClipper*> (st.get())) iv->diodeName = value;
                    continue;
                }
                st->args.emplace_back (key, Expr::parse (value, error));
            }
            if (dynamic_cast<PanStage*> (st.get()) != nullptr) needInputs = true;
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
        for (size_t c = 0; c < 2 * (size_t) maxTaps + 2; ++c) tapStore[c].reserve ((size_t) maxBlock * 4 + 16);
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
        if (needInputs)      // ingressi di entrambi i canali per gli stadi che li incrociano (pan / blend)
            for (int c = 0; c < 2; ++c)
            {
                const float* s = up.getChannelPointer ((size_t) std::min (c, numCh - 1));
                auto& b = tapStore[2 * (size_t) maxTaps + (size_t) c];
                b.resize ((size_t) N);
                for (int i = 0; i < N; ++i) b[(size_t) i] = s[i];
            }
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
