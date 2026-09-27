# BOSS-inspired distortion / fuzz / metal: circuit dossier

Scope: circuit-level data for emulating BOSS compact distortion, fuzz and metal pedals. The machine-readable version is `circuits_distortion.json`, with the same pedals, values and sources.

## How the data was gathered and what it is worth

- **Primary sources.** Most values were read directly from original BOSS service-note schematics: DS-1 (1994 ed.), HM-2 (1983), HM-3 (1993), DF-2 (1984), MZ-2 (1988), all from archive.org. The MT-2 factory schematic (Apr 1991) came from Electric Druid, and the DS-2 factory schematic from Schematic Heaven.
- **Traces.** Some pedals only have community or clone traces: FZ-2 (Aion Hypercube), FZ-3 (Aion Argent), XT-2 (Alex Taber), MD-2, PW-2 and OS-2 (Schematic Heaven redraws), and DS-1W (freestompboxes, text only).
- **Our calculations.** Derived numbers are our own unless a source is quoted. They come from ideal formulas or small nodal / hybrid-π simulations (`gyrator f0 = 1/(2π√(Rs·Rb·Cg·Cs))`, `Q = √(L/Cs)/Rs` with `L = Rs·Rb·Cg`, unloaded). They are marked *est* where approximate.
- **Limits of this research.** ElectroSmash's DNS was down, so we used its archive mirror (electrosmash.mas-effects.com). freestompboxes, diystompboxes and hobby-hour sit behind Cloudflare; we reached them only through archive.org. The web-search quota ran out before we could look for MT-2W, HM-2W and FZ-1W teardowns.
- **Diode models.** "1N4148-class" means 1N4148, 1SS133 or 1S2473. Typical SPICE values: Is = 2.52 nA, N = 1.752, Rs = 0.568 Ω, Vf ≈ 0.6–0.7 V at 1 mA. The Ge diodes (1S188FM) have Vf ≈ 0.2–0.3 V; no published model exists, so any parameters for them are estimates.

---

## DS-1 Distortion (1978–, analog) — confidence HIGH
**Supply.** 9 V, with a 4.5 V bias from an R24/R25 10k divider. The DS-1-4A and DS-1-B50A use the same circuit.

**Signal path, stage by stage:**
1. **Input buffer.** Q1 2SC2240 emitter follower. R1 1k, C1 47n, R2 470k to bias, R3 10k emitter. Zin ≈ 470k; HPF ≈ 7.2 Hz.
2. **JFET bypass switch.** Q6.
3. **Transistor booster.** Q2 2SC2240, common emitter with collector-to-base shunt feedback.
   - Parts: C3 47n (series input), R6 100k base to ground, R7 470k ∥ C4 250p (feedback), R8 10k collector, R9 22 Ω emitter, C5 0.47µ out.
   - Our simulation (β 250–450, Ic ≈ 0.47 mA, Vc ≈ 4.1–4.4 V): 21 dB at 100 Hz, 35.6 dB at 1 kHz, flat 36.5–37 dB above about 2 kHz. The gain rises 6 dB/oct because of C3 in series with the virtual-ground base, until it hits the open-loop limit.
   - ElectroSmash quotes 35 dB (×56).
   - The output swings up to about 9 Vpp, so the stage soft-clips asymmetrically. The ElectroSmash mod (R9 → 1k) brings it down to about 17 dB.
4. **Op-amp gain stage.** Non-inverting.
   - Gain = 1 + VR1(100kB)/R13 4.7k, so ×1 to ×22.3 (0–26.9 dB).
   - C8 in series with R13 sets a gain-dependent HPF: C8 = 1 µF (1994 service notes) gives 34 Hz; C8 = 0.47 µF (later units) gives 72 Hz, falling to about 3 Hz at full gain. A 100p feedback cap is a low-pass.
   - Op-amp history: TA7136AP (1978) → BA728N (1994) → M5223AL (2000, needs R40 1k pull-up) → NJM2904L / NJM3404A (2006).
   - Yeh (DAFx-07): rail clipping at about ±4.5 V is the main nonlinearity, so model slew and recovery.
