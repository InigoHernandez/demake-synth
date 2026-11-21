# Demake on the Ableton Move: the plan

**Status: agreed by the owner, 2026-11-04 ("all yes"). Step 2 heard on the owner's Move ("sound good"). Step 3 built, proven on the bench, on the owner's Move. Step 4: the owner chose Tiny5, the full-width Roll on a plain grid and all 13 landscapes (2026-11-04); every page is drawn, proven on the bench (`move/screen/sheet-step4-pages.png`) and on the owner's Move. The jog's patterns, the pad's note or chord on a step, empty patterns, Rec then Play, the lifted note limit and the longer scale, chord and progression lists are on the Move too (2026-11-05), verdict pending.** MONO2K stays untouched, and the FM-1 package stays byte for byte what it is.

Demake becomes a Schwung overtake module: it takes the pads, steps, knobs and screen, plays the same core as the FM-1, and runs in time with Move. The route and the traps are in `../MOVE-PORT-HANDBOOK.md`; the owner's brief is its section 6. The pad map is `move/padmap.svg`.

## What I checked, and where the handbook needs correcting

Read against the code on 2026-11-04.

| Finding | What it means for the port |
|---|---|
| **Demake has no undo.** The handbook and `docs/plan.md` mention one; the core has none. | Undo on the Move is new code, Move only (step 3). |
| **MONO2K's clock route doesn't fit.** Demake's MIDI clock in moves on whole pulses, loses a take's micro timing (a known gap from the audit) and turns off the delay's and tremolo's bar sync. | Run Demake's own clock and steer it gently to Move's phase. MONO2K's phase follower carries over; how it reaches the core doesn't. |
| **The tempo is a whole number** (`seq_bpm`), and Move's can have tenths. | One small hook for the step's length. The arp, delay and LFO lengths use the same number, so they need checking at tenths. |
| **Keys have a fixed velocity** (100), and the panel's key path is the one that records. | One small hook so pads play and record at their velocity. |
| **`dmk_frame` does more than draw:** it keeps the UI's clock and a few timers. | The Move never calls it. The glue does those few jobs itself. |
| **The landscapes' band is 228 x 118**, almost exactly the OLED's 2:1. | Home can be a landscape filling the whole screen. |
| **The samples are 8–22 kHz, about 0.9 s plus a loop.** The 64 originals are 12 MB in all. | Question 5 is a real choice. |
| **Schwung is at v1.9.0**, 16 commits past the handbook's. The plugin header is identical; the changes are mostly speech. | The handbook's facts hold. Check again before step 2. |
| **The id `demake` is free** in the catalog (175 modules). | Question 7 only needs your yes. |

**CPU, measured on the Move on 2026-11-04** (`sh move/cpu.sh 10 1`: Cortex-A72 at 1.5 GHz, the whole engine on one core, at no special priority, pinned to core 1, Move's own software running beside it). A block has about 2,370 µs of room.

| Case | Mean µs | p99 µs | Most µs | Mean, of a block |
|---|---|---|---|---|
| First song | 181 | 736 | 5,518 | 6.2% |
| Heavy jam | 314 | 832 | 967 | 10.8% |
| Jam + LP24, drive, LFO on pitch | 353 | 865 | 984 | 12.1% |
| Jam on low samples | 327 | 818 | 1,019 | 11.3% |
| Worst voices found | 395 | 908 | 1,060 | 13.6% |
| Worst voices, Ailanthus | 407 | 906 | 1,035 | 14.0% |

The plan's guess was 0.6 to 0.9 ms a block; the mean is under half of that. The first song's 5,518 µs is one block early in the program's first case, and no later case comes near it, so it's probably another process or the program's own start; it isn't explained yet. This run is outside Schwung: `get_param("ui")` reads the real audio thread's mean and most once Demake is played.

The FM-1's own guards stay out of the way: the CPU cap on voices never engages there, so all 12 voices play. The 64-bit multiply helpers stay inline, as that fault belongs to the FM-1's chip.

## The shape

As MONO2K's (handbook, section 2), with one difference: the core is never asked to draw.

```
Move ─┬─ pads, steps ──────────► dsp.so on_midi ─┐
      └─ knobs, jog, buttons ─► ui.js ─set_param─┤ one queue, drained before each block
                                                 ▼
dsp.so  audio thread:  the queue, the controller, the core (dmk_core.c), the page state published
        drawer thread: the 1-bit frame, from the published state alone
        saver thread:  the store's 64 KiB from a RAM copy to a temp file, then a rename
                                                 │ get_param("frame"), every 34.8 ms
                                                 ▼
ui.js   paints changed rows, sets LEDs, nothing else
```

**New, in `demake-synth/move/`:** `dmk_move.c`, `dmk_move_glue.c`, `dmk_move_ui.c`, `dmk_move_view.h`, `ui.js`, `module.json`, `build.sh`, `proof.sh`, `deploy.sh`, `screen/`, `test/`, `vendor/schwung/`, all GPL-3.0-only with their headers kept.

**The controller drives the FM-1's own UI code.** A Move knob sets the page and row, then calls `ui_encoder`, so every special case (the roll, renames, lists) behaves as on the FM-1.

**Changes to `firmware/src`:** small hooks behind `DMK_MOVE`, five in the end, two more than planned: a key's velocity, the tenths of tempo (the step's length and the lengths made from it), a key's note, so a pad can be any pitch, the chord row's keys, and a take over a note keeping the take's velocity. Proof that nothing moved: the FM-1 package rebuilds to the same SHA-256 as v0.4.24 (`b496d0ef…`).

