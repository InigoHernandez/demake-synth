# Demake on the FM-1 Firmware Hub

Everything the hub asks for (<https://thefirmwarehub.com/submit/>, <https://thefirmwarehub.com/kb/>), ready to send.
It follows the owner's handbook (`Git/FIRMWARE-HUB-HANDBOOK.md`); the hub's own pages win where they differ. Read
them again on the day you send.

**The order: the tests below → the GitHub release `v0.4.8` → the mail.**

## The mail

To **zven@thefirmwarehub.com**, from your own address, links only, no attachments. It is the hub's own form (the
"Mail me your firmware" button on <https://thefirmwarehub.com/submit/> opens it with the same subject and
questions), answered. Send it only after the tests have passed and the release is on GitHub: its Repository and
Tested lines say so.

> **Subject:** The Firmware Hub: my firmware
>
> Name of the firmware: Demake
> Your name (as it should appear): Sequencist
> Repository: https://github.com/InigoHernandez/demake-synth (release v0.4.8, demake-0.4.8.fwsc attached)
> Identity (FM-1_...): FM-1_969
> Licence: GPL-3.0-only, the complete source in the repository
> What it does (two or three sentences): An ambient lofi machine built on 64 of my own pedal recordings, played by
> three synths, each with its own piano roll, eight patterns and a chain of colour, a delay in time and motion.
> Shared reverbs, a tape echo or grains and old media over the whole mix; a chord machine that plays and
> harmonizes the synths, sixteen scenes and a song mode. Each start shows one of twelve pixel landscapes.
> Picture (link or attached): https://raw.githubusercontent.com/InigoHernandez/demake-synth/main/docs/hub/screen-4x.png
> (the screen while it plays: https://raw.githubusercontent.com/InigoHernandez/demake-synth/main/docs/hub/demake-2x.gif)
> Play in the browser / editor / video (optional): https://inigohernandez.github.io/demake-synth/ · the manual:
> https://inigohernandez.github.io/demake-synth/manual.html
> Tested on a real FM-1, leaving it again works: yes
>
> USB port name: Demake FM-1 (its update loader, Felucca's, shows as "Felucca Update"). It keeps its own store
> (0xB0000-0xC0000), written only when you save, and does not read other firmwares' projects or samples; other
> firmwares using that region can overwrite its songs (the README's "On the FM-1").

## The card

- **Name:** Demake (the repository's name is `demake-synth`; the mail and the release say Demake)
- **By:** Sequencist
- **Kind:** Synths (`synth` in `fmhub.json`)
- **Text** (`fmhub.json`, 271 characters): An ambient lofi machine on 64 of its author's own pedal recordings: three
  synths, each with a piano roll, eight patterns, colour, a delay in time and motion; shared reverbs, a tape echo or
  grains and media over the whole mix; a chord machine, sixteen scenes, a song mode.
- **Picture:** `docs/hub/screen.png` (240 × 240) and `docs/hub/screen-4x.png` (960 × 960): Home while the first song
  plays, synth 3 selected, its landscape (California's palms; the owner: the picture is Home with its landscape).
  `docs/hub/screens-wide.png`: Home, the roll and the chord machine.
- **Animation:** `docs/hub/demake.gif` (240 × 240) and `docs/hub/demake-2x.gif` (480 × 480): Home, the roll, then
  the chord machine as the first song plays, 13 s, looping. Every frame is the engine's own:
  `build/host/dmk_host --anim build/hub/frames`, then `.venv/bin/python tools/hub_art.py build/hub/frames docs/hub`.
  (`dmk_host --homes DIR` renders Home with each of the twelve landscapes, to pick another.)
- **Play in the browser:** <https://inigohernandez.github.io/demake-synth/>
- **Manual:** <https://inigohernandez.github.io/demake-synth/manual.html>
- **Editor:** none yet (no `editor` in `fmhub.json`).

## The package

- **File:** `demake-0.4.8.fwsc` (610,113 B, SHA-256 `160a356e6bb9dff0ab4a6bc139ac2e298857f49fd2e347bc26b2ce8b2d9a0903`), from
  `.venv/bin/python tools/fm1/build.py --release 0.4.8`. The installer page and the release carry this same file;
  check both digests after publishing.
- **Release:** a GitHub release `v0.4.8` on the commit it was built from (under 1.0: the card says beta). The hub
  takes every later release by itself, within three hours. The notes: `docs/hub/release-0.4.8.md`.
- **Identity:** FM-1_969, for every release (`build.py --release` sets only the version, shown at boot, and the
  file's name). The hub's live catalogue, 2026-10-25: no firmware claims it (only the catch-all `FM-1_\d{3,8}` of
  some Felucca builds matches it); 966, Demake 0.1's, is GR!MM's. Of the form FM-1_9NN, so the hub resumes an
  interrupted update. Demake 0.1-0.3 were 966-968; the installer page names only 969 as Demake.
- **Update loader:** Felucca's, from the CS-1 platform the FM-1 build is made on.
- **USB:** a MIDI device, "Demake FM-1", manufacturer Sequencist (until 0.4.7: Felucca's names). The hub's
  installer probes ports whose name has "FM-1" in it, so it finds it without a change on its side.
- **What it keeps:** its own store, 64 KiB at `0xB0000`–`0xC0000` (14 songs, 32 sounds), written only on a save; it
  does not read other firmwares' projects or samples (the README's "On the FM-1").

## Before sending: the hub's tests, on a real FM-1, with that file

1. **From the official firmware:** on <https://thefirmwarehub.com>, "Back to official V15"; then install Demake from
   <https://inigohernandez.github.io/demake-synth/install.html> (step 02, "Choose a .fwsc", the 0.4.8 file). It
   starts, shows 0.4.8 under the word at boot, and after "Connect the FM-1" the hub shows FM-1_969.
2. **From another firmware:** install another one from the hub (Felucca, for one), then Demake again.
3. **Leave it:** with Demake running, install any firmware from the hub. The update must start: the most important
   test.
4. **USB:** on a Mac, Audio MIDI Setup: the FM-1 shows as "Demake FM-1". Notes in and out, the clock.
5. **An interrupted update:** unplug once during "Writing", plug in, press Install again: it must continue.
6. **What it keeps:** save a song and a sound, switch off and on: both come back. Then install another firmware and
   Demake again, and note whether the songs survived (the README says what to expect).
7. **Play it:** the first song, scenes and the song mode, the effects, saving.

If any fails, don't publish: tell Claude what happened.

## Then

1. The installer page gets the same file (`web/firmware/demake-0.4.8.fwsc` and `demake.json`).
2. The release:

   ```bash
   gh release create v0.4.8 build/fm1/demake-0.4.8.fwsc --target "$(git rev-parse HEAD)" \
       --title "Demake 0.4.8" --notes-file docs/hub/release-0.4.8.md
   ```

   ```bash
   gh release view v0.4.8 --json assets -q '.assets[] | .name + " " + .digest'
   ```

   Its digest must be the installer's SHA-256. Never replace a published release's file: make another release.
3. The mail above, from your own address.
4. The hub puts it on its test site, then live; later releases appear by themselves, with no mail.
5. Optional: "Built from source on GitHub" (the hub's shared workflow `zvenson/fm1-build@v1`), once the build runs on
   GitHub's Ubuntu runner.

## Checklist

- [x] `FM-1_969` free in the hub's catalogue (2026-10-25), fixed in `build.py`; `--release` does not change it
- [x] The firmware shows its version (at boot, under the word); the release build is `demake-0.4.8.fwsc`
- [x] USB port "Demake FM-1" (not "Felucca"); the loader keeps Felucca's names, the hub finds it waiting by them
- [ ] It boots in fm1-emulator (Simon Johansson's, optional)
- [x] `LICENSE`, the complete source, how to build (README, `firmware/fm1/README.md`)
- [x] README: "On the FM-1" (what it keeps, backup, leaving, OCT− rescue)
- [x] `fmhub.json` (what: 271 characters, kinds: synth, play)
- [x] A 240 × 240 screen picture (+ 4x, a GIF), linked
- [ ] Installer: the same file, SHA-256 checked; it names only FM-1_969 as Demake (done in the page's code)
- [ ] Tests on a real FM-1: from V15, from another, leaving, USB, interrupted, what it keeps, playing
- [ ] Release `v0.4.8` with the `.fwsc`; its digest = the installer's
- [ ] The mail on the hub's form, after the tests and the release
- [ ] (Optional) the signed workflow from `zvenson/fm1-build@v1`
