# Dossier: delay digitali, riverberi, looper, simulatori di ampli/IR (BOSS)

Data ricerca: 2026-09-27. Fonti principali: manuali ufficiali Roland/BOSS (static.roland.com, estratti con pdftotext; i manuali scansionati di DD-2, DD-5, RV-2, RV-3 sono stati letti visivamente), pagine specifiche boss.info, recensioni tecniche (Sound On Sound, Guitar World), letteratura DSP (Yeh & Smith DAFx-06), datasheet Celestion.
Dati strutturati: `digital_delay_reverb_amp.json` (35 voci). Legenda affidabilità: **[M]** = da manuale/specifiche ufficiali, **[R]** = recensione/terzi, **[I]** = inferenza o valore tipico da confermare con misure.

---

## 1. Delay digitali

### DD-2 (1983) [M]
- Primo delay digitale compatto; IC custom derivato dal Roland SDE-3000. Sistema: **compressione logaritmica analogica + quantizzazione a 12 bit**.
- Modi: S 12.5–50 ms, M 50–200 ms, L 200–800 ms, HOLD. Il D.TIME scala il fondo scala del modo da ×0.25 a ×1.
- HOLD: momentaneo (attivo solo tenendo premuto il pedale), loop 200–800 ms; ruotando D.TIME durante l'hold cambia l'intonazione (orario = più grave). Feedback disattivato in HOLD.
- Risposta in frequenza: ripetizioni 40 Hz–7 kHz (+1/−3 dB); dry 10 Hz–60 kHz. Rumore residuo −95 dBm (IHF-A).
- Il manuale avverte che segnali acuti possono generare battimenti (aliasing): "Slight counterclockwise rotation of D.TIME will decrease beats".
- Uscite: MAIN (mix) + DIRECT; in modo "stereo" il MAIN porta solo l'effetto.
- Fonte: https://static.roland.com/assets/media/pdf/DD-2_OM.pdf

### DD-3 / DD-3T [M]
- DD-3: stesse gamme del DD-2 (S/M/L 12.5–50 / 50–200 / 200–800 ms; HOLD 200–800 ms). Risposta 40 Hz–7 kHz (−3/+1 dB). Uscita OUTPUT + DIRECT OUT (se DIRECT è collegato, OUTPUT porta solo il wet).
- Revisioni hardware: 1986 con IC custom RDD63H101 e 3 DRAM da 64K; DD-3A (1991) gate array MN51010RBA e una sola DRAM M5M4464; DD-3B (2002) SMD; dal 2014 DSP Roland (nessuno schema pubblicato). Conversione a 12 bit con compressione logaritmica.
- **Frequenza di campionamento non pubblicata.** Stima [I]: 3×64 kbit per 800 ms a 12 bit ⇒ circa 20 kHz, coerente con la banda di 7 kHz. Da verificare con misure.
- DD-3T (2019): MODE S.50ms/M.200ms/L.800ms/SHORT LOOP. SHORT LOOP ripete ciò che precede la pressione finché il pedale è tenuto (200–800 ms). Tap da pedale (tenere premuto 2 s) o jack TEMPO: 133–800 ms (BPM 75–300); suddivisioni: terzina di semiminima, croma puntata, semiminima.
- Modellazione suggerita [I]: LPF a circa 7 kHz (anti-alias più ricostruzione) nel percorso wet e quindi in retroazione (ogni ripetizione si scurisce), quantizzazione companded a 12 bit, nessuna modulazione.
- Fonti: https://static.roland.com/assets/media/pdf/DD-3_eng01_W.pdf · https://static.roland.com/assets/media/pdf/DD-3T_eng02_W.pdf · https://stompboxelectronics.com/2023/07/20/history-of-the-boss-dd-3/
- Nota: non ho trovato un'edizione "anniversario" del DD-3 documentata in modo ufficiale; il DD-3T è l'erede.

### DD-5 (1995) [M]
- A/D: 16 bit lineare, sovracampionamento 64×, delta-sigma quad, metodo AF. D/A: 16 bit. **fs = 32 kHz**. Gamma dinamica 110 dB (dry) / 100 dB (effetto).
- Modi: 1: 1–50 ms · 2: 50–200 · 3: 200–800 · 4: 800–2000 · 5: HOLD (2 s) · 6: REVERSE (1–2 s [R]) · 7: E/D 1–400 ms (dry e wet su uscite separate) · 8–11: TEMPO (con un tap di 300 ms: semiminima 300 ms, croma puntata 225, croma 150, terzina di semiminima 200).
- Fonte: https://static.roland.com/assets/media/pdf/DD-5_OM.pdf

