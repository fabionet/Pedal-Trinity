# Dossier circuitale: overdrive, boost e preamp compatti BOSS

Dossier di ricerca per PedalTrinity (GPLv3). I dati tabellari completi sono in `circuits_overdrive.json`, generato da script: tutti i guadagni e le frequenze di taglio sono calcolati, non trascritti a mano.

## Convenzioni

- **Guadagno non invertente:** `G = 1 + Zf/Zg`. Quando la gamba di massa è `Rg + Cg`, il guadagno indicato vale sopra `fc = 1/(2π·Rg·Cg)`. Sotto questa frequenza il guadagno tende a 1, e da qui nasce la "mid hump" tipica dei TS/SD.
- **Pot:** "rheostat" significa due terminali in serie al percorso. Tapers: A = logaritmico, B = lineare, C = antilog. W e G sono tapers speciali BOSS: W è piatto al centro, usato sui tone SD-1/SD-1W da 20k.
- **Soglie diodi (circa 0,1–1 mA):** Si 1S2473/1SS133/1SS387/1N914 ≈ 0,6 V; LED rosso ≈ 1,7–1,9 V; 1N4004 ≈ 0,6–0,65 V.
  - Con i diodi in retroazione di un op-amp, l'uscita è limitata a circa `Vref ± Vf(tot)`: il clipping è morbido perché resta il ramo resistivo in parallelo.
- **"Discrete op-amp" BOSS:** coppia differenziale JFET (2SK117/2SK184/2SK880) più uno stadio d'uscita PNP (2SA970/1335/1048/1587) con carico di collettore a massa (2,2–2,7 kΩ).
  - L'uscita satura contro i rail (0 V / 8 V oppure 5,6 V). Questo, non i diodi, è il meccanismo di distorsione principale di BD-2 e OD-2 Turbo.
- **Tensioni di alimentazione:** i pedali più recenti hanno un rail a 8 V (capacitance multiplier) e Vref = 4 V. I primi usano direttamente 9 V con Vref = 4,5 V.
- **Fiducia:**
  - *high*: schema di fabbrica o trace indipendente coerente con un clone commerciale.
  - *medium*: una sola fonte hobbistica, oppure copertura parziale del pedale.
  - *low*: solo specifiche o dati qualitativi.

### Modello comune: tone passivo BOSS (OD-2, BD-2, OD-3, SD-2)

Struttura: `Cin` in serie, poi pot P (lug3 dal lato ingresso). Lug1 va a `Cg` verso massa, il wiper va al pot di level (carico `RL`).

Con `a` = frazione dal lug1 al wiper:

- tra ingresso e wiper c'è `(1-a)·P`;
- tra wiper e massa c'è `a·P + 1/(sCg)`, in parallelo a `RL`.

Comportamento agli estremi:

- **a = 0:** passa-basso del primo ordine con `fc = 1/(2π·P·Cg)`.
- **a = 1:** `Cg` e P caricano il nodo d'ingresso, dando un taglio dei bassi e quindi un'enfasi relativa degli alti. Attorno a ore 12 la risposta è circa piatta (misura Chuck D. Bones sul BD-2).

### Modello comune: tone attivo TS/SD-1

- `X` è l'ingresso + dell'op-amp, dopo `R7`/`C4` verso massa. Il pot P è collegato tra X e l'ingresso −, con il wiper W verso Vref tramite `Rw + 1/(sCw)`. La retroazione è `Zf = Rf || 1/(sCf)`.
- Equazioni nodali, con `V+ = V- = Vx`:

  ```
  (Vin − Vx)/R7 = sC4·Vx + (Vx − Vw)/(a·P)
  (Vx − Vw)/(a·P) + (Vx − Vw)/((1−a)·P) = Vw/(Rw + 1/(sCw))
  Vout = Vx + Zf·(Vx − Vw)/((1−a)·P)
  ```

- Queste equazioni si risolvono direttamente per `H(s, a)` e si discretizzano con la trasformata bilineare, per campione o per blocco quando cambia il pot.

---

## OD-1 OverDrive (1977; revisione a 4558 dal dic. 1980) — fiducia: alta

