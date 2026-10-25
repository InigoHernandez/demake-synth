#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's art, in the thermal light the owner chose (2026-10-23: a blue rim, a magenta body, a pale core; never a
rainbow). Our own, generated; the references were a bought gradient pack and are never copied.

  .venv/bin/python tools/gen_art.py

- web/img/art/{flow,pool,rise,wave,twin,drift,hero}.png (800 x 500, grey): fields of blobs strung along paths, their
  coordinates warped so the edges run like liquid; each pixel the heat, 0..255. The site maps it through the ground,
  a deep blue and the thermal ramp at 2 px a cell, dithered (8-bit, as everything lit). Seeds are fixed.
- web/img/wordmark.png, web/img/iso.png, web/img/icon.png: the wordmark, its D (the isotype, 48 cells) and the
  tab's icon (64), in
  Kiloy (the owner's copy, DMK_KILOY; only these renders are committed), lit exactly as the screen lights a shape:
  each pixel's depth in the shape (the chessboard distance to its edge), the ramp from the rim to as far in as the
  shape's size allows, the rings dithered by the 4 x 4 ordered dither (dmk_gfx.c, holo_done). One pixel here is a cell
  of the site's bit texture.
- web/img/plate.png, band.png, seal.png, frame288.png, frame528.png: graphics after Kiloy's specimens ("ambient" in
  line and in light by turns; the machine's words in three rows, ink on paper, every letter whole; a seal of ticks
  round the lit D) and the screens' lit frames.
- Every lit thing in six frames, one under another, its dither in the six arrangements of T_BAYER6: the site steps
  through them at random, as the screen does (the bits move, the light stays).