5. **Diode clipper.** R14 2.2k, C9 0.47µ NP, then 1N4148 pair (D4/D5) antiparallel to AC ground, with C10 10n across the diodes. LPF 7.23 kHz while the diodes are off; hard clip at ±0.65 V. The ODE is `C·dV/dt = (Vin−V)/R − 2·Is·sinh(V/Vt)`.
6. **Tone (Big Muff style).**
   - LPF: R16 6.8k series, C12 0.1µ to ground → 234 Hz.
   - HPF: C11 22n, then R15 2.2k in series, then R17 6.8k to ground → 804 Hz with R15 included (ElectroSmash ignores R15 and gets 1064 Hz).
   - The two are blended by VR3 20kB.
   - Our nodal calculation, tone at centre, 100k level load: 100 Hz −7.3 dB, 500 Hz −13.7 dB, 700 Hz −14.5 dB (the notch), 1 kHz −13.5 dB, 3 kHz −10.2 dB. ElectroSmash says a notch near 500 Hz, about −20 dB.
7. **Output.** Level VR2 100kB, Q7 JFET switch, Q3 emitter follower (R19/R20 1M, R21 10k), R22 1k, C14 1µ, R23 100k.

**Sources:**
- https://electrosmash.mas-effects.com/boss-ds1-analysis.html · https://www.electrosmash.com/boss-ds1-analysis
- https://archive.org/details/boss_DS-1_SERVICE_NOTES
- https://ccrma.stanford.edu/~dtyeh/papers/yeh07_dafx_distortion.pdf · https://ccrma.stanford.edu/~dtyeh/papers/yeh07_dafx_clipode.pdf
- https://napulen.github.io/reports/mcgill/mumt618/

## DS-1W Waza Craft (2022–, analog) — MEDIUM
From BOSS's product page and the freestompboxes trace (text only; values are in the trace image, which we did not transcribe).

**Differences from the DS-1:**
- **Discrete op-amp.** N-JFET differential pair with CCS tail, current-mirror load, CCS-loaded PNP voltage-gain stage, CCS-loaded NPN emitter-follower output. CCS loads also appear on the input buffer.
- **Supply.** Regulated 8.45 V (9.1 V zener), plus 5 V logic (Schmitt trigger + D flip-flop bypass).

**Custom mode changes:**
- R43 + C30 switched across the top of the feedback divider.
- R36 + C26 switched across the grounded leg.
- Two diode pairs in series plus a series resistor, giving about 1.3 V clipping.
- Larger tone HPF cap (lower corner) and smaller LPF cap (higher corner), so less mid scoop.
- Different volume taper.
- BOSS states +6 dB level, thicker mids and more input sensitivity.

**Specs.** 15 mA.

**Sources.** https://www.boss.info/global/products/ds-1w/ · https://www.freestompboxes.org/viewtopic.php?t=32664

## DS-1X (2014–, DIGITAL, MDP) — MEDIUM
- **Processing.** DSP that analyses frequency, overtones, dynamics, pickup, register, single notes vs chords, and bends/vibrato. Distortion adapts to register (tight lows, fat highs).
- **Controls.** DIST, LOW, HIGH, LEVEL; each moves several internal parameters.
- **Specs.** 60 mA; Zin 1M, Zout 1k. AD/DA figures are not published.
- **Emulation idea.** Multiband waveshaper whose drive follows register and envelope.

**Source.** https://www.boss.info/global/products/ds-1x/

## DS-2 Turbo Distortion (1987–, analog) — MEDIUM
Factory schematic, low resolution.

**Supply.** 9 V regulated to 8 V by Q7 with a zener; 4 V bias.

**Signal path:**
1. Input buffer Q6 2SK184 JFET source follower.
2. C38 1µ, then R62 4.7k, then D14/D15 1SS188FM antiparallel to ground. These are low-Vf diodes, about 0.3 V, so they act as an input limiter.
3. **Q22 2SC3378 common-emitter pre-gain.**
   - Parts: R65 2.2k collector, R66 1.5k emitter, R67 220 + C42 10µ bypass.
   - Gain is about ×9 above ~60 Hz and ×1.5 below (est).
4. **Turbo mode filters,** selected by JFETs Q14 and Q15:
   - **Mode I:** C39 47n, a direct path.
   - **Mode II:** C41 2.2n + R64 10k (a treble path), plus a Q23 Sallen-Key low-pass: R69 22k, R70 22k, C43 47n feedback, C44 1n to ground. That gives **f0 ≈ 1.06 kHz, Q ≈ 3.4**, a resonant mid hump, which is where the "nasal" Turbo II voice comes from.
5. **Discrete op-amp gain stage.**
   - Parts: Q16/Q19 2SK184 differential pair, Q17 2SA1335, Q18 emitter follower. R56 1M ∥ C32 100p feedback; R54 1k + C33 0.22µ ground leg. R55 1k to DIST VR1 250kD. D8/D9 + C34 2.2n in the feedback branch, switched by Q20.
   - Gain is up to about 60 dB (est), but only between about 723 Hz (C33·R54) and 1.6 kHz (C32·R56), so the stage is strongly mid-focused.
   - **Uncertain:** the exact pot wiring and what Q20 does in each mode.
