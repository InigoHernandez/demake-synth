/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the 128 x 64 screen, drawn on the drawer's thread from the published page state alone.
 *   0-8     the page and its rows; the synths as letters, the one in view in a tag, REC, the transport
 *   10-52   the stage: the page's one picture, what it is above it
 *   54-63   the eight knobs: a meter each, a choice as dots
 * Home is the landscape across the whole screen, the Roll its grid, knobs on touch. A knob touched or just turned takes
 * the stage: its name, its value large, a long meter. Dark but for what it says: a lit band held still would age the
 * OLED, so a shape is its outline over calm dots, and solid only while it sounds. */
#include "dmk_type.h"
#include "dmk_lands1.h"

#define FCAP 5                         /* the type's capitals, rows */
#define PY0 19                         /* a picture under its name: from this row */
#define PY1 52                         /* to this one */
#define MS_READY "Ready: a pad starts"

static void ms_set(uint8_t *o, int c, int r, int on)
{
    if (c < 0 || c >= MS_W || r < 0 || r >= MS_H) return;
    if (on) o[r * 16 + (c >> 3)] |= (uint8_t)(0x80 >> (c & 7));
    else o[r * 16 + (c >> 3)] &= (uint8_t)~(0x80 >> (c & 7));
}
static int ms_get(const uint8_t *o, int c, int r)
{
    return c >= 0 && c < MS_W && r >= 0 && r < MS_H ? o[r * 16 + (c >> 3)] >> (7 - (c & 7)) & 1 : 0;
}
static void ms_rect(uint8_t *o, int x, int y, int w, int h, int on)
{
    for (int r = y; r < y + h; r++) for (int c = x; c < x + w; c++) ms_set(o, c, r, on);
}
static void ms_frame(uint8_t *o, int x, int y, int w, int h)
{
    ms_rect(o, x + 1, y, w - 2, 1, 1); ms_rect(o, x + 1, y + h - 1, w - 2, 1, 1);
    ms_rect(o, x, y + 1, 1, h - 2, 1); ms_rect(o, x + w - 1, y + 1, 1, h - 2, 1);
}
/* a pill's shape: a box with its four corners off */
static void ms_pill(uint8_t *o, int x, int y, int w, int h, int on)
{
    ms_rect(o, x, y, w, h, on);
    ms_set(o, x, y, !on); ms_set(o, x + w - 1, y, !on); ms_set(o, x, y + h - 1, !on); ms_set(o, x + w - 1, y + h - 1, !on);
}
static void ms_line(uint8_t *o, int x0, int y0, int x1, int y1)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        ms_set(o, x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
/* the calm dots: level of each 16 pixels lit, ordered */
static void ms_sky(uint8_t *o, int x, int y, int level)
{
    static const uint8_t B[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    if (B[y & 3][x & 3] < level) ms_set(o, x, y, 1);
}
/* a pure number from two: what looks random stays the same from frame to frame */
static uint32_t ms_hash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u) * 2246822519u;
    h ^= h >> 15;
    return h * 3266489917u >> 8;
}
/* a triangle from a phase of 256: -128..128, near enough a sine for this screen */
static int ms_wave(int ph)
{
    const int p = ph & 255, t = p & 127, v = 4 * t * (128 - t) / 128;
    return p < 128 ? v : -v;
}

static int ms_w(const char *s, int z)
{
    int w = 0;
    for (; *s; s++) w += *s >= 32 && *s < 127 ? TYPE[*s - 32][0] * z : 0;
    return w ? w - z : 0;
}

/* s from (x, y), its capitals' top on row y, stopped before x + w; z its size; on its colour. The pen's end */
static int ms_text(uint8_t *o, int x, int y, int w, const char *s, int z, int on)
{
    const int end = x + w;
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        const uint16_t *g = TYPE[*s - 32];
        if (x + (g[0] - 1) * z > end) break;
        for (int c = 0; c < 8; c++)
            for (int r = 0; r < 10; r++)
                if (g[1 + c] >> r & 1) ms_rect(o, x + c * z, y + r * z, z, z, on);
        x += g[0] * z;
    }
    return x;
}
static void ms_text_c(uint8_t *o, int cx, int y, const char *s, int z, int on) { ms_text(o, cx - ms_w(s, z) / 2, y, MS_W, s, z, on); }

/* the FM-1's meter, quiet: a dotted range, solid to the value, the value a mark */
static void ms_meter(uint8_t *o, int x, int y, int w, int h, int raw)
{
    const int m = raw * (w - 1) / 127, base = y + h - 2;
    for (int i = 0; i < w; i += 2) ms_set(o, x + i, base + 1, 1);
    ms_rect(o, x, base, m + 1, 2, 1);
    ms_rect(o, x + m, y + (h > 5 ? 2 : 0), 1, h - (h > 5 ? 2 : 0), 1);
}
/* a choice: its places as dots, the one chosen larger */
static void ms_dots(uint8_t *o, int x, int y, int w, int n, int idx)
{
    for (int i = 0; i < n; i++) {
        const int c = n > 1 ? x + 1 + i * (w - 3) / (n - 1) : x + w / 2;
        if (i == idx) ms_rect(o, c - 1, y + 2, 3, 3, 1);
        else ms_set(o, c, y + 3, 1);
    }
}