## The pad map

See `move/padmap.svg`. "First row" is read as the top row, the one nearest the screen.

```
          1      2      3      4      5        6      7      8
T1 ┃  HOME   ROLL   CHORD  SOUND  FILTER·AMP LFO    FX     GLOBAL    pages: song blue, sound magenta, mix pink
T2 ┃  I      ii     iii    IV     V        vi     vii    I         chords in the key, one pad a chord
T3 ┃  C#     D#     ·      F#     G#       A#     ·      ·         the keyboard: black keys
T4 ┃  C      D      E      F      G        A      B      C         white keys
     [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ][9 ][10][11][12][13][14][15][16]   the sequencer, on every page (Shift: the 16 scenes)
```

- **Eight pages on eight pads.** The Move's eight knobs let Filter and Amp share a page, which frees a pad for Global. Song joins Home, and Save goes on Menu.
- **A chord row and a piano together:** chords under one hand, a melody under the other, with no mode to change. The piano is MONO2K's shape, so your hands already know it.
- **Chord mode** (the owner's Phase 5, 2026-11-16): with the Chord page in view, or Keys on for the synth in view, row 2 (T2) picks the progression: the eight of a style, the one playing white. Shift + row 2: its first six pads the six styles (Dub techno, Ambient, Lo-fi & neo-soul, Modal, Classic, Colour scales), the seventh the User ones, the last the style's next eight (blue when there are more). Row 3 (T3) plays the progression's chords in order, then what can come next (a dominant back, borrowed and substituted chords, none twice); row 4 (T4) each of those another way (an extension richer and another inversion). By function: tonic blue, subdominant rose, dominant pink, off the scale violet, row 4 dimmer, the chord sounding white (and the progression's chord playing, on row 3). Every root follows the key and its scale. A held step takes the chord; stopped with REC lit on the Chord page, a pad writes its chord into a User progression. Outside chord mode the rows are as below.
```
chord mode
T1 ┃  HOME   ROLL   CHORD  SOUND  FILTER·AMP LFO    FX     GLOBAL    pages, as always
T2 ┃  the style's eight progressions (Shift: the six styles, User, more)
T3 ┃  i9     VImaj7 IIImaj7 VIIsus2 │ V7b9   bVI    iv     bII       in order │ what can come next
T4 ┃  i11    VImaj9 IIImaj9 VIIsus4 │ V7#9   ...                     each another way (richer, inverted)
```
- **The light:** the thermal ramp from the Move's palette: blue 17, magenta 26, pink 52, each dim until its page is in view. The chord row is dim violet 109. Keys are grey, the root lit magenta, notes sounding white, by hand or by the roll.

### The 16 steps

The owner, 2026-11-04: "I would like the 16 steps sequencer be the sequencer. Then foe adding the scenes we can use shift or turn into scenes somehow".