Versioni: *quad* (RC3403: buffer d'ingresso e d'uscita a op-amp) e *dual* (4558 con buffer a emitter follower). Il suono è ritenuto migliore nella quad.

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Buffer d'ingresso | R1 1k, C1 47n, R2 470k (dual) / C1 100n, R2 220k (quad) | HPF 7,2 Hz |
| 2 | Accoppiamento | C2 4,7n + R4 100k (dual) / 47n + 10k (quad) | HPF **339 Hz** in entrambe |
| 3 | Gain non invertente, diodi in retroazione | Rg = 4,7k + 47n; Rf = 33k + OVER DRIVE 1M (rheostat); Cf assente sullo schema dual, 47 pF nella quad | G = 8,0 → 220,8 (18,1 → 46,9 dB); fc(Rg) = 720 Hz; LPF(Cf) 3,28 kHz al massimo |
| 4 | Invertente −1 con LPF (unico "tono") | 10k / 10k // 18n | LPF 1 polo a **884 Hz** |
| 5 | Level + buffer d'uscita | 1µF, 4,7k, LEVEL 10kB, 47n, 1k, 1µF, 100k | — |

- **Clipping:** 1S2473 con 1 diodo in un verso e 2 in serie nell'altro (asimmetrico, ≈ 0,6 / 1,2 V), quindi armoniche pari. Il modello Aion aggiunge l'opzione simmetrica.

Fonti:
- https://www.hobby-hour.com/electronics/s/od1-overdrive.php (schema di fabbrica)
- https://aionfx.com/app/files/docs/parhelion_documentation.pdf (clone della versione quad)
- https://aionfx.com/project/parhelion-vintage-overdrive/
- https://electricdruid.net/designing-a-classic-overdrive/

## SD-1 Super OverDrive (1981–) — fiducia: alta

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Emitter follower Q5 (2SC732) | R1 10k, C1 47n, R2 470k, R3 10k | HPF 7,2 Hz; Zin ≈ 470k |
| 2 | Accoppiamento | C2 18n, R4 100k | HPF **88 Hz** |
| 3 | Gain, diodi in retroazione (uPC4558C/M5218) | Rg = R6 4,7k + C3 47n; Rf = R5 33k + DRIVE 1M (etichetta "1MB") | G = 8,0 → 220,8 (18,1 → 46,9 dB); fc 720 Hz; nessuna Cf, la banda è limitata dal GBW del 4558 (≈ 14 kHz a G max) |
| 4 | LPF | R7 10k, C4 18n | **884 Hz** (TS808: 1k/220n = 723 Hz) |
| 5 | Tone attivo TS | TONE 20k W; wiper → R8 470Ω + C5 27n; Rf R9 10k // **C6 10n** | shunt sul wiper 12,5 kHz; LPF in retroazione **1,59 kHz** (C6 è la tipica "treble cut" dell'SD-1, spesso rimossa come mod) |
| 6 | Level + uscita | C7 1µ, R10 4,7k, LEVEL 10kB, Q6 follower | — |

- **Clipping:** D4 in un verso, D5+D6 nell'altro (1S2473), asimmetrico 1:2. Rispetto al TS-9 (tabella Aion) le differenze sono:
  - diodi asimmetrici;
  - HPF d'ingresso 18n/100k = 88 Hz, contro 1µ/10k ≈ 16 Hz del TS;
  - LPF prima del tone 10k/18n = 884 Hz, contro 1k/220n = 723 Hz;
  - shunt del tone 470Ω/27n, contro 220Ω/220n;
  - C6 10n in retroazione;
  - DRIVE 1M, contro 500k.

  La fc della gamba di guadagno, 4,7k/47n = 720 Hz, è uguale nei due pedali.

Fonti:
- https://www.hobby-hour.com/electronics/s/sd1-super-overdrive.php (schema di fabbrica)
- https://aionfx.com/app/files/docs/stratus_deluxe_documentation.pdf (tabella SD-1 ↔ TS-9)
- https://electricdruid.net/designing-a-classic-overdrive/

## SD-1W Super OverDrive Waza Craft (2014) — fiducia: alta

Tutto è discreto: gli op-amp sono sostituiti da discrete op-amp 2SK880GR + 2SA1587GR, su rail 8 V con Vref 4 V. Il percorso del segnale ha gli stessi valori dell'SD-1 (C6 18n/100k, 4,7k + 47n, 33k + DRIVE 1M B, 10k/18n, tone 20k W con 470Ω + 27n, 10k // 10n). Le compensazioni Miller sono C8 27p + R14 2,7k e C12 33p + R23 1,5k. I diodi sono 1SS387, 1:2.

