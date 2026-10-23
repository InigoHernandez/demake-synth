#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's textures, generated (never drawn by hand): web/img/dither.png, an 8 x 8 ordered-dither tile (the
screen's own Bayer pattern, faint), and web/img/grain.png, film grain (seeded noise, grey on transparent).

  .venv/bin/python tools/gen_web_textures.py
"""
import random
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
IMG = ROOT / "web" / "img"
B4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


def main():
    IMG.mkdir(parents=True, exist_ok=True)
    d = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    for y in range(8):
        for x in range(8):
            v = B4[(y % 4) * 4 + (x % 4)]
            if v < 3:
                d.putpixel((x, y), (255, 255, 255, 34))
            elif v > 13:
                d.putpixel((x, y), (0, 0, 0, 22))
    d.save(IMG / "dither.png")
    rnd = random.Random(1997)
    g = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
    for y in range(160):
        for x in range(160):
            v = rnd.random()
            if v < 0.5:
                g.putpixel((x, y), (40, 40, 44, int(60 + 120 * v)))
    g.save(IMG / "grain.png")
    print("web/img: dither.png, grain.png")


if __name__ == "__main__":
    main()
