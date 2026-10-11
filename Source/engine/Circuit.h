/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Motore di simulazione a stadi per i pedali a guadagno/clipping.

    Il circuito di ogni pedale e' descritto da una "netlist a stadi" (stringa)
    con i valori reali dei componenti, per esempio:

        hpf R=1M C=47n; bjt g=40 vp=4 vn=3.2; opni Rg=4.7k Cg=0.47u Rf=pot(1,100k,A)+100 Cf=0 rail=4;
        dclip R=2.2k C=10n d=si; tonebm R1=6.8k C1=22n R2=6.8k C2=10n t=taper(2,B); vol a=taper(0,A)

    Gli stadi lineari sono convertiti in funzioni di trasferimento H(s) dai
    valori dei componenti e discretizzati con la trasformata bilineare; i
    clipper a diodi sono risolti campione per campione (equazione di Shockley,
    integrazione trapezoidale, Newton-Raphson). Tutto in sovracampionamento 4x.

    Stadi aggiunti nella tappa 5: octdiv o1 o2 hys thr dec gate (divisore d'ottava a flip-flop tipo OC-2: il segnale filtrato
    moltiplicato per le onde quadre a f/2 e f/4, commutate ai passaggi per lo zero, con porta sotto la soglia thr).

    Espressioni ammesse nei valori (somma di prodotti):
        numero con suffisso SI (p n u m k M)       es. 4.7k, 47n
        pot(i,R,T)   resistenza del ramo del potenziometro i (curva T=A|B|C)
        potr(i,R,T)  resistenza del ramo complementare
        taper(i,T)   posizione 0..1 con curva
        lin(i,a,b)   interpolazione lineare a..b
        log(i,a,b)   interpolazione logaritmica a..b
        sw(i,v0,v1,...) valore scelto dal selettore i
        par(a,b,...)  resistenze in parallelo 1/(1/a+1/b+...) (argomenti = espressioni complete)
        inv(a)        1/a          div(a,b)  a/b
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <string>
#include "Effect.h"

namespace pt::engine
{
    //==============================================================================
    /** Espressione valutata a frequenza di controllo. */
    class Expr
    {
    public:
        struct Factor { enum Kind { Const, Pot, PotR, Taper, Lin, Log, Sw, Par, Inv, Div } kind = Const;
                        int idx = 0; char taper = 'B'; std::vector<double> args;
                        std::vector<Expr> sub; };     // sotto-espressioni di par/inv/div
        using Term = std::vector<Factor>;

        static Expr parse (const std::string& text, std::string& error);
        double eval (const float* p, const ModelDef& def) const;
        bool isConst() const;
        /** Aggiunge a 'mask' i comandi (bit = indice) da cui dipende l'espressione. */
        void addControls (uint32_t& mask) const;
        static double parseNumber (const std::string& s, bool& ok);
        static double applyTaper (double x, char t);

    private:
        std::vector<Term> terms;
    };

    //==============================================================================
    /** Filtro analogico fino al 3° ordine discretizzato con la bilineare (double). */
    struct AnalogTF
    {
        // coefficienti in s, potenze crescenti: b[0] + b[1]s + b[2]s^2 + b[3]s^3
        double bs[4] {}, as[4] {};
        int order = 1;
        double bz[4] {}, az[4] {};
        double state[2][4] {};

        // coefficienti da cui parte la rampa per canale (comandi in movimento): dopo un nuovo progetto i coefficienti
        // scorrono campione per campione dai vecchi ai nuovi durante la chiamata successiva di ogni canale
        double fromB[2][4] {}, fromA[2][4] {};
        int fromOrder = 1;
        bool ramp[2] {};

        void design (double fs, double prewarpHz = 0.0);
        void reset() { for (auto& c : state) for (auto& v : c) v = 0; settle(); }
        /** Coefficienti attuali come punto di partenza: nessuna rampa (avvio, reset, selettori, comportamento a blocchi). */
        void settle() noexcept
        {
            for (int c = 0; c < 2; ++c) { std::copy (bz, bz + 4, fromB[c]); std::copy (az, az + 4, fromA[c]); ramp[c] = false; }
            fromOrder = order;
        }
        /** Dopo design(): rampa dai coefficienti precedenti se sono cambiati (e l'ordine e' lo stesso). */
        void armRamp() noexcept
        {
            if (order != fromOrder) { settle(); return; }
            for (int c = 0; c < 2; ++c)
            {
                bool diff = false;
                for (int i = 0; i <= order; ++i)
                    diff = diff || std::fpclassify (fromB[c][i] - bz[i]) != FP_ZERO || std::fpclassify (fromA[c][i] - az[i]) != FP_ZERO;
                ramp[c] = diff;
            }
        }
        inline double tick (int ch, double x) noexcept
        {
            auto* s = state[ch];
            const double y = bz[0] * x + s[0];
            for (int i = 0; i < order - 1; ++i)
                s[i] = bz[i + 1] * x - az[i + 1] * y + s[i + 1];
            s[order - 1] = bz[order] * x - az[order] * y;
            return y;
        }
        /** Come tick() con coefficienti dati (rampa). */
        inline double tickWith (int ch, double x, const double* b, const double* a) noexcept
        {
            auto* s = state[ch];
            const double y = b[0] * x + s[0];
            for (int i = 0; i < order - 1; ++i)
                s[i] = b[i + 1] * x - a[i + 1] * y + s[i + 1];
            s[order - 1] = b[order] * x - a[order] * y;
            return y;
        }
        /** Elabora n campioni del canale 'ch' con la rampa dei coefficienti; 'post' trasforma ogni uscita. */
        template <class Post>
        inline void rampBlock (int ch, double* x, int n, Post post) noexcept
        {
            double b[4], a[4], db[4], da[4];
            const double inv = 1.0 / std::max (1, n);
            for (int i = 0; i <= order; ++i)
            {
                b[i] = fromB[ch][i]; a[i] = fromA[ch][i];
                db[i] = (bz[i] - b[i]) * inv; da[i] = (az[i] - a[i]) * inv;
            }
            for (int k = 0; k < n; ++k)
            {
                for (int i = 0; i <= order; ++i) { b[i] += db[i]; a[i] += da[i]; }
                x[k] = post (tickWith (ch, x[k], b, a));
            }
            std::copy (bz, bz + 4, fromB[ch]); std::copy (az, az + 4, fromA[ch]);
            ramp[ch] = false;
        }
    };