6. C35 10µ, R57 4.7k, then 1SS133 pair (D11/D12) to ground: hard clip.
7. **Tone.** Active tone around Q13: VR2 100kG, C37 6.8n, R45 4.7k, R46 33k, R58 15k, R59 1k. Tilts from dark to bright.
8. **Post-filter.** Q12 two-pole low-pass (R44 22k, R33 22k, C28 4.7n) at about 3.3 kHz (est), with Q10 JFET switch.
9. **Output.** Level VR3 50kA, emitter followers Q5/Q11.

**Sources.**
- https://schematicheaven.net/effects/boss_ds2_turbodist.pdf
- https://www.hobby-hour.com/electronics/s/boss-ds2-turbo-distortion.php
- https://www.guitarscanada.com/threads/boss-ds-2-turbo-distortion-mods.43397/

## MT-2 Metal Zone (1991–, analog) — HIGH
Factory schematic.

**Parts and supply.**
- Op-amps: M5218AL ×4 (IC1–IC4).
- Transistors: 2SC3378GR. JFETs: 2SK184 / 2SK118.
- Diodes: 1SS133.
- Supply: 9 V, 4.5 V bias.

**Signal path:**
1. **Input.** Q011 JFET source follower, Zin = 1M.
2. **IC3b pre-EQ gain stage.**
   - C033 15n / R043 100k at the input → HPF 106 Hz.
   - R044 220k ∥ C032 100p feedback → LPF 7.2 kHz.
   - A gyrator (Q010) at the inverting input: R046 2.2k, C034 27n (series), C035 10n, R053 47k. **f0 ≈ 952 Hz, Q ≈ 2.8.**
   - Peak gain 1 + 220k/2.2k ≈ 40 dB ideal; Electric Druid measures about 36 dB at 1 kHz.
3. **Divider / low-pass.** R045 10k and R042 10k with C031 47n: −6 dB, fc 677 Hz from the Thevenin resistance. Electric Druid quotes about 340 Hz.
4. **IC3a gain stage.**
   - C029 33n with R040 100k → 48 Hz HPF.
   - Gain = 1 + (VR01 250kA + R051 1k)/R041 1k, with C030 10µ: **×2 to ×252 (48 dB)**.
   - C028 47p in feedback; the op-amp also clips on its rails (about 8 Vpp).
5. **Clipper.** C027, then R033 2.2k, then D003/D004 1SS133 to ground. After that, a shelf: R032 10k, then R031 4.7k + C023 15n to ground. Pole 722 Hz, zero 2.26 kHz, −9.9 dB at HF.
6. **IC4b post-distortion gyrators.**
   - Gain = 1 + R030 3.3k/Zgyr.
   - Gyrator Q008: R034 1k, C024 15n, C025 1.5n, R036 47k. **4.89 kHz, Q 2.2, +12.7 dB.**
   - Gyrator Q007: R027 470, C020 0.22µ, C017 47n, R025 470k. **105 Hz, Q 14.6, +18 dB.**
   - Together these make the scooped double peak.
7. **IC4a inverting buffer.** R028 = R026 = 22k, gain −1.
8. **HIGH and LOW.**
   - HIGH: shelf in IC1a (R015 22k, C010 10p), VR03b 100kG + R061 2.2k + C044 10n; corner about 7.2 kHz.
   - LOW: gyrator IC1b (R012 2.2k, C008 0.22µ, C009 47n, R013 100k). **105 Hz, Q 3.1**, about ±15–20 dB.
9. **MIDDLE.**
   - IC2a boost/cut: R038 = R035 = 47k, R050 = R049 = 330, VR02b 100kG, ±15 dB.
   - Resonator: a Wien bridge (C036 22n, C043 8.2n, R048 = R062 = 2.2k, VR02a 50k×2, IC2b follower), f0 = 1/(2π√(RaRbCaCb)).
   - Our range: **227 Hz – 5.39 kHz**. Electric Druid: 240 Hz – 4.7 kHz (boost) and 6.3 kHz (cut). Q stays roughly constant across the sweep.
10. **Output.** LEVEL VR04 50kA, Q004 emitter follower, Q003 JFET switch, Q001 emitter follower.

The MT-2-3A (2021) uses the same circuit.

