# Circuiti di modulazione, delay analogici e filtri: pedali compatti BOSS (dossier)

Copertura: 36 pedali. Il file JSON gemello `circuits_modulation.json` ha lo stesso contenuto in forma strutturata.

Come sono state raccolte le informazioni:
- Schemi dei cloni Aion letti direttamente: CE-2, BF-2, DM-2, PH-1R, DC-2.
- Service note BOSS: CE-2/CE-2B, CE-3, CE-5, CH-1 (synthxl.com); PH-2 (manualslib).
- Schemi: schematicheaven, schematicsonline, experimentalistsanonymous, stompboxelectronics.
- Manuali e pagine di specifiche Roland/BOSS. Le pagine support.roland.com sono state lette solo tramite estratti di ricerca.
- Alcuni siti (freestompboxes, hobby-hour, electrosmash) erano bloccati da Cloudflare. In quei casi ho usato la Wayback Machine.

Legenda:
- **[S]** specifica ufficiale;
- **[D]** / *derived* valore calcolato dai componenti;
- **[?]** / *unverified* valore incerto.

Formula ricorrente per l'LFO a triangolo (Schmitt + integratore): f = Rfb/(4·Rhyst·Rint·C). Sul CE-3 la formula dà 0.32–3.56 Hz; la service note misura 0.33–3.7 Hz.

Lacune principali:
- frequenze di clock in Hz per CE-3, CE-5, CH-1 e DC-2 (le service note non le riportano);
- circuito del PN-2;
- modello del BBD in CE-2W, DC-2W, DM-2W e VB-2W;
- specifiche interne dei pedali digitali.

## Blocchi comuni BOSS (valgono per CE-2, CE-2B, BF-2, DM-2 e derivati)

- **Buffer d'ingresso**: emitter follower (2SC732TM-GR, orig.), 470k di bias da Vref, 10k d'emettitore, guadagno ~1.
- **Pre-enfasi** (op-amp invertente 4558): Rin = 47k || (10k + 6n8), Rf = 47k || 100p.
  - G_low = -1 (0 dB); G_high = -47k/(47k||10k) = -5.7 (+15.1 dB).
  - Zero a 1/(2π·6n8·57k) = **410 Hz**; polo a 1/(2π·6n8·10k) = **2.34 kHz**; LPF di stabilità 47k·100p ≈ 33.9 kHz.
- **De-enfasi** (mixer invertente): stessa rete nella retroazione, -15.1 dB sopra 2.34 kHz. Il dry esce piatto; il rumore e il clock del BBD vengono attenuati di 15 dB.
- **Filtro a 3 poli con un solo transistor** (Sallen-Key a guadagno unitario): R1-R2-R3; Ca da n1 a GND, Cb da n2 all'emettitore (retroazione), Cc dalla base a GND. Funzione di trasferimento ricavata per analisi nodale, con follower ideale (script sotto).
- **BBD**: ritardo t = N/(2·f_clk). MN3007/MN3207 (1024 stadi) danno t = 512/f_clk; MN3005/MN3205 (4096 stadi) danno t = 2048/f_clk. Il BBD campiona sui fronti del clock: il segnale viene ritardato di N/2 periodi e convoluto con un impulso rettangolare di 1 periodo (risposta sinc) [Holters-Parker DAFx-18].
- **Coppie di chip**: MN3101 va solo con MN3007/MN3005; MN3102 va con MN3207/MN3205. I pin di alimentazione sono invertiti tra le due famiglie [Aion].
- **Modellazione consigliata**: linea di ritardo di lunghezza fissa N/2, fatta avanzare al clock del BBD (variabile). Filtri d'ingresso e d'uscita scomposti in sezioni del 1° ordine in parallelo (fratti semplici), valutate agli istanti dei fronti di clock [Holters & Parker 2018]. Riferimenti: Raffel & Smith DAFx-10, "Practical Modeling of BBD Circuits".

Risposte calcolate (python, analisi nodale, follower ideali):

| Filtro | -3 dB | -20 dB | -40 dB | Picco |
|---|---|---|---|---|
| CE-2 AA/ricostruzione 10k×3, 3n3/8n2(fb)/470p | 6.8 kHz | 14.7 kHz | 31.7 kHz | nessuno |
| CE-2 percorso wet totale (AA×ricostruzione) | 5.83 kHz | 9.87 kHz | 14.7 kHz | nessuno |
| BF-2 pre SK2 10k/10k, 12n(fb)/150p | f0 11.9 kHz, Q 4.47 | — | — | +13 dB a 11.7 kHz |
| BF-2 post 3 poli 3n9/8n2/330p | 7.39 kHz | 16.4 kHz | 34.1 kHz | nessuno |
| BF-2 post MFB 47k/47k/22k, 1n/150p | f0 12.8 kHz, Q 0.91 | — | — | +0.75 dB |
| BF-2 percorso wet totale | 12.8 kHz | 15.7 kHz | 20.6 kHz | +3.2 dB a 10.9 kHz |
| DM-2 pre 3 poli 6n8/82n/330p | 3.23 kHz | 6.0 kHz | 12.9 kHz | +4.4 dB a 2.17 kHz |
| DM-2 post 3 poli 2n2/33n/1n | 2.89 kHz | 6.2 kHz | 16.1 kHz | +6.2 dB a 1.83 kHz |
| DM-2 post SK2 39n/330p | f0 4.44 kHz, Q 5.44 | — | — | +14.7 dB |
| DM-2 percorso wet totale | 3.27 kHz | 4.93 kHz | 6.14 kHz | **+11.8 dB a 1.97 kHz** |

Nota: nel DM-2 i Q alti sono voluti. Il clock minimo è 6.8 kHz (Nyquist 3.4 kHz), quindi serve un'attenuazione molto ripida. La gobba a circa 2 kHz spiega perché il pedale va in auto-oscillazione con Intensity a ore 12 circa. **Da verificare in SPICE**, perché i follower reali hanno guadagno di circa 0.99.

---

## CE-2 Chorus (1979) — analogico — confidenza ALTA

**Catena del segnale (numerazione Aion Azure)**
1. Buffer Q1: R1 1k, C1 47n, R2 470k, R3 10k.
2. Pre-enfasi IC1A: C2 470n, C3 6n8 + R4 10k in parallelo a R5 47k; retroazione R7 47k || C4 100p.
3. Passa-alto C8 33n / R13 100k verso VB, fc 48 Hz (la stessa R13 porta anche il bias del BBD).
4. Anti-alias a 3 poli: R14-16 10k, C9 3n3, C10 8n2 (fb), C11 470p, follower Q2 (R17 10k), poi R18 4k7 verso il BBD.
5. **MN3007** (1024 stadi) + **MN3101**. Alimentazione VD 9 V tramite 33R e zener da 9.1 V; trimmer di bias 10k tra due 4k7; carico d'uscita R19 56k.
6. Passa-alto C13 33n / R20 330k, fc 14.6 Hz. Poi ricostruzione a 3 poli identica all'anti-alias (R21-23, C14 3n3, C15 8n2, C16 470p, Q3).
7. Uscita wet C17 33n (nel CE-2B: 12n), R25 1M, poi 47k verso il mixer.
8. Mixer/de-enfasi IC1B: dry 47k, wet 47k, retroazione 47k || 100p || (6n8 + 10k). Mix fisso **50/50**.
9. Uscita: R11 470R, C7 1u, R12 100k.

