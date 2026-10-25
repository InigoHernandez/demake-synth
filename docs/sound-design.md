# Demake: more sound design and effects

The next plan for the sound, after 0.2 (2026-10-24). The owner's direction: **only the samples engine, on the
owner's curated recordings**. No wavetable, string or drone engines. Everything here shapes the 64 recordings or
the space around them. As always, nothing is kept before the owner hears it: each step ends with WAVs, then a build
on the FM-1.

Marks: [M] measured, [E] estimated. Costs are host instructions a sample, counted as `docs/plan.md` 1.1 counts
them.

## 0. In one page

- **What exists:** one recording a sound, its filter (8 types) and envelope. Three effect rows a synth:
  - Colour: Saturate, Crush, Lossy, Noise.
  - Media: Tape, Cassette, VHS.
  - Motion: Chorus, Vibrato, Drift.

  Two shared buses:
  - The verb: Room, Hall, Space, Haze, Bloom, Lofi.
  - The echo: Tape or Grains.
- **What's missing to design sounds with:**
  - velocity (stored, never heard);
  - the LFO (drawn, never applied);
  - the layer (a second recording under a sound);
  - glide;
  - the console rate (Grit).
- **What the effects could add:**
  - more lofi media: vinyl, radio, a dictaphone;
  - richer motion: an ensemble, a phaser, a flanger, tremolo;
  - the ambient staples on the buses: shimmer, freeze, ping-pong, reverse;
  - a song-wide patina: a master tape stage, a tempo pump, a tape stop.
- **Factory sounds:** today PRESETS steps through bare recordings. Each recording should come with a designed sound
  that the owner picks and names from sheets.
- **Scenes and the song mode** (built): each synth's eight patterns, sixteen scenes that combine them, and a song of up
  to 128 steps, typed from the scene keys or recorded live; one synth can harmonize another.
