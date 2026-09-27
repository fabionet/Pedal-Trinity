/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Effetti basati sul tempo: delay digitali multimodo, eco a nastro a
    testine multiple, riverberi (FDN a 8 linee, molla dispersiva, plate,
    gate, reverse, shimmer, dinamico).

    Delay digitale (config):
      modes=Nome:minms:maxms:flag+flag|...   flag: hold mod analog tape reverse shimmer warm pan dual lofi
      bw=Hz (banda della ripetizione)  fbmax=1.0  sat=0|1
    Eco a nastro:
      heads=1,2,3 (spaziature relative)  maxms=  modes=h1h2h3r|...  (es. 100|010|001|011|111|101r...)
      bw=Hz  wow=profondita'  flutter=profondita'
    Riverbero:
      modes=Nome:tipo|...  tipi: room hall plate spring modulate gate reverse shimmer dynamic delay lofi
      decay=lo,hi (s)  predelay=ms  bw=Hz
*/

#include "FxCommon.h"
#include "Families.h"

namespace pt::engine
{
    namespace
    {
        struct DelayMode
        {
            std::string name;
            float minMs = 20, maxMs = 800;
            bool hold = false, mod = false, analog = false, tape = false, reverse = false, shimmer = false,
                 warm = false, pan = false, dual = false, lofi = false;
        };

        std::vector<DelayMode> parseDelayModes (const std::string& s)
        {
            std::vector<DelayMode> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find ('|', i); if (j == std::string::npos) j = s.size();
                const auto tok = s.substr (i, j - i);
                i = j + 1;
                if (tok.empty()) continue;
                DelayMode m;
                std::vector<std::string> parts;
                size_t a = 0;
                while (a <= tok.size()) { auto b = tok.find (':', a); if (b == std::string::npos) b = tok.size(); parts.push_back (tok.substr (a, b - a)); a = b + 1; }
                m.name = parts[0];
                if (parts.size() > 1) m.minMs = (float) std::atof (parts[1].c_str());
                if (parts.size() > 2) m.maxMs = (float) std::atof (parts[2].c_str());
                if (parts.size() > 3)
                {
                    const auto& f = parts[3];
                    auto hasF = [&f] (const char* k) { return f.find (k) != std::string::npos; };
                    m.hold = hasF ("hold"); m.mod = hasF ("mod"); m.analog = hasF ("analog"); m.tape = hasF ("tape");
                    m.reverse = hasF ("reverse"); m.shimmer = hasF ("shimmer"); m.warm = hasF ("warm");
                    m.pan = hasF ("pan"); m.dual = hasF ("dual"); m.lofi = hasF ("lofi");
                }
                out.push_back (m);
            }
            if (out.empty()) out.push_back ({});
            return out;
        }

