/*
    Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3 (vedi LICENSE)

    Dati della tappa 6 per i simulatori di cassa (incluso SOLO da FxStage6Cab.cpp). GENERATO da
    ~/.config/PedalTrinity/stage6/work/cab/py/gen_data.py a partire dalla ricerca stage6_cabs.json:
      - altoparlanti: voce = 5 campane f/Q/dB + passa-basso del cono (lp, 0,8 e 1,18 lp, 0,6), adattate ai minimi
        quadrati (regolarizzati) ai punti pubblicati relativi a 1 kHz (curva interpolata a 1/12 di ottava);
        il passa-alto del baffle dell'adattamento NON si usa: nel mobile lo sostituisce quello della cassa;
      - microfoni: filtri della curva del produttore in campo lontano ("filters_farfield") e peso del gradiente
        alfa del diagramma polare (0 omni, 0,5 cardioide, 0,63 supercardioide, 0,75 ipercardioide, 1 figura a 8);
      - posizioni: differenza rispetto al centro (delta_points) adattata con mensola bassa + campana + mensola alta;
      - casse: misure esterne, retro (0 chiuso, 0,5 semiaperto, 1 aperto), porta reflex, materiale, coni (centri in %
        della faccia, x da sinistra, y dall'alto), passa-alto e risonanza del sistema (engine_hints).
    Nessuna IR o risposta di prodotti esistenti e' copiata: le curve servono a generare risposte procedurali.
*/

#pragma once

#include <cstring>

namespace pt::engine::i6
{
    enum FiltType : int { FPeak = 0, FLow, FHigh };
    struct Filt { int type; float f, q, db; };

    struct Speaker { const char* id; float sizeIn, fs, lp; float bell[5][3]; };
    struct Mic { const char* id; float alpha; int nf; Filt ff[5]; };
    enum PosKind : int { OnCone = 0, Distance, Room, Back, Between };
    struct Position { const char* id; int kind; float distCm, radial; int nf; Filt f[3]; };
    struct Material { const char* name; double E, rho, panelDb, panelQ, modeDb, modeQ; };
    struct Cab
    {
        const char* id; int bass; float w, h, d, vol, open; int ported; float fb; int material; float thick;
        int nspk; float sx[8], sy[8]; float fres, qres, gres, hp; int defSpk; float defFs;
    };

    inline constexpr int kNumMaterials = 6;
    inline const Material kMaterials[kNumMaterials] = {
        { "betulla", 9e+09, 680.0, 1.5f, 3.0f, 1.2f, 6.0f },
        { "pino", 9e+09, 450.0, 2.0f, 2.5f, 1.0f, 5.0f },
        { "mdf", 3.5e+09, 750.0, 0.8f, 2.0f, 0.7f, 4.0f },
        { "okoume", 7e+09, 440.0, 1.8f, 3.0f, 1.3f, 6.0f },
        { "pioppo", 8e+09, 450.0, 1.5f, 2.8f, 1.1f, 5.0f },
        { "multistrato", 8e+09, 650.0, 1.2f, 2.8f, 1.0f, 5.0f },
    };