The steps are the sequencer on every page, as on MONO2K: 16 steps of the roll of the synth in view.

| Gesture | What it does |
|---|---|
| **A step lit** | A note starts there. Blue, the playhead white, a beat's first step faint |
| **Press a pad, then an empty step** | The step takes what the pad played, a note or a whole chord, as it sounded. The pad stays blue: it is what the steps take. Each synth has its own |
| **Hold a pad + press a step** | The step takes that pad's notes, whatever it held; the same notes again come off |
| **A step written** | What was picked is what plays: a synth following the chords plays its roll from now, unless its Follow was set by hand (the Roll's knob 3) |
| **A quick tap on a lit step** | Its notes go |
| **Hold a step + a keyboard pad** | That pitch on that step; several pads make a chord |
| **Hold a step + knobs** | The FM-1's held note: Move, Pitch, Length, Nudge. The jog its pitch, Shift + jog its velocity |
| **Left / Right** | The roll's bars, up to its 16; both together follow the playhead |
| **Shift + steps** | The 16 scenes. White the one playing, magenta the next, blue in use |

On Home's Song row the steps stay the scenes without Shift, so scenes can be played with one hand (the owner's "turn into scenes somehow").

## The controls

| Move | Demake | MONO2K's gesture? |
|---|---|---|
| **Top pad row** | The pages; again, the page's next row | New |
| **Knobs 1–8** | The page's eight; a touch shows the name and value large | Same |
| **Shift + knob 1** | The tempo, on every page | Same |
| **Jog** | The patterns' list, all four synths together: a turn opens it, a click loads (at the bar while playing, a second click now), left alone 3 s or Menu closes it. Each pattern brings its own sound (below). A pattern not played yet is silent and a bar long, chords included, and starts with the sound playing. On Sound the sound (the recordings, the YM2612's patches, the loops, the library; Shift + jog the next kind), on Save the projects | Same, the owner's, 2026-11-05 |
| **Shift + Left / Right** | The pattern of the synth in view alone | New |
| **Rec, then Play** | Ready: Rec blinks and the screen says so; the first pad starts the take from step 1, on the Move's nearest step. Move's transport runs on meanwhile, as it has Play too. Play again cancels | The FM-1's |
| **Shift + jog** | The FM-1's ALGORITHM: filter type, zoom, progression | Same |
| **Track buttons 1–4** | Synths 1–4 (the Move has four; the FM-1 three) | New |
| **Shift + track button** | That synth's Keys: chords from the piano rows on or off, without selecting it. While Shift is held the track lights show which synths have Keys | New |
| **Mute** | Tapped, the synth in view; held, track buttons 1–4 or steps 1–4 mute | Same |
| **Up / Down** | The octave | Same |
| **16 steps** | The sequencer on every page (above) | Same |
| **Shift + steps** | The 16 scenes | Same as MONO2K's patterns |
| **Left / Right** | The roll's bars, on every page | Same |
| **Shift + Left / Right** | The synth's pattern, 1–8 | Same as MONO2K's bank |
| **Rec** | REC. **Shift + Rec** saves | Same |
| **Menu** | Save: Projects and Sounds. Jog scrolls, a click opens; Menu again closes | New |
| **Undo** | Undo; **Shift + Undo** redoes. New code, Move only | Same |
| **Copy held** | A step, then others: its notes pasted there. Shift + a step, then another: the scene. A track button: that synth's pattern into a new one | Same |
| **Delete held** | A step: its notes. Shift + a step: that scene. A track button: that synth's roll. A knob: the value back to its start | Same |
| **Loop** | The arp's hold | Same |
| **Capture** | What you just played, into the roll in whole bars. On Chord: prints the chords into the roll | Same on the roll |
| **Sample** | The keyboard rows: piano or in key | Same button, another job (Question 3) |
| **Play** | Move's and Demake's: one press starts both | Same |

Modifiers go first, as on MONO2K: hold Shift, Mute, Copy or Delete, then press.

## The pages on eight knobs

Rows the FM-1 keeps apart sit side by side, so fewer taps reach the same values. Knobs 1–4 keep the FM-1's order wherever they can. A page's pad tapped again goes to its next row, as on the FM-1.