**Sources.**
- https://electricdruid.net/wp-content/uploads/2016/01/Boss-MT-2-Metal-Zone-Schematic.pdf
- https://electricdruid.net/boss-mt-2-metal-zone-pedal-analysis/
- https://schematicheaven.net/effects/boss_mt2_metalzone_eqdist.pdf

## MT-2W Waza Craft (2018–, analog) — LOW
- **Design (BOSS).** Discrete dual-stage gain; 3-band EQ with Q values tuned per mode.
- **Standard mode.** The MT-2 voice with less noise and a tighter attack.
- **Custom mode.** Wider range, rounder lower mids, tight lows, cleans up with the guitar's volume.
- **Specs.** 35 mA.
- **Gap.** No public trace; the component values that change are unknown.

**Source.** https://www.boss.info/global/products/mt-2w/

## HM-2 Heavy Metal (1983–1991, analog) — HIGH
Service notes, Dec 1983.

**Parts and supply.**
- Op-amps: IC1–IC3 M5218L / NJM4558S.
- JFETs: Q1, Q4, Q5, Q10 2SK30A-Y.
- Transistors: Q6 2SC2240GR, Q7 2SA970GR.
- Diodes: 1S2473 (Si), D6/D7 1S188FM (**Ge**).
- Supply: 9 V drawn in the schematic. Users report a fuller sound at 12 V (the adaptor was unregulated).

**Signal path:**
1. **Input.** Q1 JFET source follower (R1 10k, C1 47n, R8 1M).
2. **Q6 common-emitter shunt-feedback stage.**
   - Parts: C6 47n, R19 22k series, R21 100k, R16 470k ∥ C10 100p, R17 10k, R18 22 Ω.
   - Our simulation: about 22–23 dB from 500 Hz to 2 kHz; LPF 3.39 kHz.
3. **Q7 PNP shunt-feedback stage.** R22 22k, R29 470k ∥ C14 100p, R28 120, R27 10k. About 21 dB (est), with a second 3.4 kHz low-pass.
4. **DIST VR4 250kD.** A dual-function pot with its wiper to ground:
   - Pin 1 shunts Q6's collector through R49 150 Ω + C31 10µ. Near minimum, this collapses Q6's gain.
   - Pin 3 is IC1b's ground leg (R42 47k + C22 47n → 72 Hz HPF).
   - IC1b gain: 1 + 220k/(47k + 250k) = 1.74 at minimum, 1 + 220k/47k = 5.68 at maximum.
5. **IC1b feedback clipping.** R20 220k ∥ C9 100p (7.2 kHz), with D3 (one diode) in one direction and D4 + D5 (two in series) in the other. Asymmetric: 0.65 V one way, 1.3 V the other.
6. **Series Ge pair.** C12 1µ, R23 10k, then **D6/D7 Ge antiparallel in series with the signal**. This creates a dead zone of about 0.25 V (gating-like grit).
7. **Shunt clipper.** R30 10k, then D8/D9 Si to ground, with C16 1n (15.9 kHz).
8. **Buffer.** C15 1µ with R24 68k, IC1a buffer.
9. **COLOR MIX.**
   - IC3a: R54 3.3k in, R52 3.3k feedback, C33 470p. Boss GE-style boost/cut, up to about ±20 dB ideal.
   - **LOW** (VR2 10kG) drives gyrator IC3b: C35 1.5µ, C30 68n, R47 330, R51 100k. **86.7 Hz, Q 3.7.**
   - **HIGH** (VR3 10kG) drives two gyrators:
     - IC2a: C28 0.15µ, C27 6.8n, R41 330, R43 82k. **958 Hz, Q 3.4.**
     - IC2b: C29 0.1µ, C26 4.7n, R45 330, R44 100k. **1.28 kHz, Q 3.8.**
   - This matches David Ross's measurements: lows about 80 Hz, mids 0.9–1.3 kHz. With HIGH at maximum it rings near 1 kHz.
10. **Output.** LEVEL VR1 10kA, JFET switch, Q3 emitter follower.

**Sources.**
- https://archive.org/details/lost_manuals_Boss--HM-2--service--ID10976
- https://schematicheaven.net/effects/boss_hm2_heavymetal_dist.pdf
- https://www.hobby-hour.com/electronics/s/hm2-heavy-metal.php
- https://davidrossmusicalinstruments.com/boss-hm-2-circuit-analysis/

## HM-2W Waza Craft (2021–, analog) — MEDIUM
- **Standard mode.** BOSS says it reproduces the HM-2 "100%", with lower noise and a maximum level 3 dB higher. The HM-2 model can be used for it.
- **Custom mode.** More gain and more fundamental; the lows and high-mids are adjusted. Values unknown.
- **Specs.** 30 mA.

