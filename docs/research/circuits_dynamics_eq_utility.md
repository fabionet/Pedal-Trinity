# Dossier circuitale — pedali BOSS: dinamica, EQ, acustici, pitch/synth, utility

Documento di ricerca per PedalTrinity (plugin GPLv3, C++/JUCE) — emulazione a livello di circuito di pedali *ispirati* a BOSS. "BOSS" e i nomi dei modelli sono marchi dei rispettivi proprietari e sono citati solo come riferimento tecnico.

Compilato il 2026-09-27. Dati strutturati corrispondenti: `circuits_dynamics_eq_utility.json` (stessa cartella).

**Legenda confidenza** — H/high: letto da schema, service notes, distinta componenti o manuale ufficiale; M/medium: una sola fonte secondaria o topologia interpretata; L/low: dedotto/stimato. Valori marcati (L) vanno verificati prima di usarli come riferimento.

**Formule ricorrenti**: τ = R·C; fc = 1/(2πRC); gyrator L = R1·R2·Cg, f0 = 1/(2π√(L·Cs)), Q = √(L/Cs)/R2; Sallen-Key unity f0 = 1/(2πR√(C1C2)), Q = 0.5√(Cfb/Cgnd); non invertente G = 1 + Rf/Rg; OTA gm ≈ Iabc/(2VT).

**Limiti della ricerca**: hobby-hour.com, freestompboxes.org, diystompboxes e support.roland.com hanno risposto 403 al fetch automatico (alcuni valori da snippet di ricerca). Nessuno schema pubblico per NS-2, LMB-3, AC-2, SP-1, GE-6, GE-10; per i digitali solo specifiche ufficiali (nessun dato di latenza pubblicato).

Indice: 1. Dinamica · 2. EQ · 2b. Acoustic · 3. Pitch/Octave · 4. Synth · 5. Utility/Routing


## 1. Dinamica

Service notes originali BOSS (scansioni, schemi + distinta + tabelle di collaudo) lette per CS-1, CS-2, CS-3, LM-2: `https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-<CS1|CS2|CS3|LM2|SG1>_ServiceNotes.pdf`.

Convenzione I/O BOSS anni '80 (H): Zin 1 MΩ (JFET/op-amp + 1M di bias), Zout ≈ 1 kΩ (emitter follower + 1k serie + 100k a massa), −20 dBu nominale, 9 V con riferimento Vcc/2 (4.3–4.9 V), commutazione a JFET 2SK30A con flip-flop 2SC1815/2SC945. Serie X (MDP): 18 V interni.

### CS-1 Compression Sustainer (1978, analogico, OTTICO) — HIGH (valori) / MEDIUM (topologia)
- Specifiche: 9 V 5 mA; rumore EIN −100 dB; max in +5 dB, max out −10 dB; compressione 60 dB; carico >10 kΩ.
- Controlli: SUSTAIN 1MA (log, nella rete di retroazione, in serie a 15k); LEVEL 50KB (**lineare**, a differenza di CS-2/CS-3); switch MODE NORMAL/TREBLE.
- Elemento di guadagno: fotoaccoppiatore P873G35-380 (2× LED/CdS, PH1/PH2); op-amp TA7136P.

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso EF | 0.1µ, 1k, Q1 2SC1815-Y (220k bias, 10k emettitore) | Zin ≈ 220k (L) |
| 2 | Amp a guadagno variabile | 100k in; Rf 2.2M ∥ 220p ∥ CdS PH1; PH2 in shunt con 0.47µ; comp 27k + 22p/47p | G(LDR buio) ≈ 22 (26.8 dB); fc 2.2M∥220p = 329 Hz (si allarga con la compressione) |
| 3 | MODE | TREBLE 0.15µ+560Ω / NORMAL 1µ+2.2k | 1.89 kHz / 72 Hz; test: −55 dBm in → @2 kHz TREBLE −12 vs NORMAL −20 dBm; @100 Hz −29 vs −17 dBm |
| 4 | Rivelatore + driver LED | Q3 phase splitter 10k/10k → Q4/Q5 (0.047µ, 100k) → 1µ via 220Ω, scarica 330k → LED via 4.7k, zener RD5.1EB | attacco elettrico τ ≈ 0.22 ms; rilascio τ ≈ 330 ms; + ritardo CdS ~5–20 ms on / 100 ms–s off (L) |
| 5 | Level / uscita | 0.033µ, 47k, LEVEL 50KB; JFET Q8/Q9; EF Q2 10k, 1k, 1µ, 100k | Zout ≈ 1k |

- Tabella di collaudo (Sustain max, 1 kHz, NORMAL): in +10/0/−20/−40 dBm → out <−5/−8/−10/−11 dBm ⇒ 50 dB in → 6 dB out (ratio ≥ 8:1, carattere da limiter). Sustain min: −40 dBm → −30 dBm (NORMAL) / −24 dBm (TREBLE). Soglia efficace ≈ −45…−50 dBm (L).

### CS-2 Compression Sustainer (1981–86, analogico, OTA) — HIGH
- Specifiche: 9 V 4 mA; max in −10 dBm, max out −10 dBm; compressione 38 dB; Zin 1 MΩ; carico >10 kΩ; EIN −110 dBm (IHF-A).
- Controlli: SUSTAIN VR1 **1MC** (reverse log) in serie a R7 27k verso Iabc; ATTACK VR2 **150KC** in serie a R1 10k (carica di C1 → in realtà recupero/rilascio); LEVEL VR3 **50KA**.
- Semiconduttori: IC1 **BA662A** (OTA con buffer interno; uscita OTA pin 6, buffer 7→8); Q1–Q3 2SK30ATM-Y, Q4–Q6 2SC732TM-GR, Q7–Q11 2SC1815-BL; D2–D6 1S2473.

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso | R13 10k, C10 22n, R18 1M; Q1 JFET SF (R17 10k) → Q4 EF (R16 10k) | HPF 7.2 Hz |
| 2 | OTA | R14/R15 2.2k agli ingressi; R8 15k + C8 10n bias; C6 1µ; R11 150k carico I→V | Vout ≈ gm·150k·Vd, gm ≈ Iabc/(2VT); Iabc ≈ (V_C1 − Vbe − Vpin1)/(27k+VR1); 27k…1.027M = 38:1 ≈ 31.6 dB (spec 38 dB) |
| 3 | LPF post-OTA | buffer interno → R10 15k / C5 10n | fc = 1.06 kHz (6 dB/oct) → tono scuro del CS-2 |
| 4 | Phase splitter Q5 | R2 10k collettore / R4 10k emettitore; audio da emettitore via C4 47n + R3 10k + LEVEL | HPF ≈ 56 Hz |
| 5 | Rivelatore "transistor pump" full-wave (tipo Dyna/Ross) | Q8/Q9 2SC1815 pilotati in antifase via C2/C3 10n, R5/R6 1M, clamp D3/D4; collettori su C1 10µ | HPF basi 15.9 Hz; i picchi scaricano C1 |
| 6 | Costanti di tempo | C1 10µ ricaricato da +9 V via R1 10k + VR2 150KC | rilascio τ = 100 ms … 1.6 s; attacco (scarica) < 1 ms … pochi ms (L) |
| 7 | Uscita | Q3 JFET switch, C20 47n, Q6 EF (R33 10k), C22 1µ, R37 1k, R36 100k | Zout ≈ 1k |

