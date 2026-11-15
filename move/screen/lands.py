#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""the 13 landscapes in 1 bit, drawn for the Move's 128 x 64: the FM-1's scenes (firmware/src/dmk_ui.c, dmk_lands.c)
composed again at this size, since a shrink of dithered colour art is noise (tried: every scene that lives on colour
went). The same language: dark shapes with a lit rim, lights solid, a sky of calm ordered dots. Original drawings, made
here by code, so a scene is a few lines to change. Run from the repo's root:
  python move/screen/lands.py build/move/lands      -> scene00..12.png, sheet.png, and move/screen/dmk_lands1.h"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

W, H = 128, 64
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
NAMES = ["Peaks", "Lake", "Coast", "Night", "Room", "Desk", "Forest", "Living", "Villa", "California", "Harbour",
         "Lighthouse", "City"]


class C:
    """a 1-bit canvas: True is lit"""

    def __init__(self, seed):
        self.a = np.zeros((H, W), bool)
        self.r = np.random.default_rng(seed)

    def set(self, x, y, v=True):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < H:
            self.a[y, x] = v

    def rect(self, x, y, w, h, v=True):
        x, y, w, h = int(x), int(y), int(w), int(h)
        self.a[max(y, 0):max(y + h, 0), max(x, 0):max(x + w, 0)] = v

    def frame(self, x, y, w, h, v=True):
        self.rect(x, y, w, 1, v); self.rect(x, y + h - 1, w, 1, v)
        self.rect(x, y, 1, h, v); self.rect(x + w - 1, y, 1, h, v)

    def line(self, x0, y0, x1, y1, v=True):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            self.set(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, v)

    def disc(self, cx, cy, r, v=True):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r * .4:
                    self.set(x, y, v)

    def ring(self, cx, cy, r, v=True):
        for i in range(int(r * 7) + 8):
            t = i / (int(r * 7) + 8) * 6.2832
            self.set(cx + r * np.cos(t), cy + r * np.sin(t), v)

    def poly(self, pts, v=True):
        im = Image.new("1", (W, H), 0)
        ImageDraw.Draw(im).polygon([(int(x), int(y)) for x, y in pts], fill=1)
        self.a[np.asarray(im, bool)] = v

    def dots(self, x, y, w, h, level, v=True):
        """a box, LEVEL sixteenths of it lit by the ordered pattern"""
        for j in range(max(int(y), 0), min(int(y + h), H)):
            for i in range(max(int(x), 0), min(int(x + w), W)):
                if BAYER[j & 3, i & 3] < level:
                    self.a[j, i] = v

    def sky(self, y0, y1, l0, l1):
        """the sky's glow, rows of the ordered pattern from L0 to L1 sixteenths"""
        for y in range(int(y0), int(y1)):
            t = (y - y0) / max(y1 - y0 - 1, 1)
            self.dots(0, y, W, 1, int(round(l0 + (l1 - l0) * t)))

    def stars(self, n, y0, y1):
        for _ in range(n):
            self.set(self.r.integers(0, W), self.r.integers(y0, y1))

    def ridge(self, ys, rim=True):
        """a land: everything under the line ys[x] black, the line itself lit"""
        for x in range(W):
            y = int(ys[x])
            self.a[max(y, 0):, x] = False
            if rim:
                self.set(x, y)

    def walk(self, y, amp, step=3):
        """a ridge's line, a small random walk round y"""
        v = np.zeros(W)
        h = 0.0
        for x in range(W):
            if x % step == 0:
                h = float(np.clip(h + self.r.integers(-1, 2), -amp, amp))
            v[x] = y + h
        return v

    def halo(self, m, lit):
        """a shape M over what is there: a black pixel round it, so it never melts into the dots; LIT drawn in it"""
        g = m.copy()
        g[1:, :] |= m[:-1, :]; g[:-1, :] |= m[1:, :]; g[:, 1:] |= m[:, :-1]; g[:, :-1] |= m[:, 1:]
        self.a[g] = False
        self.a[lit] = True

    def spruce(self, cx, base, h, w, rim=True, fill=0):
        """a spruce: tiers drooping, one silhouette, its zigzag outline lit round a black body (FILL sixteenths of dots)"""
        im = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(im)
        tiers = max(int(h / 6), 2)
        for t in range(tiers):
            top = base - h + t * h * .8 / tiers
            bot = top + h * 1.3 / tiers
            hw = (t + 1.2) * w / 2 / (tiers + .2)
            d.polygon([(cx, top), (cx - hw, bot), (cx + hw, bot)], fill=1)
        d.rectangle([cx, base - 1, cx, base + 1], fill=1)
        m = np.asarray(im, bool)
        e = m.copy()
        e[1:, :] &= m[:-1, :]; e[:-1, :] &= m[1:, :]; e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
        self.a[m] = False
        if fill:
            for y, x in zip(*np.nonzero(m)):
                self.a[y, x] = BAYER[y & 3, x & 3] < fill
        if rim:
            self.a[m & ~e] = True

    def cloud(self, cx, cy, w):
        """a small cloud: flat below, lobes on top, an outline"""
        for i, (dx, r) in enumerate(((-.3, .2), (0, .28), (.3, .18))):
            self.disc(cx + dx * w, cy, r * w)
        self.rect(cx - w / 2, cy, w, w / 2, False)

    def palm(self, x, y, h, lean, frond):
        """a palm: a curved trunk, six fronds rising a little, then falling away, cut clear of the sky"""
        keep = self.a.copy()
        self.a[:] = False
        px, py = x, y
        for i in range(int(h)):
            t = i / h
            px, py = x + lean * t * t, y - i
            self.rect(px, py, 2 if h > 24 and i < h * .6 else 1, 1)
        for d, l, rise, fall in ((1, 1, .3, 1.0), (-1, 1, .3, 1.0), (.75, .75, .62, .8), (-.75, .75, .62, .8),
                                 (1, .7, -.1, 1.3), (-1, .7, -.1, 1.3)):
            n = int(frond * l * 1.7) + 2
            for i in range(n):
                t = i / (n - 1)
                self.set(px + d * frond * l * t, py - frond * rise * np.sin(t * 2.3) + frond * fall * t * t * .5)
        m = self.a.copy()
        self.a[:] = keep
        self.halo(m, m)

    def sun(self, cx, cy, r, stripes=True, horizon=None):
        """a sun; low, cut by bars widening towards its foot"""
        self.disc(cx, cy, r)
        if stripes:
            y, gap = cy, 1
            while y < cy + r + 1:
                self.rect(cx - r - 1, y, 2 * r + 3, gap, False)
                y += gap + 2
                gap = gap + 1 if gap < 2 else gap
        if horizon is not None:
            self.rect(0, horizon, W, H - horizon, False)


