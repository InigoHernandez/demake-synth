#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""step 4's second sheet for the owner: bench draw's frames (build/move/proof/screens), white on black as the OLED, x3,
under short captions. Run from the repo's root: python move/screen/sheets.py build/move/sheet_step4_pages.png"""
import sys

from PIL import Image, ImageDraw, ImageFont

D = "build/move/proof/screens"
Z, G, M = 3, 12, 28
SW, SH = 128 * Z, 64 * Z
INK, BG, LABEL = 245, 28, 150
ft = ImageFont.truetype("assets/fonts/Geist[wght].ttf", 22)
fs = ImageFont.truetype("assets/fonts/Geist[wght].ttf", 15)


def scr(n):
    im = Image.open(f"{D}/{n}.pbm").convert("L")
    return Image.eval(im, lambda v: 255 - v).resize((SW, SH), Image.NEAREST)


SECTIONS = [
    ("1  The Roll", "One cell a step, a dot at each corner. A note fills its cells: an outline, solid while it sounds", [
        ("Stopped", "roll_stop"), ("Playing", "roll_play"), ("Chords", "roll_chords"),
    ]),
    ("2  The pages", "One picture each, the knob row under it", [
        ("Arp", "arp3"), ("Pan", "home1"), ("Song", "home2"),
        ("Chord", "chord0"), ("Sound", "sound0"), ("Filter and Amp", "filter0"),
        ("Save", "save0"), ("A knob touched", "roll_touch"),
    ]),
    ("3  The effects", "Each type draws what it does. The waves travel, the reels turn while it plays", [
        ("Reverb", "tail0"), ("Delay", "echo3"), ("Ping-pong", "echo4"),
        ("Grains", "echo1"), ("Chorus", "mot1"), ("Phaser", "mot5"),
        ("Tremolo", "mot7"), ("Shimmer", "space5"), ("Reverse", "space6"),
        ("Freeze", "space7"), ("Saturate", "col1"), ("Crush", "col2"),
        ("Tape", "med0"), ("Cassette", "med1"), ("Vinyl", "med3"),
        ("Radio", "med4"), ("Dictaphone", "med5"), ("VHS", "med2"),
    ]),
]
cols = 3
w = M * 2 + cols * SW + (cols - 1) * G
h = M * 2
for t, sub, items in SECTIONS:
    h += 70 + (len(items) + cols - 1) // cols * (SH + 34)
sh = Image.new("L", (w, h), BG)
d = ImageDraw.Draw(sh)
y = M
for t, sub, items in SECTIONS:
    d.text((M, y), t, font=ft, fill=INK)
    d.text((M, y + 30), sub, font=fs, fill=LABEL)
    y += 70
    for i, (cap, n) in enumerate(items):
        x, yy = M + (i % cols) * (SW + G), y + (i // cols) * (SH + 34)
        d.text((x, yy), cap, font=fs, fill=INK)
        sh.paste(scr(n), (x, yy + 24))
    y += (len(items) + cols - 1) // cols * (SH + 34)
sh.save(sys.argv[1])
print(sh.size)
