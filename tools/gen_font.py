#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Demake's fonts: Tomato Grotesk (the owner's licence, The Designers Foundry) rendered at the screen's four sizes into
firmware/gen/dmk_font.h. The OTF files never enter the repo: they are read from DMK_FONT_DIR (default: the owner's
copy beside the moodboard). Only the bitmaps made here are committed.

  .venv/bin/python tools/gen_font.py [--sheet build/fonts.png]

Faces (docs/plan.md, 5.3): F11 Medium, 1 bit (FreeType's monochrome hinting: stems on whole pixels, the crisp
system-font look); F15 SemiBold, F28 Bold, F36 Black, 2 bits (anti-aliased, four levels). Every face keeps one
baseline and one cap height, so every capital of a size sits between the same two rows.

Glyphs: ASCII 32..126 and a few more the screens use (— – · … × ° ’). Characters the face lacks (♭ ♯ →) are drawn
here, our own, to the face's cap height and stem.
"""
import argparse
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "gen" / "dmk_font.h"
FONT_DIR = Path(os.environ.get("DMK_FONT_DIR", Path.home() / "Documents/Music/Git/Demake/Tomato Grotesk Font Family"))

FACES = [  # name, file, size in px (the em), bits a pixel
    ("F11", "TomatoGrotesk-Medium.otf", 11, 1),
    ("F15", "TomatoGrotesk-SemiBold.otf", 15, 2),
    ("F28", "TomatoGrotesk-Bold.otf", 28, 2),
    ("F36", "TomatoGrotesk-Black.otf", 36, 2),
]
EXTRA = "—–·…×°’♭♯→"
CHARS = [chr(c) for c in range(32, 127)] + list(EXTRA)
OWN = "♭♯→"                                          # drawn here: not in the face
PAD = 64


def render(font, ch, bits):
    """the glyph's ink box and levels, drawn at the baseline y = PAD"""
    im = Image.new("L", (PAD * 3, PAD * 3), 0)
    d = ImageDraw.Draw(im)
    if bits == 1:
        d.fontmode = "1"
    d.text((PAD, PAD * 2), ch, font=font, fill=255, anchor="ls")
    return im