    inline constexpr int kNumSpeakers = 30;
    inline const Speaker kSpeakers[kNumSpeakers] = {
        { "spv30", 12.0f, 75.0f, 6572.0f, { { 3515.0f, 2.013f, 7.58f }, { 2224.0f, 2.893f, 5.22f }, { 794.0f, 1.173f, 2.09f }, { 4697.0f, 4.5f, 3.0f }, { 6954.0f, 1.341f, -12.0f } } },   // Celestion Vintage 30, rms 0.69 dB
        { "spg12m", 12.0f, 75.0f, 7736.0f, { { 3733.0f, 1.568f, 11.33f }, { 2348.0f, 4.5f, 5.19f }, { 2024.0f, 4.5f, 3.43f }, { 8000.0f, 1.633f, -12.0f }, { 739.0f, 1.616f, 3.69f } } },   // Celestion G12M Greenback, rms 0.95 dB
        { "spg12h30", 12.0f, 85.0f, 5033.0f, { { 4794.0f, 2.392f, 5.45f }, { 2443.0f, 1.844f, 8.14f }, { 1531.0f, 2.484f, -5.42f }, { 3527.0f, 4.5f, 5.36f }, { 1735.0f, 4.5f, 1.1f } } },   // Celestion G12H Anniversary, rms 0.62 dB
        { "spg12t75", 12.0f, 85.0f, 4627.0f, { { 2439.0f, 1.974f, 7.58f }, { 1282.0f, 4.5f, -3.87f }, { 6210.0f, 4.5f, -9.47f }, { 8000.0f, 0.4f, 4.13f }, { 822.0f, 1.481f, 1.95f } } },   // Celestion G12T-75, rms 1.01 dB
        { "spalnicoblue", 12.0f, 75.0f, 6213.0f, { { 1466.0f, 4.5f, -5.98f }, { 2379.0f, 2.244f, 8.72f }, { 4083.0f, 1.955f, 7.1f }, { 8000.0f, 4.5f, -6.28f }, { 780.0f, 1.254f, 3.55f } } },   // Celestion Alnico Blue, rms 0.62 dB
        { "spalnicogold", 12.0f, 75.0f, 6484.0f, { { 1466.0f, 4.5f, -6.09f }, { 4010.0f, 2.326f, 7.06f }, { 2289.0f, 2.906f, 7.51f }, { 873.0f, 1.493f, 3.57f }, { 8000.0f, 4.5f, -10.87f } } },   // Celestion Alnico Gold, rms 0.74 dB
        { "spalnicocream", 12.0f, 75.0f, 3687.0f, { { 1463.0f, 1.715f, -10.72f }, { 2300.0f, 1.512f, 3.73f }, { 4859.0f, 1.821f, 6.81f }, { 1005.0f, 4.5f, 1.65f }, { 310.0f, 0.4f, -5.76f } } },   // Celestion Alnico Cream, rms 0.68 dB
        { "spg1265", 12.0f, 85.0f, 6301.0f, { { 2381.0f, 4.5f, 6.65f }, { 3216.0f, 4.5f, 6.44f }, { 1360.0f, 4.5f, -6.81f }, { 365.0f, 2.189f, -2.24f }, { 8000.0f, 1.463f, -8.62f } } },   // Celestion G12-65 Heritage, rms 1.21 dB
        { "spcreamback", 12.0f, 75.0f, 4210.0f, { { 1426.0f, 2.001f, -9.12f }, { 4342.0f, 1.638f, 5.83f }, { 2339.0f, 2.444f, 5.13f }, { 542.0f, 4.5f, 0.77f }, { 360.0f, 0.985f, -3.45f } } },   // Celestion G12M-65 Creamback, rms 0.71 dB
        { "spg10", 10.0f, 98.0f, 5222.0f, { { 1795.0f, 2.889f, -7.04f }, { 2093.0f, 2.759f, 5.57f }, { 3753.0f, 1.4f, 8.22f }, { 5655.0f, 4.5f, 1.45f }, { 922.0f, 1.092f, 2.74f } } },   // Celestion G10 Greenback, rms 0.44 dB
        { "spg10v", 10.0f, 115.0f, 5536.0f, { { 2770.0f, 0.984f, 9.6f }, { 1684.0f, 4.5f, -5.07f }, { 1305.0f, 3.794f, 3.36f }, { 399.0f, 0.741f, 5.78f }, { 374.0f, 1.855f, -4.64f } } },   // Celestion G10 Vintage, rms 0.69 dB
        { "spg12k100", 12.0f, 85.0f, 4969.0f, { { 3271.0f, 1.46f, 7.82f }, { 2288.0f, 2.981f, 4.64f }, { 1374.0f, 3.119f, -4.29f }, { 645.0f, 1.852f, 1.5f }, { 150.0f, 1.655f, 0.61f } } },   // Celestion G12K-100, rms 0.61 dB
        { "spc90", 12.0f, 85.0f, 5167.0f, { { 2726.0f, 1.586f, 8.5f }, { 5035.0f, 4.5f, 8.02f }, { 1283.0f, 4.5f, -3.96f }, { 6403.0f, 1.377f, -8.05f }, { 632.0f, 2.256f, 1.31f } } },   // Celestion (per Mesa/Boogie) Custom 90, rms 0.69 dB
        { "spp10r", 10.0f, 97.0f, 8075.0f, { { 2059.0f, 0.748f, 8.77f }, { 410.0f, 1.891f, -4.65f }, { 3896.0f, 1.371f, 8.59f }, { 8000.0f, 2.702f, -4.19f }, { 1658.0f, 4.5f, -6.45f } } },   // Jensen P10R, rms 0.53 dB
        { "spp12r", 12.0f, 80.0f, 7293.0f, { { 1837.0f, 1.633f, 12.0f }, { 378.0f, 2.249f, -4.31f }, { 2608.0f, 1.29f, 7.58f }, { 3720.0f, 2.042f, 6.43f }, { 238.0f, 0.4f, 3.04f } } },   // Jensen P12R, rms 0.95 dB
        { "spc12n", 12.0f, 113.0f, 4486.0f, { { 2067.0f, 1.388f, 12.0f }, { 1237.0f, 1.9f, 7.89f }, { 6948.0f, 0.907f, 12.0f }, { 3660.0f, 4.5f, 5.26f }, { 1490.0f, 2.871f, -8.84f } } },   // Jensen C12N, rms 0.82 dB
        { "spc12q", 12.0f, 87.5f, 4997.0f, { { 2143.0f, 2.667f, 12.0f }, { 393.0f, 2.507f, -3.4f }, { 3442.0f, 1.453f, 12.0f }, { 6302.0f, 1.426f, -9.23f }, { 829.0f, 2.643f, 3.16f } } },   // Jensen C12Q, rms 1.07 dB
        { "spp12q", 12.0f, 90.5f, 3807.0f, { { 2036.0f, 3.798f, 4.16f }, { 400.0f, 1.307f, -5.08f }, { 3246.0f, 0.587f, 12.0f }, { 2579.0f, 3.151f, -1.1f }, { 1496.0f, 4.5f, -7.03f } } },   // Jensen P12Q, rms 0.72 dB
        { "speminlegend", 12.0f, 94.0f, 4872.0f, { { 2546.0f, 0.858f, 12.0f }, { 346.0f, 0.4f, -6.68f }, { 4726.0f, 1.436f, -12.0f }, { 992.0f, 3.85f, 1.86f }, { 1447.0f, 2.537f, -7.24f } } },   // Eminence Legend 1258, rms 0.91 dB
        { "sptexasheat", 12.0f, 79.0f, 5719.0f, { { 2412.0f, 1.113f, 9.5f }, { 1449.0f, 2.396f, -11.28f }, { 7826.0f, 1.618f, -7.36f }, { 329.0f, 0.87f, -2.6f }, { 1014.0f, 3.047f, 1.79f } } },   // Eminence Texas Heat, rms 0.34 dB
        { "spevm12l", 12.0f, 55.0f, 5341.0f, { { 3125.0f, 1.34f, 12.0f }, { 995.0f, 3.103f, 2.14f }, { 8000.0f, 4.5f, -5.48f }, { 5241.0f, 3.836f, 2.98f }, { 626.0f, 0.689f, 6.05f } } },   // Electro-Voice EVM12L Classic, rms 0.58 dB
        { "spjbld120", 12.0f, 75.0f, 8288.0f, { { 2923.0f, 0.4f, 7.62f }, { 986.0f, 1.873f, -1.08f }, { 6205.0f, 2.633f, -6.2f }, { 326.0f, 0.805f, 1.1f }, { 8000.0f, 2.172f, -6.77f } } },   // JBL D120F, rms 0.19 dB
        { "spjbld130", 15.0f, 40.0f, 8456.0f, { { 3142.0f, 0.91f, 3.98f }, { 1735.0f, 0.631f, 6.21f }, { 6070.0f, 2.429f, -5.53f }, { 8000.0f, 1.413f, -6.29f }, { 617.0f, 0.4f, 3.0f } } },   // JBL D130F, rms 0.13 dB
        { "spweber", 12.0f, 98.8f, 4058.0f, { { 2073.0f, 1.006f, 10.2f }, { 150.0f, 0.4f, -0.93f }, { 4014.0f, 1.464f, 7.55f }, { 405.0f, 1.555f, -4.67f }, { 1504.0f, 4.5f, -8.69f } } },   // Weber (Ted Weber) 12A125A, rms 0.69 dB
        { "spfane", 12.0f, 79.5f, 6266.0f, { { 2959.0f, 0.4f, 12.0f }, { 1517.0f, 1.921f, -5.31f }, { 6231.0f, 2.972f, -12.0f }, { 441.0f, 0.733f, 5.54f }, { 1966.0f, 1.726f, 4.54f } } },   // Fane Fane 12" Hiwatt, rms 0.62 dB
        { "spsheffield", 12.0f, 97.7f, 4946.0f, { { 5013.0f, 4.5f, 2.1f }, { 1486.0f, 2.721f, -8.05f }, { 988.0f, 3.585f, 3.22f }, { 306.0f, 2.934f, -0.78f }, { 2493.0f, 1.055f, 11.25f } } },   // Peavey (Sheffield) Sheffield 1200-16, rms 0.32 dB
        { "spbass10", 10.0f, 52.0f, 5841.0f, { { 2615.0f, 4.5f, 7.05f }, { 601.0f, 2.018f, -8.62f }, { 4546.0f, 4.5f, 7.72f }, { 628.0f, 0.763f, 6.81f }, { 7002.0f, 4.5f, -12.0f } } },   // Eminence (per Ampeg) SVT-810E 10", rms 1.57 dB
        { "spbass15", 15.0f, 73.0f, 5506.0f, { { 2574.0f, 2.308f, 6.13f }, { 1775.0f, 1.087f, 12.0f }, { 4307.0f, 2.149f, -12.0f }, { 152.0f, 4.5f, 1.49f }, { 398.0f, 2.436f, -3.23f } } },   // Jensen C15N (Vintage reissue), rms 1.20 dB
        { "spgk10", 10.0f, 97.5f, 5198.0f, { { 4280.0f, 0.589f, -12.0f }, { 1392.0f, 0.987f, 9.44f }, { 6094.0f, 4.5f, -12.0f }, { 627.0f, 2.817f, -2.01f }, { 472.0f, 4.5f, -1.7f } } },   // Gallien-Krueger (OEM) P10/200, rms 1.20 dB
        { "spjc12", 12.0f, 85.0f, 7258.0f, { { 2622.0f, 1.691f, 1.1f }, { 8000.0f, 1.321f, -2.78f }, { 586.0f, 0.4f, -0.87f }, { 173.0f, 1.024f, -0.93f }, { 1930.0f, 1.742f, 0.47f } } },   // Roland (OEM, produttore non documentato) altoparlante 12" del JC-120, rms 0.08 dB
    };