| Page | Row | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | Shift + jog |
|---|---|---|---|---|---|---|---|---|---|---|
| Home | Mix | Level 1 | Level 2 | Level 3 | Level 4 | Space 1 | Space 2 | Space 3 | Space 4 | |
| | Pan | Pan 1 | Pan 2 | Pan 3 | Pan 4 | Width | Return | | | |
| | Song | Synth 1 | Synth 2 | Synth 3 | Synth 4 | Step | Scene | Bars | End | |
| Roll | Roll | Cursor | View | Follow | Steps | Mutate | Range | Chance | Evolve | Zoom |
| | Arp | Mode | Rate | Octave | Gate | | | | | |
| Chord | Voice | Voicing | Invert | Spread | Strum | Key | Mode | Change | Rhythm | Progression |
| | Keys | Keys | Type | | | | | | | Progression |
| Sound | Main | Sound | Start | Loop | Tune | Fine | From | Length | Grit | |
| Sound, a loop | Loop | Sound | Source | Length | Overdub | Speed | Reverse | Gain | Pitch | |
| | Cloud | Cloud | Size | Density | Spray | Shape | Shimmer | Flip | Freeze | |
| Sound, an FM sound | Main | Sound | Algorithm | Feedback | Bright | Ratio | Detune | Decay | Release | |
| | Op 1–4 | Level | Ratio | Detune | Attack | Decay | Sustain | Decay 2 | Release | |
| | Chip | Chip | Rate | Tune | Fine | LFO | Vibrato | Tremolo | | |
| | Layer | Blend | Sound | Tune | Loop | Tone | Glide | Velocity | Track | |
| Filter·Amp | | Cutoff | Reso | Env | Drive | Attack | Decay | Sustain | Release | Filter type |
| LFO | | Rate | Depth | Shape | Fade | Target | Sync | Restart | | |
| FX | Reverb | Type | Amount | Tone | Size | Mod | Space | Pan | Width | |
| | Delay | Type | Mix | Tone | Time | Regen | | | | |
| | Motion | Effect | Amount | Tone | Time | Spice | | | | |
| Global | Space | Type | Return | Tone | Size | Mod | | | | |
| | Colour | Type | Amount | Tone | Time | Spice | | | | |
| | Media | Type | Amount | Tone | Time | Spice | | | | |

Left out on purpose: GLO's Setup row. MIDI in and out have no route on the Move, Move's clock takes the Clock setting's place, and the themes don't apply to a white OLED.

## The screen

128 x 64, 1 bit, 28.7 frames a second. The first draft of the look comes as PNG sheets at step 4, for your verdict before the device.

- **Rows:** MONO2K's bands to start from: a dark header, a stage with one picture, a row of eight knob pictures, names on touch only. Demake keeps its own language inside them: one big thing, every value with a meter, a choice as dots.
- **Home:** the landscape across the whole screen, with the sound's name small on a dark tag and the three synths as lights. A touched knob shows its value over it.
- **Roll:** the owner's: full width, no knob row, "more simple", "on grid". Sixteen cells, seven rows, a dot at each corner as far as the roll goes, notes on their cells (the take's micro timing is heard, not drawn), a tick above the step. No note names, beat lines or root line.
- **The type:** Tiny5, the owner's pick. Geist Pixel's grid was finer than the screen.
- **Filter·Amp:** the filter's curve over knobs 1–4, the envelope over knobs 5–8.
- **The corners:** 14 px cards don't fit here. Small round corners keep the idea.

### The 13 landscapes in 1 bit

The plan was to shrink the FM-1's renders and dither them. Tried on all 13, 2026-11-04: halftone gave noise, and rules on what each pixel is (ink, light, sky) with a threshold per scene lost every scene that lives on colour. So each is drawn for 128 x 64 instead, in code, on the FM-1's compositions (`move/screen/lands.py`):

- Dark shapes with a lit outline, lights solid, a sky of calm ordered dots, never the busy 50 %.
- A shape over the dots gets a black halo, so it never melts into them.
- 13 KB in `dsp.so` (`move/screen/dmk_lands1.h`), nothing built at run time.
- What moves stays small and sparse (`move/screen/dmk_motion.h`): water laps, stars and windows blink, snow falls, a beam runs out. Some answers the music: the cabin's windows are the synths, the road runs and the speakers beat while it plays.

