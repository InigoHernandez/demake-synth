#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Demake's tables: every law the core needs a float for, computed here and written as integers to
firmware/gen/dmk_tables.h (never edited by hand).

  .venv/bin/python tools/gen_tables.py

- T_SIN: one cycle of a sine, 1024 points and a guard, Q15.
- T_INC: a note's phase increment at 44.1 kHz (one cycle = 2^32), MIDI notes 0..127.
- THEMES: two palettes (paper, night): four monotone entries, a thermal ramp of eight and its mark, three
  anti-aliasing tones,
  RGB565 for the FM-1 and 0xRRGGBB for the browser. The roles are fixed (docs/plan.md, 5.2); a theme moves the colours,
  never the roles.
- T_AA: for each theme, each text colour and each background, the two palette entries nearest to a third and two
  thirds of the way from the background to the text: the 2-bit glyphs' middle tones on an indexed screen. A pair
  with no near enough entry falls back to the background and the text colour (no anti-aliasing there).
- T_BAYER: the 4 x 4 ordered dither; T_BAYER6, it in six arrangements, the bits' animation.
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "gen" / "dmk_tables.h"

# The 16 entries (docs/plan.md, 5.2): a monotone UI, and one thermal ramp for everything lit.
#   PAPER the ground, LINE the structure (rules, empty tracks), LABEL what a thing is, INK what it holds (never 100 %
#   black); HOLO0..HOLO7 the thermal ramp (the owner's reference, chosen 2026-10-23: a blue rim, a magenta body, a
#   pale core), read by a lit shape's depth, so every shape glows from its own edge in, never a sweep of hues across
#   the screen; HOLO8 the mark a lit pixel carries until its shape is finished (never shown); A_INK1, A_INK2, A_LABEL
#   the glyphs' anti-aliasing tones (never used as colours of their own).
ROLES = ["PAPER", "LINE", "LABEL", "INK", "HOLO0", "HOLO1", "HOLO2", "HOLO3", "HOLO4", "HOLO5", "HOLO6", "HOLO7",
         "HMARK", "A_INK1", "A_INK2", "A_LABEL"]
MONO = {
    "paper": ["F5F5F2", "DCDCD8", "8E8E8A", "18181A"],
    "night": ["141416", "2A2A2D", "77777A", "EDEDEA"],
}
THERMAL = ["2459FF", "5A48F6", "8E3AEA", "C02FD2", "E52BC2", "FF45C2", "FF8CDA", "FFDDF2"]   # rim to core
HOLO = {"paper": THERMAL + ["E52BC2"], "night": THERMAL + ["E52BC2"]}
# the helpers: (from, to, share) of the monotone entries, PAPER = 0
HELP = [(0, 3, 1 / 3), (0, 3, 2 / 3), (0, 2, 0.5)]
AA_OK = [0, 1, 2, 3, 13, 14, 15]                  # the entries a glyph's middle tones may be drawn in


def rgb(h):
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def rgb565(h):
    r, g, b = rgb(h)
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def lin(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lab(h):
    """CIE L*a*b* (D65), for nearest-colour choices"""
    r, g, b = (lin(c) for c in rgb(h))
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116
    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def mix(a, b, t):
    """a to b by t, in linear light, as a hex"""
    ra, rb = [lin(c) for c in rgb(a)], [lin(c) for c in rgb(b)]
    out = []
    for x, y in zip(ra, rb):
        v = x + (y - x) * t
        v = 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
        out.append(round(max(0, min(1, v)) * 255))
    return "%02X%02X%02X" % tuple(out)


def dist(a, b):
    la, lb = lab(a), lab(b)
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(la, lb)))


def aa_table(pal):
    """[fg][bg] -> (c1, c2): the entries nearest a third and two thirds from bg to fg"""
    t = []
    for fg in range(16):
        row = []
        for bg in range(16):
            span = dist(pal[fg], pal[bg])
            pair = []
            for k, frac in enumerate((1 / 3, 2 / 3)):
                want = mix(pal[bg], pal[fg], frac)
                best = min(AA_OK, key=lambda i: dist(pal[i], want))
                ok = dist(pal[best], want) < max(8.0, span * 0.22)
                pair.append(best if ok else (bg if k == 0 else fg))
            row.append(tuple(pair))
        t.append(row)
    return t