**LFO**
- IC4 TL022: trigger di Schmitt (R30 47k, R31 33k) seguito da un integratore (R33 1M, C19 100n). Forma d'onda **triangolare**.
- Formula: f = k·R30/(4·R31·R33·C19) = 3.56·k Hz. Il pot Rate (100kB) insieme a R32 10k fa da partitore dell'onda quadra: k va da 0.091 a 1, quindi **f ≈ 0.32–3.5 Hz** (ElectroSmash: 3.5 Hz al massimo).
- Il pot Depth (100kB) scala il triangolo. Segue un RC 220k/10n (**72 Hz**) e il follower Q5.

**Clock**
- Oscillatore RC del MN3101: C18 47p caricato tramite R29 150k e scaricato da Q4. Il livello di partenza della carica è fissato dall'LFO (attraverso D3).
- Misurato: con **Depth = 0 circa 110 kHz (4.65 ms)**; con **Depth a metà 100–128 kHz (5.1–4.0 ms)**.
- Con Depth al massimo: stima 85–150 kHz, circa 3.4–6 ms (confidenza bassa).

**Mod noti**: C18 da 47p a 100p aumenta la profondità.

**Fonti**
- https://aionfx.com/app/files/docs/azure_documentation.pdf
- https://www.electrosmash.com/boss-ce-2-analysis
- https://www.diystompboxes.com/smfforum/index.php?topic=89044.0
- https://aionfx.com/component/mn3007/

## CE-2B Bass Chorus (1987) — analogico — confidenza MEDIA
- Stesso circuito del CE-2. Differenze secondo la service note (numerazione BOSS):
  - C14 da 33n a 12n: è il condensatore d'uscita del wet (C17 nell'Aion). Il wet viene così filtrato passa-alto a circa 282 Hz contro il nodo virtuale da 47k (contro circa 103 Hz del CE-2), e i bassi restano non modulati.
  - C26/C27 da 470p a 220p.
  - R53 da 100R a 390R.
  - R42 da 1k a 100R.
  - Zener D6 da 5.1 V a 5.6 V.
  - Aggiunto E.Level VR4 250kC (livello del wet).
- Integrati: NJM4558, TL022, MN3007. Impedenza d'ingresso 470k, assorbimento 10 mA.
- Fonti:
  - https://aionfx.com/app/files/docs/azure_documentation.pdf
  - https://www.synthxl.com/wp-content/uploads/2019/12/Boss-CE-2_CE-2B_Chorus_Service_Manual.pdf
  - https://www.hobby-hour.com/electronics/s/boss-ce2b-bass-chorus.php

## BF-2 Flanger (1980–2001) — analogico — confidenza ALTA
**Catena del segnale (numerazione Aion Aerolith)**
1. Buffer Q1 e pre-enfasi IC1A (stessa rete del CE-2).
2. **Sommatore IC2A e limitatore morbido**:
   - Ingresso dry: R12 82k in parallelo a (R13 220k + C8 220p), poi C9 47n. Guadagno -0.57 in bassa frequenza, -0.78 sopra circa 3.3 kHz.
   - Ingresso della risonanza: C24 47n + R31 39k + RES_TRIM 22k.
   - Retroazione: R15 47k || (R16 4k7 + 1N914 in antiparallelo), cioè un soft-clip del segnale che entra nel BBD.
3. Anti-alias **SK a 2 poli**: 10k/10k, C10 12n (fb), C11 150p. f0 11.9 kHz, Q ≈ 4.5 (da verificare).
4. **MN3207** (1024 stadi) + **MN3102**. Alimentazione VC ≈ 5.6 V (78L05 più un 1N914 sulla massa). Trimmer di bias 22k, R20 100k.
5. Ricostruzione: 3 poli (C16 3n9, C17 8n2 fb, C18 330p, Q3), poi **MFB a 2 poli** IC2B (47k, 1n, 22k, 47k, 150p): f0 12.8 kHz, Q 0.91. In totale il filtro è del 5° ordine.
6. Risonanza: uscita di IC2B → C22 220n → C23 47n → pot Resonance 50kC → RES_TRIM → torna al sommatore IC2A. Guadagno massimo 47k/(39k + trim) ≤ 1.2. Il trimmer va regolato appena sotto la soglia di auto-oscillazione.
7. Mixer/de-enfasi IC1B: dry 47k, **wet 27k**, retroazione 47k. Il wet entra con guadagno 1.74 rispetto al dry.

**LFO**
- IC5 TL022 alimentato a circa 5.6 V: integratore IC5A con C = 2×33u in serie antiparallela (16.5 µF bipolare), R = 1k5 + pot Speed 250kC; Schmitt IC5B con 180k/220k.
- Formula: f = 1/(3.27·R·C), quindi **0.074–12.3 Hz**. Specifica BOSS: periodo 0.1–16 s. Forma d'onda triangolare.

**Clock**
- Controllato in corrente: Manual (50kB) → Q7; Depth (50kB) → R38 220k; specchio di corrente Q5/Q6; sorgente di corrente Q4 2N3906; C25 47p nel MN3102; trimmer CLOCK 500k.
- Service manual: **40 kHz con Manual al minimo** (12.8 ms) e **500 kHz ±20% al massimo** (1.02 ms). Aion ha misurato 586 kHz sul prototipo. Specifica: ritardo 1–13 ms.
- Taratura del bias: segnale di prova a 0 dBm.

**Fonti**
- https://aionfx.com/app/files/docs/aerolith_documentation.pdf
- https://mirosol.kapsi.fi/2014/12/boss-bf-2-flanger/
- https://www.hobby-hour.com/electronics/s/boss-bf2-flanger.php
- https://www.freestompboxes.org/viewtopic.php?t=31788