- web/img/bits/ring24_0..5.png, ring12_0..5.png: the lit state for any box, a 12 px ring of the ramp for
  border-image round a pale core (the action, the playing tile, a hover); glint_0..5.png, a link's letters lit.
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
    """the piece's heat, 0..255 (the ground to the core), as a grey PNG at half size: the site lays it through the
    system's tones at 2 px a cell, dithered, its bits moving (web/app.js, the heat)"""
    x, y = coords(seed, warp)
    f = field(x, y, path, sigma)
    f = np.clip(f / np.percentile(f, 99.7) * gain, 0, 1) ** 0.85
    im = Image.fromarray((f * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(7)).resize((W // 2, H // 2), Image.LANCZOS)
    OUT.mkdir(parents=True, exist_ok=True)
    im.save(OUT / f"{name}.png", optimize=True)
    print(f"web/img/art/{name}.png")


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def bayer6():
    """the ordered dither in six arrangements, as tools/gen_tables.py makes them (T_BAYER6): the bits' animation"""
    b = BAYER.reshape(-1).tolist()
    at = lambda x, y: b[(y & 3) * 4 + (x & 3)]
    T = [lambda x, y: at(x, y), lambda x, y: at(x + 2, y + 1), lambda x, y: at(3 - x, y),
         lambda x, y: at(x, 3 - y), lambda x, y: at(y, x), lambda x, y: at(x + 1, y + 3)]
    return [np.array([[t(x, y) for x in range(4)] for y in range(4)]) for t in T]


B6 = bayer6()
PAL = np.array([hexrgb(c) for c in THERMAL], np.uint8)
INK, PAPER = hexrgb("18181A"), hexrgb("F5F5F2")


def tones(p, pat):
    """a place on the ramp (sixteenths of a tone) to a tone, dithered by arrangement pat"""
    h, w = p.shape
    yy, xx = np.mgrid[0:h, 0:w]
    t = p >> 4
    return np.clip(np.where((t < 7) & (B6[pat][yy & 3, xx & 3] < (p & 15)), t + 1, t), 0, 7)


def depth(mask):
    """each pixel's depth in its shape: the chessboard distance to the nearest unlit pixel, capped at 20 (TH_DEEP)"""
    h, w = mask.shape
    d = np.where(mask, 20, 0).astype(np.int32)
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


def lit_p(mask, d=None):
    """a lit shape's place on the ramp per pixel, as holo_done lays it"""
    d = depth(mask) if d is None else d
    dmax = int(d.max()) or 1
    pmax = TH_PMAX[min(dmax, 9)]
    return np.where(d > 0, pmax if dmax == 1 else (d - 1) * pmax // (dmax - 1), 0)


def lit_rgb(mask, pat=0, p=None):
    return PAL[tones(lit_p(mask) if p is None else p, pat)]


def sprite(frames, path):
    """six frames, one under another: the site steps through them at random (web/app.js, the bits)"""
    h = frames[0].shape[0]
    out = np.zeros((h * len(frames),) + frames[0].shape[1:], np.uint8)
    for i, f in enumerate(frames):
        out[i * h:(i + 1) * h] = f
    Image.fromarray(out, "RGBA" if out.shape[2] == 4 else "RGB").save(path, optimize=True)


def lit_frames(mask):
    p = lit_p(mask)
    frames = []
    for k in range(6):
        f = np.zeros(mask.shape + (4,), np.uint8)
        f[mask, :3] = PAL[tones(p, k)][mask]
        f[mask, 3] = 255
        frames.append(f)
    return frames


def glyph_mask(s, size, w, h, inset=0, span=False, fit=False):
    """s in Kiloy at size px, its ink's left edge at inset, centred in h (span: its ink scaled to the full width, both
    edges on the grid's lines; fit: to the full box, all four edges on them); thresholded as the screen's 2-bit text"""
    f4 = ImageFont.truetype(str(KILOY), size * 4)
    l, t, r, b = f4.getbbox(s)
    im = Image.new("L", (r - l + 64, b - t + 64), 0)
    ImageDraw.Draw(im).text((32 - l, 32 - t), s, font=f4, fill=255)   # the whole word, never clipped
    box = im.getbbox()
    k = (w - 2 * inset) / (box[2] - box[0]) if span else 1 / 4
    size2 = (w, h) if fit else (round((box[2] - box[0]) * k), round((box[3] - box[1]) * k))
    ink = im.crop(box).resize(size2, Image.LANCZOS)
    m = Image.new("L", (w, h), 0)
    m.paste(ink, (inset, (h - ink.height) // 2))
    return np.array(m) >= 128


def letters(word, size, w, h):
    """each letter's mask, the word laid out by Kiloy's advances and its ink scaled to the full width, centred in h"""
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


def rr(n, m, x0, y0, x1, y1, r):
    """a rounded rectangle's mask on an n x m grid of cells, drawn at 4x and brought down"""
    im = Image.new("L", (n * 4, m * 4), 0)
    ImageDraw.Draw(im).rounded_rectangle((x0 * 4, y0 * 4, x1 * 4 - 1, y1 * 4 - 1), r * 4, fill=255)
    return np.array(im.resize((n, m), Image.LANCZOS)) >= 128


def ramp_in(outer, band):
    """the place on the ramp from a shape's outer edge (the rim) to band cells in (the core)"""
    d = np.minimum(depth(outer), band)
    return (d - 1) * 112 // max(band - 1, 1)


def big(a, k=2):
    """cells to pixels: each cell k x k, for the browser to show without scaling"""
    return np.repeat(np.repeat(a, k, 0), k, 1)


IMG = ROOT / "web" / "img"
BITS = IMG / "bits"


def marks():
    if not KILOY.exists():
        print(f"no Kiloy at {KILOY}: the wordmark, the isotype and the graphics are kept as they are")
        return False
    BITS.mkdir(parents=True, exist_ok=True)
    # the wordmark: 384 x 96 cells, its ink from line to line; the D, its ink filling 48 x 48 cells; the tab's icon
    sprite(lit_frames(glyph_mask("demake", 92, 384, 96, span=True)), IMG / "wordmark.png")
    stack()
    stack(("ambi", "ent"), "plate_stack.png", plate=True)
    stack(("ambi", "ent", "lofi", "mach", "ine"), "band_stack.png", lit=(3, 8, 15))   # the band for a phone (the owner:
                                                                                  # as big as the word), its letters lit
    sprite(lit_frames(glyph_mask("d", 44, 48, 48, fit=True)), IMG / "iso.png")
    Image.fromarray(lit_frames(glyph_mask("d", 58, 64, 64, fit=True))[0], "RGBA").save(IMG / "icon.png", optimize=True)
    print("web/img/wordmark.png, iso.png (six frames each), icon.png")
    return True


def stack(words=("dem", "ake"), path="wordmark_stack.png", plate=False, lit=None):
    """a word for a phone, after Kiloy's specimen (WORD over TYPE): its rows 168 cells wide (7 modules at 2 px a
    cell), the letters as big as the width allows with Kiloy's tight gaps, every row one height, the narrower
    justified to the edges; whole modules tall. Lit as the screen lights a shape, or (plate) in line and in light
    by turns, as the plate, or (lit: the letters' places) in ink with those lit, as the band"""
    W, gap, lead = 168, 5, 6
    f4 = ImageFont.truetype(str(KILOY), 800)
    rows = []
    for word in words:
        ls = []
        for ch in word:
            im = Image.new("L", (900, 1000), 0)
            ImageDraw.Draw(im).text((50, 50), ch, font=f4, fill=255)
            ls.append(im)
        rows.append(ls)
    top = min(im.getbbox()[1] for ls in rows for im in ls)
    bot = max(im.getbbox()[3] for ls in rows for im in ls)
    nat = [sum(im.getbbox()[2] - im.getbbox()[0] for im in ls) + gap * (len(ls) - 1) / 1 for ls in rows]
    k = min((W - gap * (len(ls) - 1)) / (n - gap * (len(ls) - 1)) for ls, n in zip(rows, nat))
    rh = round((bot - top) * k)
    H = (len(rows) * rh + lead * (len(rows) - 1) + 23) // 24 * 24     # whole modules
    masks = []                                          # each letter's mask in the frame
    y = (H - len(rows) * rh - lead * (len(rows) - 1)) // 2
    for ls in rows:
        cut = [im.crop((im.getbbox()[0], top, im.getbbox()[2], bot)) for im in ls]
        sized = [c.resize((max(1, round(c.width * k)), rh), Image.LANCZOS) for c in cut]
        free, g = W - sum(c.width for c in sized), len(sized) - 1
        x = 0
        for i, c in enumerate(sized):
            m = np.zeros((H, W), bool)
            w = min(c.width, W - x)
            m[y:y + rh, x:x + w] = (np.array(c) >= 128)[:, :w]
            masks.append(m)
            if i < g:
                x += c.width + free // g + (1 if i < free % g else 0)
        y += rh + lead
    if lit is not None:
        frames = []
        for fk in range(6):
            out = np.zeros((H, W, 4), np.uint8)
            for i, m in enumerate(masks):
                out[m, :3] = lit_rgb(m, fk)[m] if i in lit else INK
                out[m, 3] = 255
            frames.append(out)
    elif not plate:
        frames = lit_frames(np.any(masks, axis=0))
    else:
        frames = []
        for fk in range(6):
            out = np.zeros((H, W, 4), np.uint8)
            for i, m in enumerate(masks):
                if i % 2:
                    out[m, :3] = lit_rgb(m, fk)[m]
                    out[m, 3] = 255
                else:
                    rim = m & (depth(m) == 1)
                    out[rim, :3] = INK
                    out[rim, 3] = 255
            frames.append(out)
    sprite(frames, IMG / path)
    print(f"web/img/{path}: {W} x {H} cells ({H // 24} modules), rows {rh} tall, six frames")


def graphics():
    """after Kiloy's specimens, with the machine's words; every lit thing in six frames"""
    # the plate: 576 x 144 cells, "ambient" in line and in light by turns, the line the rim in ink
    ms = letters("ambient", 120, 576, 144)
    frames = []
    for k in range(6):
        out = np.zeros((144, 576, 4), np.uint8)
        for i, m in enumerate(ms):
            if i % 2:
                out[m, :3] = lit_rgb(m, k)[m]
                out[m, 3] = 255
            else:
                rim = m & (depth(m) == 1)
                out[rim, :3] = INK
                out[rim, 3] = 255
        frames.append(out)
    sprite(frames, IMG / "plate.png")
    # the band: what it is, "ambient lofi machine", a word a row, all of one size (the widest row's ink from line to
    # line), every letter whole, ink on paper, one letter lit a row (after Kiloy's specimen of rows of letters)
    rows = [("ambient", 3), ("lofi", 1), ("machine", 4)]     # what it is, a word a row
    f4 = ImageFont.truetype(str(KILOY), 800)
    nat = [f4.getbbox(w) for w, _ in rows]
    widest = max(b[2] - b[0] for b in nat)
    k = 576 / widest
    hs = [round((b[3] - b[1]) * k) for b in nat]
    gap = 12
    H = (sum(hs) + gap * (len(rows) - 1) + 23) // 24 * 24   # whole modules at 2 px a cell
    masks = [letters(w, 200, round((b[2] - b[0]) * k), h) for (w, _), b, h in zip(rows, nat, hs)]
    frames = []
    for fk in range(6):
        band = np.zeros((H, 576, 4), np.uint8)
        y = (H - (sum(hs) + gap * (len(rows) - 1))) // 2
        for ms, (word, hot), h in zip(masks, rows, hs):
            for i, m in enumerate(ms):
                sl = band[y:y + h, :m.shape[1]]
                if i == hot:
                    sl[m, :3] = lit_rgb(m, fk)[m]
                else:
                    sl[m, :3] = INK
                sl[m, 3] = 255
            y += h + gap
        frames.append(band)
    sprite(frames, IMG / "band.png")
    # the seal: 96 x 96 cells, a ring of 60 ticks, ten of them lit, round the lit D
    yy, xx = np.mgrid[0:96, 0:96]
    r = np.hypot(xx - 47.5, yy - 47.5)
    a = (np.degrees(np.arctan2(yy - 47.5, xx - 47.5)) + 90) % 360
    ring = (np.abs(r - 46.5) < 0.6) | (np.abs(r - 33.5) < 0.6)
    tick = (r > 37) & (r < 43) & (np.abs((a + 3) % 6 - 3) < 0.9)
    d = glyph_mask("d", 60, 96, 96, 0)
    bx = np.argwhere(d)
    (y0, x0), (y1, x1) = bx.min(0), bx.max(0)
    dm = np.zeros_like(d)
    oy, ox = 48 - (y1 - y0 + 1) // 2, 48 - (x1 - x0 + 1) // 2
    dm[oy:oy + y1 - y0 + 1, ox:ox + x1 - x0 + 1] = d[y0:y1 + 1, x0:x1 + 1]
    frames = []
    for k in range(6):
        seal = np.zeros((96, 96, 4), np.uint8)
        seal[ring | tick] = (*INK, 255)
        seal[tick & (a < 60), :3] = PAL[4]
        seal[dm, :3] = lit_rgb(dm, k)[dm]
        seal[dm, 3] = 255
        frames.append(seal)
    sprite(frames, IMG / "seal.png")
    # the screens' frames: the padding round a screen lit from the frame's outer edge (the rim) to the screen's (the
    # core); 288 and 528 px at 2 px a cell, the screen 24 px in, corners 24 and 8
    for px, b, hr in ((288, 12, 4), (528, 12, 6), (432, 18, 6)):   # 432: a screen at 1.5x, 36 px in
        n = px // 2
        outer = rr(n, n, 0, 0, n, n, 12)
        ring = outer & ~rr(n, n, b, b, n - b, n - b, hr)
        p = ramp_in(outer, b)
        frames = []
        for k in range(6):
            f = np.zeros((n, n, 4), np.uint8)
            f[..., :3] = PAL[tones(p, k)]
            f[..., 3] = np.where(ring, 255, 0)
            frames.append(f)
        sprite(frames, IMG / f"frame{px}.png")
    # the lit state, for any box: a ring 12 px wide (6 cells), the ramp from its outer edge in, to be drawn by
    # border-image round a pale core (the active, the hover, the playing); corners 24 (pills, cards) and 12 (tiles),
    # in pixels already (2 x 2 a cell), six arrangements each
    for rad in (24, 12):
        n = (rad + 12) // 2 * 2 // 2 + 8                 # cells: the corner, the ring and a stretch of edge
        n = max(n, 2 * (rad // 2) + 8)
        outer = rr(n, n, 0, 0, n, n, rad // 2)
        inner = rr(n, n, 6, 6, n - 6, n - 6, max(rad // 2 - 6, 0))
        p = ramp_in(outer, 6)
        for k in range(6):
            f = np.zeros((n, n, 4), np.uint8)
            f[..., :3] = PAL[tones(p, k)]
            f[..., 3] = np.where(outer & ~inner, 255, 0)
            Image.fromarray(big(f), "RGBA").save(BITS / f"ring{rad}_{k}.png", optimize=True)
        print(f"web/img/bits/ring{rad}_0..5.png: {2 * n} px, slice {rad if rad >= 12 else 12}")
    # the hover's sparkle: a 48 px tile of the core's bits, a few cells pink, fewer magenta, six frames at random, for
    # what the pointer is on to shimmer without changing its light
    r = np.random.default_rng(11)
    for k in range(6):
        f = np.zeros((24, 24, 4), np.uint8)
        u = r.random((24, 24))
        f[u < 0.10, :3] = PAL[6]
        f[u < 0.03, :3] = PAL[4]
        f[u < 0.10, 3] = 255
        Image.fromarray(big(f), "RGBA").save(BITS / f"spark_{k}.png", optimize=True)
    glints()
    # the emulator's lit keys and buttons, in the panel's own units (2 a cell): a white key, a black key, a button,
    # an octave button, each lit from its edge in, six arrangements, for the SVG to show when the core lights them
    for name, w, h, r in (("keyw", 44, 112, 10), ("keyb", 38, 70, 10), ("btn", 40, 34, 12), ("oct", 48, 26, 12)):
        n, m = w // 2, h // 2
        mask = rr(n, m, 0, 0, n, m, r // 2)
        p = lit_p(mask)
        for k in range(6):
            f = np.zeros((m, n, 4), np.uint8)
            f[mask, :3] = PAL[tones(p, k)][mask]
            f[mask, 3] = 255
            Image.fromarray(big(f), "RGBA").save(BITS / f"{name}_{k}.png", optimize=True)
    print("web/img/plate.png, band.png (%d cells tall), seal.png, frame288.png, frame528.png, frame432.png: six frames each" % H)


def glints():
    """a link's light: a 48 px tile of the body's bits (magenta, with violet, blue and pink cells), six frames at random,
    clipped to the link's letters while the pointer is on it, so its words light as the screen lights thin text"""
    r = np.random.default_rng(13)
    for k in range(6):
        u = r.random((24, 24))
        idx = np.full((24, 24), 4)
        idx[u < 0.30] = 3
        idx[u < 0.16] = 2
        idx[u < 0.07] = 0
        idx[u > 0.90] = 5
        f = np.zeros((24, 24, 4), np.uint8)
        f[..., :3] = PAL[idx]
        f[..., 3] = 255
        Image.fromarray(big(f), "RGBA").save(BITS / f"glint_{k}.png", optimize=True)
    print("web/img/bits/glint_0..5.png: 48 px")


def main():
    if marks():
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
