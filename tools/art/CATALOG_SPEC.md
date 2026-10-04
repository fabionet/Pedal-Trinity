# Pedal Trinity – specifica del catalogo dei modelli

Ogni gruppo di pedali è un file `tools/art/catalog_<gruppo>.py` che definisce
`MODELS = [model(...), ...]` usando le funzioni di `catalog.py`.
Esempi completi e corretti: `tools/art/catalog_core.py` (ED-9, DS-1, MT-2W, GE-7).
Controllo: `python3 tools/art/catalog.py` deve terminare senza errori.

## Regole legali (obbligatorie)
* `name` e `code` sono **originali** (appaiono stampati sul pedale): mai "BOSS",
  "Roland", "Ibanez", "Tube Screamer", "Metal Zone", "Blues Driver", "Waza"
  né sigle BOSS (DS-1, SD-1…). Usa sigle nuove, max 8 caratteri, uniche, es. `DX-1`,
  `YD-1`, `CR-2`. Nomi max 22 caratteri, evocativi (es. "Orange Crunch").
* Il riferimento va solo in `inspired` (es. `"BOSS DS-1"`) e nel testo di `notes`.
* `notes`: 1–3 frasi in italiano: cosa fa il circuito, valori chiave, fonte (es. "service note", "clone Aion").

## Campi di `model()`
`model(id, code, name, inspired, category, family, colour, controls, config, notes, style="boss", text=None, accent=None, subtitle=None, stereo=False)`
* `id`: minuscolo, stabile, unico (es. `"ds1"`, `"ds1_4a"`, `"dd500"`).
* `category`: una di `CATEGORIES` in catalog.py.
* `family`: una di `FAMILIES` (vedi sotto).
* `colour`: RGB dell'enclosure come l'originale (dal campo color di boss_catalog.json).
* `accent`: colore del nome sul pedale (facoltativo); `subtitle`: tipo in maiuscolo (es. "DISTORTION").
* `stereo=True` se il pedale ha uscite stereo.

## Comandi (stesse etichette del pedale reale, max 10 caratteri)
* `knob(label, role, default=0.5, units="dial", lo=0, hi=10)` – pomello; `units` in
  `dial | db | hz | ms | percent | semitone | choice`; `lo/hi` = gamma fisica per il fumetto
  (per `hz`/`ms` la mappa è logaritmica lo..hi).
* `outer(...)` + `inner(...)` – pomello concentrico (outer seguito subito da inner).
* `selector(label, role, [scelte], default=indice, short=[sigle<=4 car.])` – pomello a scatti.
* `toggle(label, role, ("S","C"), default)` – levetta 2 posizioni.
* `slider(label, role, default=0.5, lo=-15, hi=15)` – cursore (solo EQ grafici, max 11, niente pomelli insieme).
* `button(label, role)` – pulsante (solo looper: ruoli `rec`, `stop`, `clear`, `undo`).
* Limiti grafici: max 8 posizioni rotative (un concentrico = 1 posizione), 1 levetta.
  Se il pedale reale ha più comandi, raggruppa i secondari in concentrici o selettori.
* Tutti i valori sono normalizzati 0..1; `default` = posizione di fabbrica/tipica.

## Famiglia `Circuit` (e `AmpSim`): netlist a stadi
`config` = stadi separati da `;`, argomenti `nome=valore` (niente spazi dentro un valore).
Valori: numeri con suffisso SI (`4.7k`, `47n`, `2.2u`, `100p`, `1M`, `220`) o espressioni
(somma `+` di prodotti `*`) con funzioni dei comandi (indice `i` = posizione nella lista controls):
* `pot(i,R,T)` resistenza del ramo del pot (curva `A` log audio, `B` lineare, `C` log inversa, `W`);
  `potr(i,R,T)` ramo complementare; `taper(i,T)` posizione 0..1 con curva;
  `lin(i,a,b)`, `log(i,a,b)`; `sw(i,v0,v1,...)` valore per posizione di selettore/levetta.
Il segnale è in **volt** (1.0 = 1 V; chitarra ~0.1–0.5 V). Tutto gira a 4x di sovracampionamento.

