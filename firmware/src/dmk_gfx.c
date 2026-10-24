/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's drawing: a 4-bit indexed framebuffer (the theme's 8 state colours and their 8 anti-aliasing tones), fills,
 * lines, rectangles, rounded tiles, text in Geist (2-bit glyphs whose middle tones are looked up per pixel against what
 * is under it, T_AA), and the text audit's records (host builds, DMK_TEXT_AUDIT). Included by dmk_core.c. */

static uint8_t fb[DMK_FB_BYTES] POOL;
static uint8_t theme;                                   /* THEMES in dmk_tables.h */
static int cx0, cy0, cx1 = DMK_W, cy1 = DMK_H;           /* the clip */

static void clip_set(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > DMK_W ? DMK_W : x + w; cy1 = y + h > DMK_H ? DMK_H : y + h;
}
static void clip_all(void) { cx0 = cy0 = 0; cx1 = DMK_W; cy1 = DMK_H; }

/* the thermal light (the owner's reference): a lit shape glows from its own edge in, a blue rim, a magenta body, a
 * pale core, read from each pixel's depth in its shape (the chessboard distance to the nearest unlit pixel), the
 * rings dithered into each other by the 4 x 4 ordered dither: the bit texture, its arrangement changed at random
 * every few frames (holo_pat, one of six), so the light stays and its bits move. HOLO as a colour marks the pixels
 * (HMARK); when the shape is drawn, holo_done() turns its marks into the ramp. Text and a hairline are the magenta
 * body; a thin bar a blue rim on it; only a big shape reaches the pale core */
#define HOLO (-1)
#define TH_MAX (DMK_W * 128)                             /* the largest lit shape's box, in pixels: a graphic's body */
static uint8_t th_d[TH_MAX] POOL;
static int hx0 = DMK_W, hy0 = DMK_H, hx1 = -1, hy1 = -1; /* the marks' box */
static int holo_hold;                                    /* >0: inside a shape made of several fills */
static int holo_flat;                                    /* text: all body, its strokes too thin for rings */
static uint8_t holo_pat;                                 /* which of the dither's six arrangements: the bits move */
/* the boot builds its light (as the site's splash): how much of a shape has arrived through the dither, sweeping in
 * from the left (of 256; a pixel not yet there shows holo_bg), and how hot it may glow (sixteenths of a tone: 0 the
 * blue rim alone, 112 the pale core). Outside the boot, all of it, as hot as its size allows */
static int holo_veil = 256, holo_heat = 255, holo_bg;
/* [the shape's greatest depth]: how far in its deepest ring goes, sixteenths of a tone (64: the magenta body; 112:
 * the core). Between the rim and that, the rings share the ramp, dithered */
static const uint8_t TH_PMAX[10] = {64, 64, 64, 76, 88, 100, 112, 112, 112, 112};
#define TH_DEEP 20                                       /* depth counts to here: a big shape's light fades over 20 px */

static inline int px_get(int x, int y)
{
    uint8_t b = fb[(y * DMK_W + x) >> 1];
    return x & 1 ? b & 15 : b >> 4;
}

static inline void px_put(int x, int y, int c)
{
    uint8_t *b;
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    if (c < 0) {
        c = C_HMARK;
        if (x < hx0) hx0 = x;
        if (x > hx1) hx1 = x;
        if (y < hy0) hy0 = y;
        if (y > hy1) hy1 = y;
    }
    b = &fb[(y * DMK_W + x) >> 1];
    *b = x & 1 ? (uint8_t)((*b & 0xF0) | c) : (uint8_t)((*b & 0x0F) | c << 4);
}