def peaks(c):
    c.stars(16, 0, 18)
    c.sky(14, 44, 0, 5)
    c.disc(17, 11, 4)
    c.cloud(38, 15, 16)
    c.rect(90, 13, 0, 0)
    c.cloud(104, 20, 14)
    # the mountain: its left lit, its right in dots, the snow's edge in teeth
    c.poly([(70, 7), (22, 46), (118, 46)], False)
    c.poly([(70, 7), (56, 19), (60, 23), (64, 19), (68, 25), (72, 20), (77, 25), (81, 20), (86, 21)])
    c.dots(70, 12, 18, 14, 0)
    for y in range(8, 46):
        x0 = 70 + (y - 7) * 1.23
        c.set(x0, y)
        c.set(70 - (y - 7) * 1.23, y)
    c.dots(72, 24, 40, 22, 2)
    c.poly([(0, 46), (0, 64), (128, 64), (128, 46)], False)
    # the tree line, the ground
    c.ridge(c.walk(50, 1))
    for x in range(3, 128, 6):
        c.spruce(x + c.r.integers(-1, 2), 53, c.r.integers(10, 16), 7)
    for y, l in ((56, 1), (59, 2), (62, 3)):
        c.dots(0, y, W, 2, l)


def lake(c):
    c.stars(6, 0, 12)
    c.sky(8, 34, 1, 5)
    c.cloud(28, 13, 16)
    c.cloud(84, 11, 14)
    c.disc(92, 30, 4)
    c.ridge(c.walk(31, 2, 4))
    c.rect(0, 36, W, H - 36, False)
    # the water: lines, the sun's road
    for y in range(38, 64, 3):
        for x in range(c.r.integers(0, 7), W, 9 + (y - 38) // 4):
            c.rect(x, y, 3 + (y - 38) // 6, 1)
    for i, y in enumerate(range(36, 64, 2)):
        w = 3 + i
        c.rect(92 - w // 2, y, w, 1)
    # the near shore
    c.poly([(0, 36), (0, 46), (20, 44), (44, 38), (60, 36)], False)
    c.line(0, 46, 20, 44); c.line(20, 44, 44, 38); c.line(44, 38, 60, 36)
    c.poly([(128, 34), (128, 50), (112, 44), (102, 36)], False)
    for x, h in ((4, 26), (13, 32), (21, 22), (122, 32), (113, 24), (105, 16)):
        c.spruce(x, 40 + (x > 100) * 3, h, 13)
    # the cabin, its four windows the synths (two pixels wider than the FM-1's three-window one)
    c.poly([(25, 34), (34, 26), (43, 34)], False)
    c.line(25, 34, 34, 26); c.line(34, 26, 43, 34)
    c.rect(27, 34, 15, 7, False)
    c.frame(27, 34, 15, 7)
    for x in (29, 32, 35, 38):
        c.rect(x, 36, 2, 2)


def coast(c):
    c.sky(0, 36, 1, 6)
    c.cloud(30, 11, 18)
    c.cloud(76, 8, 14)
    c.sun(86, 33, 11, horizon=36)
    # the sea: lines closing towards the horizon, the sun's road
    for i, y in enumerate((37, 38, 40, 42, 45, 48, 51)):
        for x in range(c.r.integers(0, 6), W, 8):
            c.rect(x, y, 2 + i // 2, 1)
    for i, y in enumerate(range(36, 52, 2)):
        c.rect(86 - 3 - i, y, 6 + 2 * i, 1)
    # the beach
    c.rect(0, 53, W, H - 53, False)
    c.line(0, 53, 127, 54)
    c.dots(0, 56, W, 8, 3)
    # the lifeguard's hut
    c.rect(101, 22, 15, 9, False)
    c.frame(101, 22, 15, 9)
    c.rect(104, 25, 4, 3)
    c.poly([(99, 22), (108, 17), (118, 22)], False)
    c.line(99, 22, 108, 17); c.line(108, 17, 118, 22)
    for x in (103, 113):
        c.line(x, 31, x - 2, 54)
    c.line(101, 42, 116, 42)
    # the palms
    c.palm(10, 62, 46, 12, 15)
    c.palm(36, 60, 30, 7, 11)


def night(c):
    c.stars(34, 0, 34)
    c.disc(92, 15, 8)
    c.ring(92, 15, 12)
    c.disc(89, 13, 1, False); c.disc(95, 18, 1, False); c.disc(93, 11, 0, False)
    c.cloud(24, 21, 16)
    c.dots(0, 30, W, 10, 1)
    c.ridge(c.walk(33, 3, 4))
    c.dots(0, 38, W, 5, 1)
    c.ridge(c.walk(41, 2, 3))
    c.ridge(48 + 2 * np.sin(np.arange(W) / 11))
    for x in list(range(3, 44, 8)) + list(range(92, 128, 8)):
        c.spruce(x, 51, c.r.integers(14, 24), 11)
    for x, y in ((46, 52), (58, 56), (70, 53), (79, 58), (52, 60), (64, 61), (87, 55), (38, 58)):
        c.rect(x, y, 2, 1)
        c.set(x, y - 1)


def room(c):
    c.a[:] = False
    # the window, the night and the city in it
    x0, y0, x1, y1 = 22, 3, 98, 42
    c.stars(16, y0, 20)
    c.a[:, :x0] = False; c.a[:, x1:] = False; c.a[y1:, :] = False
    c.disc(82, 11, 4)
    for x, w, h in ((24, 8, 11), (33, 6, 17), (40, 9, 9), (50, 6, 21), (57, 9, 13), (67, 7, 18), (75, 8, 8), (84, 12, 14)):
        c.rect(x, y1 - h, w, h, False)
        c.frame(x, y1 - h, w, h + 1)
        for yy in range(y1 - h + 2, y1 - 1, 3):
            for xx in range(x + 2, x + w - 1, 2):
                if c.r.integers(0, 3):
                    c.set(xx, yy)
    c.rect(x0, y0, x1 - x0, 1); c.rect(x0, y1, x1 - x0, 1)
    c.rect(x0, y0, 1, y1 - y0); c.rect(x1, y0, 1, y1 - y0 + 1)
    c.rect(60, y0, 1, y1 - y0)
    c.rect(x0, 19, x1 - x0, 1)
    # the curtains
    for x in range(1, 21, 3):
        c.rect(x, 0, 1, 50)
    for x in range(101, 111, 3):
        c.rect(x, 0, 1, 50)
    c.rect(0, 0, 112, 1)
    # the sill, the desk, a plant, a mug
    c.rect(14, 45, 98, 1)
    c.rect(18, 46, 90, 1)
    c.poly([(30, 44), (26, 37), (31, 40), (34, 34), (36, 40), (42, 36), (38, 44)])
    c.rect(32, 44, 4, 1)
    c.frame(66, 41, 5, 4)
    c.rect(71, 42, 1, 2)
    # the lamp, its glow
    c.rect(119, 26, 1, 22)
    c.poly([(113, 26), (116, 17), (122, 17), (125, 26)])
    c.dots(108, 27, 20, 20, 2)
    c.rect(112, 48, 15, 1)
    # the floor, the pattern of the rug
    c.dots(0, 54, W, 10, 3)
    c.rect(0, 52, W, 1)


def desk(c):
    # the monitor, its screen the synths' bars
    c.rect(11, 3, 56, 40)
    c.rect(13, 5, 52, 36, False)
    c.frame(16, 7, 46, 29)
    c.rect(19, 10, 22, 1); c.rect(45, 10, 8, 1)
    c.rect(19, 14, 40, 1)
    for i, h in enumerate((5, 9, 12, 7, 4, 8, 13, 10, 6, 3, 7, 11)):
        c.rect(20 + i * 3, 33 - h, 2, h)
    c.rect(29, 43, 20, 2)
    c.rect(20, 45, 38, 1)
    # the keyboard
    for y in (49, 51):
        for x in range(13, 58, 2):
            c.set(x, y)
    c.frame(10, 47, 51, 7)
    # the deck
    c.frame(72, 28, 26, 13)
    c.ring(79, 34, 2); c.ring(91, 34, 2)
    c.rect(82, 33, 6, 3)
    c.rect(74, 38, 22, 1)
    # the disc, the books
    c.ring(112, 32, 9)
    c.ring(112, 32, 3)
    c.dots(104, 24, 17, 17, 0)
    c.set(112, 32)
    for x, h, top in ((102, 16, 8), (105, 15, 9), (108, 16, 8), (111, 14, 9), (114, 15, 8), (117, 13, 10)):
        c.rect(x, top, 2, h - 2)
    # the desk's edge, its grain
    c.rect(0, 55, W, 1)
    c.dots(0, 57, W, 7, 1)


def forest(c):
    c.stars(10, 0, 12)
    c.sky(6, 40, 1, 6)
    c.dots(0, 40, W, 14, 7)                 # the fog
    for x in range(1, 128, 7):              # far: dots, small, half lost in it
        c.spruce(x + c.r.integers(-2, 3), 50, c.r.integers(16, 26), 9, rim=False, fill=3)
    for x, h in ((9, 44), (27, 34), (42, 52), (60, 38), (76, 47), (93, 36), (107, 54), (123, 40)):
        c.spruce(x, 54, h, 15 + h // 4)
    c.rect(0, 54, W, H - 54, False)
    c.line(0, 54, 127, 54)
    for _ in range(46):                     # the meadow's flowers, glowing
        x, y = c.r.integers(0, W), c.r.integers(56, H)
        c.set(x, y)
        if y > 59 and c.r.integers(0, 2):
            c.set(x + 1, y)


def living(c):
    # the blinds
    c.frame(101, 2, 25, 19)
    for y in range(4, 20, 2):
        c.rect(103, y, 21, 1)
    # the set: a CRT with its aerial, a sunset on it
    c.line(62, 15, 53, 3); c.line(62, 15, 72, 4)
    c.rect(38, 15, 48, 30, False)
    c.frame(38, 15, 48, 30)
    c.frame(41, 18, 36, 24)
    c.dots(42, 19, 34, 12, 4)
    c.disc(59, 31, 6)
    c.rect(52, 28, 14, 1, False); c.rect(52, 31, 14, 1, False)
    c.rect(42, 32, 34, 9, False)
    for y in (33, 35, 38):
        c.rect(44 + y % 4, y, 28, 1)
    c.ring(81, 22, 1); c.ring(81, 28, 1)
    # the speakers
    for x in (10, 100):
        c.frame(x, 25, 20, 29)
        c.ring(x + 10, 31, 2)
        c.ring(x + 10, 44, 6); c.ring(x + 10, 44, 3)
        c.set(x + 10, 44)
    # the deck, the tapes
    c.frame(40, 47, 34, 6)
    c.rect(43, 49, 12, 2)
    c.set(69, 49); c.set(71, 49)
    for x in range(77, 88, 3):
        c.rect(x, 46, 2, 7)
    # the table, the floor
    c.rect(32, 53, 58, 1)
    c.rect(0, 56, W, 1)
    c.dots(0, 58, W, 6, 2)


def villa(c):
    c.sky(0, 26, 1, 5)
    c.disc(114, 14, 7)
    c.ring(114, 14, 10)
    c.rect(0, 28, W, H - 28, False)
    c.line(0, 28, 127, 28)
    # the house: its long roof, the glass
    c.rect(4, 12, 86, 3)
    c.rect(8, 15, 72, 19, False)
    c.rect(8, 15, 2, 19)
    c.rect(44, 15, 3, 19)
    c.rect(78, 15, 2, 19)
    for x0, x1 in ((10, 44), (47, 78)):
        for x in range(x0 + 3, x1, 7):
            c.line(x + 7, 16, x, 33)
    c.rect(0, 34, 96, 2)
    # the pine over the roof, the palm
    c.palm(96, 34, 19, 2, 8)
    # the pool: ripples, the coping
    c.rect(0, 37, W, H - 37, False)
    for i, y in enumerate((39, 42, 45, 49, 53, 58)):
        for x in range(c.r.integers(0, 8), W, 11 - i):
            c.rect(x, y, 3 + i, 1)
    c.line(127, 60, 96, 40)
    c.line(127, 55, 104, 40)


def california(c):
    c.sky(0, 30, 1, 6)
    c.stars(8, 0, 8)
    c.sun(64, 22, 13, horizon=30)
    c.rect(0, 30, W, 1)
    # the road
    c.rect(0, 31, W, H - 31, False)
    c.line(60, 31, 2, 63); c.line(68, 31, 125, 63)
    for y, h in ((33, 2), (37, 3), (42, 4), (48, 5), (55, 7)):
        w = 1 + (y - 31) // 12
        c.rect(64 - w // 2, y, w, h)
    c.dots(0, 42, 18, 22, 2)
    c.dots(110, 42, 18, 22, 2)
    # the wires, pole to pole, going away
    for x0, y0, x1, y1 in ((3, 6, 26, 19), (26, 19, 41, 25), (124, 6, 101, 19), (101, 19, 86, 25)):
        for i in range(abs(x1 - x0) + 1):
            t = i / abs(x1 - x0)
            c.set(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + 3 * 4 * t * (1 - t))
    for x, y0 in ((3, 6), (26, 19), (41, 25), (124, 6), (101, 19), (86, 25)):
        c.rect(x, y0, 1, 31 - y0 if x not in (3, 124) else 44)
    # the palms, going away
    for x, y, h, f in ((13, 60, 44, 14), (36, 42, 21, 8), (114, 60, 44, 14), (91, 42, 21, 8)):
        c.palm(x, y, h, 3 if x > 64 else -3, f)


def harbour(c):
    c.sky(0, 40, 2, 6)
    c.disc(26, 16, 6, False)
    c.disc(26, 16, 5)
    c.rect(0, 40, W, H - 40, False)
    c.line(0, 40, 127, 40)
    for x, s in ((22, 1.0), (52, .8), (84, 1.1)):
        h = 18 * s
        c.rect(x - 11 * s, 37, 22 * s, 3, False)
        c.poly([(x - 12 * s, 36), (x + 12 * s, 36), (x + 8 * s, 40), (x - 9 * s, 40)])
        c.rect(x, 36 - h, 1, h)
        c.poly([(x + 1, 36 - h), (x + 1, 34), (x + 9 * s, 34)], False)
        c.line(x + 1, 36 - h, x + 9 * s, 34); c.line(x + 1, 34, x + 9 * s, 34)
        c.poly([(x - 1, 36 - h * .8), (x - 1, 34), (x - 6 * s, 34)], False)
        c.line(x - 1, 36 - h * .8, x - 6 * s, 34); c.line(x - 1, 34, x - 6 * s, 34)
        # its reflection, broken by the water
        for y in range(42, 42 + int(h), 2):
            w = (12 * s) * (1 - (y - 42) / h)
            c.rect(x - w / 2 + c.r.integers(-1, 2), y, max(w, 1), 1)
    for y in range(44, 64, 4):
        for x in range(c.r.integers(0, 9), W, 13):
            c.rect(x, y, 3, 1)
    # the pier, its lamp
    c.rect(104, 36, 24, 2)
    for x in range(106, 128, 5):
        c.rect(x, 38, 1, 22)
    c.rect(112, 18, 1, 18)
    c.disc(112, 15, 3)
    c.ring(112, 15, 6)


def lighthouse(c):
    c.stars(28, 0, 36)
    c.disc(30, 14, 6)
    c.disc(32, 13, 5, False)
    c.ring(30, 14, 0)
    # the beam, widening away from the lamp
    for x in range(12, 104):
        t = (104 - x) / 92
        y0, y1 = 9 - 6 * t, 10 + 18 * t
        for y in range(int(y0), int(y1) + 1):
            if 0 <= y < H and BAYER[y & 3, x & 3] < (4 if t < .5 else 2):
                c.set(x, y)
    # the sea
    c.rect(0, 44, W, H - 44, False)
    for i, y in enumerate((44, 46, 49, 52, 56, 60)):
        for x in range(c.r.integers(0, 7), W, 10):
            c.rect(x, y, 2 + i, 1)
    # the cliff
    pts = [(128, 64), (70, 64), (74, 54), (82, 50), (84, 42), (92, 37), (128, 33)]
    c.poly(pts, False)
    for a, b in zip(pts[1:], pts[2:]):
        c.line(a[0], a[1], b[0], b[1])
    c.dots(92, 44, 36, 20, 1)
    # the tower: bands, the lantern, the house
    c.rect(102, 11, 11, 25, False)
    c.poly([(104, 12), (111, 12), (113, 36), (102, 36)])
    for y in (17, 18, 19, 20, 25, 26, 27, 28):
        c.rect(100, y, 15, 1, False)
    c.rect(103, 5, 9, 1)
    c.rect(105, 7, 5, 4)
    c.rect(104, 11, 7, 1)
    c.poly([(104, 5), (107.5, 2), (111, 5)])
    c.rect(116, 28, 12, 7, False)
    c.frame(116, 28, 12, 7)
    c.rect(119, 30, 2, 2); c.rect(123, 30, 2, 2)


def city(c):
    c.stars(30, 0, 22)
    c.disc(108, 9, 3)
    c.sky(40, 54, 0, 4)
    x = 0
    far = [(0, 12, 26), (12, 9, 20), (21, 13, 31), (34, 8, 24), (42, 12, 36), (54, 10, 22), (64, 12, 30), (76, 9, 18),
           (85, 13, 38), (98, 9, 24), (107, 12, 32), (119, 9, 22)]
    for x, w, h in far:
        y = 58 - h
        c.rect(x, y, w, h, False)
        for yy in range(y + 2, 56, 2):
            for xx in range(x + 1, x + w - 1, 2):
                if c.r.integers(0, 10) < 3:
                    c.set(xx, yy)
    near = [(5, 14, 42), (26, 11, 50), (47, 14, 40), (70, 12, 54), (92, 14, 46), (114, 11, 36)]
    for x, w, h in near:
        y = 62 - h
        c.rect(x - 1, y - 1, w + 2, h + 1, False)
        c.frame(x, y, w, h + 1)
        for yy in range(y + 3, 61, 3):
            for xx in range(x + 2, x + w - 2, 2):
                if c.r.integers(0, 10) < 6:
                    c.rect(xx, yy, 1, 2)
        c.rect(x + w // 2, y - 5, 1, 5)
        c.set(x + w // 2, y - 6)
    c.rect(0, 62, W, 2, False)
    c.rect(0, 62, W, 1)


SCENES = [peaks, lake, coast, night, room, desk, forest, living, villa, california, harbour, lighthouse, city]


def header(arts, path):
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* generated by move/screen/lands.py: the 13 landscapes, 128 x 64, a bit a pixel, MSB first. Never edited by hand */",
           "static const uint8_t LANDS[13][16 * 64] = {"]
    for a in arts:
        b = np.packbits(a, axis=1)
        out.append("    {" + ",".join(str(int(v)) for v in b.ravel()) + "},")
    out += ["};", ""]
    Path(path).write_text("\n".join(out))


def main():
    d = Path(sys.argv[1])
    d.mkdir(parents=True, exist_ok=True)
    z, cols = 3, 4
    rows = (13 + cols - 1) // cols
    sheet = Image.new("L", (cols * (W * z + 12) + 12, rows * (H * z + 12) + 12), 48)
    arts = []
    for i, f in enumerate(SCENES):
        c = C(1000 + i)
        f(c)
        arts.append(c.a.copy())
        im = Image.fromarray((c.a * 255).astype(np.uint8), "L")
        im.save(d / f"scene{i:02d}.png")
        sheet.paste(im.resize((W * z, H * z), Image.NEAREST), (12 + (i % cols) * (W * z + 12), 12 + (i // cols) * (H * z + 12)))
    sheet.save(d / "sheet.png")
    header(arts, "move/screen/dmk_lands1.h")


main()