- **Modo Custom** (switch analogici TC7W66FK):
  - R53 10k + C27 33n in parallelo a R6/C7. La Rg a HF diventa 3,2k, quindi G = 11,3 → 324 (max 50,2 dB contro 46,9 dB); seconda fc a 482 Hz, cioè più bassi nel clipping.
  - Inserisce un ulteriore discrete op-amp: Rf = 10k // 15n, Rg = 47k + 1µF. Guadagno in bassa frequenza 1,21 (+1,7 dB), shelf sotto ≈ 1,06 kHz; kanengomibako simula circa +2 dB.
  - Sul PCB ci sono le piazzole di un gyrator non popolato.

Fonti:
- https://kanengomibako.github.io/pages/00334.html (trace completo)
- https://aionfx.com/app/files/docs/stratus_deluxe_documentation.pdf

## OD-2 Turbo OverDrive (1985) / OD-2R (1994) — fiducia: alta

Il pedale contiene due circuiti separati, selezionati con switch FET. Il DRIVE è un 250kA doppio: una sezione per ciascun canale.

**Canale Turbo** (rail 5,6 V da zener, bias 2,6 V: trimmer RT1 sull'OD-2, 16k/14k fissi sull'OD-2R; Aion usa 10k/10k, cioè 2,8 V):

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Buffer JFET (2SK117GR) | R1 10k, C1 22n, 1M | HPF 7,2 Hz |
| 2 | Discrete op-amp #1 (Q13/Q14 + Q15 2SA970) | ingresso C33 18n / R41 100k; Rg = 4,7k + 100n; Rf = DRIVE 250kA (+4,7k OD-2R); Cf 100p; Rc 2,2k | HPF 88 Hz; G = 1 → 54,2 (OD-2) / 2 → 55,2 (OD-2R, max 34,8 dB); fc(Rg) 339 Hz; LPF(Cf) 6,4 kHz al massimo |
| 3 | Rete interstadio | 22n/100k; 10k + 6,8n; C23 18n (fabbrica) / 10n (Aion) su 100k | HPF 72 Hz; LPF 2,34 kHz; HPF 88 Hz (18n) o 159 Hz (10n) — discrepanza da verificare |
| 4 | Discrete op-amp #2, guadagno fisso, **saturazione sui rail** | Rg = 4,7k + 150n; Rf = 270k // 100p; collettore 1,8k + 560Ω (fabbrica) / 820Ω (Aion) | G = 58,4 (35,3 dB); fc 226 Hz; LPF 5,9 kHz; partitore d'uscita 0,24 / 0,31 |
| 5 | Shelf LPF | 10k, 4,7k + 10n | polo 1,08 kHz, zero 3,39 kHz, −9,9 dB in alta frequenza |

**Canale Normal** (rail 8,2 V, bias 4,1 V): ingresso 18n + 100k, poi discrete op-amp Q18/Q19 + Q20.
- Rg = 1k + 220n (723 Hz); Rf = DRIVE (gang B) // 47p // diodi D11 da un lato, D9+D10 dall'altro (asimmetrico 1:2). G = 1 → 251 (48 dB).
- Uscita su 10k + 15n (LPF 1,06 kHz).

