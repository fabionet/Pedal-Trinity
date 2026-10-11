/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Dati della tappa 5 per i pedali digitali (incluso SOLO da FxStage5Time.cpp):
      - descrizione dei cabinet da cui si GENERANO le risposte all'impulso (nessuna IR copiata da
        prodotti esistenti: risonanza dell'altoparlante nel mobile, mobile aperto/chiuso, pettine fra
        i coni, voce dell'altoparlante, filtro del microfono, prime riflessioni);
      - netlist dei preampli (motore Circuit: triodi Koren, stack Fender/Marshall/Vox di Yeh) e delle
        saturazioni del MINI-EFX.
    I nomi dei cabinet di riferimento servono solo a documentare il carattere che si cerca di avvicinare.
*/

#pragma once

#include <string>

namespace pt::engine::i5
{
    /** Microfoni (filtri applicati alla risposta del cabinet). */
    enum Mic : int { Sm57 = 0, Md421, LargeCond, Md441, D112, SmallCond, Piezo, Sm57Off, Md441Edge, Ribbon };

    /** Un cabinet da generare. Frequenze in Hz, guadagni in dB. */
    struct CabSpec
    {
        const char* name;
        float fres, qres, gres;      // risonanza dell'altoparlante nel mobile
        float hp, lp;                // taglio basso del sistema, taglio del cono (24 dB/ottava)
        int open;                    // 1 = mobile aperto dietro: onda posteriore riflessa (cancellazione dei bassi)
        int nspk;                    // altoparlanti: pettine fra i coni (ritardi di percorso diversi)
        float v[9];                  // voce dell'altoparlante: tre campane f, q, dB (f = 0: assente)
        int mic;
        float dist;                  // 0 = a contatto ... 1 = lontano (piu' riflessioni)
        float len;                   // durata della risposta (s)
    };

    // voci tipiche degli altoparlanti (campane), dal carattere pubblicato delle curve dei produttori
#define PT_V30   2800, 1.6f, 5.0f, 1300, 1.5f, -2.0f, 5000, 3.0f, 2.0f
#define PT_GM25  1800, 1.2f, 3.0f, 3500, 2.0f, 2.0f, 800, 1.0f, 1.0f
#define PT_T75   700, 1.0f, -3.0f, 2600, 1.5f, 4.0f, 5200, 2.0f, 4.0f
#define PT_H30   1600, 1.3f, 3.0f, 3000, 2.0f, 2.5f, 500, 1.0f, -1.0f
#define PT_BLUE  2200, 1.4f, 4.0f, 4500, 2.5f, 1.5f, 900, 1.2f, 1.0f
#define PT_SILV  2400, 1.5f, 4.0f, 5000, 2.0f, 2.0f, 1000, 1.2f, 0.5f
#define PT_C12N  3200, 1.2f, 4.0f, 1000, 1.0f, -1.0f, 6000, 2.5f, 1.0f
#define PT_JBL   3500, 1.0f, 2.0f, 1200, 0.8f, -0.5f, 0, 1.0f, 0.0f
#define PT_JC    2500, 1.0f, 2.0f, 6000, 2.0f, 1.0f, 0, 1.0f, 0.0f
#define PT_EVM   2000, 1.0f, 1.0f, 0, 1.0f, 0.0f, 0, 1.0f, 0.0f
#define PT_SHEF  2500, 1.5f, 3.0f, 1000, 1.0f, -1.0f, 4500, 2.5f, 1.5f
#define PT_J15   2500, 1.2f, 3.0f, 900, 1.0f, -1.0f, 0, 1.0f, 0.0f
#define PT_BASS  2500, 1.5f, 3.0f, 700, 1.0f, -1.5f, 0, 1.0f, 0.0f
#define PT_BASSH 3500, 1.2f, 4.0f, 800, 1.0f, -1.0f, 6000, 2.0f, 2.0f
#define PT_FLAT  0, 1.0f, 0.0f, 0, 1.0f, 0.0f, 0, 1.0f, 0.0f

