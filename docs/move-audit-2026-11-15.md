# Demake on the Move: audit, 2026-11-15 (Phase 1)

Four independent reviews of `move-port` (2b5b7d6): the audio path, the controller, the screen and ui.js, and the
core's Move-only changes. Every finding below was checked against the code; most were reproduced in a test program.
Lines are as of the Phase 1 commit. Only the critical ones are fixed in this phase; the rest wait for a decision.

## Critical: fixed in Phase 1, each with a test (move/test/inside.c, also under ASan + UBSan) and a mutant

| # | Where | What | Fix |
|---|---|---|---|
| C1 | `firmware/src/dmk_chord.c:640` `chord_key` | 11ths and 13ths: `diatonic` wrote 7 bytes into `iv2[6]`, on the audio thread (ASan: stack-buffer-overflow); chords came out wrong or one note | `iv2[IV_N]` (6 on the FM-1: unchanged) |
| C2 | `firmware/src/dmk_chord.c:220` `follow_note` | Follows the root wrapped by 7 degrees: in a 5-, 6- or 8-note scale the roll moved to another note (6 of the 9 non-7 presets) | wrap by `SCALE_N` (7 on the FM-1) |
| C3 | `move/dmk_move_ui.c:1127` `held_view` | A held step's text ("Step 121 C#4 E4 G#4") ran past `sound[18]` into the notice | built in a local buffer, as many notes as fit |
| C4 | `move/dmk_move_ui.c:312` `scene_key` | Copy onto the open scene left its rolls loaded under the other scene's patterns: the next switch wrote them over that scene's | refuse the open scene, refuse an empty source, twice over one in use (as the FM-1) |
| C5 | `move/dmk_move.c:568`, `move/dmk_move_ui.c:807` | Closing Demake on Save's Sounds row saved a sound and not the project: edits since the last save lost | closing calls `mv_save_project` |
| C6 | `move/dmk_move.c:416` `dump_string` | `get_param("dump")` wrote a full roll (up to ~39 KB) into an 8 KB buffer; only the tests call it | bounded |

## Important: to decide together

| # | Where | What | Suggested fix |
|---|---|---|---|
| I1 | `move/dmk_move_ui.c:628`, `:776` | A fast turn of a list knob (over 16 steps in one message; the Sound jog over 8) does nothing at all | clamp to 16 / 8 instead of skipping |
| I2 | `move/dmk_move_ui.c:169` `un_put` | Undo restores Follow without `chord_off` / `chord_join`: a synth silent until the next chord, or chord notes left sounding on a Free synth (a slot can be 8 bars) | as `pat_changed` does |
| I3 | `move/dmk_move_ui.c:169` | Undo puts back the whole of `ui.val`, so it also reverts the tempo, the arp hold and the Save page's values, none recorded; `ui.preset` left stale | snapshot only what an edit recorded; recompute `ui.preset` |
| I4 | `move/dmk_move_ui.c:190` `un_check` | An Undo in the block right after a pattern or scene switch writes the old pattern into the new (rare, data loss) | `un_check()` first in `undo_do` and `undo_mark` |
| I5 | `move/dmk_move_ui.c:628` (core `dmk_ui.c` `roll_held`, the chord slot hold) | A pad held on Roll or Chord turns every knob, Shift + jog and Loop into edits of the held note or chord slot, while the screen shows the normal names | only knobs 1-4 edit the held note; show its fields |
| I6 | `move/dmk_move_ui.c:948` | Rec off while Ready under Move's clock: Move plays, Demake stays stopped until Move stops | start on Move's nearest step, or keep Ready |
| I7 | `move/dmk_move_ui.c:401` `held_note` | The chord row on a held step adds or removes each note (shared notes disappear), silently | clear the held step once and write the chord |
| I8 | `move/dmk_move_ui.c:856` `mv_track` | Changing synth while a step is held edits or clears the other synth's step | keep the synth with each held step, or ignore track buttons then |
| I9 | `move/dmk_move_ui.c:601` | Keys on and Rec, stopped: the piano pads write the progression on any page (a preset becomes a User copy, no Undo) | guard every chord-playing pad, not only the chord row |
| I10 | `move/dmk_move_ui.c:436` `held_turn` | Two adjacent held steps moved right spread apart | move in descending order going right |
| I11 | `move/dmk_move_ui.c:1127` with ui.js | Shift with a step held: knob 1 reads "Move" but turns the tempo | one rule on both sides |
| I12 | `move/screen/dmk_screen.c:138` | The Roll's bar counter is wrong past 9 bars ("8/6" on bar 8 of 16) | format both numbers |
| I13 | `move/dmk_move_ui.c:1216` `roll_view` | Notes above or below the seven rows are not shown at all (the FM-1 marks them at the edge) | clamp the row to an edge mark |
| I14 | `firmware/src/dmk_chord.c:421` `q_of`, `:703` `slot_write` | A chord with no exact quality is stored as the nearest one: written, it plays other notes than were heard (some 11ths, 13ths, 6ths, sus types) | add the missing qualities, or refuse with a notice |
| I15 | `firmware/src/dmk_dsp.c:175` | Closed during a song's fade-out and reopened: the synths silent until a STOP | reset the fade at create |
| I16 | `firmware/src/dmk_seq.c:646` | Each tempo change rounds the place in the step down to 1/256: in tenths (Move's tempo), Demake falls up to 69 ms behind during a ramp | an exact rescale under `DMK_MOVE` |
| I17 | `move/dmk_move.c:272` | A failed file write is never tried again, not even at close | a dirty flag the close checks |
| I18 | `firmware/src/dmk_store.c:473` | A song of format 1-3 (0.2-era, only through an old FM-1 store) opens on the wrong progression | remap there too |
| I19 | `move/dmk_move_ui.c:807` | Shift + Rec saves on the audio thread while playing: a full project (~1 ms) on the worst block (~2 ms) can pass the 2.4 ms | save while stopped, or spread it |

## Minor

- **Seven-note leftovers:** `dmk_chord.c:640`'s probes `d < 7` (the Diminished scale's 8th degree); the FM-1's roll drawing (dead on the Move).
- **Store:** `dmk_store.c:282`'s size check is a byte short on the Move (unreachable). A write during the snapshot copy (`dmk_move.c:73`) can leave one damaged record in that file until the next save.
- **The stress test** (`dmk_ui.c:2604`) plays notes past 127 with 40 voices: only 27 sound.
- **Modifiers:** one `used` flag for all of them, and Shift clears it (`dmk_move_ui.c:893`); they are not cleared on resume.
- **Lists:**
  - the patterns' list's second click (`:702`) keeps the old pattern's position, not the bar's;
  - on Sound, the jog's turn loads sounds but its click opens the patterns.