**Comune:** tone passivo (18n in serie, 10kB, 22n → 27n sull'OD-2R, LPF minimo 723 → 589 Hz), poi level e buffer.
- L'OD-2R aggiunge un recupero di volume a 2 transistor (Q8 NPN + Q9 PNP, emettitore 4,7k // (680Ω + 10µ)): circa ×7,4 (+17 dB).
- Sull'OD-2 originale il volume arrivava a malapena all'unità.

Fonti:
- https://www.hobby-hour.com/electronics/s/od2-turbo-overdrive.php (schema di fabbrica OD-2)
- https://aionfx.com/app/files/docs/aurum_documentation.pdf (OD-2R Turbo e differenze OD-2/OD-2R)
- https://aionfx.com/project/aurum-amp-overdrive/

## BD-2 Blues Driver (1995–) — fiducia: alta

Alimentazione: 9 V → 8 V tramite capacitance multiplier (Q2), Vref 4 V bufferizzato da IC1A (M5218AL).

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Buffer JFET 2SK184GR | R18 10k, C14 47n (THD) / 1µF (SMD), R15 1M | HPF 3,4 Hz (47n) |
| 2 | Discrete op-amp #1 (Q10/Q11 + Q9 2SA1335) | ingresso C18 100n / R23 220k; Rg = 1,5k + 150n; Rf = 22k + GAIN-B 250kA; Cf 47p; Miller 47p; Rc 2,2k | G = 15,7 → 182 (23,9 → 45,2 dB, ideale); misurato ≈ 40 dB con picco a 2–3 kHz; fc(Rg) 707 Hz |
| 3 | **Stack Fender TMB fisso** (T=0, M≈6, B=10) | vedi sotto | recupera i bassi persi nello stadio 1 |
| 4 | Diodi shunt a massa | 1SS133 ×2 in serie per verso | ±≈1,2 V; quasi mai in conduzione |
| 5 | Discrete op-amp #2 (Q13/Q14 + Q12) | ingresso C27 2,2n / R35 1M; Rg = 2,2k + 1µ; Rf = 33k + GAIN-A 250kA; Cf 100p; Miller 100p | HPF 72 Hz; G = 16 → 129,6 (24,1 → 42,3 dB); LPF 5,6 kHz al massimo; **satura sul rail di 8 V** |
| 6 | Shelf LPF | R26 5,6k // C17 6,8n; C19 5,6n a massa | polo 2,29 kHz, zero 4,18 kHz, −5,2 dB in alta frequenza |
| 7 | Tone passivo + level | 18n, TONE 10kB, 18n; LEVEL 100kA | LPF minimo 884 Hz; circa piatto a ore 12 |
| 8 | M5218AL non invertente con **gyrator** | C10 47n / R13 470k; Rf = 6,8k // 2,2n; gamba: C16 56n + gyrator (R21 1,2k, C9 56n, R10 470k, Q7) | L = 1,2k·470k·56n = **31,6 H**; f₀ = **120 Hz**; picco ideale +16,5 dB, ma con guadagno del follower ≈ 0,99 e re si ottengono **+6,6 dB a 117 Hz**, banda −3 dB 100–140 Hz (simulazione propria; misura PedalPCB: +6 dB a 120 Hz); guadagno 1 altrove |
| 9 | Uscita | FET switch + emitter follower | — |

- **Stack TMB:**
  - Rami: IN →(220p + 330k)→ OUT; IN →100k→ M; M →100n→ OUT; M →47n→ G; OUT →1M→ G; G →15k→ GND.
  - Si modella con la funzione di trasferimento di Yeh & Smith (DAFx-06) con t = 0, b = 1, m ≈ 0,6, oppure risolvendo direttamente i nodi sopra.
- **Diodi D1/D3:** antiparalleli tra gli ingressi dell'op-amp, intervengono solo quando satura.
- **Revisione SMD:** C14 da 1µ, sink di corrente sul buffer, MOSFET di protezione. Il suono è equivalente (kanengomibako, misure THD/SMD).

Fonti:
- https://www.hobby-hour.com/electronics/s/bd2-blues-driver.php (schema di fabbrica)
- https://aionfx.com/app/files/docs/sapphire_documentation.pdf (anche mod Galaxie)
- https://kanengomibako.github.io/pages/00291.html
- https://forum.pedalpcb.com/threads/this-week-on-the-breadboard-blues-driver-bd-2-bd-2w-part-1.7390/
- https://www.analogisnotdead.com/article25/circuit-analysis-the-boss-bd2
- https://ccrma.stanford.edu/~dtyeh/papers/yeh06_dafx.pdf

## BD-2W Blues Driver Waza Craft (2014) — fiducia: alta

Tutto discreto: il terzo stadio M5218 è sostituito da un discrete op-amp. Il percorso è come nel BD-2; le differenze principali sono Miller C18 47p (100p nel BD-2), elettrolitici al tantalio, buffer 2SK880 con sink di corrente e ingresso 2×1µF.

- **R15 (Rg dello stadio 1):** kanengomibako riporta 1,5k, Chuck D. Bones 1,2k (G max 182 contro 228).
- **Switch S/C** (TC7W66FK, commutati insieme):
  - **TIGHT:** R42 10k (tramite due condensatori da 1µ) in parallelo al "bass" da 1M dello stack; C41 100n in parallelo a C16 2,2n (HPF 72 Hz → 1,6 Hz). Risultato: −4,5 dB a 50 Hz e +1 dB a 500 Hz all'ingresso dello stadio 2.
  - **FAT:** C44 22n in parallelo a C21 18n (tone in = 40n, LPF minimo 398 Hz); C43 4,7n in parallelo a C20 5,6n.
  - **Gyrator:** S usa 1,2k / 56n / 56n (31,6 H, 120 Hz); C usa 820Ω / 68n / 56n (26,2 H, 131 Hz, misurato 135 Hz). In entrambi i casi il bump misurato è di circa 6 dB.

Fonti:
- https://kanengomibako.github.io/pages/00293.html
- https://forum.pedalpcb.com/threads/this-week-on-the-breadboard-blues-driver-bd-2-bd-2w-part-2.7404/
- https://forum.pedalpcb.com/threads/this-week-on-the-breadboard-blues-driver-bd-2-bd-2w-part-3.7419/

## OD-3 OverDrive (1997–) — fiducia: alta

Rail: 8 V; rail a zener MTZ5.6B per gli stadi di guadagno (misurato 4,6 V in un'unità del 1998; altre trace e il clone Aion usano 9 V); VCOM 4 V.

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Buffer JFET Q8, Q10 (2SK184GR) | 10k, 47n, 1M; C29 18n / R46 100k | HPF 88 Hz |
| 2 | Notch a T pontato | 10k–10k, 3,3k + 56n a massa, 10n in ponte | **−4,5 dB a 500 Hz** |
| 3 | Discrete op-amp (Q13/Q14 + Q11 2SA1048), diodi 1SS133 antiparalleli in retroazione | Rg = 470Ω + 470n; Rf = 3,3k + DRIVE 100kA; Rc 2,7k | G = 8,0 → 220,8 (18,1 → 46,9 dB); fc 720 Hz; misurato 16 → 43 dB a 1 kHz. D9/D12 limitano l'ingresso |
| 4 | Partitore + LPF | 56n, 100k, 33k, 1,2n | −12,1 dB; LPF 5,3 kHz |
| 5 | JFET a source comune Q15, 1N4004 antiparalleli gate↔drain (via 470n) | Rd 10k, Rs 2,2k // (1k + 10µ) | ≈ ×14,5 stimato (misurato ≈ 11 dB); secondo stadio di clipping morbido |
| 6 | M5218 non invertente | 4,7k + 12n; 47k / 150k; Rf 6,8k, Rg 1k + 10µ | LPF 2,82 kHz; G 7,8 (≈ 15,5 dB con il partitore) |
| 7 | M5218 invertente con EQ | Rin = 12k // (4,7k + 6,8n); Rf = 12k // (4,7k + 18n) | 0 dB in bassa e alta frequenza; **notch −4,4 dB a 1,6 kHz** |
| 8 | Tone + level | 33n, TONE 10kB, 22n; LEVEL 100kB | LPF minimo 723 Hz |

- **Clipping:** simmetrico, quindi armoniche dispari; lo stadio JFET aggiunge una lieve asimmetria.

Fonti:
- https://kanengomibako.github.io/pages/00279.html (trace)
- https://forum.pedalpcb.com/threads/this-week-on-the-breadboard-boss-od-3.12205/
- https://aionfx.com/app/files/docs/heliodor_documentation.pdf

## OS-2 OverDrive/Distortion (1990–) — fiducia: alta

Alimentazione: 9 V, Vref 4,5 V. Due percorsi paralleli dopo il buffer 2SK184GR: DS (stile RAT) e OD (stile SD-1).

- **Percorso DS** (NJM1458):
  - Ingresso 22n/220k (33 Hz).
  - Gamba di massa doppia: 1,2k + 2,2µ (60 Hz) // 100Ω + 0,47µ (3,39 kHz).
  - Rf = 12k + DRIVE-B 250kA (in serie a 10µ) // 4,7M // 100p. G a HF = 131 → 2840 (fino a 69 dB); nella banda media 11 → 219. In pratica domina il limite di GBW/slew del 1458.
  - Poi 1k + 1SS133 antiparalleli a massa: clipping duro ±0,6 V.
  - Tono fisso tipo DS-1 al 40%: LPF 6,8k/100n = 234 Hz, HPF 18n su 9k = 982 Hz, miscela 8,2k/12k; follower; 20k + 820p (LPF 9,7 kHz).
- **Percorso OD:**
  - Ingresso 4,7n/100k (339 Hz).
  - Rg = 100Ω + 4,7µ (339 Hz); Rf = 1k + DRIVE-A 250kA // 100p // D7 da un lato, D9+D8 dall'altro (1:2). G = 11 → 2511 (fino a 68 dB).
  - Poi 20k + 10n (LPF 796 Hz).
- **COLOR 20kB:** crossfade con il wiper a massa AC; ogni lato ha 20k di sorgente. Somma passiva (DS tramite 33k, OD tramite 100k) su M5218 non invertente, G = 1 + 150k/22k = 7,8 (17,9 dB).
- **Tone TS-like:** 20k "G", wiper → 4,7k + 10n (3,39 kHz), Rf 10k // 1n (15,9 kHz); senza LPF d'ingresso.
- **Uscita:** LEVEL 50kA, poi buffer.
- kanengomibako segnala che alcuni valori (C27, R29) differiscono dagli schemi che circolano in rete.

Fonti:
- https://kanengomibako.github.io/pages/00294.html
- https://www.boss.info/us/products/os-2/specifications/

## SD-2 Dual OverDrive (1993–98), canale Lead — fiducia: media

Il canale Crunch non è coperto perché manca un trace.

| # | Stadio | Componenti | Risultati |
|---|---|---|---|
| 1 | Source follower 2SK184 | 47n, 1M, 4,7k | — |
| 2 | 4558 + **gyrator mid** | Rf 2,2k // 100p; gamba 27n + gyrator (2,2k, 47k, 8,2n, Q2) | L = 0,85 H; **f₀ = 1,05 kHz**, Q ≈ 2,5, +6 dB ideale |
| 3 | Gain (GAIN 250kA) | Rg = 4,7k + 82n; Cf 68p | G = 1 → 54,2 (34,7 dB); fc 413 Hz; LPF 9,4 kHz |
| 4 | Invertente ad alto guadagno | 68n, 15k; 1M // 180p | G = −66,7 (36,5 dB); banda 156–884 Hz; clipping sui rail |
| 5 | LED a massa | 1k + LED rossi antiparalleli; 1k // 22n; 10k | ±≈1,8 V |
| 6 | Non invertente, diodi 1:2 in retroazione | Rf 100k // 470p; Rg 47k + 22n | G 3,13 (9,9 dB); LPF 3,39 kHz |
| 7 | LPF + shelf dei bassi | 2,2k + 15n (4,8 kHz); 22k, 47n + 12k | shelf 100 → 282 Hz, −9 dB in alta frequenza |
| 8 | Invertente | 33k; 100k // 220p | ×3,03 (9,6 dB); LPF 7,2 kHz |
| 9 | Tone + volume + uscita | 100n, TONE 10kB, 27n; VOL 100kA | LPF minimo 589 Hz |

Fonti:
- https://aionfx.com/app/files/docs/tachyon_documentation.pdf
- https://aionfx.com/project/tachyon-amp-overdrive/

## ODB-3 Bass OverDrive (1994–) — fiducia: media

Fonte: schema hobbistico (schematicsonline / freestompboxes). Buffer 2SK184, HPF 100n/100k (16 Hz), poi due rami.

- **Ramo drive:**
  - Discrete op-amp #1: Rf = 2,2k + GAIN 250k, 330p; Rg = 1,5k + 10µ. G = 2,5 → 169 (44,6 dB).
  - Discrete op-amp #2: Rf = 100k // 330p // 2 LED rossi; Rg = 1,8k + 100n (884 Hz). G = 56,6 (35 dB); LPF 4,8 kHz.
  - Partitore 1,5k/820Ω (0,35) e filtri verso BALANCE.
- **Ramo clean:** half M5218. La topologia non è chiara nel disegno disponibile.
- **BALANCE 250kB:** crossfade con resistenze da 47k. Il pot carica i filtri del ramo drive, quindi la risposta dipende dalla posizione.
- **EQ attivo:** M5218, Rf 22k // 47p. HIGH 25kB con 2,2k + 15n (≈ 4,8 kHz). LOW 25kB con 0,15µ verso un gyrator (3,3k / 47n / 100k): L ≈ 15,5 H, f ≈ 104 Hz, da verificare.
- **Uscita:** LEVEL 50kA, follower.

Fonti:
- https://schematicsonline.com/schematic/boss-odb-3-bass-overdrive/
- https://www.freestompboxes.org/viewtopic.php?t=30932
- http://dirtboxlayouts.blogspot.com/2022/12/boss-odb3-bass-overdrive.html

## PW-2 Power Driver (1996–97) — fiducia: media

Rail: 8 V, 4,5 V e 2,2 V a zener, Vref 4 V. Dirtbox segnala valori di zener e resistenze corretti in una versione successiva dello schema.

1. Buffer 2SK118.
2. Emettitore comune 2SC2458 sul rail 4,5 V: Rc 1,8k, Re 1,8k // (33Ω + 100µ). ≈ ×29 (29 dB), saturazione dura.
3. Discrete op-amp (2SK184 + 2SA1048) su 4,5 V: Rf = 100Ω + DRIVE 50kA; Rg = 470Ω + 10µ. G 1,2 → 108 (40,6 dB).
4. M5218 invertente 15k/4,7k (×3,2).
5. EQ attivo a due gyrator:
   - FAT (100k W): 2,2k/100k/68n e 1µF → 15 H, **≈ 41 Hz**.
   - MUSCLE (100k W): 220Ω/100k/10n e 0,1µF → 0,22 H, **≈ 1,07 kHz**.
6. Uscita: LEVEL 50kA, poi follower.

Fonti:
- https://schematicsonline.com/schematic/boss-pw-2-power-drive/
- http://dirtboxlayouts.blogspot.com/2020/11/boss-pw-2-power-driver.html
- https://www.freestompboxes.org/viewtopic.php?t=5103

## FA-1 FET Amplifier (1980–84, preamp/booster) — fiducia: media

1. Source follower JFET 2SK246GR, bias 3,3M, ingresso 10n (4,8 Hz).
2. Low cut: C3 47n in serie; in FLAT è in parallelo a 470n. Su 15k dà 20,5 Hz (FLAT) o 226 Hz (CUT).
3. Op-amp invertente HA1457 (≈ 1458): Rf = 470k // VOLUME 1MA (rheostat). G massimo ≈ 21,3 (26,6 dB), minimo circa 0.
4. Baxandall attivo: BASS 50kB con 2×33n, TREBLE 50kB con 2×5,6n, 8,2k/8,2k, 33k, 10k.
5. Uscita: 470Ω + 10µ.

Fonti:
- https://aionfx.com/app/files/docs/prism_documentation.pdf
- https://aionfx.com/project/prism-fet-amplifier/

## TB-2W Tone Bender Waza Craft (2022) — fiducia: bassa

Riferimento circuitale: Tone Bender MkII (clone Aion Deimos).

- **Circuito di riferimento:** Q1 PNP al germanio a emettitore comune (R1 100k, a volte 10k; Rc 10k; ingresso 4,7µ con 10n in shunt). Poi coppia Fuzz Face Q2/Q3 accoppiata in DC: 47k, 8,2k, 470Ω, retroazione 100k, ATTACK 1k (B originale) + 4,7µ, uscita 10n su LEVEL 100kA.
- **Dati BOSS:**
  - Zin a effetto acceso 15 kΩ, Zout 15 kΩ in THRU.
  - Selettore di tensione 7 / 9 / 12 V (convertitore buck più regolatore programmabile).
  - Bypass a relè, true o bufferizzato.
  - Transistor al germanio selezionati.
- Secondo un utente del forum, lo schema del libretto mostra un JFET tra Q1 e Q2: non verificato (allegato non accessibile).

Fonti:
- https://www.boss.info/us/products/tb-2w/
- https://forum.pedalpcb.com/threads/boss-tb-2w-photos.7181/
- https://aionfx.com/app/files/docs/deimos_documentation.pdf

## BP-1W Booster/Preamp Waza Craft (2023) — fiducia: bassa

Non esiste un trace pubblico. Dati disponibili:

- **Modi:** CE (preamp del CE-1: brillante, bassi stretti), RE (preamp del RE-201: grasso, caldo, compressivo), NAT (boost pulito).
- **Buffer d'ingresso:** STD con Zin 1 MΩ, VTG con Zin 100 kΩ.
- **Livelli:** −20 dBu; Zout 1 kΩ.
- **Consumo:** 60 mA.

Fonti:
- https://www.boss.info/us/products/bp-1w/

## JB-2 Angry Driver (BOSS × JHS, 2017) — fiducia: bassa

- Il lato BD-2 è derivato dal Blues Driver; il lato JHS dall'Angry Charlie (drive a op-amp in stile Marshall).
- Knob concentrici DRIVE / TONE / LEVEL per ciascun lato.
- MODE a 6 posizioni: solo BOSS, solo JHS, i due in serie in entrambi gli ordini, parallelo, A/B commutato dal footswitch.
- Zin 1 MΩ, consumo 55 mA.
- Non esiste un trace pubblico.

Fonti:
- https://www.boss.info/us/products/jb-2/

## DN-2 Dyna Drive (2003) — fiducia: bassa (tipo di tecnologia non confermato)

- Usa la tecnologia "Dyna Amp" del Roland CUBE-60: suono pulito con plettrata leggera, overdrive con plettrata forte o volume della chitarra alto.
- Controlli: DRIVE / TONE / LEVEL.
- Zin 1 MΩ, Zout 1 kΩ.
- Il consumo di 36 mA suggerisce un DSP, ma non è verificato.
- Per l'emulazione serve un modello dipendente dall'inviluppo.

Fonti:
- https://www.boss.info/us/products/dn-2/

## OD-1X OverDrive (2014, MDP digitale) — fiducia: media, solo comportamento misurato

Hardware: codec AK4556 e DSP; i pot da 50k sono letti dall'ADC.

Misure di kanengomibako:
- **DRIVE:** escursione di circa 40 dB tra 0 e 100; con il gain alto bassi e alti calano leggermente.
- **LOW:** picco a circa 100 Hz, circa −20 / +13 dB rispetto a LOW 50; sotto circa 80 Hz taglio ripido.
- **HIGH:** shelf/picco attorno a 7 kHz con un dip mantenuto a circa 4 kHz; oltre 10 kHz passa-basso ripido.
- **Forma d'onda:** asimmetrica, con molte armoniche pari.
- **Consumo:** 65 mA.

Fonti:
- https://kanengomibako.github.io/pages/00289.html
- https://www.boss.info/us/products/od-1x/

## BB-1X Bass Driver (2016, MDP digitale) — fiducia: bassa

- Controlli: LEVEL, BLEND, LOW, HIGH, DRIVE.
- Uscite: OUTPUT (1 kΩ) e LINE OUT bilanciata (600 Ω) con voicing dedicato.
- Zin 1 MΩ, consumo 55 mA.
- Non ci sono misure pubbliche.

Fonti:
- https://www.boss.info/us/products/bb-1x/

---

## Lacune e note

- **BD-1W:** non esiste. Non c'è pagina boss.info (404) né altre fonti; probabilmente è una confusione con BD-2W o BP-1W.
- **"PW-?":** l'unico compatto trovato è il PW-2 Power Driver, documentato sopra.
- **SD-2 Crunch:** nessun trace trovato.
- **ODB-3:** il ramo clean è da verificare. **PW-2:** lo schema hobbistico ha correzioni note (zener).
- **Discrepanze tra fonti:**
  - OD-2 Turbo: C23 18n (fabbrica) contro 10n (Aion); R32 560Ω contro 820Ω. Potrebbero essere differenze tra OD-2 e OD-2R.
  - BD-2W: R15 1,5k contro 1,2k.
- **Non trattati qui:** SP-1 Spectrum, un booster parametrico (clone Aion "Chroma", https://aionfx.com/project/chroma-parametric-boost/), e i Waza DS-1W e altri distorsori, che appartengono al gruppo distorsione.
- **Fonti non raggiungibili:** freestompboxes.org e i vecchi schemi gaussmarkov sono bloccati da Cloudflare (403), quindi non sono stati letti direttamente.
