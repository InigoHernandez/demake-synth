Demake 0.4.8 for the M-VAVE FM-1: an ambient lofi machine on 64 of its author's own pedal recordings. Three synths, each with a piano roll, eight patterns, colour, a delay in time and motion; shared reverbs, a tape echo or grains, and media over the whole mix; a chord machine that plays and harmonizes the synths, sixteen scenes and a song mode; twelve pixel landscapes.

- **File:** `demake-0.4.8.fwsc`, identity **FM-1_969**, SHA-256 `160a356e6bb9dff0ab4a6bc139ac2e298857f49fd2e347bc26b2ce8b2d9a0903` (the same file as the [installer](https://inigohernandez.github.io/demake-synth/install.html)'s).
- **Install:** from the [FM-1 Firmware Hub](https://thefirmwarehub.com) or the [installer page](https://inigohernandez.github.io/demake-synth/install.html), over USB in Chrome or Edge. Back up what your FM-1 holds first, with the firmware it runs now.
- **Leave it:** install any other firmware as usual; Demake starts its update loader (Felucca's). Holding OCT− while switching on always reaches the updater.
- **What it keeps:** its own store, 64 KiB of the data area (`0xB0000`–`0xC0000`), written only when you save; it does not read other firmwares' projects or samples, and other firmwares that use that region can overwrite its songs (README, "On the FM-1").
- **USB:** a MIDI device named "Demake FM-1"; notes in and out by synth channel, the clock in or out.
- **Try it first:** [in the browser](https://inigohernandez.github.io/demake-synth/) · [Manual](https://inigohernandez.github.io/demake-synth/manual.html)

New in 0.4.8: one identity for every release (FM-1_969), the USB port named "Demake FM-1", the version at boot. Since 0.4.7: a delay on each synth, the media over the whole mix, the roll's Vary (notes that move in the scale, a chance to play), song names.

A beta (under 1.0). Licence GPL-3.0-only; the update protocol and platform are Felucca's and CS-1's (Hügelton Instruments), GPL-3.0. The recordings are the author's own. Not affiliated with M-VAVE.