/* the marks to the ramp: depth by two chamfer passes over their box (outside it: unlit) */
static void holo_done(void)
{
    int w = hx1 - hx0 + 1, h = hy1 - hy0 + 1, x, y, i, dmax = 1, pmax;
    if (hx1 < 0 || holo_hold) return;
    if (w * h > TH_MAX) {                               /* too big a box: the body's tone, flat */
        for (y = hy0; y <= hy1; y++) for (x = hx0; x <= hx1; x++) if (px_get(x, y) == C_HMARK) px_put(x, y, C_HOLO4);
    } else {
        for (y = 0, i = 0; y < h; y++) for (x = 0; x < w; x++, i++) th_d[i] = px_get(hx0 + x, hy0 + y) == C_HMARK ? TH_DEEP : 0;
        for (y = 0, i = 0; y < h; y++)
            for (x = 0; x < w; x++, i++) {
                int m;
                if (!th_d[i]) continue;
                m = x ? th_d[i - 1] : 0;
                if (y) {
                    int u = th_d[i - w], ul = x ? th_d[i - w - 1] : 0, ur = x < w - 1 ? th_d[i - w + 1] : 0;
                    if (u < m) m = u;
                    if (ul < m) m = ul;
                    if (ur < m) m = ur;
                } else m = 0;
                if (m + 1 < th_d[i]) th_d[i] = (uint8_t)(m + 1);
            }
        for (y = h - 1, i = w * h - 1; y >= 0; y--)
            for (x = w - 1; x >= 0; x--, i--) {
                int m;
                if (!th_d[i]) continue;
                m = x < w - 1 ? th_d[i + 1] : 0;
                if (y < h - 1) {
                    int d = th_d[i + w], dl = x ? th_d[i + w - 1] : 0, dr = x < w - 1 ? th_d[i + w + 1] : 0;
                    if (d < m) m = d;
                    if (dl < m) m = dl;
                    if (dr < m) m = dr;
                } else m = 0;
                if (m + 1 < th_d[i]) th_d[i] = (uint8_t)(m + 1);
                if (th_d[i] > dmax) dmax = th_d[i];
            }
        /* the shape's own ramp: its rim blue, its deepest ring as far in as its size allows; a hairline is body */
        if (holo_flat && dmax > 2) holo_flat = 0;      /* a heavy face (the wordmark) keeps its rings */
        pmax = TH_PMAX[holo_flat ? 1 : dmax > 9 ? 9 : dmax];
        for (y = 0, i = 0; y < h; y++)
            for (x = 0; x < w; x++, i++) {
                int p, t, X = hx0 + x, Y = hy0 + y;
                if (!th_d[i]) continue;
                p = dmax > 1 && !holo_flat ? (th_d[i] - 1) * pmax / (dmax - 1) : pmax;
                if (p > holo_heat) p = holo_heat;
                if (holo_veil < 256 && T_BAYER6[0][(Y & 3) * 4 + (X & 3)] * 9 + X * 115 / DMK_W >= holo_veil) {
                    px_put(X, Y, holo_bg);
                    continue;
                }
                t = p >> 4;
                if (t < 7 && T_BAYER6[holo_pat][(Y & 3) * 4 + (X & 3)] < (p & 15)) t++;
                px_put(X, Y, C_HOLO0 + t);
            }
    }
    hx0 = DMK_W; hy0 = DMK_H; hx1 = hy1 = -1;
}

/* ---- the text audit (host builds): every text's ink box; a fill that covers one hides it */
#ifdef DMK_TEXT_AUDIT
typedef struct { int16_t x0, y0, x1, y1; uint8_t hidden, cut; char s[24]; } ta_rec_t;
static ta_rec_t ta[256];
static int ta_n, ta_bad;
static char ta_why[4][96];
static void ta_reset(void) { ta_n = 0; }
static void ta_hide(int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < ta_n; i++)
        if (ta[i].x0 >= x && ta[i].y0 >= y && ta[i].x1 <= x + w && ta[i].y1 <= y + h) ta[i].hidden = 1;
}
/* a window over the screen: every text under it, whole or part, is hidden by it */
static void ta_cover(int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < ta_n; i++)
        if (ta[i].x0 < x + w && ta[i].x1 >= x && ta[i].y0 < y + h && ta[i].y1 >= y) ta[i].hidden = 1;
}
static void ta_add(int x0, int y0, int x1, int y1, const char *s, int cut)
{
    int i;
    if (ta_n >= 256) return;
    ta[ta_n].x0 = (int16_t)x0; ta[ta_n].y0 = (int16_t)y0; ta[ta_n].x1 = (int16_t)x1; ta[ta_n].y1 = (int16_t)y1;
    ta[ta_n].hidden = 0; ta[ta_n].cut = (uint8_t)cut;
    for (i = 0; i < 23 && s[i]; i++) ta[ta_n].s[i] = s[i];
    ta[ta_n].s[i] = 0;
    ta_n++;
}
/* texts that touch or sit a dot apart, and texts cut by a clip or the screen: their number this frame */
static int ta_check(void)
{
    int i, j, n = 0;
    for (i = 0; i < ta_n; i++) {
        if (ta[i].hidden) continue;
        if (ta[i].cut) {
            if (n < 4) snprintf(ta_why[n], sizeof ta_why[n], "cut: \"%s\"", ta[i].s);
            n++;
        }
        for (j = i + 1; j < ta_n; j++) {
            if (ta[j].hidden) continue;
            if (ta[i].x0 <= ta[j].x1 + 1 && ta[j].x0 <= ta[i].x1 + 1 && ta[i].y0 <= ta[j].y1 + 1 && ta[j].y0 <= ta[i].y1 + 1) {
                if (n < 4) snprintf(ta_why[n], sizeof ta_why[n], "touch: \"%s\" / \"%s\"", ta[i].s, ta[j].s);
                n++;
            }
        }
    }
    ta_bad += n;
    return n;
}
#else
#define ta_hide(x, y, w, h) ((void)0)
#define ta_cover(x, y, w, h) ((void)0)
#endif