def BAYER6(b):
    """the ordered dither in six arrangements: as it is, moved by (2, 1), mirrored, flipped, turned, moved by (1, 3)"""
    at = lambda x, y: b[(y & 3) * 4 + (x & 3)]
    T = [lambda x, y: at(x, y), lambda x, y: at(x + 2, y + 1), lambda x, y: at(3 - x, y),
         lambda x, y: at(x, 3 - y), lambda x, y: at(y, x), lambda x, y: at(x + 1, y + 3)]
    return [[t(x, y) for y in range(4) for x in range(4)] for t in T]


def main():
    THEMES = {n: MONO[n] + HOLO[n] + [mix(MONO[n][f], MONO[n][t], k) for f, t, k in HELP] for n in MONO}
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* generated by tools/gen_tables.py: do not edit */",
             "#pragma once", "#include <stdint.h>", ""]
    sin = [round(32767 * math.sin(2 * math.pi * i / 1024)) for i in range(1025)]
    lines.append("static const int16_t T_SIN[1025] = {" + ",".join(map(str, sin)) + "};")
    inc = [round(440 * 2 ** ((n - 69) / 12) / 44100 * 2 ** 32) for n in range(128)]
    lines.append("static const uint32_t T_INC[128] = {" + ",".join(f"{v}u" for v in inc) + "};")
    lines.append("")
    lines.append("enum { " + ", ".join(f"C_{r} = {i}" for i, r in enumerate(ROLES)) + " };")
    lines.append(f"#define DMK_THEMES {len(THEMES)}")
    lines.append("static const char *const THEME_NAME[DMK_THEMES] = {" + ", ".join(f'"{n.capitalize()}"' for n in THEMES) + "};")
    dark = [1 if lab(pal[0])[0] < 50 else 0 for pal in THEMES.values()]   # (kept for the glue's use)
    lines.append("/* a dark theme (its paper darker than its ink): text on the title colour is ink, not highlight */")
    lines.append("static const uint8_t THEME_DARK[DMK_THEMES] = {" + ",".join(map(str, dark)) + "};")
    lines.append("static const uint16_t T_PAL565[DMK_THEMES][16] = {")
    for pal in THEMES.values():
        lines.append("    {" + ",".join(f"0x{rgb565(h):04X}" for h in pal) + "},")
    lines.append("};")
    lines.append("static const uint32_t T_PALRGB[DMK_THEMES][16] = {")
    for pal in THEMES.values():
        lines.append("    {" + ",".join(f"0x{h}u" for h in pal) + "},")
    lines.append("};")
    lines.append("/* [theme][fg][bg]: the two middle tones of a 2-bit glyph, packed c1 | c2 << 4 */")
    lines.append("static const uint8_t T_AA[DMK_THEMES][16][16] = {")
    for pal in THEMES.values():
        t = aa_table(pal)
        lines.append("    {")
        for fg in range(16):
            lines.append("        {" + ",".join(str(c1 | c2 << 4) for c1, c2 in t[fg]) + "},")
        lines.append("    },")
    lines.append("};")
    # ---- the sound's laws (docs/plan.md, 1 and 2): every one a table, the core integer only
    SR = 44100
    arr = lambda name, typ, vals: lines.append(f"static const {typ} {name}[{len(vals)}] = {{" + ",".join(map(str, vals)) + "};")
    # the filter: a Chamberlin state-variable, its frequency coefficient 2 sin(pi fc / fs) Q15 for 256 cutoffs from
    # 30 Hz to 5.6 kHz on an exponential law (stable while f^2 + 2fq < 4: at f 0.79 and q 2, 3.8; the samples end at
    # 5.5 kHz, and the top index is the filter open); its damping from the resonance, Q14 (2: none, 0.05: ringing)
    arr("T_SVF", "uint16_t", [round(32767 * 2 * math.sin(math.pi * 30 * (5600 / 30) ** (i / 255) / SR)) for i in range(256)])
    arr("T_RESQ", "uint16_t", [round(16384 * (2 - 1.95 * (v / 127) ** 0.7)) for v in range(128)])
    # pitch: 2^(k/12) for k = -48..48 and 2^(c/1200) for c = -50..50, Q16
    arr("T_SEMI", "uint32_t", [round(65536 * 2 ** ((k - 48) / 12)) for k in range(97)])
    arr("T_CENT", "uint32_t", [round(65536 * 2 ** ((c - 50) / 1200)) for c in range(101)])
    # the envelope, per control block of 16 samples: the attack's step (Q24) for 2 ms .. 8 s; the decay's and the
    # release's factor (Q16) for a fall of 60 dB in 30 ms .. 18 s
    arr("T_ATK", "uint32_t", [round((1 << 24) * 16 / (0.002 * 4000 ** (v / 127) * SR)) for v in range(128)])
    # the LFO's free rate: its phase step a 16-sample block, 0.05 .. 20 Hz on an exponential law
    arr("T_LFO", "uint32_t", [round(0.05 * 400 ** (v / 127) * 2 ** 32 * 16 / SR) for v in range(128)])
    # T_EXP's times in ms (to -60 dB), for the screen: 30 ms .. 18 s
    arr("T_EXPMS", "uint16_t", [round(1000 * 0.03 * 600 ** (v / 127)) for v in range(128)])
    arr("T_EXP", "uint16_t", [min(65535, round(65536 * math.exp(-16 * 6.9078 / (0.03 * 600 ** (v / 127) * SR)))) for v in range(128)])
    # mu-law, for the echo's line (8 bits a sample: its memory and its grain)
    def mulaw_dec(b):
        b = ~b & 0xFF
        sign, exp, man = b & 0x80, (b >> 4) & 7, b & 15
        v = ((man << 3) + 0x84) << exp
        v -= 0x84
        return -v if sign else v
    arr("T_MULAW", "int16_t", [max(-32767, min(32767, mulaw_dec(b))) for b in range(256)])
    # the master's limiter (straight to 3/4 of full scale, then a tanh knee) and the tape's saturation (tanh), both
    # over -131072..131072 in 257 points
    def lim(x):
        k = 0.75 * 32767
        a = abs(x)
        y = a if a < k else k + (32767 - k) * math.tanh((a - k) / (32767 - k))
        return round(math.copysign(min(y, 32767), x))
    pts = [-131072 + i * 1024 for i in range(257)]
    arr("T_LIM", "int16_t", [lim(x) for x in pts])
    arr("T_SAT", "int16_t", [round(32767 * math.tanh(x / 32767)) for x in pts])
    # one-pole low-pass coefficients 1 - exp(-2 pi fc / fs), Q15, fc 20 Hz .. 20 kHz, at the voices' rate and at the
    # buses' half rate
    arr("T_OP", "uint16_t", [round(32767 * (1 - math.exp(-2 * math.pi * min(20 * 1000 ** (v / 127), 19000) / SR))) for v in range(128)])
    arr("T_OPH", "uint16_t", [round(32767 * (1 - math.exp(-2 * math.pi * min(20 * 1000 ** (v / 127), 10000) / (SR / 2)))) for v in range(128)])
    # MOTION's phaser: its stages' all-pass coefficient a = (g - 1) / (g + 1), g = tan(pi f / fs), Q14, for a break
    # of 150 Hz * 2^(5 i / 32): 150 Hz .. 4.8 kHz in 33 points (the four-stage pedal's sweep, as Felucca's PH_A)
    arr("T_PHA", "int16_t", [round(16384 * (math.tan(math.pi * 150 * 2 ** (5 * i / 32) / SR) - 1)
                                   / (math.tan(math.pi * 150 * 2 ** (5 * i / 32) / SR) + 1)) for i in range(33)])
    # equal-power pan, Q15: the right's gain at v, the left's at 127 - v
    arr("T_PAN", "uint16_t", [round(32767 * math.sin(v / 127 * math.pi / 2)) for v in range(128)])
    # the echo's time, half-rate samples: 30 ms .. 1.45 s on an exponential law
    arr("T_ETIME", "uint16_t", [round(SR / 2 * 0.03 * (1.45 / 0.03) ** (v / 127)) for v in range(128)])
    lines.append("")
    bayer = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
    lines.append("static const uint8_t T_BAYER[16] = {" + ",".join(map(str, bayer)) + "};")
    lines.append("/* the bits' six patterns: the ordered dither moved, mirrored and turned, the same share of each tone in every")
    lines.append(" * 4 x 4 cell, so the light stays and its bits move (tools/gen_art.py makes the same six) */")
    lines.append("static const uint8_t T_BAYER6[6][16] = {")
    for pat in BAYER6(bayer):
        lines.append("    {" + ",".join(map(str, pat)) + "},")
    lines.append("};")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    print(f"{OUT.relative_to(ROOT)}: {len(lines)} lines")


if __name__ == "__main__":
    main()