| stadio | argomenti | significato |
|---|---|---|
| `hpf` / `lpf` | `R C` | RC 1° ordine (fc = 1/2πRC) |
| `hpf2` / `lpf2` / `bpf` | `f Q` | 2° ordine |
| `peak` | `f Q g` | campana analogica (gyrator, Wien, notch con g<0) |
| `lshelf` / `hshelf` | `f Q g` | mensole |
| `shelf1` | `R Cs Cp` | R∥Cs in serie, Cp a massa |
| `opni` | `Rg Cg Rf Cf rail railn` | op-amp non invertente, H=1+Zf/Zg (Zg=Rg+1/sCg, Zf=Rf∥Cf), clip ai binari ±rail V |
| `opinv` | `Ri Ci Rf Cf rail` | op-amp invertente |
| `fbclip` | `Rg Cg Rf Cf d up dn rail` | op-amp non invertente con diodi in retroazione (TS/OD-1/SD-1) – risolto con Shockley+Newton; `up`/`dn` diodi in serie per verso (asimmetria 1:2 → up=1 dn=2) |
| `dclip` | `R C d up dn bias` | diodi verso massa dopo R, C in parallelo (DS-1, RAT…) |
| `series` | `RL d` | diodi antiparalleli in serie al segnale con carico RL (zona morta) |
| `bjt` | `g vp vn soft inv` | stadio a transistor: guadagno g, escursione +vp/-vn volt, ginocchio `soft` (2 = morbido, 6 = duro) |
| `triode` | `Rp Rk B byp` | triodo 12AX7 (Koren) a catodo comune, uscita in volt |
| `power` | `V g sag` | finale push-pull con sag |
| `tonebm` | `R1 C1 R2 C2 t k` | tono passivo LPF/HPF miscelato (Big Muff/DS-1) |
| `tonets` | `R7 C4 P t Rw Cw Rf Cf` | tono attivo TS808/SD-1 (nodale) |
| `fmv` | `R1 R2 R3 R4 C1 C2 C3 t m l` | stack Fender/Marshall/Vox (Yeh) |
| `rect` | `g mix vk` | raddrizzatore a doppia semionda (ottava tipo Superfuzz) |
| `tap` / `mix` | `wet dry` | salva il segnale / miscela con quello salvato (clean blend) |
| `gain` / `vol` | `db a` | guadagno fisso / volume (`a=taper(i,A)`) |
| `rail` | `rail` | limitazione ai binari |

Diodi `d=`: `si` (1N914/1S2473/1SS133), `si2` (1N4148), `ge` (1N34A/1S188), `led`, `sch` (Schottky), `mos`.
Regole pratiche: traduci il dossier stadio per stadio con i valori reali; i gyrator diventano `peak`
con f0/Q calcolati; il pot di livello finale è `vol a=taper(i,T)` (eventuale `*k` solo se il dossier
indica guadagno di uscita). Stadi digitali (MDP/COSM): approssima con gli stessi blocchi seguendo le specifiche.

## Chiavi `config` delle altre famiglie (e ruoli dei comandi)
* **Compressor** – ruoli `level sustain attack release tone threshold ratio enhance`;
  config `type=sustainer|limiter topo=ff|fb attack=lo,hi(ms) releaseRange=lo,hi release=ms maxgain=dB thr=lo,hi(dB) ratio=lo,hi knee=dB tone=Hz enhance=Hz`.
* **NoiseGate** – ruoli `threshold decay level mode`; config `thr=lo,hi decay=lo,hi(ms)`.
* **SlowGear** – ruoli `sens attack`; config `attack=lo,hi(ms)`.
* **Volume** – ruoli `volume min`.
* **GraphicEQ** – ruoli `b0..b10`, `level`; config `freqs=... qs=... qmin=0.9 range=15 lvl=15`.
* **ParametricEQ** – ruoli `g0.. f0.. q0..`, `level`; config `types=ls,pk,pk,hs f0=lo,hi f1=lo,hi ... q=... range=15`.
* **Wah** – ruoli `sens peak freq mode(0 up/1 down) filter(0 bp/1 lp) rate depth decay level`;
  config `type=auto|touch|lfo|pedal f=lo,hi q=lo,hi filt=bp|lp attack=ms release=ms rate=lo,hi`.