## DM-2 Delay (1981) — analogico — confidenza ALTA
**Catena del segnale (numerazione Aion Amethyst)**
1. Buffer Q1 e pre-enfasi IC1A. Qui entra anche il segnale di feedback.
2. **Compressore NE570/571 (sezione A)**: C9 220n sul raddrizzatore, τ = 10k·C ≈ 2.2 ms.
3. Anti-alias a 3 poli: 10k×3, C12 6n8, C13 82n (fb), C14 330p, Q2. Risposta: -3 dB a 3.2 kHz, picco +4.4 dB. Bias tramite R14 100k dal trimmer BIAS 22k.
4. **MN3005** (4096 stadi) + **MN3101** nella v1 del 1981. **MN3205 + MN3102** nella v2 del 1982, con zener da 8.2 V; il percorso audio è identico.
   - Le due uscite passano da 100k/100k e dal trimmer CANCEL 10k, che cancella il residuo di clock.
5. Ricostruzione: 3 poli (C19 2n2, C20 33n fb, C21 1n, Q3), poi SK a 2 poli (C22 39n fb, C23 330p, Q4). Percorso wet totale: -3 dB a 3.3 kHz, -40 dB a 6.1 kHz, gobba di circa +12 dB a 2 kHz (calcolata).
6. **Espansore NE570 (sezione B)**: C25 220n. Uscita verso i pot Intensity (50k) ed Echo (50k).
7. Il feedback torna all'ingresso tramite R27 22k + C28 100n. Mixer/de-enfasi IC1B con il dry sempre a guadagno unitario.

**Clock**
- Oscillatore RC del MN3101: C17 100p; R = pot Repeat Rate 1MB + trimmer CLOCK 1M + 10k/18k/22k.
- Taratura: **6.8 kHz (146 µs) con il ritardo massimo, cioè 300 ms**. Con 20 ms il clock sale a circa 102 kHz.
- Specifica: 20–300 ms.
- Nell'originale il pot "Repeat Rate" funziona al contrario (girando in senso orario il ritardo si accorcia).

**Fonti**
- https://aionfx.com/app/files/docs/amethyst_documentation.pdf
- https://www.hobby-hour.com/electronics/s/dm2-delay.php
- https://www.henrikhansson.com/product/dm-2-delay/
- https://thatdelaypedal.com/2019/01/14/boss-dm-2-all-you-need-to-know/

## PH-1R Phaser (1980) — analogico — confidenza MEDIA
**Catena del segnale (numerazione Aion Emerald)**
1. Buffer Q1.
2. IC1A: ingresso invertente (47k/47k, guadagno -1). Sull'ingresso **non invertente** arriva la risonanza: pot Resonance 10kB → R7 12k → C5 33n verso R6 47k || C4 100p a VB. Il feedback è quindi filtrato passa-alto a circa 82 Hz e amplificato fino a circa 1.6×. [?] Un secondo revisore legge invece il ritorno sul nodo invertente, con guadagno 47k/12k = 3.9 e passa-alto a 402 Hz: va verificato sullo schema originale. Specifica ufficiale: LFO 100 ms–16 s, Zin 470k. Il nodo VC ha 0.1 µF con una sorgente di circa 688k, cioè un passa-basso a circa 2.3 Hz: a rate elevati la profondità effettiva cala.
3. Partitore 1k5/1k5 (×0.5). Con questo il guadagno d'anello massimo resta intorno a 0.8.
4. **4 stadi all-pass con JFET**:
   - Ogni stadio: 10k in ingresso, 10k in retroazione; C 10n in serie verso l'ingresso +; nodo + verso VB tramite Rds || 100k.
   - Funzione: H = (sRC-1)/(sRC+1), con f90 = 1/(2π·R·10n). f_min = 159 Hz (limitata dal 100k); circa 8 kHz con Rds ≈ 2k.
   - Linearizzazione del gate: 330k dal drain (accoppiato con 10n) e 330k dal bus di controllo.
   - JFET 2SK30A-GR accoppiati per Vgs(off) entro 0.05 V (codici colore: arancio circa -1.72 V, verde circa -1.80 V, blu circa -1.94 V).
5. Mixer IC1B: dry 47k; wet 22k (dopo il partitore da 0.5, quindi circa 1:1). Pot Level 100kA.

**LFO**
- Oscillatore di rilassamento su TL022. Schmitt con soglia β = 150k/(150k+470k) = 0.242, che legge C19 (15 µF al tantalio) tramite 150k; C19 si carica tramite 4k7 + pot Rate 1MC.
- Periodo: T = 0.988·R·C, quindi **circa 0.067–14 Hz**. Forma d'onda triangolare a tratti esponenziali; buffer IC4B.
- Profondità: 68k → pot Depth 100kB → 2M2 verso il bus di gate, che è polarizzato da un trimmer di bias 100k tramite 1M.

**Fonti**
- https://aionfx.com/app/files/docs/emerald_documentation.pdf
- https://aionfx.com/project/emerald-resonant-phaser/

---

# Flanger / phaser / vibrato

## PH-1 (1977) — analogico — MEDIA
- [S] 4 stadi / 720°, LFO 16 s–100 ms; solo Rate e Depth. Stesso nucleo del PH-1R (2SK30A, 100k su D-S). [?] partitore 1/2 e guadagno di compensazione forse assenti.
- Fonti: https://support.roland.com/hc/en-us/articles/201932549-PH-1-Specifications ; https://forum.pedalpcb.com/threads/jfet-alternatives-for-boss-ph-1-phaser-can-i-use-any-matched-quad.23457/

## PH-2 Super Phaser (1984) — analogico OTA — MEDIA
- [S] 12 stadi / 2160°, LFO 14 s–100 ms, Zin 1M, circuito antilog nel controllo del filtro. Mode I: swell da alto a basso; Mode II: phasing marcato.
- 8 stadi OTA variabili (2× IR3109, 470p, attenuatori 68k/68k/560R) + 4 all-pass fissi a op-amp (10k/10k, 10n + 5k6, circa 2.8 kHz [?]). NE571 compressore prima / espansore dopo. HD14053 commuta i modi [?]. LFO: Schmitt + integratore IR9022, 2×33 µ in serie (16.5 µ), Rate 250kC + 2k2 → triangolo. Res 10kB, Depth 100kB.
- Fonti: https://cdn.roland.com/assets/media/pdf/PH-2_OM.pdf ; http://superphasermods.blogspot.com/2009/08/schematics.html ; https://forum.pedalpcb.com/threads/phabio-the-one-that-got-away.23410/ ; https://www.manualslib.com/manual/3362019/Boss-Ph-2.html

## PH-3 (2000) — digitale — MEDIA
- Selettore stadi 4/8/10/12 (12 = due da 6 in serie) + FALL, RISE (barber-pole), STEP. Rate/Depth/Res; con Depth al minimo lo sweep si ferma (tranne Rise/Fall). Tap 0.2–16 s/ciclo; EXP su Rate. Rumore −92 dBu.
- Fonte: https://images.equipboard.com/uploads/item/manual/867/boss-ph-3-phase-shifter-manual.pdf