static void ms_knobs(uint8_t *o, const mv_view_t *v)
{
    for (int i = 0; i < 8; i++) {
        const mv_knob_t *k = &v->knob[i];
        const int x = 1 + i * 16;
        if (!k->on) continue;
        if (k->n && k->n <= 6) ms_dots(o, x, 56, 14, k->n, k->idx);
        else ms_meter(o, x, 56, 13, 7, k->raw);
    }
}

static void ms_header(uint8_t *o, const mv_view_t *v)
{
    int x = ms_text(o, 2, 1, 70, v->page, 1, 1);
    if (v->rows > 1) {                                  /* its rows: as the FM-1's: a short pill, dots */
        x += 2;
        for (int i = 0; i < v->rows; i++) {
            if (i == v->mrow) { ms_rect(o, x, 3, 4, 2, 1); x += 6; }
            else { ms_rect(o, x, 3, 2, 2, 1); x += 4; }
        }
    }
    if (v->pic.kind == PIC_ROLL && v->bars > 1) {       /* a roll of bars: which of them the steps show */
        char b[6] = {(char)('1' + v->bar % 9), '/', (char)('0' + v->bars % 10), 0, 0, 0};
        ms_text(o, x + 3, 1, 20, b, 1, 1);
    }
    for (int s = 0; s < 3; s++) {                       /* the synths */
        const char c[2] = {v->letters[s], 0};
        const int cx = 86 + s * 10;
        if (s == v->synth) { ms_pill(o, cx - 2, 0, 9, FCAP + 2, 1); ms_text(o, cx, 1, 8, c, 1, 0); }
        else if (!(v->mutes >> s & 1)) ms_text(o, cx, 1, 8, c, 1, 1);
        else ms_rect(o, cx, 3, 4, 1, 1);
    }
    if (v->rec) ms_rect(o, 117, 2, 3, 3, 1);
    if (v->playing || (v->ready && v->t >> 2 & 1)) { ms_rect(o, 123, 1, 1, 5, 1); ms_rect(o, 124, 2, 1, 3, 1); ms_rect(o, 125, 3, 1, 1, 1); }
    else ms_rect(o, 123, 2, 3, 3, 1);
}

/* what the picture is, on the line above it */
static void ms_title(uint8_t *o, const mv_pic_t *p)
{
    ms_text(o, 2, 11, 90, p->name, 1, 1);
    if (p->tag[0]) ms_text(o, 126 - ms_w(p->tag, 1), 11, 60, p->tag, 1, 1);
}

/* a note: its rim alone, quiet; sounding, filled */
static void ms_note(uint8_t *o, int x, int y, int w, int h, int lit)
{
    if (lit) { ms_pill(o, x, y, w, h, 1); return; }
    if (w < 3 || h < 3) { ms_rect(o, x, y, w, h, 1); return; }
    ms_frame(o, x, y, w, h);
}

/* the grid: sixteen cells, seven rows, a dot at each corner as far as the roll goes; nothing else but the notes */
#define RY 12                          /* its top */
#define RH 7                           /* a row */
static void ms_grid(uint8_t *o, int cols, int y0, int rh)
{
    for (int c = 0; c <= cols; c++)
        for (int r = 0; r <= 7; r++) ms_set(o, c == 16 ? 127 : c * 8, y0 + r * rh, 1);
}

static void ms_roll(uint8_t *o, const mv_view_t *v)
{
    const mv_roll_t *w = &v->roll;
    ms_grid(o, w->end >= 0 ? (w->end + 7) / 8 : 16, RY, RH);
    for (int i = 0; i < w->count; i++) {
        const mv_note_t *n = &w->n[i];
        if (n->row < 0) { ms_rect(o, n->x + 1, RY - 2, n->w, 1, 1); continue; }   /* above the view, below it */
        if (n->row > 6) { ms_rect(o, n->x + 1, RY + 7 * RH + 1, n->w, 1, 1); continue; }
        ms_note(o, n->x + 1, RY + n->row * RH + 1, n->w, RH - 1, n->lit);
    }
    if (w->head >= 0) {                                 /* the playhead, or the cursor: its cell's line */
        const int x = w->head + 1, n = w->cw8 / 8 > 1 ? w->cw8 / 8 - 1 : 1;
        for (int c = x; c < x + n; c++) ms_set(o, c, RY - 2, 1);
    }
}