    //==========================================================================
    /** Cube Baby: posizioni 2-9 del selettore IR CAB (1 = spento). */
    inline const CabSpec kCubeCabs[8] = {
        { "Vetta 1x12 (aperto, V30)",          95, 1.4f, 5.0f, 70, 5200, 1, 1, { 2400, 1.5f, 3.0f, 4000, 2.0f, 2.0f, 800, 1.0f, -1.0f }, Sm57, 0.2f, 0.06f },
        { "1960AV 4x12 V30",                   85, 1.6f, 6.0f, 60, 5000, 0, 4, { PT_V30 }, Sm57, 0.1f, 0.06f },
        { "1960A G12T-75, preampli La610",     90, 1.7f, 6.0f, 60, 5500, 0, 4, { PT_T75 }, Sm57, 0.1f, 0.06f },
        { "VHT Deliverance 2x12 V30",         100, 1.5f, 5.0f, 65, 5200, 0, 2, { PT_V30 }, Sm57, 0.15f, 0.06f },
        { "Soldano 2x12 V30",                  95, 1.5f, 5.0f, 65, 5100, 0, 2, { 2500, 1.4f, 5.0f, 1200, 1.2f, -1.5f, 4500, 2.5f, 2.0f }, Sm57, 0.1f, 0.06f },
        { "5150 + Mesa Oversized 4x12",        75, 1.8f, 7.0f, 55, 5000, 0, 4, { 2700, 1.5f, 4.0f, 400, 1.0f, -1.5f, 5000, 3.0f, 2.0f }, Sm57, 0.1f, 0.06f },
        { "JSX KT77 + Stiletto 4x12, C1000",   80, 1.6f, 6.0f, 60, 5600, 0, 4, { PT_V30 }, SmallCond, 0.15f, 0.06f },
        { "Diezel 4x12 V30, SM57",             82, 1.7f, 6.5f, 60, 5000, 0, 4, { 2900, 1.5f, 5.0f, 1400, 1.3f, -2.0f, 5200, 3.0f, 2.0f }, Sm57, 0.1f, 0.06f },
    };

    /** Cube Baby Bass. */
    inline const CabSpec kBassCabs[8] = {
        { "Jensen 10, MD441",                 110, 1.3f, 4.0f, 45, 4500, 0, 4, { PT_BASSH }, Md441, 0.1f, 0.07f },
        { "Studio 22, MD441 bordo",            80, 1.4f, 5.0f, 45, 4000, 0, 2, { PT_BASS }, Md441Edge, 0.1f, 0.07f },
        { "SWR 15, AT4050",                    55, 1.4f, 5.0f, 35, 3500, 0, 1, { PT_BASS }, LargeCond, 0.15f, 0.08f },
        { "Workingman 15, MD441 bordo",        60, 1.5f, 5.0f, 38, 3200, 0, 1, { PT_BASS }, Md441Edge, 0.1f, 0.08f },
        { "Peavey TP, MD421",                  70, 1.4f, 5.0f, 40, 4500, 0, 4, { PT_BASSH }, Md421, 0.1f, 0.07f },
        { "Peavey TP, D112",                   70, 1.4f, 5.0f, 40, 4500, 0, 4, { PT_BASSH }, D112, 0.1f, 0.07f },
        { "Workingman 15, AT4050",             60, 1.5f, 5.0f, 38, 3600, 0, 1, { PT_BASS }, LargeCond, 0.15f, 0.08f },
        { "Electro-Voice 12, MD441",           75, 1.3f, 4.0f, 45, 5500, 0, 1, { PT_EVM }, Md441, 0.1f, 0.07f },
    };

