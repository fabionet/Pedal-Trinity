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

    Espressioni ammesse nei valori (somma di prodotti):
        numero con suffisso SI (p n u m k M)       es. 4.7k, 47n
        pot(i,R,T)   resistenza del ramo del potenziometro i (curva T=A|B|C)
        potr(i,R,T)  resistenza del ramo complementare
        taper(i,T)   posizione 0..1 con curva
        lin(i,a,b)   interpolazione lineare a..b
        log(i,a,b)   interpolazione logaritmica a..b
        sw(i,v0,v1,...) valore scelto dal selettore i
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
        struct Factor { enum Kind { Const, Pot, PotR, Taper, Lin, Log, Sw } kind = Const;
                        int idx = 0; char taper = 'B'; std::vector<double> args; };
        using Term = std::vector<Factor>;

        static Expr parse (const std::string& text, std::string& error);
        double eval (const float* p, const ModelDef& def) const;
        bool isConst() const;
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

        void design (double fs, double prewarpHz = 0.0);
        void reset() { for (auto& c : state) for (auto& v : c) v = 0; }
        inline double tick (int ch, double x) noexcept
        {
            auto* s = state[ch];
            const double y = bz[0] * x + s[0];
            for (int i = 0; i < order - 1; ++i)
                s[i] = bz[i + 1] * x - az[i + 1] * y + s[i + 1];
            s[order - 1] = bz[order] * x - az[order] * y;
            return y;
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

        std::vector<std::pair<std::string, Expr>> args;
        double arg (const char* name, const float* p, const ModelDef& def, double fallback) const;
        bool has (const char* name) const;
        double fs = 0;
    };

    //==============================================================================
    class CircuitEffect : public Effect
    {
    public:
        explicit CircuitEffect (const ModelDef&);
        void prepare (double sampleRate, int maxBlock) override;
        void reset() override;
        void process (float* const* ch, int numCh, int n) override;

        /** Errori di parsing della netlist (vuoto se tutto ok). */
        std::string error;

    private:
        void updateStages();

        std::vector<std::unique_ptr<Stage>> stages;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        std::vector<double> work;
        std::unique_ptr<std::vector<double>[]> tapStore;   // punti "tap" per miscelazioni clean/drive
        float smoothed[maxControls] {};
        float lastUpdated[maxControls] {};
        double sr = 48000.0, osr = 192000.0;
        int maxN = 512;
        bool first = true;
    };
}