- **The budget is the limit for the sound, not ideas:**
  - CPU0 is at 1503 in the heaviest case (1818 before the third synth's chain moved to the second core), under the
    proven 1650 a core. The calibration puts a core near 3470, but the FM-1 hasn't been measured.
  - The RAM pool has 28 KB free.
  - Stage 1 measures the FM-1, moves work to the second core and frees RAM before anything heavy is added.
- **Order:** seven stages, below. Section 10 lists what is the owner's to decide.

## 1. Where it is

| Part | Built | Cost now |
| --- | --- | --- |
| Voice | ADPCM recording, Q16 pitch, SVF (LP24 LP12 HP BP Notch Peak Vowel Off), ADSR | 12 voices: ~900 in the heaviest case [M] |
| Colour | Saturate, Crush (with its output stage), Lossy (3 bands, holes), Noise (hiss, crackle, hum), Warm, Distort | Noise 86, Lossy 75, Crush 32 [M] |
| Media | Tape, Cassette (dropouts glide), VHS (smooth tracking), Vinyl, Radio, Dictaphone | VHS 122, Cassette 119, Tape 82 [M] |
| Motion | Chorus (2 taps, stereo), Vibrato, Drift, Ensemble, Phaser, Flanger, Tremolo | Flanger 69, Chorus 61 [M] |
| Verb | Room (combs), Hall, Space, Haze, Bloom, Lofi (8-line FDN on the second core) | Haze 381 [M] |
| Echo | Tape (mu-law, 1.5 s), Grains (6, octaves and fifths) | — |
| Mix | DC blocker, half level through the effects, soft limiter, fades on every type change | — |

- **The heaviest case** (Lossy, VHS and Chorus on all three, Haze, Grains):
  - CPU0 1503 (its wait for the second included), CPU1 1143 [M]: each core runs half the voices; the first does two
    synths' chains and the mix, the second the verb's network and the third synth's chain.
- **RAM:** pool 316 KB of 344 KB, RAM 48 KB of 96 KB.
- **Flash:** 496 KB of 582 KB, so 85 KB left for code and tables. The recordings are already in.

## 2. Stage 1: measure, balance, make room

1. **Measure the FM-1 once.**
   - The diagnostic build (`tools/fm1/build.py --diag`) shows each core's load on the screen.
   - In that build, HOME and SAVE held, then REC, sets the heaviest case from section 1 (twelve notes struck every
     two seconds); the same keys put the sound back.
   - The owner reads the two numbers in it, then while playing a typical song.
   - That gives the ratio between host instructions and the device (phase 0's missing measurement), so every cost
     below becomes a percentage of the real chip.
2. **Balance the cores (done).** The second core ran at less than half the first. It now takes the third synth's
   chain (Colour, Media, Motion) with the verb's network, while the first runs the other two: 1503 and 1143 in the
   heaviest case [M], the sound bit for bit the same. The echo bus stays on the first core for now. The second core
   already joins the first before any flash write, so the store is unaffected.
3. **Free RAM for sound.**
   - The boot's held frame (29 KB) is needed only during the boot, while nothing sounds. It can share its memory
     with a sound buffer that starts empty after the boot.
   - The lit shapes' depth cache (31 KB) only needs 16 KB.
   - Together that's ~45 KB more for effect lines, without touching the landscapes or the screen.
   - Stage 3 and stage 4 need no new RAM (the new media and motion types share their row's line), so this waits for
     stage 5's spaces.
4. **Two fixes the chord audit found:**
   - velocity is stored but unused;
   - the LFO page draws but does nothing.

   Both come first in stage 3, because they are the cheapest sound design there is.

## 3. Stage 2: scenes, the song mode, harmony (built, 2026-10-24)

Modelled, at the owner's ask, on how a pocket studio arranges its patterns: patterns per track, scenes that combine
them, a song of scenes.

- **Patterns per synth.**
  - Each synth has eight patterns of its own: its rolls, up to 64 steps each.
  - The Roll's head names the one it edits.
- **Scenes.**
  - Sixteen scenes, on the sixteen white keys with HOME held, switched on the bar.
  - A scene says which pattern each synth plays, with the progression and which synths are muted.
  - An empty scene picked starts as a copy of the one playing, its patterns shared.
  - A variation is one synth's pattern changed in it (below).
  - HOME held + OCT+, then a scene key, copies the scene there; over one in use, the key twice.
- **The Song page** (HOME held + SEQ), two rows on ALGORITHM:
  - **Scene:** the scene open as three lanes, each synth's pattern and its notes.
    - K1–K3 each synth's pattern (at the bar when playing); K4 the progression.
    - White keys 1–8 are the selected synth's pattern; black keys mute.
    - OCT+ copies the selected synth's pattern into its next empty one, now the scene's: a variation in one press.
      OCT− twice clears it.
  - **Steps:** the song, up to 128 steps, each a scene for 1–8 bars.
    - Stopped, a scene key writes a step and moves on.
    - K1 the step, K2 its scene, K3 its bars, K4 the end.
    - OCT− deletes a step, OCT+ copies it.
- **Two modes, one switch:** HOME held + PLAY, scene mode or song mode, on any page.
  - Home's foot says which: "Scene 3", "Song mode", "Song 3/12".
  - HOME's light beats the bar in a song.
  - Picking a scene live leaves the song.
- **The end:** Loop, Stop, Fade out (the synths fade over the last step, the space's tails ring), or Fades (in over
  the first step too).
- **Recorded live:** on the Song page, REC and PLAY, then switch scenes with HOME held. Each bar is counted to the
  scene playing; stopping ends the take and turns song mode on.
- **Harmony**, after the sequencers that harmonize one track from another:
  - FOLLOW (SEQ held + ALGORITHM) gains Harmony of 1, 2 and 3.
  - The synth leaves its roll and plays each note its lead plays, moved by its interval in scale steps, on the scale
    as the chord bends it.
  - The interval is SEQ held + K2, an octave below to an octave above, a third above at first.
  - The lead's keys are harmonized live.
- **Saved with the song** (record version 4, which converts older songs: a pattern of three rolls becomes a scene).
- **Cost:** the sequencer's clock only, and the fade once a block.

## 4. Stage 3: the sound itself (built, 2026-10-24, but short loops)

Everything here reads the same recordings; a sound gains depth, not new sources. As built:
- **SOUND** has three rows: Main; **Layer** (Blend, Tune in octaves and fifths, Loop, Tone; ALGORITHM the recording);
  **Voice** (Glide, Velocity, Track, Grit at 22 or 11 kHz).
- **FILTER**'s fourth knob is **Drive** (the unbuilt Drift gave way).
- **LFO** has two rows: Wave (Rate 0.05-20 Hz, Depth, Shape, Fade; ALGORITHM the target: pitch, cutoff, level, pan,
  start) and Sync (free or 4 bars to 1/16; restarted by a phrase's first note, or free). The owner asked for it to be
  felt: its depth is straight, to 3.8 octaves either way on the cutoff; the pitch squares it.
- Record version 5; older songs and sounds load with all of it off, so they sound as they did (bit for bit, measured).
- Cost [M]: off, the heaviest case is CPU0 1522 (1504 before); with a layer under all twelve voices and the LFO on,
  CPU0 1716, CPU1 1355.
- Short loops wait: a loop at any block needs its decoder's state there, and a seam without a crossfade clicks.

| Control | What it gives | Where | Cost |
| --- | --- | --- | --- |
| **Velocity** | the Roll's velocity heard: to level and to cutoff (a sound's own amount) | AMP's Level; FILTER's Env | ~1 a voice [E] |
| **LFO, applied** | one a synth: Sine, Triangle, Ramp, Square, Random; free or synced to the tempo; to pitch, cutoff, level, pan, the sample's start, or an effect's amount; fade-in | the LFO page (already drawn) | control rate, ~2 a voice [E] |
| **Layer** | a second recording under the sound, through the same filter and envelope: a breath under a piano, a tape wash under a pluck. Blend, Tune (octaves and fifths first), Loop, Tone | SOUND's second row (shown as Soon now) | ~65 a voice while on [E] |
| **Glide** | portamento between notes, for lines and slow pads | SOUND, ALGORITHM with EDIT held | ~2 a voice [E] |
| **Grit** | the console's rate: a synth's voices at 22 or 11 kHz, band-limited; darker, older, and it halves their cost | SOUND, its last free knob | saves ~half a voice [E] |
| **Short loops** | the plan's SHORT 1/2 to 1/16: a shorter loop inside the sustain, for the looped-pad character | SOUND's Loop | ~0 [E] |
| **Filter keytrack and drive** | cutoff follows the note; a little saturation inside the filter, for resonance that sings rather than whistles | FILTER, ALGORITHM with SEL held after the type | ~8 a voice [E] |

Not here, on purpose:
- **Reverse recordings.** The ADPCM blocks decode forwards only. Storing each block's state would cost ~95 KB of
  flash. Reverse comes as an echo instead (section 6).
- **Unison.** It would double the voices a note uses. Drift's per-note detune and Chorus give the width more
  cheaply.

## 5. Stage 4: effects per synth (built, 2026-10-25; in 0.4.4)

Each row keeps its four macros: K1 Amount, K2 Tone, K3 Time or Rate, K4 Character. Every new type gets the same
treatment as 0.2's:
- a fade when it comes in or goes, and its own state from rest as it comes in (`fx_enter`);
- glides on its own events;
- the click detector, run over its corners (each type switched on and off, and to the next, every second; its
  knobs swept; all at 0 and at 127): no clicks, nothing clipped [M] (Vinyl's crackle is its own);
- WAVs of those corners for the owner (`--fxdemo`: the nine, and each sound dry to hear them against).

The new types come after the old in each list, so a stored type keeps its place: the store's version 6 (a song) and
the library's form 2 (a sound) move a type stored at 5, 4, 4 a row to its place in 7, 7, 8. The stress test and
`--cpu` take each row's heaviest: Noise, VHS, Flanger.

| Row | Type | What it is | K1 · K2 · K3 · K4 | Cost a synth [M] |
| --- | --- | --- | --- | --- |
| Colour | **Warm** | a triode's lean: an even harmonic and a soft top on the lows and mids (below ~4 kHz), the highs passed by clean so it thickens without fizz; BODY lifts the lows into it | Drive · Tilt · Body · Mix | 69 |
| Colour | **Distort** | a soft-kneed clip that SHAPE turns into a sine fold; ~5 kHz in, a slow level match out (+0..2 dB on any sound) | Drive · Tone · Shape · Mix | 62 |
| Media | **Vinyl** | 33⅓: the platter's wow (0.56 Hz, off centre and a little warped, to ~1.1 %), the top rolled off, the surface's crackle (its size cubed: most small) and hiss, a ~40 Hz rumble | Wow · Tone · Crackle · Rumble | 95 |
| Media | **Radio** | AM through a small speaker: 150 Hz–5 kHz narrowing to 600 Hz–1.2 kHz, its top four poles, static in the band, the station drifting off and back (to -12 dB) with the static rising as it goes | Band · Tone · Static · Drift | 73 |
| Media | **Dictaphone** | a microcassette: narrow, a 7–12 Hz capstan warble on a 1.5 Hz wow, the automatic level control (to 8x) raising its hiss and motor in the gaps; the slower, the darker | Warble · Tone · Speed · Squash | 94 |
| Motion | **Ensemble** | the string machine's: three taps at 120°, each on a slow (0.3–1.5 Hz) and a fast (4–8 Hz) sweep, WIDTH apart | Depth · Tone · Rate · Width | 67 |
| Motion | **Phaser** | the pedal's four all-pass stages, mono (after Felucca's), swept 150 Hz–4.8 kHz around TONE's place | Depth · Tone · Rate · Reso | 61 |
| Motion | **Flanger** | a tape flange (after Felucca's): 0.25–8 ms on a squared triangle, the right a quarter behind, fed back to 0.85 | Depth · Tone · Rate · Reso | 69 |
| Motion | **Tremolo** | the level, or with PAN the sides against each other, on 1/2 .. 1/16T of the tempo, in time with the bars; SHAPE squares it (edges ~1 % of the cycle) | Depth · Shape · Rate · Pan | 10 |

- Each new type costs no more than its row's heaviest (Noise 86, VHS 122), but Ensemble and Flanger, 6-8 over the
  Chorus's 61. The heaviest case (Noise, VHS, Flanger on all three, Haze, Grains): CPU0 1575, CPU1 1200 [M]; with a
  layer under every voice and the LFO on, CPU0 1602, CPU1 1375. Under the proven 1650.
- Levels against the dry sound, at the demos' settings, on three sounds: within -2..+3 dB [M] (Tremolo -1.5 at full
  depth: it halves the level half the time).
- Flash and RAM: the code and one 33-point table; no new line (each shares its row's).

### 5.1 The media on the mix, a delay a synth (the owner's, 2026-10-25; in 0.4.7)

The owner: "media fx should be global and echo delay per track". Their choice of three:

- **Media is one, over the whole mix** (GLO's third row: Verb, Echo, Media, Setup), after the verb's and the echo's
  returns, before the width and the limiter. Its six types and knobs as they were. The tapes run at half the mix's
  level, so they bend as one synth did; Vinyl, Radio and Dictaphone at its own (their crackle, hiss and level are
  theirs). Each side its own state, the same settings and dice, so the wow is one. Its left runs on the first core,
  its right on the second, after the verb's network. Levels against the per-synth media on one synth: within
  -0.1..+0.9 dB [M] (the verb's return now goes through it too).
- **Each synth has a Delay** (FX's second row, where Media was): Off, Delay, Ping-pong. Mix, Tone (325 Hz to 5 kHz),
  Time (1/16 to 1/2, dotted, triplets, of the tempo), Regen. An 8-bit mu-law line at 11.025 kHz, 0.84 s (the
  ping-pong's right reads twice as late: 0.42 s), a time longer than the line halved till it fits, so still in time.
  The read glides to a new time; the wet fades in and out on a type change. 28 a sample [M].
- **The shared Echo keeps Tape and Grains**, its line halved (0.74 s) for the delays' memory: its Time now reaches
  0.74 s, not 1.45 s. Its Delay and Ping-pong (never released) went to the synths.
- Store version 7 (never released before): a song of 0.4.6 moves its MIDI row a row down, gives the mix the media
  of its first synth that had one (none: off), and starts its synths' delays off. A sound of before (library form
  3) loads its delay off; its media is not a sound's any more.
- The heaviest case (Noise, Ping-pong, Flanger on all three, Haze, Grains, VHS on the mix): CPU0 1571, CPU1 1247
  [M]; with a layer under every voice and the LFO on, CPU0 1520, CPU1 1379. Under the proven 1650.

## 6. Stage 5: the spaces

Shared buses, at 22.05 kHz, one type each at a time, as now.

| Bus | New type | What it gives | RAM | Cost |
| --- | --- | --- | --- | --- |
| Verb | **Shimmer** | the Haze with an octave up in its loop: the ambient staple, endless and bright | ~4 KB more | +~70 over the FDN [E] |
| Verb | **Freeze** | on any verb, a long press holds the tail endlessly, the input closed; the pad that never ends | — | ~0 [E] |
| Verb | **Plate** | a dense, bright tank for keys and plucks | ~45 KB: only with stage 1's freed RAM | 244 (plan 1.3) |
| Echo | **Ping-pong** | each repeat crosses sides | the echo's line | 65 (plan 1.3) |
| Echo | **Filtered** | a band-pass in the loop: dub repeats that thin out | the echo's line | ~70 (plan 1.3) |
| Echo | **Reverse** | the line read backwards in crossfaded windows: swells before the note | the echo's line | ~70 [E] |
| Echo | **Swell** | every repeat fades in instead of starting hard: ambient trails | the echo's line | ~70 [E] |
| Grains | **Freeze, reverse, spread** | the grains held on what they last heard, played backwards, thrown across the stereo field | the echo's line | +~10 [E] |

Spring is left out: its drip suits surf more than this machine's sound. It can come back if the owner wants it.

## 7. Stage 6: the song and the hands

| Part | What it gives | Where | Cost |
| --- | --- | --- | --- |
| **Master tape** | the whole mix through a gentle saturation and glue, with a whisper of wow: the final patina | GLO's Song row (shown as Soon now) | ~40, ~4 KB [E] |
| **Pump** | the synths dip on the beat, the space doesn't: the breathing of ambient house | GLO's Song row | ~10 [E] |
| **Tape stop** | the whole machine slows to a halt, then spins back up: the voices' pitch and the lines' speed together | HOME held + a key or a button | ~5 [E] |
| **Performance** | the three cutoffs and the verb's return live (as plan 3.6 has it), and Freeze | HOME held, K1–K4 | — |

## 8. Stage 7: the factory sounds

Today a sound is a recording and whatever the synth was set to. Instead:
- **A designed sound for every recording.** Its filter, envelope, effects and LFO are set so the recording sounds
  its best, as one coherent palette:
  - pads that bloom;
  - keys that sit;
  - plucks with air;
  - basses that hold the floor.
- **Sheets for the owner to pick from, as on MONO2K.** Each candidate is a short phrase in a WAV, grouped by kind.
  The owner keeps, rejects and names.
- **PRESETS order:** the 64 factory sounds, then the owner's own (User 01 on).
- **INIT** stays the first song's plain sound, the recording kept.
- **Space:** a sound is 45 bytes, so 64 factory sounds take ~3 KB of flash.

## 9. The order

| Stage | Contents | Ends with |
| --- | --- | --- |
| 1 | measure the FM-1; balance the cores; free RAM | the two load numbers; the heaviest case at ~1500 a core |
| 2 | scenes and per-synth patterns, the Song page, the song mode, harmony (built) | a song played start to end on the FM-1; a build |
| 3 | velocity, the LFO, layer, glide, grit, short loops, keytrack and drive | WAVs; a build |
| 4 | Warm, Distort, Vinyl, Radio, Dictaphone, Ensemble, Phaser, Flanger, Tremolo (built) | WAVs of each one's corners; a build |
| 5 | Shimmer, Freeze, Ping-pong, Filtered, Reverse, Swell, Grains+, Plate if the RAM allows | WAVs; a build |
| 6 | master tape, pump, tape stop, the performance layer | WAVs; a build |
| 7 | the factory sounds, from the owner's sheets | the release |

The song mode comes early: it costs no CPU, needs no measurement, and turns the patterns into finished pieces.

Releases: 0.3 (FM-1_968) went out after stage 2, once it ran on the owner's FM-1; 0.4 (FM-1_969) after stage 3 and
three reviews' fixes, at the owner's word. That was the last identity in Demake's range, so 0.4.1 .. 0.4.4 keep it
(0.4.4: stage 4, at the owner's word; 0.4.5: its letter white; 0.4.6: twelve landscapes; 0.4.7: the media on the
mix, a delay a synth, Vary, at the owner's word). Since 0.4.8 every release keeps FM-1_969 (the FM-1 Firmware Hub's
rule: one identity a firmware, never derived from the version), so a 0.5 needs no new range.

Every package goes to the owner first, as `~/Downloads` builds, before a release.

## 10. For the owner

- **What first:** which of sections 3–8 matter most? The order above is a suggestion; any stage can move.
- **The song mode:** should a step also be able to change the key, or the tempo?
- **The effects list:** strike anything that doesn't fit Demake's sound, and add what's missing, as a sound to
  describe or a reference to listen to (as the long modulated verb and the cloud of grains were).
- **The master and the pump:** wanted, or does the mix stay as the three synths make it?
- **Factory sounds:** one designed sound per recording (64), or more (a few variations of the favourites)?
- **Releases:** the range is used up (0.4 = FM-1_969): which identities for the versions after it?