static void fill(int x, int y, int w, int h, int c)
{
    int i, j, x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y, x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    uint8_t two = (uint8_t)(c < 0 ? 0 : c << 4 | c);
    ta_hide(x, y, w, h);
    if (c < 0) {                                        /* lit: pixel by pixel, then to the ramp */
        for (j = y0; j < y1; j++) for (i = x0; i < x1; i++) px_put(i, j, c);
        holo_done();
        return;
    }
    for (j = y0; j < y1; j++) {
        i = x0;
        if (i & 1 && i < x1) px_put(i++, j, c);
        for (; i + 1 < x1; i += 2) fb[(j * DMK_W + i) >> 1] = two;
        if (i < x1) px_put(i, j, c);
    }
}

/* ordered dither: c1 on the share level / 16 of the pixels (0: all c0, 16: all c1) */
static inline int dith(int x, int y, int level) { return T_BAYER[(y & 3) * 4 + (x & 3)] < level; }

static void dither(int x, int y, int w, int h, int c0, int c1, int level)
{
    int i, j;
    ta_hide(x, y, w, h);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) px_put(i, j, dith(i, j, level) ? c1 : c0);
}

static void hline(int x, int y, int w, int c) { int i; for (i = 0; i < w; i++) px_put(x + i, y, c); }
static void vline(int x, int y, int h, int c) { int i; for (i = 0; i < h; i++) px_put(x, y + i, c); }
static void rect(int x, int y, int w, int h, int c) { hline(x, y, w, c); hline(x, y + h - 1, w, c); vline(x, y, h, c); vline(x + w - 1, y, h, c); }

/* the inset of a rounded corner of radius r on row i from the top (0 <= i < r): a quarter circle, integer */
static int corner_in(int r, int i)
{
    int d = r - i, k = 0;                               /* the widest k with k*k + d*d <= r*r, measured at pixel */
    while ((k + 1) * (k + 1) * 4 + (2 * d - 1) * (2 * d - 1) <= 4 * r * r) k++;   /* centres: half-pixel offsets */
    return r - k;
}

/* a rounded tile: filled with c (-1: not filled), its outline in line (-1: none) */
static int tile_r, tile_w, tile_h;
static int tile_in(int i, int j)                        /* (i, j) inside the tile's shape */
{
    int in;
    if (i < 0 || j < 0 || i >= tile_w || j >= tile_h) return 0;
    in = j < tile_r ? corner_in(tile_r, j) : j >= tile_h - tile_r ? corner_in(tile_r, tile_h - 1 - j) : 0;
    return i >= in && i < tile_w - in;
}

static void tile(int x, int y, int w, int h, int r, int c, int line)
{
    int i, j;
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    tile_r = r; tile_w = w; tile_h = h;
    ta_hide(x, y, w, h);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (!tile_in(i, j)) continue;
            if (line >= 0 && (!tile_in(i - 1, j) || !tile_in(i + 1, j) || !tile_in(i, j - 1) || !tile_in(i, j + 1)))
                px_put(x + i, y + j, line);
            else if (c >= 0) px_put(x + i, y + j, c);
        }
}