## BF-1 (1977) — analogico — BASSA
- [S] ritardo 0.5–16 ms, sweep 100 ms–20 s. SAD1024 + clock CMOS; comandi come BF-2. Clock riportato 40 kHz–1 MHz [?] (per 16 ms servirebbero circa 32 kHz).
- Fonti: https://support.roland.com/hc/en-us/articles/201958039-BF-1-Specifications ; https://forum.pedalpcb.com/threads/bandit-of-arizona-boss-bf-1-flanger.23932/

## HF-2 Hi Band Flanger (1985) — analogico — MEDIA
- [S] 0.5–6.5 ms, LFO 100 ms–16 s. Scheda tipo BF-2 con **MN3204 (512 stadi)** + MN3102: t = 256/f_clk. Differenze: R10 22k (BF-2: 39k), trimmer CL 1MB (470kB), R46 100k (330k), filtro 33R/47 µ sui 5 V. Nessun controllo hi-cut: la "banda alta" viene dal BBD più corto.
- Fonti: https://support.roland.com/hc/en-us/articles/201948099-HF-2-Specifications ; https://www.hobby-hour.com/electronics/s/boss-hf2-hi-band-flanger.php ; https://forum.pedalpcb.com/threads/hero-of-arizona-boss-hf-2-flanger.17330/

## BF-2B (1987) — analogico — MEDIA
- [S] 0.5–6.5 ms, LFO 100 ms–16 s. Scheda HF-2 (MN3204). C6 10n (HF-2: 47n) sul wet → passa-alto 41 → 194 Hz [D]; C7 15n sul ritorno della risonanza → passa-alto circa 174–272 Hz [D]. Trimmer della risonanza tarato basso in fabbrica.
- Fonti: https://support.roland.com/hc/en-us/articles/201950079-BF-2B-Specifications ; http://www.bossareaforum.com/Forum/viewtopic.php?t=977

## BF-3 (2001) — digitale — MEDIA
- 0.3–14.4 ms (ingresso Guitar), 0.3–6.3 ms (ingresso Bass), LFO 100 ms–18 s, tap 0.1–18 s. Modi: STANDARD, ULTRA, GATE/PAN (slicer in mono, pan in stereo), MOMENTARY. Con Depth al massimo il Manual non ha effetto.
- Fonti: https://www.manualslib.com/manual/19659/Boss-Bf-3.html ; https://static.roland.com/assets/media/pdf/BF-3_eng01_W.pdf

## VB-2 Vibrato (1982) — analogico — MEDIA
- [S] rise time 150 ms–5 s, LFO 2–15 Hz, ritardo 4 ms (clock centrale circa 128 kHz [D]), uscita 100% wet.
- MN3207 + MN3102 (47p, 33k, 2k7).
- Filtro pre: 15k/15k/47k 6n8, poi 10k/10k 10n/47p. Filtro post: 10k×3 2n7/4n7/220p, poi 10k×2 3n9/47p/150p.
- LFO **sinusoidale**: T bridged TL022 (2×47n, 2×1M) + clipper Q8.
- Rise: OTA BA662A [?], 6u8 + 250kA; latch con BA634 [?].
- Fonti: https://support.roland.com/hc/en-us/articles/201967789-VB-2-Specifications ; https://stompboxelectronics.com/wp-content/uploads/2022/12/Boss-VB-2-Vibrato-Schematic.pdf ; https://stompboxelectronics.com/2022/12/27/the-boss-vb-2-lfo-circuit/

## VB-2W (Waza) — analogico BBD — BASSA
- S = VB-2; C = modulazione più marcata e risposta in frequenza diversa. Modi UNLATCH / LATCH / BYPASS; jack DEPTH. Modello del BBD non confermato.
- Fonte: https://static.roland.com/assets/media/pdf/VB-2W_e01_W.pdf


---

# Chorus / Dimension

## CE-2W — analog — confidenza MEDIA
- **Controlli**: Rate: LFO speed; Depth: LFO amount (also in CE-1 chorus mode); Mode (3-pos -): STANDARD / CE-1 CHORUS / CE-1 VIBRATO
- Stadio 1, BBD chorus: analog BBD (part unconfirmed). *Standard = CE-2 replica; CE-1 modes emulate CE-1 chorus/vibrato (vibrato = wet only)*
- **BBD/delay**: chip=analog BBD (unconfirmed); aa_filter=-
- **LFO**: shape=triangle (Standard, as CE-2); depth=-
- **Modi**: name=STANDARD; notes=CE-2 | name=CE-1 CHORUS; notes=with variable depth (original CE-1 had none) | name=CE-1 VIBRATO
- **Mix**: A = mix; stereo: A = wet, B = dry
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/CE-2W_eng02_W.pdf
  - https://www.premierguitar.com/gear/boss-ce-2w-waza-craft-chorus-review

## CE-3 — analog — confidenza ALTA
- **Controlli**: Rate (100k B): LFO speed [period 3 s..270 ms +/-20% (0.33-3.7 Hz) [service note]]; Depth (100k B): LFO amount; Mode (switch -): I: A=dry+wet, B=dry-wet; II: A=wet, B=dry
- Stadio 1, input: Q1 2SK30A JFET, Zin 1M
- Stadio 2, pre-emphasis IC1: R20 47k, C2 6n8 + R21 10k, R19 47k fb, C4 100p → f_zero_hz=410; f_pole_hz=2341; gain_high_db=15.1
- Stadio 3, level switch: Q2 FET + R18 120k lowers level when effect on. *removing R18 -> closer to CE-2*
- Stadio 4, AA LPF: C10 33n, 10k x3, C11 3n3 / C14 8n2 fb / C12 470p, Q6 → f_minus3db_hz=6800
- Stadio 5, BBD: MN3207 + MN3102, bias VR3 10kB
- Stadio 6, recon LPF: 10k x3, C24 3n3 / C23 8n2 / C22 470p, Q8; C21 68n out → f_minus3db_hz=6800
- Stadio 7, output A: IC1 sum, R8 47k fb, de-emph C6 6n8 + R7 10k, C5 100p
- Stadio 8, output B: IC2 dry minus wet (47k inverter + 47k sum, 47k fb, 6n8+10k)
- Stadio 9, LFO: as CE-2 (47k/33k Schmitt, 1M/0.1u integrator), triangle 1.5-5.5 V (4 Vpp) at IC5 pin 7 → f_min_hz=0.33; f_max_hz=3.7
- Stadio 10, clock: as CE-2: R45 220k, C26 10n, 4k7/4k7, R40 150k, C25 47p, R41 33k, R42 2k7
- **BBD/delay**: chip=MN3207 (+MN3102); stages=1024; aa_filter=as CE-2 (3n3/8n2/470p); notes=clock Hz not in service notes; assume ~CE-2 (100-128 kHz) - low confidence
- **LFO**: shape=triangle; rate_hz_min=0.33; rate_hz_max=3.7; depth=as CE-2
- **Filtro**: pre_emphasis=f_zero_hz=410; f_pole_hz=2341; gain_high_db=15.1
- **Modi**: name=Mode I; notes=A=dry+wet, B=dry-wet (anti-phase); mono use | name=Mode II; notes=A=wet, B=dry
- **Mix**: 50/50 (A)
- **Fonti**:
  - https://www.synthxl.com/wp-content/uploads/2019/10/Boss-CE-3-Service-Note.pdf
  - https://cdn.roland.com/assets/media/pdf/CE-3_OM.pdf
  - https://www.hobby-hour.com/electronics/s/boss-ce3-chorus.php

