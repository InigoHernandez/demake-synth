# Demake

An ambient lofi machine for the M-VAVE FM-1: three synths, a piano roll each, a chord machine, eight patterns a
song, a chain of colour, media, motion and space effects per synth. The plan, and every decision so far, is
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
- **The design rules (plan 5.1), for the screen, the emulator and the website:** Geist only (`assets/fonts/`, SIL
  OFL 1.1), completely lowercase (the text audit fails a capital), a rigid scale (screen: 11, 16, 32 px; site: 13, 24
  and the name); eight exact colours on the screen, each a state (PAPER LINE LABEL INK FOCUS PLAY REC HARM), three on
  the site (paper, ink, focus); a rigid grid of 1 px rules; every value a slider or a segment bar; no gradients,
  dither, icons, soft shadows or decoration.
- The browser runs the same core: after any change to `firmware/src`, rebuild with `sh web/build.sh`.
- Never publish a package that has not run on the owner's FM-1.

## Build, test

- Generators: `.venv/bin/python tools/gen_tables.py`, `tools/gen_font.py [--sheet build/fonts.png]`.
- Host: `cc -std=c99 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Wno-unused-function -Ifirmware/src -Ifirmware/gen -o
  build/host/dmk_host host/dmk_host.c`, then `build/host/dmk_host build/host/tour` (every screen as a PNG, the text
  audit over each) and `.venv/bin/python tools/montage.py build/host/tour build/screens.png 4`.
  `build/host/dmk_host --selftest`, `--wav FILE`. With `-DDMK_BENCH` and the FM-1's flags (`-fno-builtin
  -fno-vectorize -fno-slp-vectorize`): `--bench`, the bench's instructions a sample (27 + 6.0 a level).
- Web: `sh web/build.sh`, then the `demake-web` preview (port 8761, in `../.claude/launch.json`).
- Checks: `sh tools/check.sh`: warnings, the soft-float symbol check on armv7m, ASan + UBSan over the self-test and
  the tour with its text audit, the browser build.
- The FM-1: `.venv/bin/python tools/fm1/build.py [--diag] [--bench] [--release 0.Y]` makes `build/fm1/demake*.fwsc`
  (identity FM-1_965; a release 0.Y is FM-1_965+Y) with JieLi's toolchain in a linux/amd64 container (see
  `firmware/fm1/README.md`). The platform (`firmware/fm1/{hal,loader,src}`) is CS-1's as MONO2K carries it, GPL-3.0,
  its headers kept; `dmk_fm1.c` is the glue.

## The screen

- 240 x 240, 4 bits a pixel: the theme's eight state colours and eight anti-aliasing tones (`tools/gen_tables.py`,
  plan 5.2). A colour means its state and nothing else (FOCUS only what is being edited, PLAY only what sounds).
- The grid: status 0-19, four 60 px fields under K1-K4 20-67, the stage 68-219, the footer 220-239; 1 px rules between
  cells; text 6 px into its cell. Boxes over the stage are an ink frame with a hard 2 px shadow (`box`).
- The host's text audit (`DMK_TEXT_AUDIT`) fails the tour on any two texts that touch, any text cut, any capital;
  boxes call `ta_cover`. Fit text with `text_fit`, never guess widths. No overlapping text, ever.

## How the work runs

- Small, reversible changes, each with its test; the owner hears WAVs (and sees screens) before anything is kept.
- Ask when it is the owner's decision (names, licences, identity numbers, the look); decide and say so otherwise.

## Style

C99, 4 spaces, a unity build (`dmk_core.c` includes the rest), static everything, short comments that say why.
