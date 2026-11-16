/* SPDX-License-Identifier: GPL-3.0-only */
/* The update loader's screen (firmware/fm1/src/ldr_screen.c) on a computer, through stand-ins for lcd.c that draw into
 * a 240x240 picture: every state as a PNG, and the checks. The word fully lit is the core's own update screen, pixel
 * for pixel, in all six dithers (the loader's depths and ramp against dmk_gfx.c); it lights in reading order and only
 * as far as written; the meter and the share follow; and what a sector costs the panel.
 *   cc -std=c99 -O2 -Ifirmware/src -Ifirmware/gen -o build/host/ldr_screen_test host/ldr_screen_test.c
 *   build/host/ldr_screen_test [DIR]      (DIR: the PNGs) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DMK_VER "0.0"
#include "dmk_core.c"

/* ---- lcd.c's stand-ins: the panel as a picture (RGB565, the panel's byte order undone), the bytes it was sent */
static uint16_t P[240 * 240];
static uint32_t sent;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = y; j < y + h && j < 240; j++) for (uint32_t i = x; i < x + w && i < 240; i++) P[j * 240 + i] = c;
    sent += w * h * 2;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) P[(y + j) * 240 + x + i] = (uint16_t)(px[j * w + i] >> 8 | px[j * w + i] << 8);
    sent += w * h * 2 + 11;                             /* (and the window's commands) */
}
#include "../firmware/fm1/src/ldr_screen.c"

