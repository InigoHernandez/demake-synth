Demake 0.4.23 for the M-VAVE FM-1: an ambient lofi machine on 64 of its author's own pedal recordings. Three synths, each with a piano roll of up to 256 steps, eight patterns, its own reverb, a delay, tape echo or grains, and motion; huge shared reverbs, colour and old media over the whole mix; a chord machine and an arpeggiator; sixteen scenes and a song mode; thirteen pixel landscapes.

- **File:** `demake-0.4.23.fwsc`, identity **FM-1_969**, SHA-256 `46313ed53f757d16ebc0a919a14847d1713b8bb64fde5dc58046fe59793e9c10` (the same file as the [installer](https://inigohernandez.github.io/demake-synth/install.html)'s).
- **Install:** from the [FM-1 Firmware Hub](https://thefirmwarehub.com) or the [installer page](https://inigohernandez.github.io/demake-synth/install.html), over USB in Chrome or Edge. Back up what your FM-1 holds first, with the firmware it runs now.
- **Leave it:** install any other firmware as usual; Demake starts its update loader (Felucca's). Holding OCT− while switching on always reaches the updater.
- **What it keeps:** its own store, 64 KiB of the data area (`0xB0000`–`0xC0000`), written only when you save; it does not read other firmwares' projects or samples, and other firmwares that use that region can overwrite its songs (README, "On the FM-1"). Songs and sounds saved by earlier versions open in 0.4.23.
- **USB:** a MIDI device named "Demake FM-1"; notes in and out by synth channel, the clock in or out.
- **Try it first:** [in the browser](https://inigohernandez.github.io/demake-synth/) · [Manual](https://inigohernandez.github.io/demake-synth/manual.html)

New in 0.4.23 (the red screens a player reported):
- **No crash from the CPU's own fault:** on the FM-1, a 64-bit multiply beside a memory read now and then leaves a value a bit off; 0.4.21 had 81 of them. All are gone (the sound bit for bit the same), and the build now refuses any that come back.
- **MIDI note 0 into the chord keys** no longer brings up the red screen.
- **An inversion past a chord's notes** plays the chord as built, not stray notes.

New in 0.4.22 (a player's requests and reports):
- **Chords on one synth, notes on the others:** the Chord page's Keys row is each synth's own; from MIDI too, channel by channel.
- **A loop's region:** LOOP is Off, Full or Set; a Loop row on the Sound page sets where the loop starts and how long it is, on any recording, its jump at each wrap smoothed.
- **Fewer clicks:** a frame of the screen waits while the sound is short of time, eight voices at most at a heavy load, and no click when the Space, the delay or the media changes type.
- **No distortion at full level:** a loud mix is turned down before the limiter, not flat-topped; GLO's Loud shows when.
- **On the site:** How to play, the panel pointed at control by control.

New since 0.4.18:
- **GLO's Space heard:** each synth sends to it again (0.4.18 sent it nothing), a full send about as loud as the sound itself, Lofi's tail back.
- **MIDI chords:** with the Chord page's Keys row at All (ARP lit), each MIDI note in plays its key's chord, strummed, at its velocity, through the arp when it is on (a player's request).
- **Fixes from a player:** the LFO on Pan swings the sound from side to side (it stayed put); clouds drifting across Home's landscape no longer smear over the screen's margins.

0.4.18 brought, since 0.4.11:
- **Sound:** a reverb on each track (Room, Plate, Spring, Hall); eight huge reverbs shared on GLO's Space (Hall, Space, Haze, Bloom, Lofi, Shimmer, Reverse, Freeze); a tape echo and grains in each track's delay; colour on the whole mix.
- **Sequencing:** an arpeggiator on each synth (thirteen ways, Hold); patterns up to 256 steps; recording as played (micro timing, a strum's spacing kept), Nudge, REC and PLAY waiting for the first key; Vary on the chords and the arp too, and Evolve (the roll changing as it plays); Follow on the roll's K3; a scene's own copy of a pattern (SEQ held + OCT+).
- **Saving:** Save as two pages, Projects (save, open, a new empty project, rename, delete) and Sounds (load, keep, keep over, delete, init).
- **Panel:** SELECT picks the synth on every page, PRESETS moves between rows; Home's three rows, Level with Tempo, Space, Pan.
- **Fixes from an audit:** no notes left on (the arp, MIDI, new projects), takes never written wrong, saves never past their sector, MIDI never echoed back, the clock on its beat, no word cut on the screen.

A beta (under 1.0). Licence GPL-3.0-only; the update protocol and platform are Felucca's and CS-1's (Hügelton Instruments), GPL-3.0. The recordings are the author's own. Not affiliated with M-VAVE.