def own_glyph(ch, cap, stem, bits):
    """♭ ♯ →, drawn at 8x and brought down: our own shapes, sized to the face"""
    s = 8
    h = cap * s
    w = {"♭": int(cap * 0.62), "♯": int(cap * 0.8), "→": int(cap * 1.05)}[ch] * s
    st = max(s, int(stem * s * 0.75))
    im = Image.new("L", (w + 2 * s, h + 2 * s), 0)
    d = ImageDraw.Draw(im)
    if ch == "♭":
        d.rectangle([s, 0, s + st, h], fill=255)                       # the stem, full height
        d.ellipse([s, h * 0.48, w + s, h], fill=255)                    # the bowl on its foot
        d.ellipse([s + st, h * 0.48 + st, w + s - st, h - st], fill=0)  # and its hole
        d.rectangle([s, h * 0.48, s + st, h], fill=255)
    elif ch == "♯":
        t = max(s, int(st * 0.8))
        for x in (w * 0.3, w * 0.68):
            d.rectangle([x, 0, x + t, h], fill=255)
        for y in (h * 0.32, h * 0.66):
            d.polygon([(s, y + t * 0.5), (w + s, y - t * 0.5), (w + s, y + t * 0.5), (s, y + t * 1.5)], fill=255)
    else:
        y = h * 0.55
        d.rectangle([s, y - st / 2, w - h * 0.35, y + st / 2], fill=255)
        d.polygon([(w - h * 0.5, y - h * 0.35), (w + s, y), (w - h * 0.5, y + h * 0.35)], fill=255)
    small = im.resize((max(1, (w + 2 * s) // s), max(1, (h + 2 * s) // s)), Image.LANCZOS)
    if bits == 1:
        small = small.point(lambda v: 255 if v >= 110 else 0)
    full = Image.new("L", (PAD * 3, PAD * 3), 0)
    full.paste(small, (PAD, PAD * 2 - cap - 1))
    return full


def levels(v, bits):
    if bits == 1:
        return 1 if v >= 128 else 0
    q = round(v / 85)                                 # 0, 85, 170, 255 -> 0..3
    return max(0, min(3, q))


def face_data(name, path, size, bits):
    font = ImageFont.truetype(str(path), size)
    hbox = render(font, "H", bits).getbbox()
    cap = PAD * 2 - hbox[1]
    ibox = render(font, "l", bits).getbbox()
    stem = max(1, round((ibox[2] - ibox[0])))
    glyphs = []
    for ch in CHARS:
        im = own_glyph(ch, cap, stem, bits) if ch in OWN else render(font, ch, bits)
        adv = round(font.getlength(ch)) if ch not in OWN else (im.getbbox()[2] - PAD + max(1, size // 8)
                                                                if im.getbbox() else size // 3)
        box = im.getbbox()
        if not box:
            glyphs.append((ch, adv, 0, 0, 0, 0, []))
            continue
        x0, y0, x1, y1 = box
        rows = []
        for y in range(y0, y1):
            rows.append([levels(im.getpixel((x, y)), bits) for x in range(x0, x1)])
        # trim empty rows and columns the threshold left
        while rows and not any(rows[0]):
            rows.pop(0); y0 += 1
        while rows and not any(rows[-1]):
            rows.pop(); y1 -= 1
        if not rows:
            glyphs.append((ch, adv, 0, 0, 0, 0, []))
            continue
        while not any(r[0] for r in rows):
            rows = [r[1:] for r in rows]; x0 += 1
        while not any(r[-1] for r in rows):
            rows = [r[:-1] for r in rows]; x1 -= 1
        glyphs.append((ch, adv, x0 - PAD, PAD * 2 - y0, x1 - x0, y1 - y0, rows))
    ascent = max(g[3] for g in glyphs if g[5])
    descent = max(g[5] - g[3] for g in glyphs if g[5])
    return dict(name=name, size=size, bits=bits, cap=cap, ascent=ascent, descent=descent, glyphs=glyphs)


def pack(rows, bits):
    """row by row, each row padded to a byte: 1 bit MSB first, or 2 bits (4 pixels a byte) high bits first"""
    out = []
    per = 8 // bits
    for r in rows:
        for i in range(0, len(r), per):
            b = 0
            for k in range(per):
                v = r[i + k] if i + k < len(r) else 0
                b |= v << (8 - bits * (k + 1))
            out.append(b)
    return out


def write(faces):
    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* generated by tools/gen_font.py from Tomato Grotesk (the owner's licence, The Designers Foundry): do not edit.",
         " * The font files are not in this repo; these bitmaps are rendered from them. */",
         "#pragma once", "#include <stdint.h>", "",
         "typedef struct { uint16_t off; uint8_t adv; int8_t x; int8_t top; uint8_t w, h; } dmk_glyph_t;",
         "typedef struct { uint8_t bits, ascent, descent, cap, size, n; const dmk_glyph_t *g; const uint8_t *data;"
         " const uint16_t *code; } dmk_font_t;", ""]
    codes = [ord(c) for c in CHARS]
    L.append(f"#define DMK_NGLYPH {len(CHARS)}")
    L.append("static const uint16_t FONT_CODE[DMK_NGLYPH] = {" + ",".join(str(c) for c in codes) + "};")
    for f in faces:
        data, gl = [], []
        for ch, adv, x, top, w, h, rows in f["glyphs"]:
            gl.append(f"{{{len(data)},{adv},{x},{top},{w},{h}}}")
            data += pack(rows, f["bits"])
        L.append(f"static const uint8_t {f['name']}_DATA[{max(1, len(data))}] = {{" + ",".join(map(str, data or [0])) + "};")
        L.append(f"static const dmk_glyph_t {f['name']}_G[DMK_NGLYPH] = {{" + ",".join(gl) + "};")
        L.append(f"static const dmk_font_t {f['name']} = {{{f['bits']}, {f['ascent']}, {f['descent']}, {f['cap']}, "
                 f"{f['size']}, DMK_NGLYPH, {f['name']}_G, {f['name']}_DATA, FONT_CODE}};")
        print(f"{f['name']}: {f['size']} px, {f['bits']} bit, cap {f['cap']}, ascent {f['ascent']}, "
              f"descent {f['descent']}, {len(data)} B")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(L) + "\n")


def sheet(faces, path):
    """every face's glyphs, at 1x and 3x, ink on paper"""
    lines = ["Demake  ROLL — FELT PIANO  A♭3  F♯4  Am(add9) → Fmaj7", "CURSOR VIEW LENGTH FINE 1/8 0.6 Hz 72 BPM",
             "".join(CHARS[:60]), "".join(CHARS[60:])]
    rows = []
    for f in faces:
        for t in lines:
            rows.append((f, t))
    H = sum(f["ascent"] + f["descent"] + 4 for f, _ in rows) + 8
    im = Image.new("RGB", (640, H), (242, 240, 234))
    y = 4
    lut = {c: g for c, g in zip(CHARS, faces[0]["glyphs"])}
    for f, t in rows:
        x = 4
        lut = {c: g for c, g in zip(CHARS, f["glyphs"])}
        for ch in t:
            g = lut.get(ch)
            if not g:
                continue
            _, adv, gx, top, w, h, r = g
            for yy, row in enumerate(r):
                for xx, v in enumerate(row):
                    if v:
                        k = v / (1 if f["bits"] == 1 else 3)
                        c = tuple(round(242 * (1 - k) + 22 * k) for _ in range(3))
                        px, py = x + gx + xx, y + f["ascent"] - top + yy
                        if 0 <= px < 640 and 0 <= py < H:
                            im.putpixel((px, py), c)
            x += adv
        y += f["ascent"] + f["descent"] + 4
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    im.save(path)
    print("sheet", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", help="write a specimen PNG here")
    a = ap.parse_args()
    for _, fn, _, _ in FACES:
        if not (FONT_DIR / fn).exists():
            raise SystemExit(f"no {fn} in {FONT_DIR} (DMK_FONT_DIR)")
    faces = [face_data(n, FONT_DIR / fn, s, b) for n, fn, s, b in FACES]
    write(faces)
    if a.sheet:
        sheet(faces, a.sheet)


if __name__ == "__main__":
    main()
