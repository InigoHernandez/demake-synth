/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on a computer: the same core, for tests, screenshots and renders.
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -Ifirmware/gen -o build/host/dmk_host host/dmk_host.c
 *   build/host/dmk_host DIR            the tour: every screen as DIR/NN_name.png, the text audit over each
 *   build/host/dmk_host --selftest     behaviour checks; exit 1 on a failure
 *   build/host/dmk_host --wav FILE     a chord held and let go, 3 s
 *   build/host/dmk_host --bench        instructions per sample of the bench build's levels (build with -DDMK_BENCH) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef DMK_TEXT_AUDIT
#define DMK_TEXT_AUDIT 1
#endif
#include "dmk_core.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

static uint32_t now_ms;
static int fails;

/* ---- PNG, uncompressed (stored deflate blocks): enough for screenshots, no library */
static uint32_t crc_tab[256];
static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t c)
{
    size_t i;
    if (!crc_tab[1]) for (uint32_t k = 0; k < 256; k++) { uint32_t v = k; for (int j = 0; j < 8; j++) v = v & 1 ? 0xEDB88320u ^ v >> 1 : v >> 1; crc_tab[k] = v; }
    c = ~c;
    for (i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ c >> 8;
    return ~c;
}
static void be32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    be32(h, n); memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32_of((const uint8_t *)type, 4, 0);
    c = crc32_of(d, n, c);
    be32(h, c); fwrite(h, 1, 4, f);
}
static void png_rgb(const char *path, const uint8_t *rgb, int w, int h, int scale)
{
    int W = w * scale, H = h * scale;
    size_t raw_n = (size_t)H * (W * 3 + 1), z_n, i;
    uint8_t *raw = malloc(raw_n), *z, ihdr[13];
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    for (int y = 0; y < H; y++) {
        raw[(size_t)y * (W * 3 + 1)] = 0;
        for (int x = 0; x < W; x++) memcpy(raw + (size_t)y * (W * 3 + 1) + 1 + x * 3, rgb + ((size_t)(y / scale) * w + x / scale) * 3, 3);
    }
    z_n = 2 + raw_n + (raw_n / 65535 + 1) * 5 + 4;
    z = malloc(z_n);
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    for (i = 0; i < raw_n; i += 65535) {
        size_t n = raw_n - i < 65535 ? raw_n - i : 65535;
        z[o++] = i + n >= raw_n; z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8); z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + i, n); o += n;
    }
    uint32_t a = 1, b = 0;
    for (i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    be32(z + o, b << 16 | a); o += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

static void screen_rgb(const uint8_t *frame, uint8_t *rgb)
{
    const uint32_t *pal = dmk_palette_rgb();
    for (int i = 0; i < DMK_W * DMK_H; i++) {
        int c = i & 1 ? frame[i >> 1] & 15 : frame[i >> 1] >> 4;
        rgb[i * 3] = (uint8_t)(pal[c] >> 16); rgb[i * 3 + 1] = (uint8_t)(pal[c] >> 8); rgb[i * 3 + 2] = (uint8_t)pal[c];
    }
}

/* ---- time and controls, as a player would use them */
static void wait_ms(uint32_t ms)
{
    int16_t b[128 * 2];
    uint32_t end = now_ms + ms;
    while (now_ms < end) { dmk_render(b, 128); now_ms += 3; dmk_frame(now_ms); }
}
static void tap(int b) { dmk_button(b, 1); wait_ms(60); dmk_button(b, 0); wait_ms(60); }

static int shots;
static void shot(const char *dir, const char *name)
{
    char path[512];
    static uint8_t rgb[DMK_W * DMK_H * 3];
    const uint8_t *fr = dmk_frame(now_ms);
    int bad = ta_check();
    if (bad) {
        fails++;
        printf("text audit: %s: %d problem(s)\n", name, bad);
        for (int i = 0; i < bad && i < 4; i++) printf("    %s\n", ta_why[i]);
    }
    if (!dir) return;
    screen_rgb(fr, rgb);
    snprintf(path, sizeof path, "%s/%02d_%s.png", dir, shots++, name);
    png_rgb(path, rgb, DMK_W, DMK_H, 2);
}

static void tour(const char *dir)
{
    dmk_init(1);
    now_ms = 0;
    wait_ms(700); shot(dir, "boot");
    wait_ms(1000); shot(dir, "home");
    static const struct { int b; const char *n; } P[] = {{B_SEQ, "roll"}, {B_EDIT, "sound"}, {B_SEL, "filter"},
        {B_ENV, "amp"}, {B_FX, "fx"}, {B_ARP, "chord"}, {B_GLO, "global"}, {B_LFO, "lfo"}};
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) { tap(P[i].b); shot(dir, P[i].n); }
    tap(B_FX); tap(B_FX); shot(dir, "fx_media");
    tap(B_GLO); tap(B_GLO); shot(dir, "global_song"); tap(B_GLO); shot(dir, "global_setup"); tap(B_GLO); tap(B_GLO);
    tap(B_EDIT); tap(B_EDIT); shot(dir, "sound_layer");
    tap(B_SEL); for (int i = 0; i < 30; i++) dmk_encoder(E_K1, -1); for (int i = 0; i < 40; i++) dmk_encoder(E_K2, 1);
    shot(dir, "filter_turned");
    tap(B_HOME); dmk_encoder(E_SELECT, 1); shot(dir, "home_synth2");
    dmk_button(B_HOME, 1); wait_ms(500); shot(dir, "home_held"); dmk_button(B_HOME, 0); wait_ms(60);
    tap(B_OCTU); wait_ms(300); shot(dir, "notice");
    dmk_button(B_SAVE, 1); wait_ms(1200); dmk_button(B_SAVE, 0); wait_ms(60); shot(dir, "discs");
    tap(B_GLO); tap(B_GLO); tap(B_GLO); tap(B_GLO);     /* to SETUP */
    for (int t = 1; t < DMK_THEMES; t++) {
        dmk_encoder(E_K1, 1);
        char n[24], lc[12]; int i;
        for (i = 0; i < 11 && THEME_NAME[theme][i]; i++) lc[i] = (char)(THEME_NAME[theme][i] | 0x20);   /* a file's name: lowercase */
        lc[i] = 0;
        tap(B_HOME); snprintf(n, sizeof n, "home_%s", lc); shot(dir, n);
        tap(B_SEQ); snprintf(n, sizeof n, "roll_%s", lc); shot(dir, n);
        tap(B_GLO);
    }
    printf("tour: %d screens%s%s, text audit %s\n", shots, dir ? " in " : "", dir ? dir : "", ta_bad ? "FAILED" : "clean");
}

