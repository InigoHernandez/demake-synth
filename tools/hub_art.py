#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 Firmware Hub's pictures of Demake, from the engine's own frames (docs/hub/SUBMIT.md).

  build/host/dmk_host --anim build/hub/frames
  .venv/bin/python tools/hub_art.py build/hub/frames docs/hub

Writes screen.png (240 x 240: Home while the first song plays), screen-4x.png (960 x 960), screens-wide.png (Home,
the roll and the chord machine side by side, 2x), demake.gif (240 x 240, 13 s, looping) and demake-2x.gif (480 x
480). Every pixel is the screen's: 16 colours, scaled by whole numbers, never smoothed.
"""
import sys
from pathlib import Path

from PIL import Image

HOME, ROLL, CHORD = 30, 75, 115         # the frames picked (100 ms each: Home 0-49, the roll 50-89, the chord 90-129)


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    frames = [Image.open(p).convert("RGB") for p in sorted(src.glob("f_*.png"))]
    if len(frames) < CHORD + 1:
        raise SystemExit(f"{src}: {len(frames)} frames, {CHORD + 1} wanted (dmk_host --anim)")
    big = lambda im, k: im.resize((im.width * k, im.height * k), Image.NEAREST)
    frames[HOME].save(out / "screen.png", optimize=True)
    big(frames[HOME], 4).save(out / "screen-4x.png", optimize=True)
    gap, w = 24, 480
    wide = Image.new("RGB", (3 * w + 2 * gap, w), (24, 24, 26))
    for i, f in enumerate((HOME, ROLL, CHORD)):
        wide.paste(big(frames[f], 2), (i * (w + gap), 0))
    wide.save(out / "screens-wide.png", optimize=True)
    for k, name in ((1, "demake.gif"), (2, "demake-2x.gif")):
        seq = [big(f, k).quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
        seq[0].save(out / name, save_all=True, append_images=seq[1:], duration=100, loop=0, optimize=False, disposal=1)
    for p in sorted(out.glob("*")):
        if p.suffix in (".png", ".gif"):
            print(f"{p}  {p.stat().st_size} B")


if __name__ == "__main__":
    main()