**Source.** https://www.boss.info/global/products/hm-2w/

## HM-3 Hyper Metal (1993–, analog) — HIGH
Service notes, May 1993.

**Parts and supply.** Op-amps: 2× M5218AL. Supply regulated to 8 V (Q6 2SC2458 pass transistor); 4 V bias.

**Signal path:**
1. **Input.** Q1 2SK184 source follower, then Q2 switch.
2. **Pre-filter.** R6 4.7k + C3 6.8n → LPF 4.98 kHz. C4 47n + R7 47k → HPF 72 Hz.
3. **IC1a gain.** Gain = 1 + (R9 220 + VR4 50kA)/R8 220, with C5 10µ: **×2 to ×229 (47 dB)**. C6 47p across the feedback.
4. **Low-pass.** R10 10k + C7 10n → **1.59 kHz**.
5. **IC1b.** Gain 1 + R12 220k/R11 22k = ×11 (20.8 dB). HPF 154 Hz (C8 47n), LPF 7.2 kHz (C9 100p). Feedback clipping is asymmetric: D2 + D3 in series one way, D4 alone the other (1SS133).
6. **Shunt clipper.** C10 1µ, R13 2.2k, D5/D6 to ground.
7. **Shelving network.** R14 10k ∥ C11 10n (corner 1.59 kHz), then R15 5.6k + C12 47n to ground (about 605 Hz).
8. **Buffer.** IC2a.
9. **EQ.** IC2b (R17 = R18 = 3.3k, C14 10n → 4.8 kHz LPF) with transistor gyrators:
   - LOW (VR2 10kW): Q11 — C33 1.5µ, C34 68n, R47 330, R48 100k. **86.7 Hz, Q 3.7.**
   - HIGH (VR3 10kW) feeds two gyrators:
     - Q9: C29 39n, C30 8.2n, R42 1k, R43 100k. **890 Hz, Q 4.6.**
     - Q10: C31 22n, C32 1.2n, R44 330, R45 100k. **5.39 kHz, Q 4.1.**
10. **Output.** Level VR1 50kA.

**Sources.**
- https://archive.org/details/lost_manuals_Boss--HM-3--service--ID10967
- https://schematicheaven.net/effects/boss_hm3_hypermetal_dist.pdf

## ML-2 Metal Core (2007–) — LOW
- **What we found.** No schematic, trace or teardown. Controls: DIST, LOW ("7-string" lows), HIGH, LEVEL.
- **Current draw.** 60 mA — the same as the COSM FZ-5 and about four times a typical analog BOSS distortion. That hints at DSP content, but this is **not verified**.
- **Emulation.** Would have to be black-box, from audio measurements.

**Source.** https://www.boss.info/global/products/ml-2/

## MD-2 Mega Distortion (2001–, analog) — MEDIUM
Community trace; no diodes anywhere in the audio path (confirmed by mirosol).

**Signal path:**
1. **Input.** 2SK184 source follower, then 2SK118 switch.
2. **Discrete JFET op-amp.**
   - Parts: 2× 2SK184 differential pair, 2SA1048 output. Cin 39n / 100k → HPF 41 Hz. Feedback 1M ∥ 1n5 (the 1n5 rolls gain off above about 106 Hz). Ground leg 15k to 2k2 + 22µ.
   - **GAIN BOOST** is a 100k rheostat with 22k + 1µ back from the output, forming a T-network. The gain range needs simulation (below 100 Hz, about ×59 est).
3. **TL072 non-inverting.** 1 + (1k + DIST 100k)/1k = ×2 to ×102, with 470p (about 3.4 kHz at maximum).
4. **TL072 inverting.** ×−2.2.
5. **Complementary push-pull clipper.** 2SA1048 / 2SC2458, each biased 56k/56k, Rc 3.9k, Re 5.6k ∥ (100 Ω + 47µ). AC gain about ×30–40 per half. Each transistor clips one half-wave, which produces the asymmetric "saw" character.
6. **Emitter follower + shelf.** 2SC2458 emitter follower, then 22k / 10k divider with 10k + 27n shelf (pole ≈ 349 Hz, zero 589 Hz).
7. **TL072 tone stage.** 56k ∥ 220p feedback. TONE: 100k + 10k + 2n2 (7.2 kHz). **BOTTOM:** 100k + 47k with an op-amp gyrator (3.3k, 100n, 56n, 100k) → **117 Hz, Q 4.1**.
8. **Output.** Level 50k.