        //==============================================================================
        class DigitalDelayEffect : public Effect
        {
        public:
            explicit DigitalDelayEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                modes = parseDelayModes (cfg.str ("modes", "Delay:20:800"));
                bw = cfg.num ("bw", 8000);
                fbMax = (float) cfg.num ("fbmax", 1.0);
                pLevel = role (*this, "level", 0.5f);
                pFeedback = role (*this, "feedback", 0.4f);
                pTime = role (*this, "time", 0.5f);
                pMode = role (*this, "mode", 0.0f);
                pTone = role (*this, "tone", 0.6f);
                pMod = role (*this, "mod", 0.3f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                float maxMs = 100;
                for (auto& m : modes) maxMs = std::max (maxMs, m.maxMs);
                for (auto& l : line) l.allocate ((int) (sr * (maxMs * 0.001 * 1.1 + 0.1)));
                for (auto& p : shifter) p.prepare (sr, 80.0);
                reset();
            }
            void reset() override
            {
                for (auto& l : line) l.clear();
                for (auto& p : shifter) p.clear();
                for (auto& f : fbFilter) f.reset();
                curDelay = -1; revPos = 0; fb[0] = fb[1] = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                const auto& m = modes[(size_t) std::clamp (pMode.step (*this), 0, (int) modes.size() - 1)];
                const float targetMs = logMap (pTime.get (*this), m.minMs, m.maxMs);
                const float target = (float) (targetMs * 0.001 * sr);
                if (curDelay < 0) curDelay = target;
                const float level = pLevel.get (*this) * 1.2f;
                float feedback = std::min (fbMax, pFeedback.get (*this) * 1.05f);
                if (m.hold) feedback = 1.0f;
                double cut = bw * (m.analog ? 0.45 : m.warm ? 0.6 : m.lofi ? 0.3 : 1.0);
                if (pTone.present()) cut *= std::pow (0.12, 1.0 - pTone.get (*this));
                fbFilter[0].set (sr, cut, 0.6); fbFilter[1].set (sr, cut, 0.6);
                modLfo.setRate (sr, m.tape ? 0.9 : 0.6);
                flutter.setRate (sr, 7.3);
                const float modDepth = (m.mod || m.tape) ? pMod.get (*this) * (m.tape ? 0.0035f : 0.0025f) : 0.0f;
                for (auto& p : shifter) p.setRatio (2.0f);

                for (int i = 0; i < n; ++i)
                {
                    // glissando del tempo come nei delay hardware quando si gira D.TIME
                    curDelay += (target - curDelay) * 0.00025f;
                    float d = curDelay;
                    if (modDepth > 0)
                    {
                        d *= 1.0f + modDepth * modLfo.next (1) + (m.tape ? 0.0008f * flutter.next (1) : 0.0f);
                        modLfo.advance(); flutter.advance();
                    }
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float wet[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        float y;
                        if (m.reverse)
                        {
                            // lettura all'indietro per segmenti lunghi quanto il tempo di delay
                            const float seg = std::max (64.0f, d);
                            const float pos = std::fmod ((float) revPos, seg);
                            const float win = std::sin (3.14159265f * pos / seg);
                            y = line[c].read (2.0f * pos + 1.0f) * win;
                        }
                        else y = line[c].read (d);
                        wet[c] = y;
                    }
                    if (m.reverse) ++revPos;

                    for (int c = 0; c < 2; ++c)
                    {
                        float lp, bp, hp;
                        fbFilter[c].tick (c, wet[c], lp, bp, hp);
                        float f = lp;
                        if (m.analog || m.tape) f = softSat (f * 1.3f) / 1.3f;
                        if (m.shimmer) f = 0.6f * f + 0.4f * shifter[c].tick (f);
                        fb[c] = f;
                    }
                    const float in0 = m.hold ? 0.0f : inL, in1 = m.hold ? 0.0f : inR;
                    if (m.pan || m.dual)
                    {
                        // ping-pong: ogni ripetizione cambia lato
                        line[0].push (0.5f * (in0 + in1) + feedback * fb[1]);
                        line[1].push (feedback * fb[0]);
                    }
                    else
                    {
                        line[0].push (in0 + feedback * fb[0]);
                        line[1].push (in1 + feedback * fb[1]);
                    }
                    ch[0][i] = inL + level * wet[0];
                    if (numCh > 1) ch[1][i] = inR + level * wet[1];
                    else ch[0][i] = inL + level * 0.5f * (wet[0] + wet[1]);
                }
            }
        private:
            Config cfg;
            std::vector<DelayMode> modes;
            double bw = 8000, sr = 48000;
            float fbMax = 1, curDelay = -1, fb[2] {};
            long long revPos = 0;
            RoleParam pLevel, pFeedback, pTime, pMode, pTone, pMod;
            DelayLine line[2];
            SVF fbFilter[2];
            PitchShifter shifter[2];
            LFO modLfo, flutter;
        };

        //==============================================================================
        /** Riverbero a molla: cascata di all-pass dispersivi + linea con retroazione. */
        struct Spring
        {
            float ap[24] {}, apy[24] {};
            DelayLine loop;
            SVF lp;
            float last = 0;
            void prepare (double sr) { loop.allocate ((int) (sr * 0.2)); lp.set (sr, 4200, 0.7); }
            void clear() { std::fill (std::begin (ap), std::end (ap), 0.0f); std::fill (std::begin (apy), std::end (apy), 0.0f); loop.clear(); lp.reset(); last = 0; }
            inline float tick (float x, float decay, double sr) noexcept
            {
                float v = x + decay * last;
                constexpr float a = 0.62f;                  // dispersione: il "chirp" della molla
                for (int k = 0; k < 24; ++k)
                {
                    const float y = -a * v + ap[k] + a * apy[k];
                    ap[k] = v; apy[k] = y; v = y;
                }
                float l, b, h;
                lp.tick (0, v, l, b, h);
                loop.push (l);
                last = loop.read ((float) (0.034 * sr));
                return last;
            }
        };