- **Screen:**
  - the Song title can pass `pic.name[20]` at 125+ steps (`:1273`);
  - `ascii()` (`:1018`) can step past a cut notice's terminator;
  - the water (`dmk_motion.h:7`) reads its own output, so several pixels go at once.
- **Dead code:**
  - `v->row` and the test-only view fields (oct, scene, pat, sync, bpm10, in_key) force redraws;
  - `mc.lit[16]` (`:128`), `cap_on`'s `p` and `MP[MP_SAVE].jog` are unused.
- **Duplication and magic numbers:**
  - the button CCs are kept three times (`dmk_move_view.h:28` unused in C, ui.js, `mv_button`'s literals);
  - the undo keys, 16 steps a bar and the pads' boundaries 16 and 24 are bare numbers;
  - `mv_scene`, `mv_save` and the copy notices repeat the core's.
- **Comments:** the frame encoding says two bits past the edge, but it is four (`dmk_move.c:300`); a redundant `? 2 : 2` (`dmk_screen.c:336`).

## Checked and sound

- **Real-time rules:** no allocation, lock, file call, log or sleep on the audio thread. Every entry point but create and destroy runs on Schwung's audio callback, so the event queue and the frame reads need no atomics.
- **Threads:**
  - the view's seq-lock and the frame triple buffer are correctly ordered;
  - the saver's handoff is correct;
  - the big buffers are touched at create.
- **Bounded work:**
  - per-block work is linear in the notes;
  - the O(n²) paths run only on edits.
- **Arithmetic:**
  - the poly trim and its clamp cannot overflow;
  - tempo arithmetic stays in range from 20 to 300 BPM.
- **The FM-1 build:** its preprocessed code and objects are unchanged.
- **The store:**
  - every remap runs exactly once per version;
  - the slice-by-8 CRC equals the bitwise one;
  - a full project round-trips byte for byte.
- **Tables:** every list's length agrees.
- **Hung notes:** no hung notes on page, synth, octave, Sample or Keys changes while a pad is held.
- **Clock follower:** start, stop, continue, the 70 ms fallback and re-locking all behave.

## CPU baseline

On the Move (Cortex-A72, 1.5 GHz), with the render on a spare core and Move's software beside it (`sh move/cpu.sh 5 1`, 2026-11-05: the same code). Times are µs per block, out of about 2,370 µs of room:

| Case | Mean | p99 | Most |
|---|---|---|---|
| First song | 148 | 430 | 582 |
| Heavy jam | 631 | 1,039 | 1,081 |
| Jam, LP24, drive, LFO | 753 | 1,349 | 1,991 |
| Worst voices (40) | 1,055 | 1,486 | 1,618 |

The real audio thread inside Schwung is still to read: `sh move/cpu_live.sh 10` with Demake open and playing (each SCHED_FIFO thread's share of a core, from /proc).

Memory: about 9 MB of static RAM, nearly all for the store (`st_ram`, its snapshot), the Undo ring and the patterns; the Move has about 1.1 GB free.
