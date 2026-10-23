/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's drawing: a 4-bit indexed framebuffer (16 colours, the theme's), fills, ordered dither, rounded tiles,
 * text in Tomato Grotesk (1-bit and 2-bit glyphs; a 2-bit glyph's middle tones are looked up per pixel against what
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

static inline int px_get(int x, int y)
{
    uint8_t b = fb[(y * DMK_W + x) >> 1];
    return x & 1 ? b & 15 : b >> 4;
}

static inline void px_put(int x, int y, int c)
{
    uint8_t *b;
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    b = &fb[(y * DMK_W + x) >> 1];
    *b = x & 1 ? (uint8_t)((*b & 0xF0) | c) : (uint8_t)((*b & 0x0F) | c << 4);
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
    uint8_t two = (uint8_t)(c << 4 | c);
    ta_hide(x, y, w, h);
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

/* a gradient from c0 to c1 across (horiz) or down the box, dithered */
static void grad(int x, int y, int w, int h, int c0, int c1, int horiz)
{
    int i, j, n = horiz ? w : h, k;
    ta_hide(x, y, w, h);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            k = horiz ? i : j;
            px_put(x + i, y + j, dith(x + i, y + j, n > 1 ? (k * 17) / n : 0) ? c1 : c0);
        }
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

/* a rounded tile filled with a dithered gradient, top to bottom */
static void tile_grad(int x, int y, int w, int h, int r, int c0, int c1, int line)
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
            else px_put(x + i, y + j, dith(x + i, y + j, h > 1 ? j * 17 / h : 0) ? c1 : c0);
        }
}

/* the late-90s bevel: lit top-left, dark bottom-right (sunken: the other way round) */
static void bevel(int x, int y, int w, int h, int sunken)
{
    int a = sunken ? C_SHADOW : C_HILITE, b = sunken ? C_HILITE : C_DARK, a2 = sunken ? C_DARK : C_FACE, b2 = sunken ? C_FACE : C_SHADOW;
    hline(x, y, w - 1, a); vline(x, y, h - 1, a);
    hline(x, y + h - 1, w, b); vline(x + w - 1, y, h, b);
    hline(x + 1, y + 1, w - 3, a2); vline(x + 1, y + 1, h - 3, a2);
    hline(x + 1, y + h - 2, w - 2, b2); vline(x + w - 2, y + 1, h - 2, b2);
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
                if (v == 3) px_put(X, Y, fg);
                else {
                    uint8_t m = T_AA[theme][fg][px_get(X, Y)];
                    px_put(X, Y, v == 1 ? m & 15 : m >> 4);
                }
            }
        x += g->adv + (ch == ' ' ? 0 : track);
    }
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