* **BBDChorus / BBDFlanger / AnalogDelay** (BBD a clock reale) – ruoli `rate depth level manual res time feedback rise mode tone`;
  config `type=chorus|dimension|vibrato|flanger|delay stages=N clk=lo,hi(Hz) rate=lo,hi(Hz) lfo=0 tri|1 sine aa=fc(Hz) comp=0|1 fbmax=0.95 stereo=0|1 wet=1 sat=1 rise=ms modes=rate,depth,rate,depth...(dimension)`.
  Ritardo = stages/(2·clk).
* **Phaser** – ruoli `rate depth res mode manual`; config `stages=4,8,12 f=lo,hi rate=lo,hi fbmax=0.7 lfo=0|1 depth=1 resfix=0`.
* **Tremolo** – ruoli `rate depth wave mode level`; config `type=tremolo|pan|slicer|rotary rate=lo,hi`.
* **DigitalDelay** – ruoli `level feedback time mode tone mod`; config
  `modes=Nome:minms:maxms:flag+flag|...` (flag: hold mod analog tape reverse shimmer warm pan dual lofi) `bw=Hz fbmax=1.0`.
* **TapeEcho** – ruoli `time feedback level reverb mode sat bass treble`; config
  `heads=1,2,3 maxms=600 minms=150 bw=5000 wow=0.002 modes=100|010|001|011|111|101r|...` (1 = testina attiva, r = molla).
* **Reverb** – ruoli `level time tone mode predelay`; config `modes=Nome:tipo|...` (tipi room hall plate spring modulate gate reverse shimmer dynamic delay lofi) `decay=lo,hi(s) predelay=ms bw=Hz`.
* **Pitch** – ruoli `oct1 oct2 direct up shift fine balance key harmony mode level`;
  config `type=analog|poly|shifter|harmonist range=12 lp=Hz gain=x`.