    /** Cube Baby AC: risposte di corpo acustico (risonanza dell'aria, della tavola, presenza) e cabinet. */
    inline const CabSpec kAcCabs[8] = {
        { "Martin D-45, piezo",                98, 5.0f, 8.0f, 60, 9000, 0, 1, { 195, 3.0f, 5.0f, 450, 1.0f, -4.0f, 3000, 1.2f, 3.0f }, Piezo, 0.0f, 0.12f },
        { "acustica generica",                110, 4.0f, 6.0f, 60, 10000, 0, 1, { 220, 3.0f, 4.0f, 500, 1.0f, -3.0f, 2500, 1.0f, 2.0f }, LargeCond, 0.1f, 0.12f },
        { "Wurlitzer (piccolo combo)",        140, 1.5f, 4.0f, 80, 4500, 1, 1, { 1800, 1.0f, 3.0f, 3500, 2.0f, 1.0f, 0, 1.0f, 0.0f }, Sm57, 0.3f, 0.06f },
        { "Martin D-45, microfono alla buca",  98, 4.0f, 10.0f, 55, 7000, 0, 1, { 200, 3.0f, 3.0f, 600, 1.0f, -2.0f, 2500, 1.0f, 1.0f }, LargeCond, 0.0f, 0.12f },
        { "Soldano 2x12, microfono sul cono",  95, 1.5f, 5.0f, 65, 5500, 0, 2, { 2500, 1.4f, 5.0f, 1200, 1.2f, -1.5f, 4500, 2.5f, 3.0f }, Sm57, 0.05f, 0.06f },
        { "1x12 aperto, in asse sulla griglia",100, 1.4f, 4.0f, 70, 6000, 1, 1, { PT_BLUE }, LargeCond, 0.2f, 0.06f },
        { "Taylor 314ce, Soundelux",          105, 4.0f, 7.0f, 60, 12000, 0, 1, { 210, 3.0f, 4.0f, 450, 1.0f, -3.0f, 4000, 1.2f, 3.0f }, LargeCond, 0.1f, 0.12f },
        { "Pro Junior 1x10, a pavimento",     120, 1.3f, 4.0f, 80, 4500, 1, 1, { 2200, 1.2f, 3.0f, 900, 1.0f, 1.0f, 0, 1.0f, 0.0f }, LargeCond, 0.8f, 0.07f },
    };

