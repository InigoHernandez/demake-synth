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
  - CPU0 is at 1818 in the heaviest case, past the proven 1650 a core. The calibration puts a core near 3470, but the
    FM-1 hasn't been measured.
  - The RAM pool has 28 KB free.
  - Stage 1 measures the FM-1, moves work to the second core and frees RAM before anything heavy is added.
- **Order:** seven stages, below. Section 10 lists what is the owner's to decide.

## 1. Where it is

| Part | Built | Cost now |
| --- | --- | --- |
| Voice | ADPCM recording, Q16 pitch, SVF (LP24 LP12 HP BP Notch Peak Vowel Off), ADSR | 12 voices: ~900 in the heaviest case [M] |
| Colour | Saturate, Crush (with its output stage), Lossy (3 bands, holes), Noise (hiss, crackle, hum) | Lossy 76, Crush 33 [M] |
| Media | Tape, Cassette (dropouts glide), VHS (smooth tracking) | VHS 161, Tape 103, Cassette 140 [M] |
| Motion | Chorus (2 taps, stereo), Vibrato, Drift | Chorus 78 [M] |
| Verb | Room (combs), Hall, Space, Haze, Bloom, Lofi (8-line FDN on the second core) | Haze 381 [M] |
| Echo | Tape (mu-law, 1.5 s), Grains (6, octaves and fifths) | — |
| Mix | DC blocker, half level through the effects, soft limiter, fades on every type change | — |

- **The heaviest case** (Lossy, VHS and Chorus on all three, Haze, Grains):
  - CPU0 1818, CPU1 835 [M]: the first core does the per-synth chains, the second the verb and half the voices.
- **RAM:** pool 316 KB of 344 KB, RAM 48 KB of 96 KB.
- **Flash:** 496 KB of 582 KB, so 85 KB left for code and tables. The recordings are already in.

## 2. Stage 1: measure, balance, make room

1. **Measure the FM-1 once.**
   - The diagnostic build (`tools/fm1/build.py --diag`) shows each core's load on the screen.
   - The owner plays the heaviest song from section 1, then a typical one, and reads the two numbers.
   - That gives the ratio between host instructions and the device (phase 0's missing measurement), so every cost
     below becomes a percentage of the real chip.
2. **Balance the cores.** The second core runs at less than half the first. It takes:
   - the third synth's chain (Colour, Media, Motion: up to ~300);
   - the echo bus.

   That leaves ~1500 on each core in the heaviest case [E]. The second core already joins the first before any
   flash write, so the store is unaffected.
3. **Free RAM for sound.**
   - The boot's held frame (29 KB) is needed only during the boot, while nothing sounds. It can share its memory
     with a sound buffer that starts empty after the boot.
   - The lit shapes' depth cache (31 KB) only needs 16 KB.
   - Together that's ~45 KB more for effect lines, without touching the landscapes or the screen.
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

## 4. Stage 3: the sound itself

Everything here reads the same recordings; a sound gains depth, not new sources.

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

## 5. Stage 4: effects per synth

Each row keeps its four macros: K1 Amount, K2 Tone, K3 Time or Rate, K4 Character. Every new type gets the same
treatment as 0.2's:
- a fade when it comes in or goes;
- glides on its own events;
- the click detector, run over its corners;
- WAVs of those corners for the owner.

| Row | New type | What it sounds like | K1 · K2 · K3 · K4 | Cost, RAM |
| --- | --- | --- | --- | --- |
| Colour | **Warm** | a soft, uneven saturation that thickens without fizz; the "tube" in a tape deck | Drive · Tilt · Body · Mix | ~35, — [E] |
| Colour | **Distort** | drive into a clip that turns into a fold: grit for plucks and keys | Drive · Tone · Shape · Mix | 54, — (plan 1.2) |
| Media | **Vinyl** | a record at 33⅓: slow wow, rumble, crackle and the top rolled off | Wow · Tone · Crackle · Rumble | ~60, its line [E] |
| Media | **Radio** | AM through a small speaker: a 300 Hz–3 kHz band, static, the tuning drifting | Band · Tone · Static · Drift | ~50, — [E] |
| Media | **Dictaphone** | a microcassette: narrow, fast-warbling, squeezed, a motor whine | Warble · Tone · Speed · Squeeze | ~80, its line [E] |
| Motion | **Ensemble** | three chorus voices on a slow and a fast sweep: a string machine's shimmer | Depth · Tone · Rate · Width | ~75, its line (plan 1.2) |
| Motion | **Phaser** | four slow all-pass stages: the swirl under pads | Depth · Tone · Rate · Feedback | ~40, — [E] |
| Motion | **Flanger** | a short tape flange, slow and deep | Depth · Tone · Rate · Feedback | ~45, its line [E] |
| Motion | **Tremolo** | the level or the pan, swung to the tempo | Depth · Shape · Rate · Pan | ~10, — [E] |

The rows have room: Colour holds 4 types and Off, Media 3, Motion 3. With these, each row has 6 or 7.

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
| 4 | Warm, Distort, Vinyl, Radio, Dictaphone, Ensemble, Phaser, Flanger, Tremolo | WAVs of each one's corners; a build |
| 5 | Shimmer, Freeze, Ping-pong, Filtered, Reverse, Swell, Grains+, Plate if the RAM allows | WAVs; a build |
| 6 | master tape, pump, tape stop, the performance layer | WAVs; a build |
| 7 | the factory sounds, from the owner's sheets | the release |

The song mode comes early: it costs no CPU, needs no measurement, and turns the patterns into finished pieces.

Releases: 0.3 (FM-1_968) went out after stage 2, once it ran on the owner's FM-1. One identity is left in Demake's
range (0.4 = FM-1_969):
- 0.4 after stages 1, 3 and 4;
- the stages after that need the range widened (section 10).

Or the range is widened (section 10). Every package goes to the owner first, as `~/Downloads` builds, before a
release.

## 10. For the owner

- **What first:** which of sections 3–8 matter most? The order above is a suggestion; any stage can move.
- **The song mode:** should a step also be able to change the key, or the tempo?
- **The effects list:** strike anything that doesn't fit Demake's sound, and add what's missing, as a sound to
  describe or a reference to listen to (as the long modulated verb and the cloud of grains were).
- **The master and the pump:** wanted, or does the mix stay as the three synths make it?
- **Factory sounds:** one designed sound per recording (64), or more (a few variations of the favourites)?
- **Releases:** one release left in the range (0.4), then a new identity range for the versions after it?