    //==============================================================================
    /** Modello di diodo (Shockley) per i clipper. */
    struct DiodeModel
    {
        double is = 2.52e-9, n = 1.752;
        static DiodeModel byName (const std::string& name);
    };

    //==============================================================================
    class Stage
    {
    public:
        virtual ~Stage() = default;
        /** Aggiorna i coefficienti (frequenza di controllo). */
        virtual void update (double fs, const float* p, const ModelDef& def) = 0;
        virtual void reset() = 0;
        virtual void process (int ch, double* x, int n) = 0;
        /** Copia stato e coefficienti da uno stadio dello stesso tipo e della stessa netlist (catena di riserva
            delle dissolvenze dei selettori); nessuna allocazione se le memorie hanno gia' la stessa dimensione. */
        virtual void copyFrom (const Stage& other) = 0;
        /** Nessuna rampa verso i coefficienti appena calcolati (avvio, selettori, comportamento a blocchi). */
        virtual void settle() {}

        std::vector<std::pair<std::string, Expr>> args;
        double arg (const char* name, const float* p, const ModelDef& def, double fallback) const;
        bool has (const char* name) const;
        double fs = 0;
        uint32_t controls = 0;      // comandi da cui dipendono gli argomenti (bit = indice): update() solo se cambiano

    protected:
        Stage() = default;
        Stage (const Stage&) = default;
        // gli argomenti (e quindi 'controls') sono uguali per costruzione: si copia solo lo stato
        Stage& operator= (const Stage& o) { fs = o.fs; return *this; }
    };

    //==============================================================================
    class CircuitEffect : public Effect
    {
    public:
        static constexpr int maxTaps = 8;      // memorie "tap n=0..7" per rami paralleli (tap / recall / mix)
        explicit CircuitEffect (const ModelDef&);
        void prepare (double sampleRate, int maxBlock) override;
        void reset() override;
        void process (float* const* ch, int numCh, int n) override;

        /** Comportamento dei comandi fino alla 1.2.6 (lisciatura 0.35 per blocco, coefficienti ricalcolati una volta
            per blocco): per i circuiti usati dentro altri effetti (SubCircuit), che lisciano gia' i comandi da se'. */
        void setBlockSmoothing (bool on) noexcept { blockSmoothing = on; }

        /** Errori di parsing della netlist (vuoto se tutto ok). */
        std::string error;

        // Comandi che si muovono (dalla 1.2.7): i pomelli sono lisciati da due poli in cascata (kTauMs ciascuno) e solo gli stadi
        // che ne dipendono si ricalcolano, a sotto-blocchi di kSub campioni (alla frequenza di base); fra un ricalcolo e l'altro
        // filtri lineari, Rf degli op-amp e soglia/rapporto dei compressori scorrono campione per campione. I selettori e le
        // levette passano subito al nuovo valore con una dissolvenza di kFadeMs fra la catena di riserva (copia della principale,
        // con la vecchia topologia) e quella principale. A comandi fermi il percorso e' quello di prima: uscita identica bit per
        // bit alle versioni precedenti.
        static constexpr int kSub = 16;
        static constexpr double kTauMs = 6.0, kFadeMs = 15.0;

    private:
        void updateStages();
        void updateMasked (uint32_t mask, bool ramp);
        void processLegacy (float* const* ch, int numCh, int n);
        void runStages (std::vector<std::unique_ptr<Stage>>& chain, int c, double* x, int n);

        std::vector<std::unique_ptr<Stage>> stages;
        std::vector<std::unique_ptr<Stage>> spare;         // catena di riserva (stessa netlist) per le dissolvenze dei selettori
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        std::vector<double> work, workSpare;
        std::unique_ptr<std::vector<double>[]> tapStore;   // punti "tap" (maxTaps x 2 canali) per miscelazioni e rami paralleli
        float smoothed[maxControls] {};                    // valori passati agli stadi
        double glide1[maxControls] {}, glide2[maxControls] {};   // stati della lisciatura a due poli dei pomelli
        uint32_t steppedMask = 0;                          // comandi a scatti (selettori, levette)
        double sr = 48000.0, osr = 192000.0;
        int maxN = 512;
        int fadeLen = 1, fadePos = 0;                      // durata e posizione della dissolvenza (campioni sovracampionati)
        bool fading = false;
        bool first = true;
        bool blockSmoothing = false;
        bool needInputs = false;   // uno stadio legge gli ingressi di entrambi i canali (pan / blend)
    };
}
