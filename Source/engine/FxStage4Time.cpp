/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Tappa 4 (Ibanez): delay, chorus e flanger digitali ("type=i4...").

    type=i4adm - delay digitali Ibanez/Maxon 1985-2005 a modulazione delta adattativa (ADM):
      sistema I.D.P.C. delle serie Master (1985) e Power (1986-1989), LSI Maxon dei DDL20/DML20 e
      dei DCP, chip Mitsubishi M50195 (DL10, DPL10, DL5), M50198 (DSC10, CF7), M65831 (EM5),
      M65850 (TC7) e il modulatore delta discreto del DE7 (NJM311 + integratore).
      Il tempo di ritardo si ottiene variando il CLOCK di una memoria fissa (bit della gamma):
      frequenza dei bit = bit / ritardo. Quindi, come nell'hardware, ai tempi lunghi i bit al
      secondo calano: piu' rumore granulare e piu' "slope overload" (acuti forti che si
      impastano), e girando D-TIME il suono scivola di intonazione (glissando del clock).
      Catena per campione:
        ingresso + retroazione -> limitazione morbida -> filtro anti-alias (Butterworth 4 poli)
        -> compressore 2:1 del compander (se c'e') -> codec ADM (CVSD a 3 bit di storia,
        adattamento sillabico, integratore con perdita, rumore del comparatore) alla frequenza
        dei bit reale (simulata fino a 32 bit per campione audio, oltre si scala il passo
        per conservare la pendenza massima) -> memoria -> espansore 1:2 -> filtro di
        ricostruzione -> blocco della continua -> uscita / anello.
      Chiavi di config:
        ranges=Nome:minms:maxms:bit[:flag+flag[:lfolo:lfohi]]|...   flag: hold mono anti ens
        bw=Hz (anti-alias e ricostruzione)  comp=0|1  ctau=ms (compander)
        fref=Hz slew=Hz idle=dB  (passo massimo = pendenza di un seno a fondo scala di 'slew'
          Hz alla frequenza dei bit 'fref'; passo minimo = massimo * 10^(idle/20))
        lfo=lo,hi (Hz)  mod=clamp|free  sweep=rapporto (solo free)
          clamp: D-TIME e LFO si sommano nell'esponente della gamma (escursione massima =
          rapporto della gamma, come lo sweep 8:1 / 4:1 dichiarato), free: ritardo * sweep^(w*lfo/2)
        hiss=dBV (fruscio dello stadio d'uscita, dal rumore dichiarato)  fbmax=  fbclip=V (diodi nell'anello)  fbtone=Hz (filtro dell'anello)
        mix=dry,wet  out=mono|inv|direct|pan|dual  color=lo,hi (filtro COLOR)
        altpos=k altbw=Hz altq= altclip=V altfb= altlfo=lo,hi (levetta 'alt': modo ECHO / KRAZY)
        presets=gamma:tempo:repeat:livello:width:speed|... (selettore SOUND dei DCP, 0 = manuale)
        fs=V (fondo scala del convertitore, volt di picco)  tc=ms (inerzia del clock)
      Ruoli: time level feedback mode rate depth tone balance alt preset.

    type=i4layer - LD303 Layer Delayer (2026, DSP moderno): ritardo a semiminima con prese a
      sedicesimi e terzine nella battuta successiva, TONE nastro/analogico/digitale nella
      registrazione, filtro ladder sul ramo wet, autopan, MIX. Nessuna quantizzazione d'epoca.
      Ruoli: mix res cutoff time feedback tone pan v16 v3 filter.

    Tutta la memoria si alloca in prepare(); nessuna allocazione nel thread audio.
    Ogni anello di retroazione ha guadagno <= 1 e una limitazione morbida prima di rientrare.
*/

#include "FxStage4.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        std::vector<std::string> splitText (const std::string& s, char sep)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (i <= s.size())
            {
                auto j = s.find (sep, i);
                if (j == std::string::npos) j = s.size();
                out.push_back (s.substr (i, j - i));
                i = j + 1;
            }
            return out;
        }

        inline float numAt (const std::vector<std::string>& v, size_t k, float fallback)
        {
            return k < v.size() && ! v[k].empty() ? (float) std::atof (v[k].c_str()) : fallback;
        }

        //==============================================================================
        /** Codec a modulazione delta adattativa (CVSD): comparatore, registro di 3 bit,
            passo sillabico, integratore con perdita. Il decodificatore e' la copia
            dell'integratore del codificatore, quindi l'uscita e' la sua scala media sul
            campione audio. Passi in unita' di fondo scala (1 = picco del convertitore). */
        struct AdmCodec
        {
            static constexpr float cap = 32.0f;          // bit simulati al massimo per campione audio
            float sMax = 0.02f, sMin = 2.0e-4f;          // passo per bit alla frequenza reale
            float tauS = 0.002f, tauL = 0.0015f;         // costanti sillabica e di perdita (s)
            float dither = 1.5e-4f;                      // rumore del comparatore
            float sr = 48000;
            float y = 0, s = 0, xPrev = 0, last = 0;
            int hist = 5;
            double ph = 0;
            Noise nz;
            uint32_t seed0 = 0x9E3779B9u;

            void setup (double sampleRate, double fref, double slewHz, double idleDb, uint32_t seed)
            {
                sr = (float) sampleRate;
                sMax = (float) (2.0 * juce::MathConstants<double>::pi * slewHz / std::max (1000.0, fref));
                sMin = sMax * (float) std::pow (10.0, idleDb / 20.0);
                seed0 = seed;
                reset();
            }
            /** Stato iniziale identico a ogni reset (anche il rumore del comparatore): uscita deterministica. */
            void reset() { y = 0; s = sMin; xPrev = 0; last = 0; hist = 5; ph = 0; nz.s = seed0; }

            /** x: ingresso (fondo scala +-1), bps: bit reali per campione audio. */
            inline float tick (float x, float bps) noexcept
            {
                float scale = 1.0f, rate = bps;
                if (bps > cap) { scale = bps / cap; rate = cap; }
                ph += rate;
                const int n = (int) ph;
                if (n <= 0) { xPrev = x; return last; }
                ph -= n;
                const float fBit = rate * sr;
                const float beta = 1.0f - 1.0f / (tauS * fBit);
                const float h = 1.0f - 1.0f / (tauL * fBit);
                const float sMx = sMax * scale, sMn = sMin * scale;
                float sc = std::max (s * scale, sMn);
                float acc = 0, xi = xPrev;
                const float dx = (x - xPrev) / (float) n;
                for (int k = 0; k < n; ++k)
                {
                    xi += dx;
                    const bool b = xi + dither * nz.uni() > y;
                    hist = ((hist << 1) | (b ? 1 : 0)) & 7;
                    const bool run = hist == 7 || hist == 0;
                    sc = beta * sc + (run ? (1.0f - beta) * sMx : 0.0f);
                    sc = std::max (sc, sMn);
                    y = h * y + (b ? sc : -sc);
                    y = std::clamp (y, -1.25f, 1.25f);
                    acc += y;
                }
                s = sc / scale;
                xPrev = x;
                last = acc / (float) n;
                return last;
            }
        };

        //==============================================================================
        /** Un chip di ritardo con i suoi filtri: anti-alias, compander, ADM, memoria, ricostruzione. */
        struct AdmVoice
        {
            Svf aa1, aa2, rc1, rc2, alt;
            OnePole dc, loopLp, colLp;
            AdmCodec codec;
            DelayLine line, gline;                       // memoria dei campioni e del guadagno del compressore
            float env = 0, envA = 0.999f, envFloor = 3.0e-4f;
            Noise hn;
            uint32_t hissSeed = 1;
            float hiss = 0;                              // fruscio dello stadio d'uscita (unita' di fondo scala)

            void prepare (double sr, int maxSamples, double bw, double ctauMs, double fref, double slew, double idle,
                          uint32_t seed, double fbTone, double altBw, double altQ, float hissUnits)
            {
                line.allocate (maxSamples);
                gline.allocate (maxSamples);
                // due Butterworth del 4° ordine in cascata: -3 dB complessivi alla banda dichiarata
                const double fc = 1.116 * bw;
                aa1.set (sr, fc, 0.5412); aa2.set (sr, fc, 1.3066);
                rc1.set (sr, fc, 0.5412); rc2.set (sr, fc, 1.3066);
                hiss = hissUnits; hissSeed = seed ^ 0x5bd1e995u;
                alt.set (sr, altBw > 0 ? altBw : bw, altQ > 0 ? altQ : 0.707);
                dc.set (sr, 18.0);
                loopLp.set (sr, fbTone > 0 ? fbTone : 0.45 * sr);
                envA = (float) std::exp (-1.0 / (std::max (0.1, ctauMs) * 0.001 * sr));
                codec.setup (sr, fref, slew, idle, seed);
                reset();
            }
            void reset()
            {
                line.clear(); gline.clear(); env = 0; hn.s = hissSeed;
                aa1.reset(); aa2.reset(); rc1.reset(); rc2.reset(); alt.reset();
                dc.reset(); loopLp.reset(); colLp.reset(); codec.reset();
            }
            /** Scrive un campione (fondo scala +-1) attraverso anti-alias, compressore e codec. */
            inline void write (float v, float bps, bool useComp) noexcept
            {
                v = aa2.lp (aa1.lp (v));
                if (useComp)
                {
                    // compressore 2:1 (NE571/SA571): guadagno = sqrt(rif / valor medio), tra -10 e +20 dB;
                    // il guadagno viaggia con il campione e l'espansore lo inverte esattamente (compander
                    // ideale: niente errori d'inseguimento che alzerebbero il guadagno d'anello)
                    env = std::abs (v) + envA * (env - std::abs (v));
                    const float g = std::clamp (std::sqrt (0.3f * 0.6366f / std::max (env, envFloor)), 0.316f, 10.0f);
                    gline.push (g);
                    v *= g;
                }
                line.push (codec.tick (std::clamp (v, -1.0f, 1.0f), bps));
            }
            /** HOLD: la memoria ricircola i bit gia' scritti, senza riconversione. */
            inline void recirculate (float len) noexcept { line.push (line.read (len)); gline.push (gline.read (len)); }
            inline float read (float dSamples, bool useComp, float altMix) noexcept
            {
                const float ds = std::max (1.0f, dSamples);
                float r = line.read (ds);
                if (useComp) r /= std::max (0.316f, gline.read (ds));     // espansore 1:2: il rumore del codec scende col segnale
                r += hiss * hn.gauss();
                r = rc2.lp (rc1.lp (r));
                r = dc.hp (r);
                // filtro del modo 'alt' sempre in funzione (stato pronto) e inserito con una dissolvenza
                const float a = alt.lp (r);
                r += altMix * (a - r);
                return r;
            }
        };

        inline float diodeClip (float x, float lv) noexcept { return lv * std::tanh (x / lv); }

        /** Limitazione dell'ingresso del convertitore: lineare fino a k del fondo scala, poi ginocchio
            morbido che non supera 1 (mai sopra l'identita': il guadagno d'anello resta <= 1). */
        inline float kneeSat (float x, float k = 0.8f) noexcept
        {
            const float a = std::abs (x);
            return a <= k ? x : std::copysign (k + (1.0f - k) * std::tanh ((a - k) / (1.0f - k)), x);
        }

        //==============================================================================
        class I4AdmDelay final : public Effect
        {
        public:
            enum Out { Mono, Inv, Direct, Pan, Dual };

            struct Range
            {
                float minMs = 20, maxMs = 400, bits = 65536, lfoLo = -1, lfoHi = -1;
                bool hold = false, mono = false, anti = false, ens = false;
            };
            struct Preset { int range = 0; float t = 0.5f, fb = 0.3f, lv = 0.5f, w = 0.0f, r = 0.3f; };

            explicit I4AdmDelay (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                for (const auto& tok : splitText (cfg.str ("ranges", "Delay:50:400:65536"), '|'))
                {
                    if (tok.empty()) continue;
                    const auto p = splitText (tok, ':');
                    Range r;
                    r.minMs = std::max (0.05f, numAt (p, 1, 50));
                    r.maxMs = std::max (r.minMs, numAt (p, 2, r.minMs));
                    r.bits = std::max (16.0f, numAt (p, 3, 65536));
                    if (p.size() > 4)
                    {
                        const auto& f = p[4];
                        r.hold = f.find ("hold") != std::string::npos;
                        r.mono = f.find ("mono") != std::string::npos;
                        r.anti = f.find ("anti") != std::string::npos;
                        r.ens = f.find ("ens") != std::string::npos;
                    }
                    r.lfoLo = numAt (p, 5, -1); r.lfoHi = numAt (p, 6, -1);
                    ranges.push_back (r);
                }
                if (ranges.empty()) ranges.push_back ({});
                for (const auto& tok : splitText (cfg.str ("presets", ""), '|'))
                {
                    if (tok.empty()) continue;
                    const auto p = splitText (tok, ':');
                    Preset q;
                    q.range = std::clamp ((int) numAt (p, 0, 0), 0, (int) ranges.size() - 1);
                    q.t = numAt (p, 1, 0.5f); q.fb = numAt (p, 2, 0.3f); q.lv = numAt (p, 3, 0.5f);
                    q.w = numAt (p, 4, 0.0f); q.r = numAt (p, 5, 0.3f);
                    presets.push_back (q);
                }
                bw = cfg.num ("bw", 7000);
                useComp = cfg.num ("comp", 0) > 0.5;
                ctau = cfg.num ("ctau", 15);
                fref = cfg.num ("fref", 500000);
                slew = cfg.num ("slew", 3000);
                idle = cfg.num ("idle", -40);
                auto lf = cfg.list ("lfo"); lfoLo = lf.size() > 0 ? (float) lf[0] : 0.1f; lfoHi = lf.size() > 1 ? (float) lf[1] : 8.0f;
                freeMod = cfg.str ("mod", "clamp") == "free";
                sweep = (float) std::max (1.0, cfg.num ("sweep", 2.0));
                fbMax = (float) std::clamp (cfg.num ("fbmax", 0.95), 0.0, 1.0);
                fbClip = (float) cfg.num ("fbclip", 0);
                fbTone = cfg.num ("fbtone", 0);
                auto mx = cfg.list ("mix"); dryMix = mx.size() > 0 ? (float) mx[0] : 1.0f; wetMix = mx.size() > 1 ? (float) mx[1] : 1.0f;
                const auto o = cfg.str ("out", "mono");
                out = o == "inv" ? Inv : o == "direct" ? Direct : o == "pan" ? Pan : o == "dual" ? Dual : Mono;
                auto cl = cfg.list ("color"); colLo = cl.size() > 0 ? (float) cl[0] : 1000.0f; colHi = cl.size() > 1 ? (float) cl[1] : 12000.0f;
                altPos = (int) cfg.num ("altpos", 1);
                altBw = cfg.num ("altbw", 0); altQ = cfg.num ("altq", 0.707);
                altClip = (float) cfg.num ("altclip", 0);
                altFb = (float) std::clamp (cfg.num ("altfb", -1.0), -1.0, 1.0);
                auto al = cfg.list ("altlfo"); altLfoLo = al.size() > 0 ? (float) al[0] : -1.0f; altLfoHi = al.size() > 1 ? (float) al[1] : -1.0f;
                fsV = (float) cfg.num ("fs", 1.5);
                hissDb = cfg.num ("hiss", -100);
                tcMs = cfg.num ("tc", 80);

                pTime = role (*this, "time", 0.5f); pLevel = role (*this, "level", 1.0f); pFb = role (*this, "feedback", 0.0f);
                pMode = role (*this, "mode", 0.0f); pRate = role (*this, "rate", 0.3f); pDepth = role (*this, "depth", 0.0f);
                pTone = role (*this, "tone", 1.0f); pBal = role (*this, "balance", 0.5f); pAlt = role (*this, "alt", 0.0f);
                pPreset = role (*this, "preset", 0.0f);
            }

            void prepare (double s, int) override
            {
                sr = (float) s;
                float maxMs = 1;
                for (const auto& r : ranges) maxMs = std::max (maxMs, r.maxMs * (freeMod ? std::sqrt (sweep) : 1.0f));
                const int maxSamples = (int) (s * maxMs * 0.001 * 1.05) + 64;
                const int nv = (out == Pan || out == Dual) ? 2 : 1;
                for (int v = 0; v < 2; ++v)
                {
                    // fruscio dichiarato (dBV nella banda) -> rumore bianco prima del filtro di ricostruzione
                    const double inBand = std::pow (10.0, hissDb / 20.0) / std::sqrt (std::min (1.0, 2.0 * bw / s));
                    voice[v].prepare (s, v < nv ? maxSamples : 64, bw, ctau, fref, slew, idle, 0x9E3779B9u + 7919u * (uint32_t) v,
                                      fbTone, altBw, altQ, (float) (inBand / fsV));
                }
                fadeStep = (float) (1.0 / (0.010 * s));
                tS.set (s, tcMs); fbS.set (s, 20); lvS.set (s, 20); wS.set (s, 40); rS.set (s, 60); colS.set (s, 30); balS.set (s, 20);
                altS.set (s, 10); clipS.set (s, 10);
                reset();
            }

            void reset() override
            {
                for (auto& v : voice) v.reset();
                lfo.ph = 0.25; fbState[0] = fbState[1] = 0; holdLen = 0; settle = 0; curKey = -1; fadeG = 1.0f;
                tS.init = fbS.init = lvS.init = wS.init = rS.init = colS.init = balS.init = altS.init = clipS.init = false;
            }

            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                // comandi (o memoria del selettore SOUND)
                const int pre = pPreset.present() ? pPreset.step (*this) : 0;
                int wantRange;
                float tK, fbK, lvK, wK, rK;
                if (pre > 0 && pre <= (int) presets.size())
                {
                    const auto& P = presets[(size_t) pre - 1];
                    wantRange = P.range; tK = P.t; fbK = P.fb; lvK = P.lv; wK = P.w; rK = P.r;
                }
                else
                {
                    wantRange = pMode.present() ? std::clamp (pMode.step (*this), 0, (int) ranges.size() - 1) : 0;
                    tK = pTime.get (*this); fbK = pFb.get (*this); lvK = pLevel.get (*this); wK = pDepth.get (*this); rK = pRate.get (*this);
                }
                const int key = wantRange * 64 + pre;
                const bool altOn = pAlt.present() && pAlt.step (*this) == altPos;
                const float fbLimit = altOn && altFb >= 0 ? altFb : fbMax;
                const float fbT = std::clamp (fbK, 0.0f, 1.0f) * fbLimit;
                const float lvT = std::clamp (lvK, 0.0f, 1.0f);
                const float colT = pTone.present() ? logMap (pTone.get (*this), colLo, colHi) : 0.0f;
                const float balT = pBal.get (*this);
                const float altT = altOn && altBw > 0 ? 1.0f : 0.0f, clipT = altOn && altClip > 0 ? 1.0f : 0.0f;

                for (int i = 0; i < n; ++i)
                {
                    // cambio di gamma/memoria: il wet scende in 10 ms, si cambia, 2 ms di silenzio (i filtri di
                    // ricostruzione assorbono il salto della lettura), poi risale in 10 ms (come il mute dei chip)
                    bool changed = false;
                    if (curKey < 0) { curKey = key; changed = true; }
                    else if (key != curKey && settle == 0)
                    {
                        fadeG -= fadeStep;
                        if (fadeG <= 0.0f) { fadeG = 0.0f; curKey = key; changed = true; settle = (int) (0.002f * sr); }
                    }
                    else if (settle > 0) --settle;
                    else if (fadeG < 1.0f) fadeG = std::min (1.0f, fadeG + fadeStep);
                    const float g = fadeG;
                    if (changed)
                    {
                        curRange = std::clamp (curKey / 64, 0, (int) ranges.size() - 1);
                        tS.snap (tK);
                        const Range& H = ranges[(size_t) curRange];
                        if (H.hold)   // il campione congelato e' lungo quanto il tempo della gamma HOLD
                            holdLen = std::max (2.0f, std::round (H.minMs * std::pow (H.maxMs / H.minMs, std::clamp (tK, 0.0f, 1.0f)) * 0.001f * sr));
                    }
                    const Range& R = ranges[(size_t) curRange];
                    // LFO del clock
                    float lo = lfoLo, hi = lfoHi;
                    if (R.lfoLo > 0) { lo = R.lfoLo; hi = R.lfoHi; }
                    if (altOn && altLfoLo > 0) { lo = altLfoLo; hi = altLfoHi; }
                    const float hz = logMap (rS.next (rK), lo, hi);
                    lfo.setHz (sr, hz);
                    const float w = wS.next (wK);
                    const float t = tS.next (tK);
                    const float ratio = R.maxMs / R.minMs;
                    auto delayMs = [&] (float lv) noexcept
                    {
                        if (freeMod)
                            return R.minMs * std::pow (ratio, t) * std::pow (sweep, 0.5f * w * lv);
                        return R.minMs * std::pow (ratio, std::clamp (t + 0.5f * w * lv, 0.0f, 1.0f));
                    };
                    const float useAlt = altS.next (altT), clipMix = clipS.next (clipT);
                    const float l1 = lfo.tri();
                    const float d1 = delayMs (w > 1.0e-4f ? l1 : 0.0f);
                    lfo.step();

                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR) / fsV;
                    const float fb = fbS.next (fbT);
                    const float d1s = d1 * 0.001f * sr;

                    float wet1, wet2 = 0.0f;
                    if (R.hold)
                    {
                        wet1 = voice[0].read (holdLen, useComp, useAlt);
                        voice[0].recirculate (holdLen);
                    }
                    else
                    {
                        wet1 = voice[0].read (d1s, useComp, useAlt);
                        if (clipMix > 0) wet1 += clipMix * (diodeClip (wet1, altClip) - wet1);
                        // la dissolvenza del cambio di gamma apre anche l'anello: nessun gradino scritto in memoria
                        float f1 = (out == Pan ? fbState[1] : wet1) * fb * g;
                        if (fbTone > 0) f1 = voice[0].loopLp.lp (f1);
                        if (fbClip > 0) f1 = diodeClip (f1, fbClip);
                        voice[0].write (kneeSat (x + f1), R.bits / (d1 * 0.001f) / sr, useComp);

                        if (out == Pan)
                        {
                            // secondo chip: riprende l'eco del primo (ping-pong; in mono e' il ritardo doppio)
                            wet2 = voice[1].read (d1s, useComp, useAlt);
                            fbState[1] = wet2;
                            voice[1].write (kneeSat (wet1 * g), R.bits / (d1 * 0.001f) / sr, useComp);
                        }
                        else if (out == Dual)
                        {
                            const double off = R.anti ? 0.5 : R.ens ? 0.25 : 0.0;
                            const float d2 = delayMs (w > 1.0e-4f ? lfo.tri (off) : 0.0f);
                            wet2 = voice[1].read (d2 * 0.001f * sr, useComp, useAlt);
                            voice[1].write (kneeSat (x + wet2 * fb * g), R.bits / (d2 * 0.001f) / sr, useComp);
                        }
                    }

                    // livelli d'uscita
                    float wetG = wetMix * g * (pLevel.present() || pre > 0 ? lvS.next (lvT) : 1.0f);
                    float dryG = dryMix;
                    if (pBal.present())
                    {
                        const float b = balS.next (balT);
                        dryG = std::min (1.0f, 2.0f * (1.0f - b));
                        wetG *= std::min (1.0f, 2.0f * b);
                    }
                    if (pTone.present())
                    {
                        const float fc = colS.next (colT);
                        voice[0].colLp.setFast (sr, fc); voice[1].colLp.setFast (sr, fc);
                        wet1 = voice[0].colLp.lp (wet1);
                        if (out == Pan || out == Dual) wet2 = voice[1].colLp.lp (wet2);
                    }
                    const float w1 = wet1 * fsV * wetG, w2 = wet2 * fsV * wetG;
                    float oL, oR;
                    switch (out)
                    {
                        case Inv:    oL = dryG * inL + w1; oR = dryG * inR - w1; break;
                        case Direct: oL = dryG * inL + w1; oR = dryG * inR; break;
                        case Pan:    oL = dryG * inL + w1; oR = dryG * inR + w2; break;
                        case Dual:
                            if (R.mono)      { oL = dryG * inL + w1; oR = dryG * inR; }
                            else if (R.ens)  { oL = dryG * inL + 0.8f * (w1 + 0.5f * w2); oR = dryG * inR + 0.8f * (w2 + 0.5f * w1); }
                            else             { oL = dryG * inL + w1; oR = dryG * inR + w2; }
                            break;
                        case Mono:
                        default:     oL = dryG * inL + w1; oR = dryG * inR + w1; break;
                    }
                    if (numCh > 1) { ch[0][i] = oL; ch[1][i] = oR; }
                    else ch[0][i] = out == Pan ? dryG * inL + w2 : oL;    // DPL10 in mono: ritardo doppio
                }
            }

        private:
            Config cfg;
            std::vector<Range> ranges;
            std::vector<Preset> presets;
            double hissDb = -100, bw = 7000, ctau = 15, fref = 500000, slew = 3000, idle = -40, fbTone = 0, altBw = 0, altQ = 0.707, tcMs = 80;
            bool useComp = false, freeMod = false;
            float sr = 48000, lfoLo = 0.1f, lfoHi = 8.0f, sweep = 2.0f, fbMax = 0.95f, fbClip = 0, dryMix = 1, wetMix = 1,
                  colLo = 1000, colHi = 12000, altClip = 0, altFb = -1, altLfoLo = -1, altLfoHi = -1, fsV = 1.5f;
            int altPos = 1, curRange = 0, settle = 0, curKey = -1;
            float fadeG = 1.0f, fadeStep = 0.002f;
            Out out = Mono;
            float fbState[2] {}, holdLen = 0;
            RoleParam pTime, pLevel, pFb, pMode, pRate, pDepth, pTone, pBal, pAlt, pPreset;
            AdmVoice voice[2];
            Lfo lfo;
            Smooth tS, fbS, lvS, wS, rS, colS, balS, altS, clipS;
        };

        //==============================================================================
        /** Filtro ladder a 4 poli (TPT a un polo per stadio, retroazione con un campione di ritardo e tanh). */
        struct Ladder
        {
            float s[4] {}, y3 = 0;
            void reset() { for (auto& v : s) v = 0; y3 = 0; }
            inline float tick (float x, float G, float k) noexcept
            {
                float u = std::tanh (x - k * y3);
                for (auto& st : s)
                {
                    const float v = (u - st) * G;
                    const float y = v + st;
                    st = y + v;
                    u = y;
                }
                y3 = u;
                return u * (1.0f + 0.5f * k);
            }
        };

        /** LD303 Layer Delayer: semiminima + prese a sedicesimi e terzine nella battuta successiva. */
        class I4Layer final : public Effect
        {
        public:
            explicit I4Layer (const ModelDef& d) : Effect (d), cfg (d.config)
            {
                auto tm = cfg.list ("time"); tLo = tm.size() > 0 ? (float) tm[0] : 40.0f; tHi = tm.size() > 1 ? (float) tm[1] : 1500.0f;
                pMix = role (*this, "mix", 0.5f); pRes = role (*this, "res", 0.0f); pCut = role (*this, "cutoff", 1.0f);
                pTime = role (*this, "time", 0.5f); pFb = role (*this, "feedback", 0.3f); pTone = role (*this, "tone", 1.0f);
                pPan = role (*this, "pan", 0.0f); pV16 = role (*this, "v16", 0.0f); pV3 = role (*this, "v3", 0.0f);
                pFilt = role (*this, "filter", 0.0f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                line.allocate ((int) (s * (tHi * 0.001 * 1.8 * 1.01)) + 64);
                tS.set (s, 120); mS.set (s, 20); fbS.set (s, 20); toS.set (s, 30); cS.set (s, 25); rsS.set (s, 25);
                g16S.set (s, 20); g3S.set (s, 20); panS.set (s, 40); fS.set (s, 15);
                wow.setHz (s, 0.55); flut.setHz (s, 6.8);
                reset();
            }
            void reset() override
            {
                line.clear(); ladder.reset(); lp.reset(); hp.reset();
                wow.ph = flut.ph = 0; panL.ph = 0;
                tS.init = mS.init = fbS.init = toS.init = cS.init = rsS.init = g16S.init = g3S.init = panS.init = fS.init = false;
            }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float tT = std::log (logMap (pTime.get (*this), tLo, tHi));
                const float mT = pMix.get (*this);
                const float fbT = std::min (1.0f, pFb.get (*this));
                const float toneT = pTone.get (*this);
                const float cutT = std::log (logMap (pCut.get (*this), 80.0f, 16000.0f));
                const float resT = 3.6f * pRes.get (*this);
                const float g16T = volKnob (pV16.get (*this), 0.75f, 1.413f), g3T = volKnob (pV3.get (*this), 0.75f, 1.413f);
                const float panT = pPan.get (*this);
                const float fOn = pFilt.step (*this) == 1 ? 1.0f : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float T = std::exp (tS.next (tT));
                    const float tone = toS.next (toneT);
                    const float tape = std::clamp (1.0f - 2.0f * tone, 0.0f, 1.0f), dig = std::clamp (2.0f * tone - 1.0f, 0.0f, 1.0f);
                    const float ana = 1.0f - tape - dig;
                    // TONE: nastro (scuro, saturo, wow e flutter) -> analogico (BBD) -> digitale (pieno)
                    const float lpHz = std::exp (tape * std::log (3200.0f) + ana * std::log (2600.0f) + dig * std::log (16000.0f));
                    const float hpHz = 90.0f * tape + 60.0f * ana + 25.0f * dig;
                    const float drive = 1.6f * tape + 1.25f * ana + 1.0f * dig;
                    lp.setFast (sr, lpHz); hp.setFast (sr, hpHz);
                    const float mod = 1.0f + tape * (0.0025f * wow.sine() + 0.0006f * flut.sine());
                    wow.step(); flut.step();
                    const float d = T * 0.001f * sr * mod;
                    const float t1 = line.read (d);
                    const float s2 = line.read (1.25f * d), s3 = line.read (1.5f * d), s4 = line.read (1.75f * d);
                    const float r2 = line.read (d * (4.0f / 3.0f)), r3 = line.read (d * (5.0f / 3.0f));
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    // registrazione: ingresso + retroazione della prima presa, con il colore del TONE
                    float v = x + fbS.next (fbT) * t1;
                    v = 1.5f * kneeSat (v / 1.5f);     // binari del DSP: lineare fino a 1,2 V
                    v = lp.lp (v); v = hp.hp (v);
                    if (drive > 1.001f) v = softSat (v * drive) / drive;
                    line.push (v);
                    float wet = t1 + g16S.next (g16T) * (s2 + s3 + s4) + g3S.next (g3T) * (r2 + r3);
                    // filtro ladder sul ramo wet (click del CUT OFF = acceso/spento, con dissolvenza)
                    const float fon = fS.next (fOn);
                    if (fon > 1.0e-4f)
                    {
                        const float G = [&] { const float g = fastTanPi (std::exp (cS.next (cutT)), sr); return g / (1.0f + g); }();
                        const float f = ladder.tick (wet, G, rsS.next (resT));
                        wet += fon * (f - wet);
                    }
                    else { cS.next (cutT); rsS.next (resT); }
                    // autopan del wet: PAN SPEED a zero = fermo al centro
                    const float pk = panS.next (panT);
                    const float depth = std::clamp ((pk - 0.02f) / 0.05f, 0.0f, 1.0f);
                    panL.setHz (sr, logMap (pk, 0.1f, 10.0f));
                    const float p = 0.25f + 0.25f * depth * panL.sine();
                    panL.step();
                    const float gL = 1.41421356f * std::cos (pi * p), gR = 1.41421356f * std::sin (pi * p);
                    // MIX: al centro dry e wet pieni, agli estremi solo uno dei due
                    const float mm = mS.next (mT);
                    const float wg = std::min (1.0f, 2.0f * mm), dg = std::min (1.0f, 2.0f * (1.0f - mm));
                    if (numCh > 1)
                    {
                        ch[0][i] = dg * inL + wg * gL * wet;
                        ch[1][i] = dg * inR + wg * gR * wet;
                    }
                    else ch[0][i] = dg * inL + wg * wet;
                }
            }
        private:
            Config cfg;
            float sr = 48000, tLo = 40, tHi = 1500;
            RoleParam pMix, pRes, pCut, pTime, pFb, pTone, pPan, pV16, pV3, pFilt;
            DelayLine line;
            Ladder ladder;
            OnePole lp, hp;
            Lfo wow, flut, panL;
            Smooth tS, mS, fbS, toS, cS, rsS, g16S, g3S, panS, fS;
        };
    }

    std::unique_ptr<Effect> makeI4Time (const ModelDef& d, const std::string& type)
    {
        if (type == "i4adm")   return std::make_unique<I4AdmDelay> (d);
        if (type == "i4layer") return std::make_unique<I4Layer> (d);
        return nullptr;
    }
}