        class ReverbEffect : public Effect
        {
        public:
            explicit ReverbEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                const auto m = cfg.str ("modes", "Hall:hall");
                size_t i = 0;
                while (i <= m.size())
                {
                    auto j = m.find ('|', i); if (j == std::string::npos) j = m.size();
                    const auto tok = m.substr (i, j - i);
                    const auto c = tok.find (':');
                    if (! tok.empty()) types.push_back (c == std::string::npos ? tok : tok.substr (c + 1));
                    i = j + 1;
                }
                if (types.empty()) types.push_back ("hall");
                auto dc = cfg.list ("decay"); decLo = dc.size() > 0 ? dc[0] : 0.3; decHi = dc.size() > 1 ? dc[1] : 8.0;
                preMs = cfg.num ("predelay", 10);
                bw = cfg.num ("bw", 9000);
                pLevel = role (*this, "level", 0.4f);
                pTime = role (*this, "time", 0.5f);
                pTone = role (*this, "tone", 0.5f);
                pMode = role (*this, "mode", 0.0f);
                pPre = role (*this, "predelay", 0.2f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                static const int base[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };   // lunghezze primi-simili
                for (int k = 0; k < 8; ++k) { lines[k].allocate ((int) (base[k] * 3.2 * sr / 44100.0) + 64); len[k] = base[k] * (float) (sr / 44100.0); }
                pre[0].allocate ((int) (sr * 0.6)); pre[1].allocate ((int) (sr * 0.6));
                for (auto& a : diff) a.allocate ((int) (sr * 0.02));
                spring.prepare (sr);
                shimmer.prepare (sr, 90);
                reset();
            }
            void reset() override
            {
                for (auto& l : lines) l.clear();
                for (auto& p : pre) p.clear();
                for (auto& a : diff) a.clear();
                for (auto& z : damp) z = 0;
                spring.clear(); shimmer.clear();
                gateEnv = 0; hold = 0;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                const auto& type = types[(size_t) std::clamp (pMode.step (*this), 0, (int) types.size() - 1)];
                const float t60 = logMap (pTime.get (*this), (float) decLo, (float) decHi);
                float size = 1.0f, bright = 1.0f, modAmt = 0.0f;
                if (type == "room")     { size = 0.45f; bright = 0.8f; }
                if (type == "hall")     { size = 1.6f;  bright = 0.75f; modAmt = 4.0f; }
                if (type == "plate")    { size = 0.9f;  bright = 1.25f; }
                if (type == "modulate") { size = 1.3f;  bright = 0.9f; modAmt = 18.0f; }
                if (type == "lofi")     { size = 0.8f;  bright = 0.35f; }
                if (type == "shimmer" || type == "dynamic" || type == "delay") { size = 1.4f; modAmt = 6.0f; }
                const double cut = bw * bright * std::pow (0.2, 1.0 - pTone.get (*this));
                const float dampC = (float) std::exp (-2.0 * 3.14159265 * cut / sr);
                const float level = pLevel.get (*this) * 1.4f;
                const float preD = (float) ((pPre.present() ? pPre.get (*this) * 200.0 : preMs) * 0.001 * sr) + (type == "delay" ? (float) (0.3 * sr) : 0.0f);
                lfo.setRate (sr, 0.7);
                float g[8];
                for (int k = 0; k < 8; ++k)
                    g[k] = std::pow (10.0f, -3.0f * (len[k] * size) / (t60 * (float) sr));
                shimmer.setRatio (2.0f);

                for (int i = 0; i < n; ++i)
                {
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    float x = 0.5f * (inL + inR);
                    // pre-delay e diffusione d'ingresso (4 all-pass)
                    pre[0].push (x);
                    float v = pre[0].read (std::max (1.0f, preD));
                    static const float dl[4] = { 142, 107, 379, 277 };
                    for (int k = 0; k < 4; ++k)
                    {
                        const float dd = dl[k] * (float) (sr / 44100.0);
                        const float delayed = diff[k].read (dd);
                        const float y = -0.6f * v + delayed;
                        diff[k].push (v + 0.6f * y);
                        v = y;
                    }
                    float wetL, wetR;
                    if (type == "spring")
                    {
                        const float s = spring.tick (v * 0.6f, std::min (0.93f, 0.5f + 0.06f * t60), sr);
                        wetL = s; wetR = s;
                    }
                    else
                    {
                        // FDN 8x8 con matrice di Householder
                        const float m = lfo.next (1); lfo.advance();
                        float out[8], sum = 0;
                        for (int k = 0; k < 8; ++k)
                        {
                            const float mod = (k & 1) ? modAmt * m : -modAmt * m;
                            out[k] = lines[k].read (len[k] * size + mod);
                            damp[k] = out[k] + dampC * (damp[k] - out[k]);
                            out[k] = damp[k] * g[k];
                            sum += out[k];
                        }
                        const float h = sum * 0.25f;
                        float fbIn = v;
                        if (type == "shimmer") fbIn += 0.35f * shimmer.tick (0.5f * (out[0] + out[3]));
                        for (int k = 0; k < 8; ++k)
                            lines[k].push (out[k] - h + fbIn * ((k & 1) ? 0.35f : -0.35f));
                        wetL = out[0] + out[2] + out[4] + out[6];
                        wetR = out[1] + out[3] + out[5] + out[7];
                    }
                    // modi con inviluppo sul riverbero
                    const float e = std::abs (x);
                    if (type == "gate")
                    {
                        if (e > 0.02f) hold = (int) (0.28 * sr);
                        gateEnv += ((hold-- > 0 ? 1.0f : 0.0f) - gateEnv) * 0.002f;
                        wetL *= gateEnv; wetR *= gateEnv;
                    }
                    else if (type == "reverse")
                    {
                        if (e > 0.02f && hold <= 0) hold = (int) (0.45 * sr);
                        const float ramp = hold > 0 ? 1.0f - (float) hold / (float) (0.45 * sr) : 0.0f;
                        --hold;
                        wetL *= ramp * ramp; wetR *= ramp * ramp;
                    }
                    else if (type == "dynamic")
                    {
                        gateEnv = std::max (e, gateEnv * 0.9995f);
                        const float duck = 1.0f / (1.0f + 8.0f * gateEnv);
                        wetL *= duck; wetR *= duck;
                    }
                    ch[0][i] = inL + level * wetL * 0.5f;
                    if (numCh > 1) ch[1][i] = inR + level * wetR * 0.5f;
                    else ch[0][i] = inL + level * 0.25f * (wetL + wetR);
                }
            }
        private:
            Config cfg;
            std::vector<std::string> types;
            double decLo = 0.3, decHi = 8, preMs = 10, bw = 9000, sr = 48000;
            RoleParam pLevel, pTime, pTone, pMode, pPre;
            DelayLine lines[8], pre[2], diff[4];
            float len[8] {}, damp[8] {}, gateEnv = 0;
            int hold = 0;
            Spring spring;
            PitchShifter shimmer;
            LFO lfo;
        };

