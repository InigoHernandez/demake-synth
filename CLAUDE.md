# Demake

An ambient lofi machine for the M-VAVE FM-1 on the owner's 64 recordings (the samples engine only): three synths, a
piano roll and eight patterns each, sixteen scenes and a song of them, a chord machine and harmony, a chain of
colour, media, motion and space effects per synth, a store of songs and sounds. The plan, and every decision so far, is
`docs/plan.md`; read it first. The design rules are its 5.1, the build order its section 7.

## Hard rules

- Target: JieLi AC791N, two cores. Integer DSP only (Q15 / Q31), **no 64-bit division** (a 64-bit multiply is fine),
  no floats in `firmware/src`; every float law is a table from `tools/gen_tables.py`. The chip has a single-precision
  FPU that other firmwares use; Demake does not (the plan, decision 5).
- Big buffers go in `POOL` (Felucca's `.pool` section on the device): RAM 96 KiB `.data + .bss`, pool 336 KiB.
- Clean room: Pure LoFi, the Liven Ambient 0 and the PS1 are references to listen to and measure, never sources of
  code or data. No Sonicware, Sony, Arturia, Microsoft or other brand names or marks in the product.
- DSP sources: pichenettes/eurorack (MIT; its fx are float: algorithm references), electro-smith/DaisySP (MIT,
  `Source/` only; float), hugelton/Felucca (GPL-3.0, integer) and Felucca-Engines (MIT). Keep their notices in
  `LICENSES/` when code from them lands.
- The samples are the owner's own recordings (`../Demake/sample sounds/`); every factory sound gets a new short name.
  Demake plays only them: no synth engines (the owner, 2026-10-24).
- **The design (plan 5 and 6), for the screen, the emulator and the website:** Geist for all text (`assets/fonts/`,
  SIL OFL 1.1) in sentence case, names as written (FX, LFO, Am(add9)); Kiloy only in the wordmark and its D, never
  committed as a font (only its renders: `tools/gen_art.py`, the bitmap FL face); Kiloy's shapes in the UI (corners
  14 on the screen, 24 on the site, pills, round ends). Four greys (PAPER LINE LABEL INK) and one light for what is
  lit: the thermal ramp (a blue rim, a magenta body, a pale core) by each shape's depth, dithered (`HOLO`,
  `holo_done`); never a sweep of hues across the screen (the owner rejected rainbows twice). Cards and spacing for
  structure, a two-line title (ink, then grey), tick meters. The site: every box on the 48 px module's lines.
- The browser runs the same core: after any change to `firmware/src`, rebuild with `sh web/build.sh`.
- Never publish a package that has not run on the owner's FM-1.

## Build, test

- Generators: `.venv/bin/python tools/gen_tables.py`, `tools/gen_samples.py [--wav build/samples]` (the owner's 64
  WAVs from `DMK_SAMPLES` into `firmware/gen/dmk_samples.h` and `assets/samples/PROCESSING.md`; all 64 on every
  build), `tools/gen_font.py [--sheet build/fonts.png]`,
  `tools/gen_art.py` (the site's art, wordmark, D, icon, plate, band, seal and the screens' frames; Kiloy from
  `DMK_KILOY`).
- Host: `cc -std=c99 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Wno-unused-function -Ifirmware/src -Ifirmware/gen -o
  build/host/dmk_host host/dmk_host.c`, then `build/host/dmk_host build/host/tour` (every screen as a PNG, the text
  audit over each) and `.venv/bin/python tools/montage.py build/host/tour build/screens.png 4`.
  `build/host/dmk_host --selftest`, `--wav FILE`, `--demo DIR [N...]` (each sound dry and with its FX, 11 s of
  Am(add9) and a line), `--cpu` (host instructions a sample, built with the FM-1's flags below). With `-DDMK_BENCH` and the FM-1's flags (`-fno-builtin
  -fno-vectorize -fno-slp-vectorize`): `--bench`, the bench's instructions a sample (27 + 6.0 a level).
- Web: `sh web/build.sh`, then the `demake-web` preview (port 8761, in `../.claude/launch.json`). After a layout
  change, run `tools/site_audit.js` in the browser on each page at 24, 16 and 7 modules (1280, 900 and 375 px): no
  text on text, none past its cell or over a box, every item on the 48 px lines, about one row of air after each
  section.
- Checks: `sh tools/check.sh`: warnings, the soft-float symbol check on armv7m, ASan + UBSan over the self-test and
  the tour with its text audit, the browser build.
- The FM-1: `.venv/bin/python tools/fm1/build.py [--diag] [--bench] [--release 0.Y]` makes `build/fm1/demake*.fwsc`
  (identity FM-1_965; a release 0.Y is FM-1_965+Y) with JieLi's toolchain in a linux/amd64 container (see
  `firmware/fm1/README.md`). The platform (`firmware/fm1/{hal,loader,src}`) is CS-1's as MONO2K carries it, GPL-3.0,
  its headers kept; `dmk_fm1.c` is the glue.

## The screen

- 240 x 240, 4 bits a pixel: four greys, the thermal ramp HOLO0-7 and its mark HMARK, three anti-aliasing tones
  (`tools/gen_tables.py`, plan 5.2). Draw lit things with `HOLO` as the colour; the primitive finishes the shape
  (`holo_done`). Rounded shapes: `round_fill`, `round_line` (anti-aliased like the text).
- The layout: head 0-39, four 60 px fields under K1-K4 40-101, the stage's cards 102-219 (6 px margins and gaps,
  `cell_x`/`cell_w`), the foot 224-239. Overlays are rounded ink cards (`box`).
- The host's text audit (`DMK_TEXT_AUDIT`) fails the tour on any two texts that touch and any text cut; boxes call
  `ta_cover`. Fit text with `text_fit`, never guess widths. No overlapping text, ever.

## How the work runs

- Small, reversible changes, each with its test; the owner hears WAVs (and sees screens) before anything is kept.
- Ask when it is the owner's decision (names, licences, identity numbers, the look); decide and say so otherwise.

## Style

C99, 4 spaces, a unity build (`dmk_core.c` includes the rest), static everything, short comments that say why.
