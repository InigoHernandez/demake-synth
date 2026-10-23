# Demake synth: the plan

A lofi ambient synth firmware for the M-VAVE FM-1, melancholic, with a late-90s console demake look and sound.
Written 2026-10-23, before any code. Every number below is marked **[M]** (measured) or **[E]** (estimated); the
measurements are Mac instruction counts with the FM-1's build flags (1.1), taken from MONO2K's code and from
throwaway integer prototypes of each kernel.

## 0. In one page

- **Four tracks**, each with an engine (SAMPLE, PAD, DRONE, PLUCK), its own pattern in a **piano roll**, and a chain:
  COLOR → MEDIA → MOTION → SPACE. Two **shared send buses** (ECHO, VERB) take every reverb and delay.
- **A chord machine** (key, mode, 16 melancholic presets, voicing, inversion, spread, strum) drives a track, and the
  other tracks' notes follow its chords.
- **Playful first:** nearly every task in one or two presses (the few exceptions are flagged in 3.7); keys write where the
  cursor is, a held key is the note you edit, OCT− takes away, OCT+ copies, everything undoes.
- **The look:** four tones (paper, fog, ink, one warm light) with ordered dithering, a pixel HUD font and a soft
  grotesk, save blocks with little animated icons.
- **The sound:** the owner's 64 recordings as 4-bit block ADPCM at 11–22 kHz, a per-track GRIT (22 kHz, ADPCM), and
  reverbs voiced by ear after console rooms, halls and space echoes.
- **It fits:** four tracks with typical effects and 16 voices come to ~3000 instructions a sample over two cores
  against a yardstick of 2 × 1650; the device calibration in Hortator puts a core at ~3470 (1.4).

**Decisions for the owner** (each is also in its section):

1. **The display font** (5.3): Tomato Grotesk ships only if your licence from The Designers Foundry covers embedding
   in an app; otherwise an OFL face.
2. **Where saves and extra samples live** (2.6): I suggest keeping MONO2K's store (`0xC0000..0xE0000`) untouched,
   so both firmwares keep their saves.
3. **The identity range** (2.7): `FM-1_965` (tests) and `966`+ (releases) look free.
4. **The samples' licence** (2.5): your recordings ship in the package. The code is GPL-3.0; the sounds could be
   GPL-3.0 too, or CC BY 4.0 if you want others to reuse them.
5. **Additions to the brief, to keep or drop:** a SOUND page (EDIT: the engine and its source), a HOME page
   (overview and mixer), an LFO page (later), DRIFT as FILTER's fourth knob.
6. **Integer only, as the brief says, though the chip has an FPU.** AMB-1, FoMni, FiMba and FuMi compile their DSP
   in single-precision float with the SDK's `-mcpu=r3 -mfprev1`; MONO2K, CS-1, OberVave and Clipper forbid float.
   This plan stays integer: it keeps MONO2K's tooling, the browser and host renders identical to the device's, and
   the platform's build checks. Phase 0 can time the FPU in ten minutes, for the record.

## 1. The effects

### 1.1 Budget and method

- **Counting.** Mac instructions per 44.1 kHz output sample (per stereo frame for stereo kernels), built as the FM-1
  builds (`cc -O2 -fno-builtin -fno-vectorize -fno-slp-vectorize`), read from `proc_pid_rusage` `ri_instructions`,
  median of 3 runs of 10 s. Felucca's yardstick (its heaviest mix, 1574 with plain `-O2`) is ~1650 in this stricter
  count (MONO2K's ratio, ×1.05).
- **Capacity per core.** The yardstick, 1650, is proven to run on one core. Hortator's on-device calibration (6.86 ns
  a host instruction, heavy DSP within ±8 %) puts 100 % of a core at ~3470; with 20 % kept back for the screen and the
  panel, that is ~2750 a core [E]. MONO2K's busiest pattern (a 2714 critical path over two cores [M]) "runs without
  overload" on the FM-1. Clipper's tests use "1.7 % per 100 host instructions", which would put a core at ~5900:
  the calibrations disagree, and no firmware has measured the clock (claims range from 240 to 360 MHz). **This plan
  budgets against the yardstick** and treats the rest as headroom until phase 0 measures the ratio on the device.
- **The JieLi compiler's code** for the kernels was checked against the Mac's: its inner loops are 0.9–1.5× as long,
  median ~1.2. Delay lines with many wrap tests and 64-bit state run ~1.4×, plain filters ~1.0× [M, static]. Kernels
  with delay lines get extra margin.
- **Rates.** Per-track effects run at 44.1 kHz. The buses run at 22.05 kHz: lofi by nature, half the memory, and
  the halving and doubling (a half-band each way) is ~31 a sample in stereo, included in every bus figure.
- **Sources.** pichenettes/eurorack (MIT) and electro-smith/DaisySP (MIT, `Source/`) do all their arithmetic in
  float, even where their delay buffers are 12- or 16-bit, so they are **algorithm references**, ported to Q15 / Q31
  by hand. hugelton/Felucca (GPL-3.0, integer, runs on this chip) and Felucca-Engines (MIT, integer) are the code
  references. MONO2K's code is the owner's own (GPL-3.0). Paths: ER = `eurorack/`, DS = `DaisySP/Source/`,
  FEL = `Felucca/firmware/src/`, FE = `Felucca-Engines/src/`, M2K = `mono2k/firmware/src/`.

### 1.2 Per track: COLOR, MEDIA, MOTION, SPACE

One effect per row and per track, picked with ALGORITHM, OFF first. Macro slots: K1 AMOUNT, K2 TONE, K3 TIME,
K4 CHARACTER (3.4).

| Row | Effect | Algorithm | Source | RAM / track | Instr / sample |
| --- | --- | --- | --- | --- | --- |
| COLOR | SATURATE | tanh from a 257-entry table, interpolated; a bias for even harmonics; tilt EQ before and after (two one-poles). AMOUNT drive, TONE tilt, CHARACTER even ↔ odd | FE `dsp.c` `softclip()` + `TANH_Q15`; the bias after FEL `fx.c` `track_dist()` | 16 B | 18 [M] + 14 = **32** [E] |
| COLOR | DISTORT | drive 1–9× into a clip that morphs into a triangle fold, a 2-pole tone low-pass, make-up gain. AMOUNT drive, TONE low-pass, CHARACTER clip → fold | FEL `fx.c` `ins_drive()` / `ins_shape()`; M2K `fold16()` | 24 B | 18 + 14 + 22 = **54** [E] |
| COLOR | CRUSH | bit depth by rounding (no DC), sample-and-hold on a fractional Q16 clock, an SVF low-pass in front. AMOUNT bits 16 → 3, TONE anti-alias, TIME rate 44.1 → 1 kHz, CHARACTER clock jitter | ER `braids/braids.cc` bit masks and decimation; FEL `INS_HOLD`; FE `tsvf_lp` | 32 B | 9.5 [M] + 22 = **32** [E] |
| COLOR | CODEC | **own design**, MP3-style smear: a 4-band SVF crossover at 22.05 kHz; per frame each band's quantizer step from the frame's peak; weak high bands gated (holes, "birdies"); the step applied through a look-ahead so noise smears before transients (pre-echo). AMOUNT loss, TONE band limit (to 4 kHz), TIME frame 64–512 samples, CHARACTER dropouts and warble (random band and frame gating). If it doesn't sound codec enough: a 64-point integer MDCT instead | own; no codec effect exists in any of the repos | 0.8 KB | 4-band **101** [M]; MDCT M = 32 **139** [M] |
| COLOR | NOISE | hiss (xorshift, one-pole colour), crackle (random clicks from a probability table, sign alternating, ×5/8 decay), hum (50 / 60 Hz, harmonics 1–5 from the sine table, slowly wandering). AMOUNT hiss, TONE colour, TIME crackle density, CHARACTER hum. Sounds while the track or its tail does | M2K `m2k_fx.c` `bus_block()` hiss and crackle; FE `eng_noise.c`; hum own | 20 B | **60** [M] |
| MEDIA | TAPE | a modulated delay (wow 0.3–1 Hz with a slowly random rate, flutter 5–7 Hz; Q16 linear read), a head-bump biquad (60–120 Hz, +2–4 dB), soft-clip saturation, head-loss one-pole. AMOUNT saturation, TONE bump and HF, TIME wow, CHARACTER flutter | M2K `bus_block()` tape; ER `beads/dsp/fx/wow_and_flutter.h` (the random-rate wow); FE `softclip()` | 4.2 KB | **74** [M] (×1.4 on the target: a delay line) |
| MEDIA | CASSETTE | TAPE's line with a faster, random warble; HF loss as a 2-pole low-pass at 6–10 kHz that closes as the level rises; dropouts: random 10–150 ms dips of 3–20 dB that close the low-pass further. AMOUNT warble depth, TONE HF loss, TIME warble rate, CHARACTER dropouts | own, on TAPE's parts | 4.2 KB | **~88** [E] |
| MEDIA | VHS | band limit 80 Hz–10 kHz, a slow deep wobble with fast jitter, a head-switch tick at 25 / 29.97 Hz, "tracking" events (20–200 ms: a mute or noise burst, a pitch dip, a short repeat from the line). AMOUNT wobble depth, TONE band limit, TIME wobble rate, CHARACTER tracking glitches | own, on TAPE's parts | 8.3 KB (repeats up to 93 ms) | **~95** [E] |
| MOTION | CHORUS | 2 swept taps (quadrature LFOs) at 7–20 ms, stereo out; CHARACTER 1 tap mono → 2 taps → a 3-tap ensemble with a fast and a slow LFO. AMOUNT depth and mix, TONE wet low-pass, TIME rate, CHARACTER voices and width | ER `rings/dsp/fx/chorus.h`, `ensemble.h` (topology); FEL `fx.c` chorus; M2K `mod_tick()` | 2.1 KB | 2 taps **53** [M]; ensemble ~75 [E] |
| MOTION | VIBRATO | pitch modulation of the track's voices at control rate (no delay line): a sine at 3–8 Hz. AMOUNT depth, TIME rate, CHARACTER fade-in | own | — | **~2 a voice** [E] |
| MOTION | DRIFT | each voice its own smoothed random pitch wander, and a random detune per note. AMOUNT wander depth, TIME wander rate, CHARACTER per-note detune | ER `braids/vco_jitter_source.h` (MIT, integer); FE drift | 8 B / voice | **~2 a voice** [E] |
| SPACE | the sends | VERB SEND, ECHO SEND, PAN, WIDTH (alternate voices spread left and right) | — | — | **5** [E] |

VIBRATO and DRIFT act on the voices rather than on a delay line: cheaper, cleaner, and each voice can drift its own
way. They still sit in the MOTION row, where the brief puts them.

### 1.3 Shared: the ECHO and VERB buses