    /** IR BOX (SK14): 32 preset (25 chitarra, 7 basso). */
    inline const CabSpec kIrBoxCabs[32] = {
        { "Tweed Deluxe 2x12 Blue, 57 bordo",  100, 1.3f, 4.0f, 70, 6000, 1, 2, { PT_BLUE }, Sm57Off, 0.1f, 0.0465f },
        { "Showman 2x12 D130",                  70, 1.2f, 4.0f, 60, 7000, 1, 2, { PT_JBL }, Sm57, 0.1f, 0.0465f },
        { "JC-120 2x12, U47",                   85, 1.3f, 4.0f, 65, 6500, 1, 2, { PT_JC }, LargeCond, 0.15f, 0.0465f },
        { "1960B 4x12 V30, C414",               85, 1.6f, 6.0f, 60, 5000, 0, 4, { PT_V30 }, LargeCond, 0.15f, 0.0465f },
        { "Basketweave 4x12 G12M",              90, 1.5f, 5.0f, 65, 4800, 0, 4, { PT_GM25 }, Sm57, 0.1f, 0.0465f },
        { "1960B 4x12 T75, U47",                90, 1.7f, 6.0f, 60, 5500, 0, 4, { PT_T75 }, LargeCond, 0.15f, 0.0465f },
        { "1960B 4x12 V30, 57",                 85, 1.6f, 6.0f, 60, 5000, 0, 4, { PT_V30 }, Sm57, 0.1f, 0.0465f },
        { "Basketweave 4x12 G12H30, U47",       88, 1.5f, 5.5f, 62, 5000, 0, 4, { PT_H30 }, LargeCond, 0.15f, 0.0465f },
        { "Uberkab 4x12 T75/V30",               82, 1.7f, 6.0f, 58, 5200, 0, 4, { 2700, 1.5f, 4.5f, 700, 1.0f, -2.0f, 5200, 2.5f, 3.0f }, Sm57, 0.1f, 0.0465f },
        { "ENGL Pro 4x12 V30",                  80, 1.7f, 6.5f, 58, 5000, 0, 4, { PT_V30 }, Sm57, 0.1f, 0.0465f },
        { "5150 4x12 Sheffield 1200",           80, 1.7f, 6.0f, 60, 5000, 0, 4, { PT_SHEF }, Sm57, 0.1f, 0.0465f },
        { "Twin 2x12 D120, U87",                85, 1.3f, 4.0f, 65, 7500, 1, 2, { PT_JBL }, LargeCond, 0.15f, 0.0465f },
        { "Twin 2x12 C12N, KM84",              105, 1.3f, 4.0f, 70, 5800, 1, 2, { PT_C12N }, SmallCond, 0.15f, 0.0465f },
        { "AC30 2x12 Silver, SM57",            100, 1.4f, 4.5f, 70, 6000, 1, 2, { PT_SILV }, Sm57, 0.1f, 0.0465f },
        { "AC30 2x12 Silver, U87",             100, 1.4f, 4.5f, 70, 6000, 1, 2, { PT_SILV }, LargeCond, 0.15f, 0.0465f },
        { "Twin 2x12 D120, 57 fuori asse",      85, 1.3f, 4.0f, 65, 7500, 1, 2, { PT_JBL }, Sm57Off, 0.1f, 0.0465f },
        { "Showman 2x12 D130, U47",             70, 1.2f, 4.0f, 60, 7000, 1, 2, { PT_JBL }, LargeCond, 0.15f, 0.0465f },
        { "Orange 4x12 V30, C414",              82, 1.6f, 6.0f, 60, 5000, 0, 4, { 2600, 1.5f, 4.5f, 1300, 1.5f, -1.5f, 5000, 3.0f, 2.0f }, LargeCond, 0.15f, 0.0465f },
        { "Diezel 4x12 V30, SM57",              82, 1.7f, 6.5f, 60, 5000, 0, 4, { 2900, 1.5f, 5.0f, 1400, 1.3f, -2.0f, 5200, 3.0f, 2.0f }, Sm57, 0.1f, 0.0465f },
        { "Mesa 4x12, U87",                     75, 1.8f, 7.0f, 55, 5000, 0, 4, { PT_V30 }, LargeCond, 0.15f, 0.0465f },
        { "Electro-Voice 4x12, C414",           80, 1.4f, 5.0f, 60, 6000, 0, 4, { PT_EVM }, LargeCond, 0.15f, 0.0465f },
        { "4x12 'R' (nome illeggibile)",        84, 1.6f, 6.0f, 60, 5200, 0, 4, { PT_GM25 }, Sm57, 0.1f, 0.0465f },
        { "Supro Thunderbolt 1x15 Jensen",      65, 1.3f, 4.0f, 55, 5000, 1, 1, { PT_J15 }, Sm57, 0.15f, 0.0465f },
        { "Matchless ES212 G12H30",             95, 1.4f, 5.0f, 68, 5200, 1, 2, { PT_H30 }, Sm57, 0.1f, 0.0465f },
        { "Rectifier 4x12 V30",                 75, 1.8f, 7.0f, 55, 5000, 0, 4, { 2700, 1.5f, 4.0f, 400, 1.0f, -1.5f, 5000, 3.0f, 2.0f }, Sm57, 0.1f, 0.0465f },
        { "basso 4x10 Jensen, AT4050 fuori asse",80, 1.4f, 5.0f, 42, 4200, 0, 4, { PT_BASSH }, LargeCond, 0.2f, 0.0465f },
        { "basso 'Studio 22'",                  80, 1.4f, 5.0f, 45, 4000, 0, 2, { PT_BASS }, Md441, 0.1f, 0.0465f },
        { "basso SVT-410HE",                    65, 1.5f, 5.0f, 40, 4500, 0, 4, { PT_BASSH }, Md421, 0.1f, 0.0465f },
        { "basso DB 410 (stima)",               70, 1.4f, 5.0f, 40, 4800, 0, 4, { PT_BASSH }, Md441, 0.1f, 0.0465f },
        { "basso Peavey TP 6x10",               70, 1.4f, 5.0f, 40, 4500, 0, 4, { PT_BASSH }, Md421, 0.1f, 0.0465f },
        { "basso Eden",                         62, 1.5f, 5.0f, 38, 4500, 0, 4, { PT_BASSH }, LargeCond, 0.15f, 0.0465f },
        { "basso Workingman 15",                60, 1.5f, 5.0f, 38, 3400, 0, 1, { PT_BASS }, Md441, 0.1f, 0.0465f },
    };

#undef PT_V30
#undef PT_GM25
#undef PT_T75
#undef PT_H30
#undef PT_BLUE
#undef PT_SILV
#undef PT_C12N
#undef PT_JBL
#undef PT_JC
#undef PT_EVM
#undef PT_SHEF
#undef PT_J15
#undef PT_BASS
#undef PT_BASSH
#undef PT_FLAT

