/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Multi-effetto della tappa 3B (parte analogica col motore Circuit, parte digitale coi
    mattoni della tappa 3):
      * b3battalion - Battalion: compressore, distorsione MOSFET (netlist) con BLEND, EQ a 4
                      bande in tre posizioni (PRE/POST/DRY EQ), gate in fondo alla catena;
      * b3bassmeta  - Bass Metaphors: canali paralleli dry / EQ additivo a 2 poli /
                      distorsione, compressore livellatore SQUASH;
      * b3analogizer- The Analogizer: stadio di guadagno fino a +26 dB e BBD cortissimo
                      (3,5-65 ms) miscelato al diretto;
      * b3epitome   - Epitome: Micro POG -> Stereo Electric Mistress -> Holy Grail Plus, con
                      SHIMMER (riverbero solo sulle ottave, eco con il POG nell'anello);
      * b3soulpog   - Soul POG: Soul Food (netlist) + Nano POG con ordine commutabile;
      * b3tonetattoo- Tone Tattoo: Metal Muff (netlist) con gate -> Neo Clone -> Memory Toy.
*/

#include "FxClassicB.h"

namespace pt::engine
{
    namespace
    {
        using namespace cx;

        /** Gate a soglia con rilevatore separato (attacco 1 ms, rilascio regolabile). */
        struct GateB
        {
            Envelope det;
            float g = 1, rel = 0.999f, att = 0.9f;
            void prepare (double sr, double relMs = 80) { det.set (sr, 1, 20); rel = (float) std::exp (-1.0 / (relMs * 0.001 * sr)); att = (float) std::exp (-1.0 / (0.001 * sr)); }
            inline float tick (float detIn, float thr) noexcept
            {
                const float e = det.tick (detIn);
                const float tgt = thr <= 0.0f || e > thr ? 1.0f : 0.0f;
                g = tgt > g ? tgt + att * (g - tgt) : tgt + rel * (g - tgt);
                return g;
            }
        };

        /** Compressore a valore efficace con guadagno di compensazione. */
        struct CompB
        {
            float env = 0, a = 0.99f, r = 0.999f;
            void prepare (double sr, double atkMs = 5, double relMs = 120) { a = (float) std::exp (-1.0 / (atkMs * 0.001 * sr)); r = (float) std::exp (-1.0 / (relMs * 0.001 * sr)); }
            inline float tick (float x, float thr, float ratio, float makeup) noexcept
            {
                const float p = x * x;
                env = p > env ? p + a * (env - p) : p + r * (env - p);
                const float lvl = std::sqrt (env + 1.0e-12f);
                const float g = lvl > thr ? std::pow (lvl / thr, 1.0f / ratio - 1.0f) : 1.0f;
                return x * g * makeup;
            }
        };

        /** Dissolvenza a blocchi per i cambi d'ordine della catena. */
        struct BlockFade
        {
            int cur = -1, want = -1;
            float g = 1;
            /** Restituisce il guadagno a inizio e fine blocco; aggiorna 'cur' quando il guadagno arriva a zero. */
            void begin (int w, float& g0, float& g1) noexcept
            {
                if (cur < 0) cur = w;
                want = w;
                g0 = g;
                if (want != cur) { g = 0.0f; }
                else g = std::min (1.0f, g + 0.5f);
                g1 = g;
            }
            void end() noexcept { if (g <= 0.0f) cur = want; }
        };

        //==============================================================================
        /** Battalion Bass Preamp + DI. */
        class B3Battalion final : public Effect
        {
        public:
            explicit B3Battalion (const ModelDef& d) : Effect (d)
            {
                pVol = role (*this, "volume", 0.7f); pBass = role (*this, "bass", 0.5f); pLoMid = role (*this, "lomid", 0.5f);
                pHiMid = role (*this, "himid", 0.5f); pTreb = role (*this, "treble", 0.5f); pComp = role (*this, "comp", 0.0f);
                pGate = role (*this, "gate", 0.0f); pLevel = role (*this, "level", 0.5f); pBlend = role (*this, "blend", 0.5f);
                pDrive = role (*this, "drive", 0.4f); pTone = role (*this, "tone", 0.5f); pOrder = role (*this, "order", 0.0f);
                dist.init ("hpf R=1M C=47n; opni Rg=4.7k Cg=1u Rf=pot(0,1M,A)+22k Cf=47p rail=4.3; dclip R=2.2k C=0 d=mos; "
                           "lpf R=potr(1,47k,B)+1.5k C=6.8n; hpf R=100k C=100n", { 0.4f, 0.5f });
            }
            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; mb = std::max (16, maxBlock);
                dist.prepare (s, mb);
                buf.assign ((size_t) mb, 0.0f); eqd.assign ((size_t) mb, 0.0f);
                gate.prepare (s, 80); comp.prepare (s, 5, 150);
                eq[0].prepare (s, SmoothEq::LowShelf, 200, 0.7); eq[1].prepare (s, SmoothEq::Peak, 280, 0.9);
                eq[2].prepare (s, SmoothEq::Peak, 750, 0.9); eq[3].prepare (s, SmoothEq::HighShelf, 2000, 0.7);
                for (auto* sm : { &volS, &lvlS, &blS, &cS }) sm->set (s, 20);
                reset();
            }
            void reset() override { dist.reset(); for (auto& f : eq) f.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int o = 0; o < n; o += mb) processChunk (ch, numCh, o, std::min (mb, n - o));
            }
        private:
            void processChunk (float* const* ch, int numCh, int off, int n)
            {
                juce::ScopedNoDenormals nd;
                setEq();
                for (int k = 0; k < 2; ++k) cp[k] += 0.3f * ((k == 0 ? pDrive.get (*this) : pTone.get (*this)) - cp[k]);
                dist.set (0, cp[0]); dist.set (1, cp[1]);
                const float ckT = pComp.get (*this);
                const float gk = pGate.get (*this);
                const float gThr = gk < 0.02f ? 0.0f : 0.0003f * std::pow (300.0f, gk);
                const float lvl = 2.0f * taperA (pLevel.get (*this)) * 2.0f, vol = 2.0f * taperA (pVol.get (*this)) / 0.96f;
                float f0, f1;
                fade.begin (std::clamp (pOrder.step (*this), 0, 2), f0, f1);
                const int order = fade.cur;                         // 0 PRE EQ, 1 POST EQ, 2 DRY EQ
                // 1) ingresso, compressore e (POST EQ) equalizzatore prima della distorsione
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, off + i);
                    const float ck = cS.next (ckT);                                            // COMPRESSOR: rapporto 1:1 .. 10:1, a zero escluso
                    const float ratio = 1.0f + 9.0f * ck, thr = 0.12f - 0.1f * ck;
                    const float makeup = std::pow (0.12f / thr, 1.0f - 1.0f / ratio) * 0.8f + 0.2f;
                    const float cw = std::min (1.0f, ck * 50.0f);
                    const float v = (1.0f - cw) * x + cw * comp.tick (x, thr, ratio, makeup);
                    eqd[(size_t) i] = order == 1 ? outRail (eqTick (v)) : v;
                    buf[(size_t) i] = eqd[(size_t) i];
                }
                // 2) distorsione MOSFET (netlist, sovracampionata)
                dist.process (buf.data(), n);
                // 3) BLEND, EQ dopo (PRE EQ) o solo sul pulito (DRY EQ), gate e volume
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, off + i);
                    const float clean = eqd[(size_t) i], dd = buf[(size_t) i] * lvlS.next (lvl);
                    const float b = blS.next (pBlend.get (*this));
                    float y;
                    if (order == 0) y = outRail (eqTick ((1.0f - b) * clean + b * dd));     // binari degli stadi attivi
                    else if (order == 1) y = (1.0f - b) * clean + b * dd;
                    else y = (1.0f - b) * outRail (eqTick (clean)) + b * dd;
                    y *= gate.tick (x, gThr);
                    const float fg = f0 + (f1 - f0) * (float) i / (float) n;
                    y = outRail (y * volS.next (vol) * fg);
                    for (int c = 0; c < numCh; ++c) ch[c][off + i] = y;
                }
                fade.end();
            }
            /** EQ a 4 bande (+-15 dB) con guadagni lisciati. */
            inline float eqTick (float v) noexcept { return eq[3].tick (eq[2].tick (eq[1].tick (eq[0].tick (v, eqDb[0]), eqDb[1]), eqDb[2]), eqDb[3]); }
            void setEq()
            {
                eqDb[0] = (pBass.get (*this) - 0.5f) * 30.0f; eqDb[1] = (pLoMid.get (*this) - 0.5f) * 30.0f;
                eqDb[2] = (pHiMid.get (*this) - 0.5f) * 30.0f; eqDb[3] = (pTreb.get (*this) - 0.5f) * 30.0f;
            }
            float sr = 48000, eqDb[4] {}, cp[2] { 0.4f, 0.5f };
            int mb = 512;
            RoleParam pVol, pBass, pLoMid, pHiMid, pTreb, pComp, pGate, pLevel, pBlend, pDrive, pTone, pOrder;
            SubCircuit dist;
            std::vector<float> buf, eqd;
            SmoothEq eq[4];
            GateB gate;
            CompB comp;
            BlockFade fade;
            Smooth volS, lvlS, blS, cS;
        };

        //==============================================================================
        /** Bass Metaphors: dry / EQ / distorsione in parallelo, poi compressore SQUASH. */
        class B3BassMeta final : public Effect
        {
        public:
            explicit B3BassMeta (const ModelDef& d) : Effect (d)
            {
                pDry = role (*this, "direct", 0.6f); pEq = role (*this, "eqlevel", 0.4f); pTreb = role (*this, "treble", 0.3f);
                pBass = role (*this, "bass", 0.3f); pDist = role (*this, "dist", 0.0f); pVol = role (*this, "volume", 0.6f);
                pSquash = role (*this, "squash", 0.0f);
                dist.init ("hpf R=1M C=100n; opni Rg=2.2k Cg=1u Rf=220k Cf=100p rail=4.3; dclip R=2.2k C=10n d=si; lpf R=10k C=4.7n; hpf R=100k C=47n", {});
            }
            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; mb = std::max (16, maxBlock);
                dist.prepare (s, mb);
                buf.assign ((size_t) mb, 0.0f);
                lp1.set (s, 120, 0.707); hp1.set (s, 3000, 0.707);
                comp.prepare (s, 8, 250);
                for (auto* sm : { &dS, &eS, &tS, &bS, &xS, &vS, &qS }) sm->set (s, 20);
                reset();
            }
            void reset() override { dist.reset(); lp1.reset(); hp1.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int o = 0; o < n; o += mb)
                {
                    const int m = std::min (mb, n - o);
                    for (int i = 0; i < m; ++i) buf[(size_t) i] = monoIn (ch, numCh, o + i);
                    dist.process (buf.data(), m);
                    run (ch, numCh, o, m);
                }
            }
        private:
            void run (float* const* ch, int numCh, int off, int n)
            {
                juce::ScopedNoDenormals nd;
                const bool squash = pSquash.step (*this) == 1;
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, off + i);
                    // EQ additivo: acuti (passa-alto 2 poli) e bassi (passa-basso 2 poli) aggiunti da 0 a +12 dB
                    const float eqc = x + 3.0f * tS.next (pTreb.get (*this)) * hp1.hp (x) + 3.0f * bS.next (pBass.get (*this)) * lp1.lp (x);
                    float y = 2.0f * taperA (dS.next (pDry.get (*this))) * x + 2.0f * taperA (eS.next (pEq.get (*this))) * eqc
                            + 3.0f * taperA (xS.next (pDist.get (*this))) * buf[(size_t) i];
                    const float q = qS.next (squash ? 1.0f : 0.0f);
                    y = outRail (y);
                    y = (1.0f - q) * y + q * comp.tick (y, 0.05f, 6.0f, 3.0f);
                    y = outRail (y * volKnob (vS.next (pVol.get (*this)), 0.6f, 3.0f));
                    for (int c = 0; c < numCh; ++c) ch[c][off + i] = y;
                }
            }
            float sr = 48000;
            int mb = 512;
            RoleParam pDry, pEq, pTreb, pBass, pDist, pVol, pSquash;
            SubCircuit dist;
            std::vector<float> buf;
            Svf lp1, hp1;
            CompB comp;
            Smooth dS, eS, tS, bS, xS, vS, qS;
        };

        //==============================================================================
        /** The Analogizer: guadagno + BBD cortissimo miscelato al diretto. */
        class B3Analogizer final : public Effect
        {
        public:
            explicit B3Analogizer (const ModelDef& d) : Effect (d)
            {
                pVol = role (*this, "volume", 0.6f); pSpread = role (*this, "spread", 0.3f); pBlend = role (*this, "blend", 0.5f);
                pGain = role (*this, "gain", 0.3f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                line.prepare (s, 4096, 7000, 0.0003f, true, false, 1.3f, 2.0e-5f);
                for (auto* sm : { &vS, &bS, &gS }) sm->set (s, 20);
                dS.set (s, 60);
                reset();
            }
            void reset() override { line.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const float g = std::pow (10.0f, 26.0f * pGain.get (*this) / 20.0f);      // 0..+26 dB
                const float dMs = logMap (pSpread.get (*this), 3.5f, 65.0f);
                const float vol = volKnob (pVol.get (*this), 0.6f, 3.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, i);
                    // stadio di guadagno: lineare ai livelli bassi, saturazione morbida asimmetrica ad alti livelli
                    const float z = gS.next (g) * x;
                    const float v = 2.2f * std::tanh ((z + 0.08f * z * z) / 2.2f);
                    const float w = line.tick (v, dS.next (dMs));
                    const float b = bS.next (pBlend.get (*this));
                    const float y = outRail (vS.next (vol) * ((1.0f - b) * v + b * w) / std::max (1.0f, 0.5f * gS.y));
                    for (int c = 0; c < numCh; ++c) ch[c][i] = y;
                }
            }
        private:
            float sr = 48000;
            RoleParam pVol, pSpread, pBlend, pGain;
            BbdChain line;
            Smooth vS, bS, gS, dS;
        };

        //==============================================================================
        /** Epitome: Micro POG + Stereo Electric Mistress + Holy Grail Plus. */
        class B3Epitome final : public Effect
        {
        public:
            explicit B3Epitome (const ModelDef& d) : Effect (d)
            {
                pBlend = role (*this, "blend", 0.4f); pAmount = role (*this, "amount", 0.5f); pShim = role (*this, "shimmer", 0.0f);
                pRev = role (*this, "mode", 1.0f / 3.0f); pRate = role (*this, "rate", 0.5f); pFl = role (*this, "flanger", 0.0f);
                pCh = role (*this, "chorus", 0.0f); pSub = role (*this, "oct1", 0.0f); pUp = role (*this, "up1", 0.0f);
                pDry = role (*this, "direct", 0.8f);
            }
            void prepare (double s, int) override
            {
                sr = (float) s;
                bank.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 1);
                bankFb.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 1);
                mist.prepare (s);
                core.prepare (s);
                echo.allocate ((int) (1.1 * s));
                fader.prepare (s, 15);
                for (auto* sm : { &blS, &flS, &chS, &subS, &upS, &dryS, &tS }) sm->set (s, 20);
                routeS.set (s, 2);
                reset();
            }
            void reset() override { bank.reset(); bankFb.reset(); mist.reset(); core.clear(); echo.clear(); eFb = 0; logT = -100; }
            void process (float* const* ch, int numCh, int n) override
            {
                juce::ScopedNoDenormals nd;
                const int rv = std::clamp (pRev.step (*this), 0, 3);                         // SPRING, HALL, ROOM, FLERB
                const bool shim = pShim.step (*this) == 1;
                const bool echoMode = shim && rv == 3;
                const float am = pAmount.get (*this), bl = pBlend.get (*this);
                // Holy Grail Plus: AMOUNT per modo (come la tappa 3A)
                static const VerbCore::Algo al[4] = { VerbCore::GSpring, VerbCore::Hall, VerbCore::Room, VerbCore::Flerb };
                const VerbCore::Algo a = al[rv];
                float t60 = 2.6f, tone = 6500, pa = 0.5f, pb = 0.5f;
                if (a == VerbCore::GSpring) { t60 = logMap (am, 0.6f, 5.0f); tone = 4200; }
                else if (a == VerbCore::Hall) t60 = logMap (am, 0.8f, 8.0f);
                else if (a == VerbCore::Room) { t60 = 1.1f; tone = logMap (1.0f - am, 1500.0f, 11000.0f); }
                else { pa = am; pb = am; }
                const float lt = std::log (t60);
                logT = logT < -50.0f ? lt : lt + 0.82f * (logT - lt);
                const int key = (echoMode ? 100 : 0) + (int) a * 2 + (shim ? 1 : 0);
                const float echoMs = logMap (am, 60.0f, 1000.0f);
                for (int i = 0; i < n; ++i)
                {
                    bool changed;
                    const float fg = fader.tick (key, changed);
                    if (changed) { core.clear(); echo.clear(); eFb = 0; }
                    if ((i == 0 && fader.current == key) || changed)
                        if (fader.current == key && fader.current < 100) core.set (a, std::exp (logT), tone, 0.0f, 0.0f, pa, pb, 0);
                    const float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
                    const float x = 0.5f * (inL + inR);
                    bank.push (x);
                    const float sub = subS.next (sliderGain (pSub.get (*this))), up = upS.next (sliderGain (pUp.get (*this))), dk = dryS.next (pDry.get (*this));
                    const int nums[2] = { 1, 2 }, levs[2] = { 1, 0 };
                    const float gains[2] = { sub, up };
                    const float oct = bank.voices (nums, levs, gains, 2) * kBankGain;
                    const float dryG = sliderGain (dk);
                    const float b = blS.next (bl);
                    float l, r;
                    const bool shimCur = fader.current < 100 && (fader.current & 1) == 1;
                    if (fader.current >= 100)
                    {
                        // SHIMMER + FLERB = eco col POG nell'anello: ogni ripetizione cambia ottava; DRY = feedback del dry
                        const float y = echo.read (echoMs * 0.001f * sr);
                        bankFb.push (y);
                        const float shifted = bankFb.voices (nums, levs, gains, 2) * kBankGain;
                        float fbIn = 0.9f * dk * y + 0.9f * shifted;
                        const float tot = 0.9f * dk + 0.9f * (sub + up);
                        if (tot > 0.95f) fbIn *= 0.95f / tot;                                 // guadagno d'anello <= 0,95
                        echo.push (x + oct + loopSat (fbIn));
                        const float pre = x + oct + b * 1.3f * y * fg;                       // in ECO il DRY e' il feedback del dry
                        mist.tick (pre, pRate.get (*this), 0.33f, flS.next (pFl.get (*this)), chS.next (pCh.get (*this)), 0.8f, 1.0f, l, r);
                    }
                    else if (shimCur)
                    {
                        // SHIMMER: il riverbero prende solo le ottave, poi la Mistress
                        float wl, wr;
                        core.tick (oct, wl, wr);
                        const float pre = dryG * x + (1.0f - 0.5f * b) * oct + 1.3f * b * 0.5f * (wl + wr) * fg;
                        mist.tick (pre, pRate.get (*this), 0.33f, flS.next (pFl.get (*this)), chS.next (pCh.get (*this)), 0.8f, 1.0f, l, r);
                    }
                    else
                    {
                        const float pog = dryG * x + oct;
                        float ml, mr;
                        mist.tick (pog, pRate.get (*this), 0.33f, flS.next (pFl.get (*this)), chS.next (pCh.get (*this)), 0.8f, 1.0f, ml, mr);
                        float wl, wr;
                        core.tick (0.5f * (ml + mr), wl, wr);
                        l = (1.0f - b) * ml + 1.3f * b * wl * fg; r = (1.0f - b) * mr + 1.3f * b * wr * fg;
                    }
                    const float rg = routeS.next (fg);                                         // i cambi di instradamento passano per un breve silenzio
                    l *= rg; r *= rg;
                    if (numCh > 1) { ch[0][i] = outRail (l); ch[1][i] = outRail (r); }
                    else ch[0][i] = outRail (0.5f * (l + r));
                }
            }
        private:
            static float sliderGain (float s) noexcept { return 1.56f * s * s; }
            float sr = 48000, eFb = 0, logT = -100;
            RoleParam pBlend, pAmount, pShim, pRev, pRate, pFl, pCh, pSub, pUp, pDry;
            SpectralBank bank, bankFb;
            MistressEngine mist;
            VerbCore core;
            DelayLine echo;
            XFader fader;
            Smooth blS, flS, chS, subS, upS, dryS, tS, routeS;
        };

        //==============================================================================
        /** Soul POG: Soul Food + Nano POG. */
        class B3SoulPog final : public Effect
        {
        public:
            explicit B3SoulPog (const ModelDef& d) : Effect (d)
            {
                pDry = role (*this, "direct", 0.8f); pSub = role (*this, "oct1", 0.0f); pUp = role (*this, "up1", 0.0f);
                pVol = role (*this, "level", 0.5f); pTreb = role (*this, "treble", 0.5f); pDrive = role (*this, "drive", 0.4f);
                pMode = role (*this, "mode", 0.0f); pOrder = role (*this, "order", 0.0f); pSfOn = role (*this, "sfon", 1.0f);
                pPogOn = role (*this, "pogon", 1.0f);
                // Soul Food (stessa netlist della tappa analogica): 0 VOL, 1 DRIVE, 2 TREBLE
                sf.init ("hpf R=1M C=100n; tap n=0; lpf R=15k C=100n; gain a=lin(1,1,0.12); tap n=1; recall n=0; "
                         "opni Rg=2k Cg=82n+27n Rf=pot(1,100k,B)+680 Cf=390p rail=8.3; hpf R=10k C=1u; dclip R=1k C=0 d=sch; "
                         "gain a=lin(1,0.5,1.6); mix n=1 wet=1 dry=1; lpf R=27k C=1.5n; hshelf f=1400 Q=0.5 g=lin(2,-8,0)+taper(2,A)*18 rail=8.3; "
                         "hpf R=10k C=1u; vol a=taper(0,B)*2.5; rail rail=8.5", { 0.5f, 0.4f, 0.5f });
            }
            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; mb = std::max (16, maxBlock);
                sf.prepare (s, mb);
                buf.assign ((size_t) mb, 0.0f); tmp.assign ((size_t) mb, 0.0f);
                bankA.prepare (s, 45.0f, 7000.0f, 4, 0.14f, 1);
                bankB.prepare (s, 45.0f, 7000.0f, 5, 0.11f, 1);
                for (auto* sm : { &dS, &sS, &uS, &mS, &sfS, &pgS }) sm->set (s, 20);
                reset();
            }
            void reset() override { sf.reset(); bankA.reset(); bankB.reset(); }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int o = 0; o < n; o += mb) chunk (ch, numCh, o, std::min (mb, n - o));
            }
        private:
            void chunk (float* const* ch, int numCh, int off, int n)
            {
                juce::ScopedNoDenormals nd;
                const float tgt[3] = { pVol.get (*this), pDrive.get (*this), pTreb.get (*this) };
                for (int k = 0; k < 3; ++k) { cp[k] += 0.3f * (tgt[k] - cp[k]); sf.set (k, cp[k]); }
                float f0, f1;
                fade.begin (pOrder.step (*this), f0, f1);
                const bool sfFirst = fade.cur == 0;                       // SF > POG
                const bool modeB = pMode.step (*this) == 1;
                const bool sfOn = pSfOn.step (*this) == 1, pogOn = pPogOn.step (*this) == 1;
                for (int i = 0; i < n; ++i) buf[(size_t) i] = tmp[(size_t) i] = monoIn (ch, numCh, off + i);
                if (sfFirst) { sf.process (buf.data(), n); mixSf (n, sfOn); pog (n, modeB, pogOn); }
                else { pog (n, modeB, pogOn); for (int i = 0; i < n; ++i) tmp[(size_t) i] = buf[(size_t) i]; sf.process (buf.data(), n); mixSf (n, sfOn); }
                for (int i = 0; i < n; ++i)
                {
                    const float fg = f0 + (f1 - f0) * (float) i / (float) n;
                    for (int c = 0; c < numCh; ++c) ch[c][off + i] = outRail (buf[(size_t) i] * fg);
                }
                fade.end();
            }
            /** Soul Food acceso/spento (bypass bufferizzato con dissolvenza): buf = uscita, tmp = ingresso. */
            void mixSf (int n, bool on)
            {
                for (int i = 0; i < n; ++i) { const float g = sfS.next (on ? 1.0f : 0.0f); buf[(size_t) i] = g * buf[(size_t) i] + (1.0f - g) * tmp[(size_t) i]; }
            }
            void pog (int n, bool modeB, bool on)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float x = buf[(size_t) i];
                    const float m = mS.next (modeB ? 1.0f : 0.0f);
                    const float sub = sS.next (sliderGain (pSub.get (*this))), up = uS.next (sliderGain (pUp.get (*this)));
                    float oct = 0;
                    if (m < 0.999f) { bankA.push (x); const int nn[2] = { 1, 2 }, ll[2] = { 1, 0 }; const float gg[2] = { sub, up }; oct += (1.0f - m) * bankA.voices (nn, ll, gg, 2); }
                    if (m > 0.001f)
                    {
                        // MODE: ottava sopra con armoniche arricchite e banco piu' fitto (migliore polifonia)
                        bankB.push (x);
                        const int nn[3] = { 1, 2, 4 }, ll[3] = { 1, 0, 0 }; const float gg[3] = { sub, up, 0.25f * up };
                        oct += m * bankB.voices (nn, ll, gg, 3);
                    }
                    const float y = sliderGain (dS.next (pDry.get (*this))) * x + oct * kBankGain;
                    const float g = pgS.next (on ? 1.0f : 0.0f);
                    buf[(size_t) i] = g * y + (1.0f - g) * x;
                }
            }
            static float sliderGain (float s) noexcept { return 1.56f * s * s; }
            float sr = 48000;
            int mb = 512;
            RoleParam pDry, pSub, pUp, pVol, pTreb, pDrive, pMode, pOrder, pSfOn, pPogOn;
            float cp[3] { 0.35f, 0.4f, 0.5f };
            SubCircuit sf;
            std::vector<float> buf, tmp;
            SpectralBank bankA, bankB;
            BlockFade fade;
            Smooth dS, sS, uS, mS, sfS, pgS;
        };

        //==============================================================================
        /** Tone Tattoo: Metal Muff -> Neo Clone -> Memory Toy. */
        class B3ToneTattoo final : public Effect
        {
        public:
            explicit B3ToneTattoo (const ModelDef& d) : Effect (d)
            {
                pDelay = role (*this, "time", 0.4f); pFb = role (*this, "feedback", 0.3f); pBlend = role (*this, "blend", 0.3f);
                pGain = role (*this, "gain", 0.0f); pRate = role (*this, "rate", 0.35f); pDepth = role (*this, "depth", 0.5f);
                pTreb = role (*this, "treble", 0.5f); pBass = role (*this, "bass", 0.5f); pVol = role (*this, "volume", 0.5f);
                pDrive = role (*this, "drive", 0.5f); pThr = role (*this, "gatethr", 0.0f); pScoop = role (*this, "scoop", 0.0f);
                // Metal Muff: 0 DRIVE, 1 BASS (+-18,5 dB a 105 Hz), 2 TREBLE, 3 SCOOP (OFF/LO/HI a 1,2 kHz), 4 VOLUME
                muff.init ("hpf R=1M C=22n; opni Rg=1k Cg=4.7u Rf=pot(0,100k,A)+1k Cf=100p rail=3.8; dclip R=2.2k C=4.7n d=si; hpf R=10k C=100n; "
                           "opni Rg=2.2k Cg=1u Rf=47k Cf=220p rail=3.8; dclip R=1k C=10n d=si; lshelf f=105 Q=0.6 g=lin(1,-18.5,18.5); "
                           "hshelf f=3500 Q=0.6 g=lin(2,-12,12); peak f=1200 Q=0.8 g=sw(3,0,-7.5,-11); vol a=taper(4,A)*1.5; rail rail=4.4",
                           { 0.5f, 0.5f, 0.5f, 0.0f, 0.5f }, { 0, 0, 0, 3, 0 });
            }
            void prepare (double s, int maxBlock) override
            {
                sr = (float) s; mb = std::max (16, maxBlock);
                muff.prepare (s, mb);
                buf.assign ((size_t) mb, 0.0f);
                clone.prepare (s, 1024, 7500, 0.0002f, false, true, 1.1f);
                toy.prepare (s, 8192, 4300, 0.0003f, true, false, 0.75f, 2.0e-5f, 20.0);
                gate.prepare (s, 60);
                fbLp.set (s, 3000); fbHp.set (s, 70);
                for (auto* sm : { &blS, &fbS, &gS, &cdS, &cwS }) sm->set (s, 20);
                tS.set (s, 80); posS.set (s, 4); rateS.set (s, 40);
                reset();
            }
            void reset() override { muff.reset(); clone.reset(); toy.reset(); fbState = 0; lfo.ph = 0.25; first = true; }
            void process (float* const* ch, int numCh, int n) override
            {
                for (int o = 0; o < n; o += mb) chunk (ch, numCh, o, std::min (mb, n - o));
            }
        private:
            void chunk (float* const* ch, int numCh, int off, int n)
            {
                juce::ScopedNoDenormals nd;
                const float tgt[4] = { pDrive.get (*this), pBass.get (*this), pTreb.get (*this), pVol.get (*this) };
                for (int k = 0; k < 4; ++k) cp[k] += 0.3f * (tgt[k] - cp[k]);
                muff.set (0, cp[0]); muff.set (1, cp[1]); muff.set (2, cp[2]); muff.set (3, pScoop.get (*this)); muff.set (4, cp[3]);
                for (int i = 0; i < n; ++i) buf[(size_t) i] = monoIn (ch, numCh, off + i);
                muff.process (buf.data(), n);
                const float thk = pThr.get (*this);
                const float gThr = thk < 0.02f ? 0.0f : 0.0004f * std::pow (150.0f, thk);   // THRESHOLD a zero = gate spento
                const int dep = std::clamp (pDepth.step (*this), 0, 2);                       // OFF, LO, HI
                const float swing = dep == 2 ? 0.85f : 0.35f;
                lfo.setHz (sr, logMap (pRate.get (*this), 0.4f, 9.0f));
                const float dT = logMap (pDelay.get (*this), 30.0f, 550.0f);
                if (first) { tS.snap (dT); first = false; }
                const float gain = std::pow (10.0f, 23.0f * pGain.get (*this) / 20.0f);       // GAIN 0..+23 dB
                for (int i = 0; i < n; ++i)
                {
                    const float x = monoIn (ch, numCh, off + i);
                    float v = buf[(size_t) i] * gate.tick (x, gThr);
                    // Neo Clone: BBD 1024 stadi 6-18 ms, profondita' LO/HI (OFF = sezione spenta)
                    const float l = lfo.tri(); lfo.step();
                    const float pos = posS.next (0.5f + 0.5f * swing * l);
                    const float w = clone.tick (v, 6.0f * std::pow (3.0f, pos));
                    const float on = cwS.next (dep > 0 ? 1.0f : 0.0f);
                    v = (1.0f - on) * v + on * (0.5f * v + 0.6f * w);
                    // Memory Toy: GAIN su dry e ritardato, BBD 8192 stadi 30-550 ms, feedback limitato
                    const float xin = 3.8f * std::tanh (gS.next (gain) * v / 3.8f);
                    const float d = tS.next (dT);
                    const double fcl = 8192.0 / (2.0 * d * 0.001);
                    toy.setFilter (std::clamp (0.225 * fcl, 800.0, 4300.0));
                    const float y = toy.tick (xin + loopSat (fbS.next (pFb.get (*this)) * fbState), d);
                    fbState = fbHp.hp (fbLp.lp (y));
                    const float b = blS.next (pBlend.get (*this));
                    const float out = outRail ((1.0f - b) * xin + b * y);
                    for (int c = 0; c < numCh; ++c) ch[c][off + i] = out;
                }
            }
            float sr = 48000, fbState = 0;
            int mb = 512;
            bool first = true;
            RoleParam pDelay, pFb, pBlend, pGain, pRate, pDepth, pTreb, pBass, pVol, pDrive, pThr, pScoop;
            float cp[4] { 0.5f, 0.5f, 0.5f, 0.35f };
            SubCircuit muff;
            std::vector<float> buf;
            BbdChain clone, toy;
            GateB gate;
            OnePole fbLp, fbHp;
            Lfo lfo;
            Smooth blS, fbS, gS, cdS, cwS, tS, posS, rateS;
        };
    }

    std::unique_ptr<Effect> makeB3Multi (const ModelDef& d, const std::string& type)
    {
        if (type == "b3battalion")  return std::make_unique<B3Battalion> (d);
        if (type == "b3bassmeta")   return std::make_unique<B3BassMeta> (d);
        if (type == "b3analogizer") return std::make_unique<B3Analogizer> (d);
        if (type == "b3epitome")    return std::make_unique<B3Epitome> (d);
        if (type == "b3soulpog")    return std::make_unique<B3SoulPog> (d);
        if (type == "b3tonetattoo") return std::make_unique<B3ToneTattoo> (d);
        return nullptr;
    }
}