/* ---- Kiloy's shapes: rounded rectangles and pills, their edges anti-aliased like the text (four tones from T_AA;
 * the field takes no edge tones). The grid's rules stay straight; what sits in it is round */

/* the share of pixel (i, j), 0..16, inside a w x h rectangle with corners of radius r: 4 x 4 samples in a corner */
static int rr_cov(int i, int j, int w, int h, int r)
{
    int cx, cy, sx, sy, n = 0;
    if (i < 0 || j < 0 || i >= w || j >= h) return 0;
    cx = i < r ? r : i >= w - r ? w - r : -1;
    cy = j < r ? r : j >= h - r ? h - r : -1;
    if (cx < 0 || cy < 0) return 16;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int dx = 8 * i + 2 * sx + 1 - 8 * cx, dy = 8 * j + 2 * sy + 1 - 8 * cy;
            n += dx * dx + dy * dy <= 64 * r * r;
        }
    return n;
}

/* c over the pixel at share v of 16 */
static void px_cov(int x, int y, int c, int v)
{
    int l = (v * 3 + 8) >> 4;                           /* 0..3, as a glyph's levels */
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || !l) return;
    if (c < 0) { if (l >= 2) px_put(x, y, c); return; }
    if (l == 3) { px_put(x, y, c); return; }
    {
        uint8_t m = T_AA[theme][c][px_get(x, y)];
        px_put(x, y, l == 1 ? m & 15 : m >> 4);
    }
}

static int r_fit(int w, int h, int r) { if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2; return r < 0 ? 0 : r; }

/* a filled rounded rectangle; r = h / 2 makes a pill */
static void round_fill(int x, int y, int w, int h, int r, int c)
{
    int i, j;
    r = r_fit(w, h, r);
    if (w <= 0 || h <= 0) return;
    if (!r) { fill(x, y, w, h, c); return; }
    holo_hold++;
    fill(x + r, y, w - 2 * r, h, c);
    fill(x, y + r, r, h - 2 * r, c);
    fill(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            px_cov(x + i, y + j, c, rr_cov(i, j, w, h, r));
            px_cov(x + w - 1 - i, y + j, c, rr_cov(w - 1 - i, j, w, h, r));
            px_cov(x + i, y + h - 1 - j, c, rr_cov(i, h - 1 - j, w, h, r));
            px_cov(x + w - 1 - i, y + h - 1 - j, c, rr_cov(w - 1 - i, h - 1 - j, w, h, r));
        }
    holo_hold--;
    if (c < 0) holo_done();
}

/* a rounded rectangle's outline, t pixels thick */
static void round_line(int x, int y, int w, int h, int r, int t, int c)
{
    int i, j, k;
    r = r_fit(w, h, r);
    if (!r) { for (k = 0; k < t; k++) rect(x + k, y + k, w - 2 * k, h - 2 * k, c); return; }
    for (k = 0; k < t; k++) {                           /* the straight runs */
        hline(x + r, y + k, w - 2 * r, c); hline(x + r, y + h - 1 - k, w - 2 * r, c);
        vline(x + k, y + r, h - 2 * r, c); vline(x + w - 1 - k, y + r, h - 2 * r, c);
    }
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            int q[4][2] = {{i, j}, {w - 1 - i, j}, {i, h - 1 - j}, {w - 1 - i, h - 1 - j}};
            for (k = 0; k < 4; k++) {
                int a = rr_cov(q[k][0], q[k][1], w, h, r);
                int b = rr_cov(q[k][0] - t, q[k][1] - t, w - 2 * t, h - 2 * t, r - t > 0 ? r - t : 0);
                px_cov(x + q[k][0], y + q[k][1], c, a > b ? a - b : 0);
            }
        }
    if (c < 0) holo_done();
}

/* ---- text */
static int utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    int c = *p++;
    if (c >= 0xF0) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; }
    else if (c >= 0xE0) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
    else if (c >= 0xC0) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

static const dmk_glyph_t *glyph(const dmk_font_t *f, int c)
{
    int i;
    if (c >= 32 && c < 127) return &f->g[c - 32];
    for (i = 95; i < f->n; i++) if (f->code[i] == c) return &f->g[i];
    return &f->g['?' - 32];
}