/* the arp: its order on a chord of four over its octaves, sixteen notes, each as long as its gate */
static int arp_at(int mode, int k, int m)
{
    const int k2 = m > 1 ? k % (2 * m - 2) : 0;
    switch (mode) {
    case 2: return m - 1 - k % m;                                       /* Down */
    case 3: return k2 < m ? k2 : 2 * m - 2 - k2;                        /* Hill */
    case 4: return k2 < m ? m - 1 - k2 : k2 - m + 1;                    /* Valley */
    case 5: { const int p = k % (2 * m); return p < m ? p : 2 * m - 1 - p; }   /* Bounce */
    case 6: { const int p = k % m; return p & 1 ? m - 1 - p / 2 : p / 2; }     /* Fold */
    case 7: { const int p = k % m, mid = (m - 1) / 2, a = p & 1 ? mid + (p + 1) / 2 : mid - p / 2; return a < m ? a : m - 1; }   /* Unfold */
    case 8: return (int)(ms_hash((uint32_t)k, 7u) % (uint32_t)m);       /* Dice */
    case 9: { int a = m / 2; for (int i = 0; i <= k; i++) { a += ms_hash((uint32_t)i, 3u) & 1u ? 1 : -1; if (a < 0) a = 1; if (a >= m) a = m - 2; } return a; }   /* Walk */
    case 12: return k2 & 1 ? k2 / 2 + 1 : 0;                            /* Thumb */
    case 13: return k2 & 1 ? m - 1 : k2 / 2;                            /* Pinky */
    default: return k % m;                                              /* Up, Played */
    }
}
static void ms_arp(uint8_t *o, const mv_pic_t *p)
{
    const int rh = 5, y0 = 17, m = 4 * p->p[0], cw = 2 + p->p[1] * 5 / 127;
    ms_title(o, p);
    if (!p->type) { for (int x = 2; x < 126; x += 2) ms_set(o, x, 36, 1); return; }
    for (int k = 0; k < 16; k++) {
        const int lit = k == p->live[0], x = k * 8 + 1;
        if (p->type == 11) {                            /* Chord: all of it, an octave higher each step */
            const int base = (k % p->p[0]) * 7 / 4;
            for (int i = 0; i < 3; i++) if (base + i < 7) ms_note(o, x, y0 + (6 - base - i) * rh + 1, cw, rh - 1, lit);
            continue;
        }
        const int at = arp_at(p->type, k, m), row = m > 1 ? 6 - at * 6 / (m - 1) : 6;
        ms_note(o, x, y0 + row * rh + 1, cw, rh - 1, lit);
    }
}

static void ms_pan(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    for (int t = 0; t < 3; t++) {
        const int y = 13 + t * 13, x0 = 62, w = 62, m = x0 + p->p[t] * (w - 1) / 127;
        if (t == v->synth) { ms_pill(o, 0, y - 2, 60, FCAP + 4, 1); ms_text(o, 3, y, 56, p->line[t], 1, 0); }
        else ms_text(o, 3, y, 56, p->line[t], 1, 1);
        for (int x = x0; x < x0 + w; x += 2) ms_set(o, x, y + 2, 1);
        ms_rect(o, x0 + w / 2, y, 1, 5, 1);
        ms_rect(o, m - 1, y - 1, 3, 7, 0);
        ms_rect(o, m, y - 1, 2, 7, 1);
    }
}

/* a cell: plain a rim, the cursor's two pixels thick, the one sounding solid; its word on row ty */
static void ms_cell(uint8_t *o, int x, int y, int w, int h, const char *s, int z, int ty, int now, int cur)
{
    if (now) ms_pill(o, x, y, w, h, 1);
    else ms_frame(o, x, y, w, h);
    if (cur) ms_frame(o, x + 1, y + 1, w - 2, h - 2);
    ms_text_c(o, x + w / 2, ty, s, z, !now);
}
static void ms_song(uint8_t *o, const mv_pic_t *p)
{
    ms_title(o, p);
    for (int i = 0; i < p->cells; i++) {
        const mv_cell_t *c = &p->cell[i];
        const int x = 2 + i * 25, y = PY0 + 1, now = i == p->now;
        ms_cell(o, x, y, 24, 31, c->name, 2, c->name[0] == '+' ? y + 11 : y + 3, now, i == p->cur);
        for (int b = 0; b < c->bars && b < 8; b++)      /* its bars: ticks */
            ms_rect(o, x + 12 - c->bars + b * 2, y + 17, 1, 3, !now);
        if (c->name[0] != '+')
            for (int s = 0; s < 3; s++) {               /* its scene's synths: a dot each, a ring muted */
                const int dx = x + 6 + s * 5;
                if (c->mutes >> s & 1) ms_set(o, dx + 1, y + 25, !now);
                else ms_rect(o, dx, y + 24, 3, 3, !now);
            }
    }
}

static void ms_chord(uint8_t *o, const mv_pic_t *p)
{
    ms_title(o, p);
    ms_text(o, 2, 21, 124, p->big[0] ? p->big : "-", 2, 1);
    for (int i = 0; i < p->cells; i++)
        ms_cell(o, 2 + i * 31, 39, 30, 11, p->cell[i].name, 1, 42, i == p->now, i == p->cur);
}