- Collaudo (LEVEL max): Attack max/Sustain max 1 kHz: −50 dBm → −12 dBm; −40 → −8.5 dBm. Sustain min: −40 → −33 dBm. 0 dBm → ≈ −8 dBm (leggera distorsione). 100 Hz 0 dBm: Attack max −4.5 / min −10.5 dBm. ⇒ makeup a basso livello ≈ +38 dB (Sustain max) / +7 dB (min); ratio ≈ 3:1 tra −50 e −40 dBm, ≥ 80:1 tra −40 e 0 dBm.
- Nota BA662A: praticamente introvabile; sostituzione con BA6110 richiede transistor aggiuntivi (corrente di controllo insufficiente).

### CS-3 Compression Sustainer (1986–, analogico, VCA) — HIGH
- VCA: **NEC µPC1252H2** (SIP 8 pin, dB-lineare; cloni usano V2181/THAT2181). Op-amp M5218L ×2, BA718; Q7 2SK117-GR (buffer inviluppo).
- Specifiche: 9 V 10–11 mA; −20 dBu; Zin 1 MΩ; Zout 1 kΩ; carico ≥10 kΩ; EIN −110 dBm.
- Controlli: LEVEL 50KA; TONE 20KB; ATTACK **250KC**; SUSTAIN 20KB.

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso | R1 10k, C2 22n, R3 1M a 4.5 V, clamp D2/D3, IC1a buffer | HPF 7.2 Hz |
| 2 | VCA + I→V | C4 1µ, R9 10k (V→I) → IC3 pin1; out pin 8 → IC2a con R6 10k ∥ C5 1.8n; R7 2.7k simmetria; controllo R8 47Ω + R10 1k (÷ 0.045) | gain unitario a 0 V; LPF 8.84 kHz; HPF (C4,R9) 15.9 Hz; legge ≈ 6 mV/dB per classe 2181 (µPC1252 non verificato, L) |
| 3 | Rivelatore **feedback** (dopo VCA) full-wave | semiond. +: R12 47k, C7 10n, R14 1M, D5 → Q8; semiond. −: C6 1µ, R15 100k → IC4a inv. (R16 100k) → R17 47k, C9 10n, R18 1M, D4 → Q6; collettori su C8 4.7µ | HPF side-chain ≈ 339 Hz (L); attacco < 1 ms (L) |
| 4 | Recupero (ATTACK) | C8 4.7µ caricato via R2 22k + VR3 250KC | τ = 103 ms … 1.28 s; CW preserva l'attacco della pennata |
| 5 | SUSTAIN | VR4 20KB tra R4 4.7k (+V) e R3 22k (massa), wiper bufferizzato IC4b, clamp D6 su C8 | limita la Vc max (guadagno max); wiper ≈ 4.24…8.09 V (L); CCW = limiter |
| 6 | TONE shelving attivo | IC2b BA718, R5 10k in, R4 10k fb; VR2 20KB tra gli ingressi, wiper → R1 4.7k + C1 27n | shelf ≈ 1.25 kHz (estremi) / 400 Hz (centro); ≈ ±10 dB HF (L) |
| 7 | Uscita | C13 47n, R36 10k, LEVEL 50KA, Q4 JFET, C15 47n, Q1 EF 10k, C17 1µ, R31 1k, R30 100k | HPF ≈ 56 Hz; Zout ≈ 1k |

- Dinamica: compressore feedback con rivelatore di picco full-wave; attacco < 1 ms, rilascio 0.1–1.3 s; ratio dipendente dal programma ≈ 3:1…>10:1 (M/L).