static void selftest(void)
{
    int16_t b[512 * 2];
    int64_t e;
    dmk_init(7);
    now_ms = 0;
    dmk_render(b, 512);
    e = 0; for (int i = 0; i < 1024; i++) e += b[i] < 0 ? -b[i] : b[i];
    if (e) { fails++; printf("selftest: sound before any key\n"); }
    dmk_key(0, 1);
    for (int k = 0; k < 20; k++) dmk_render(b, 512);
    e = 0; for (int i = 0; i < 1024; i++) e += b[i] < 0 ? -b[i] : b[i];
    if (e < 1000 * 512) { fails++; printf("selftest: a held key is too quiet (%lld)\n", (long long)e); }
    dmk_key(0, 0);
    for (int k = 0; k < 400; k++) dmk_render(b, 512);
    e = 0; for (int i = 0; i < 1024; i++) e += b[i] < 0 ? -b[i] : b[i];
    if (e) { fails++; printf("selftest: a released key still sounds (%lld)\n", (long long)e); }
    for (int k = 0; k < DMK_KEYS; k++) dmk_key(k, 1);       /* every key at once: 12 voices, no clip past full scale */
    for (int k = 0; k < 20; k++) dmk_render(b, 512);
    for (int k = 0; k < DMK_KEYS; k++) dmk_key(k, 0);
    /* a tap on a page button opens it, a second moves its row */
    wait_ms(1600);
    tap(B_FX); if (ui.page != PG_FX || ui.row[PG_FX]) { fails++; printf("selftest: FX does not open FX\n"); }
    tap(B_FX); if (ui.row[PG_FX] != 1) { fails++; printf("selftest: FX again is not the next row\n"); }
    /* a modifier that was used does nothing on release */
    dmk_button(B_GLO, 1); wait_ms(30); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_GLO, 0); wait_ms(30);
    if (ui.page != PG_FX) { fails++; printf("selftest: a used GLO opened its page on release\n"); }
    tour(NULL);
    printf("selftest: %s\n", fails ? "FAILED" : "ok");
}

static void wav(const char *path)
{
    int n = DMK_SR * 3, i;
    int16_t *b = malloc((size_t)n * 4);
    FILE *f = fopen(path, "wb");
    dmk_init(3);
    dmk_key(4, 1); dmk_key(8, 1); dmk_key(11, 1);
    for (i = 0; i < n; i += 128) { if (i == DMK_SR * 2) { dmk_key(4, 0); dmk_key(8, 0); dmk_key(11, 0); } dmk_render(b + i * 2, 128); }
    uint8_t h[45] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0....\0\0\0\0\x04\0\x10\0data....";
    uint32_t v = 36 + (uint32_t)n * 4; memcpy(h + 4, &v, 4);
    v = DMK_SR; memcpy(h + 24, &v, 4); v = DMK_SR * 4; memcpy(h + 28, &v, 4);
    v = (uint32_t)n * 4; memcpy(h + 40, &v, 4);
    fwrite(h, 1, 44, f); fwrite(b, 4, (size_t)n, f); fclose(f); free(b);
    printf("wav: %s\n", path);
}

#ifdef __APPLE__
static uint64_t instructions(void)
{
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    return ri.ri_instructions;
}
#endif

static void bench(void)
{
#if defined(DMK_BENCH) && defined(__APPLE__)
    static int16_t b[128 * 2];
    int levels[] = {0, 100, 200, 400, 600, 800, 1000};
    dmk_init(5);                                        /* no key held: the bench alone over the idle render */
    for (unsigned l = 0; l < sizeof levels / sizeof levels[0]; l++) {
        bench_level = levels[l];
        for (int i = 0; i < 100; i++) dmk_render(b, 128);
        uint64_t a = instructions();
        int frames = DMK_SR * 10;
        for (int i = 0; i < frames; i += 128) dmk_render(b, 128);
        uint64_t z = instructions();
        printf("bench level %4d: %7.1f instructions a sample\n", levels[l], (double)(z - a) / frames);
    }
#else
    printf("bench: build with -DDMK_BENCH on a Mac\n");
#endif
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest")) { selftest(); return fails ? 1 : 0; }
    if (argc > 2 && !strcmp(argv[1], "--wav")) { wav(argv[2]); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--bench")) { bench(); return 0; }
    tour(argc > 1 ? argv[1] : "build/host");
    return fails ? 1 : 0;
}