    inline constexpr int kNumMics = 11;
    inline const Mic kMics[kNumMics] = {
        { "mcsm57", 0.5f, 4, { { FLow, 88.0f, 0.7f, -9.8f }, { FPeak, 5533.0f, 2.86f, 2.8f }, { FPeak, 6833.0f, 0.35f, 3.8f }, { FHigh, 13358.0f, 0.7f, -4.6f } } },   // Shure SM57
        { "mcmd421", 0.5f, 4, { { FLow, 53.0f, 0.7f, -8.1f }, { FPeak, 6515.0f, 0.63f, 0.8f }, { FPeak, 4416.0f, 1.27f, 3.8f }, { FHigh, 2388.0f, 0.7f, 4.1f } } },   // Sennheiser MD 421-II
        { "mce906", 0.63f, 3, { { FLow, 100.0f, 0.7f, -12.5f }, { FPeak, 4380.0f, 2.26f, 4.8f }, { FHigh, 12056.0f, 0.7f, -6.6f } } },   // Sennheiser e 906
        { "mcr121", 1.0f, 3, { { FLow, 88.0f, 0.7f, 3.7f }, { FPeak, 3731.0f, 0.4f, 3.9f }, { FHigh, 12453.0f, 0.7f, -0.8f } } },   // Royer R-121
        { "mcm160", 0.75f, 3, { { FLow, 77.0f, 0.7f, -14.9f }, { FPeak, 786.0f, 0.44f, -2.2f }, { FHigh, 10422.0f, 0.7f, -4.1f } } },   // beyerdynamic M 160
        { "mcc414", 0.5f, 4, { { FLow, 588.0f, 0.7f, -0.2f }, { FPeak, 3937.0f, 1.32f, 1.4f }, { FPeak, 1901.0f, 1.55f, -1.6f }, { FHigh, 10592.0f, 0.7f, 3.1f } } },   // AKG C414 XLS / C414 XLII
        { "mcu87", 0.5f, 3, { { FLow, 52.0f, 0.7f, -3.1f }, { FPeak, 9319.0f, 1.18f, 2.6f }, { FHigh, 13872.0f, 0.7f, -1.8f } } },   // Neumann U 87 Ai
        { "mcu47fet", 0.55f, 4, { { FLow, 235.0f, 0.7f, -1.8f }, { FPeak, 3445.0f, 0.53f, 2.8f }, { FPeak, 10059.0f, 1.63f, 2.8f }, { FHigh, 12725.0f, 0.7f, 0.0f } } },   // Neumann U 47 fet
        { "mcmd441", 0.63f, 3, { { FLow, 51.0f, 0.7f, -6.8f }, { FPeak, 3628.0f, 4.28f, -1.2f }, { FHigh, 12588.0f, 0.7f, -2.9f } } },   // Sennheiser MD 441-U
        { "mcsm7b", 0.5f, 3, { { FLow, 65.0f, 0.7f, -3.8f }, { FPeak, 1932.0f, 0.32f, 1.6f }, { FHigh, 14252.0f, 0.7f, -3.8f } } },   // Shure SM7B
        { "mcat4050", 0.5f, 4, { { FLow, 174.0f, 0.7f, -0.8f }, { FPeak, 10801.0f, 1.54f, 3.8f }, { FPeak, 2489.0f, 3.27f, 1.4f }, { FHigh, 14961.0f, 0.7f, 0.0f } } },   // Audio-Technica AT4050
    };

