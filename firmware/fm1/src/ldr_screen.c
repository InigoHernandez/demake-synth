/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's update screen in the update loader (loader.c), as the site's installer shows it (web/installing.js): the
 * word "dem ake" in outline lights pixel by pixel, in reading order through the ordered dither, exactly as far as the
 * loader has written; its light is the core's own (each pixel's depth and each line's ramp, from gen/ldr_art.h, made
 * by host/ldr_art.c from the update screen the app draws), so it is the app's word, lit. Under it a line for the state,
 * a meter of 41 ticks (in ink to where it is, the one at it tall and lit) and how far it is. When no installer comes
 * (a power cut in the middle, a cable pulled) a pass of light runs along the outline and the line says what to do;
 * once lit, the word's bits step through their six arrangements, a band of rows at a time.
 *
 * It calls lcd.c alone and keeps everything in RAM (the flash's XIP is off in the loader). It draws between sectors
 * (ls_write) and on a clock (ls_tick, from ldr_poll), only what changed: a narrow band of the word, a tick or two, the
 * share; about two milliseconds a sector. If the panel does not answer (ls.ok 0) nothing is drawn, so the update never
 * waits on the screen (MONO2K's upd_screen.c, the same rules).
 *
 *   ls_begin(ms)           the frame: the word in outline, "Writing Demake", the meter empty, 0 %
 *   ls_wait()              no installer: "Open the installer", what to do, the pass along the outline
 *   ls_check()             an installer came: "Writing Demake" again
 *   ls_write(done, total)  done of total written (going back: a new pass, the word unlit after it)
 *   ls_done()              written: all lit, "Done", 100 %
 *   ls_stop(flash)         it stopped: "Stopped" and press Install again; or another flash chip, nothing written
 *   ls_tick(ms)            the pass and the bits' steps, called often */
#include "../gen/ldr_art.h"

#define LS_Q 1024                    /* how far: of this */
#define LS_DITH 6                    /* the reveal: the dither's 0..15 times this, about a tenth of the way */
#define LS_SPAN 928                  /* the rest of the way: across the two lines in reading order */
#define LS_LO 4
#define LS_TICKS 41
#define LS_MX0 40                    /* the meter: 41 ticks over 160 px, as the site's */
#define LS_MSTEP 4
#define LS_MY 175                    /* a tick: 12 high; the one at the share 18, and 2 wide */
#define LS_MH 12
#define LS_ATY 172
#define LS_ATH 18
#define LS_AT_C (LA_HOLO0 + 4)       /* the ramp's magenta body */
#define LS_TY0 140                   /* the state's line's band */
#define LS_TY1 166
#define LS_SY0 (LA_GY - 2)           /* the second line's: the share, or what to do */
#define LS_SY1 (LA_GY + LA_GH + 3)
#define LS_SX0 24                    /* (a line of what to do: wide) */
#define LS_SX1 215
#define LS_DX0 92                    /* (the share alone: "100 %" and a margin) */
#define LS_DX1 147
#define LS_DEEP 0x1113u              /* the pass: the ramp's deep blue, #10229A */
#define LS_PASS 7                    /* its half width */
#define LS_PASS_MS 40
#define LS_STEP_MS 45                /* a band of the word's rows re-dithered */
#define LS_BAND 6
#define LS_WAIT_MS 7000u             /* no installer this long: say so */

enum { LS_IDLE, LS_WAIT, LS_WRITE, LS_DONE, LS_STOP };
static struct {
    uint8_t ok, mode, pat, band, tick_at, title, sub;
    int16_t pass, pct;
    uint16_t q;                      /* how far the word is lit, of LS_Q */
    uint32_t t, t0;
    char share[6];
    int16_t share_x;
} ls;
static uint16_t ls_px[240u * 8u];    /* a band of a region, as the panel takes it (the DMA reads it: lcd_sync first) */

static uint16_t ls_be(uint16_t c) { return (uint16_t)(c >> 8 | c << 8); }   /* the panel's byte order */

static int ls_depth(int x, int y)
{
    uint8_t b = LA_DEPTH[(y - LA_WY) * ((LA_WW + 1) / 2) + ((x - LA_WX) >> 1)];
    return (x - LA_WX) & 1 ? b & 15 : b >> 4;
}
static int ls_at(int x, int y)       /* where in the writing a pixel of the word lights, of LS_Q */
{
    return LA_BAYER[0][(y & 3) * 4 + (x & 3)] * LS_DITH + ((y >= LA_SPLIT ? LA_WW : 0) + x - LA_WX) * LS_SPAN / (2 * LA_WW) + LS_LO;
}
static uint16_t ls_word(int x, int y)
{
    int d = ls_depth(x, y), p, t;
    if (!d) return LA_GROUND;
    if (ls_at(x, y) < ls.q) {        /* lit: holo_done's tone, its bits in the arrangement of the moment */
        p = LA_PD[y >= LA_SPLIT][d];
        t = p >> 4;
        if (t < 7 && LA_BAYER[ls.pat][(y & 3) * 4 + (x & 3)] < (p & 15)) t++;
        return LA_PAL[LA_HOLO0 + t];
    }
    if (d > 1) return LA_GROUND;     /* not yet: the outline, its rim alone */
    if (ls.mode == LS_WAIT && x - ls.pass < LS_PASS && ls.pass - x < LS_PASS && LA_BAYER[0][(y & 3) * 4 + (x & 3)] < 10)
        return LS_DEEP;
    return LA_LINE;
}
static int ls_line;                  /* the line ls_text draws (LA_T) */
static uint16_t ls_text(int x, int y)
{
    int i = x - LA_T[ls_line].x, j = y - LA_T[ls_line].y, w = LA_T[ls_line].w;
    uint8_t b;
    if (i < 0 || j < 0 || i >= w || j >= LA_T[ls_line].h) return LA_GROUND;
    b = LA_TDATA[LA_T[ls_line].off + j * ((w + 1) / 2) + (i >> 1)];
    return LA_PAL[i & 1 ? b & 15 : b >> 4];
}
static uint16_t ls_digits(int x, int y)
{
    int gx = ls.share_x;
    const char *c;
    if (y < LA_GY || y >= LA_GY + LA_GH) return LA_GROUND;
    for (c = ls.share; *c; c++) {
        int g = *c == '%' ? 10 : *c == ' ' ? 11 : *c - '0', a = LA_G[g].adv;
        if (x >= gx && x < gx + a) {
            uint8_t b = LA_GDATA[LA_G[g].off + (y - LA_GY) * ((a + 1) / 2) + ((x - gx) >> 1)];
            return LA_PAL[(x - gx) & 1 ? b & 15 : b >> 4];
        }
        gx += a;
    }
    return LA_GROUND;
}
static uint16_t ls_meter(int x, int y)
{
    int k = x - LS_MX0, i = k / LS_MSTEP, at = ls.mode == LS_DONE ? LS_TICKS : ls.tick_at;
    if (k < 0 || i >= LS_TICKS) return LA_GROUND;
    if (i == at) return k % LS_MSTEP < 2 && y >= LS_ATY && y < LS_ATY + LS_ATH ? LA_PAL[LS_AT_C] : LA_GROUND;
    if (k % LS_MSTEP || y < LS_MY || y >= LS_MY + LS_MH) return LA_GROUND;
    return i < at ? LA_INK : LA_LINE;
}

/* a region (inclusive), a band of rows a transfer */
static void ls_region(int x0, int x1, int y0, int y1, uint16_t (*f)(int, int))
{
    int w, rows, y, x, i;
    if (!ls.ok) return;
    if (x0 < 0) x0 = 0;
    if (x1 > 239) x1 = 239;
    if (x0 > x1 || y0 > y1) return;
    w = x1 - x0 + 1;
    rows = (int)(sizeof ls_px / sizeof ls_px[0]) / w;
    for (y = y0; y <= y1; y += rows) {
        int h = y + rows - 1 > y1 ? y1 - y + 1 : rows;
        lcd_sync();
        for (i = 0; i < h; i++) for (x = 0; x < w; x++) ls_px[i * w + x] = ls_be(f(x0 + x, y + i));
        lcd_blit((uint32_t)x0, (uint32_t)y, (uint32_t)w, (uint32_t)h, ls_px);
    }
}
static void ls_word_cols(int x0, int x1, int y0, int y1)
{
    if (x0 < LA_WX) x0 = LA_WX;
    if (x1 > LA_WX + LA_WW - 1) x1 = LA_WX + LA_WW - 1;
    ls_region(x0, x1, y0, y1, ls_word);
}
static void ls_title(int t)
{
    ls.title = (uint8_t)t;
    ls_line = t;
    ls_region(0, 239, LS_TY0, LS_TY1, ls_text);
}
static void ls_sub(int s)            /* the second line: what to do (LA_S_*) */
{
    ls.sub = (uint8_t)s;
    ls.pct = -1;
    ls_line = s;
    ls_region(LS_SX0, LS_SX1, LS_SY0, LS_SY1, ls_text);
}
static void ls_pct(int p)            /* the second line: the share (over a line of what to do: all of it) */
{
    int k = 0, w = 0, wide = ls.pct < 0;
    const char *c;
    if (p == ls.pct) return;
    ls.pct = (int16_t)p;
    ls.sub = 0;
    if (p >= 100) ls.share[k++] = '1';
    if (p >= 10) ls.share[k++] = (char)('0' + p / 10 % 10);
    ls.share[k++] = (char)('0' + p % 10);
    ls.share[k++] = ' ';
    ls.share[k++] = '%';
    ls.share[k] = 0;
    for (c = ls.share; *c; c++) w += LA_G[*c == '%' ? 10 : *c == ' ' ? 11 : *c - '0'].adv;
    ls.share_x = (int16_t)(120 - w / 2);
    if (wide) ls_region(LS_SX0, LS_SX1, LS_SY0, LS_SY1, ls_digits);
    else ls_region(LS_DX0, LS_DX1, LA_GY, LA_GY + LA_GH - 1, ls_digits);
}
static void ls_ticks(int at)         /* the meter to `at`: the ticks between redrawn */
{
    int a = ls.tick_at, x0, x1;
    if (at == a && ls.mode != LS_DONE) return;
    ls.tick_at = (uint8_t)at;
    x0 = LS_MX0 + (a < at ? a : at) * LS_MSTEP;
    x1 = LS_MX0 + (a > at ? a : at) * LS_MSTEP + 1;
    if (ls.mode == LS_DONE) { x0 = LS_MX0; x1 = LS_MX0 + (LS_TICKS - 1) * LS_MSTEP + 1; }
    ls_region(x0, x1, LS_ATY, LS_ATY + LS_ATH - 1, ls_meter);
}
static void ls_light(int q)          /* the word lit to q: on each line, the columns whose pixels changed */
{
    int a = ls.q, lo, hi, r;
    if (q == a) return;
    ls.q = (uint16_t)q;
    lo = (a < q ? a : q) - 15 * LS_DITH - LS_LO;
    hi = a > q ? a : q;
    if (lo < 0) lo = 0;
    for (r = 0; r < 2; r++) {
        int x0 = LA_WX + lo * 2 * LA_WW / LS_SPAN - r * LA_WW - 2, x1 = LA_WX + hi * 2 * LA_WW / LS_SPAN - r * LA_WW + 2;
        if (r) ls_word_cols(x0, x1, LA_SPLIT, LA_WY + LA_WH - 1);
        else ls_word_cols(x0, x1, LA_WY, LA_SPLIT - 1);
    }
}

static void ls_begin(uint32_t ms)
{
    ls.mode = LS_IDLE; ls.q = 0; ls.tick_at = 0; ls.pat = 0; ls.band = 0; ls.pct = -1;
    ls.pass = (int16_t)(LA_WX - LS_PASS);
    ls.t = ls.t0 = ms;
    if (!ls.ok) return;
    lcd_fill(0, 0, 240, 240, LA_GROUND);
    ls_word_cols(LA_WX, LA_WX + LA_WW - 1, LA_WY, LA_WY + LA_WH - 1);
    ls_title(LA_T_WRITE);
    ls_region(LS_MX0, LS_MX0 + (LS_TICKS - 1) * LS_MSTEP + 1, LS_ATY, LS_ATY + LS_ATH - 1, ls_meter);
    ls_pct(0);
}
static void ls_wait(void)
{
    ls.mode = LS_WAIT;
    ls_title(LA_T_WAIT);
    ls_sub(LA_S_WAIT);
}
static void ls_check(void)
{
    int was = ls.mode;
    ls.mode = LS_WRITE;
    if (was == LS_WAIT) ls_word_cols(LA_WX, LA_WX + LA_WW - 1, LA_WY, LA_WY + LA_WH - 1);   /* (the pass away) */
    if (ls.title != LA_T_WRITE) ls_title(LA_T_WRITE);
    if (ls.pct < 0) ls_pct(ls.q * 100 / LS_Q);
}
static void ls_write(uint32_t done, uint32_t total)
{
    int q;
    if (done > total) done = total;
    q = total ? (int)(done * LS_Q / total) : 0;          /* (the app area: under 2^20 bytes, so 32 bits hold it) */
    if (ls.mode != LS_WRITE) ls_check();
    ls_light(q);
    ls_ticks((q * (LS_TICKS - 1) + LS_Q / 2) / LS_Q);
    ls_pct(q >= LS_Q ? 100 : q * 100 / LS_Q);
}
static void ls_done(void)
{
    ls.mode = LS_DONE;
    ls_light(LS_Q);
    ls_ticks(LS_TICKS);
    ls_title(LA_T_DONE);
    ls_pct(100);
}
static void ls_stop(int flash)
{
    ls.mode = LS_STOP;
    ls_title(flash ? LA_T_FLASH : LA_T_STOP);
    ls_sub(flash ? LA_S_FLASH : LA_S_STOP);
}
static void ls_tick(uint32_t ms)
{
    if (!ls.ok) return;
    if (ls.mode == LS_IDLE && ms - ls.t0 > LS_WAIT_MS) ls_wait();
    if (ls.mode == LS_WAIT) {
        int was = ls.pass;
        if (ms - ls.t < LS_PASS_MS) return;
        ls.t = ms;
        ls.pass = (int16_t)(ls.pass >= LA_WX + LA_WW + LS_PASS ? LA_WX - LS_PASS : ls.pass + 3);
        if (ls.pass < was) { ls_word_cols(was - LS_PASS, was + LS_PASS, LA_WY, LA_WY + LA_WH - 1); was = ls.pass; }
        ls_word_cols(was - LS_PASS, ls.pass + LS_PASS, LA_WY, LA_WY + LA_WH - 1);
    } else if (ls.q && ms - ls.t >= LS_STEP_MS) {      /* the lit bits step, a band of rows at a time */
        int y = LA_WY + ls.band * LS_BAND, y1 = y + LS_BAND - 1;
        ls.t = ms;
        if (y1 > LA_WY + LA_WH - 1) y1 = LA_WY + LA_WH - 1;
        ls_word_cols(LA_WX, LA_WX + LA_WW - 1, y, y1);
        if (++ls.band * LS_BAND >= LA_WH) { ls.band = 0; ls.pat = (uint8_t)((ls.pat + 1) % 6); }
    }
}