**The colour isn't lost:** the pads carry the thermal ramp, and the steps can too.

## The order of work

Each step ends with something you can hear, see or try.

1. **This plan.** Agreed, with the questions answered.
2. **First sound.** `dsp.so` plays the first song on your Move; Play starts both; the steps show the playhead.
   Proof: the render matches the host's bit for bit; the FM-1 package's SHA-256 is unchanged; the CPU is measured on the device at the heavy cases. All three done 2026-11-04 (`sh move/proof.sh`, `sh move/test/mutants.sh`, `sh move/cpu.sh`). Added for the first test: the two piano rows, track buttons 1 to 3, Up and Down. Whole BPM only, and no phase lock yet: both come in step 3.
3. **Hands on.** Every control, saving, the LEDs, velocity, Undo, in time with Move, on a plain screen. You play it.
   Built 2026-11-04: `sh move/proof.sh` and `sh move/test/mutants.sh` pass. Against Move's true time, the clock wanders about 0.2 ms at 71 to 133 BPM, tenths included, about 1.5 ms early on average. The plain screen uses Tiny5 for now; step 4's sheet still picks the face. Undo covers the rolls, every value and the mutes, not a progression's written chords, and a scene or pattern switched empties it.
4. **The screen.** Sheets first, Roll then the landscapes, then every page. Your verdict comes before the device.
5. **More power, only if you want it.** Behind Move build flags, never on the FM-1.
   Loops (the owner's, 2026-11-21: "a looper ... like a Torso S-4 lite": record Demake's mix or the audio in, then mangle and overdub; their choices: a kind of sound, Shift + jog between the kinds, REC to record then overdub, playing with the transport, the mix without the loop's own synth; reverse, speed, pitch, a granular cloud and overdub layers; no global LFOs for now). **Step 1, built:** Loop 1–4 after the YM2612's patches in the Sound list; **Shift + jog** on the Sound page jumps to the next kind (recordings, patches, loops), to the last of it the synth had. A synth on a loop plays it whenever Demake plays, locked to the bar (the transport's place in Q32 steps, so a tempo changed plays it bar for bar), through its filter (the LFO on the cutoff), its chain, level, pan and sends; its notes do nothing. Its Sound page: **Loop** row Sound, Source (Mix or Jack), Length (1, 2, 4, 8, 16 bars; within 64 s), Overdub (how much of the old layer a pass keeps), Speed (1/4 to 4), Reverse, Gain. **REC** on it waits for the next bar (stopped: PLAY's first) and records the length, then plays; REC again overdubs one pass on the next bar; **Delete + REC** clears; REC lit while writing, blinking armed. The mix recorded: every synth but the ones playing that loop, with their own effects, before Global's Space, Colour and Media (the loop goes through them on its way out, once). Played back at the level it was recorded (full level, the middle pan). In mono into its synth like every Demake sound. Its picture: its shape and where it plays. Each pattern keeps its loop values (format 21). Four buffers of 64 s stereo (45 MB), touched as Demake opens. **Step 2, built:** **Pitch** on the Loop row (−24 to +24 semitones, apart from Speed: Speed 2 and Pitch −12 plays twice as fast at the pitch recorded): Haizea's HEGAN reading the loop, two taps sweeping a 35 ms window round the playhead, crossfaded. A second row, **Cloud** (press the Sound pad again): Cloud (the grains' share, blended by power), Size (10 ms to 1.28 s), Density (0.5 to ~117 grains a second, each 0.5 to 1.5 times the interval apart), Spray (grains scattered back through up to half the loop), Shape (Smooth, Soft, Even, Sharp, Click: how much of a grain rises), Shimmer (the share an octave up), Flip (the share backwards), Freeze (the grains held on the place where it was turned on while the playhead goes on). Every grain at Speed and Pitch; 16 at most a synth; overlapping, their sum held by its power. Clouds' granular mode as the algorithm (MIT, none of its code), integer, nothing allocated; at Pitch 0 and Cloud 0 the loop plays exactly as step 1 played it. The picture shows the playhead and the first three grains. Still format 21 (no file of it left this machine). **To come:** step 3 the loops saved with the project as WAV files (until then a loop lasts until Demake closes).

   The YM2612 (the owner's brief, Phase 4, 2026-11-18; their choices: one FM synth at a time, six notes; ymfm, picked blind against Nuked-OPN2 in all three pairs, Nuked's chip beside a jam having gone past the 2 ms a block allows; the 16 patches as heard; the page below; clean by default, RAW on): the Sound knob (and the Sound page's jog) goes on past the 64 recordings into 16 FM patches, four each of plucks (Tine, Harp, Koto, Glass pluck), pads (Haze, Tape choir, Drift organ, Dust strings), basses (Grit bass, Sub, Slap, Dub) and bells (Bell, Music box, Glass, Chime); the jog then on into the library. Only one synth plays the chip: another's list ends at the recordings and says which synth has it. An FM synth's notes each take one of the chip's six channels, read apart, and go through its own filter, envelope, velocity and the synth's chain like a recording's. Its Sound page has six rows: **Main** Sound, Algorithm, Feedback, Bright (the modulators' levels), Ratio (their multiples), Detune (the carriers spread), Decay, Release, the last five over the patch from their middle; **Op 1–4** Level, Ratio, Detune, Attack, Decay, Sustain, Decay 2, Release; **Chip** Clean or Grit (the YM2612's DAC step), Smooth or Raw (the chip's samples held, aliasing kept), Tune, Fine, the chip's LFO, Vibrato, Tremolo. Delete + an FM knob: the patch's own value (a macro its middle). Its picture: each operator's level as a bar, the ones heard filled. Each pattern keeps its FM sound; save format 20 (a song of before opens with none). `move/dmk_ym.c` (the engine), `move/dmk_ymfm.cpp` (ymfm, its own memory, nothing allocated while it plays), `move/vendor/ymfm` (BSD-3-Clause, unchanged; `LICENSE-ymfm` in the package). On the Move, `cpu.sh 8 1`: the jam with six FM notes mean 905 µs, p99 1,343 (the jam alone 744); the worst voices with them 1,163, 1,643. Saving an FM sound into the library is still to come.

   Chord mode (the owner's brief, Phase 5, 2026-11-16; their choices: row 2 picks only in chord mode, row 3 in order then what comes next with the variants under, colour by function, Shift + row 2 for the style): see the pad map. 56 progressions, eleven new (Basement, Tram depot, Steam pipes, Signal box: dub techno; Velvet chair ii9 V13 Imaj9 and Corner store iiø V7b9 i9; Hill road, Dorian i7 IV7; Old tale i VI III VII, Andalusia i VII VI V, Home again ii V I, Four winds I V vi IV), in six styles. Save format 19: a song of before keeps its progressions (the User ones moved past the new presets).
   Each pattern its sound (the owner's brief, Phase 3, 2026-11-16): a synth's eight patterns each keep that synth's sound: the Sound, Filter, Amp, FX and LFO pages (the mixer's level, Space send and pan with them: the owner, "Mixer switches too"), its Vary and Arp, its Keys and Strum, how it follows and its harmony's interval. Global, Home's own column, the key, the Keys type and the progression stay the song's (the progression the scene's, the owner's choice). Turning a control changes the sound of the pattern playing; a switch keeps it there and brings the next one's; an empty pattern starts with the sound playing, a copied one with its source's. At the bar while playing, the values a voice reads as it sounds (cutoff, resonance, drive, the filter's envelope, sustain, level, pan, sends, the effects' mixes and knobs, the LFO's rate and depth) glide over 35 ms; the recording, tune, the envelope's times and every list's choice change at once, before the bar's notes; a voice still sounding keeps its recording, and the Space's tails go on. A value turned during the glide is the player's. Save format 18 (about 4 KB more a project, in the same 384 KiB slot); a song of before gives every pattern it kept the song's sound, as it played. Undo looks for a switch first (audit I4) and lets a chord go or join when it puts a Follow back (I2).
   Four synths (the owner's brief, Phase 2, 2026-11-15): track buttons 1–4, Keys on Shift + track, Home's Mix and Pan rows a synth a knob (the tempo on Shift + knob 1 alone, the width and the Space's return on the Pan row's knobs 5 and 6, the Song row's Chords given to synth 4: the progression is on Chord), "Harmony of 4", MIDI channels 1–4, the Lake's cabin four windows. Synth 4 starts as a new song's: Scirpus, a bass, following the key, no notes, in older songs too. Save format 17 in `projects4.dmks` (384 KiB a song, so four synths' every pattern full fit); `projects.dmks` (format 16, three synths) and `demake.dmks` are read until the first save and never written. On the Move, the render on a spare core: worst voices mean 1,068 µs, most 1,714 (three synths: 1,055, 1,618).
   Asked for 2026-11-05: 2,048 notes a pattern and projects to 49,152 notes (`projects.dmks`, 256 KiB a project; the old `demake.dmks` is read until the first save and never written), 23 scales of five to eight notes, 53 chord qualities, 21 Keys types, 45 progressions and the four User ones. Songs of before keep their scale, progression and Keys type. On the owner's Move the same day.

   40 voices, the owner's (2026-11-05), shared by the three synths, 16 of them from one roll. Past twelve voices' power the sum is held at what twelve give (`poly_run`), so more notes thicken a chord and don't drive the limiters: 40 notes measure as 12 do, RMS within about 1 dB, no clipping, and twelve or fewer are untouched to the bit (`move/test/level.c`). Long tails now ring that twelve voices took: the first song reaches 17 voices, its sum never held down (an earlier note here said 40: that test measured a full project, not the first song).

   On the Move, the real build, `cpu.sh 5 1`: first song mean 148 µs, p99 430; heavy jam 631, 1,039; worst voices 1,055, 1,486, most 1,618; the jam with LP24 and drive touched 1,991. Before that, `DMK_VOICES` patched in a copy (`cpu N 3 1`, worst voices, of about 2,370 µs of room):

   | Voices | Mean µs | p99 µs | Most µs |
   |---|---|---|---|
   | 12 | 404 | 768 | 863 |
   | 24 | 725 | 1,076 | 1,262 |
   | 32 | 862 | 1,326 | 1,508 |
   | 48 | 1,223 | 1,614 | 1,723 |
6. **Release, at your word.** Package, release, site, catalog. Tags `move-vX.Y.Z`, **never marked Latest**, because the Hub takes Latest as the FM-1 firmware.

## The proof

- `proof.sh` on the Mac and in the Move's container, renders compared bit for bit.
- `dsp.so` driven through the plugin API as Schwung drives it.
- The audio thread's rules counted: no allocation, lock, log or file call.
- Every control tested, Move's clock with jitter, saving and loading.
- A take's micro timing survives under Move's clock, which MONO2K's route would have lost.
- The screen audited: every value fits, every frame redraws the same from its saved view.
- Each new test shown to fail on a deliberately broken copy.

## Questions for the owner

### Decided (the owner, 2026-11-04)

1. **Global, Save and Song:** Filter and Amp share a pad, Global gets one, Song is a Home row, Save is on Menu.
2. **The type:** a pixel face in sentence case. Tiny5 and Geist Pixel (both OFL) compared at real size on a sheet, the owner picks. Geist Pixel would keep the Geist family, though its pixel grid at this size is unchecked.
3. **The keyboard:** the chord row plus a two-row piano, with Sample switching the piano rows to in key.
8. **The 16 steps:** the sequencer, on every page. Shift + steps: the scenes.

4. **In time with Move:** yes, by default, as MONO2K; Demake on its own clock if Move sends none.
5. **The samples:** the FM-1's bank first, so the sound and the proof hold. A better bank from the originals can follow in step 5.
6. **Extras:** a strict port first. Undo and Capture come with the controls; more voices, aftertouch and the rest wait for step 5.
7. **The name:** Demake, id `demake`.

## Risks

- **Move OS updates:** expect a Schwung reinstall after each one.
- **The clock:** steering Demake's own clock is new. If it can't stay locked, the fallback is MONO2K's route, at the cost of micro timing.
- **Undo:** snapshots cost little to take, but restoring one while playing may cut notes. It may have to act only on the roll.
- **Tiny type:** sentence case at 5 or 6 px may read badly, so the sheet decides.
- **The landscapes:** a shrink can turn to noise. Hand work per scene is the likely cost.
- **Schwung's own warnings:** it is unofficial and moves fast, so each newer call is guarded.