    inline constexpr int kNumPositions = 12;
    inline const Position kPositions[kNumPositions] = {
        { "mpcenter", OnCone, 2.5f, 0.0f, 0, { {} } },
        { "mpcapedge", OnCone, 2.5f, 0.3f, 3, { { FLow, 600.0f, 0.7f, 0.0472478f }, { FPeak, 1727.51f, 0.988043f, -2.4803f }, { FHigh, 4121.66f, 0.7f, -5.51687f } } },   // delta adattato, rms 0.21 dB
        { "mpcone", OnCone, 2.5f, 0.6f, 3, { { FLow, 600.0f, 0.7f, 0.0784629f }, { FPeak, 1657.33f, 1.1321f, -5.04034f }, { FHigh, 4328.29f, 0.7f, -8.74242f } } },   // delta adattato, rms 0.17 dB
        { "mpedge", OnCone, 2.5f, 0.9f, 3, { { FLow, 600.0f, 0.7f, 0.178288f }, { FPeak, 1635.11f, 0.940448f, -7.47052f }, { FHigh, 4169.66f, 0.7f, -11.7851f } } },   // delta adattato, rms 0.25 dB
        { "mpoffaxis", OnCone, 2.5f, 0.3f, 3, { { FLow, 169.59f, 0.7f, -0.461063f }, { FPeak, 5285.39f, 0.695921f, -3.33477f }, { FHigh, 9000.0f, 0.7f, -8.8783f } } },   // delta adattato, rms 0.08 dB
        { "mpdist1", Distance, 1.0f, 0.0f, 0, { {} } },
        { "mpdist5", Distance, 5.0f, 0.0f, 0, { {} } },
        { "mpdist15", Distance, 15.0f, 0.0f, 0, { {} } },
        { "mpdist30", Distance, 30.0f, 0.0f, 0, { {} } },
        { "mproom", Room, 150.0f, 0.0f, 0, { {} } },
        { "mpback", Back, 10.0f, 0.0f, 3, { { FLow, 217.064f, 0.7f, 2.97244f }, { FPeak, 5565.19f, 0.456947f, -8.4782f }, { FHigh, 9000.0f, 0.7f, -15.2771f } } },   // delta adattato, rms 0.21 dB
        { "mpbetween", Between, 3.5f, 0.0f, 3, { { FLow, 560.695f, 0.7f, 1.96578f }, { FPeak, 3627.65f, 0.444929f, 6.0f }, { FHigh, 1959.54f, 0.7f, -20.0f } } },   // delta adattato, rms 0.19 dB
    };

