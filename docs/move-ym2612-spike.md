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
| **The Move (Cortex-A72, 1.5 GHz)** | 1 | **to measure** | |

Estimate for the Move: the same render runs about 4.6 times slower there than in the container on this Mac (Demake's
first song: 32 µs a block here, 148 µs on the Move's spare core), so one chip would be about **570 µs a block, about
20 % of a core**: as much as the whole of Demake playing today (live, 17.9 % with it closed and 38.3 % playing).

To measure on the Move (switched on, Demake closed):

```
scp build/opn2spike/spike_a72 move.local:/data/UserData/schwung/.opn2spike
ssh move.local '/data/UserData/schwung/.opn2spike 20 1; /data/UserData/schwung/.opn2spike 20 2; rm /data/UserData/schwung/.opn2spike'
```

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