/* the sound's shape: its outline, mirrored, dots in it; the loop's ends and its start tall lines, a voice a solid one */
static void ms_sound(uint8_t *o, const mv_pic_t *p)
{
    const int mid = 36, w = 124, x0 = 2;
    int ya[124], last = -1;
    ms_title(o, p);
    for (int i = 0; i < w; i++) {
        const int f = i * 47 * 16 / (w - 1), a = f >> 4, t = f & 15, b = a < 47 ? a + 1 : 47;
        ya[i] = (p->env[a] * (16 - t) + p->env[b] * t) * 15 / (255 * 16);
        if (ya[i] < 1) ya[i] = 1;
    }
    for (int i = 0; i < w; i++) {
        const int h = ya[i], x = x0 + i, top = last < 0 ? h : last;
        for (int y = h < top ? h : top; y <= (h > top ? h : top); y++)
            if (p->on || !(x & 1)) { ms_set(o, x, mid - y, 1); ms_set(o, x, mid + y, 1); }
        if (p->on) for (int y = mid - h + 1; y < mid + h; y++) ms_sky(o, x, y, 3);
        last = h;
    }
    if (p->on && p->p[0])
        for (int i = 1; i <= 2; i++) {                  /* the loop */
            const int x = x0 + p->p[i] * (w - 1) / 127, h = ya[x - x0] + 3;
            ms_rect(o, x, mid - h, 1, 2 * h + 1, 1);
            ms_rect(o, x + (i == 1 ? 0 : -1), mid - h, 2, 1, 1);
            ms_rect(o, x + (i == 1 ? 0 : -1), mid + h, 2, 1, 1);
        }
    if (p->p[3]) ms_rect(o, x0 + p->p[3] * (w - 1) / 127, PY0, 1, PY1 - PY0 + 1, 1);   /* where it starts */
    for (int i = 0; i < 4; i++)
        if (p->head[i]) {                               /* each voice in it */
            const int x = x0 + (p->head[i] - 1) * (w - 1) / 127, h = ya[x - x0];
            ms_rect(o, x - 1, mid - h - 1, 3, 2 * h + 3, 0);
            ms_rect(o, x, mid - h - 1, 1, 2 * h + 3, 1);
        }
}

/* a curve from its heights: joined, dots under it */
static void ms_curve(uint8_t *o, int x0, int w, const int16_t *ys, int base, int level)
{
    for (int i = 0; i < w; i++) {
        const int y = ys[i], l = i ? ys[i - 1] : y;
        for (int r = y < l ? y : l; r <= (y > l ? y : l); r++) ms_set(o, x0 + i, r, 1);
        for (int r = y + 1; r <= base; r++) ms_sky(o, x0 + i, r, level);
    }
}

/* the filter's response over knobs 1-4: the cutoff where the knob puts it, the resonance a peak there, the slope of its
 * type; the envelope's reach a mark on the floor, a note playing a small one under it */