## CE-5 — analog — confidenza MEDIA
- **Controlli**: E.Level (250k C): wet level into mixer; Rate (250k C): LFO integrator R (+R55 10k) [~0.27-7.1 Hz (derived)]; Depth (100k B): LFO amount (R32 220k/C25 10n/Q9); Low Filter (50k dual A): SVF 2nd-order HPF on wet [fc ~1026 Hz (cut) .. 88 Hz (flat)]; High Filter (250k C): 1st-order LPF on wet (VR + 1k into 1n2) [~133 kHz .. 530 Hz]
- Stadio 1, input: Q1 2SK184, Q2 buffer; Zin 1M
- Stadio 2, pre-emphasis + limiter: IC1b 47k, 6n8 + 10k, 47k fb, 100p; back-to-back RD3.0 zeners → f_zero_hz=410; f_pole_hz=2341; gain_high_db=15.1. *no compander*
- Stadio 3, AA LPF: 10k x3, 470p/1n8/220p, emitter follower → topology=3rd-order transistor, 10k x3, 470p GND / 1n8 fb / 220p GND; f_minus3db_hz=22080; peak_db=1.06; wet_total_minus3db_hz=19890
- Stadio 4, BBD: MN3007 + MN3101 (1st ed.); bias RT1 4k7
- Stadio 5, recon LPF: same as AA → topology=3rd-order transistor, 10k x3, 470p GND / 1n8 fb / 220p GND; f_minus3db_hz=22080; peak_db=1.06; wet_total_minus3db_hz=19890
- Stadio 6, Low Filter (SVF HPF): IC2b/IC3a/IC2a, 10k summers, integrators 4k7 + VR1a with 33n → fc_hz_range=88, 1026; Q=unknown
- Stadio 7, High Filter (LPF): VR1b 250kC + R69 1k into C42 1n2; C38 33n/R61 1M into IC3b → fc_hz_range=530, 133000
- Stadio 8, mixer: IC1a; E.Level series 250kC
- Stadio 9, LFO: IC4a Schmitt R53 47k/R54 33k (C34 10n); IC4b integrator R55 10k + VR3 250kC, C35/C36 10u+10u b2b (~5u)
- Stadio 10, clock: R31 150k, C21 47p || C22 5p, R29 33k, R30 2k7, Q8
- **BBD/delay**: chip=MN3007 (+MN3101); stages=1024; aa_filter=3rd-order 470p/1n8/220p, -3 dB ~22 kHz (derived); notes=2nd edition (CE-5A) digital: ES56028S echo IC
- **LFO**: shape=triangle; rate_hz_min=0.27; rate_hz_max=7.1; depth=clock CV
- **Filtro**: low_filter=SVF HPF 88-1026 Hz; high_filter=1st-order LPF 530 Hz-133 kHz; pre_emphasis=f_zero_hz=410; f_pole_hz=2341; gain_high_db=15.1
- **Modi**: name=1st edition (1991); notes=MN3007/MN3101 | name=CE-5A 2nd edition; notes=ES56028S digital echo IC, 22 mA
- **Mix**: A = mix; stereo: A = wet, B = dry
- **Fonti**:
  - https://www.synthxl.com/wp-content/uploads/2020/03/Boss-CE-5-Service-Manual.pdf
  - https://cdn.roland.com/assets/media/pdf/CE-5_OM.pdf
  - https://www.hobby-hour.com/electronics/s/boss-ce5-chorus-ensemble.php

## CH-1 — analog — confidenza MEDIA
- **Controlli**: E.Level (250k C): wet level; EQ (250k C): 1st-order LPF on wet (VR + 1k into 1n2) [~133 kHz .. 530 Hz]; Rate (250k C): LFO (R46 15k + VR3) [~0.27-4.7 Hz (derived)]; Depth (100k B): LFO amount
- Stadio 1, pre-emphasis + limiter: R3 47k, C31 6n8, R4 10k, RD3.0 zeners → f_zero_hz=410; f_pole_hz=2341; gain_high_db=15.1
- Stadio 2, AA / recon LPF: 10k x3, 470p/1n8/220p → topology=3rd-order transistor, 10k x3, 470p GND / 1n8 fb / 220p GND; f_minus3db_hz=22080; peak_db=1.06; wet_total_minus3db_hz=19890
- Stadio 3, BBD: MN3007 + MN3101; clock R22 150k, C18 47p
- Stadio 4, EQ: R59 1k + VR2 into C35 1n2; C39 33n/R56 1M into IC5a → fc_hz_range=530, 133000
- Stadio 5, LFO: R43 47k, R45 33k, integrator R46 15k + VR3 250kC, C26/C27 10u+10u
- Stadio 6, depth: VR4 100kB -> R18 220k / C9 10n / Q5
- **BBD/delay**: chip=MN3007 (+MN3101); stages=1024; aa_filter=as CE-5; notes=later (~2001+) units digital
- **LFO**: shape=triangle; rate_hz_min=0.27; rate_hz_max=4.7; depth=clock CV
- **Filtro**: eq=1st-order LPF 530 Hz-133 kHz
- **Modi**: name=analog (1989) | name=digital (c.2001+)
- **Mix**: A = mix, B = dry
- **Fonti**:
  - https://schematicheaven.net/effects/boss_ch1_superchorus.pdf
  - https://www.synthxl.com/wp-content/uploads/2020/03/Boss-CH-1-Service-Manual.pdf

