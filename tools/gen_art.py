#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's art, in the thermal light the owner chose (2026-10-23: a blue rim, a magenta body, a pale core; never a
rainbow). Our own, generated; the references were a bought gradient pack and are never copied.

  .venv/bin/python tools/gen_art.py

- web/img/art/{flow,pool,rise,wave,twin,drift}.jpg (1600 x 1000): fields of blobs strung along paths, their coordinates
  warped so the edges run like liquid, mapped through the ramp, grained. Seeds are fixed: a run makes the same files.
- web/img/wordmark.png, web/img/iso.png, web/img/icon.png: the wordmark, its D (the isotype, 48 cells) and the
  tab's icon (64), in
  Kiloy (the owner's copy, DMK_KILOY; only these renders are committed), lit exactly as the screen lights a shape:
  each pixel's depth in the shape (the chessboard distance to its edge), the ramp from the rim to as far in as the
  shape's size allows, the rings dithered by the 4 x 4 ordered dither (dmk_gfx.c, holo_done). One pixel here is a cell
  of the site's bit texture.
- web/img/plate.png, band.png, seal.png: graphics after Kiloy's specimens: a word of the sound in line and in light by
  turns; three rows of the machine's words too big for their rows, cropped, paper on ink; a seal, a ring of ticks
  round the lit D.
"""
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "web" / "img" / "art"
W, H = 1600, 1000

KILOY = Path(os.environ.get("DMK_KILOY", Path.home() / "Documents/Music/Git/Demake/Kiloy — Rounded Techno Font/Kiloy.ttf"))

# the screen's ramp, rim to core (tools/gen_tables.py, THERMAL), and how far in a shape of a depth reaches (TH_PMAX)
THERMAL = ["2459FF", "5A48F6", "8E3AEA", "C02FD2", "E52BC2", "FF45C2", "FF8CDA", "FFDDF2"]
TH_PMAX = [64, 64, 64, 76, 88, 100, 112, 112, 112, 112]
BAYER = np.array([0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]).reshape(4, 4)

# the art's field: a dark ground, then the thermal ramp from the rim in (no amber: the owner's choice)
HEAT = [(0.00, (8, 8, 12)), (0.20, (8, 8, 12)), (0.34, (16, 34, 150)), (0.46, (36, 89, 255)), (0.56, (90, 72, 246)),
        (0.64, (142, 58, 234)), (0.72, (229, 43, 194)), (0.82, (255, 69, 194)), (0.91, (255, 140, 218)),
        (1.00, (255, 230, 246))]


def ramp(t, stops):
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        m = (t >= p0) & (t <= p1)
        k = ((t - p0) / max(p1 - p0, 1e-6))[m][:, None]
        out[m] = np.array(c0) * (1 - k) + np.array(c1) * k
    return out


def coords(seed, warp):
    """the page's coordinates, warped by slow waves so the shapes' edges run like liquid"""
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    x, y = x / W, y / H * (H / W)
    r = np.random.default_rng(seed)
    for _ in range(3):
        fx, fy, px, py = r.uniform(0.8, 2.6), r.uniform(0.8, 2.6), r.uniform(0, 6.3), r.uniform(0, 6.3)
        x, y = x + warp * np.sin(fy * y * 6.28 + py), y + warp * np.sin(fx * x * 6.28 + px)
    return x, y


def field(x, y, path, sigma):
    """blobs at the path's points, each its own size"""
    f = np.zeros_like(x)
    for (cx, cy), s in zip(path, sigma):
        f += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * s * s))
    return f


def walk(seed, start, step, n, wander):
    r = np.random.default_rng(seed)
    p, pts = np.array(start, np.float32), []
    for _ in range(n):
        pts.append(tuple(p))
        p = p + np.array(step) + r.normal(0, wander, 2)
    return pts