**Sources.**
- https://schematicheaven.net/effects/boss_md2_megadist.pdf
- https://mirosol.kapsi.fi/2014/01/boss-md-2-mega-distortion/

## XT-2 Xtortion (1996–, analog) — MEDIUM
Trace by Alex Taber. Supply 8 V, bias 4 V.

**Signal path:**
1. **Input.** JFET buffer and switch.
2. **PUNCH (10kC pot).** Boost/cut around IC1.2 (R20 = R22 = 18k, C12 470p, R24 33k) with three gyrators:
   - (a) C23 56n, C24 3.3n, R34 470, R37 ≈ 820k (the value is missing on the trace) → about 596 Hz, Q ≈ 10 — **uncertain**.
   - (b) C14 5.6n, C22 330p, R33 3.3k, R35 470k → **2.97 kHz, Q 2.9**.
   - (c) Q8 transistor gyrator: C27 22n, C28 1.2n, R39 2.7k, R38 330k → **1.04 kHz, Q 2.6**.
   - Fully counter-clockwise gives a slight treble boost; fully clockwise gives a strong mid boost (Thermionic Studios).
3. **IC1.1 gain.** 1 + (R23 100 + VR1 50kA)/R19 270, with C11 4.7µ (125 Hz): **×1.37 to ×187 (45 dB)**.
4. **Clipper.** C15 1µ, R30 2k, Si pair to ground, C21 33n → LPF 2.4 kHz.
5. **IC3 post-EQ.** R40 = R32 = 7.9k, C26 47p. Fixed gyrators:
   - C29 3.9n, C30 220p, R43 3.9k, R41 470k → **4.0 kHz, Q 2.6**.
   - Q11: C38 0.68µ, C37 68n, R53 3.9k, R42 100k → **37.5 Hz, Q 1.6**.
6. **CONTOUR.** C18 0.1µ, R26 33k, R28 47k, C20 1n, C19 22n, VR3 100k, R25 1k, C16 0.1µ, C17 10n. A variable mid scoop or hump centred at about 900 Hz (reported).
7. **Output.** LEVEL VR4 100k.

**Sources.**
- https://schematicheaven.net/effects/boss_xt2_xtortion_dist.pdf
- https://thermionic-studios.com/wiki/index.php?title=XT-2

## DF-2 Super Distortion & Feedbacker (1984–, hybrid) — HIGH
Service notes, Nov 1984.

**Audio path (essentially a DS-1):**
1. **Input.** Q1 2SK30A source follower.
2. **Q2 booster.** 2SC732: R7 470k ∥ C2 47p, R10 10k, R14 22, R6 100k, C3 47n, R3 10k series. Our simulation: about 28–29 dB. R4 150k injects the feedback tone here.
3. **IC1a gain (M5218L).** 1 + (R18 1k + VR2 250kA)/R17 4.7k, with C6 1µ: **×1.2 to ×54 (34.7 dB)**.
4. **Clipper.** C10 0.47µ, R22 2.2k, D2/D3 1SS133 to ground, C11 10n → 7.2 kHz.
5. **Tone.** Identical to the DS-1: R21 6.8k / C12 0.1µ (234 Hz) and C13 22n + R23 2.2k + R24 6.8k (804 Hz), VR3 20kB.
6. **Output.** Q6 recovery amplifier (R20 470k, R26 10k, R19 4.7k, later 10k). LEVEL 10kB (20kB from SN 513600). Q7 emitter follower.

**Feedbacker (switch held):**
- **Parts.** Fundamental detector (IC2/IC3 IR9022 / TL022, Schmitt stages), PLL (IC7 HD14046 = CD4046) with D flip-flop IC5 HD14013 (divide-by-2 / octave), LFO IC2a, low-pass and envelope generator (Q8–Q12), analog switches IC8 HD14066.
- **Operation.** The synthesized fundamental / overtone is injected into the distortion input. OVER TONE (10kG) sets how much of it you get.
- **Emulation.** Pitch tracker, then oscillator (fundamental + octave), then LPF and envelope, fed into the DS-1 model.

**Sources.**
- https://archive.org/details/boss_DF-2_SERVICE_NOTES
- https://schematicheaven.net/effects/boss_df2_distortion_feedbacker.pdf

## FZ-2 Hyper Fuzz (1993–1997, analog) — HIGH
Aion FX Hypercube: an exact replica of the effect path.