### DD-6 (2002) [M]
- Modi: 80 ms, 300 ms, 800 ms, 2600 ms, REVERSE, HOLD (5.2 s, sound-on-sound), WARP (premendo il pedale feedback e livello salgono oltre le posizioni dei knob).
- Uscita "Long delay" (ingresso A, uscita B): tempi raddoppiati (80→160, 300→600, 800→1600, 2600→5200 ms). Altri metodi: Panning, Effetto+Dry, Stereo. Stereo 2-in/2-out. Globale 1 ms–5.2 s; alcuni effetti limitati a 2.6 s in stereo. Rumore −90 dBu.
- I minimi di ciascun modo non sono indicati nel manuale.
- Fonte: https://static.roland.com/assets/media/pdf/DD-6_OM.pdf

### DD-7 (2008) [M]
| Modo | Normale | Long |
|---|---|---|
| 50ms | 1–50 ms | 2–100 |
| 200ms | 50–200 | 100–400 |
| 800ms | 200–800 | 400–1600 |
| 3200ms | 800–3200 | 1600–6400 |
| MODULATE (chorus sul wet) | 20–800 | 40–1600 |
| ANALOG (modello del BOSS DM-2: le ripetizioni cambiano progressivamente) | 20–800 | 40–1600 |
| REVERSE | 300–3200 | 600–6400 |
| HOLD | 40 s mono / 20 s stereo | |
- EXP controlla tempo, feedback e livello; tap da footswitch esterno. Stereo: Panning, Effetto+Dry, Long.
- Fonti: https://static.roland.com/assets/media/pdf/DD-7_e01_W.pdf · https://www.boss.info/us/products/dd-7/specifications/

### DD-8 (2019) [M]
| Modo | Descrizione | Tempo |
|---|---|---|
| STANDARD | digitale pulito | 20–800 ms |
| ANALOG | analogico morbido | 20–800 |
| TAPE | modulazione da nastro | 20–800 |
| WARM | digitale morbido | 20–800 |
| REVERSE | al contrario; con E.LEVEL al massimo solo wet | 300–5000 |
| +RV | delay + riverbero | 20–800 |
| SHIM | con pitch-shift | 200–800 |
| MOD | digitale modulato | 20–800 |
| WARP | "sognante" | 20–800 |
| GLT | "mitragliatrice" mentre il pedale è premuto; FEEDBACK = profondità; tempi del tap ÷4 | 10–400 |
| LOOP | looper | 40 s mono / 20 s stereo |
- Tap 67–10 000 ms (BPM 24–300) con TAP DIVISION; uscita Long che raddoppia i tempi; CARRYOVER; TWIST da switch esterno; E.LEVEL a ore 3 = wet pari al dry.
- Fonti: https://static.roland.com/assets/media/pdf/DD-8_eng03_W.pdf · https://static.roland.com/assets/media/pdf/DD-8_Reference_eng01_W.pdf

### DD-20 Giga Delay (2003) [M]
- 1 ms–23 s (passi di 1 ms; Time Advance). Undici modi: SMOOTH (diffuso, simile a un riverbero), TAPE (modello RE-201, 1 o 2 testine), ANALOG (modello DM-2), STANDARD, DUAL (corto + lungo in serie, corto di default 50 ms), PAN, MODULATE (rate/depth, default r80/d70), REVERSE, SOS (23 s), TWIST (oscilla, accelera e sale di intonazione mentre si tiene il pedale), WARP.
- TONE piatto al centro. Rumore −93 dBu.
- Fonte: https://static.roland.com/assets/media/pdf/DD-20_OM.pdf

### DD-200 [M]
- 96 kHz, AD/DA a 32 bit, virgola mobile a 32 bit. Dodici modi, ognuno con un PARAM dedicato: STANDARD (attacco), ANALOG (carattere/distorsione), TAPE (**modello Roland RE-201**, combinazione di 3 testine; il punto decimale indica distorsione aggiunta), DRUM (**modello Binson EchoRec 2**, 4 testine/ALL), SHIMMER, TERA ECHO, PAD ECHO, PATTERN (16 delay), LO-FI, DUAL (secondo delay in % del primo), DUCKING, REVERSE.
- TAP DIVISION 33–300% del tap (9 valori). Looper circa 60 s mono. 127 memorie. Tempo massimo circa 10 s [R].
- Fonti: https://static.roland.com/assets/media/pdf/DD-200_eng03_W.pdf · https://www.boss.info/us/products/dd-200/specifications/ · https://articles.boss.info/exploring-the-differences-between-boss-delay-pedals/

