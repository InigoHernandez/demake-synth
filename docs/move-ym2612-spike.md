# Demake on the Move: the YM2612 spike (Phase 4), 2026-11-16

The brief: before Nuked-OPN2 goes into the module, one instance, one fixed patch, its CPU measured on the Move; if one
costs too much to run several beside the rest of the module, stop and say so (the alternative: ymfm).

## What was built

`build/opn2spike/spike.c` (outside the module, nothing linked into dsp.so): Nuked-OPN2 1.0.9
(github.com/nukeykt/Nuked-OPN2 at 335747d, LGPL-2.1-or-later, unchanged) as one translation unit; one chip, an
e-piano patch (algorithm 4, two pairs) on all six channels, an Am9 held and struck again every second, rendered at
44.1 kHz from the chip's 53,267 Hz, linear between two chip samples or raw (the latest held, its aliasing kept);
the time of each 128-frame block measured. `spike SECONDS [CHIPS] [raw] [ym2612] [wav FILE]`; `spike_a72` is the
same, static, for the Move.

What the library is: one internal clock a call (`OPN2_Clock`), 24 of them a chip sample, so 1.28 million calls a
second a chip, whatever is sounding; writes go in through the chip's own bus (an address, a data byte, busy 32 clocks
after). YM2612 or YM3438 is `OPN2_SetChipType`, one setting for every instance in the process (a static), so not per
track without changing the library.

## Measured

| Where | Chips | Mean a block | Share of a core |
|---|---|---|---|
| This Mac (Apple silicon, clang -O2) | 1 | 124 µs | 4.3 % |
| This Mac, raw | 1 | 126 µs | 4.3 % |
| This Mac | 2 | 263 µs | 9.1 % |
| The aarch64 container on this Mac (gcc 12 -O2, mutants running beside it) | 1 | 128 µs | 4.4 % |
| **The Move (Cortex-A72, 1.5 GHz), 2026-11-18, Demake closed** | 1 | **936 µs** (worst 2,830) | **32.2 %** |
| The Move, raw | 1 | 900 µs (worst 1,383) | 31.0 % |
| The Move | 2 | 1,846 µs (worst 3,175) | 63.6 % |