**Signal path:**
1. **Input.** Q1 2SK184 source follower.
2. **Discrete op-amp boost.**
   - Parts: Q2/Q3 2SK184, Q4 2SA1335. Gain = 1 + (R8 2.2k + GAIN 50kA)/R7 1.5k, with C3 2.2µ: **×2.47 to ×35.8 (31 dB)**, HPF 48 Hz. C4 47p feedback.
   - BOOST mode takes its output from here.
3. **Low-pass and splitter.** R10 10k + C5 33n → **482 Hz**. Then Q5 phase splitter (R13 4.7k collector, R14 10k emitter: −0.47 / +1).
4. **Super-Fuzz octave core.**
   - Q6/Q7 long-tailed pair: R15/R18 1k, C7/C8 1µ, bias 100k/27k, common collector R22 10k, common emitter 1.8k + 47µ.
   - Full-wave rectification produces the octave-up. Aion measured an original's transistors at hFE 231 and 289, so they are **not matched**.
   - C10 1µ, then 1N914 pair (D2/D3) to ground.
5. **FUZZ I.** R29 10k / C16 15n (1.06 kHz), then IC1B inverting at ×−2.7. Flat voice.
6. **FUZZ II.** Two parallel paths (R24 47k / C12 47n / R25 10k / C13 4.7n, and C11 1n + R23 10k) into IC1A at ×−12 (R27 120k / R26 10k).
   - Our calculation: +4.6 dB at 50 Hz, −8.9 dB at 700 Hz, **−15 dB at 1 kHz** (a scoop about 19 dB deep), +1.9 dB at 5 kHz.
7. **Tone (IC2A).**
   - TREBLE 50kB with C19 15n + R35 3.3k → shelf at **3.2 kHz**.
   - BASS 50kB with gyrator IC2B (C20 150n, C21 47n, R36 3.3k, R37 100k) → **104 Hz, Q 3.1**.
8. **Output.** LEVEL 50kA, Q8 emitter follower. The original disables LEVEL in BOOST mode.

**Sources.**
- https://aionfx.com/project/hypercube-fuzz-distortion/
- https://aionfx.com/app/files/docs/hypercube_documentation.pdf
- https://www.coda-effects.com/2015/08/boss-hyper-fuzz-boss-fz2-wall-of-fuzz.html

## FZ-3 Fuzz (1997–, analog) — HIGH
Aion FX Argent, based on a 2021 trace. Aion says earlier traces had the wrong values.

**Transistors.** 2SK184GR (input), 7× 2SC2458.

**Signal path:**
1. **Input.** Q1 source follower.
2. **Pre-gain.** C2 + C3 (1µ + 330n in series = 248n) → Q2 common emitter (R8 33k / R7 10k, about ×−3.3; C4 10n + R9 2.2k HF shunt) → Q3 emitter follower.
3. **Fuzz-Face-like pair.** R11 6.8k, C5 1µ, Q4/Q5, R12 100k ∥ C6 1n feedback, R13 22k, R14 + R15 3.3k. **FUZZ 1kC** + C7 10µ on Q5's emitter.
4. **Recovery.** C8 18n into Q6 (×−1), with C9 8.2n → LPF 1.94 kHz.
5. **Tone.** Big Muff style: R21 47k / C12 10n and C11 10n / R20 47k, both 339 Hz, R22 47k, TONE 100kW. The centre position is roughly flat.
6. **Output.** VOLUME 100kA, Q7 emitter follower.

There are no clipping diodes.

**Sources.**
- https://aionfx.com/project/argent-silicon-fuzz/
- https://aionfx.com/app/files/docs/argent_documentation.pdf

## FZ-5 (2007–, DIGITAL COSM) — MEDIUM
- **Models.** Maestro FZ-1A, Fuzz Face and Octavia (per BOSS). The mode letters are S / F / O; the mapping is medium confidence.
- **Controls.** FUZZ, MODE, LEVEL.
- **Specs.** 60 mA.

**Source.** https://www.boss.info/global/products/fz-5/