static int fails, checks;
static void check(int ok, const char *what) { checks++; printf("  %-5s %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; }

/* ---- PNG, stored deflate blocks (as dmk_host.c's) */
static uint32_t crc_tab[256];
static uint32_t crc(const uint8_t *p, size_t n, uint32_t c)
{
    if (!crc_tab[1]) for (uint32_t k = 0; k < 256; k++) { uint32_t v = k; for (int j = 0; j < 8; j++) v = v & 1 ? 0xEDB88320u ^ v >> 1 : v >> 1; crc_tab[k] = v; }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ c >> 8;
    return ~c;
}
static void be(uint8_t *o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *t, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    be(h, n); memcpy(h + 4, t, 4);
    fwrite(h, 1, 8, f); if (n) fwrite(d, 1, n, f);
    uint32_t c = crc((const uint8_t *)t, 4, 0); c = crc(d, n, c);
    be(h, c); fwrite(h, 1, 4, f);
}
static void png(const char *dir, const char *name)       /* at 2x, as the tour's */
{
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    enum { W = 480, ROW = 1 + W * 3 };
    static uint8_t raw[ROW * W], z[ROW * W + ROW * W / 65535 * 5 + 64];
    for (int y = 0; y < W; y++) {
        raw[y * ROW] = 0;
        for (int x = 0; x < W; x++) {
            uint16_t c = P[(y / 2) * 240 + x / 2];
            uint8_t *o = &raw[y * ROW + 1 + x * 3];
            o[0] = (uint8_t)((c >> 11) * 255 / 31); o[1] = (uint8_t)((c >> 5 & 63) * 255 / 63); o[2] = (uint8_t)((c & 31) * 255 / 31);
        }
    }
    size_t n = 0, left = sizeof raw, at = 0;
    uint32_t a = 1, b = 0;
    z[n++] = 0x78; z[n++] = 0x01;
    while (left) {
        uint16_t k = (uint16_t)(left > 65535 ? 65535 : left);
        z[n++] = left == k; z[n++] = (uint8_t)k; z[n++] = (uint8_t)(k >> 8); z[n++] = (uint8_t)~k; z[n++] = (uint8_t)(~k >> 8);
        memcpy(z + n, raw + at, k); n += k; at += k; left -= k;
    }
    for (size_t i = 0; i < sizeof raw; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be(z + n, b << 16 | a); n += 4;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    uint8_t ih[13];
    be(ih, W); be(ih + 4, W); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    fwrite(sig, 1, 8, f); chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)n); chunk(f, "IEND", 0, 0);
    fclose(f);
}

static int fbpx(const uint8_t *fr, int x, int y) { uint8_t b = fr[(y * DMK_W + x) >> 1]; return x & 1 ? b & 15 : b >> 4; }
static int lit_px(int x, int y) { uint16_t c = P[y * 240 + x]; for (int t = 0; t < 8; t++) if (c == LA_PAL[LA_HOLO0 + t]) return 1; return 0; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    const uint32_t total = 0x8F000u;                    /* the app area, as ldr_core.c counts it */
    dmk_init(7);
    theme = 1;
    ls.ok = 1;
    ls_begin(0);
    png(dir, "ldr_0_begin");
    int outline = 1, dark = 1;
    for (int y = LA_WY; y < LA_WY + LA_WH; y++) for (int x = LA_WX; x < LA_WX + LA_WW; x++) {
        int d = ls_depth(x, y);
        outline &= (P[y * 240 + x] == LA_LINE) == (d == 1);
        dark &= !lit_px(x, y);
    }
    check(outline && dark && P[0] == LA_GROUND, "the start: the word's rim alone, in the line's grey, on the night ground");
    /* no installer: the line says so, a pass along the outline */
    ls_tick(LS_WAIT_MS + 1);
    for (uint32_t t = LS_WAIT_MS + 1; t < LS_WAIT_MS + 1 + 30 * LS_PASS_MS; t += LS_PASS_MS) ls_tick(t);
    png(dir, "ldr_1_wait");
    int deep = 0;
    for (int i = 0; i < 240 * 240; i++) deep += P[i] == LS_DEEP;
    check(ls.mode == LS_WAIT && ls.title == LA_T_WAIT && deep > 0, "no installer for 7 s: Open the installer, a pass of deep blue on the outline");
    /* an installer: writing, in reading order */
    ls_check();
    deep = 0;
    for (int i = 0; i < 240 * 240; i++) deep += P[i] == LS_DEEP;
    check(ls.title == LA_T_WRITE && deep == 0, "an installer: Writing Demake, the pass gone");
    uint32_t most = 0, all = 0, n = 0;
    for (uint32_t d = 0x1000; d <= total * 37 / 100; d += 0x1000) {
        uint32_t s0 = sent;
        ls_write(d, total);
        if (sent - s0 > most) most = sent - s0;
        all += sent - s0; n++;
    }
    png(dir, "ldr_2_write37");
    {
        int line1 = 0, line2 = 0, order = 1;
        for (int y = LA_WY; y < LA_WY + LA_WH; y++) for (int x = LA_WX; x < LA_WX + LA_WW; x++) {
            if (!lit_px(x, y)) continue;
            if (y < LA_SPLIT) line1++; else line2++;
            order &= ls_at(x, y) < ls.q;
        }
        check(line1 > 0 && line2 == 0 && order, "37 %: the first line lighting from the left, the second not yet, nothing past the share");
        check(ls.pct == 36 || ls.pct == 37, "the share says it");
        check(P[LS_ATY * 240 + LS_MX0 + ls.tick_at * LS_MSTEP] == LA_PAL[LS_AT_C] && P[LS_MY * 240 + LS_MX0] == LA_INK
              && P[LS_MY * 240 + LS_MX0 + (LS_TICKS - 1) * LS_MSTEP] == LA_LINE, "the meter: ink to it, the tick at it lit, the rest grey");
    }
    printf("  a sector: %u bytes to the panel at most, %u on average (~%.1f ms at 12 MHz)\n", most, all / n, (all / n) * 8.0 / 12e6 * 1e3);
    check(most < 12000, "a sector redraws a few kilobytes at most (under 8 ms at 12 MHz)");
    /* stopped, then resumed: a new pass from the start unlights after it */
    ls_stop(0);
    png(dir, "ldr_3_stopped");
    check(ls.title == LA_T_STOP && ls.sub == LA_S_STOP, "it stopped: Stopped, and what to do");
    ls_check();
    ls_write(0x1000, total);
    int after = 0;
    for (int y = LA_WY; y < LA_WY + LA_WH; y++) for (int x = LA_WX; x < LA_WX + LA_WW; x++) after += lit_px(x, y) && ls_at(x, y) >= ls.q;
    check(after == 0 && ls.pct <= 1, "a new pass: the word unlit after where it is");
    for (uint32_t d = 0x1000; d <= total; d += 0x1000) ls_write(d, total);
    ls_done();
    for (uint32_t t = 0; t < 40; t++) ls_tick(100000 + t * LS_STEP_MS);
    png(dir, "ldr_4_done");
    /* lit, it is the core's word: each dither, every pixel of the word */
    {
        int same = 1;
        for (int k = 0; k < 6; k++) {
            const uint8_t *fr;
            int pk;
            fr = dmk_update_frame(5, 0);
            pk = holo_pat;
            ls.pat = (uint8_t)pk;
            ls_word_cols(LA_WX, LA_WX + LA_WW - 1, LA_WY, LA_WY + LA_WH - 1);
            for (int y = LA_WY; y < LA_WY + LA_WH; y++) for (int x = LA_WX; x < LA_WX + LA_WW; x++) {
                int c = fbpx(fr, x, y);
                same &= P[y * 240 + x] == (c >= C_HOLO0 && c <= C_HOLO7 ? LA_PAL[c] : LA_GROUND);
            }
        }
        check(same, "done: the word is the app's own update screen's, pixel for pixel, in all six dithers");
        check(ls.title == LA_T_DONE && ls.pct == 100 && P[LS_MY * 240 + LS_MX0 + (LS_TICKS - 1) * LS_MSTEP] == LA_INK, "Done, 100 %, the meter full");
    }
    ls_stop(1);
    png(dir, "ldr_5_flash");
    check(ls.title == LA_T_FLASH && ls.sub == LA_S_FLASH, "another flash chip: said, and that the firmware stays");
    /* a silent panel: nothing drawn */
    memset(P, 0, sizeof P);
    ls.ok = 0;
    ls_begin(0); ls_write(total / 2, total); ls_done();
    int none = 1;
    for (int i = 0; i < 240 * 240; i++) none &= P[i] == 0;
    check(none, "a panel that does not answer: nothing drawn");
    printf("  %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