static void ms_filter(uint8_t *o, const mv_pic_t *p)
{
    int16_t ys[60];
    const int x0 = 2, w = 60, base = PY1, top = PY0 + 1, ly = top + 9, type = p->type;
    const int q = p->p[1] * 9 / 127, fc = x0 + p->p[0] * (w - 1) / 127, env = p->p[2] - 64;
    int f1 = fc, f2 = fc;
    if (type == FT_VOWEL) {                             /* the vowel's two formants, where the cutoff puts them */
        const int c = p->p[0] * 2 > 255 ? 255 : p->p[0] * 2, pos = c * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
        f1 = x0 + ((VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8) * (w - 1) / 255;
        f2 = x0 + ((VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8) * (w - 1) / 255;
    }
    for (int i = 0; i < w; i++) {
        const int x = x0 + i, dx = x - fc, ad = dx < 0 ? -dx : dx;
        int y = ly;
        if (type == FT_LP24 || type == FT_LP12 || type == FT_BP) { if (dx > 0) y = ly + dx * (type == FT_LP24 ? 3 : type == FT_LP12 ? 2 : 2); }
        if ((type == FT_HP || type == FT_BP) && dx < 0) y = ly + ad * (type == FT_HP ? 3 : 2);
        if (type <= FT_BP && ad < 5) y -= q * (5 - ad) / 5;
        if (type == FT_NOTCH && ad < 7 - q / 3) y = ly + (7 - q / 3 - ad) * 3;
        if (type == FT_PEAK && ad < 6) y = ly - (6 - ad) * (3 + q) / 6;
        if (type == FT_VOWEL) {
            const int a1 = x > f1 ? x - f1 : f1 - x, a2 = x > f2 ? x - f2 : f2 - x;
            y = ly + 13;
            if (a1 < 5) y -= (5 - a1) * (14 + q) / 5;
            if (a2 < 5) y -= (5 - a2) * (10 + q) / 5;
        }
        ys[i] = (int16_t)(y < top ? top : y > base ? base : y);
    }
    ms_curve(o, x0, w, ys, base, 3);
    if (type != FT_OFF) ms_rect(o, fc - 1, ys[fc - x0] - 1, 3, 3, 1);
    if (env && type != FT_OFF) {
        int ex = fc + env * (w - 1) / 127;
        ex = ex < x0 ? x0 : ex > x0 + w - 1 ? x0 + w - 1 : ex;
        ms_rect(o, ex < fc ? ex : fc, base, (ex < fc ? fc - ex : ex - fc) + 1, 1, 1);
        ms_rect(o, ex, base - 2, 1, 3, 1);
    }
    if (p->live[0] >= 0) { const int lx = x0 + p->live[0] * (w - 1) / 127; ms_rect(o, lx - 1, base - 4, 3, 3, 0); ms_rect(o, lx, base - 4, 1, 3, 1); }
}

/* the envelope over knobs 5-8: a corner each, a note playing a point along it */
static void ms_amp(uint8_t *o, const mv_pic_t *p)
{
    const int x0 = 66, w = 60, base = PY1, top = PY0 + 1, peak = base - (base - top) * p->p[3] / 127;
    const int a = 1 + p->p[4] * 15 / 127, d = 1 + p->p[5] * 15 / 127, r = 1 + p->p[7] * 15 / 127;
    const int sy = base - (base - peak) * p->p[6] / 127, hold = w - 1 - a - d - r;
    const int px[5] = {x0, x0 + a, x0 + a + d, x0 + a + d + hold, x0 + w - 1}, py[5] = {base, peak, sy, sy, base};
    int16_t ys[60];
    for (int s = 0; s < 4; s++) {
        const int n = px[s + 1] - px[s];
        for (int k = 0; k <= n; k++) ys[px[s] + k - x0] = (int16_t)(py[s] + (n ? (py[s + 1] - py[s]) * k / n : 0));
    }
    ms_curve(o, x0, w, ys, base, 3);
    for (int s = 1; s < 4; s++) ms_rect(o, px[s] - 1, py[s] - 1, 3, 3, 1);
    if (p->live[1] >= 0) {
        const int stage = p->live[1] >> 5, f = p->live[1] & 31;
        const int lx = px[stage] + (px[stage + 1] - px[stage]) * f / 31, ly = ys[lx - x0];
        ms_rect(o, lx - 2, ly - 2, 5, 5, 0);
        ms_rect(o, lx - 1, ly - 1, 3, 3, 1);
    }
}

/* the LFO: its wave across the stage, as many cycles as the rate, as tall as the depth, travelling as it really runs */
static void ms_lfo(uint8_t *o, const mv_pic_t *p)
{
    const int x0 = 2, w = 124, mid = 36, cyc = 1 + p->p[0] * 7 / 127, amp = 1 + p->p[1] * 15 / 127, fade = p->p[3] * (w - 1) / 127;
    int16_t ys[124];
    ms_title(o, p);
    for (int x = x0; x < x0 + w; x += 4) ms_set(o, x, mid, 1);
    for (int i = 0; i < w; i++) {
        const int ph = i * cyc * 256 / w + p->live[0], q = ph & 255;
        int v;
        if (p->type == 0) v = ms_wave(ph);
        else if (p->type == 1) v = q < 128 ? -128 + q * 2 : 384 - q * 2;
        else if (p->type == 2) v = 128 - q;
        else if (p->type == 3) v = q < 128 ? 128 : -128;
        else v = (int)(ms_hash((uint32_t)(ph >> 8), 1u) & 255u) - 128;
        const int g = fade && i < fade ? i * 256 / fade : 256;
        ys[i] = (int16_t)(mid - v * amp / 128 * g / 256);
    }
    ms_curve(o, x0, w, ys, 0, 0);
    if (fade) { ms_rect(o, x0 + fade - 2, mid - 2, 5, 5, 0); ms_rect(o, x0 + fade - 1, mid - 1, 3, 3, 1); }
}

/* a reverb's tail: as high as its amount, as long as its size, its tone in the dots, by its type's shape */
static void ms_tail(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    const int x0 = 2, w = 124, base = PY1, amp = 8 + p->p[0] * 24 / 127, len = 40 + p->p[2] * 84 / 127, lvl = 1 + p->p[1] * 4 / 127;
    int16_t ys[124];
    ms_title(o, p);
    if (!p->on) { for (int x = x0; x < x0 + w; x += 2) ms_set(o, x, base, 1); return; }
    for (int i = 0; i < w; i++) {
        const int t = i < len ? (len - i) * 256 / len : 0;
        int h = amp * t / 256 * t / 256;
        if (p->type == TL_REVERSE) { const int r = i < len ? i * 256 / len : 0; h = amp * r / 256 * r / 256; }
        if (p->type == TL_FREEZE) h = amp * 3 / 4;
        if (p->type == TL_BLOOM) { const int up = len / 3; h = i < up ? amp * i / up : amp * (len - i > 0 ? len - i : 0) / (len - up); }
        ys[i] = (int16_t)(base - h);
    }
    ms_curve(o, x0, w, ys, base, lvl);
    if (p->type == TL_SHIMMER)
        for (int i = 0; i < 9; i++) {                   /* its octaves: sparks over it */
            const int x = x0 + (int)(ms_hash((uint32_t)i, 11u) % (uint32_t)(len > 8 ? len - 4 : 4));
            ms_set(o, x, ys[x - x0] - 3 - (int)(ms_hash((uint32_t)i, 12u) & 3u) - (v->t + (uint32_t)i * 3u) / 3 % 3, 1);
        }
}

/* a delay: each repeat a bar, as far apart as its time, shrinking by its feedback; Ping-pong to either side of a line,
 * Grains a cloud of short ones */
static void ms_echo(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    const int x0 = 3, gap = 8 + p->p[2] * 20 / 127, keep = 120 + p->p[3] * 130 / 127, ping = p->type == 2, grains = p->type == 4;
    const int mid = ping ? 36 : PY1, top = ping ? 14 : 30;
    int h = 6 + p->p[0] * top / 127;
    ms_title(o, p);
    if (!p->on) { for (int x = x0; x < 126; x += 2) ms_set(o, x, PY1, 1); return; }
    if (grains) {
        const int n = 8 + p->p[2] * 40 / 127, sz = 1 + p->p[0] * 5 / 127;
        for (int i = 0; i < n; i++) {
            const int x = 2 + (int)(ms_hash((uint32_t)i, 21u) % 122u), y = PY0 + 1 + (int)(ms_hash((uint32_t)i, 22u) % 32u);
            ms_rect(o, x, y, sz, 1, 1);
        }
        return;
    }
    if (ping) for (int x = x0; x < 126; x += 2) ms_set(o, x, mid, 1);
    for (int i = 0, x = x0; x < 124 && h > 0; i++, x += gap) {
        const int up = !ping || !(i & 1), lean = p->type == 3 ? (int)(ms_hash((uint32_t)i, 5u) & 1u) : 0;
        const int y = up ? mid - h : mid + 1;
        ms_frame(o, x + lean, y, 5, h + (ping ? 0 : 1));
        h = h * keep / 256;
    }
}

/* a modulation: its wave as deep as its depth, as quick as its rate, travelling; two that drift apart for the chorus's
 * kind, a comb for the sweeps, bars for the tremolo */
static void ms_motion(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    const int x0 = 2, w = 124, mid = 36, amp = 1 + p->p[0] * 15 / 127, cyc = 1 + p->p[2] * 5 / 127, ty = p->type, run = v->t * (2 + cyc);
    int16_t ys[124];
    ms_title(o, p);
    if (!p->on) { for (int x = x0; x < x0 + w; x += 2) ms_set(o, x, mid, 1); return; }
    if (ty == 7) {                                      /* Tremolo */
        for (int i = 0; i < w; i += 4) {
            const int h = (ms_wave(i * cyc * 256 / w - run) + 128) * amp / 256 + 2;
            ms_rect(o, x0 + i, mid - h, 2, 2 * h, 1);
        }
        return;
    }
    if (ty == 5 || ty == 6) {                           /* Phaser, Flanger */
        const int step = ty == 5 ? 6 : 3;
        for (int i = 0; i < w; i += step) {
            const int h = (ms_wave(i * cyc * 256 / w - run) + 128) * amp / 256 + 1;
            ms_rect(o, x0 + i, mid - h, 1, 2 * h + 1, 1);
        }
        return;
    }
    for (int k = 0; k < (ty == 1 || ty == 4 ? 2 : 1); k++) {
        for (int i = 0; i < w; i++) ys[i] = (int16_t)(mid - ms_wave(i * cyc * 256 / w - run + k * (40 + run / 4)) * amp / 128);
        ms_curve(o, x0, w, ys, 0, 0);
    }
}

/* the colour: a wave through it, what it does to the wave */
static void ms_colour(uint8_t *o, const mv_pic_t *p)
{
    const int x0 = 2, w = 124, mid = 36, ty = p->type, a = p->p[0];
    int16_t ys[124];
    ms_title(o, p);
    if (!p->on) { for (int x = x0; x < x0 + w; x += 2) ms_set(o, x, mid, 1); return; }
    for (int i = 0; i < w; i++) {
        int s = ms_wave(i * 2 * 256 / w);               /* -128..128 */
        if (ty == 1 || ty == 5) { const int lim = 128 - a * (ty == 1 ? 90 : 45) / 127; s = s > lim ? lim + (s - lim) / 4 : s < -lim ? -lim + (s + lim) / 4 : s; s = s * 128 / (lim + (128 - lim) / 4); }
        if (ty == 6) { const int lim = 128 - a * 110 / 127, c = s > lim ? lim : s < -lim ? -lim : s; s = c * 128 / lim; }
        if (ty == 2) { const int st = 8 + a * 56 / 127; s = (s + 128) / st * st - 128; }
        if (ty == 3) { const int k = 4 + (127 - a) * 12 / 127; if (i % k == 0 || i % k == 1) s = 0; }
        if (ty == 4) s += (int)(ms_hash((uint32_t)i, 9u) % 64u - 32) * a / 127;
        ys[i] = (int16_t)(mid - s * 15 / 128);
    }
    ms_curve(o, x0, w, ys, 0, 0);
}

static void ms_ring(uint8_t *o, int cx, int cy, int r, int on)
{
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        const int ox[8] = {x, y, -y, -x, -x, -y, y, x}, oy[8] = {y, x, x, y, -y, -x, -x, -y};
        for (int i = 0; i < 8; i++) ms_set(o, cx + ox[i], cy + oy[i], on);
        y++;
        if (e <= 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
    }
}
/* a hub, its spoke turning a sixteenth of a turn at a time */
static void ms_reel(uint8_t *o, int cx, int cy, int r, int turn)
{
    ms_ring(o, cx, cy, r, 1);
    const int a = turn * 16, s = ms_wave(a) * (r - 1) / 128, c = ms_wave(a + 64) * (r - 1) / 128;
    ms_line(o, cx - c, cy - s, cx + c, cy + s);
}

/* the medium itself, small: its reels turn while it plays */
static void ms_media(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    const int turn = v->playing ? v->t : 0, cx = 64, cy = 36;
    ms_title(o, p);
    switch (p->type) {
    case 1:                                             /* Tape: two reels, the tape across */
        ms_reel(o, cx - 22, cy - 2, 13, turn); ms_reel(o, cx + 22, cy - 2, 13, turn);
        ms_line(o, cx - 26, cy + 11, cx - 8, PY1 - 1); ms_line(o, cx + 26, cy + 11, cx + 8, PY1 - 1);
        ms_rect(o, cx - 8, PY1 - 1, 17, 1, 1);
        break;
    case 2:                                             /* Cassette */
        ms_frame(o, cx - 30, PY0 + 1, 60, 32);
        ms_frame(o, cx - 22, PY0 + 6, 44, 14);
        ms_reel(o, cx - 13, PY0 + 13, 4, turn); ms_reel(o, cx + 13, PY0 + 13, 4, turn);
        ms_line(o, cx - 18, PY1 - 1, cx - 14, PY0 + 24); ms_line(o, cx + 18, PY1 - 1, cx + 14, PY0 + 24);
        ms_rect(o, cx - 14, PY0 + 24, 29, 1, 1);
        break;
    case 3:                                             /* VHS */
        ms_frame(o, cx - 36, PY0 + 4, 72, 26);
        ms_frame(o, cx - 26, PY0 + 8, 52, 12);
        ms_reel(o, cx - 16, PY0 + 14, 3, turn); ms_reel(o, cx + 16, PY0 + 14, 3, turn);
        ms_rect(o, cx - 26, PY0 + 24, 52, 1, 1);
        break;
    case 4:                                             /* Vinyl */
        ms_ring(o, cx - 8, cy, 16, 1); ms_ring(o, cx - 8, cy, 11, 1); ms_reel(o, cx - 8, cy, 4, turn);
        ms_line(o, cx + 20, PY0 + 2, cx + 14, cy + 2); ms_line(o, cx + 14, cy + 2, cx + 4, cy + 7);
        ms_rect(o, cx + 18, PY0, 5, 3, 1);
        break;
    case 5:                                             /* Radio */
        ms_frame(o, cx - 30, PY0 + 6, 60, 27);
        ms_line(o, cx + 20, PY0 + 6, cx + 26, PY0);
        for (int y = PY0 + 10; y < PY0 + 30; y += 3) for (int x = cx - 26; x < cx; x += 2) ms_set(o, x, y, 1);
        ms_ring(o, cx + 15, PY0 + 22, 6, 1);
        ms_rect(o, cx + 5, PY0 + 11, 21, 1, 1);
        ms_rect(o, cx + 8 + p->p[2] * 16 / 127, PY0 + 9, 1, 5, 1);
        break;
    case 6:                                             /* Dictaphone */
        ms_frame(o, cx - 13, PY0, 26, 34);
        ms_frame(o, cx - 9, PY0 + 3, 18, 10);
        ms_reel(o, cx - 4, PY0 + 8, 2, turn); ms_reel(o, cx + 4, PY0 + 8, 2, turn);
        for (int y = PY0 + 17; y < PY0 + 30; y += 2) for (int x = cx - 8; x < cx + 9; x += 2) ms_set(o, x, y, 1);
        break;
    default:
        for (int x = 2; x < 126; x += 2) ms_set(o, x, 36, 1);
        break;
    }
}

/* a list: its rows, the cursor's framed, the project open a dot */
static void ms_list(uint8_t *o, const mv_pic_t *p)
{
    const int three = p->kind == PIC_SOUNDS, h = three ? 13 : 10;
    for (int i = 0; i < p->cells; i++) {
        const int y = 11 + i * h, ty = y + (h - 1 - FCAP) / 2 + 0;
        char n[3] = {(char)('0' + p->num[i] / 10), (char)('0' + p->num[i] % 10), 0};
        if (i == p->cur) ms_frame(o, 0, y - 1, MS_W, h);
        ms_text(o, 4, ty, 10, three ? n + 1 : n, 1, 1);
        ms_text(o, 16, ty, 88, p->line[i], 1, 1);
        if (i == p->open) ms_rect(o, 120, ty + 1, 3, 3, 1);
        if (three && p->cell[i].name[0]) ms_text(o, 124 - ms_w(p->cell[i].name, 1), ty, 40, p->cell[i].name, 1, 1);
    }
}

/* the patterns' list: eight cells, the one playing solid, the cursor's rim thick, one waiting for the bar blinking, those
 * with notes framed; under them the notes of the one under the cursor, small */
static void ms_pats(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    const mv_roll_t *w = &v->roll;
    ms_title(o, p);
    for (int i = 0; i < p->cells; i++) {
        const char c[2] = {(char)('1' + i), 0};
        const int x = 2 + i * 124 / 8, now = i == p->now || (i == p->open && (v->t >> 2 & 1)), cur = i == p->cur;
        if (now || cur || p->p[0] >> i & 1) ms_cell(o, x, 19, 14, 11, c, 1, 22, now, cur);
        else ms_text_c(o, x + 7, 22, c, 1, 1);
    }
    for (int i = 0; i < w->count; i++) ms_rect(o, 2 + w->n[i].x, 34 + w->n[i].row * 2, w->n[i].w, 1, 1);
}

/* a knob touched: over the stage, on black */
static void ms_large(uint8_t *o, const mv_knob_t *k, int foot)
{
    ms_rect(o, 0, 9, MS_W, foot - 9, 0);
    ms_text(o, 2, 12, 124, k->name, 1, 1);
    const int z = ms_w(k->val, 3) <= 124 ? 3 : ms_w(k->val, 2) <= 124 ? 2 : 1;
    ms_text(o, 2, 22, 124, k->val, z, 1);
    if (k->n && k->n <= 12) ms_dots(o, 2, 44, 124, k->n, k->idx);
    else ms_meter(o, 2, 44, 124, 7, k->raw);
}

/* a word over the stage, on black */
static void ms_tag(uint8_t *o, int y, const char *s)
{
    const int w = ms_w(s, 1) + 6, x = (MS_W - w) / 2;
    ms_rect(o, x - 1, y - 1, w + 2, FCAP + 8, 0);
    ms_frame(o, x, y, w, FCAP + 6);
    ms_text(o, x + 3, y + 3, w, s, 1, 1);
}

#include "dmk_motion.h"

static void ms_home(uint8_t *o, const mv_view_t *v)
{
    memcpy(o, LANDS[v->land % 13], MS_BYTES);
    ms_motion_land(o, v);
    const char *s = v->ready ? MS_READY : v->note[0] ? v->note : v->sound;
    const int w = ms_w(s, 1) + 6;
    ms_rect(o, 0, 54, w + 1, 10, 0);                    /* the sound's name, small, on a dark tag */
    ms_pill(o, 0, 55, w, FCAP + 4, 0);
    ms_text(o, 3, 57, w, s, 1, 1);
    for (int t = 0; t < 3; t++) {                       /* the synths, as lights */
        const int x = 110 + t * 6;
        ms_rect(o, x - 1, 1, 6, 6, 0);
        if (v->lights >> t & 1) ms_rect(o, x, 2, 4, 4, 1);
        else ms_frame(o, x, 2, 4, 4);
    }
    if (v->focus >= 0 && v->focus < 8 && v->knob[v->focus].on) {
        const mv_knob_t *k = &v->knob[v->focus];
        ms_rect(o, 20, 18, 88, 30, 0);
        ms_frame(o, 20, 18, 88, 30);
        ms_text(o, 25, 21, 78, k->name, 1, 1);
        ms_text(o, 25, 29, 78, k->val, ms_w(k->val, 2) <= 78 ? 2 : 1, 1);
        ms_meter(o, 25, 40, 78, 5, k->raw);
    }
}

static void ms_stage(uint8_t *o, const mv_view_t *v)
{
    const mv_pic_t *p = &v->pic;
    switch (p->kind) {
    case PIC_ROLL: ms_roll(o, v); break;
    case PIC_ARP: ms_arp(o, p); break;
    case PIC_PAN: ms_pan(o, v); break;
    case PIC_SONG: ms_song(o, p); break;
    case PIC_CHORD: ms_chord(o, p); break;
    case PIC_SOUND: ms_sound(o, p); break;
    case PIC_SHAPES: ms_filter(o, p); ms_text(o, 2, 11, 40, p->name, 1, 1); ms_amp(o, p); break;
    case PIC_LFO: ms_lfo(o, p); break;
    case PIC_TAIL: ms_tail(o, v); break;
    case PIC_ECHO: ms_echo(o, v); break;
    case PIC_MOTION: ms_motion(o, v); break;
    case PIC_COLOUR: ms_colour(o, p); break;
    case PIC_MEDIA: ms_media(o, v); break;
    case PIC_PROJECTS: case PIC_SOUNDS: ms_list(o, p); break;
    case PIC_PATS: ms_pats(o, v); break;
    default: break;
    }
}

static void ms_draw(const mv_view_t *v, uint8_t *o)
{
    memset(o, 0, MS_BYTES);
    if (v->mp == 0 && v->mrow == 0 && v->pic.kind != PIC_PATS) { ms_home(o, v); return; }
    const int all = v->pic.kind == PIC_ROLL || v->pic.kind == PIC_PATS;   /* (the stage to the foot: no knob row) */
    ms_header(o, v);
    ms_stage(o, v);
    if (!all) ms_knobs(o, v);
    if (v->focus >= 0 && v->focus < 8 && v->knob[v->focus].on) ms_large(o, &v->knob[v->focus], all ? MS_H : 53);
    else if (v->ready && v->pic.kind != PIC_PATS) ms_tag(o, 25, MS_READY);
    else if (v->note[0] && v->pic.kind != PIC_PATS) ms_tag(o, 25, v->note);
    else if (v->held) ms_tag(o, 25, v->sound);
}