    //==========================================================================
    /** Preampli valvolare a tre/quattro triodi sullo schema del simulatore IR-2 a catalogo (gain fra il primo e il
        secondo triodo, filtro di Miller, terzo triodo con catodo scelto, stack Yeh, finale con sag), senza cabinet.
        Comandi della netlist: 0 gain, 1 bass, 2 mid, 3 treble, 4 brillantezza (+-9 dB a 2,5 kHz). */
    struct AmpVoice
    {
        const char* name;
        const char *hpC, *g1, *lpC, *g2, *rk3, *R1, *R2, *R3, *R4, *C1, *C2, *C3, *g3, *sag;
    };

    inline std::string ampNetlist (const AmpVoice& v)
    {
        std::string s;
        s += "hpf R=470k C="; s += v.hpC;
        s += "; triode Rp=100k Rk=1.5k B=300 byp=1; gain a=taper(0,A)*"; s += v.g1; s += "+"; s += v.g1; s += "*0.03";
        s += "; triode Rp=100k Rk=1.5k B=300 byp=1; hpf R=470k C=22n; lpf R=100k C="; s += v.lpC;
        s += "; gain a="; s += v.g2;
        s += "; triode Rp=100k Rk="; s += v.rk3; s += " B=300 byp=1";
        s += "; fmv R1="; s += v.R1; s += " R2="; s += v.R2; s += " R3="; s += v.R3; s += " R4="; s += v.R4;
        s += " C1="; s += v.C1; s += " C2="; s += v.C2; s += " C3="; s += v.C3;
        s += " t=taper(3,B) m=taper(2,B) l=taper(1,A); gain a="; s += v.g3;
        s += "; power V=1 g=1 sag="; s += v.sag;
        s += "; hshelf f=2.5k Q=0.7 g=lin(4,-9,9)";
        return s;
    }

    //                     nome                       hpC    g1     lpC    g2     rk3    R1     R2     R3    R4     C1     C2     C3     g3      sag
    inline const AmpVoice kCubeAmps[9] = {
        { "Power-Zone Clean (Koch Powertone, stima)", "47n", "0.10", "47p",  "0.02", "1.5k", "250k", "250k", "10k", "100k", "330p", "100n", "47n", "0.36",  "0.12" },
        { "US Gold 100 Clean (Friedman BE-100)",      "47n", "0.10", "47p",  "0.02", "1.5k", "220k", "1M",   "22k", "33k",  "470p", "22n",  "22n", "0.30",  "0.12" },
        { "Two Stone Coral OD (Two-Rock Coral)",      "22n", "0.35", "220p", "0.04", "1.5k", "250k", "1M",   "10k", "100k", "250p", "47n",  "22n", "0.075", "0.20" },
        { "Doctor3 B (Dr. Z, stima)",                 "22n", "0.35", "100p", "0.04", "1.5k", "1M",   "1M",   "10k", "100k", "50p",  "22n",  "22n", "0.056", "0.30" },
        { "Cali JP A (Mesa JP-2C, stima)",            "4.7n","1",    "470p", "0.10", "2.7k", "250k", "250k", "10k", "100k", "750p", "22n",  "22n", "0.035", "0.15" },
        { "Day Tripper OD (Vox AC30 spinto)",         "22n", "0.60", "220p", "0.06", "1.5k", "1M",   "1M",   "10k", "100k", "50p",  "22n",  "22n", "0.056", "0.35" },
        { "Shittcow Dist (non identificato)",         "4.7n","1",    "470p", "0.15", "10k",  "250k", "1M",   "25k", "47k",  "470p", "22n",  "22n", "0.028", "0.15" },
        { "Wo Stone Coral OD (Coral ad alto guadagno)","6.8n","1",   "470p", "0.10", "2.7k", "250k", "1M",   "10k", "100k", "250p", "47n",  "22n", "0.035", "0.20" },
        { "Mr Smith Dist (PRS, stima)",               "2.2n","1",    "470p", "0.20", "10k",  "250k", "250k", "25k", "47k",  "500p", "22n",  "22n", "0.028", "0.25" },
    };