## FZ-1W Waza Craft (2021–, analog) — LOW
- **Design.** Silicon transistors, all analog, temperature-stable.
- **Input impedance.** **22 kΩ when on** — keep this in the model (it interacts with the guitar's pickups and volume knob the way a Fuzz Face does).
- **Modes.** Vintage: raspy, cleans up with the guitar's volume. Modern: wider gain, focused mids, TONE changes only brightness.
- **Specs.** 16 mA.
- **Gap.** No trace found.

**Source.** https://www.boss.info/global/products/fz-1w/

## DA-2 Adaptive Distortion (2013–, DIGITAL MDP) — MEDIUM
- **Processing.** Several distortion processors adapting in real time across the spectrum and register.
- **Controls.** A-DIST, LOW, HIGH, LEVEL.
- **Specs.** 45 mA.
- **Emulation idea.** Band split, envelope-driven waveshapers, then recombine.

**Source.** https://www.boss.info/global/products/da-2/

## ST-2 Power Stack (2010–) — LOW
- **What we found.** No schematic or trace. Controls: SOUND (gain and character together), BASS, TREBLE, LEVEL.
- **Current draw.** 65 mA, which suggests DSP — **unverified**.
- **Emulation.** Would have to be a generic tube-stack preamp model.

**Source.** https://www.boss.info/global/products/st-2/

## MZ-2 Digital Metalizer (1987–, hybrid) — MEDIUM
Service notes, Jan 1988.

**Digital section.** 12-bit linear, 70 kHz sampling. MN5010RBA controller, 4416 DRAM, and an A/D built from an R-ladder (RKM14L472) plus an NJM311 comparator. Draws 70 mA.

**Modes.** SGL, DOUB I–III (doubling), CH I–II (chorus); stereo outputs A/B.

**Analog distortion:**
1. **Stage 1.** Discrete JFET op-amp (Q2/Q3 2SK117 + Q13 2SA970) on a **5 V rail**. C3 3.9n / R5 100k → 408 Hz pre-emphasis. Gain 1 + 270k/4.7k = ×58 (35 dB), C4 0.1µ → 339 Hz, C5 100p → 5.9 kHz.
2. **Filter.** Q4 Sallen-Key low-pass (R13 = R14 = 33k, C7 22n, C8 1n) → **1.03 kHz, Q 2.35**, in parallel with a Q15 high-pass path.
3. **Stage 2.** Discrete op-amp (Q5/Q6 + Q14) with DRIVE 250kA.
4. **Post.** R22 47k / C12 10n, Q16 emitter follower.

Clipping comes from the rails; we saw no diodes in the audio path.

**Source.** https://archive.org/details/boss_MZ-2_SERVICE_NOTES

## PW-2 Power Driver (1996–, analog) — MEDIUM
Community trace. Supply rails: 8 V, 4.5 V and 2.2 V.

**Signal path:**
1. **Input.** JFET buffer.
2. **Pre-gain.** 2SC2458 common emitter: 1.8k collector, 1.8k emitter ∥ (33 Ω + 100µ). About ×20 or more (est).
3. **Discrete JFET op-amp.** 2SK184 pair + 2SA1048, **on a 4.5 V supply**. DRIVE 50kA: 1 + (VR + 100)/470 = ×1.2 to ×108. It clips on the rails; there are no diodes.
4. **Inverting stage.** M5218 at ×−3.2.
5. **FAT / MUSCLE (100kW each).** BJT gyrators:
   - FAT: 1µ, 68n, 2.2k, 100k → **41 Hz, Q 1.8**.
   - MUSCLE: 0.1µ, 10n, 220 Ω, 100k → **1.07 kHz, Q 6.7**.
6. **Output.** Level 50kA, emitter follower.

**Source.** https://schematicheaven.net/effects/boss_pw2_powerdriver_overdrive.pdf

## OS-2 OverDrive/Distortion (1990–, analog) — MEDIUM
Community trace; this pedal overlaps with the overdrive family.

**Signal path:**
1. **Two parallel paths:**
   - **OD path:** IC2b with feedback diodes, asymmetric (D7 one way, D8 + D9 the other). VR3a 270k, R37 1k, R39 100 + C23 4.7µ.
   - **DIST path:** IC2a (VR3b 270k, R22 12k, R30 100 / C18 0.47µ, R31 1.2k / C19 2.2µ), then R23 1k, D3/D4 shunt to ground, then an RC network and Q2 recovery.
2. **COLOR** (VR4 20k, log) crossfades the two paths.
3. **Gain and tone.** IC1a (1 + 150k/22k = ×7.8), then IC1b tone (VR2 20k, R1 4.7k + C1 10n).
4. **Output.** Level 50k.

**Source.** https://schematicheaven.net/effects/boss_os2_overdrive_dist.pdf

## Not found
- **"DST-?".** No BOSS compact pedal with a DST- code exists in the release chronology (http://www.bossareaforum.com/Forum/viewtopic.php?t=463). The closest are the DS-1X and the DS-1 variants (DS-1-4A, DS-1-B50A, DS-1 WH).
- **LMB-3.** A bass limiter, so out of scope.