Every track sends to both; each bus holds one type at a time (GLO, 3.4), stereo, at 22.05 kHz.

| Bus | Type | Algorithm | Source | RAM | Instr / sample |
| --- | --- | --- | --- | --- | --- |
| ECHO | TAPE | 8-bit mu-law lines (1.5 s), wow on the read; a time longer than the line slows the line's clock instead (darker, like tape); one-pole HP and LP and a soft clip in the loop. AMOUNT return, TONE loop filter, TIME synced 1/16 … 1/2 dotted or free, CHARACTER feedback | M2K `m2k_fx.c` `delay_block()`, `mulaw_enc()`, `dl_read()`; ER `clouds/dsp/mu_law.h` (MIT alternative) | 66 KB | **65** [M] |
| ECHO | PING-PONG | sum and difference lines, so each repeat crosses sides | M2K `delay_block()` dual mode | (shared) | **65** [M] |
| ECHO | FILTERED | a band-pass in the loop: TONE its centre, CHARACTER feedback and width | M2K's loop filters | (shared) | **~70** [E] |
| VERB | ROOM | **own voicing** after console rooms, by ear: MONO2K's SPU-style comb and all-pass network with our own tap lengths and gains, ~0.25 s of lines. (The console's documented preset registers are not used.) AMOUNT return, TONE damping, TIME size, CHARACTER early reflections | M2K `bus_block()` reverb; FEL `fx.c` `rev_room()` (Freeverb-style) | 7–11 KB | **72–90** [M] |
| VERB | HALL | 8-line FDN with a Hadamard (adds only) matrix, input diffusers, damped and swept lines, ~1.5 s; voiced after console halls. CHARACTER modulation | FEL `fx.c` `rev_hall()`; DaisySP-LGPL `ReverbSc` (float, LGPL) as a reference | 66 KB | **216** [M] |
| VERB | SPACE | ROOM's network with long discrete echo taps spread over ~1.4 s, voiced after a console's space echo. CHARACTER echo density | own, on ROOM | (in the 66 KB) | **~110** [E] |
| VERB | PLATE | Dattorro / Griesinger: 4 input all-passes, a figure-8 tank of two halves with LP damping and slow-modulated all-passes, ~1 s of lines. CHARACTER modulation | ER `clouds/dsp/fx/reverb.h` (topology; Q15 accumulators) | 45 KB | **244** [M] |
| VERB | SPRING | 2 springs, each 12 stretched all-passes (a + z⁻²) / (1 + a·z⁻²) for the chirp, a 30–60 ms loop with LP and decay, pickups with a slow wobble. CHARACTER drip | FEL `fx.c` `rev_spring()` (after Välimäki, Parker and Abel) | 4.6 KB | **156** [M] |
| VERB | SHIMMER | PLATE with a 2-tap, crossfaded +12 semitone pitch shifter in its loop (LP and gain under 1). CHARACTER shimmer amount | ER `clouds/dsp/fx/pitch_shifter.h` (topology); FEL `perform.c` `perf_harm()` | 49 KB | **279** [M] |

The VERB types share one 66 KB buffer (switching type clears it); the ECHO has its own 66 KB.

### 1.4 What fits

Voices and engines come from section 2 (~110 a voice at 44.1 kHz [E from M parts]). "Typical" is SATURATE or CRUSH,
TAPE and CHORUS on every track, PLATE and the ECHO; "heavy" is CODEC, VHS and the ensemble on every track, SHIMMER
and the ECHO.

| Part | Lean | Typical | Heavy |
| --- | --- | --- | --- |
| base: sequencer, mix, control-rate work, master limiter | 150 [E] | 150 | 150 |
| 4 track chains (per track: COLOR + MEDIA + MOTION + sends + 25 overhead) | 4 × 63 = 252 | 4 × 189 = 756 | 4 × 301 = 1204 |
| buses | ROOM 90 | ECHO 65 + PLATE 244 = 309 | ECHO 65 + SHIMMER 279 = 344 |
| 16 voices × 110 | 1760 | 1760 | 1760 |
| **total** | **2252** | **2975** | **3458** |
| two cores, buses on core 2 | ~1130 each | ~1490 each | ~1730 each |
| against the yardstick (1650) / the calibrated plan (2750) | 68 % / 41 % | 90 % / 54 % | **105 %** / 63 % |

So, measured against the yardstick:

- **Every reverb, the shimmer and the echo must be shared buses.** One stereo 1.5 s echo is 66 KB, so four would
  take most of the 336 KB pool. The reverbs cost 72–279 and 5–66 KB each.
- **COLOR, MEDIA and MOTION stay per track.** Each costs 32–101; the per-track lines total 2–8 KB.
- **CODEC on all four tracks at once** is the one case past the yardstick (the heavy column). Four steps keep it safe:
  - the voice pool sheds voices under load, oldest released first, and shows LOAD on screen (as Hortator does above
    95 %);
  - GRIT 22K halves a track's voice cost (2.3);
  - if the device's ratio is as Hortator measured, it fits anyway (63 % of the plan);
  - the MDCT version (139) is only used if the 4-band one (101) does not sound right.