    inline const AmpVoice kMiniAmps[9] = {
        { "Dark DLX (Deluxe blackface)",              "47n", "0.06", "47p",  "0.02", "1.5k", "250k", "250k", "10k", "100k", "250p", "100n", "47n", "0.47",  "0.15" },
        { "Tweed Deluxe",                             "22n", "0.30", "100p", "0.03", "1.5k", "250k", "1M",   "25k", "56k",  "250p", "20n",  "20n", "0.063", "0.35" },
        { "Bogner Ecstasy blu",                       "10n", "0.50", "220p", "0.06", "1.5k", "220k", "1M",   "22k", "33k",  "470p", "22n",  "22n", "0.04",  "0.25" },
        { "Bad Cat Hot Cat 30",                       "22n", "0.50", "220p", "0.05", "1.5k", "1M",   "1M",   "10k", "100k", "50p",  "22n",  "22n", "0.056", "0.30" },
        { "Brown Eye (Friedman BE)",                  "6.8n","1",    "470p", "0.10", "2.7k", "220k", "1M",   "22k", "33k",  "470p", "22n",  "22n", "0.035", "0.20" },
        { "Marshall JCM800",                          "4.7n","1",    "470p", "0.15", "10k",  "250k", "1M",   "25k", "47k",  "470p", "22n",  "22n", "0.028", "0.15" },
        { "ENGL Powerball II",                        "3.3n","1",    "1n",   "0.20", "10k",  "220k", "1M",   "22k", "33k",  "470p", "22n",  "22n", "0.028", "0.15" },
        { "Mesa Dual Rectifier",                      "3.3n","1",    "470p", "0.20", "10k",  "250k", "250k", "25k", "47k",  "500p", "22n",  "22n", "0.028", "0.25" },
        { "Soldano SLO-100 lead",                     "4.7n","1",    "470p", "0.15", "10k",  "250k", "1M",   "25k", "47k",  "470p", "22n",  "22n", "0.035", "0.15" },
    };

    /** EQ a tre bande del MINI-EFX (comandi 1 bass, 2 mid, 3 treble; frequenze stimate). */
#define PT_EFX_EQ "; lshelf f=120 Q=0.7 g=lin(1,-12,12); peak f=800 Q=0.8 g=lin(2,-12,12); hshelf f=3.2k Q=0.7 g=lin(3,-12,12)"