## CEB-3 — hybrid (analog path, digital echo IC) — confidenza BASSA
- **Controlli**: E.Level (100k? A?): wet level; Low Filter (15k? dual B?): cuts lows from effect path only [cut..flat]; Rate (250k? C?): LFO; Depth (100k B): LFO amount
- Stadio 1, input: Q1/Q3 buffers, IC2 NJM022 pre-stage (R8 27k, C9 100p)
- Stadio 2, delay: ES56028S echo IC (F_ADJ driven by LFO VCO); internal compander CC/GC 7.5k/75k/0.33u; LPF1: 10k,10k,12k,18k with 270p/2n7/2n7; LPF2: 10k,22k,12k,10k with 270p/3n3/18n/3n3. *schematic of CEB-3A board; original 1995 part unverified*
- Stadio 3, Low Filter: IC4 NJM022 active filter on wet with dual pot
- Stadio 4, LFO: IC3 NJM022 Schmitt + integrator (47k, 10u+10u)
- Stadio 5, outputs: JFET switching; A = mix / B
- **BBD/delay**: chip=ES56028S (digital echo IC, BBD replacement); aa_filter=on-chip op-amp LPFs (see stages)
- **LFO**: shape=triangle; depth=F_ADJ clock modulation
- **Filtro**: low_filter=cut lows from wet only
- **Mix**: A = mix; stereo: A = wet, B = dry
- **Fonti**:
  - https://cdn.roland.com/assets/media/pdf/CEB-3_OM.pdf
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-CEB-3-Bass-Chorus.pdf

## CE-20 — digital — confidenza MEDIA
- **Controlli**: Rate: (Intensity for Dimensional D / CE-1); Depth: no effect in Dimensional D / CE-1; Brilliance; Ambience; Effect Tone Low/High; E.Level
- **Modi**: name=Rich | name=Bass | name=Acoustic | name=Standard | name=Dimensional D (SDD-320 model) | name=CE-1 model
- **Mix**: E.Level; stereo I/O
- **Fonti**:
  - https://cdn.roland.com/assets/media/pdf/CE-20_OM.pdf

## DC-2 — analog — confidenza MEDIA
- **Controlli**: Mode buttons (radio x4 -): 1..4 (Aion clone: 3 toggles, 7 combinations) [see modes]
- Stadio 1, input + compressor: NE570 (IC2) before BBDs
- Stadio 2, AA LPF (x2): 10k x3, 470p/1n8/220p → topology=3rd-order transistor, 10k x3, 470p GND / 1n8 fb / 220p GND; f_minus3db_hz=22080; peak_db=1.06; wet_total_minus3db_hz=19890
- Stadio 3, BBD x2: 2x MN3207 + 2x MN3102; supply 78L06 -> ~6.8-7 V; bias TR1/TR2 100k ~3.4 V (opt. 3.3-3.8 V)
- Stadio 4, recon LPF (x2): 10k x3, 470p/1n8/220p → topology=3rd-order transistor, 10k x3, 470p GND / 1n8 fb / 220p GND; f_minus3db_hz=22080; peak_db=1.06; wet_total_minus3db_hz=19890
- Stadio 5, expander: NE570 (IC11) both halves
- Stadio 6, output matrix: de-emphasis/spatial matrix with 18n, 1n2, 3n3, 4n7; 180k/47k/39k/33k/220k. *mono sum if only A used*
- Stadio 7, LFO: single triangle; Schmitt R35 100k/R33 33k; integrator R36 330k + R37 180k, C29/C30 10u+10u b2b; IC6A inverter 47k/47k -> anti-phase to 2nd clock; scaler IC5B R38 22k + R39 33k in, R45 47k fb, R47 5k6/R44 4k7 to clocks → rate_formula=f = R35/(4*R33*Rint*C)
- **BBD/delay**: chip=2x MN3207 (+2x MN3102); stages=1024; aa_filter=470p/1n8/220p 3rd order, -3 dB ~22 kHz (derived); compander=NE570; notes=SDD-320 reported ~5-5.5 ms with 10% offset between channels (unverified)
- **LFO**: shape=triangle, anti-phase between channels; rate_hz_min=0.3; rate_hz_max=0.84; depth=mode-switched scaler gain 0.855-1.42
- **Modi**: name=1; rate_hz=0.3; depth_gain=0.855; switching=none | name=2; rate_hz=0.3; depth_gain=1.42; switching=shorts R38 | name=3; rate_hz=0.62; depth_gain=0.87; switching=R98 82k across R36; R97 470k across R38 | name=4; rate_hz=0.84; depth_gain=0.89; switching=shorts R36; R99 220k across R38
- **Mix**: A/B stereo matrix; mono sum when only A
- **Fonti**:
  - https://aionfx.com/app/files/docs/blueshift_documentation.pdf
  - https://schematicheaven.net/effects/boss_dc2_dimension_chorus.pdf
  - https://www.birthofasynth.com/Scott_Stites/Pages/dimc_main.html
  - https://cdn.roland.com/assets/media/pdf/DC-2_OM.pdf

## DC-2W — analog — confidenza MEDIA
- **Controlli**: Mode buttons (1-4 -): combinations allowed (1, 2, 3, 4, 1+4, 2+4, 3+4 as SDD-320); S/SDD-320 (switch -): S = DC-2; SDD-320 = brighter
- Stadio 1, as DC-2 (analog path), MCU for mode logic (inferred): 
- **BBD/delay**: chip=BBD (unconfirmed); aa_filter=-
- **LFO**: shape=triangle anti-phase; depth=-
- **Modi**: name=S (DC-2) | name=SDD-320
- **Mix**: stereo I/O (stereo in -> mono out unsupported)
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/DC-2W_eng02_W.pdf
  - https://static.roland.com/manuals/sh-4d/eng/68126346.html

## DC-3 — digital — confidenza MEDIA
- **Controlli**: E.Level; Equalizer; Rate; Depth
- Stadio 1, DSP: fs 50 kHz; 20 Hz-20 kHz (+1/-3 dB); noise < -100 dBm → fs_hz=50000
- **Mix**: E.Level; outputs A/B
- **Fonti**:
  - https://cdn.roland.com/assets/media/pdf/DC-3_OM.pdf

---

# Tremolo / pan / swell / wah / filtri / delay / digitali

## TR-2 — analog — confidenza MEDIA
- **Controlli**: Rate (100k B): LFO speed (VR2) [90-900 ms period (1.1-11.1 Hz) [spec]]; Wave (100k A): blend triangle -> square (VR1 into summer IC3A) [triangle..square]; Depth (100k B): LFO amount into VCA control (VR3) [0..full]
- Stadio 1, input JFET buffer: 2SK184, R15 1M, C8 47n → gain=1
- Stadio 2, VCA: M5207L01 (one half, current output), in via C6 10u + R9 10k. *later revision reportedly THAT2181 (unverified)*
- Stadio 3, I-V converter: IC2A M5216, R12 22k || C7 10p → lpf_hz=723000
- Stadio 4, output: JFET switches 2SK118 (Q1 effect, Q3 dry), 2SC2458 follower. *100% wet*
- Stadio 5, LFO: IC4A integrator C20/C21 1u back-to-back (0.5u), R31 56k; IC4B comparator R37 47k, C18 11n. *triangle slightly asymmetric; square duty >50%*
- Stadio 6, CV smoothing: IC3B (R27 1k5 fb) -> R8 100k -> C4 0.1u to VCA control → tau_ms=10; fc_hz=16. *rounds square edges*
- **LFO**: shape=triangle<->square (Wave blend); rate_hz_min=1.1; rate_hz_max=11.1; depth=VCA gain modulation
- **Mix**: 100% wet (VCA)
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/TR-2_OM.pdf
  - https://www.experimentalistsanonymous.com/diy/Schematics/Tremolos%20and%20Panners/Boss%20TR-2.pdf
  - https://tremolo-project.blogspot.com/2017/08/boss-tr-2.html
  - https://www.freestompboxes.org/viewtopic.php?t=30098