### LM-2 Limiter (1987, analogico, VCA feed-forward) — HIGH (valori) / MEDIUM (topologia)
- VCA µPC1252H2; M5218L, BA718 ×2, M5223L; termistore SPT-1000 (compensazione del log-detector). Rif. +4.3 V (Q8 su divisore 5.6k/3.9k).
- Specifiche: 9 V 12 mA; rumore < −100 dBm; Zin 1 MΩ; carico ≥10 kΩ.
- Controlli: LEVEL 100KA (retroazione I→V del VCA), TONE 20KB, RELEASE 1MA, THRESHOLD 50KB (+ R1 330k). Trimmer VR5 1kB (RATIO), VR6 50kB (THRESHOLD ADJ).

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso | R1 10k, C1 47n, R18 1M a 4.3 V, Q2 2SK117 SF (R15 4.7k) | HPF ≈ 3.4 Hz |
| 2 | VCA + I→V | C2 10µ, R13 18k, R11 4.7k/C11 10n → IC1; IC2b fb = LEVEL 100k ∥ C15 220p; R14 47Ω controllo | gain max ≈ 100k/18k = 5.6 (+15 dB); LPF 7.2 kHz a Level max (L) |
| 3 | Shelf passa-basso | R32 18k, R33 5.6k + C21 10n → Q7 EF | polo 674 Hz, zero 2.84 kHz, shelf ≈ −12.5 dB (L) |
| 4 | TONE | IC2a R29/R30/R20 10k, C12 10p; VR3 20kB + R2 2.2k + C 12n | angolo ≈ 6.03 kHz (M) |
| 5 | Side-chain (feed-forward dal buffer d'ingresso) | C16 0.1µ, R37 47k → IC3a log-amp (diodi antiparalleli, R39 1M, R40/R41 10k, C19 47µ) → raddrizzatore di precisione IC3b/IC4b | — |
| 6 | Peak hold | C24 1µ tantalio; rilascio via D9, R47 22k + RELEASE 1MA | attacco < 1 ms (L); rilascio τ = 22 ms … 1.02 s |
| 7 | Legge di controllo | IC4a → R48 2.2k + SPT-1000 → IC5b (somma soglia) → IC5a + D10 (azione solo sopra soglia) → VR5 + R28 2.2k → VCA | ratio fisso alto (≥10:1, L); taratura: 100 Hz 0 dBm → −18 dBm out (VR5) |
| 8 | Uscita | C13 47n, Q4 JFET, C3 47n, Q1 EF 10k, C28 10µ, R5 1k, R6 100k | Zout ≈ 1k |

### LMB-3 Bass Limiter Enhancer (analogico) — LOW (circuito) / MEDIUM (specifiche)
- Controlli: THRESHOLD, RATIO (continuo ≈ 1:1 → limiter, L), ENHANCE (enhancer armonico/EQ su bassi e alti, L), LEVEL. Esempio manuale: ratio 2:1, +6 dB sopra soglia → +3 dB.
- Specifiche: −20 dBu; Zin 1 MΩ; Zout 1 kΩ; rumore −86 dBu (IHF-A); 17 mA. Nessuno schema trovato.
- Fonti: https://www.boss.info/us/products/lmb-3/specifications/ · https://www.strumentimusicali.net/manuali/BOSS_LMB3_ENG.pdf

### NS-2 Noise Suppressor (1987, analogico) — MEDIUM (manuale) / LOW (circuito)
- Controlli: THRESHOLD, DECAY (CW = decadimento più lungo), MODE REDUCTION/MUTE. Pedale: in REDUCTION alterna Normal/Reduction; in MUTE alterna Reduction/Mute. LED REDUCTION + CHECK/MUTE.
- Routing: INPUT → SEND e rivelatore; RETURN → VCA → OUTPUT (il rivelatore "ascolta" il segnale pulito e sopprime il rumore degli effetti nel loop).
- Funzionamento: VCA + rivelatore d'inviluppo veloce come **espansore verso il basso** sotto soglia (non gate duro).
- Specifiche: −20 dBu; Zin 1 MΩ; Zout 1 kΩ; 20 mA. VCA probabilmente µPC1252H2 (L). Modello suggerito: espansione 1:2…1:4 sotto soglia, attacco ~1 ms, decay 30 ms…1–2 s (L).
- Fonte: https://static.roland.com/assets/media/pdf/NS-2_e01_W.pdf

### SG-1 Slow Gear (1979, analogico, attack-delay) — MEDIUM
- Fonti: replica GGG "SGO" + ridisegno service notes. Controlli: SENS 100kA; ATTACK 20kB; trimmer TR1 10k (GGG) / "25k?" (ridisegno).
- Elemento di guadagno: **JFET 2SK30A** come resistenza variabile/attenuatore in serie (nessun VCA integrato).

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso EF | R1 1k, C1 47n, T1 (2SC/BC549) R2 470k bias, R3 10k | Zin ≈ 470k (L) |
| 2 | Percorso audio | R4 22k, C3 1µ, R6 470k al rail zener 5.6 V; T2 JFET in serie, gate via R7 47k + C6 0.47µ + R8 1M; C5 1µ, C7 33n, R10 1M → T3 EF (R9 1M, R11 10k); uscita R12 1k, C8 1µ, R13 100k | smoothing gate τ = 22 ms |
| 3 | Rivelatore di trigger | C2 22n → SENS 100kA (R14 470Ω) → C9 22n → IC1 non-inv (R15 220k bias, R17 1M, R16 390Ω, C10 1µ) → R19 390k∥C12 1n → T4 phase splitter (4.7k/4.7k) → C13/C14 1µ, D2/D3, R22/R23 100k → T5/T6 | HPF 32.9 Hz; G = 1+1M/390 = 2565 (+68 dB) sopra 408 Hz (con R16 3.9k: 257, +48 dB, 41 Hz — fonti in disaccordo) |
| 4 | Rampa | T5/T6 scaricano C16 10µ e C15 47n (via D4) a ogni nuova nota; ricarica via R24 10k + TR1 + ATTACK 20kB | τ = 100…400 ms; fade-in udibile ≈ 0.3 s … ~2 s (3–5 τ, L) |
| 5 | Soglia trigger | — | ≈ 0.6 V / 2565 ≈ 0.25 mV di picco a SENS max (L); scala con SENS (log) |

### CP-1X Compressor (digitale MDP) — HIGH (specifiche)
- Controlli: COMP, RATIO, ATTACK, LEVEL; LED CHECK + indicatore GAIN REDUCTION. 18 V interni; −20 dBu; Zin 1 MΩ; Zout 1 kΩ; 90 mA; bypass bufferizzato. Fs/bit/bande non pubblicati (analisi multibanda MDP non quantificata; piattaforma presumibilmente come NS-1X: 48 kHz, L).
- Fonte: https://www.boss.info/global/products/cp-1x/specifications/

### BC-1X Bass Comp (digitale MDP) — HIGH (specifiche)
- Controlli: THRESHOLD, RATIO, RELEASE, LEVEL; meter GAIN REDUCTION 12 segmenti; CHECK. 18 V interni; −20 dBu; 1 MΩ / 1 kΩ; 90 mA. Multibanda (numero bande non pubblicato).
- Fonte: https://www.boss.info/global/products/bc-1x/specifications/

### NS-1X Noise Suppressor (digitale MDP) — HIGH (specifiche)
- 48 kHz; AD 24 bit + AF method; DA 32 bit; elaborazione 32 bit float. INPUT/RETURN −20 dBu nominale, +7 dBu max, 1 MΩ; OUTPUT/SEND −20 dBu, +7 dBu max, 1 kΩ. 60 mA. Headroom 27 dB.
- Controlli: THRESHOLD, DECAY, DAMP (nome da verificare, M), modi Gate / Reduction / Mute; LED REDUCTION, CHECK; loop send/return come NS-2.
- Fonte: https://www.boss.info/global/products/ns-1x/specifications/

**Lignaggio VCA**: CS-1 opto LED/CdS → CS-2 OTA BA662A (Iabc) → CS-3/LM-2 (e probabilmente NS-2) VCA dB-lineare µPC1252H2 → SG-1 JFET. **Rivelatori**: CS-2/CS-3 "transistor pump" full-wave che scarica un elettrolitico ricaricato dal pot "Attack" (in realtà recupero); CS-2 feed-forward-ish tipo Dyna/Ross, CS-3 feedback con HPF side-chain ~340 Hz; LM-2 feed-forward log-amp + raddrizzatore di precisione + peak hold + soglia.

Fonti dinamica: https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-CS1_ServiceNotes.pdf · https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-CS2_ServiceNotes.pdf · https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-CS3_ServiceNotes.pdf · https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-LM2_ServiceNotes.pdf · https://stompboxelectronics.com/wp-content/uploads/2023/01/BOSS-SG1_ServiceNotes.pdf · https://generalguitargadgets.com/wp-content/uploads/ggg_sgo_sc.pdf · https://static.roland.com/assets/media/pdf/CS-3_eng03_W.pdf · https://www.freestompboxes.org/viewtopic.php?f=1&t=4405&start=20 · https://support.roland.com/hc/en-us/articles/201947179-CS-2-Specifications

## 2. EQ

Formule gyrator (tutte le bande GE-7/GE-7B): L = R1·R2·Cg; f0 = 1/(2π√(L·Cs)); Q_ramo = √(L/Cs)/R2. Impedanza esatta del gyrator: Zin = R2(1 + jωCg·R1)/(1 + jωCg·R2) ≈ R2 + jωL (ωCg·R2 ≤ 0.015). Topologia di ogni ramo: wiper slider → Cs → nodo X; X → R2 330Ω → uscita op-amp (inseguitore); X → Cg → ingresso +; ingresso + → R1 → Vcc/2; 470k di pull-down su ogni uscita op-amp (R47–R52).

### GE-7 Equalizer (1981–) — HIGH
Schema originale ridisegnato (J. Halverson 2000) letto direttamente + clone Effects Layouts "Prismatic EQ" + simulazione LTspice di cushychicken.
- Specifiche: bande 100/200/400/800/1.6k/3.2k/6.4k Hz, ±15 dB; Level ±15 dB; Zin 1 MΩ; Zout 1 kΩ; −20 dBu; 30 mA (spec attuale; ≈ 6 mA nella versione TL022).

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| 1 | Ingresso + **pre-enfasi** | R30 10k serie, C22 47n, R29 470k a Vcc/2; U5A HA12017 non-inv; Rf R31 4.7k; ramo shunt R28 470Ω + C20 15n; C21 47p, R46 22k | HPF 7.2 Hz; high-shelf ×1 → ×11 (+20.8 dB): zero 1/(2π·5.17k·15n) = 2.05 kHz, polo 1/(2π·470·15n) = 22.6 kHz |
| 2 | Level | U4B JRC4558; R27 10k al +, R26 10k fb; VR8 10k B tra + e −, wiper → R2 2.2k + C16 10µ a massa | estremi G = 1+10k/2.2k = 5.55 → ±14.9 dB; piatto sopra ~7 Hz |
| 3 | Amp sommatore EQ | U4A JRC4558; R24 3.3k → nodo A (+); R25 3.3k fb → nodo B (−); slider VR1–VR7 B10K tra A e B, wiper → ramo verso Vcc/2 | boost (ramo lato B): G ≈ 1 + 3.3k/|Zramo| → a risonanza Zramo ≈ 330Ω ⇒ ×11 (+20.8 dB) ideale; con slider centrati (2.5k + Zb) ≈ ±15 dB nominali — risolvere con analisi nodale |
| 4 | 6 rami gyrator | vedi tabella | Q_ramo 3.1–4.1; Q effettivo (con ~1.1k di slider/sorgente) ≈ 0.7–0.9 ≈ bande di un'ottava (L) |
| 5 | Banda 6.4 kHz (**non gyrator**) | wiper → C1 47n → R3 820Ω → Vcc/2 | ramo RC: |Z| → 820Ω sopra fc = 4.13 kHz ⇒ **high-shelf**; guadagno estremo 1+3.3k/820 = 5.0 (≈ ±14 dB), transizione 2–5 kHz |
| 6 | Bypass + **de-enfasi** | C19 1µ + R32 1M → JFET Q4/Q5 2SK30A (flip-flop 2SC945 Q2/Q3); R33 4.7k serie, R36 470Ω + C27 15n a Vcc/2 | shelf passa-basso: polo 2.05 kHz, zero 22.6 kHz — annulla esattamente la pre-enfasi |
| 7 | Uscita | C26 47n, R34 1M, Q1 2SC732 EF, R15 10k, R14 1k, C11 1µ, R12 100k | Zout ≈ 1k |
| 8 | Alimentazione | Vcc/2 da R20/R23 3.3k + C18 47µ | rumore simulato in ingresso 7.8 µVrms (2 Hz–22 kHz) |

Rami gyrator GE-7 (R2 = 330Ω per tutti; op-amp U1–U3 TL022):

| Banda | Cs (serie) | Cg | R1 | Op-amp | L (H) | f0 calcolata | Q_ramo |
|---|---|---|---|---|---|---|---|
| 100 | C10 1.5µ (tant.) | C13 56n | R16 100k | U3B | 1.848 | 95.6 Hz | 3.36 |
| 200 | C9 0.68µ | C8 33n | R9 82k | U3A | 0.893 | 204 Hz | 3.47 |
| 400 | C7 0.33µ | C14 15n | R18 100k | U2B | 0.495 | 394 Hz | 3.71 |
| 800 | C5 0.15µ | C6 8.2n | R7 100k | U2A | 0.271 | 790 Hz | 4.07 |
| 1.6k | C4 0.1µ | C15 3.9n | R21 82k | U1B | 0.106 | 1549 Hz | 3.11 |
| 3.2k | C2 39n | C3 2.2n | R5 82k | U1A | 0.0595 | 3303 Hz | 3.74 |
| 6.4k | C1 47n + R3 820Ω (RC, shelf) | — | — | — | — | fc 4.13 kHz | — |

Fonti: https://experimentalistsanonymous.com/diy/Schematics/Tone%20Control%20and%20EQs/BOSS%20GE-7.pdf · https://cushychicken.github.io/ltspice-boss-ge7-equalizer/ · http://effectslayouts.com/wp-content/uploads/2023/05/Prismatic-EQ-Build-Doc.pdf · https://tagboardeffects.blogspot.com/2014/05/4-band-gyrator-eq.html · https://forum.pedalpcb.com/threads/boss-ge-7-frequency-change.16561/ · https://www.boss.info/us/products/ge-7/specifications/

### GE-7B Bass Equalizer (1987–1995) — MEDIUM
- Bande 62/125/250/500/1k/2k/4k Hz, ±15 dB, + Level. Stessa topologia del GE-7.
- Valori bass (dal build doc Prismatic, per slot GE-7; R2 = 330Ω):

| Slot | Cs / Cg | R1 | f0 calcolata | Q_ramo |
|---|---|---|---|---|
| 100 | 2.2µ / 82n | 100k | 65 Hz | 3.36 |
| 200 | 1.5µ / 47n | 82k | 115 Hz | 2.79 |
| 400 | 470n / 22n | 100k | 272 Hz | 3.77 |
| 800 | 220n / 12n | 100k | 539 Hz | 4.07 |
| 1.6k | 150n / 3.9n | 82k | 1265 Hz | 2.54 |
| 3.2k | 68n / 3.3n | 82k | 2042 Hz | 3.47 |
| top | RC 560Ω + 68n | — | fc 4.18 kHz (shelf, ≈ ±16.8 dB estremo) | — |

- Fonti: http://effectslayouts.com/wp-content/uploads/2023/05/Prismatic-EQ-Build-Doc.pdf · https://reverb.com/p/boss-ge-7b-bass-equalizer · https://www.henrikhansson.com/product/ge-7b-bass-equalizer/

### GEB-7 Bass Equalizer (1995–) — MEDIUM (bande) / LOW (valori)
- Bande 50/120/400/500/800/4.5k/10k Hz, ±15 dB, Level ±15 dB (pagina ufficiale conferma "7 bande, 50 Hz–10 kHz"). Zin 1 MΩ; Zout 1 kΩ; −20 dBu; 30 mA; bypass bufferizzato.
- Valori da una lista mod (GE-7 v.2 Taiwan → GEB-7): C1 27n, C3 15n, C4 2.2n, C5 15n, C6 82n, C7 0.1µ, C8 0.15µ, C9 1.5µ, C10 4.7µ, C13 68n, C15 33n, C16 27n, C20 22n, C31 22n (mappatura designatori incerta). Verifiche: 4.7µ/68n/100k → 49.4 Hz; 1.5µ/33n/100k → 125 Hz.
- Ricetta di emulazione (L): mantenere Q_ramo ≈ 3.5 con R2 = 330Ω ⇒ √(L/Cs) = 1155Ω ⇒ L = 1155/(2πf), Cs = 1/(1155·2πf). Banda 10k: probabilmente shelf RC 820Ω + 27n → 7.2 kHz.
- Fonti: https://www.boss.info/us/products/geb-7/specifications/ · https://www.freestompboxes.org/viewtopic.php?t=723&start=120

### GE-6 Graphic Equalizer (1978–1981) — LOW
- Bande 100/200/400/800/1.6k/3.2k Hz (ottave), ±15 dB; nessuno slider Level (M/L); 9 V, 7 mA. Schema non reperito: probabile gyrator stile GE-7 senza shelf 6.4k né stadio Level (L) → riusare i rami GE-7 100–3.2k.
- Fonte: https://docs.pedalpcb.com/project/6BandEQ-Potentiometer.pdf (generico, non clone)

### GE-10 Graphic Equalizer (1976–1985) — LOW/MEDIUM
- Alimentato da rete (trasformatore). Bande 31.25/62.5/125/250/500/1k/2k/4k/8k/16k Hz; ±12 dB per banda (anteprima service notes; retail dice ±15 dB → usare ±12); guadagno uscita fino a +15 dB (M/L); jack footswitch.
- Fonte: https://www.scribd.com/doc/300654764/Boss-GE-10-Service-Notes

### PQ-4 Parametric Equalizer (1991) — HIGH (manuale)
- Controlli: LOW shelf <100 Hz ±18 dB (concentrico con PRESENCE); PRESENCE shelf >8 kHz ±18 dB; MIDDLE freq 100 Hz–1.6 kHz, livello ±18 dB; HIGH freq 500 Hz–8 kHz, livello ±18 dB; LEVEL ±18 dB (prima dell'EQ). Q fisso (≈ 1–1.5 ottave, L).
- Schema a blocchi: buffer → level → 4 filtri paralleli (Low LP, Middle BP, High BP, Presence HP) ciascuno con pot di livello → somma → switch elettronico → uscita.
- Zin 1 MΩ; Zout 1 kΩ; rumore ≤ −98 dBm (IHF-A); 23 mA.
- Fonte: https://cdn.roland.com/assets/media/pdf/PQ-4_OM.pdf

### SP-1 Spectrum (1977–1981) — LOW
- Boost di medie a banda singola tipo parametrico ("fixed wah"): SPECTRUM = frequenza centrale 500 Hz–5 kHz; BALANCE = quantità del segnale filtrato miscelato al dry. Nessun Level.
- Circuito derivato dallo "Spectrum" degli ampli Roland; probabilmente passa-banda sweepabile (pot doppio) sommato al dry; Q ignoto, suggerito 2–4 (L).
- Specifiche: Zin 220 kΩ; Zout 600 Ω; S/N > 80 dB; 23 mA.
- Fonte: https://www.bosszone.info/?page_id=14

### EQ-200 Graphic Equalizer (2019, digitale) — HIGH
- 2 canali (A, B) da 10 bande ±15 dB + LEVEL. TYPE (centri banda):
  - "30/800/12.8k": 30/60/120/200/400/800/1.6k/3.2k/6.4k/12.8k (il "200" dopo 120 rompe la spaziatura d'ottava — verificare con il pannello; atteso 240);
  - "32/1k/16k": 32/63/125/250/500/1k/2k/4k/8k/16k;
  - "28/880/14k": 28/55/110/220/440/880/1.75k/3.5k/7k/14k.
- STRUCT: PARA+LINK on = EQ stereo; PARA+LINK off = due EQ mono indipendenti; SERIES = A→B (ingresso sommato in mono, stesso segnale su entrambe le uscite). Funzione insert send/return, 128 memorie, MIDI, CTL/EXP (EXP su livello A, B o totale).
- 96 kHz, AD/DA 32 bit, 32 bit float; −10 dBu nominale, +7 dBu max; Zin 2 MΩ; Zout 1 kΩ; 170 mA.
- Fonti: https://static.roland.com/assets/media/pdf/EQ-200_eng01_W.pdf · https://www.boss.info/us/products/eq-200/specifications/

## 2b. Acoustic

### AC-2 Acoustic Simulator (1997) — LOW (circuito) / MEDIUM (specifiche)
- Controlli: LEVEL; TOP (brillantezza/armoniche); BODY (quantità di risonanza del corpo); MODE STANDARD / ENHANCE (più brillante e presente).
- Probabilmente analogico (18 mA; schema su hobby-hour non accessibile). Zin 1 MΩ; Zout 1 kΩ; −20 dBu; rumore −80 dBm.
- Ipotesi filtri (L): BODY = picco risonante 100–250 Hz + scoop 400–800 Hz; TOP = presence 3–8 kHz; ENHANCE = più HF e scoop.
- Fonti: https://www.boss.info/us/products/ac-2/ · https://www.manualslib.com/manual/314778/Boss-Ac-2.html

### AC-3 Acoustic Simulator (~2007, digitale COSM, derivato da AD-8) — MEDIUM
- MODE: STANDARD / JUMBO / ENHANCE / PIEZO; TOP; BODY; REVERB/LEVEL concentrici. Zin 1 MΩ; Zout 1 kΩ; −20 dBu; 70 mA.
- Fonte: https://www.boss.info/us/products/ac-3/specifications/

### AD-2 Acoustic Preamp (~2017, digitale) — MEDIUM
- Controlli: ACOUSTIC RESONANCE (corpo/calore multi-parametro), AMBIENCE (riverbero), NOTCH (anti-feedback, range non pubblicato ≈ 50–500 Hz, L), pedale. Uscite OUTPUT 1 kΩ e LINE OUT bilanciata (DI) 600 Ω. Zin 10 MΩ; −20 dBu; 65 mA; rumore ≤ −94 dBm (M).
- Fonte: https://www.boss.info/global/products/ad-2/specifications/

## 3. Pitch / Octave

### OC-2 Octave (analogico, 1982–2003) — confidence: HIGH (valori da schema clone + specifiche originali)

Riferimento valori: PedalPCB "Ocelot" (clone fedele OC-2, sostituzioni: TL072↔TL022, CD4027↔BA634, 2N3904↔2SC732). Semiconduttori originali: IC1–IC5 TL022CP, IC6 BA634 (T flip-flop), IC7 uPD4013C, JFET 2SK30ATM-Y (Q1,Q3,Q4,Q7,Q8), Q2 2SC732TM-GR, Q5/Q6 2SC1815-Y, D10/D11 1S188FM (germanio, clamp), D3–D9 1SS133, zener RD5.1EB. Specifiche: Zin 1 MΩ, carico >10 kΩ, max in −5 dBm, max out 0 dBm, input minimo operativo −60 dBm @250 Hz, rumore −100 dBm (IHF-A), 4 mA.

| # | Stadio | Componenti | Derivati |
|---|---|---|---|
| S1 | Ingresso non-invertente (IC1.1) | C2 1µ, R2 1M pulldown, R4 1M a VREF; Rf R5 10k; Rg R6 2k7 + C3 10µ | G = 1+10k/2.7k = 4.70 (+13.4 dB); HPF in 0.16 Hz; shelf: zero 5.9 Hz, polo 1.25 Hz |
| S2a | LPF RC (solo catena di analisi) | R29 22k / C12 10n | fc = 723 Hz |
| S2b | Sallen-Key LPF unity (IC3.2) | R30=R31=330k, C13 2n2 (fb), C14 220p (gnd) | f0 = 1/(2πR√(C13·C14)) = 693 Hz; Q = 0.5√(C13/C14) = 1.58 |
| S2c | RC | R32 33k / C15 330p | 14.6 kHz (trascurabile) → nodo A |
| S2d | Rete lag A→B | R33 33k serie; C16 10n + R34 68k a massa | H = (1+sR34C16)/(1+s(R33+R34)C16): polo 157.6 Hz, zero 234 Hz, guadagno HF 0.673 |
| S3 | Peak follower ± (IC4) | diodi 1N914/1SS133, R37/R38 1k, C17/C18 1µ, R35/R36 10k | attacco τ ≈ 1 ms, rilascio τ ≈ 11 ms |
| S4 | Comparatori + latch SR (IC5 + 4013 con CLK/D a massa) | R39/R40 22k verso R/S | q=1 se xA < env−; q=0 se xA > env+; altrimenti hold → onda quadra a f0 con isteresi ≈ picco-picco |
| S5 | Divisori | 4013 (D=Q̅) ÷2 → s1 (f0/2); BA634/CD4027 (J=K=1) ÷2 → s2 (f0/4); gate JFET via 1M/1M | Vgs ≈ −4.5 V (off) / 0 V (on) |
| S6 | Generatore Oct1 (IC1.2 + JFET) | clamp C4 1µ + Ge D (anodo a VREF); R7=R8 47k, R9 100k, Rf R12 27k | JFET off G = +0.497; on G = −0.574; y1 ≈ 0.5·s1·[x + A(t)] (A = inviluppo di picco negativo); scarica clamp τ ≈ 36 ms (L) |
| S6b | LPF Oct1 | R13 22k/C5 22n (329 Hz) + SK IC2.2 R14=R15=330k, C6 4n7, C7 470p | f0 = 324 Hz, Q = 1.58 (≈+4 dB); ~3° ordine |
| S7 | Generatore Oct2 (cascata da uscita Oct1 filtrata) | C8 1µ + Ge clamp; stessa rete ±0.5 (47k/47k/100k/27k), switch s2 | — |
| S7b | LPF Oct2 | R22 22k/C9 47n (154 Hz) + SK IC3.1 330k/330k, C10 10n, C11 1n | f0 = 152 Hz, Q = 1.58 |
| S8 | Mixer passivo + emitter follower | 3 pot B100K (Direct, Oct1, Oct2) sorgente→VREF, wiper via 100k (R1,R3,R25) a base Q1; R26 10k; uscita C1 1µ, R27 100k, R28 1k | Vout ≈ (αD·4.7x + α1·y1 + α2·y2)/3; Direct al max ≈ ×1.57 (+3.9 dB); HPF out 1.6 Hz (14 Hz su 10 kΩ) |

Note modello: monofonico; il termine A(t)·s1 è l'onda quadra sub-ottava modulata dall'inviluppo (fondamentale a f0/2), il termine x·s1 è componente ring-mod; l'asimmetria +0.50/−0.57 lascia un filo di dry. Tracking migliore con pickup al manico / tono chiuso. Taper originale pot non verificato (B assunto, L).

Fonti: https://docs.pedalpcb.com/project/Ocelot-PedalPCB.pdf · https://championleccy.com/wp-content/uploads/2018/01/boss_octave_oc2_guitar_effect_pedal_sch.pdf · https://docs.architolk.nl/subwave/oc-2 · https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html · https://mirosol.kapsi.fi/2014/08/boss-oc-2-octave/ · https://forum.pedalpcb.com/threads/boss-oc-2-clone-pedalpcb-ocelot-octave.11649/ · https://www.hobby-hour.com/electronics/s/oc2-octave.php

### OC-3 Super Octave (digitale, 2003) — HIGH
- Controlli: DIRECT LEVEL, OCT1 LEVEL, CONTROL, MODE (POLY / OCT2 / DRIVE). In DRIVE, DIRECT LEVEL = volume totale.
- CONTROL: POLY → RANGE (−1 oct polifonico, estende l'effetto verso l'acuto); OCT2 → livello −2 oct (modo compatibile OC-2, mono); DRIVE → quantità di distorsione (con DIRECT OUT collegato distorce solo l'ottava).
- Ingressi GUITAR IN / BASS IN (BASS IN commuta l'elaborazione); uscite OUTPUT e DIRECT OUT (split dry/ottava).
- −20 dBu, Zin 1 MΩ, Zout 1 kΩ, 50 mA. Latenza non pubblicata.
- Fonte: https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf

### OC-5 Octave (digitale, 2019) — HIGH
- Controlli: DIRECT LEVEL, +1OCT LEVEL, −1OCT LEVEL, −2OCT/RANGE; switch VINTAGE/POLY; switch GUITAR/BASS.
- VINTAGE = simulazione OC-2 (note singole, manopola = livello −2 oct). POLY = accordi, manopola = RANGE (banda dell'effetto −1 oct; tutto a sinistra solo la nota più grave).
- Uscite OUTPUT e DIRECT OUT. −20 dBu, 1 MΩ / 1 kΩ, 60 mA, bypass bufferizzato.
- Fonti: https://static.roland.com/assets/media/pdf/OC-5_eng03_W.pdf · https://www.boss.info/global/products/oc-5/specifications/

### PS-2 Digital Pitch Shifter/Delay (1987) — MEDIUM
- Pitch −1…+1 ottava; delay 30 ms – 2 s; modi (probabili): delay 125 ms / 500 ms / 2 s, pitch shift manuale, +1 oct, −1 oct (lista esatta L).
- Risposta: diretto 10 Hz–100 kHz; effetto 80 Hz–6 kHz (+1/−3 dB) → nel modello limitare la banda del wet a ~6 kHz.
- Rumore < −90 dBm, Zin 1 MΩ, 60 mA.
- Fonti: https://support.roland.com/hc/en-us/articles/201958119-PS-2-Specifications · https://www.effectsdatabase.com/model/boss/compact/ps2 · https://www.pedal-of-the-day.com/2014/09/24/boss-ps-2-digital-pitch-shifter-delay/

### PS-3 Digital Pitch Shifter/Delay (1994–1999) — MEDIUM
- A/D 16 bit lineare 128× oversampling, D/A 16 bit, fs 32 kHz.
- 11 modi: Detune (±30 cent), Fast pitch shift (bassa latenza), Slow pitch shift (più ritardo, più accurato), Inverse (reverse) pitch shift, Detune&Detune, Detune&Pitch (out A/B), Pitch&Pitch, Delay 32–125 / 125–500 / 500–2000 ms, EXP (pedale tra pitch A e B).
- Pitch: 26 passi = ±12 semitoni cromatici + ±2 ottave. Uscite A/B.
- Fonti: https://support.roland.com/hc/en-us/articles/201933129-PS-3-Specifications · https://musewiki.org/Boss_PS-3

### PS-5 Super Shifter (1999) — MEDIUM/HIGH
- Modi: PITCH SHIFTER, HARMONIST, DETUNE, T.ARM, FLUTTER. Controlli: MODE, PITCH, H.R KEY, BALANCE (centro = parità), D.TIME/SPEED (delay in DETUNE, velocità in T.ARM/FLUTTER; inattivo in PITCH/HARMONIST).
- PITCH: ±1,2,5,7,12,24 semitoni (±2 ottave), polifonico su accordi. HARMONIST: diatonico su note singole, ±2 ottave nella tonalità scelta. T.ARM: glide verso il pitch impostato tenendo premuto; FLUTTER: vibrato rapido. EXP (EV-5) = pitch continuo. Stereo A/B, 50 mA.
- Fonti: https://casaveerkamp.net/archivos/PS-5.pdf · https://www.manualslib.com/manual/314922/Boss-Ps-5.html

### PS-6 Harmonist (2010) — HIGH
- Controlli: MODE, KEY/FALL TIME, SHIFT, BALANCE/RISE TIME; EXP; out A/B. −20 dBu, 1 MΩ, 65 mA.
- HARMONY (diatonico, maggiore): −3rd, −6th, −1oct, +1oct, −4th&−6th, 3rd&5th, +1oct&−1oct, 3rd&−4th (fino a 3 voci incluso dry).
- PITCH SHIFT: −12, +12, −24, +24, +7&−5, +12&−5 semitoni.
- DETUNE: ±5/10/15/20 cent, coppia ±5&±15.
- S-BEND: +1,−1,+2,−2,+3,−3,+4 oct, +2 cromatico; tempi rise/fall dai knob duali.
- Fonti: https://www.boss.info/global/products/ps-6/specifications/ · https://www.manualslib.com/manual/1016177/Boss-PS-6-Harmonist.html · https://support.roland.com/hc/en-us/articles/4514667135003

### HR-2 Harmonist (1994–1999) — MEDIUM
- 2 voci (A,B), ciascuna: −1oct, −6th, −5th, −4th, −3rd, detune, +3rd, +4th, +5th, +6th, +1oct. KEY a 12 posizioni (maggiore/minore). Armonia diatonica intelligente, tracking mono.
- Controlli E.LEVEL, VOICE A, VOICE B, KEY. Jack INPUT, DETECTOR IN (ingresso separato per il rilevamento del pitch), OUT A/B. 75 mA.
- Fonti: https://www.effectsdatabase.com/model/boss/compact/hr2 · https://bkpedalreviews.wordpress.com/2023/02/12/review-boss-hr-2-harmonist/ · https://modulargrid.net/p/boss-hr-2-harmonist

## 4. Synth

### SY-1 Synthesizer (2019) — HIGH
- 121 suoni = 11 tipi × 11 variazioni: LEAD1, LEAD2, PAD, BASS, STR, ORGN, BELL, SFX1, SFX2, SEQ1, SEQ2.
- Controlli: TYPE, VARIATION (1–11), TONE/RATE, DEPTH, EFFECT, DIRECT; GUITAR/BASS. Pedale tenuto = SOUND HOLD; EXP/CTL: hold, +1 oct, tap tempo; loop SEND/RETURN sul dry.
- Tracking polifonico ("latency-free" dichiarato). Zin 2.2 MΩ (boss.info; il manuale riporta 1 MΩ – conflitto), Zout 1 kΩ, 115 mA.
- Fonti: https://www.boss.info/global/products/sy-1/specifications/ · https://static.roland.com/assets/media/pdf/SY-1_eng03_W.pdf

### SY-200 (2020) — HIGH
- 12 modi: LEAD, PAD, STRING, BELL, ORGAN, BASS, DUAL, SWEEP, NOISE, SFX, SEQ, ARPEGGIO; 171 suoni, 128 memorie. 44.1 kHz, AD/DA 24 bit, DSP 32 bit float. Zin 2 MΩ, Zout 1 kΩ, 230 mA. Polifonico.
- Fonte: https://www.boss.info/global/products/sy-200/specifications/

### SYB-3 Bass Synthesizer (1996, digitale) — MEDIUM
- A/D 19 bit (riportato), D/A 16 bit. 11 modi: 7 a oscillatore interno (mono; saw, square, PWM, saw+noise, saw−1oct, PWM+noise, saw+noise−1oct) + 4 di elaborazione del segnale (wave-shape normal/reverse, auto-wah normal/reverse).
- Controlli: FREQ (cutoff), RESO, DECAY (inviluppo filtro), EFFECT, DIRECT, MODE; pedale = hold. 80 mA.
- Fonti: https://www.synthmania.com/syb-3.htm · https://www.vintagesynth.com/boss/syb-3-bass-synthesizer

### SYB-5 Bass Synthesizer (~2002, digitale) — HIGH (manuale)
- Controlli: FREQ, RES, DECAY/RATE (decay sweep filtro nei modi 1,2,4,5,7–11; al MAX filtro fisso; rate LFO nei modi 3,6), MODE, EFFECT, DIRECT. EXP = cutoff o rate LFO. Hold nei modi 1–9. Modo stereo (effetto/diretto separati).
- Modi 1–9 (oscillatore interno, note singole): 1 saw; 2 saw + saw−1oct; 3 saw + auto filter (LFO); 4 square; 5 square + square−1oct; 6 square + auto filter; 7 pulse; 8 pulse + all-pass (phaser); 9 PWM. Modi 10–11 WAVE SHAPE: filtro sale (10) / scende (11) alla pennata.
- Fonti: https://brianhilmers.com/gear/manuals/boss-syb5-eng01-w.pdf · https://www.vintagesynth.com/boss/syb-5-bass-synthesizer

### PC-2 Percussion Synthesizer (1984–85, analogico) — MEDIUM
- Esiste, ma non è un compatto standard: case poco più grande con pad in gomma; è una drum synth, non un effetto con ingresso chitarra.
- Controlli: PITCH, SWEEP, DECAY, LFO DEPTH, LFO RATE, SENSITIVITY. VCO triangolare 85 Hz–3 kHz; LFO triangolo/quadra 2–400 Hz; VCA decay 30 ms–5 s, attacco istantaneo. Trigger da pad (sensibile alla dinamica) o impulso positivo sul jack trigger. Mono, nessun generatore di rumore.
- Fonti: https://www.vintagesynth.com/boss/pc-2-percussion-synthesizer · https://www.effectsdatabase.com/model/boss/pc2

## 5. Utility / Routing

### AB-2 2-way Selector — HIGH
- Jack: IN/OUT (comune), A, B. Percorso audio **passivo** (contatti meccanici) → bidirezionale:
  - **Selettore A/B → 1**: due sorgenti su A e B, uscita da IN/OUT.
  - **Splitter/router 1 → A/B**: ingresso su IN/OUT, uscita su A **oppure** B (es. due ampli, o mute usando un'uscita scollegata).
- Un solo percorso attivo alla volta: **nessun modo A+B** (non è un ABY; non è un mixer). Nessun buffer; perdita ≈ 0 dB (modellare come unità). Pubblicizzato come "silent switching".
- Alimentazione: 2× AAA (R03/LR03) **solo per i LED** A (rosso) / B (giallo); si accende inserendo il jack IN/OUT; ≤5 mA; ~100 h (carbone). Nessun jack DC. Livello nominale −20 dBu. 96×90×43 mm, 240 g.
- Manuale: non commutare segnali di potenza (casse); possibile ronzio con una chitarra su due ampli.
- Modello plugin: switch ideale A XOR B, gain unitario; crossfade opzionale di pochi ms per il "silent switching" (L).
- Fonti: https://www.boss.info/global/products/ab-2/specifications/ · https://www.boss.info/global/products/ab-2/ · https://www.manualslib.com/manual/314777/Boss-Ab-2.html

### LS-2 Line Selector — HIGH
- Jack: INPUT (anche interruttore alimentazione), OUTPUT, SEND A, SEND B, RETURN A, RETURN B, DC IN, DC OUT.
- Controlli: MODE (6 posizioni), LEVEL A, LEVEL B (da −∞/mute a +20 dB, **centro = 0 dB unity**; agiscono sul segnale di RETURN della linea; non attivi in OUTPUT SELECT), pedale, LED linea A/B (anche check batteria).
- Modi:
  1. **A↔B**: alterna Linea A (IN→SEND A, RETURN A→OUT) e Linea B (IN→SEND B, RETURN B→OUT); nessun bypass.
  2. **A↔BYPASS**: Linea A oppure bypass (IN→OUT).
  3. **B↔BYPASS**: Linea B oppure bypass.
  4. **A→B→BYPASS**: ciclo a tre stati.
  5. **A+B MIX↔BYPASS**: IN inviato a SEND A e SEND B contemporaneamente; RETURN A + RETURN B sommati su OUT (mix parallelo con livelli indipendenti) / bypass.
  6. **OUTPUT SELECT**: ciclo IN→SEND A, IN→SEND B, IN→OUTPUT (router 1 in / 3 out); return inutilizzati.
- Usi: mixer (modo 5), splitter (modo 5), selettore di ingressi A/B→1 usando RETURN A/B come ingressi in modo A↔B (M/L; INPUT deve avere un jack per accendere a batteria).
- **Attivo e bufferizzato**: Zin 1 MΩ, Zout 1 kΩ; bypass quindi bufferizzato (non true bypass, L). −20 dBu nominale; carico ≥10 kΩ; 25 mA (manuale 2017) / 30 mA (spec page); batteria 9 V carbone ~14 h, alcalina ~30 h.
- DC OUT: solo con alimentatore (non a batteria), cavo PCS-20A; assorbimento totale entro la portata dell'alimentatore PSA (200 mA; 500 mA per alcune versioni).
- Modello plugin: buffer unitario; LEVEL: centro 0 dB, max +20 dB, min −∞ (curva L); A+B MIX = somma dei due return.
- Fonti: https://static.roland.com/assets/media/pdf/LS-2_eng01_W.pdf · https://www.boss.info/global/products/ls-2/specifications/ · http://www.egodeath.com/bossls2.htm · https://manuals.plus/boss/ls-2-line-selector-manual

### ES-5 / ES-8 Effects Switching System (sintesi) — HIGH
- ES-5: 5 loop mono con ordine libero e percorsi paralleli (mixer), 200 patch, uscite CTL 1/2, 3/4 (TRS), ingresso CTL/EXP, MIDI; buffer in/out commutabili per patch; −10 dBu nominale / +13 dBu max; Zin 1 MΩ (buffer on), Zout 1 kΩ; 125 mA.
- ES-8: 8 loop (7 e 8 stereo) + loop volume, 800 patch, 2 ingressi, OUT 1/L, 2/R, TUNER; 3 CTL out, 2 EXP out, 2 CTL/EXP in, MIDI; −10 dBu / +18 dBu max; 1 MΩ / 1 kΩ; 400 mA.
- Entrambi: riordino loop, rami paralleli, buffer e livello per patch, mute del rumore di commutazione.
- Fonti: https://www.boss.info/global/products/es-5/specifications/ · https://www.boss.info/global/products/es-8/specifications/

### FV (pedali volume) — MEDIUM
- FV-500H (alta impedenza, mono): INPUT, OUTPUT, TUNER OUT, EXP. Pot volume 250 kΩ (M; riportato come doppio 25k/250k che usa ~1/3 della rotazione; P/N F3229172R2). Taper non pubblicato → modellare audio/log (L).
- FV-500L (bassa impedenza, per sorgenti <600 Ω, stereo): IN 1/2, OUT 1/2, TUNER OUT (solo da IN 1), EXP; pot 25 kΩ (M). Pot expression separato 10 kΩ (M).
- MINIMUM VOLUME (FV-500/FV-50): livello a tallone giù; non cambia il massimo; a zero = mute per accordare via TUNER OUT. Range non pubblicato (0…~unity, L).
- FV-50H/FV-50L: In ×2, Out ×2, Tuner Out, Minimum Volume; H = alta impedenza prima degli effetti; L = stereo bassa impedenza. FV-30H/30L: versioni semplici (30L stereo); Minimum Volume non documentato.
- Tutti passivi, nessuna alimentazione.
- Fonti: https://static.roland.com/assets/media/pdf/FV-500L_eng06_W.pdf · https://www.boss.info/us/products/fv-500h_500l/ · https://www.boss.info/global/products/fv-50h_50l/ · https://www.boss.info/us/products/fv-30h_30l/ · https://forum.fractalaudio.com/threads/boss-fv-500l-pot-replacements.40946/

### TU-2 / TU-3 / TU-3W accordatori — HIGH (TU-2: MEDIUM)
- **TU-3**: rilevamento C0 (16.35 Hz) – C8 (4186 Hz); precisione ±1 cent; riferimento A4 436–445 Hz a passi di 1 Hz (default 440).
  - Modi: Chromatic (+ flat ♭ −1, ♭♭ −2 semitoni); Guitar (+ flat fino a −6 semitoni; mostra "7" per la 7ª corda); Bass (+ flat fino a −3; mostra B basso / C alto per basso 5/6 corde).
  - Meter LED 21 segmenti: CENT (default) e STREAM (le luci scorrono e rallentano avvicinandosi all'intonazione); Accu-Pitch Sign; modalità High Brightness (tieni premuto ≥2 s).
  - Uscite: **OUTPUT mutata a tuner acceso**; jack **BYPASS** sempre passante (per suonare mentre si accorda); a tuner spento OUTPUT = buffer.
  - Zin 1 MΩ, Zout 1 kΩ; 20 mA (80 mA high brightness); DC OUT via PCS-20A entro portata PSA (200/500 mA).
- **TU-3W** (Waza Craft): stesse specifiche di rilevamento/modi; switch posteriore **buffered bypass / true bypass**; accordatura muta; 50 mA (110 mA HB); DC OUT.
- **TU-2**: C0–C8 (M); ±3 cent (M); riferimento 438–445 Hz; modi Chromatic/Guitar/Bass con flat (−1) e double flat (−2); meter cent/stream; OUTPUT mutata a tuner acceso, BYPASS sempre passante; Zin 1 MΩ; 55 mA max; DC OUT (totale ≤200 mA).
- Fonti: https://www.boss.info/global/products/tu-3/specifications/ · https://www.boss.info/global/products/tu-3w/specifications/ · https://www.manualslib.com/manual/1208038/Boss-TU-3.html · https://support.roland.com/hc/en-us/articles/201958219-TU-2-The-Bypass-Output-Jack · https://www.manualslib.com/manual/618627/Boss-Chromatic-Tuner-TU-2.html

### PSM-5 Power Supply & Master Switch (1984–1990) — MEDIUM
- Un loop commutato a pedale: off = GUITAR→AMP diretto; on = GUITAR→SEND→(effetti)→RETURN→AMP. Commuta l'intero loop. LED rosso=on / verde=off. Commutazione elettronica tipo FET BOSS, percorso bufferizzato (L).
- Jack: GUITAR, AMP, SEND, RETURN, DC in, uscite 9 V DC. Zin GUITAR/RETURN 1 MΩ; carico AMP/SEND >10 kΩ (M). Alimentazione PSA 9 V 200 mA; sezione switch 13 mA; DC out per fino a 7 compatti (entro 200 mA totali).
- Fonti: https://support.roland.com/hc/en-us/articles/201952179-PSM-5-Specifications · https://www.craveguitars.co.uk/home/features/effects/feature-1986-boss-psm-5-power-supply-master-switch/ · https://www.effectsdatabase.com/model/boss/compact/psm5 · https://schematicsonline.com/wp-content/uploads/2022/11/Boss-PSM-5-Power-Supply-Master-Switch.pdf