    inline constexpr int kPosCapEdge = 1;
    inline constexpr int kPosBack = 10;
    inline constexpr int kPosRoom = 9;

    inline constexpr int kNumCabs = 33;
    inline const Cab kCabs[kNumCabs] = {
        { "cbprinceton110", 0, 505.0f, 406.0f, 241.0f, 38.2f, 1.0f, 0, 0.0f, 1, 19.0f, 1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 115.0f, 1.3f, 4.0f, 85.0f, 13, 97.0f },
        { "cbdeluxe112", 0, 614.0f, 429.0f, 241.0f, 50.0f, 1.0f, 0, 0.0f, 1, 19.0f, 1, { 56.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 100.0f, 1.3f, 4.0f, 72.0f, 15, 113.0f },
        { "cbtwin212", 0, 664.0f, 508.0f, 267.0f, 73.0f, 1.0f, 0, 0.0f, 1, 19.0f, 2, { 25.0f, 75.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 59.0f, 59.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 100.0f, 1.3f, 4.0f, 70.0f, 15, 113.0f },
        { "cbbassman410", 0, 597.0f, 575.0f, 267.0f, 74.4f, 1.0f, 0, 0.0f, 1, 19.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 31.0f, 31.0f, 72.0f, 72.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 105.0f, 1.3f, 4.5f, 70.0f, 13, 97.0f },
        { "cbac30212", 0, 702.0f, 556.0f, 265.0f, 84.5f, 1.0f, 0, 0.0f, 5, 18.0f, 2, { 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 57.0f, 57.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 100.0f, 1.4f, 4.5f, 70.0f, 4, 75.0f },
        { "cbac15112", 0, 602.0f, 456.0f, 265.0f, 58.0f, 1.0f, 0, 0.0f, 5, 18.0f, 1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 57.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 105.0f, 1.4f, 4.0f, 75.0f, 1, 75.0f },
        { "cbv212blue", 0, 705.0f, 550.0f, 260.0f, 81.2f, 0.5f, 0, 0.0f, 0, 18.0f, 2, { 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 52.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 95.0f, 1.4f, 4.5f, 68.0f, 4, 75.0f },
        { "cbm1960a", 0, 770.0f, 755.0f, 365.0f, 160.4f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 28.0f, 72.0f, 28.0f, 72.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 74.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 90.0f, 1.7f, 6.0f, 60.0f, 3, 85.0f },
        { "cbm1960b", 0, 770.0f, 755.0f, 365.0f, 173.6f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 28.0f, 72.0f, 28.0f, 72.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 74.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 86.0f, 1.7f, 6.5f, 58.0f, 3, 85.0f },
        { "cbm1960bw", 0, 770.0f, 820.0f, 365.0f, 174.9f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 28.0f, 72.0f, 28.0f, 72.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 26.0f, 26.0f, 74.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 88.0f, 1.5f, 5.0f, 64.0f, 1, 75.0f },
        { "cbm1936", 0, 740.0f, 600.0f, 305.0f, 106.8f, 0.0f, 0, 0.0f, 2, 18.0f, 2, { 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 52.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 100.0f, 1.6f, 5.5f, 66.0f, 3, 85.0f },
        { "cbhiwatt412", 0, 762.0f, 762.0f, 330.0f, 155.0f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 90.0f, 1.6f, 5.5f, 60.0f, 24, 79.5f },
        { "cbppc412", 0, 780.0f, 740.0f, 370.0f, 174.9f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 28.0f, 28.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 82.0f, 1.6f, 6.0f, 60.0f, 0, 75.0f },
        { "cbppc212", 0, 780.0f, 530.0f, 375.0f, 124.6f, 0.0f, 0, 0.0f, 0, 18.0f, 2, { 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 52.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 95.0f, 1.5f, 5.0f, 64.0f, 0, 75.0f },
        { "cbmatchless212", 0, 686.0f, 533.0f, 267.0f, 76.3f, 1.0f, 0, 0.0f, 0, 19.0f, 2, { 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 56.0f, 56.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 95.0f, 1.4f, 5.0f, 68.0f, 8, 75.0f },
        { "cbjc120212", 0, 760.0f, 622.0f, 280.0f, 103.5f, 0.0f, 0, 0.0f, 5, 18.0f, 2, { 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 60.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 92.0f, 1.3f, 4.0f, 65.0f, 29, 85.0f },
        { "cbrecto412", 0, 765.0f, 835.0f, 362.0f, 187.7f, 0.0f, 0, 0.0f, 0, 19.0f, 4, { 26.0f, 74.0f, 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 28.0f, 28.0f, 72.0f, 72.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 75.0f, 1.8f, 7.0f, 55.0f, 0, 75.0f },
        { "cbrecto412tr", 0, 759.0f, 768.0f, 362.0f, 170.5f, 0.0f, 0, 0.0f, 0, 19.0f, 4, { 26.0f, 74.0f, 26.0f, 74.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 80.0f, 1.7f, 6.5f, 58.0f, 0, 75.0f },
        { "cbmesa112", 0, 476.0f, 400.0f, 324.0f, 45.3f, 0.0f, 1, 70.0f, 0, 19.0f, 1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 95.0f, 1.2f, 3.5f, 62.0f, 12, 85.0f },
        { "cbmesa212", 0, 765.0f, 444.0f, 362.0f, 95.6f, 0.0f, 0, 0.0f, 0, 19.0f, 2, { 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 50.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 80.0f, 1.6f, 6.0f, 58.0f, 0, 75.0f },
        { "cb5150412", 0, 762.0f, 822.0f, 359.0f, 184.3f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 80.0f, 1.7f, 6.0f, 60.0f, 25, 97.7f },
        { "cbsoldano412", 0, 711.0f, 737.0f, 356.0f, 151.4f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 88.0f, 1.6f, 6.0f, 62.0f, 0, 75.0f },
        { "cbengl412", 0, 750.0f, 750.0f, 360.0f, 152.4f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 80.0f, 1.7f, 6.5f, 58.0f, 0, 75.0f },
        { "cbdiezel412", 0, 745.0f, 745.0f, 365.0f, 165.4f, 0.0f, 0, 0.0f, 3, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 82.0f, 1.7f, 6.5f, 60.0f, 0, 75.0f },
        { "cbbogner412", 0, 756.0f, 756.0f, 356.0f, 165.9f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 82.0f, 1.7f, 6.0f, 58.0f, 0, 75.0f },
        { "cbfriedman412", 0, 762.0f, 762.0f, 356.0f, 168.7f, 0.0f, 0, 0.0f, 0, 18.0f, 4, { 27.0f, 73.0f, 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 85.0f, 1.6f, 5.5f, 60.0f, 0, 75.0f },
        { "cbfriedman212", 0, 762.0f, 508.0f, 305.0f, 92.2f, 0.0f, 1, 75.0f, 0, 18.0f, 2, { 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 50.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 92.0f, 1.3f, 4.5f, 65.0f, 0, 75.0f },
        { "cbsvt810", 1, 660.0f, 1219.0f, 407.0f, 262.5f, 0.0f, 0, 0.0f, 4, 18.0f, 8, { 27.0f, 73.0f, 27.0f, 73.0f, 27.0f, 73.0f, 27.0f, 73.0f }, { 13.0f, 13.0f, 36.0f, 36.0f, 60.0f, 60.0f, 83.0f, 83.0f }, 95.0f, 1.0f, 3.0f, 62.0f, 26, 52.0f },
        { "cbsvt410", 1, 610.0f, 635.0f, 407.0f, 127.6f, 0.0f, 0, 0.0f, 5, 18.0f, 5, { 27.0f, 73.0f, 27.0f, 73.0f, 50.0f, 0.0f, 0.0f, 0.0f }, { 27.0f, 27.0f, 73.0f, 73.0f, 50.0f, 0.0f, 0.0f, 0.0f }, 65.0f, 1.5f, 5.0f, 45.0f, 26, 52.0f },
        { "cbb15115", 1, 533.0f, 533.0f, 356.0f, 81.3f, 0.0f, 1, 65.0f, 4, 16.0f, 1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 105.0f, 1.5f, 4.5f, 75.0f, 27, 73.0f },
        { "cbgk410", 1, 610.0f, 698.0f, 457.0f, 158.2f, 0.0f, 1, 48.0f, 4, 19.0f, 5, { 27.0f, 73.0f, 27.0f, 73.0f, 50.0f, 0.0f, 0.0f, 0.0f }, { 33.0f, 33.0f, 72.0f, 72.0f, 10.0f, 0.0f, 0.0f, 0.0f }, 72.0f, 1.2f, 3.5f, 40.0f, 28, 97.5f },
        { "syn1x8", 0, 400.0f, 380.0f, 200.0f, 20.0f, 1.0f, 0, 0.0f, 5, 15.0f, 1, { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 58.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 130.0f, 1.2f, 3.5f, 100.0f, 9, 98.0f },
        { "syn2x10", 0, 620.0f, 470.0f, 240.0f, 55.0f, 1.0f, 0, 0.0f, 1, 19.0f, 2, { 27.0f, 73.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 58.0f, 58.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 105.0f, 1.3f, 4.0f, 75.0f, 13, 97.0f },
    };

    /** Formati del laboratorio (indici in kCabs): 1x8 open, 1x10 open, 1x12 open, 1x12 ported, 2x10 open, 2x12 open, 2x12 closed, 4x10 open, 4x12 closed, 4x10 bass, 8x10 bass, 1x15 bass. */
    inline const int kLabFormats[12] = { 31, 0, 1, 18, 32, 4, 13, 3, 8, 28, 27, 29 };

    inline int findCab (const char* id) { for (int i = 0; i < kNumCabs; ++i) if (std::strcmp (kCabs[i].id, id) == 0) return i; return -1; }
    inline int findSpeaker (const char* id) { for (int i = 0; i < kNumSpeakers; ++i) if (std::strcmp (kSpeakers[i].id, id) == 0) return i; return -1; }
    inline int findMic (const char* id) { for (int i = 0; i < kNumMics; ++i) if (std::strcmp (kMics[i].id, id) == 0) return i; return -1; }
    inline int findPosition (const char* id) { for (int i = 0; i < kNumPositions; ++i) if (std::strcmp (kPositions[i].id, id) == 0) return i; return -1; }
}