## PN-2 — analog — confidenza BASSA
- **Controlli**: Rate (? ?): LFO speed [130 ms-14 s period (0.07-7.7 Hz) [spec]; measured 107-1482 ms]; Depth (? ?): modulation amount; Mode (4-pos -): PAN tri / PAN square / TREM tri / TREM square
- Stadio 1, input: inputs A (mono)/B, 1 Mohm (500k mono)
- Stadio 2, VCA x2: reportedly M5207 dual VCA (as TR-2) - unverified. *pan: channels driven antiphase*
- Stadio 3, LFO: triangle / square (~50% duty, more symmetric than TR-2)
- **LFO**: shape=triangle or square; rate_hz_min=0.07; rate_hz_max=7.7; depth=VCA gain
- **Modi**: name=PAN triangle | name=PAN square | name=TREMOLO triangle | name=TREMOLO square
- **Mix**: 100% wet
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/PN-2_OM.pdf
  - https://tremolo-project.blogspot.com/2017/08/boss-pn-2-panning-tremolo.html
  - https://www.freestompboxes.org/viewtopic.php?t=27903

## SG-1 — analog — confidenza MEDIA
- **Controlli**: Sens (100k A): detector input level (with R10 470R); Attack (20k (25k ok) B): series R charging C6 680n -> swell time [fast (stock)]
- Stadio 1, input buffer: Q1 emitter follower → gain=1
- Stadio 2, JFET shunt attenuator: R3 22k series, C2 1u; Q2 2SK30A-GR (R4 470k) shunting to VD 5.6 V zener rail. *variable divider R3 vs Rds*
- Stadio 3, gate drive: C6 680n via R8 1M; feedback C5 33n + R7 1M from output to gate
- Stadio 4, output follower: Q3
- Stadio 5, detector: LM741 gain 1+1M/390R = 2565 (68 dB); Q4 phase splitter 4k7/4k7; Q5/Q6 pulse detectors R17/R18 100k, C16 10u, C15 47n → gain_db=68. *note-onset detector resets/re-triggers swell*
- **Filtro**: type=envelope swell (JFET VCA); attack_cap=680n; attack_R=0-20k + 1M
- **Mix**: 100% wet
- **Fonti**:
  - https://aionfx.com/app/files/docs/onyx_documentation.pdf

## TW-1 — analog — confidenza MEDIA
- **Controlli**: Sens (50k (+4k7?) B): envelope sensitivity; Peak (100k A): resonance/Q of filter loop; Drive (switch -): UP: hard->mellow; DOWN: mellow->hard
- Stadio 1, input: Q2 stage; Zin 220k
- Stadio 2, IC1a inverting: uPC4558, 100k/100k → gain=-1
- Stadio 3, resonant band-pass (photocoupler tuned): C8 47n + R13 150k -> IC1b, feedback LDR || R14 330k || C9 220p; C10 22n closes loop, Peak sets Q; photocoupler P873G35-380 (LED+CdS) → f_peak_cal_hz=1300-1650 (Drive=DOWN, 350 Hz -45 dBm in). *topology partly inferred*
- Stadio 4, envelope: IC2a half-wave rectifier R23 22k, R36 1M, D6/D7; ~1k into C15 1u (attack ~1 ms), ~27k discharge (release ~27 ms, ?) → attack_ms=1; release_ms=27
- Stadio 5, LED driver: IC2b 100k/100k polarity (UP/DOWN) -> R32 10k -> Q4/Q5 2SA1015 -> LED; VR3 100k trim
- Stadio 6, output: JFET switches Q8/Q9. *100% wet*
- **Filtro**: type=resonant band-pass, LDR (CdS) tuned; element=photocoupler P873G35-380
- **Modi**: name=UP | name=DOWN
- **Mix**: 100% wet
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/TW-1_OM.pdf
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-TW-1-Touch-wah.pdf

## AW-2 — analog — confidenza MEDIA
- **Controlli**: Rate (250k C): LFO integrator R (VR4) [~0.2-9 Hz (derived, ?)]; Depth (50k B): LFO amount (VR3); Manual (50k B): base frequency LO-HI (VR2); Sens (50k B): envelope sensitivity (VR1)
- Stadio 1, input: Q9 2SC3378; Zin 1M
- Stadio 2, IC1a: M5218, 220k/220k → gain=-1
- Stadio 3, resonant band-pass (photocoupler): C8 47n + R16 82k -> IC1b, LDR || R50 330k || C9 220p; C21 4n7 closes loop; L1 700 mH network (R46 39k, R45 100k). *same photocoupler loop concept as TW-1*
- Stadio 4, envelope: IC2a BA728 rectifier (R11 10k, R12 2M2); R14 1k + C7 1u attack; R19 68k release; IC3a R21 100k || 0.235u smoothing → attack_ms=1; release_ms=68; smoothing_ms=23.5
- Stadio 5, CV summer + LED drive: IC3b sums envelope + LFO (Depth) + Manual -> Q7/Q6 2SA1048 -> LED; RT1 100k trim
- Stadio 6, LFO: IC5b Schmitt (R40 180k, R39 220k) + IC5a integrator (R41 5k6 + Rate, C18/C19 10u b2b = 5u); IC4b D7/D8 + R34 39k triangle->sine shaper → f_formula=f = R39/(4*R40*R*C)
- **LFO**: shape=sine-shaped triangle; rate_hz_min=0.2; rate_hz_max=9; depth=sums with envelope into LED CV
- **Filtro**: type=resonant band-pass, LDR tuned; direction=up only
- **Mix**: 100% wet
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/AW-2_OM.pdf
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-AW-2-Auto-Wah.pdf

## AW-3 — digital — confidenza MEDIA
- **Controlli**: Decay: envelope decay / humanizer morph time; Manual/Vowel2; Sens/Vowel1; Mode: UP/DOWN/SHARP/HUMANIZER/TEMPO
- **LFO**: shape=tempo sweep; rate_hz_min=0.25; rate_hz_max=5; depth=tap 200 ms-4 s (default 500 ms)
- **Filtro**: type=DSP wah / vowel formant
- **Modi**: name=UP | name=DOWN | name=SHARP; notes=stronger, up | name=HUMANIZER; notes=vowel1->vowel2 over Decay | name=TEMPO; notes=tap 200 ms-4 s
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/AW-3_OM.pdf