static int track;                                       /* extra pixels between letters (text_fit tightens it) */

static int text_w(const char *s, const dmk_font_t *f)
{
    int w = 0, n = 0;
    while (*s) { int c = utf8_next(&s); w += glyph(f, c)->adv + (c == ' ' ? 0 : track); n++; }
    return n ? w - track : 0;
}

/* draws s with its line's top at y; returns the x after it. 2-bit glyphs take their middle tones from T_AA against
 * each pixel underneath, so text sits right on any flat colour (never put it on dither) */
static int text(int x, int y, const char *s, const dmk_font_t *f, int fg)
{
    int x0 = x, ix0 = 9999, iy0 = 9999, ix1 = -9999, iy1 = -9999, cut = 0;
    const char *s0 = s;
    while (*s) {
        int ch = utf8_next(&s);
        const dmk_glyph_t *g = glyph(f, ch);
        const uint8_t *d = f->data + g->off;
        int gx = x + g->x, gy = y + f->ascent - g->top, i, j;
        int bpr = f->bits == 1 ? (g->w + 7) >> 3 : (g->w + 3) >> 2;
        for (j = 0; j < g->h; j++)
            for (i = 0; i < g->w; i++) {
                int v, X = gx + i, Y = gy + j;
                if (f->bits == 1) v = d[j * bpr + (i >> 3)] >> (7 - (i & 7)) & 1 ? 3 : 0;
                else v = d[j * bpr + (i >> 2)] >> (6 - 2 * (i & 3)) & 3;
                if (!v) continue;
                if (X < ix0) ix0 = X;
                if (X > ix1) ix1 = X;
                if (Y < iy0) iy0 = Y;
                if (Y > iy1) iy1 = Y;
                if (X < cx0 || Y < cy0 || X >= cx1 || Y >= cy1) { cut = 1; continue; }
                if (fg < 0) { if (v >= 2) px_put(X, Y, fg); }   /* the field: its body, no edge tones */
                else if (v == 3) px_put(X, Y, fg);
                else {
                    uint8_t m = T_AA[theme][fg][px_get(X, Y)];
                    px_put(X, Y, v == 1 ? m & 15 : m >> 4);
                }
            }
        x += g->adv + (ch == ' ' ? 0 : track);
    }
    if (fg < 0) { holo_flat = 1; holo_done(); holo_flat = 0; }
#ifdef DMK_TEXT_AUDIT
    if (ix1 >= ix0) ta_add(ix0, iy0, ix1, iy1, s0, cut);
#else
    (void)s0; (void)cut; (void)iy0; (void)iy1; (void)ix0; (void)ix1;
#endif
    return x - track + (x > x0 ? 0 : track);
}

/* centred on cx */
static void text_c(int cx, int y, const char *s, const dmk_font_t *f, int fg) { text(cx - text_w(s, f) / 2, y, s, f, fg); }
static void text_r(int rx, int y, const char *s, const dmk_font_t *f, int fg) { text(rx - text_w(s, f), y, s, f, fg); }

/* fitted into w: tighter tracking first, then the string shortened with an ellipsis */
static void text_fit(int x, int y, int w, const char *s, const dmk_font_t *f, int fg)
{
    char b[48];
    int n = 0, k;
    track = 0;
    if (text_w(s, f) <= w) { text(x, y, s, f, fg); return; }
    track = -1;
    if (text_w(s, f) <= w) { text(x, y, s, f, fg); track = 0; return; }
    track = 0;
    while (s[n] && n < 40) n++;
    for (k = n; k > 0; k--) {
        int i;
        for (i = 0; i < k; i++) b[i] = s[i];
        while (k > 0 && ((uint8_t)b[k - 1] & 0xC0) == 0x80) k--;   /* not inside a UTF-8 character */
        b[k] = (char)0xE2; b[k + 1] = (char)0x80; b[k + 2] = (char)0xA6; b[k + 3] = 0;   /* … */
        if (text_w(b, f) <= w) break;
    }
    text(x, y, b, f, fg);
}

/* a number as text */
static char *fmt_int(char *o, int v)
{
    char t[12];
    int n = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    if (v < 0) *o++ = '-';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 11);
    while (n) *o++ = t[--n];
    *o = 0;
    return o;
}

static char *str_cat(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
