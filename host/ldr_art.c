/* SPDX-License-Identifier: GPL-3.0-only */
/* The update loader's pictures (firmware/fm1/gen/ldr_art.h), drawn by the core itself, so the FM-1's screen while the
 * loader writes is the app's own: the word as the update screen lights it (each pixel's depth, each line's ramp, the
 * six dithers: the loader turns them into the thermal light as dmk_gfx.c's holo_done does), the lines for each state
 * in Geist, the digits for the share, all in the night palette (the update screen's ground is dark in both themes).
 * It checks that the depths and the dither give back the core's own frame, pixel for pixel, in all six arrangements.
 *   cc -std=c99 -O2 -Ifirmware/src -Ifirmware/gen -o build/host/ldr_art host/ldr_art.c && build/host/ldr_art OUT.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DMK_VER "0.0"
#include "dmk_core.c"

#define NIGHT 1                                         /* THEME_DARK: the night theme's palette */
#define WORD_ROWS 136                                   /* the word lies above this row on the update screen */
#define TITLE_Y 142                                     /* the update screen's line, its top (dmk_ui.c ui_update) */
#define SUB_Y 198                                       /* the loader's second line: the share, or what to do */

static uint8_t F[6][DMK_W * DMK_H];                     /* the update screen's word, in each of the six dithers */
static int fails;
static void check(int ok, const char *what) { if (!ok) { fprintf(stderr, "ldr_art: %s\n", what); fails++; } }

static int px(const uint8_t *fr, int x, int y) { uint8_t b = fr[(y * DMK_W + x) >> 1]; return x & 1 ? b & 15 : b >> 4; }

/* a line's text alone on the ground, its pixels as the core draws them: the box round them */
typedef struct { int x, y, w, h; uint8_t p[DMK_W * 32]; } strip_t;
static void strip(strip_t *s, const char *t, const dmk_font_t *f, int c, int y)
{
    int x0 = DMK_W, x1 = -1, y0 = DMK_H, y1 = -1;
    clip_all();
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    text_c(DMK_W / 2, y, t, f, c);
    for (int Y = 0; Y < DMK_H; Y++)
        for (int X = 0; X < DMK_W; X++)
            if (px(fb, X, Y) != C_PAPER) { if (X < x0) x0 = X; if (X > x1) x1 = X; if (Y < y0) y0 = Y; if (Y > y1) y1 = Y; }
    check(x1 >= 0 && (y1 - y0 + 1) * (x1 - x0 + 1) <= (int)sizeof s->p, t);
    s->x = x0; s->y = y0; s->w = x1 - x0 + 1; s->h = y1 - y0 + 1;
    for (int Y = 0; Y < s->h; Y++) for (int X = 0; X < s->w; X++) s->p[Y * s->w + X] = (uint8_t)px(fb, x0 + X, y0 + Y);
}