- **The two cores:** each 16-sample block the first core hands tracks [split, 4) to the second, balanced by cost.
  The buses run on the second core one block late (0.36 ms), which takes them off the first core's critical path
  (MONO2K runs its bus after the join; Demake doesn't need to). The second core's job runs from XIP flash and its
  loop from RAM (CS-1's audio worker): the first core joins it before any flash write (2.6).

## 2. Engines and samples

### 2.1 The engines

Any track can play any engine; a track's voices all use its engine. Per voice: the engine, a ZDF SVF (TYPE LP12,
LP24, BP, HP, OFF; 27 [M] with coefficients every 16 samples) and the ADSR (7.5 [M]).

| Engine | What it is | Built from | Instr / voice |
| --- | --- | --- | --- |
| SAMPLE | a sample from flash, 4-bit block ADPCM decoded on the fly (2.4), pitched by a Q16 phase with linear interpolation; START trims the attack (by blocks); LOOP OFF, FULL (the sample's loop), or SHORT 1/2 … 1/16: a shorter loop inside the sustain, for the short-sample, looped-pad character | the decoder ours, from the format's public description; the player after FEL `eng_sample.c` (IMA ADPCM streamed from flash) | ADPCM voice 62–79 [M] + 35 = **~105** |
| PAD | 3 wavetable oscillators on the owner's single cycles (32 of MONO2K's 64, band-limited twice), detuned around the note; MORPH crossfades to the next wave and back, slowly | M2K DIGI-WAV and SWAV-ENS (the owner's own) | 3 × 24 [M] + 10 + 35 = **~117** |
| DRONE | 4 partials per voice (the COLOR set: root, fifth, octave, ninth …), each pair beating (BEAT, 0–8 Hz), each partial's level wandering on its own smoothed random (EVOLVE), a little breath noise; a long note never repeats itself | own design; sines from the table | 4 × 10 [M] + 20 + 35 = **~95** |
| PLUCK | Karplus-Strong: a noise or sample burst into a delay line with a damping loop filter (TONE, DECAY); BODY blends in two resonant modes for kalimba, glass and bell | FEL `phys_dsp.c` (MIT port of DaisySP / Plaits' string and modal, integer; its per-note 64/32 divisions become a reciprocal and a Newton step); ER `braids` `RenderPlucked` (MIT, integer) | **~100** [E] |

### 2.2 Voices and the two cores

- A pool of **16 voices** shared by the four tracks; each track has a POLY of 1–8 (the chord track 6, the others 4
  by default). A new note takes a free voice, else the track's oldest released voice, else its oldest.
- The budget (1.4) holds 16 voices with typical effects on two cores at the yardstick. If phase 0 measures the device
  as Hortator did, the pool grows to 24.
- Voice state is small (~200 B each); sample data is read from flash through XIP by either core.
- The second core, as MONO2K uses it (CS-1's audio worker): the job is checked against a one-core render, sample
  for sample, in the host self-test. CoralFlower ships its second core off by default after RAM misreads on some
  units, and Clipper fixed the same by raising the core voltage: Demake keeps a one-core build (`DUAL=0`), and the
  voice pool shrinks to fit it.

### 2.3 GRIT, per track (SOUND K4)

| GRIT | What it does | Cost |
| --- | --- | --- |
| OFF | voices at 44.1 kHz | — |
| 22K | the track's voices render at 22.05 kHz and are doubled once per track with a half-band: the console's rate, band-limited to ~10 kHz | about **half** a voice's cost, + ~15 a track |
| 22K+ADPCM | as 22K, and the track's output requantized through a 4-bit block ADPCM encode and decode: the console's grain on everything, attacks crunchy, tones round | + ~18 a track (M2K `adpcm_step` 36 [M] at half rate) |
| 11K+ADPCM | the same at 11.025 kHz | about a quarter of a voice's cost |

### 2.4 The sample format

- **Mono, 4-bit block ADPCM**, after the console's public format description (psx-spx): 16-byte blocks of 2 header
  bytes (shift; one of 5 two-tap predictor filters; loop flags) and 28 samples of 4 bits, **4.57 bits a sample**.
  Every block can be decoded on its own given the two samples before it, so loops and START land on any block. The
  encoder tries every filter and shift for each block and keeps the one with the least error. It is our own
  implementation from the description, not anyone's code.
- **Rates, per sample:** 11,025 Hz (6,300 B/s), 16,000 Hz (9,143 B/s) or 22,050 Hz (12,600 B/s), chosen from the
  measured bandwidth. The owner's recordings are dark (99.9 % of their energy under 0.3–4.9 kHz [M]), so most go at
  11,025 Hz without loss.
- **Header, 32 B:** name (12), rate, root note and fine tune (cents), loop start and end (blocks), flags (loop,
  one-shot, category), peak, length (blocks), CRC-16.
- **The pipeline** (`tools/gen_samples.py`, never edited by hand):
  1. read the original WAV, untouched, from `assets/samples/`;
  2. take out DC, resample with a band limit, trim, fade in over 2 ms;
  3. normalise to a recorded peak;
  4. find the loop: zero crossings with matching slopes and a spectral seam score, as the audit did; or bake an
     equal-power crossfade of 30–100 ms;
  5. nudge the rate a few cents so the loop is a whole number of blocks (the fine tune keeps the pitch);
  6. encode, decode again and print the SNR;
  7. write a row per sample in `assets/samples/PROCESSING.md`.

### 2.5 The samples: the owner's 64 recordings

The owner made all 64 with their pedals and recorded them; some names come from the presets that inspired them.
Every factory sound gets a new short name (12 characters fit the screen), and each sample's source is listed as the
owner's recording.

**The set, measured [M]:**

- 48 kHz, 16-bit mono, 1.66–2.02 s each, 128.5 s in all, every file peak-normalised to 0 dBFS, no metadata chunks.
- Every one is a held C (5 at C2, 18 at C3, 35 at C4, 4 at C5; two carry an F layer). Nine are 15–28 cents off, so
  the header's fine tune is needed.
- The pitch wobble is baked in: 25 cents peak to peak at the median, 40+ on twelve files. TAPE's wow defaults low,
  so the wobble isn't doubled.
- 41 pads, 12 sustained with a fast attack, 7 keys that decay and hold, 4 plucks; no rhythmic loops or noise
  textures (NOISE and the DRONE engine cover those).
- They are dark: energy above 4 kHz is under −40 dB on 49 of them.

**What fits** [M from the audit's cuts; flash estimate in 2.6]:

| Bank | Cut | Size |
| --- | --- | --- |
| the 16 most useful (keys, pads, a bass, a wind, textures) | attack + loop at the first good seam, rate from the bandwidth | **119 KiB** |
| all 64, the same cuts | | **542 KiB** |
| all 64, tight (11,025 Hz, a 1.0 s cut, one-shots at 1.5 s) | | **379 KiB** |

Bank A, in the app slot, ships with the package: about 240 KiB, so ~32 sounds at their best cuts, the 16 above
first. Bank B goes in the data area (2.6), loaded from the website: ~116 KiB, so ~16–20 more. About 50 of the 64 fit
as cut; all 64 fit only cut tight, and then just. The owner picks which sounds make bank A, from WAVs of the cut and
encoded samples (phase 1).

**Licence of the sounds:** the owner's to choose (decision 4). The originals and the encoded bank sit in the repo
under that licence, with `PROCESSING.md`.

### 2.6 Flash and RAM

**Flash** (the app slot is 581,564 B; the data area `0x93000..0xE0000`, mapped through XIP at boot):

| Part | Size |
| --- | --- |
| code (platform + core); MONO2K's is ~204 KB with fifteen machines | ~200 KB [E] |
| fonts: PIX, the display face at 15 and 28 px in 2 bits, the boot size | ~20 KB [E] |
| icons (32 × 3 frames × 16 × 16 × 2 bits) and pictures | ~16 KB [E] |
| tables (sine, pitch, filter, tanh, mu-law, times, chords) | ~25 KB [E] |
| PAD waves: 32 × 256 × 2 B, two band-limited versions | 32 KB |
| factory songs and sounds | ~16 KB [E] |
| **left for bank A** | **~270 KB, of which 240 KiB planned** |

| Data area region | Use |
| --- | --- |
| `0x93000..0xB0000` (116 KiB) | bank B: the owner's extra samples, written by the website's editor over the link (header, CRC, loop points, as the store) |
| `0xB0000..0xC0000` (64 KiB) | Demake's store: 15 songs (a song packs to ~1–3 KB), 32 USER sounds, settings; written as MONO2K writes (4 KiB sectors, the header with its CRC last) |
| `0xC0000..0xE0000` | left alone: MONO2K's store (and Felucca's projects), so switching firmware keeps both firmwares' saves |

Every part of the data area is someone's: OberVave keeps programs at `0x97000`, Clipper projects at
`0x97000..0x9EFFF` and user slots from `0xA0000`, Felucca samples at `0xA0000..0xDBFFF`, CoralFlower its loops at
`0xB6000`, zp12 its store at `0xC4000..0xD7FFF`. No layout avoids them all, so Demake avoids the owner's own
(MONO2K's) and the installer page says what an install of Demake may overwrite. Demake checks its own magic and
CRC, so it never reads another firmware's data as its own.
A sector erase stops the sound for ~50 ms, so saves happen only while the sequencer stands. The first core joins the
second (which then reads no flash) before every write.

**RAM:** `.data + .bss` within 96 KiB (voices, tracks, UI state, the platform); the pool within 336 KiB:

| Pool | Size |
| --- | --- |
| VERB bus (one buffer for every type, the shimmer's shifter inside) | 66 KB |
| ECHO bus (stereo mu-law, 1.5 s at 22.05 kHz) | 66 KB |
| MEDIA lines, 4 × 8.3 KB | 33 KB |
| MOTION lines, 4 × 2.1 KB | 8 KB |
| PLUCK strings (16 voices × 1.35 KB, notes from C2) | 22 KB |
| CODEC look-ahead, 4 × 0.8 KB | 3 KB |
| framebuffer (2 bits) and strip | 18 KB |
| the song open (4 × 256 notes × 6 B, progressions, sounds) | 8 KB |
| undo (a 16-step journal and 2 snapshots) | 16 KB |
| store and link buffers | 8 KB |
| **total** | **~248 KB of 336** |

### 2.7 Names

- **Product:** Demake (repo `demake-synth`). No brand, console or product marks in the name, the screens or the sounds.
- **Identity:** taken, from the local firmwares and the Hub's list in zp12's `tools/fm1_sync.py`: `900`–`931` (SLOOP,
  Felucca, DUELL, FM-106, CoralFlower and others), `93X`–`94X` (sloopDX), `950`–`958` (CS-1, OberVave 955, Frodhes),
  `961` (FOMAJAM), `970`–`979` (zp12), `98N` (MONO2K, GammaWave, a Doom port), `99N` (Dinghy), `700`/`7XXYYZZ` (AMB-1,
  FuMi), `800`/`8XXYYZZ` (FoMni, FiMba). **`962`–`969` look free: `FM-1_965` for test builds, `966`+ for releases**,
  checked against the Hub's live list before the first release.
- **SysEx:** `F0 7D 'D' 'M' 'K' cmd pack7(body) F7` (0x7D, the non-commercial ID, with a magic of its own), MONO2K's
  protocol otherwise.

## 3. The workflow on the FM-1's controls

### 3.1 The panel

```
 MASTER   SELECT   ┌────────────┐   K1      K2      K3      K4
 (a pot)           │            │   FX   SEL   ENV   LFO   EDIT  GLO
                   │   SCREEN   │
 PRESETS  ALGO     │  240 x 240 │   HOME SAVE  ARP   SEQ   PLAY  REC
 OCT−  OCT+        └────────────┘
 ┌──────────────────── 27 keys, F3 … G5 (16 white, 11 black) ───────────────────┐
```

The four knobs sit to the right of the screen, level with its top band; SELECT and ALGORITHM sit to its left;
PRESETS, MASTER and OCT−/OCT+ further left. The screen's knob row is drawn at the knobs' height, in their order.

### 3.2 The rules (one page, then every combo is checked against it)

1. **The keys play the current track.** On ROLL and CHORD, while the sequencer is stopped, a key also **writes at
   the cursor**. While it plays, keys only play, and REC records them in time.
2. **One job per encoder, everywhere:** SELECT = the track, PRESETS = the track's sound, ALGORITHM = the page's
   left cell (named on screen), K1–K4 = the four cells on top. A page looks and works the same on every track.
3. **Tap a page button to open its page, tap it again for the page's next row. Hold it for its layer**; the layer's
   map is on screen after 350 ms, and the keys and knobs it changes are named there.
4. **Hold the thing, then act on it.** A held key is a note (or a chord slot), SEQ held is the pattern, ARP held is
   the progression, GLO held is the history. **OCT− takes away, OCT+ adds a copy:** delete / duplicate a note,
   clear / double a pattern, undo / redo. Alone, OCT− and OCT+ shift the keyboard's octave.
5. **Everything undoes** (GLO + OCT−, 16 steps; GLO + OCT+ redoes). Only writing over a *different* save in flash
   asks a question (YES = OCT+, NO = OCT−). Clearing a whole song needs a 1 s hold with a ring filling on screen.
6. **No hidden state.** What a held button does is on screen while it is held; the keys' LEDs show the chord now;
   the cursor is always visible on ROLL.
7. **Mechanics** (from MONO2K): a modifier that was used does nothing on release; a second press within 50 ms of the
   first counts as together (rubber domes); a tap is under 350 ms with nothing else touched; no double-taps, no
   timing gestures that destroy anything.

These answer what reviews and the owner found hard in other instruments: unlabelled double-assigned buttons, modes
that change what a key means, no undo, and state you cannot see on the screen.

**Not MONO2K's scheme.** The pages follow the buttons' printed labels wherever one fits (SEQ the roll, ENV the
envelope, FX the effects, ARP the chords, EDIT the sound, HOME home, GLO global, LFO the LFO); only SEL → FILTER is
arbitrary (it is MONO2K's too, so the owner's hands already know it). MONO2K's verbs (REC copies, PLAY pastes, SEQ
clears), parameter locks, trig conditions, trig types and latches are left out: a note is edited by holding it, and
the only verbs are OCT− and OCT+. Undo keeps MONO2K's GLO + OCT− because it is rule 4's "take away".

### 3.3 Always, on every page

| Control | Action | Presses |
| --- | --- | --- |
| a key | plays the current track (snapped to the key and the chord, 4.6) | 1 |
| SELECT | the track, 1–4 (the tabs in the header) | 1 |
| PRESETS | the track's sound: loads at once, so turning and playing auditions (GLO + OCT− brings the last one back) | 1 |
| ALGORITHM | the page's left cell (see 3.4) | 1 |
| K1–K4 | the page's four cells (see 3.4) | 1 |
| OCT− / OCT+ | keyboard octave −2 … +2; the ROLL's view follows | 1 |
| PLAY | play / stop (playback starts at bar 1; a stop is a stop, not a pause) | 1 |
| REC | while playing: recording on / off at once. While stopped: armed, starts with PLAY | 1 |
| SAVE | saves the song in its block now ("SAVING…" on screen; only while stopped, see 2.6) | 1 |
| MASTER | output level (a pot) | — |
| GLO held + OCT− / OCT+ | undo / redo | 2 |

### 3.4 Pages: one button each, the same layout on every track

| Button | Page | Tap again | ALGORITHM (left cell) | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SEQ | **ROLL** | — | ZOOM 16 / 32 / 64 | CURSOR | VIEW | LENGTH | FINE |
| SEQ, a key held | ROLL, that note | — | VELOCITY | MOVE | PITCH | LENGTH | NUDGE |
| EDIT | **SOUND** | — | ENGINE | SOURCE | SHAPE | MOVE | GRIT |
| SEL | **FILTER** | — | TYPE | CUTOFF | RESONANCE | ENV AMOUNT | DRIFT |
| ENV | **AMP** | — | LEVEL | ATTACK | DECAY | SUSTAIN | RELEASE |
| FX | **FX** (the chain) | COLOR → MEDIA → MOTION → SPACE | the row's EFFECT (OFF first) | AMOUNT | TONE | TIME | CHARACTER |
| FX, SPACE row | the track's sends | | — | VERB SEND | ECHO SEND | PAN | WIDTH |
| ARP | **CHORD** | VOICE → KEY | PROGRESSION | VOICING | INVERSION | SPREAD | STRUM |
| ARP, KEY row | | | PROGRESSION | KEY | MODE | CHANGE | RHYTHM |
| HOME | **HOME** (4 lanes, the mixer) | LEVEL → VERB → ECHO → PAN | TEMPO | track 1 | track 2 | track 3 | track 4 |
| GLO | **GLOBAL** | VERB → ECHO → SONG → SETUP | the bus TYPE | AMOUNT | TONE | TIME / SIZE | CHARACTER |
| GLO, SONG row | | | — | SWING | METRONOME | REC START | CLOCK |
| GLO, SETUP row | | | — | THEME | KEYS | MIDI CH | VELOCITY |
| LFO | **LFO** (build phase 4) | — | TARGET | RATE | DEPTH | SHAPE | FADE |

**"The same knob does the same job"**, made concrete:

- On SOUND, every engine keeps the same slots: K1 the source or tone, K2 its shape, K3 its movement, K4 GRIT.
- Every effect (FX rows and the two shared buses on GLO) has the same four slots: **K1 AMOUNT** (how much),
  **K2 TONE** (dark … bright), **K3 TIME** (rate, size, time), **K4 CHARACTER** (the effect's own spice: flutter,
  feedback, dropouts). An effect with fewer macros leaves a slot empty (drawn "—"). Learning one effect teaches all.
- On ROLL, K1 is always time (cursor or move), K2 pitch (view or transpose), K3 length, K4 fine time.
- On HOME, Kn is track n.
- FILTER and AMP keep the brief's parameters in the brief's order. FILTER's TYPE sits on ALGORITHM so CUTOFF gets K1,
  the knob nearest the screen; DRIFT (a slow random wander of the cutoff, up to ±1 octave) is an addition for the
  fourth knob. AMP's LEVEL sits on ALGORITHM. The filter's envelope is the AMP envelope (one envelope per track,
  ENV AMOUNT bipolar): one shape to learn, and a pad's filter opens with its swell, a pluck's closes with its decay.

**SOUND's slots per engine** (2.1 has the engines):

| ENGINE | K1 SOURCE | K2 SHAPE | K3 MOVE | K4 GRIT |
| --- | --- | --- | --- | --- |
| SAMPLE | which sample | START (attack trim) | LOOP: OFF · FULL · SHORT 1/2 … 1/16 | OFF · 22K · 22K+ADPCM · 11K+ADPCM |
| PAD | WAVE (of 32) | DETUNE (3 oscillators) | MORPH speed (to the next wave and back) | " |
| DRONE | COLOR (partial set) | BEAT (detune of the pairs, 0–8 Hz) | EVOLVE speed | " |
| PLUCK | TONE (exciter brightness) | DECAY | BODY (string → bell) | " |

### 3.5 The ROLL in detail

| State | Control | Action |
| --- | --- | --- |
| stopped | a key | writes a note at the cursor (the pen's length and velocity) and holds it, sounding; if that pitch already starts at the cursor, holds that note instead |
| stopped | keys together (or added while one is held) | a chord at the cursor |
| stopped | key released | the cursor moves to the note's end, ready for the next one (step entry); after a delete it stays |
| a key held | K1 / K2 / K3 / K4 | MOVE (steps) / PITCH (scale steps; semitones with SNAP off) / LENGTH (steps) / NUDGE (±1/8 step, up to ±1/2) |
| a key held | ALGORITHM | VELOCITY |
| a key held | OCT− | **deletes** the note (or every held note) |
| a key held | OCT+ | **duplicates** it right after itself; the copy is now held, so OCT+ again makes a run |
| nothing held | K1 / K2 / K3 / K4 | CURSOR / VIEW (scroll pitch) / LENGTH (the pen's) / FINE (the cursor ±1/8 step) |
| nothing held | ALGORITHM | ZOOM: 16, 32 or 64 steps across |
| cursor on notes | — | the notes starting at the cursor are outlined, **and their keys' LEDs blink**: hold the blinking key to edit, delete or duplicate it |
| playing | a key | plays (not written); the view follows the playhead |
| playing + REC | a key | recorded: start to the nearest step, length rounded to steps (at least 1), pitch snapped; overdub. One REC pass is one undo step |
| SEQ held | K1 / K2 / K3 / K4 | SHIFT (rotate the pattern) / TRANSPOSE (all notes, scale steps) / STEPS (1–64) / SPEED (1/16, 1/8, 1/4, 1/2 per step) |
| SEQ held | ALGORITHM | FOLLOW: FREE · KEY · CHORD · ROOT · PLAYS CHORDS (4.6) |
| SEQ held | OCT− / OCT+ | clear the pattern / double it (length × 2, the notes copied) |

Every track has its own pattern, length and speed, so tracks loop against each other (a 7-step pluck over a 16-step
pad over a 64-step chord track at 1/4): the Eno tape-loop trick, for free. A note longer than the pattern is cut at
its end. SNAP is on by default, and the ROLL then shows only the scale's rows (GLO > SETUP > KEYS = FREE turns it off, 4.6).

### 3.6 Held layers

| Held | Map on screen | Keys | Knobs / encoders | OCT− | OCT+ | Other |
| --- | --- | --- | --- | --- | --- | --- |
| a key (ROLL, CHORD) | the note's (slot's) values | — | 3.5 / 4.4 | delete | duplicate | |
| SEQ | PATTERN | — | SHIFT, TRANSPOSE, STEPS, SPEED; ALGORITHM FOLLOW | clear | double | |
| ARP | PROGRESSION | play chords live | K1 SLOT (the cursor) | clear USER progression | print the chords into the chord track's roll (its FOLLOW becomes FREE) | |
| HOME | MUTES | the 4 lowest white keys mute / unmute tracks 1–4 (lit = sounding) | — | held 1 s (a ring fills): new song | — | |
| GLO | HISTORY | — | — | undo | redo | |
| EDIT | SOUND | — | — | INIT the sound | — | SAVE: the sound into the library (USER 01 …) |
| FX | CHAIN | — | K1–K4: the AMOUNT of COLOR, MEDIA, MOTION, SPACE (its VERB SEND) | — | — | |
| SAVE (1 s) | opens the MEMORY CARD page | — | K1 the block, K2 card 1 (yours) / 2 (demos) | held 1 s: erase the block (asks) | — | SAVE saves here (asks if it holds another song); PLAY loads (asks if the song now has unsaved edits) |
| SEL, ENV, LFO | the page's help card after 1 s | — | — | — | — | |

### 3.7 Presses per task (flagged: more than two)

| Task | Presses | How |
| --- | --- | --- |
| play a note, pick a track, change the sound | 1 | a key, SELECT, PRESETS |
| write a note | 1 | a key (ROLL, stopped) |
| write a 4-note melody | 4 | four keys on ROLL: each one advances the cursor (5 from power-on, with SEQ) |
| set a note's length / move / transpose / nudge / velocity | 2 | hold its key, turn K3 / K1 / K2 / K4 / ALGORITHM |
| delete / duplicate a note | 2 | hold its key (it blinks), OCT− / OCT+ |
| zoom | 1 | ALGORITHM on ROLL |
| pattern length, speed | 2 | SEQ held, K3 / K4 |
| clear / double a pattern | 2 | SEQ held, OCT− / OCT+ |
| undo / redo | 2 | GLO held, OCT− / OCT+ |
| record live | 2 | REC, PLAY (or REC alone while playing) |
| filter cutoff / type | 2 | SEL, K1 / ALGORITHM |
| attack … release, track level | 2 | ENV, K1–K4 / ALGORITHM (or HOME, Kn) |
| engine | 2 | EDIT, ALGORITHM |
| an effect's amount | 2 | FX held, K1–K4 |
| reverb send | 2 | FX held, K4 |
| ⚑ choose an effect or turn it on / off | 2–5 | FX, then FX again up to 3 times to reach its row, ALGORITHM. The page reopens on the last row used, and the stack of rows is always drawn, so it is rarely more than 3 |
| ⚑ an effect's TONE / TIME / CHARACTER | 2–5 | as above, then K2–K4 |
| reverb type and macros | 2 | GLO, ALGORITHM / K1–K4 (VERB is GLO's first row) |
| ⚑ echo type and macros | 3 | GLO, GLO, ALGORITHM / K1–K4 |
| ⚑ swing, metronome, clock | 4 | GLO three times, K1–K4 (set once a song) |
| tempo | 2 | HOME, ALGORITHM |
| ⚑ every track's verb or echo send at once (the mixer) | 3–4 | HOME, HOME (VERB row) or HOME ×3 (ECHO), K1–K4. (One track's: FX held, K4, or the SPACE row) |
| mute a track | 2 | HOME held, one of the 4 lowest white keys |
| pick a progression; voicing, inversion, spread, strum | 2 | ARP, ALGORITHM / K1–K4 |
| ⚑ key and mode | 3 | ARP, ARP, K1 / K2 (set once a song) |
| write your own chord | 2 | ARP, a key |
| print the chords into a roll | 2 | ARP held, OCT+ |
| which track the chords play | 2 | SEQ held, ALGORITHM (PLAYS CHORDS) |
| save the song | 1 | SAVE |
| save a sound | 2 | EDIT held, SAVE |
| ⚑ save into another block | 4 | SAVE held 1 s, K1, SAVE, YES if it holds a song |
| ⚑ load a song | 3–4 | SAVE held 1 s, K1, PLAY, YES if the open song has unsaved edits |
| new song | 2 | HOME held, OCT− held 1 s |

The flagged ones are either set once a song (key, mode, swing, clock), protected on purpose (saving over and loading
from flash), or reach a deeper FX row; for that last one the held FX layer gives every row's AMOUNT in 2 presses.

**Against the two reference firmwares** (counted from their code, from power-on, by the firmware survey):

| Task | OberVave 1.5.3 | Clipper 1.1 | Demake |
| --- | --- | --- | --- |
| write a 4-note melody | 6 | 5–6 | **5** (SEQ, 4 keys) |
| change one note's length | 4 or more, or a 3-input combo | 3–6 (fails if the next step holds a note) | **2** |
| delete one note | 4 or more | 3–6 | **2** |
| filter cutoff | 1 | 1 | 2 |
| turn an effect on | 2 | 2 (an insert 4, the reverb type 8) | 2–5 |
| save | 2 | 4 and slot detents | **1** |

What made those two hard, and the rule here that avoids it:
- A button that is two buttons split by a timer, and a third state shown only by a toast. Here a tap is a page and
  a hold is a layer, and a layer always draws its map (rule 3).
- One encoder with four meanings, keys that mean other things in other views, and seven meanings on OCT+. Here each
  encoder has one job, and OCT− / OCT+ are only "take away" and "copy" (rules 2 and 4).
- Hidden state: a second track that starts switched off, pages that edit the other sound with only a colour change,
  modes that change how recording works. Here every track is always on, and the few settings that change how keys
  behave (KEYS, SNAP) sit on one SETUP row and show on screen (rule 6).
- Step entry two views deep, and 48–54 pages. Here the roll is one tap away and there are ten pages, one per button.
- Silent destructive holds (REC held 2 s clears a track), one level of undo. Here clearing is OCT− with a visible
  noun, a whole song needs a ring-filled hold, and there are 16 steps of undo (rule 5).
- OCT− + OCT+ held together is the update gesture in Clipper and AMB-1: Demake never uses the two together.

### 3.8 LEDs (on or off; brightness is a bonus on the web)

- **Keys:** lit = a tone of the chord sounding now (they move with the progression, so the harmony is visible on the
  keyboard). On ROLL, the keys of the notes under the cursor blink. With HOME held, the 4 lowest white keys are lit
  for the tracks that sound.
- **Page buttons:** the open page's button is lit; a held layer's button blinks.
- **PLAY** lit while playing, blinking on each bar's first beat. **REC** lit while recording, blinking when armed.
- **OCT− / OCT+**: lit when the octave is below / above 0.

## 4. The chord machine

### 4.1 Key, mode, progression

The song has one **key** (12) and one **mode**: AEOLIAN (default), DORIAN, PHRYGIAN, LYDIAN, MIXOLYDIAN, IONIAN,
plus two for melodies only (scale snap; the chord presets use the modes above): MINOR PENTATONIC, MAJOR PENTATONIC.
A **progression** is up to 16 slots; each slot is a chord (root as a degree of the mode, with an optional semitone
offset for borrowed roots; a quality; a bass) and a length (half-bars, 1–16). It loops on its own length, apart from
the tracks' patterns. The default song is A AEOLIAN, FIRST SNOW, 72 BPM.

### 4.2 The presets

Numerals are written the usual way, against the major scale (♭VI is a minor key's sixth); the example is in A,
and every chord was spelled out by script against its mode: the only tones outside the mode are the ones meant to
be (the G♯ of E7, the C♯ of A7, the line cliché's G♯ and F♯, the borrowed iv's F). Lengths are bars per chord.
All of them are common patterns (Open Music Theory's modal schemas,
<https://human.libretexts.org/Bookshelves/Music/Music_Theory/Open_Music_Theory_2e_(Gotham_et_al.)/07%3A_Popular_Music/7.12%3A_Modal_Schemas>;
the Royal Road, <https://en.wikipedia.org/wiki/Royal_road_progression>; the "Just the Two of Us" progression; the line
cliché; the Andalusian cadence, <https://en.wikipedia.org/wiki/Andalusian_cadence>); the voicings and names are ours.

| # | Name | Mode | Numerals (bars) | In A | Voicing |
| --- | --- | --- | --- | --- | --- |
| 1 | FIRST SNOW | Aeolian | i(add9) · ♭VImaj7 · ♭IIImaj7 · ♭VII(sus2), 1 each | Am(add9) · Fmaj7 · Cmaj7 · Gsus2 | WIDE |
| 2 | LAST TRAIN | Aeolian | i9 · iv9 · ♭VImaj7 · v7, 1 each | Am9 · Dm9 · Fmaj7 · Em7 | OPEN |
| 3 | FOGBOUND | Aeolian | ♭VImaj9 · ♭VII6 · i9, 1 · 1 · 2 | Fmaj9 · G6 · Am9 | WIDE |
| 4 | OVERPASS | Aeolian (Royal Road) | ♭VImaj7 · ♭VII7 · v7 · i7, 1 each | Fmaj7 · G7 · Em7 · Am7 | CLOSE, AUTO |
| 5 | VENDING LIGHT | Aeolian ("Two of Us") | ♭VImaj7 · V7 · i7 · v7–I7, 1 · 1 · 1 · ½+½ | Fmaj7 · E7 · Am7 · Em7–A7 | SHELL |
| 6 | BLUE HOUR | Dorian | i9 · IV9, 2 each | Am9 · D9 | OPEN |
| 7 | POWER LINES | Dorian | i11 · ♭VIImaj7 · ♭IIImaj7 · IV(add9), 1 each | Am11 · Gmaj7 · Cmaj7 · D(add9) | QUARTAL |
| 8 | TOWER BEACON | Phrygian | i7 · ♭IImaj7 · i7 · ♭vii7, 1 each | Am7 · B♭maj7 · Am7 · Gm7 | CLOSE |
| 9 | SLOW THAW | Lydian | Imaj9 · II(add9) · iii7 · II(add9), 1 each | Amaj9 · B(add9) · C♯m7 · B(add9) | WIDE |
| 10 | CANDLE | Aeolian (line cliché) | i · i(maj7) · i7 · i6, 1 each | Am · Am(maj7) · Am7 · Am6 | CLOSE, inner line |
| 11 | EMBERS | Aeolian | i9 · iv9 · i9 · ♭VImaj7, 1 each | Am9 · Dm9 · Am9 · Fmaj7 | OPEN |
| 12 | LOW CLOUDS | Aeolian, tonic pedal | i(add9) · ♭VImaj7/1 · ♭VII/1 · i(add9), 1 each | Am(add9) · Fmaj7/A · G/A · Am(add9) | WIDE, BASS PEDAL |
| 13 | SNOWFIELD | Aeolian | ♭VImaj7 · i(add9), 2 each | Fmaj7 · Am(add9) | WIDE |
| 14 | STATIC | Aeolian, no thirds | i(sus2) · ♭VII(sus2) · ♭VI(sus2) · ♭VII(sus2), 1 each | Asus2 · Gsus2 · Fsus2 · Gsus2 | OPEN |
| 15 | MEMORY CARD | Ionian, borrowed iv | Imaj9 · iii7 · IVmaj7 · iv6, 1 each | Amaj9 · C♯m7 · Dmaj7 · Dm6 | OPEN |
| 16 | NIGHT DRIVE | Aeolian (Andalusian, softened) | i(add9) · ♭VII6 · ♭VImaj7 · V7sus4–V7, 1 · 1 · 1 · ½+½ | Am(add9) · G6 · Fmaj7 · E7sus4–E7 | WIDE |
| U1–U4 | USER | any | your own (4.4) | | |

Ambient taste is built in: no bright dominant pull except where the pattern is the point (5, 16), 9ths and sus2 for
open colour, slow changes (1–2 bars, CHANGE stretches to ×4), and pedal or open-fifth options for drones.

### 4.3 Qualities and voicings

**Qualities** (K2 with a slot's key held, 4.4): the diatonic seventh (the default when you write a root), triad,
m7, m9, m11, m(add9), m6, m(maj7), maj7, maj9, 6/9, add9, sus2, sus4, 7sus4, 7, 9, m7♭5, dim, aug, 5 (no third).

**Voicings** (K1 on the VOICE row), our own rules over MIDI notes:

| VOICING | Rule |
| --- | --- |
| CLOSE | every tone within one octave above the root |
| OPEN | drop-2: CLOSE with the second voice from the top dropped an octave |
| WIDE | the root an octave below; then the 5th; the 3rd, 7th and 9th an octave up (the "ambient piano" spread) |
| SHELL | root, 7th (or 6th), 3rd above; the 9th on top when the chord has one |
| QUARTAL | stacked fourths from the root, each snapped to the mode (So What chords); the quality is ignored |
| CLUSTER | the chord's tones packed into the smallest span (seconds), for tension pads |

- **INVERSION** (K2): **AUTO** (default) picks the inversion whose voices move least from the last chord (the sum of
  semitone moves, ties to the lower), so any preset voice-leads smoothly; then 0, 1, 2, 3 fixed.
- **SPREAD** (K3): 0–3 octaves of extra width: the gaps between voices grow, the top voice kept under C7.
- **STRUM** (K4): −250 … +250 ms between successive voices; positive strums up, negative down, 0 together.
- **BASS** (a held slot's K4): ROOT, 3RD, 5TH or PEDAL (the key's tonic, whatever the chord).
- **RHYTHM** (KEY row K4): HOLD (each chord sustains its length), PULSE 1/4, PULSE 1/8 (re-struck), ARP UP, DOWN,
  UP-DOWN, RANDOM (at the playing track's SPEED).
- **CHANGE** (KEY row K3): every slot's length ×1/2, ×1, ×2, ×4.

At most 6 voices a chord (5 tones + a bass). The chord track's voices come from the voice pool (2.2).

### 4.4 Step your own

On CHORD, stopped: choose a USER progression with ALGORITHM (or edit a preset: the first edit copies it into the
next USER slot, named "USER 1 · FIRST SNOW"). Then the same gestures as the ROLL:

| Control | Action |
| --- | --- |
| a key | writes a chord whose root is that key (snapped to the mode) into the slot at the cursor, quality = the diatonic seventh, length = the last one; sounds it; the cursor moves to the next slot |
| a key held | K1 SLOT (move it), K2 QUALITY, K3 LENGTH, K4 BASS; OCT− deletes the slot, OCT+ duplicates it |
| keys together | an explicit chord: those notes exactly (snapped), kept as they were played; VOICING then only spreads it |
| ARP held + K1 | the slot cursor |
| playing + REC | keys play chords live (one key = one chord, as above) and are written into the slots at the bar they fall in |

### 4.5 Driving a track

Any track whose FOLLOW (SEQ held + ALGORITHM) is **PLAYS CHORDS** plays the progression: each slot voiced by
VOICING, INVERSION, SPREAD and BASS, struck with STRUM, in the RHYTHM. Its own ROLL then shows the chords as
blocks, read-only; ARP held + OCT+ prints them into it as ordinary notes (FOLLOW becomes FREE) for hand editing.
The default song has track 1 on PLAYS CHORDS (a pad), track 2 a melody on KEY, track 3 a bass on ROOT, track 4 a
pluck on CHORD.

### 4.6 Scale snap and chord following

Sets are 12-bit pitch-class masks.

- **S**, the scale: from the key and mode.
- **C**, the chord sounding at a given time: the progression's slot at that time, as pitch classes.
- **S′**, the scale following the chord: S, plus every tone of C that is not in S, each replacing the scale tone a
  semitone from it (E7 in A Aeolian: G♯ replaces G; A7: C♯ replaces C). With no chord, S′ = S.
- **Snap(p, set)**: the nearest pitch whose class is in the set; a tie goes to a chord tone, else down.

| Where | What happens |
| --- | --- |
| a key played or recorded | its pitch becomes Snap(p, S′) at the time it is written (the cursor's time when stopped, the playhead's when recording). Keys that are already in S′ play as they are |
| FOLLOW FREE | the notes play as written |
| FOLLOW KEY (default) | each note plays as Snap(p, S′) of the chord sounding at the note's start, so a melody written over Am follows the G♯ of an E7 |
| FOLLOW CHORD | each note plays as Snap(p, C): every note lands on a chord tone, so a melody becomes an arpeggio of each chord |
| FOLLOW ROOT | the pattern moves with the chord: each note shifts by the scale steps from the progression's first root to the root now, then Snap(p, S′). For basslines and ostinati |
| FOLLOW PLAYS CHORDS | the track plays the progression (4.5) |
| the ROLL | with SNAP on, one row per tone of S; rows of C at the cursor are marked; the root's rows are labelled in ink. A note outside S (a G♯ written over E7) sits on the row of the tone it replaces, marked ♯ |
| the LEDs | the keys of C now are lit |

Notes are stored as written (absolute pitch); following happens at playback, so turning FOLLOW to FREE gives back
exactly what was written. GLO > SETUP > KEYS: **PIANO** (default: the keys as printed, snapped), **SCALE** (the 16
white keys are the mode's degrees from the tonic up, the 11 black keys the chord's tones: every key fits), **FREE**
(no snap; the ROLL shows all twelve rows).

### 4.7 Data

A slot is 4 bytes: root degree (3 bits) and offset (2), quality (5), bass (2), length (4), flags. A progression is
16 slots, 64 bytes; the 16 presets are a table in flash, the 4 USER progressions are saved with the song.

## 5. The screen

### 5.1 The look, from the moodboard

Two families of images: a soft cream-and-pastel plugin UI (paper cards, rounded knobs, presets as physical media,
iridescent pastel gradients), and snowy late-90s console scenes (fog, dithered textures, low-poly silhouettes, one
warm light in the dark: a campfire, headlights, a tower's beacon, a vending machine; a crisp white pixel HUD font
with a dark outline).

**Taken:** fog and snow as the texture, ordered dithering as the grain, **one warm light** as the only accent, a pixel
HUD font for small text, a soft grotesk for names and values, presets as save blocks on a memory card with little
animated icons.
**Left:** the plugin's layout, its floppy-disk labels, logos and Japanese subtitles (its signature, too close to
copy), and the game's characters and vehicles. No brand, console or product names or marks anywhere in the product.

### 5.2 Pixels: four tones, dithered

The FM-1's LCD is colour (RGB565). Demake draws in **2 bits a pixel, four tones**: paper, fog, ink and light. The
framebuffer is 240 × 240 × 2 bits = 14,400 B (in the pool). Each 8-line strip that changed goes out through a 4-entry
RGB565 table (3,840 B of strip buffer), as MONO2K's 1-bit strips do; the SPI cost per strip is the same.

**Dither:** a 4 × 4 Bayer matrix between neighbouring tones (paper ↔ fog, fog ↔ ink) gives 16 steps per pair: fog
gradients, filled curves, muted lanes, the pictures. **Never behind small text:** text sits on flat paper or flat fog.
**The light tone** marks one thing at a time (the cursor or the playhead, the chord now, REC), at most two places on
a screen, as a single lamp in the fog.

| THEME (GLO > SETUP) | paper | fog | ink | light |
| --- | --- | --- | --- | --- |
| SNOW (default) | `#D8D6D0` | `#9A9893` | `#2A2B2F` | `#F0A33C` amber |
| NIGHT | `#1B1C20` | `#45464D` | `#E2DED6` | `#F29A36` ember |
| VENDING | `#0E1428` | `#28397E` | `#D2DAFF` | `#73A0FF` blue |
| PEACH | `#F1E5D6` | `#DDB197` | `#3A2E29` | `#C9623F` terracotta |

The values are first picks from the images, to be checked on the FM-1's panel (its gamma and viewing angle) and in
the browser.

### 5.3 Type

| Role | Face | Size | Rendering |
| --- | --- | --- | --- |
| labels: knob names, row names, note names, hints | **PIX**, our own pixel font (5 × 7 caps, digits, ♭ ♯ ▶ ■ ● ↑ ↓; advance 6 px), drawn as ASCII art in `tools/gen_font.py` | 7 px | 1 bit; an outlined variant (1 px of ink or paper around) for text over pictures, the HUD look |
| values in the knob cells, the header's page and sound | the display face, SemiBold | 15 px | 2-bit, with fog as the anti-aliasing tone |
| the big name or chord on a stage, the boot | the display face, Bold | 28 px (36 on the boot) | 2-bit |

One size per role, every capital between the same two rows (the owner asked for this consistency on MONO2K).
Text is fitted with `text_fit` (tighter tracking, then a shorter string), never guessed.

**The display face: Tomato Grotesk, if its licence allows.** The files read "Copyright © 2020 Andrea Biggio & The
Designers Foundry. All rights reserved" (their name table). Shipping glyphs rendered from it inside a distributed
firmware, and drawing it on a public website, needs a licence from The Designers Foundry that covers apps or
embedding; the font files themselves never go into the repo (they are git-ignored with the rest of `reference/`).
If the owner's licence does not cover it, the fallback is an OFL face with the same wide, soft shapes; candidates to
compare in renders: Bricolage Grotesque and Familjen Grotesk (both on Google Fonts under the OFL; check the licence
file when one is chosen). The generator renders whichever face is installed locally, so the choice changes no code.

### 5.4 The grid

| Band | Rows (px) | What is there | Rule |
| --- | --- | --- | --- |
| HEADER | 0–15 | the four track tabs (the current one inverted), the page · the sound's name, ▶ ■ ●, the tempo, the chord now (light) | a fog gradient behind, text on a flat strip |
| KNOBS | 16–55 | four cells of 60 px, in K1–K4's order, at the knobs' height | label (PIX) on top, value (display 15) below, a 1-px bar of the value at the cell's foot |
| STAGE | 56–215 | the page's picture: the roll, the lanes, a curve, the chain, the chord | held windows (maps, the memory card, questions) sit over it with a 2-px shadow |
| FOOTER | 216–239 | the ALGORITHM cell (left, 88 px: label and value), then what OCT− / OCT+ do now | hints change with what is held ("− DEL  + DUP") |

The empty shell, its bands and the text audit come first (build phase 0), before any page.

### 5.5 Icons and motion

- **Save-block icons:** 16 × 16, three frames, four tones, our own pixel art (ASCII art in `tools/gen_icons.py`):
  snowflake, pine, beacon tower, campfire, vending can, power line, cassette, floppy, moon, cloud, bell, key, string,
  flute, wave, truck. One per sound category and one per song; they cycle at 4 frames a second where they are shown.
- **Snow:** on HOME while playing, up to 12 one-pixel flakes drift down the fog of the header and the lanes' margins.
- **Page change:** a four-frame dither dissolve.
- **Boot:** the wordmark resolves out of the fog while snow falls, ~1.5 s, skipped by any key.
- **Saving:** "SAVING… DON'T SWITCH OFF" with a block bar filling.

### 5.6 Sketches

One character is about 6 × 8 px; `▓` a note, `█` a held note, `▌` the cursor (light), `░ ▒` dither, `•` a chord
tone's row. The real ROLL has 20 rows of 8 px on its stage; the sketches show fewer.

**ROLL, stopped, nothing held**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  ROLL · FELT PIANO    ■  72 │
│░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░ Am9  │
├─────────┬─────────┬─────────┬──────────┤
│CURSOR   │VIEW     │LENGTH   │FINE      │
│ 1.3     │ A3–C5   │ 1/8     │ 0        │
├────┬────┴─────────┴─────────┴──────────┤
│    │1·······2·······3·······4·······   │
│ C5•│                                   │
│ B4•│                                   │
│▐A4▌│▓▓▓▓▓▓▓▓            ▓▓▓▓           │
│ G4•│                                   │
│ F4 │                                   │
│ E4•│        ▓▓▓▓                       │
│ D4 │            ▌                      │
│ C4•│            ▌       ▓▓▓▓▓▓▓▓       │
│ B3•│            ▌                      │
│▐A3▌│▓▓▓▓▓▓▓▓▓▓▓▓▌▓▓▓▓▓▓▓               │
├────┴───────────────────────────────────┤
│ZOOM 16   ▕██▒▒▒▒▒▒▏         A AEOLIAN  │
└────────────────────────────────────────┘
```

The footer's strip is the whole pattern (64 steps, 1 px each) with the visible window in ink.

**ROLL, a key held (A4 at step 1.1)**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  ROLL · FELT PIANO    ■  72 │
│░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░▒▒░░ Am9  │
├─────────┬─────────┬─────────┬──────────┤
│MOVE     │PITCH    │LENGTH   │NUDGE     │
│ 1.1     │ A4      │ 2/4     │ +1/8     │
├────┬────┴─────────┴─────────┴──────────┤
│    │1·······2·······3·······4·······   │
│ B4•│                                   │
│▐A4▌│████████            ▓▓▓▓           │
│ G4•│                                   │
│ F4 │                                   │
│ E4•│        ▓▓▓▓                       │
├────┴───────────────────────────────────┤
│VELOCITY 96            − DEL   + DUP    │
└────────────────────────────────────────┘
```

**HOME**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  HOME                 ▶  72 │
│░░▒▒░░  ·    ░░▒▒░░▒▒   ·  ░░▒▒░░▒ Am9  │
├─────────┬─────────┬─────────┬──────────┤
│LEVEL 1  │LEVEL 2  │LEVEL 3  │LEVEL 4   │
│ 80      │ 64      │ 72      │ 50       │
├─────────┴─────────┴─────────┴──────────┤
│1 ✻ SNOW PAD     CHORDS ▬▬▬▬▬▬▬│▬▬▬▬▬▬  │
│2 ♪ FELT PIANO   KEY    ·▬·▬▬··│▬·▬··▬  │
│3 ▲ TOWER DRONE  ROOT   ▬▬▬▬▬▬▬│▬▬▬▬▬▬  │
│4 ✶ GLASS PLUCK  CHORD  ░░▒░░▒░│░▒░░▒░  │
│                                        │
│ Am(add9) │ Fmaj7 │▐Cmaj7▌│ Gsus2       │
├────────────────────────────────────────┤
│TEMPO 72     FIRST SNOW · A AEOLIAN     │
└────────────────────────────────────────┘
```

Each lane: the sound's icon and name, its FOLLOW, its pattern in miniature with the playhead (light). Below: the
progression with the chord now lit. Tapping HOME again turns the knob row into VERB, ECHO and PAN for the four tracks.

**SOUND (SAMPLE)**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  SOUND · FELT PIANO   ▶  72 │
├─────────┬─────────┬─────────┬──────────┤
│SAMPLE   │START    │LOOP     │GRIT      │
│ 03 FELT │ 12 ms   │ SHORT/4 │ 22K+ADPCM│
├─────────┴─────────┴─────────┴──────────┤
│  ▁▃▆█▇▆▅▅▄▄▄▃▃▃▃▃▂▂▂▂▂▂▂▂▁▁▁▁▁▁▁       │
│  ▲              ▕▒▒▒loop▒▒▒▏           │
│                                        │
│  ┌──┐ KEYS · C4 · 22.05 kHz · 1.2 s    │
│  └──┘                                  │
├────────────────────────────────────────┤
│ENGINE SAMPLE   PAD · DRONE · PLUCK     │
└────────────────────────────────────────┘
```

**FILTER** and **AMP**

```
┌────────────────────────────────────────┐  ┌────────────────────────────────────────┐
│▐1▌ 2  3  4  FILTER · SNOW PAD    ▶  72 │  │▐1▌ 2  3  4  AMP · SNOW PAD       ▶  72 │
├─────────┬─────────┬─────────┬──────────┤  ├─────────┬─────────┬─────────┬──────────┤
│CUTOFF   │RESO     │ENV AMT  │DRIFT     │  │ATTACK   │DECAY    │SUSTAIN  │RELEASE   │
│ 1.2 kHz │ 30      │ +40     │ 20       │  │ 1.4 s   │ 2.0 s   │ 80      │ 4.5 s    │
├─────────┴─────────┴─────────┴──────────┤  ├─────────┴─────────┴─────────┴──────────┤
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▄▅▆▄▃▂▁                │  │           ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄              │
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▂▁              │  │       ▄▀▀              ▀▄              │
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▓▂▁           │  │    ▄▀                    ▀▄▄           │
│                ▲ cutoff, drifting      │  │  ▄▀          ● now           ▀▀▄▄▄     │
├────────────────────────────────────────┤  ├────────────────────────────────────────┤
│TYPE LP24   LP12 · BP · HP · OFF        │  │LEVEL 80                                │
└────────────────────────────────────────┘  └────────────────────────────────────────┘
```

**FX**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  FX · FELT PIANO      ▶  72 │
├─────────┬─────────┬─────────┬──────────┤
│AMOUNT   │TONE     │TIME     │CHARACTER │
│ 40      │ dark 30 │ wow 0.6 │ flutter 2│
├─────────┴─────────┴─────────┴──────────┤
│ COLOR    ● CRUSH         ▮▮▯▯▯▯        │
│▐MEDIA    ● TAPE          ▮▮▮▮▯▯       ▌│
│ MOTION   ○ CHORUS (off)  ▮▮▮▯▯▯        │
│ SPACE      VERB 30 · ECHO 10 · PAN C   │
├────────────────────────────────────────┤
│EFFECT TAPE    CASSETTE · VHS · OFF     │
└────────────────────────────────────────┘
```

**CHORD**

```
┌────────────────────────────────────────┐
│▐1▌ 2  3  4  CHORD · FIRST SNOW   ▶  72 │
├─────────┬─────────┬─────────┬──────────┤
│VOICING  │INVERT   │SPREAD   │STRUM     │
│ WIDE    │ AUTO    │ 2 oct   │ ↑ 60 ms  │
├─────────┴─────────┴─────────┴──────────┤
│              Am(add9)                  │
│ ┌─────────┬─────────┬─────────┬──────┐ │
│ │▐i add9 ▌│VI maj7  │III maj7 │VII s2│ │
│ └─────────┴─────────┴─────────┴──────┘ │
│  ▕ ▏▕ ▏▕█▏▕ ▏▕ ▏▕█▏▕ ▏▕ ▏▕█▏▕ ▏▕█▏▕ ▏  │
├────────────────────────────────────────┤
│PROG FIRST SNOW       A AEOLIAN · T1    │
└────────────────────────────────────────┘
```

The keyboard strip shows the voicing's notes over two octaves; the slot playing (or the cursor, stopped) is lit.

**MEMORY CARD** (SAVE held 1 s)

```
┌────────────────────────────────────────┐
│  MEMORY CARD 1               11 FREE   │
├────────────────────────────────────────┤
│   ┌──┐  ┌──┐  ┌──┐  ┌──┐  ┌──┐         │
│   │✻ │  │▲ │  │♪ │  │✶ │  │  │         │
│   └──┘  └──┘  └──┘  └──┘  └──┘         │
│   ┌──┐  ┌──┐  ┌──┐  ┌──┐  ┌──┐         │
│   │  │  │  │  │  │  │  │  │  │         │
│   └──┘  └──┘  └──┘  └──┘  └──┘         │
│   (3 rows of 5: 15 blocks)             │
│                                        │
│ ▶ FIRST SNOW · 4 tracks · 72 BPM       │
├────────────────────────────────────────┤
│SAVE here   PLAY load   K2 card 1 / 2   │
└────────────────────────────────────────┘
```

Card 2 holds the demo songs (read-only); a demo saved goes into a block of card 1.

### 5.7 The text audit

MONO2K's audit carries over to the 2-bit buffer: every text records its ink box, a fill hides what it covers, and
the check reports texts that touch or sit a dot apart, text cut by a clip or the screen, and text past its window,
over the whole host tour and a sweep of every page, engine, effect, knob value and held map. No overlapping text,
ever; screenshots are still looked at for text crowding a line.

## 6. Build order

Each phase ends with something the owner plays on the FM-1, and nothing is kept before it is heard (WAVs and numbers
first, as on MONO2K).

**Phase 0: groundwork.**
- The repo as MONO2K's: `firmware/src` (the core, a unity build, integer only), `firmware/gen` (generated tables,
  fonts, icons, samples), `firmware/fm1` (the platform from MONO2K's, with its `LICENSES/`), `host/`, `web/`, `tools/`.
- A small API (`dmk.h`: init, render, key, button, encoder, midi, frame in 2 bits, leds) and the three homes: host
  harness, browser build (wasm in an AudioWorklet), FM-1 package.
- The names (2.7): identity range, SysEx magic, store region.
- A sine on the FM-1 and the `--diag` load meter; **measure the ratio of host instructions to FM-1 load once** with a
  known kernel, so section 1's budget is checked against the device, not only the yardstick.
- The screen shell: the 2-bit framebuffer, the four themes, the strips, PIX and the display face generated, the bands,
  the text audit and the tour.

**Phase 1: the smallest playable version (one track).**
- SAMPLE engine (block ADPCM, loops, its voices) and `tools/gen_samples.py` with 4 of the owner's samples, the owner picking from WAVs (2.5).
- SOUND (SAMPLE only), FILTER, AMP.
- ROLL: cursor, write, hold to edit (MOVE, PITCH, LENGTH, NUDGE, VELOCITY), delete, duplicate, chords at the cursor,
  zoom 16 / 32 / 64, the blinking keys, scale snap in a fixed key, undo and redo (16 steps).
- PLAY, REC: live recording quantized, overdub, one undo per take.
- FX: TAPE on the MEDIA row; one shared VERB (ROOM), its send on the SPACE row.
- SAVE: one song in flash.
- Tests: every gesture of 3.7 in the host self-test; the CPU on the FM-1; the owner writes a 4-bar melody without
  reading anything.

**Phase 2: chords.**
- KEY and MODE, the 16 presets, slots, VOICING, INVERSION (AUTO), SPREAD, STRUM, RHYTHM, CHANGE, BASS; step your own;
  print to the roll.
- FOLLOW (KEY, CHORD, ROOT, PLAYS CHORDS), the key LEDs.
- A second track and the PAD engine, so a chord pad and a melody can follow each other.

**Phase 3: four tracks.**
- Four tracks with their own length, speed, shift, transpose; HOME (lanes, mixer, mutes, tempo); the voice pool; the
  second core (2.2); the DRONE and PLUCK engines; GRIT.
- The memory card (15 songs, the demos card), the sound library (factory and USER), the store and the link.
- Exit: four tracks at full voices within the budget, on the FM-1's meter.

**Phase 4: the whole FX set.**
- COLOR: SATURATE, DISTORT, CRUSH, CODEC, NOISE. MEDIA: CASSETTE, VHS. MOTION: CHORUS, VIBRATO, DRIFT.
- ECHO bus: TAPE, PING-PONG, FILTERED. VERB bus: HALL, SPACE, PLATE, SPRING, SHIMMER (ROOM came in phase 1).
- The LFO page.
- Each effect: WAVs of its macro corners for the owner, every control on a law measured over its whole range, its
  CPU measured against section 1's estimate (the table is updated with the measurement).

**Phase 5: release.**
- The web: emulator, manual (screens from the host tour), installer (Felucca's files), change log.
- Factory songs and sounds (the owner picks), the Patch Editor (songs, sounds, user samples into the data area).
- A package tried on the owner's FM-1 before anything is published.

## Appendix A. Sources, notices and references

**`LICENSES/`** gets each notice when the first line of code under it lands:

- The platform's notices, as MONO2K's `firmware/fm1/LICENSES/` has them: CS-1, Felucca, SLOOP, Dinghy, FM-106,
  fm1-velocity (GPL-3.0). The JieLi AC79 SDK's three package files (Apache-2.0) are read from the SDK at build time
  and are not in the tree.
- `MIT-eurorack.txt`: eurorack has no LICENSE file, so its MIT notice is taken from the header of the files used
  ("Copyright 20xx Emilie Gillet"), and each ported file keeps its own header. Its README asks that "Mutable
  Instruments" not name derivative works; nothing here does. Its AVR projects are GPL and not used.
- `MIT-stmlib.txt` (stmlib's LICENSE); `MIT-DaisySP.txt` (DaisySP's LICENSE: Electrosmith, Emilie Gillet for Plaits,
  Paul Batchelor for Soundpipe). `LGPL-2.1-DaisySP-LGPL.txt` only if an LGPL file (ReverbSc) is used as more than a
  reference.
- `MIT-Felucca-Engines.txt` (softclip, the SVF, noise, drift). Felucca's GPL-3.0 files keep their headers;
  `phys_dsp.c` and `phys_symp.c` bring their MIT notices for DaisySP and Rings.
- Fonts: the display face's licence (the OFL file, or the terms of the owner's Tomato Grotesk licence). PIX and the
  icons are our own.
- The owner's samples and single-cycle waves, under the licence the owner chooses (decision 4).

**Not taken:** Hügelton's assets (icons, panel photo, drum pack: all rights reserved), the names Felucca and
Hügelton (trademarks), any sample, font or asset of the reference firmwares.

**References, never sources of code or data:**
- Arturia Pure LoFi: its web pages, and its screenshots in the moodboard.
- Sonicware Liven Ambient 0: its web pages and reviews. Its firmware file in `Git/ambient zero/` stays unopened.
- The console's sound chip: only the public psx-spx documentation, for the structure of its ADPCM blocks and its
  reverb's rate and shape. Its preset register values are not used.
- The moodboard's game screenshots, for mood only.
- Open Music Theory and the other harmony sources, for common progressions (patterns, not works).

## Appendix B. The samples, each with its source

All 64 are the owner's recordings, made with their pedals (some named after the presets that inspired them). The
table gives the planned cut and size; ★ marks the 16 proposed first for bank A. Rates come from the measured
bandwidth; "loop" is the best seam found in the attack-and-loop cut; one-shots keep their whole decay.

| # | File | Character | Pitch | Rate | Cut | Size | Source |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 01 | 808 Buzz | keys | C3+24 | 16 kHz | loop 0.67–0.97 s | 8.7 KiB | the owner's recording |
| 02 ★ | A Forest Of Tiny Things | pluck | F2−28 | 11.025 kHz | one-shot | 12.3 KiB | the owner's recording |
| 03 ★ | Acoustic Bass | pluck | C2+1 | 11.025 kHz | one-shot | 9.4 KiB | the owner's recording |
| 04 | Additive Monsoon | pad | C4+3 | 11.025 kHz | loop 0.32–0.98 s | 6.1 KiB | the owner's recording |
| 05 | An Isolated Illusion | pad, slow attack | C4+12 | 22.05 kHz | loop 1.34–1.95 s | 24.0 KiB | the owner's recording |
| 06 | Anahit's Dream | pad, slow attack | F2+3 | 11.025 kHz | loop 0.43–0.89 s | 5.5 KiB | the owner's recording |
| 07 | Autumn Stroll | pad | C4−16 | 11.025 kHz | loop 0.62–0.98 s | 6.0 KiB | the owner's recording |
| 08 | Avenoir | pad, slow attack | C4+4 | 11.025 kHz | loop 0.97–1.49 s | 9.2 KiB | the owner's recording |
| 09 ★ | Blue Fog | pad, slow attack | C3+1 | 11.025 kHz | loop 0.52–0.96 s | 6.0 KiB | the owner's recording |
| 10 ★ | Blurred Memories | pad, slow attack | C4+22 | 11.025 kHz | loop 0.53–0.84 s | 5.2 KiB | the owner's recording |
| 11 ★ | Broken Lullaby | pad, slow attack | C4+0 | 11.025 kHz | loop 0.41–0.82 s | 5.1 KiB | the owner's recording |
| 12 | Broken World | sustained | C2−8 | 11.025 kHz | loop 0.10–0.98 s | 6.1 KiB | the owner's recording |
| 13 | Candlelight | keys | C4+25 | 11.025 kHz | loop 0.52–0.94 s | 5.8 KiB | the owner's recording |
| 14 | Casiotone | pad, slow attack | C4−4 | 22.05 kHz | loop 0.88–1.46 s | 18.0 KiB | the owner's recording |
| 15 | Circuit's Anima | keys | C2−2 | 11.025 kHz | loop 0.60–0.91 s | 5.6 KiB | the owner's recording |
| 16 | Clouded View | pad, slow attack | C3−7 | 11.025 kHz | loop 0.97–1.50 s | 9.2 KiB | the owner's recording |
| 17 ★ | Cold Clouds | pad, slow attack | C4+5 | 11.025 kHz | loop 0.52–0.87 s | 5.4 KiB | the owner's recording |
| 18 | Cold Serenity | pad, slow attack | C5+2 | 16 kHz | loop 1.27–1.96 s (seam poor: crossfade) | 17.5 KiB | the owner's recording |
| 19 | Dance Without Moving | sustained | C4+2 | 11.025 kHz | loop 0.45–0.91 s | 5.7 KiB | the owner's recording |
| 20 | Decade Sub | pad, slow attack | C3+11 | 11.025 kHz | loop 0.40–0.99 s | 6.2 KiB | the owner's recording |
| 21 | Deckard's Dusty Tapes | pad, slow attack | C4+3 | 11.025 kHz | loop 1.21–1.94 s (seam poor: crossfade) | 12.0 KiB | the owner's recording |
| 22 | DeGaussian Blur | sustained | C3−2 | 11.025 kHz | loop 0.14–0.76 s | 4.7 KiB | the owner's recording |
| 23 | Desert Nights | pad | C3+0 | 11.025 kHz | loop 0.43–0.80 s | 5.0 KiB | the owner's recording |
| 24 | Digital Eden | pad, slow attack | C4−8 | 11.025 kHz | loop 0.66–0.99 s | 6.1 KiB | the owner's recording |
| 25 | Dream Of Me | pad, slow attack | C4−2 | 16 kHz | loop 0.68–0.99 s | 8.9 KiB | the owner's recording |
| 26 | Dudukwinds | pad, slow attack | C4−12 | 11.025 kHz | loop 0.47–0.98 s | 6.1 KiB | the owner's recording |
| 27 | Eastern Entrance | pad, slow attack | C4−2 | 11.025 kHz | loop 0.68–0.99 s | 6.1 KiB | the owner's recording |
| 28 | Faker's Movement | pad, slow attack | C4+1 | 11.025 kHz | loop 0.68–1.81 s | 11.2 KiB | the owner's recording |
| 29 ★ | Faraway Thoughts | pad, slow attack | C4+10 | 11.025 kHz | loop 0.54–0.97 s | 6.0 KiB | the owner's recording |
| 30 | Felt Echoes | pluck | C4−0 | 11.025 kHz | one-shot | 12.3 KiB | the owner's recording |
| 31 | Fifth Dimension | pad, slow attack | C2−0 | 16 kHz | loop 0.68–1.00 s | 8.9 KiB | the owner's recording |
| 32 | Forgotten Sampler | pad, slow attack | C3−14 | 22.05 kHz | loop 0.92–1.45 s | 17.8 KiB | the owner's recording |
| 33 | Fusuri Brothers | pad, slow attack | C4−1 | 11.025 kHz | loop 1.00–1.50 s | 9.2 KiB | the owner's recording |
| 34 | Gemstone | pad, slow attack | C4−0 | 22.05 kHz | loop 0.60–0.92 s | 11.4 KiB | the owner's recording |
| 35 | Ghost In The Wires | sustained | C3+8 | 11.025 kHz | loop 0.30–0.97 s | 6.0 KiB | the owner's recording |
| 36 | Grand Padness | pad, slow attack | C3+18 | 11.025 kHz | loop 0.54–0.94 s | 5.8 KiB | the owner's recording |
| 37 | It Blossoms | pad | C3+14 | 11.025 kHz | loop 0.61–0.91 s | 5.6 KiB | the owner's recording |
| 38 | Lost Dream | sustained | C3+7 | 11.025 kHz | loop 0.58–0.91 s | 5.6 KiB | the owner's recording |
| 39 | Lunar Phase | sustained | C2+5 | 11.025 kHz | loop 0.69–1.00 s | 6.2 KiB | the owner's recording |
| 40 | Midfielder | keys | C4+20 | 16 kHz | loop 0.66–0.99 s | 8.8 KiB | the owner's recording |
| 41 ★ | Mindful Resonance | keys | C4+5 | 11.025 kHz | loop 0.44–0.74 s | 4.6 KiB | the owner's recording |
| 42 | Nyquist Keys | sustained | C4−1 | 11.025 kHz | loop 0.69–1.36 s | 8.4 KiB | the owner's recording |
| 43 ★ | Panavision | pad, slow attack | C4+20 | 11.025 kHz | loop 0.38–0.92 s | 5.7 KiB | the owner's recording |
| 44 ★ | Piano Quiver | pluck | C4+1 | 16 kHz | one-shot | 17.9 KiB | the owner's recording |
| 45 | Pipes At Dawn | pad, slow attack | C5+4 | 22.05 kHz | loop 0.62–0.95 s | 11.7 KiB | the owner's recording |
| 46 | Plastic Ball | pad, slow attack | C3−3 | 11.025 kHz | loop 1.32–1.96 s | 12.1 KiB | the owner's recording |
| 47 | Polar | pad, slow attack | C4+6 | 11.025 kHz | loop 1.30–1.93 s | 11.9 KiB | the owner's recording |
| 48 ★ | Purple Eterea | pad, slow attack | C3−2 | 16 kHz | loop 0.68–0.99 s | 8.8 KiB | the owner's recording |
| 49 ★ | Rainforest Bansuri | pad, slow attack | C4−2 | 11.025 kHz | loop 0.63–0.96 s | 5.9 KiB | the owner's recording |
| 50 | Raspberry Fields | sustained | C4+1 | 11.025 kHz | loop 0.42–0.73 s | 4.5 KiB | the owner's recording |
| 51 ★ | Reversed Breeze | pad, slow attack | C4+0 | 11.025 kHz | loop 0.97–1.42 s | 8.8 KiB | the owner's recording |
| 52 ★ | Rusty Keys | keys | C4−2 | 16 kHz | loop 0.15–0.73 s | 6.5 KiB | the owner's recording |
| 53 ★ | Simba Keys | keys | C4+1 | 11.025 kHz | loop 0.38–0.93 s | 5.7 KiB | the owner's recording |
| 54 | Smiling Onion | pad, slow attack | C3+4 | 11.025 kHz | loop 0.92–1.39 s | 8.6 KiB | the owner's recording |
| 55 | Snow Ghosts | sustained | C4+14 | 11.025 kHz | loop 0.43–0.96 s | 6.0 KiB | the owner's recording |
| 56 | Somber Mood | pad, slow attack | C3+10 | 11.025 kHz | loop 1.21–1.81 s (seam poor: crossfade) | 11.2 KiB | the owner's recording |
| 57 ★ | Surprise Me Piano | sustained | C4−0 | 11.025 kHz | loop 0.50–0.97 s | 6.0 KiB | the owner's recording |
| 58 | Svalbard | pad | C5−5 | 11.025 kHz | loop 0.55–0.91 s | 5.6 KiB | the owner's recording |
| 59 | Synthetic Sun | pad, slow attack | C4−20 | 11.025 kHz | loop 0.94–1.44 s | 8.9 KiB | the owner's recording |
| 60 | The Beekeepers | pad, slow attack | C4−12 | 16 kHz | loop 1.25–1.97 s | 17.6 KiB | the owner's recording |
| 61 | The Observers | sustained | C3−12 | 11.025 kHz | loop 0.56–0.87 s | 5.4 KiB | the owner's recording |
| 62 | Vast Garden | pad, slow attack | C5+4 | 16 kHz | loop 0.36–0.79 s | 7.1 KiB | the owner's recording |
| 63 | Wave From Another Sea | sustained | C3+5 | 11.025 kHz | loop 0.30–0.92 s | 5.7 KiB | the owner's recording |
| 64 | World's Edges | pad, slow attack | C3−3 | 11.025 kHz | loop 0.57–0.99 s | 6.1 KiB | the owner's recording |

Every sample gets a new factory name (12 characters) chosen by the owner, from WAVs of the cut and encoded versions (phase 1).