## FT-2 — analog — confidenza MEDIA
- **Controlli**: Q (50k A): damping feedback (VR3); Cutoff Freq (50k B): base cutoff (VR2, R3 1k, R1 4k7); Sens (250k B): envelope sensitivity (VR1); Mode (switch -): DOWN / UP / MANUAL (EXP jack)
- Stadio 1, state-variable filter (VCA-tuned): M5207L01 dual VCA -> M5218 integrators C28=C29=1n5; IC4a/IC4b summers 47k; band-pass output (?) -> R56 47k, C9 6n8. *f proportional to VCA gain / (R*C)*
- Stadio 2, envelope: IC5b gain; IC5a/IC6a full-wave rectifier (10k, R41 20k, R37 47k); R38 10k + C22 2u2 → tau_ms=22
- Stadio 3, envelope post-filter: Sallen-Key LPF R40/R43 10k, C25 10u, C23 0.22u → f0_hz=10.7; Q=3.4. *envelope may overshoot (derived, ?)*
- Stadio 4, direction: IC3b polarity UP/DOWN
- Stadio 5, output: JFET switch. *100% wet*
- **Filtro**: type=state-variable (VCA/OTA-style), band-pass out; integrator_C=1n5
- **Modi**: name=DOWN | name=UP | name=MANUAL
- **Mix**: 100% wet
- **Fonti**:
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-FT-2-Dynamic-Filter.pdf

## DM-3 — analog — confidenza MEDIA
- **Controlli**: Repeat Rate (50k B): clock (VR3 via R54 10k, D5); RT3 100kB trim [20-300 ms]; Intensity (50k B): feedback (VR1); Echo (50k B): wet level (VR2)
- Stadio 1, pre-emphasis: IC1b R10/R9 47k, C5 100p, C4 6n8, R11 10k → f_zero_hz=410; f_pole_hz=2341
- Stadio 2, compressor: uPC1571C (NE570 class)
- Stadio 3, AA LPF 3rd order: Q3, 10k x3, C12 10n GND, C14 82n fb, C15 330p GND. *like DM-2 but 10n first cap*
- Stadio 4, BBD: MN3205 4096 + MN3102 (C44 220p, R56 560k, Q4 R57 33k/R58 2k7); BIAS RT1 22kB, CANCEL RT2 10kB → fclk_300ms_hz=6830; fclk_20ms_hz=102000
- Stadio 5, recon LPF: Q6 3rd order 10k x3, C42 2n2, C18 33n, C41 1n; Q7 SK2 10k x2, C19 39n, C37 330p → sk_f0_hz=4436; sk_Q=5.44. *same as DM-2 post filter*
- Stadio 6, expander + mixer: uPC1571C; IC1a R38/R40 47k; MAIN + DIRECT outs
- **BBD/delay**: chip=MN3205 (+MN3102); stages=4096; clock_hz_min=6830; clock_hz_max=102000; delay_ms_min=20; delay_ms_max=300; aa_filter=as DM-2 (pre first cap 10n instead of 6n8); compander=uPC1571C
- **Mix**: dry unity + wet (Echo)
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/DM-3_OM.pdf
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-DM-3-Analog-delay.pdf

## DM-2W — analog — confidenza MEDIA
- **Controlli**: Repeat Rate: delay [S: 20-300 ms; C: 40-800 ms]; Intensity: feedback; Echo: wet level; S/C (switch -): Standard (DM-2) / Custom (longer, cleaner)
- **BBD/delay**: chip=BBD (part not confirmed); delay_ms_min=20; delay_ms_max=800; aa_filter=-
- **Modi**: name=Standard; notes=20-300 ms | name=Custom; notes=40-800 ms, cleaner
- **Mix**: dry + wet; DIRECT OUT; RATE expression jack
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/DM-2W_eng03_W.pdf
  - https://www.boss.info/global/products/dm-2w/

## RT-20 — digital — confidenza MEDIA
- **Controlli**: Overdrive: drive; Balance: horn/bass rotor; Rise Time: slow<->fast transition; Slow Speed: CCW stops rotor front; Fast Speed: center = standard; Direct Level; Effect Level
- Stadio 1, DSP: TC220CCA0AF-B01, codec AK4552VT, 11.2896 MHz xtal → fs_hz=44100
- **Modi**: name=I; notes=Leslie 122 standard + OD | name=II; notes=on-mic, more tremolo | name=III; notes=Marshall 1959 dist | name=IV; notes=Uni-Vibe + Marshall; Balance = vibe intensity
- **Mix**: Direct/Effect levels
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/RT-20_OM.pdf
  - https://www.schematicsonline.com/wp-content/uploads/2022/11/Boss-RT-20-Rotary-Ensemble.pdf

## SL-2 — digital — confidenza MEDIA
- **Controlli**: Variation: pattern variation; Attack: slice attack; Duty: slice length; Balance: dry/wet; Tempo: 40-300 BPM, tap
- **LFO**: shape=rhythmic patterns; depth=40-300 BPM
- **Modi**: name=SINGLE 1-2 | name=DUAL 3-4 | name=TREMOLO 5 | name=HARMONIC 6 | name=SFX 7-8
- **Mix**: Balance knob
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/SL-2_eng01_W.pdf

## SL-20 — digital — confidenza MEDIA
- **Controlli**: Pattern/Bank: pattern select (harmonic banks 4-5); Attack; Duty; Effect Level; Direct Level; Tempo: 30-250 BPM
- **LFO**: shape=1-bar patterns; depth=30-250 BPM, MIDI clock
- **Modi**: name=MONO | name=STEREO FIXED | name=RANDOM | name=PING-PONG | name=AUTO | name=3D Cross | name=3D Panner
- **Mix**: Effect/Direct levels
- **Fonti**:
  - https://static.roland.com/assets/media/pdf/SL-20_OM.pdf

## MO-2 — digital — confidenza BASSA
- **Controlli**: Balance: dry/effect; Tone; Detune; Mode: 3 modes
- **Modi**: name=Mode 1 | name=Mode 2 | name=Mode 3
- **Mix**: Balance
- **Fonti**:
  - https://www.boss.info/global/products/mo-2/

---

## Riferimenti di modellazione
- Holters & Parker, DAFx-18, "A Combined Model for a BBD and its Input and Output Filters" (caso di studio: chorus del Juno-60): https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2018/09/Holters-Parker-2018-A-Combined-Model-for-a-Bucket-Brigade-Device-and-its-Input-and-Output-Filters.pdf
- Raffel & Smith, DAFx-10, "Practical Modeling of Bucket-Brigade Device Circuits": https://www.dafx.de/paper-archive/details/JhVfAOFXD1lAtctkMTUODg
- Filtri calcolati con `filt.py` (analisi nodale, follower ideali).