static void hex4(FILE *o, const uint8_t *p, int w, int h, int *n)   /* rows of 4-bit pixels, two a byte, each row whole */
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x += 2) {
            fprintf(o, "%s0x%02X,", (*n)++ % 16 ? "" : "\n    ", p[y * w + x] << 4 | (x + 1 < w ? p[y * w + x + 1] : 0));
        }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    dmk_init(7);
    theme = NIGHT;
    int pat[6];
    for (int k = 0; k < 6; k++) {
        const uint8_t *fr = dmk_update_frame(5, 0);
        pat[k] = holo_pat;
        memcpy(F[k], fr, DMK_W * DMK_H / 2);
    }
    /* the word: its two lines' marks (the thermal tones), each line its own shape (text_c lights a line at a time) */
    int gy0[2] = {DMK_H, DMK_H}, gy1[2] = {-1, -1}, gx0[2] = {DMK_W, DMK_W}, gx1[2] = {-1, -1}, g = -1, was = 0;
    for (int y = 0; y < WORD_ROWS; y++) {
        int any = 0;
        for (int x = 0; x < DMK_W; x++) {
            int c = px(F[0], x, y);
            if (c >= C_HOLO0 && c <= C_HOLO7) {
                if (!any && !was) g++;
                any = 1;
                if (g < 2) { if (x < gx0[g]) gx0[g] = x; if (x > gx1[g]) gx1[g] = x; if (y < gy0[g]) gy0[g] = y; gy1[g] = y; }
            }
        }
        was = any;
    }
    check(g == 1, "the word is not two lines");
    static uint8_t dep[DMK_W * DMK_H];
    uint8_t pd[2][TH_DEEP + 1];
    memset(dep, 0, sizeof dep);
    for (g = 0; g < 2; g++) {                           /* holo_done's depths: two chamfer passes over the line's box */
        int w = gx1[g] - gx0[g] + 1, h = gy1[g] - gy0[g] + 1, dmax = 1;
        static uint8_t d[DMK_W * DMK_H];
        for (int y = 0, i = 0; y < h; y++) for (int x = 0; x < w; x++, i++) {
            int c = px(F[0], gx0[g] + x, gy0[g] + y);
            d[i] = c >= C_HOLO0 && c <= C_HOLO7 ? TH_DEEP : 0;
        }
        for (int y = 0, i = 0; y < h; y++)
            for (int x = 0; x < w; x++, i++) {
                int m;
                if (!d[i]) continue;
                m = x ? d[i - 1] : 0;
                if (y) {
                    int u = d[i - w], ul = x ? d[i - w - 1] : 0, ur = x < w - 1 ? d[i - w + 1] : 0;
                    if (u < m) m = u;
                    if (ul < m) m = ul;
                    if (ur < m) m = ur;
                } else m = 0;
                if (m + 1 < d[i]) d[i] = (uint8_t)(m + 1);
            }
        for (int y = h - 1, i = w * h - 1; y >= 0; y--)
            for (int x = w - 1; x >= 0; x--, i--) {
                int m;
                if (!d[i]) continue;
                m = x < w - 1 ? d[i + 1] : 0;
                if (y < h - 1) {
                    int dd = d[i + w], dl = x ? d[i + w - 1] : 0, dr = x < w - 1 ? d[i + w + 1] : 0;
                    if (dd < m) m = dd;
                    if (dl < m) m = dl;
                    if (dr < m) m = dr;
                } else m = 0;
                if (m + 1 < d[i]) d[i] = (uint8_t)(m + 1);
                if (d[i] > dmax) dmax = d[i];
            }
        int flat = dmax <= 2, pmax = TH_PMAX[flat ? 1 : dmax > 9 ? 9 : dmax];
        check(dmax <= 15, "a depth past 15 (4 bits)");
        for (int k = 0; k <= TH_DEEP; k++) {
            int p = k == 0 ? 0 : dmax > 1 && !flat ? (k - 1) * pmax / (dmax - 1) : pmax;
            pd[g][k] = (uint8_t)(p > 255 ? 255 : p);
        }
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) dep[(gy0[g] + y) * DMK_W + gx0[g] + x] = d[y * w + x];
    }
    int wx = gx0[0] < gx0[1] ? gx0[0] : gx0[1], wx1 = gx1[0] > gx1[1] ? gx1[0] : gx1[1];
    int wy = gy0[0], wh = gy1[1] - gy0[0] + 1, ww = wx1 - wx + 1, split = gy0[1];
    /* the depths and the dither give back the core's frame: every pixel of the word, in every arrangement */
    int same = 1;
    for (int k = 0; k < 6; k++)
        for (int y = wy; y < wy + wh; y++)
            for (int x = wx; x < wx + ww; x++) {
                int d = dep[y * DMK_W + x], c = px(F[k], x, y), want;
                if (!d) { same &= !(c >= C_HOLO0 && c <= C_HOLO7); continue; }
                int p = pd[y >= split][d], t = p >> 4;
                if (t < 7 && T_BAYER6[pat[k]][(y & 3) * 4 + (x & 3)] < (p & 15)) t++;
                want = C_HOLO0 + t;
                same &= c == want;
            }
    check(same, "the depths and dither do not give back the core's word");
    /* the lines: each state's (Geist, the update screen's sizes), and the second line's */
    static const char *const TITLE[] = {"Writing Demake", "Open the installer", "Stopped", "Done", "Another flash chip"};
    static const char *const SUB[] = {"Press Install to finish", "Replug, press Install again", "Your firmware stays as it was"};
    enum { NT = sizeof TITLE / sizeof TITLE[0], NS = sizeof SUB / sizeof SUB[0] };
    static strip_t st[NT + NS];
    for (int i = 0; i < NT; i++) strip(&st[i], TITLE[i], &FM, C_INK, TITLE_Y);
    for (int i = 0; i < NS; i++) strip(&st[NT + i], SUB[i], &FS, C_LABEL, SUB_Y);
    /* the digits for the share, the label's grey: each from the line's top, its advance wide */
    static const char GL[] = "0123456789% ";
    static uint8_t gp[12][16 * 24];
    int gadv[12], gh = FS.ascent + FS.descent;
    for (int i = 0; i < 12; i++) {
        char t[2] = {GL[i], 0};
        clip_all();
        fill(0, 0, DMK_W, DMK_H, C_PAPER);
        gadv[i] = text(0, SUB_Y, t, &FS, C_LABEL);
        check(gadv[i] > 0 && gadv[i] <= 16 && gh <= 24, "a digit too wide");
        for (int y = 0; y < gh; y++) for (int x = 0; x < gadv[i]; x++) gp[i][y * gadv[i] + x] = (uint8_t)px(fb, x, SUB_Y + y);
    }
    if (fails) return 1;

    FILE *o = fopen(argv[1], "w");
    if (!o) return 1;
    int n;
    fprintf(o, "/* SPDX-License-Identifier: GPL-3.0-only */\n/* generated by host/ldr_art.c from the core (firmware/src): do not edit. The update screen's word, lines and\n"
               " * digits for the update loader (src/ldr_screen.c), in the night palette */\n#pragma once\n#include <stdint.h>\n\n");
    fprintf(o, "#define LA_WX %d\n#define LA_WY %d\n#define LA_WW %d\n#define LA_WH %d\n#define LA_SPLIT %d   /* the word's second line from here */\n",
            wx, wy, ww, wh, split);
    fprintf(o, "static const uint16_t LA_PAL[16] = {");
    for (int i = 0; i < 16; i++) fprintf(o, "%s0x%04X", i ? "," : "", T_PAL565[NIGHT][i]);
    fprintf(o, "};\n#define LA_GROUND 0x%04X\n#define LA_LINE 0x%04X\n#define LA_INK 0x%04X\n#define LA_LABEL 0x%04X\n",
            T_PAL565[NIGHT][C_PAPER], T_PAL565[NIGHT][C_LINE], T_PAL565[NIGHT][C_INK], T_PAL565[NIGHT][C_LABEL]);
    fprintf(o, "#define LA_HOLO0 %d\n", C_HOLO0);
    fprintf(o, "static const uint8_t LA_BAYER[6][16] = {");
    for (int k = 0; k < 6; k++) { fprintf(o, "%s{", k ? ", " : ""); for (int i = 0; i < 16; i++) fprintf(o, "%s%d", i ? "," : "", T_BAYER6[k][i]); fprintf(o, "}"); }
    fprintf(o, "};\n/* each line's ramp by depth (sixteenths of a tone), and each pixel's depth (0: not the word), two a byte */\n");
    fprintf(o, "static const uint8_t LA_PD[2][16] = {");
    for (g = 0; g < 2; g++) { fprintf(o, "%s{", g ? ", " : ""); for (int k = 0; k < 16; k++) fprintf(o, "%s%d", k ? "," : "", pd[g][k]); fprintf(o, "}"); }
    fprintf(o, "};\nstatic const uint8_t LA_DEPTH[%d] = {", (ww + 1) / 2 * wh);
    {
        static uint8_t d4[DMK_W * DMK_H];
        for (int y = 0; y < wh; y++) for (int x = 0; x < ww; x++) d4[y * ww + x] = dep[(wy + y) * DMK_W + wx + x];
        n = 0; hex4(o, d4, ww, wh, &n);
    }
    fprintf(o, "\n};\n/* the lines (their pixels as palette entries, two a byte, each row whole) */\n");
    fprintf(o, "enum { LA_T_WRITE, LA_T_WAIT, LA_T_STOP, LA_T_DONE, LA_T_FLASH, LA_S_WAIT, LA_S_STOP, LA_S_FLASH, LA_T_N };\n");
    fprintf(o, "static const struct { uint8_t x, y, w, h; uint16_t off; } LA_T[LA_T_N] = {");
    {
        int off = 0;
        for (int i = 0; i < NT + NS; i++) { fprintf(o, "%s{%d, %d, %d, %d, %d}", i ? ", " : "", st[i].x, st[i].y, st[i].w, st[i].h, off); off += (st[i].w + 1) / 2 * st[i].h; }
        fprintf(o, "};\nstatic const uint8_t LA_TDATA[%d] = {", off);
    }
    n = 0;
    for (int i = 0; i < NT + NS; i++) hex4(o, st[i].p, st[i].w, st[i].h, &n);
    fprintf(o, "\n};\n/* the share's glyphs: 0-9, %%, a space; the line's height, each its advance wide */\n#define LA_GY %d\n#define LA_GH %d\n", SUB_Y, gh);
    fprintf(o, "static const struct { uint8_t adv; uint16_t off; } LA_G[12] = {");
    {
        int off = 0;
        for (int i = 0; i < 12; i++) { fprintf(o, "%s{%d, %d}", i ? ", " : "", gadv[i], off); off += (gadv[i] + 1) / 2 * gh; }
        fprintf(o, "};\nstatic const uint8_t LA_GDATA[%d] = {", off);
    }
    n = 0;
    for (int i = 0; i < 12; i++) hex4(o, gp[i], gadv[i], gh, &n);
    fprintf(o, "\n};\n");
    fclose(o);
    printf("ldr_art: the word %dx%d at %d,%d (lines split at %d), depths %d B, lines %d, glyphs %d high: checked in six dithers\n",
           ww, wh, wx, wy, split, (ww + 1) / 2 * wh, NT + NS, gh);
    return 0;
}
