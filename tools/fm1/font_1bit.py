#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The platform's fonts (firmware/fm1/gen/felucca_font.h) at one bit a pixel.

Felucca's gen_font.py writes them four bits a pixel (an alpha ramp), but Terminus is a bitmap font: every pixel is
off or full, and the ink sits in a few columns of a wider box. Packed one bit a pixel, the box cut to the ink's columns
from the pen (rounded up to whole bytes), FONT_S takes 1,520 bytes instead of 9,120 (gfx.c's cv_text reads it).
The glyphs draw the same pixels, which this checks; a font with a pixel between off and full is refused.

  tools/fm1/font_1bit.py IN OUT      IN: the four-bit header as gen_font.py writes it (git show 5fc5073:...)
"""
import re
import sys

FULL = 15                                               # a four-bit pixel fully on
FONTS = ("FONT_S", "FONT_L")


def arr(src, name):
    m = re.search(r"static const (\w+) " + name + r"\[\d+\] = \{(.*?)\};", src, re.S)
    return m.group(1), [int(x, 0) for x in m.group(2).replace("\n", " ").split(",") if x.strip()]


def pixels4(data, off, bw, h):
    """each glyph's set pixels (x from the bitmap's left, y), four bits a pixel, two to a byte, high nibble first"""
    out = []
    for g, o in enumerate(off):
        w, bpr, on = bw[g], (bw[g] + 1) // 2, set()
        for y in range(h):
            for x in range(w):
                v = data[o + y * bpr + x // 2]
                v = (v & 15) if x & 1 else v >> 4
                if v not in (0, FULL):
                    raise SystemExit(f"glyph {g} has a pixel of {v}: not a one-bit font")
                if v:
                    on.add((x, y))
        out.append(on)
    return out


def pixels1(data, off, bw, h):
    out = []
    for g, o in enumerate(off):
        w, bpr = bw[g], (bw[g] + 7) // 8
        out.append({(x, y) for y in range(h) for x in range(w) if data[o + y * bpr + x // 8] >> (7 - x % 8) & 1})
    return out


def c_array(ctype, name, vals, per=24):
    rows = [", ".join(f"0x{v:02x}" if ctype == "uint8_t" and name.endswith("_DATA") else str(v) for v in vals[i:i + per])
            for i in range(0, len(vals), per)]
    if name.endswith("_DATA"):
        return f"static const {ctype} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"
    return f"static const {ctype} {name}[{len(vals)}] = {{{', '.join(str(v) for v in vals)}}};\n"


def pack(src, name):
    head = re.search(r"static const felucca_font_t " + name + r" = \{([^}]*)\}", src).group(1).split(",")
    h, pad, first, last = (int(v) for v in head[:4])
    _, data = arr(src, name + "_DATA")
    _, off = arr(src, name + "_OFF")
    _, bw = arr(src, name + "_BW")
    _, adv = arr(src, name + "_ADV")
    old = pixels4(data, off, bw, h)
    cols = [x for on in old for x, _ in on]
    c0, c1 = min(cols), max(cols)
    if c0 > pad:
        raise SystemExit(f"{name}: ink from column {c0}, right of the pen ({pad}): keep a pad")
    w = (c1 - c0 + 1 + 7) // 8 * 8                      # the ink's columns from the pen, whole bytes
    bpr, npad = w // 8, pad - c0
    new, noff = [], []
    for on in old:
        noff.append(len(new))
        rows = [0] * (h * bpr)
        for x, y in on:
            x -= c0
            rows[y * bpr + x // 8] |= 0x80 >> (x % 8)
        new += rows
    nbw = [w] * len(off)
    back = pixels1(new, noff, nbw, h)
    for g in range(len(off)):                           # the same pixels, from the same pen
        if {(x + c0, y) for x, y in back[g]} != old[g]:
            raise SystemExit(f"{name}: glyph {g} draws differently")
    text = (c_array("uint8_t", name + "_DATA", new) + c_array("uint16_t", name + "_OFF", noff)
            + c_array("uint8_t", name + "_ADV", adv) + c_array("uint8_t", name + "_BW", nbw)
            + f"static const felucca_font_t {name} = {{ {h}, {npad}, {first}, {last}, {name}_ADV, {name}_BW, "
              f"{name}_OFF, {name}_DATA }};\n")
    return text, len(data), len(new)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    src = open(argv[1]).read()
    if "one bit a pixel" in src:
        raise SystemExit(f"{argv[1]} is packed already")
    first = src.splitlines()[0]
    out = [first.rstrip(" */").rstrip(")") + "; one bit a pixel, the leftmost the top bit, the box from the pen to the ink's last"
           " column in whole bytes: tools/fm1/font_1bit.py) */", "#pragma once", "#include <stdint.h>", ""]
    for name in FONTS:
        text, was, now = pack(src, name)
        out.append(text)
        print(f"{name}: {was} B -> {now} B, every glyph the same pixels")
    open(argv[2], "w").write("\n".join(out))


if __name__ == "__main__":
    main(sys.argv)
