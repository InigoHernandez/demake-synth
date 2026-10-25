# The Firmware Hub mail, ready to paste

**Before you send it:** run the hub's tests in `SUBMIT.md` on your FM-1 with this exact file (the mail says it was
tested, and 0.4.11 is new). Then the GitHub release `v0.4.11` must exist, with `demake-0.4.11.fwsc` attached (the hub lists
only releases). From the `demake-synth` folder:

```bash
gh release create v0.4.11 build/fm1/demake-0.4.11.fwsc --target "$(git rev-parse HEAD)" --title "Demake 0.4.11" --notes-file docs/hub/release-0.4.11.md
```

Its digest must be `sha256:04dfc485e161c41d442f707bccd53a35cf212974a9d37c2be8ae58fc70e1c794`, the same as the
install page's file.

**How to send it:** press "Mail me your firmware" on <https://thefirmwarehub.com/submit/>. It opens the hub's form,
addressed to **zven@thefirmwarehub.com** with this subject. Paste the answers below, from your own address, links
only, no attachments.

---

**Subject:** The Firmware Hub: my firmware

```text
Name of the firmware: Demake
Your name (as it should appear): Sequencist
Repository: https://github.com/InigoHernandez/demake-synth (release v0.4.11, demake-0.4.11.fwsc attached)
Identity (FM-1_...): FM-1_969
Licence: GPL-3.0-only, the complete source in the repository
What it does (two or three sentences): An ambient lofi machine built on 64 of my own pedal recordings, played by three synths, each with its own piano roll, eight patterns and a chain of colour, a delay in time and motion. Eleven shared reverbs, a tape echo or grains and old media over the whole mix; a chord machine that plays and harmonizes the synths, sixteen scenes and a song mode. Each start shows one of thirteen pixel landscapes.
Picture (link or attached): https://raw.githubusercontent.com/InigoHernandez/demake-synth/main/docs/hub/screen-4x.png (the screen while it plays: https://raw.githubusercontent.com/InigoHernandez/demake-synth/main/docs/hub/demake-2x.gif)
Play in the browser / editor / video (optional): https://inigohernandez.github.io/demake-synth/ · the manual: https://inigohernandez.github.io/demake-synth/manual.html
Tested on a real FM-1, leaving it again works: yes

USB port name: Demake FM-1 (its update loader, Felucca's, shows as "Felucca Update"). It keeps its own store (0xB0000-0xC0000), written only when you save, and does not read other firmwares' projects or samples; other firmwares using that region can overwrite its songs (the README's "On the FM-1").
```