        //==============================================================================
        /** Eco a nastro a testine multiple (Space Echo) con molla, wow & flutter e saturazione. */
        class TapeEchoEffect : public Effect
        {
        public:
            explicit TapeEchoEffect (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                heads = cfg.list ("heads");
                if (heads.empty()) heads = { 1, 2, 3 };
                maxMs = cfg.num ("maxms", 600);
                minMs = cfg.num ("minms", maxMs / 4.0);
                bw = cfg.num ("bw", 5000);
                wow = (float) cfg.num ("wow", 0.002);
                const auto m = cfg.str ("modes", "100|010|001|110|011|111|101|100r|010r|001r|111r|000r");
                size_t i = 0;
                while (i <= m.size())
                {
                    auto j = m.find ('|', i); if (j == std::string::npos) j = m.size();
                    const auto tok = m.substr (i, j - i);
                    if (! tok.empty())
                    {
                        Mode md;
                        for (size_t k = 0; k < tok.size() && k < 4; ++k) md.head[k] = tok[k] == '1';
                        md.reverb = tok.find ('r') != std::string::npos;
                        modes.push_back (md);
                    }
                    i = j + 1;
                }
                if (modes.empty()) modes.push_back ({});
                pTime = role (*this, "time", 0.5f);
                pFeedback = role (*this, "feedback", 0.4f);
                pLevel = role (*this, "level", 0.5f);
                pReverb = role (*this, "reverb", 0.0f);
                pMode = role (*this, "mode", 0.0f);
                pSat = role (*this, "sat", 0.4f);
                pBass = role (*this, "bass", 0.5f);
                pTreble = role (*this, "treble", 0.5f);
                pWow = role (*this, "wow", -1.0f);
                pTape = role (*this, "tape", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = s;
                tape.allocate ((int) (sr * maxMs * 0.001 * 1.2) + 64);
                spring.prepare (sr);
                reset();
            }
            void reset() override { tape.clear(); spring.clear(); lp.reset(); hp.reset(); curSpeed = -1; fb = 0; }
            void process (float* const* ch, int numCh, int n) override
            {
                const auto& md = modes[(size_t) std::clamp (pMode.step (*this), 0, (int) modes.size() - 1)];
                // REPEAT RATE = velocita' del nastro: il ritardo della testina piu' lontana
                const float target = (float) (logMap (1.0f - pTime.get (*this), (float) minMs, (float) maxMs) * 0.001 * sr);
                if (curSpeed < 0) curSpeed = target;
                const float feedback = pFeedback.get (*this) * 1.1f;
                const float level = pLevel.get (*this) * 1.3f, rev = pReverb.get (*this);
                const float drive = 1.0f + 3.0f * pSat.get (*this);
                lp.set (sr, bw * std::pow (2.0, (pTreble.get (*this) - 0.5) * 2.0), 0.7);
                hp.set (sr, 90.0 * std::pow (2.0, -(pBass.get (*this) - 0.5) * 2.0), 0.7);
                wowLfo.setRate (sr, 0.55); flutLfo.setRate (sr, 6.1);
                const bool aged = pTape.step (*this) > 0;                 // nastro usurato: piu' wow e meno alte
                const float wowNow = (pWow.present() ? 0.0004f + 0.006f * pWow.get (*this) : wow) * (aged ? 1.8f : 1.0f);
                if (aged) lp.set (sr, bw * 0.65 * std::pow (2.0, (pTreble.get (*this) - 0.5) * 2.0), 0.7);
                const float hmax = (float) *std::max_element (heads.begin(), heads.end());
                for (int i = 0; i < n; ++i)
                {
                    curSpeed += (target - curSpeed) * 0.0002f;         // inerzia del motore
                    const float mod = 1.0f + wowNow * wowLfo.next (1) + 0.3f * wowNow * flutLfo.next (1);
                    wowLfo.advance(); flutLfo.advance();
                    float echo = 0;
                    int active = 0;
                    for (int h = 0; h < (int) std::min<size_t> (4, heads.size()); ++h)
                        if (md.head[h])
                        {
                            echo += tape.read (curSpeed * mod * (float) heads[(size_t) h] / hmax);
                            ++active;
                        }
                    if (active > 1) echo *= 0.8f;
                    float l, b, hh;
                    lp.tick (0, echo, l, b, hh);
                    float hl, hb, hhp;
                    hp.tick (0, l, hl, hb, hhp);
                    const float shaped = hhp;
                    const float x = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
                    // registrazione sul nastro con saturazione
                    tape.push (std::tanh (drive * (x + feedback * shaped)) / drive);
                    float out = level * shaped;
                    if (md.reverb) out += rev * 0.8f * spring.tick (x + out, 0.75f, sr);
                    for (int c = 0; c < numCh; ++c) ch[c][i] += out;
                }
            }
        private:
            struct Mode { bool head[4] { true, false, false, false }; bool reverb = false; };
            Config cfg;
            std::vector<double> heads;
            std::vector<Mode> modes;
            double maxMs = 600, minMs = 150, bw = 5000, sr = 48000;
            float wow = 0.002f, curSpeed = -1, fb = 0;
            RoleParam pTime, pFeedback, pLevel, pReverb, pMode, pSat, pBass, pTreble, pWow, pTape;
            DelayLine tape;
            Spring spring;
            SVF lp, hp;
            LFO wowLfo, flutLfo;
        };
    }

    std::unique_ptr<Effect> makeDigitalDelay (const ModelDef& d) { return std::make_unique<DigitalDelayEffect> (d); }
    std::unique_ptr<Effect> makeReverb (const ModelDef& d)       { return std::make_unique<ReverbEffect> (d); }
    std::unique_ptr<Effect> makeTapeEcho (const ModelDef& d)     { return std::make_unique<TapeEchoEffect> (d); }
}