* **Synth** – ruoli `wave freq res decay sens level direct mode`; config `type=analog|digital f=lo,hi`.
* **Acoustic** – ruoli `top body level mode reverb`; config `modes=f:g:q|f:g:q;f:g:q|... top=Hz cut=Hz`.
* **CabIR** – ruoli `cab mic dist level mix`; config `cabs=nome:fres:q:lp:hp:bu:bug|...` (l'utente può caricare un .wav).
* **Router** – `type=ab2` ruoli `select direction`; `type=ls2` ruoli `mode levelA levelB select`.
* **Tuner** – ruoli `mode ref`. **Looper** – ruoli `level` + pulsanti `rec stop clear undo`; config `maxs=60`.

## Colori tipici
Usa il colore reale del pedale di riferimento: es. arancione (236,110,30), giallo (240,200,30),
azzurro BD (40,110,200), verde chiaro (140,200,80), rosa (230,110,160), viola (110,60,150),
nero/grigio scuro (30,30,33), argento (196,199,204), bianco (225,225,220), rosso (200,40,40), blu CE (40,120,200).

## Pedali a bilanciere e scatole di misura reale (stili `treadle` e `box`)
Wah, volume ed espressione usano `style="treadle"` e un dizionario `look` (vedi `shaped_layout.py`):
* il **primo comando** e' il bilanciere (`knob`, ruolo `freq` per i wah, `volume` per i volumi): nell'interfaccia
  e' il `TreadleControl` (strip 254, anchor = tallone, top = punta), trascinabile in verticale; l'interruttore in
  punta (on/off) e' la zona `foot`.
* `look=dict(boss=True)`: pedali BOSS a bilanciere, stessa forma e misure della replica (`real_specs.json`).
* altrimenti `look=dict(shape=..., dims=(larghezza, lunghezza, altezza) mm, place={etichetta: posizione}, leds=[...],
  finish=..., rocker_colour=..., tread=(motivo, colore1, colore2), body_motif=...)`; forme `crybaby`, `junior`, `mini`,
  `wide`, `dvx`, `dvp1`, `eb`, `morley`, `dearmond`. Posizioni: `("L"|"R", pct)` fiancata sinistra/destra vista dal
  chitarrista (0 = tallone, 100 = punta; il comando sta su un'aletta che sporge dal fianco, perno verticale),
  `("P", xf, pct)` pannello della carcassa larga, `("H", xf)` striscia del tallone; tipo facoltativo `knob`, `big`,
  `button` (pulsante/kickswitch: levetta con strip 255, stato mostrato da un anello luminoso), `lever`, `foot`.
* finiture: `crinkle paint gloss hammer chrome gold copper brushed raw smoked sparkle anodized rust`; motivi semplificati
  della gomma/carcassa: `ribs grip studs dots stripes camo rings frame diamonds twin` (mai artwork o loghi reali).
* `style="box"`: scatola di misura reale (es. wah fissi Q Zone) con `look=dict(dims=..., rows=[[...], ...])` come le
  repliche a scatola.
* `real=dict(code=..., name=...)`: replica REAL MOD di un pedale non BOSS (sigla e nome reali, senza il marchio del
  produttore, filtrato da `real_catalog.NO_BRAND`).
Wah a induttore (famiglia `Wah`, `type=inductor`): ruoli `freq q range voice fine level boost gain dist`
(vedi `catalog_wah.py` e il commento di `InductorWahEffect` in `FxFilters.cpp`).

## Pedali a scatola con la forma del pedale vero (MXR, Electro-Harmonix: `stomp_layout.py`)
`catalog_mxr.py` e `catalog_ehx.py` usano `stomp_catalog.stomp(...)` (stile `box` con `look(...)` e `real=(sigla, nome)`):
* `look(shape, dims, place, feet=, leds=, wedge=, extra=, finish=, gfx=, knob=, print=)`; forme `mxr`, `mxr_big`, `nano`,
  `pico`, `xo`, `xo_wide` (pressofusi), `pie` (lamiera piegata a cuneo, `wedge=(h davanti, h dietro)` mm), `slab`, `tank`
  (fondo pressofuso colorato e coperchio in lamiera), `tube` (serie a valvole, `extra=[("tubes", x%, y%, n)]`).
* `place=[(etichetta, x%, y%, tipo[, alias]), ...]` posizioni del pedale vero (x 0 = sinistra, y 0 = bordo lontano), tipi
  `knob big med small encoder lever button slider`; `feet=[(etichetta, x%, y%[, alias])]` (il primo e' l'effetto),
  `leds=[(x%, y%, colore)]` (il primo e' quello di stato). I comandi del modello si assegnano per etichetta/alias, poi per
  tipo; le posizioni senza comando diventano comandi fissi decorativi; le sovrapposizioni si risolvono spostando i comandi.
* `gfx`: fondo, fasce/riquadri/diagonali/cerchi/raggiere (solo geometrie semplici: mai loghi, artwork, ritratti o firme),
  posizione e carattere di nome, sigla e sottotitolo (`stomp_textures.py`); `print` = nome stampato sulla replica REAL MOD.
* Parte funzionale: `sound_stage3_analog.json` / `sound_stage3_digital.json` (tappa 3A) e `sound_stage3b_analog.json` /
  `sound_stage3b_digital.json` (tappa 3B) per id (etichette oltre 10 caratteri abbreviate); i comandi scritti nel catalogo
  sono il segnaposto usato solo se l'id manca nei JSON.
* Tappa 3B (`catalog_mxr_b.py`, `catalog_ehx_b.py`, priorita' B): `gfx["name_deco"] = dict(style=band|box|frame|lines,
  col=, pad=mm)` disegna la decorazione attorno al blocco del nome dove `stomp_textures` lo piazza davvero (mai sopra
  scritte o comandi: la fascia diventa riquadro, il margine si riduce). Il nome sta nella fascia libera piu' ampia tra i
  comandi e il footswitch; un LED stimato che la chiude va accanto al footswitch dell'effetto.
* Pedali a bilanciere della serie (wah, volume, pan, pitch a pedale, anche la serie 'Next Step' che oscilla tutta intera):
  `stomp_catalog.rocker(..., treadle_look(shape, dims, leds=, finish=, rocker=, tread=), segnaposto, note,
  place_spec=[(etichetta, "L"|"R", pct dal tallone, tipo), ...])` (stile `treadle`, forme di `shaped_layout.py`): il
  bilanciere e' sempre il primo comando (dal JSON per etichetta/ruolo da bilanciere, altrimenti quello del segnaposto) e i
  comandi del JSON prendono le posizioni di `place_spec` per somiglianza dell'etichetta.