    /** Saturazioni del MINI-EFX (comando 0 = GAIN). */
    inline const char* const kEfxNets[9] = {
        // 1 BOOST: op-amp non invertente pulito (1-100x), binari a 4.2 V
        "hpf R=1M C=47n; opni Rg=10k Cg=4.7u Rf=pot(0,100k,A)+1k Cf=100p rail=4.2" PT_EFX_EQ,
        // 2 ODRIVE-TS: netlist del TS808 a catalogo (ed9) con TONE a meta' corsa
        "hpf R=510k C=20n; fbclip Rg=4.7k Cg=47n Rf=51k+pot(0,500k,A) Cf=51p d=si up=1 dn=1 rail=4.2; "
        "tonets R7=1k C4=220n P=20k t=0.5 Rw=220 Cw=220n Rf=1k Cf=0 rail=4.2; hpf R=100k C=1u" PT_EFX_EQ,
        // 3 ODRIVE-BLUES: tipo Bluesbreaker, due op-amp con diodi nella retroazione del secondo
        "hpf R=1M C=47n; opni Rg=10k Cg=1u Rf=10k+pot(0,100k,A) Cf=100p rail=4.2; "
        "fbclip Rg=10k Cg=100n Rf=100k Cf=220p d=si up=1 dn=1 rail=4.2; lpf R=10k C=6.8n" PT_EFX_EQ,
        // 4 ODRIVE-DB: 'Dumble in a box' generico: due stadi, diodi asimmetrici, stack con medie piene
        "hpf R=1M C=22n; opni Rg=4.7k Cg=470n Rf=22k+pot(0,100k,A) Cf=100p rail=4.2; "
        "fbclip Rg=10k Cg=100n Rf=47k+pot(0,250k,A) Cf=100p d=si up=1 dn=2 rail=4.2; "
        "fmv R1=250k R2=1M R3=25k R4=56k C1=250p C2=22n C3=22n t=0.6 m=0.6 l=0.4" PT_EFX_EQ,
        // 5 ODRIVE-AMP: 'amp in a box' a triodi (voce CRUNCH dell'IR-2), stack fisso
        "hpf R=470k C=22n; triode Rp=100k Rk=1.5k B=300 byp=1; gain a=taper(0,A)*0.25+0.008; triode Rp=100k Rk=1.5k B=300 byp=1; "
        "hpf R=470k C=22n; lpf R=100k C=220p; gain a=0.03; triode Rp=100k Rk=1.5k B=300 byp=1; "
        "fmv R1=220k R2=1M R3=22k R4=33k C1=470p C2=22n C3=22n t=0.5 m=0.5 l=0.5; gain a=0.075; power V=1 g=1 sag=0.2" PT_EFX_EQ,
        // 6 DIST-SC1: distorsione classica a medi scavati (netlist del DS-1 a catalogo, TONE a meta')
        "hpf R=470k C=47n; hpf R=10k C=26n; bjt g=60 vp=4.8 vn=3.6 soft=2.2; lpf R=10k C=1n; "
        "opni Rg=4.7k Cg=1u Rf=pot(0,100k,B)+10 Cf=100p rail=3.8; dclip R=2.2k C=10n d=si; tonebm R1=6.8k C1=100n R2=6.8k C2=22n t=0.5" PT_EFX_EQ,
        // 7 DIST-BRITISH: 'JCM in a box' a triodi con catodo freddo (voce SUPER LEAD dell'IR-2)
        "hpf R=470k C=4.7n; triode Rp=100k Rk=1.5k B=300 byp=1; gain a=taper(0,A)*1+0.03; triode Rp=100k Rk=1.5k B=300 byp=1; "
        "hpf R=470k C=22n; lpf R=100k C=470p; gain a=0.15; triode Rp=100k Rk=10k B=300 byp=1; "
        "fmv R1=250k R2=1M R3=25k R4=47k C1=470p C2=22n C3=22n t=0.6 m=0.5 l=0.5; gain a=0.028; power V=1 g=1 sag=0.15" PT_EFX_EQ,
        // 8 DIST-METAL1: netlist dell'MT-2 a catalogo con l'EQ a meta' corsa
        "hpf R=1M C=47n; hpf R=100k C=15n; peak f=950 Q=0.223 g=36.2; hshelf f=10k Q=0.53 g=11.6; lpf R=220k C=100p rail=3.8; gain db=-6; "
        "lpf R=5k C=47n; hpf R=100k C=33n; opni Rg=1k Cg=10u Rf=pot(0,250k,A)*1+1k Cf=47p rail=3.8; dclip R=2.2k C=0 d=si; "
        "shelf1 R=4.7k Cs=15n Cp=32n; peak f=4900 Q=0.80 g=10.1; peak f=110 Q=1.54 g=5.9; peak f=1100 Q=0.6 g=0" PT_EFX_EQ,
        // 9 DIST-METAL2: come METAL1 con passa-alto d'ingresso piu' alto (bassi stretti), piu' guadagno e medie scavate
        "hpf R=1M C=4.7n; hpf R=100k C=15n; peak f=950 Q=0.223 g=36.2; hshelf f=10k Q=0.53 g=11.6; lpf R=220k C=100p rail=3.8; gain db=-3; "
        "lpf R=5k C=47n; hpf R=100k C=33n; opni Rg=1k Cg=10u Rf=pot(0,500k,A)*1+1k Cf=47p rail=3.8; dclip R=2.2k C=0 d=si; "
        "shelf1 R=4.7k Cs=15n Cp=32n; peak f=4900 Q=0.80 g=10.1; peak f=110 Q=1.54 g=7; peak f=700 Q=0.7 g=-6" PT_EFX_EQ,
    };
#undef PT_EFX_EQ
}