Estimate for the Move: the same render runs about 4.6 times slower there than in the container on this Mac (Demake's
first song: 32 µs a block here, 148 µs on the Move's spare core), so one chip would be about **570 µs a block, about
20 % of a core**: as much as the whole of Demake playing today (live, 17.9 % with it closed and 38.3 % playing).

Measured on the Move, 2026-11-18: one chip 936 µs a block of the 2,902 a 128-frame block lasts, **32 % of a core**,
7.3 times the container (the estimate's 4.6 came from Demake's render and does not carry to this one); its worst
block 2,830 µs, nearly the whole block; two chips 64 %. One Nuked-OPN2 chip costs about what all of Demake playing
does (38 % live), so a chip a track is out and even one shared chip leaves no room: **too dear; ymfm next** (below).

How it was measured (switched on, Demake closed):

```
scp build/opn2spike/spike_a72 move.local:/data/UserData/schwung/.opn2spike
ssh move.local '/data/UserData/schwung/.opn2spike 20 1; /data/UserData/schwung/.opn2spike 20 2; rm /data/UserData/schwung/.opn2spike'
```

## ymfm, the same test (2026-11-18)

`build/ymfmspike/spike_ymfm.cpp`: ymfm (Aaron Giles, BSD-3-Clause, github.com/aaronsgiles/ymfm at 81aec25, unchanged;
`ymfm_opn.cpp` with `ymfm_adpcm.cpp` and `ymfm_ssg.cpp` for the link), its `ym3438` (or `ym2612`, the DAC's ladder),
the same patch, chord, retrigger and resampling as spike.c; `spike_ymfm_a72` static from the arm64 container with
g++ 12 (`demake-move-build-cxx:bookworm`). One catch: ymfm applies a write at once, so a key off and a key on in the
same chip sample leave the notes held, never struck again (its level fell a third over 20 s); two chip samples go
between them, as Nuked-OPN2's write timing gives spike.c.

| On the Move, Demake closed | Chips | Mean a block | Worst | Share of a core |
|---|---|---|---|---|
| ymfm, YM3438 | 1 | **155 µs** | 488 | **5.3 %** |
| ymfm, raw | 1 | 153 µs | 544 | 5.3 % |
| ymfm, YM2612 (the ladder) | 1 | 156 µs | 618 | 5.4 % |
| ymfm | 2 | 338 µs | 1,123 | 11.6 % |
| ymfm, a chip a track | 4 | 650 µs | 1,865 | 22.4 % |
| Nuked-OPN2, run again the same minute | 1 | 898 µs | 1,884 | 31.0 % |

ymfm costs a sixth of Nuked-OPN2 on the Move (on the Mac a fifth). The sound, level-matched to -18 dBFS
(`~/Downloads/ym2612-trial-nuked.wav`, `ym2612-trial-ymfm.wav`, 8 s each): the chord's six notes and the octave the
patch adds within 1.5 dB of each other; the spectra (100 Hz to 8 kHz) correlate 0.85, apart mostly in the quiet floor
between the partials. The owner preferred the Nuked file's sound (2026-11-18); most of that difference was the test's: Nuked-OPN2's writes
take 52 chip clocks each, so its six notes start staggered and beat differently. With the same write timing in
spike_ymfm.cpp (each write followed by 52 clocks of dropped output) the two renders' loudness over time correlates
0.999 (was 0.70) and the samples themselves 0.90 (was 0.08): `~/Downloads/ym2612-trial-ymfm-same-timing.wav`.
**ymfm for Phase 4**, at the owner's word; four chips at once (22 %) beside Demake playing (38 %
live) want a chip asleep when silent.

What ymfm asks of the module: C++ in dsp.so (g++ beside gcc, libstdc++ and libgcc linked statically so the Move's own
need not match, no exceptions or RTTI in the audio path; build.sh's import list checked again), and its BSD-3-Clause
notice kept with its files (`move/vendor/ymfm/`); BSD-3 sits inside a GPL-3.0 module as it is.

## Licence: LGPL-2.1 inside a GPL-3.0 module

- Nuked-OPN2's files say "version 2.1 of the License, or (at your option) any later version". LGPL-2.1's section 3
  lets a copy be put under the ordinary GPL instead (version 2 or later), and the "or later" reaches GPL-3.0: so
  `ym3438.c` can be compiled straight into `dsp.so`, statically, and the whole module stays GPL-3.0.
- What that asks of us when we distribute the module (the Schwung catalogue, GitHub releases): the complete source of
  dsp.so, Nuked-OPN2 included, under the same licence. The repository is public and the module's source already is,
  so this is met by keeping the library's files in the repository (`move/vendor/nuked-opn2/`, its `LICENSE` and its
  headers with Alexey Khokholov's copyright, unchanged).
- If we change the library (for example a chip type per instance), LGPL 2.1 section 2a: each changed file carries a
  notice that it was changed, and the date. Keeping it unchanged is simpler.
- Nothing here touches the FM-1: the engine is Move-only (`DMK_MOVE`), as the brief and the owner allow ("Yes, Move
  only"); the FM-1 package stays byte for byte what it is.

## If one chip is too dear

- **Sleep when silent:** a chip whose six envelopes are all at full attenuation and that has no write waiting can stop
  being clocked (its output is zero); a key-on wakes it. In an ambient song with long releases this saves less than
  it seems, but a track not playing costs nothing.
- **ymfm** (Aaron Giles, BSD-3-Clause, C++): computes a sample at a time instead of 24 clocks, much cheaper, very close
  in sound, YM2612 and YM3438 both; it needs C++ in dsp.so (libstdc++ linked statically) or a C port of its OPN core.
- **Fewer voices:** one chip (six voices) shared by every track that picks the engine, instead of a chip a track.

## Nuked-OPN2 in the module, timed (2026-11-18)

The owner chose Nuked-OPN2 by ear. Built into the module (`move/dmk_ym.c`: one chip, each channel read apart from the
pins into its own voice, the chip asleep when silent; `move/test/inside.c`'s seven checks pass), then timed on the
Move with `sh move/cpu.sh 8 1` (the render on one core, as Schwung runs it: core 3, no second core of ours):

| Case | Mean | p99 | Most | Of a 2,902 µs block |
|---|---|---|---|---|
| jam | 742 µs | 1,406 | 1,866 | 25.6 % |
| worst voices | 1,105 µs | 1,637 | 2,012 | 38.1 % |
| **jam + YM2612, six notes** | **1,756 µs** | **2,289** | 2,988 | 60.5 % |
| **worst voices + YM2612** | **1,983 µs** | **2,440** | 2,722 | 68.3 % |

About 2 ms of a block is usable (the handbook), so one Nuked chip beside a real song goes past it: clicks. No compiler
setting helps (on the Move, one chip alone: -O2 920 µs, -O3 969, -O3 with unrolling 993, LTO 975). The brief's rule
("if a single instance costs too much ... stop and tell me"): stopped here, the owner asked. Ways on: ymfm in the
audio thread (one chip ~155 µs); or Nuked on a thread of its own on cores 0-2 at normal priority, a few blocks ahead
(its notes ~10 ms behind the rest, a gap in it when the Move is busy).