### DD-500 [M]
- 96 kHz, 32 bit, fino a 10 s (dipende dal modo). Dodici modi: STANDARD, ANALOG (parametro STAGE = numero di stadi BBD; il tempo cresce in proporzione), TAPE (RE-201 con testine a 1×/2×/3×), VINTAGE DIGITAL (SDE-3000/SDE-2000; TIMEx2 dimezza fs e raddoppia il tempo; fase del delay e del feedback invertibili; high-cut dell'SDE-3000), DUAL (serie/parallelo), PATTERN (16 tap), REVERSE, SFX (bit depth, sample rate, tremolo), SHIMMER, FILTER, SLOW ATTACK, TERA ECHO.
- Looper: 60 s mono a 96 kHz, 120 s mono a 48 kHz, 60 s stereo a 48 kHz.
- Fonti: https://static.roland.com/assets/media/pdf/DD-500_e02_W.pdf · https://www.boss.info/us/products/dd-500/specifications/

### SDE-3 [R, bassa affidabilità]
- Due delay paralleli in stile SDE-3000; 800 ms in stereo, 1600 ms in mono; controlli OFFSET e HI CUT; modi Stereo/Panning.
- https://www.guitarworld.com/gear/effects-pedals/boss-sde-3-dual-digital-delay-review

### TE-2 Tera Echo (2013) [M/R]
- MDP (algoritmo proprietario). Controlli: E.LEVEL, TONE (brillantezza), FEEDBACK (decadimento), S-TIME (spread/lunghezza). Tenendo premuto il pedale il suono dell'effetto si congela (HOLD). Stereo in/out.
- Sound On Sound: multitap con spaziatura dei tap crescente e filtro sensibile all'inviluppo prima del delay; il TONE sposta la risonanza del filtro; loop di freeze di circa 1 s.
- https://static.roland.com/assets/media/pdf/TE-2_M_eng02_W.pdf · https://www.soundonsound.com/reviews/boss-te2-tera-echo

### DM analogici
DM-2, DM-3, DM-2W e DM-101 sono BBD analogici e restano fuori da questo gruppo. Riferimento DM-2W: 20–300 ms standard, 800 ms in modo Custom [R].

---

## 2. Space Echo: RE-2 / RE-202 / RE-20 e RE-201 originale

### Geometria delle testine [M]
- Tempi a multipli interi del tempo della testina 1: **H2 = 2·T1, H3 = 3·T1** (RE-202: **H4 = 4·T1**). Il REPEAT RATE (velocità del nastro) scala tutte le testine insieme; il manuale RE-2 precisa che cambia anche il timbro.
- Tap: imposta T1. RE-2/RE-202: T1 max 1 s in modo Normal ("stesso range del RE-201"), 2 s in Long. RE-20: tap max 3 s nelle posizioni 3/7 (H3), 6 s in Long.
- RE-201 reale: nessun tempo ufficiale; massimo tipico circa 600 ms (H3 a velocità minima) su macchine revisionate (Soundgas). Stima [I]: T1 circa 60–200 ms.

### Mappa dei modi RE-2 (11 modi; tabella letta dalle coordinate del PDF) [M]
| Modo | Testine | Riverbero |
|---|---|---|
| 1 | H1 | – |
| 2 | H2 | – |
| 3 | H3 | – |
| 4 | H2+H3 | – |
| 5 | H1 | sì |
| 6 | H2 | sì |
| 7 | H3 | sì |
| 8 | H1+H2 | sì |
| 9 | H2+H3 | sì |
| 10 | H1+H3 | sì |
| 11 | H1+H2+H3 | sì |
Coincide con il RE-201: modi 1–4 solo eco, 5–11 eco più molla, 12 solo riverbero (sul RE-2 si ottiene portando ECHO a 0).

### RE-202 (12 modi, 4 testine, riverbero separato) [M, modi 8–12 con affidabilità media]
1:H1 · 2:H2 · 3:H3 · 4:H1+H2 · 5:H2+H3 · 6:H1+H3 · 7:H1+H2+H3 · 8:H1+H4 · 9:H3+H4 · 10:H1+H3+H4 · 11:H1+H2+H4 · 12:tutte.
- Controlli: SATURATION ("distorsione del preamp + saturazione magnetica del nastro", sensazione di compressione), WOW & FLUTTER, BASS/TREBLE sull'eco, TAPE (nastro nuovo o usato: cambiano tono e oscillazione), REVERB TYPE (molla RE-201, hall, plate, room, ambience).
- Specifiche: 48 kHz, AD 24 bit + AF, DA 32 bit, virgola mobile a 32 bit; MIDI; 127 memorie.
- RE-2: TONE unico di tipo tilt; opzione per il dry "RE-201 preamp simulation" oppure bypass analogico; TWIST (rotazione aggressiva, runaway); INTENSITY al massimo = oscillazione.
- Fonti: https://static.roland.com/assets/media/pdf/RE-2_eng01_W.pdf · https://static.roland.com/assets/media/pdf/RE-202_reference_eng02_W.pdf · https://www.boss.info/us/products/re-202/specifications/ · https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2 · https://soundgas.com/blogs/resources/whats-the-maximum-delay-time-of-a-roland-space-echo · https://media.sweetwater.com/store/media/re-20_om.pdf

### Modellazione del nastro [I, valori tipici da tarare]
- Linea con tempo frazionario modulato: wow 0.3–1 Hz e flutter 4–12 Hz (flutter con componente casuale); profondità da circa 0.05% a oltre 1% di pitch.
- In retroazione: saturazione morbida (tanh o isteresi) → HPF circa 80–150 Hz → LPF 3–5 kHz che dipende dalla velocità (a nastro più lento banda più stretta); BASS/TREBLE a shelving.
- Cambiando REPEAT RATE si modifica la velocità di lettura: il pitch scivola durante il cambio (effetto "twist" o runaway).
- Molla: vedi §3 (catena dispersiva di allpass).

---

## 3. Riverberi

| Pedale | Tecnologia | Modi | Tempo / pre-delay |
|---|---|---|---|
| RV-2 (1987) [M] | **31.25 kHz, 12 bit lineare**, banda 30 Hz–10 kHz, solo alimentatore | Room (≤8 m²), Hall1 (≤15 m²), Hall2 (≤30 m²), Plate, Delay (ping-pong in stereo), Gate | non pubblicato |
| RV-3 (1994) [M] | **32 kHz, 16 bit** (A/D 128×, AF), GD 110/95 dB | 1–3 Delay 32–125/125–500/500–2000 ms; 4–7 Delay+Room1/Room2/Hall/Plate (tempo del riverbero fisso, delay 32–1000 ms); 8–11 Room1/Room2/Hall/Plate | – |
| RV-5 (2002) [M] | 2-in/2-out | Spring (modello **Accutronics, con l'interferenza fra due molle**), Plate, Hall, Room, Gate, Modulate (hall modulata) | non pubblicato |
| RV-6 (2015) [M] | stereo, EXP = profondità | Room, Hall, Plate, Spring, Modulate, +Delay (TONE = feedback del delay [R]), Shimmer, Dynamic (profondità che segue la dinamica del playing) | non pubblicato |
| RV-200 [M] | 96 kHz / 32 bit | Room (Amb/S/M/L), Hall (S/M/L), Plate (damp L/H −50..50), Spring (1–3 molle), Shimmer, Arpverb, Slowverb, Modulate, +Delay, Lo-Fi, Gate, Reverse; LOW/HIGH, DENSITY | TIME 0.1–10.0 s; pre-delay: range non trovato |
| RV-500 [M] | 96 kHz / 32 bit | Room, Hall, Plate, Spring (1–3), Shimmer (2 voci), Fast Decay, Early Reflection, Non-Linear, SFX, Dual, **SRV (Roland SRV-2000)**, **Space Echo (RE-201)** | TIME 0.1–10 s (ER e Reverse 0.1–1 s); PRE-DELAY 0–200 ms; low cut 20–800 Hz; EQ −24..+12 dB |
| FRV-1 [M] | COSM del **Fender Reverb 1963** (unità a valvole separata) | DWELL (drive nella molla; a 0 nessun riverbero; alto = "twang"), TONE, MIXER | – |

- FRV-1, circuito 6G15 reale [I]: 12AT7 in ingresso, 6K6GT pilota con trasformatore, 7025 in recupero; tank lungo a 3 molle. DWELL = guadagno nel driver (si satura nel pilota), MIXER = miscela dry/wet.
- Modellazione della molla [I]: cascata di 50–200 allpass del primo ordine (o stretched allpass) per il "chirp" dispersivo; ritardo di transito circa 30–40 ms per molla; banda utile circa 100 Hz–4.5 kHz; più molle con tempi leggermente diversi (interferenza, come descritto per l'RV-5); feedback per il decadimento (2–3 s).
- Fonti: https://static.roland.com/assets/media/pdf/RV-2_OM.pdf · https://static.roland.com/assets/media/pdf/RV-3_OM.pdf · https://static.roland.com/assets/media/pdf/RV-5_OM.pdf · https://static.roland.com/assets/media/pdf/RV-6_eng01_W.pdf · https://static.roland.com/assets/media/pdf/RV-200_eng01_W.pdf · https://static.roland.com/assets/media/pdf/RV-500_eng01_W.pdf · https://static.roland.com/assets/media/pdf/FRV-1_ejgfispd03_W.pdf · https://www.boss.info/us/products/rv-200/specifications/ · https://www.boss.info/us/products/rv-500/specifications/

---

## 4. Looper compatti e twin

| Pedale | Tempo massimo | Funzioni / formato |
|---|---|---|
| RC-1 | circa 12 min stereo | anello LED, undo/redo, frase minima 0.25 s |
| RC-2 | 16 min in totale, 11 frasi | loop quantize, ritmi guida, undo/redo, AUX |
| RC-20XL | 16 min, 11 frasi | reverse, quantize, ingresso mic |
| RC-3 | circa 3 h, 99 frasi | stereo, ritmi, auto-rec, WAV 44.1 kHz/16 bit |
| RC-30 | circa 3 h, 99 frasi | 2 tracce, LOOP FX, WAV 44.1/16 |
| RC-5 | 1.5 h per traccia, 13 h in totale, 99 memorie | 57×2 ritmi, 7 kit, AD/DA 32 bit, WAV 44.1 kHz/32f, MIDI |
| RC-10R | circa 6 h | 2 tracce, oltre 280 ritmi, 16 kit, 44.1 kHz/32f |
| SL-20 (slicer) | registrazione di 40 s | 30–250 bpm, pattern HARMONIC, 6 tipi stereo, MIDI IN |
Fonti: manuali RC-1_M_eng02_W, RC-2_OM, RC-3_eng03_W, RC-5_eng02_W, RC-10R_eng03_W, RC-20XL_OM, RC-30_eng03_W, SL-20_OM (tutti su https://static.roland.com/assets/media/pdf/) · https://www.boss.info/us/products/rc-5/specifications/

---

## 5. Simulatori di ampli e IR

### FBM-1 '59 Bassman [M] e circuito 5F6-A
- Controlli: GAIN (volume dell'ampli = drive), PRESENCE, MIDDLE, BASS, TREBLE, LEVEL; jack INPUT e BRIGHT IN (acuti più evidenti). Uscita 2.2 kΩ. Pensato per entrare in un ampli: il manuale non documenta una simulazione di cassa.
- **Tone stack 5F6-A (Yeh & Smith, DAFx-06):** R1 (treble) 250k, R2 (bass) 1M, R3 (mid) 25k, R4 (slope) 56k, C1 250 pF, C2 = C3 = 20 nF. Pilotato da un cathode follower (sorgente sotto 1 kΩ). Trasferimento del terzo ordine, discretizzabile in forma chiusa con la bilineare.
- Caratteristiche (ampbooks): scoop dei medi −10 dB con bassi e acuti al minimo; banda passante dei bassi −3 dB a 142 Hz; taglio inferiore degli acuti circa 2.5 kHz. Variante Marshall 1987/1959: slope 33k, C1 500 pF, 22 nF (scoop −7 dB).
- Resto del 5F6-A [I]: 12AY7 in ingresso, 12AX7 come guadagno più cathode follower, invertitore di fase long-tail, 2×5881/6L6, raddrizzatrice GZ34/5AR4 (sag), 4×10" Jensen P10R in cassa aperta.

### FDR-1 '65 Deluxe Reverb [M] e AB763
- Controlli: GAIN, LEVEL, TREBLE, BASS, REVERB, VIBRATO (profondità; rate impostato col pedale, periodo 0.3–3.0 s). Il "vibrato" Fender è in realtà un tremolo d'ampiezza.
- **Tone stack AB763 Deluxe:** C_treble 250 pF, C_bass 0.1 µF, C_mid 0.047 µF (alcune fonti indicano 0.022 µF), R_mid fisso 6.8k (manca il pot dei medi), slope 100k, pot treble e bass 250k (log), volume 1M; bright cap 47 pF sul canale vibrato. Pilotato dalla placca (impedenza di sorgente più alta del Bassman: va modellata nel circuito). [M/R, affidabilità media sui pot]
- Resto [R/I]: V1 7025 con Rk 1.5k / 25 µF, carico di placca 100k; 2×6V6GT in push-pull classe AB con bias fisso; retroazione negativa 820 Ω; GZ34; 1×12".
- Fonti: https://static.roland.com/assets/media/pdf/FBM-1_OM.pdf · https://static.roland.com/assets/media/pdf/FDR-1_OM.pdf · https://ccrma.stanford.edu/~dtyeh/papers/yeh06_dafx.pdf · https://www.ampbooks.com/mobile/classic-circuits/bassman-tonestack/ · https://kr-sound.com/how-the-fender-ab763-deluxe-reverb-works/

### IR-2 [M]
- 96 kHz, AD 24 bit + AF, DA 32 bit, virgola mobile a 32 bit. Undici coppie ampli/cassa (IR Celestion; microfoni indicati):
  CLEAN (1×12 V-Type, chiusa, R-121) · TWN Twin Reverb (2×12 A-Type, aperta, MD421+R-121) · TWEED Bassman 4×10 (G10 Gold, chiusa, R-121+SM57) · DIAMOND AC30 (2×12 Blue, aperta) · CRUNCH MDP (2×12 G12-65, aperta) · BRIT Marshall 1959 (4×12 G12M Heritage, chiusa, MD421) · HI-GAIN MDP (4×12 Creamback) · SLDN Soldano SLO-100 (4×12 V30) · BROWN (4×12 G12M Heritage) · MODDED MDP (4×12 G12K-100) · RFIER Mesa Dual Rectifier canale 2 Modern (4×12 V30).
- Controlli: GAIN, BASS, MIDDLE, TREBLE, LEVEL, AMBIENCE (Room/Hall/Plate). Due canali. IR utente: WAV mono 44.1/48/96 kHz, 16/24 bit o 32f, lunghezza 200 o 500 ms. Impostazione dell'uscita secondo la destinazione (LINE, JC-120 RETURN/INPUT, ecc.).
- https://static.roland.com/assets/media/pdf/IR-2_eng02_W.pdf · https://static.roland.com/assets/media/pdf/IR-2_IR_Loader_eng01_W.pdf · https://www.boss.info/us/products/ir-2/specifications/

### IR-200 [M]
- 96 kHz, AD 32 bit + AF. Ampli per chitarra: Natural, JC-120, Twin Combo, Diamond Amp, Tweed Combo, X-Hi Gain, British Stack, BGR UB Metal. Ampli per basso: Natural Bass, X-Drive Bass, Concert.
- 154 IR preset (144 BOSS + 10 Celestion), 128 slot utente, IR fino a 500 ms, due casse A/B, 128 memorie.
- Casse BOSS: 1×12 aperta, 2×12 JC-120, 2×12 Jensen C-12K, 2×12 G12M, 4×10 Jensen P10R, 4×12 G12M, 4×12 G12T-75; basso 1×18, 2×15, 4×10, 8×10.
- https://www.boss.info/us/products/ir-200/specifications/ · https://static.roland.com/assets/media/pdf/IR-200_eng02_W.pdf

### Altri
- ST-2 Power Stack è analogico (fuori gruppo). Non esiste un pedale compatto "Katana".
- MO-2: MDP multi-overtone, MODE 1–3, BALANCE, DETUNE, TONE, uscita stereo. https://static.roland.com/assets/media/pdf/MO-2_M_eng02_W.pdf

---

## 6. Modellare IR di cassa da dati pubblici (per il plugin)

**Dati degli altoparlanti (catalogo Celestion) [M]:** i 12" per chitarra hanno Fs 70–85 Hz e banda nominale circa 70/75–5000 Hz (Greenback, V30, G12H: Fs 75–85 Hz; alcuni modelli fino a 5.5 kHz). Il G10 Gold ha Fs 80 Hz e banda 80–6000 Hz. Nel catalogo compaiono anche 10" e 8" con Fs 90–115 Hz. In una 4×12 chiusa la risonanza sale a circa 80–120 Hz; in cassa aperta scende leggermente (Aiken, forum Fractal).
Fonti: https://www.toutlehautparleur.com/media/catalog/product/datasheet/celestion/Guitar_Speaker_Catalogue.pdf · https://celestion.com/product/g12m-greenback/ · https://www.aikenamps.com/index.php/frequency-response-of-a-marshall-4x12-cabinet

**Ricetta parametrica [I] (valori di partenza da tarare contro IR reali):**
1. **HPF / risonanza:** HPF del secondo ordine a fc = Fs_cab. Cassa chiusa: fc circa 90–110 Hz, Q 1.2–2 (picco di +3..+6 dB), sotto 12 dB/ottava. Cassa aperta: fc circa 70–90 Hz, Q circa 0.7, poi una cancellazione di dipolo, cioè un ulteriore HPF del primo ordine a circa 120–200 Hz.
2. **Medi e breakup del cono:** picchi o nulli in 1.5–4 kHz (peaking ±3..8 dB, Q 2–5). Vintage 30: picco pronunciato verso 2–3 kHz. Greenback: medi più morbidi, circa 1.5–2 kHz.
3. **Roll-off:** LPF ripido a circa 4.5–6 kHz (4° ordine o più, da −24 a −48 dB/ottava) con 1–2 notch fra 6 e 9 kHz.
4. **Microfono:** on-axis al centro del cono = più brillante. Spostandosi verso il bordo o fuori asse (45°) il LPF effettivo scende di circa 1–2 kHz. Un dinamico cardioide vicino (SM57/MD421 a 1–5 cm) dà un effetto prossimità di +3..+8 dB sotto circa 200 Hz. Un ribbon R-121 (figura a 8) è più scuro con bassi pieni.
5. **Comb da posizione / riflessioni:** una riflessione con differenza di percorso Δd produce un ritardo τ = Δd/c e nulli a f_k = (2k+1)/(2τ). Esempio: riflessione dal pavimento con Δd = 0.4 m ⇒ τ ≈ 1.16 ms ⇒ primo nullo a circa 430 Hz, poi ogni circa 860 Hz. È un comb poco profondo (−3..−10 dB) perché la riflessione è attenuata. Con due microfoni a distanze diverse si ottengono gli stessi nulli (regola 3:1). In una 4×12 le differenze di percorso fra i coni generano comb sopra circa 1 kHz se il microfono è distante.
   Fonte: https://www.soundonsound.com/techniques/how-record-guitar-cabs-one-mic (esempio 0.3 m / 0.7 m).
6. **Implementazione:** generare l'IR a 48/96 kHz come somma di biquad (HPF risonante, peaking, LPF del 4° ordine) più un ritardo di mic (d/c) e un'eco di pavimento (guadagno 0.2–0.4, τ da geometria). Convertire in FIR a fase minima da circa 200–500 ms (come i formati IR-2/IR-200) e applicarlo con convoluzione partizionata. In alternativa usare direttamente i biquad (costo basso).

**Nota sull'impedenza:** un IR cattura la risposta acustica ma non l'interazione fra finale e impedenza dello speaker (picco d'impedenza a Fs e salita induttiva). Per ampli a valvole senza retroazione (AC30) conviene aggiungere nel modello del finale un'enfasi a Fs e sopra circa 2 kHz.

---

## 7. Lacune e affidabilità
- Frequenza di campionamento di DD-2 e DD-3: non pubblicata (per il DD-3 c'è solo una stima).
- Gamme del TIME per RV-5 e RV-6 e range del pre-delay dell'RV-200: non pubblicati.
- Tempi assoluti del RE-201 (T1 min/max): nessun dato ufficiale; circa 600 ms massimi con H3 (valore empirico).
- Mappa dei modi 8–12 del RE-202 estratta dalle coordinate del PDF: affidabilità media.
- Mid cap del Deluxe AB763: 0.047 µF (standard) oppure 0.022 µF secondo alcune fonti.
- Modelli interni di VINTAGE DIGITAL e TAPE nel DD-500 (oltre a SDE-3000, SDE-2000 e RE-201): solo da fonti di rivenditori.