def render(name, path, sigma, seed, warp=0.035, gain=1.0):
    x, y = coords(seed, warp)
    f = field(x, y, path, sigma)
    f = np.clip(f / np.percentile(f, 99.7) * gain, 0, 1) ** 0.85
    img = ramp(f, HEAT)
    img = np.asarray(Image.fromarray(img.astype(np.uint8)).filter(ImageFilter.GaussianBlur(7)), np.float32)
    r = np.random.default_rng(seed + 100)
    img += r.normal(0, 11, (H, W, 1)).astype(np.float32) + r.normal(0, 3, (H, W, 3)).astype(np.float32)
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(OUT / f"{name}.jpg", quality=86, optimize=True)
    print(f"web/img/art/{name}.jpg")


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def depth(mask):
    """each pixel's depth in its shape: the chessboard distance to the nearest unlit pixel, capped at 9"""
    h, w = mask.shape
    d = np.where(mask, 9, 0).astype(np.int32)
    for y in range(h):                                   # the two chamfer passes
        for x in range(w):
            if d[y, x]:
                m = min(d[y, x - 1] if x else 0, d[y - 1, x] if y else 0, d[y - 1, x - 1] if x and y else 0,
                        d[y - 1, x + 1] if y and x < w - 1 else 0)
                d[y, x] = min(d[y, x], m + 1)
    for y in range(h - 1, -1, -1):
        for x in range(w - 1, -1, -1):
            if d[y, x]:
                m = min(d[y, x + 1] if x < w - 1 else 0, d[y + 1, x] if y < h - 1 else 0,
                        d[y + 1, x + 1] if x < w - 1 and y < h - 1 else 0, d[y + 1, x - 1] if x and y < h - 1 else 0)
                d[y, x] = min(d[y, x], m + 1)
    return d


