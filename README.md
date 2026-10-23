# Demake synth

An ambient lofi machine for the M-VAVE FM-1: three synths, each with a piano roll and a layer from the owner's own
samples, eight patterns a song to switch between live, a chord machine for melancholic progressions, and a chain of
colour, media, motion and space effects per synth. It is voiced like a late-90s console demake, and drawn with one
typeface (Geist), in lowercase, eight colours that each mean a state, and a rigid grid.

**Status: phase 0, the groundwork.** The plan is [docs/plan.md](docs/plan.md): the effects and their CPU budget,
the engines and the sample format, the workflow on the FM-1's controls, the chord machine, the screen, the website,
and the build order. Built so far: the FM-1 platform and package, the core's screen shell (every page on the grid, eight
state colours, Geist, the text audit), a sine a key, the browser build with a first page, and a bench build to
measure the FM-1's load against the host's instruction counts.

```bash
sh tools/check.sh                              # warnings, soft-float symbols, sanitizers, the tour's text audit, wasm
sh web/build.sh                                # web/dmk.wasm; serve web/ to play it in the browser
.venv/bin/python tools/fm1/build.py            # build/fm1/demake.fwsc (identity FM-1_965); --diag, --bench
```

It will run on the open FM-1 platform of CS-1 and Felucca, as MONO2K does, so it is GPL-3.0-only (see
[LICENSE](LICENSE)). The credits and the notices of every source it uses will live in `LICENSES/`.

By Sequencist ([GitHub](https://github.com/InigoHernandez), [Bandcamp](https://sequencist.bandcamp.com)).