def lit_rgb(mask, d=None):
    """the thermal tones of a lit shape, per pixel (RGB; meaningless where unlit)"""
    h, w = mask.shape
    d = depth(mask) if d is None else d
    dmax = int(d.max()) or 1
    pmax = TH_PMAX[dmax]
    p = np.where(d > 0, pmax if dmax == 1 else (d - 1) * pmax // (dmax - 1), 0)
    t = p >> 4
    yy, xx = np.mgrid[0:h, 0:w]
    t = np.where((t < 7) & (BAYER[yy & 3, xx & 3] < (p & 15)), t + 1, t)
    return np.array([hexrgb(c) for c in THERMAL], np.uint8)[t]


def lit(mask):
    """a mask lit as the screen lights it: RGBA, transparent where unlit"""
    out = np.zeros(mask.shape + (4,), np.uint8)
    out[..., :3] = lit_rgb(mask)
    out[..., 3] = np.where(mask, 255, 0)
    return Image.fromarray(out, "RGBA")


def glyph_mask(s, size, w, h, inset=0, span=False, fit=False):
    """s in Kiloy at size px, its ink's left edge at inset, centred in h (span: its ink scaled to the full width, both
    edges on the grid's lines; fit: to the full box, all four edges on them); thresholded as the screen's 2-bit text"""
    f4 = ImageFont.truetype(str(KILOY), size * 4)
    l, t, r, b = f4.getbbox(s)
    im = Image.new("L", (r - l + 64, b - t + 64), 0)    # the whole word, never clipped
    ImageDraw.Draw(im).text((32 - l, 32 - t), s, font=f4, fill=255)
    box = im.getbbox()
    k = (w - 2 * inset) / (box[2] - box[0]) if span else 1 / 4
    size2 = (w, h) if fit else (round((box[2] - box[0]) * k), round((box[3] - box[1]) * k))
    ink = im.crop(box).resize(size2, Image.LANCZOS)
    m = Image.new("L", (w, h), 0)
    m.paste(ink, (inset, (h - ink.height) // 2))
    return np.array(m) >= 128


def marks():
    if not KILOY.exists():
        print(f"no Kiloy at {KILOY}: the wordmark, the isotype and the icon are kept as they are")
        return
    # the wordmark: 384 x 96 cells, shown at 2 px a cell (16 x 4 of the site's 48 px modules), ink from x = 0
    wm = glyph_mask("demake", 92, 384, 96, span=True)
    lit(wm).save(ROOT / "web" / "img" / "wordmark.png", optimize=True)
    # the isotype: the D, its ink filling 48 x 48 cells (two by two modules at 2 px a cell, every edge on a line),
    # and the tab's icon, 64 x 64
    lit(glyph_mask("d", 44, 48, 48, fit=True)).save(ROOT / "web" / "img" / "iso.png", optimize=True)
    lit(glyph_mask("d", 58, 64, 64, fit=True)).save(ROOT / "web" / "img" / "icon.png", optimize=True)
    print("web/img/wordmark.png, iso.png, icon.png")


INK, PAPER = hexrgb("18181A"), hexrgb("F5F5F2")


def letters(word, size, w, h):
    """each letter's mask, the word laid out by Kiloy's advances and its ink scaled to the full width"""
    f4 = ImageFont.truetype(str(KILOY), size * 4)
    l, t, r, b = f4.getbbox(word)
    W, H = r - l + 64, b - t + 64
    full = Image.new("L", (W, H), 0)
    ImageDraw.Draw(full).text((32 - l, 32 - t), word, font=f4, fill=255)
    box = full.getbbox()
    k = w / (box[2] - box[0])
    out = []
    for i, ch in enumerate(word):
        im = Image.new("L", (W, H), 0)
        ImageDraw.Draw(im).text((32 - l + f4.getlength(word[:i]), 32 - t), ch, font=f4, fill=255)
        ink = im.crop(box).resize((w, round((box[3] - box[1]) * k)), Image.LANCZOS)
        m = Image.new("L", (w, h), 0)
        m.paste(ink, (0, (h - ink.height) // 2))
        out.append(np.array(m) >= 128)
    return out


def graphics():
    """after Kiloy's specimens: letters in line and in light; the word cropped as a pattern; a seal of ticks"""
    if not KILOY.exists():
        return
    # the plate: 576 x 144 cells (24 x 6 modules at 2 px): a word of the sound, its letters in line and in light by
    # turns; the line the rim, in ink
    out = np.zeros((144, 576, 4), np.uint8)
    for i, m in enumerate(letters("ambient", 120, 576, 144)):
        if i % 2:
            out[m, :3] = lit_rgb(m)[m]
        else:
            rim = m & (depth(m) == 1)
            out[rim, :3] = INK
            m = rim
        out[m, 3] = 255
    Image.fromarray(out, "RGBA").save(OUT.parent / "plate.png", optimize=True)
    # the band: 576 x 192 cells, three rows of the machine's words, each too tall for its row and run off the sides,
    # paper on ink, one letter lit (after Kiloy's specimen of cropped rows)
    band = np.zeros((192, 576, 3), np.uint8)
    band[:] = INK
    for r, (word, off, hot) in enumerate([("lofisnow", 40, -1), ("tapedrone", 150, 4), ("echochord", 90, -1)]):
        hgt = 84
        ms = letters(word, 200, round(hgt * 1.04 * len(word)), hgt)
        for i, m in enumerate(ms):
            m = m[10:74, off:off + 576]                  # the row: 64 cells, cropped top and bottom, run off the sides
            if m.shape[1] < 576:
                m = np.pad(m, ((0, 0), (0, 576 - m.shape[1])))
            sl = band[r * 64:(r + 1) * 64]
            if i == hot:
                sl[m] = lit_rgb(m)[m]
            else:
                sl[m] = PAPER
    Image.fromarray(band).save(OUT.parent / "band.png", optimize=True)
    # the seal: 96 x 96 cells, a ring of 60 ticks (a meter around), ten of them lit, the D lit in the middle
    seal = np.zeros((96, 96, 4), np.uint8)
    yy, xx = np.mgrid[0:96, 0:96]
    cx = cy = 47.5
    r = np.hypot(xx - cx, yy - cy)
    a = (np.degrees(np.arctan2(yy - cy, xx - cx)) + 90) % 360
    ring = (np.abs(r - 46.5) < 0.6) | (np.abs(r - 33.5) < 0.6)
    tick = (r > 37) & (r < 43) & (np.abs((a + 3) % 6 - 3) < 0.9)
    seal[ring | tick] = (*INK, 255)
    on = tick & (a < 60)
    seal[on, :3] = hexrgb(THERMAL[4])
    d = glyph_mask("d", 60, 96, 96, 0)
    dm = np.zeros_like(d)
    bx = np.argwhere(d)
    (y0, x0), (y1, x1) = bx.min(0), bx.max(0)
    dm[48 - (y1 - y0 + 1) // 2:48 - (y1 - y0 + 1) // 2 + (y1 - y0 + 1), 48 - (x1 - x0 + 1) // 2:48 - (x1 - x0 + 1) // 2 + (x1 - x0 + 1)] = d[y0:y1 + 1, x0:x1 + 1]
    seal[dm, :3] = lit_rgb(dm)[dm]
    seal[dm, 3] = 255
    Image.fromarray(seal, "RGBA").save(OUT.parent / "seal.png", optimize=True)
    # the screens' frames: the padding round a screen lit, the thermal ramp from the frame's outer edge (the rim) to
    # the screen's (the core), dithered: 288 and 528 px at 2 px a cell, the screen's hole 24 px in, corners 24 and 8
    for px in (288, 528):
        n, b = px // 2, 12
        big = Image.new("L", (n * 4, n * 4), 0)
        ImageDraw.Draw(big).rounded_rectangle((0, 0, n * 4 - 1, n * 4 - 1), 12 * 4, fill=255)
        outer = np.array(big.resize((n, n), Image.LANCZOS)) >= 128
        hole = Image.new("L", (n * 4, n * 4), 0)
        ImageDraw.Draw(hole).rounded_rectangle((b * 4, b * 4, (n - b) * 4 - 1, (n - b) * 4 - 1), 4 * 4, fill=255)
        hole = np.array(hole.resize((n, n), Image.LANCZOS)) >= 128
        d = np.minimum(depth(outer), b)                  # from the outer edge in, capped at the band
        ring = outer & ~hole
        p = (d - 1) * 112 // (b - 1)
        t = p >> 4
        yy, xx = np.mgrid[0:n, 0:n]
        t = np.where((t < 7) & (BAYER[yy & 3, xx & 3] < (p & 15)), t + 1, t)
        fr = np.zeros((n, n, 4), np.uint8)
        fr[..., :3] = np.array([hexrgb(c) for c in THERMAL], np.uint8)[np.clip(t, 0, 7)]
        fr[..., 3] = np.where(ring, 255, 0)
        Image.fromarray(fr, "RGBA").save(OUT.parent / f"frame{px}.png", optimize=True)
    print("web/img/plate.png, band.png, seal.png, frame288.png, frame528.png")


def main():
    marks()
    graphics()
    a = 0.625                                            # the page's height in its own units
    flow = walk(1, (0.02, 0.02), (0.075, 0.045), 14, 0.03)
    render("flow", flow, [0.05 + 0.03 * ((i * 7) % 3) for i in range(len(flow))], 1)
    pool = [(0.05, 0.05), (0.18, 0.12), (0.32, 0.22), (0.12, 0.3)] + walk(2, (0.4, 0.3), (0.07, 0.03), 7, 0.02)
    render("pool", pool, [0.14, 0.09, 0.06, 0.07] + [0.035] * 7, 2, gain=1.1)
    rise = walk(3, (0.55, a + 0.05), (0.01, -0.07), 12, 0.035)
    render("rise", rise, [0.07 - 0.003 * i for i in range(12)], 3, warp=0.045)
    wave = [(i / 12, 0.42 + 0.06 * np.sin(i * 1.1)) for i in range(13)]
    render("wave", wave, [0.06] * 13, 4, warp=0.03, gain=0.95)
    twin = walk(5, (0.12, 0.45), (0.06, -0.025), 6, 0.02) + walk(6, (0.9, 0.08), (-0.06, 0.03), 6, 0.02)
    render("twin", twin, [0.05] * 12, 5)
    r = np.random.default_rng(7)
    drift = [(r.uniform(0, 1), r.uniform(0, a)) for _ in range(9)]
    render("drift", drift, [r.uniform(0.03, 0.07) for _ in range(9)], 7, warp=0.03)
    hero = walk(8, (-0.02, 0.32), (0.08, -0.01), 15, 0.025)
    render("hero", hero, [0.09] * 15, 8, warp=0.04, gain=1.05)


if __name__ == "__main__":
    main()
