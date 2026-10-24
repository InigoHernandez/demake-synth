/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on a computer: the same core, for tests, screenshots and renders.
 *   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -Ifirmware/gen -o build/host/dmk_host host/dmk_host.c
 *   build/host/dmk_host DIR            the tour: every screen as DIR/NN_name.png, the text audit over each
 *   build/host/dmk_host --selftest     behaviour checks; exit 1 on a failure
 *   build/host/dmk_host --wav FILE     a chord held and let go, 3 s
 *   build/host/dmk_host --bench        instructions per sample of the bench build's levels (build with -DDMK_BENCH)
 *   build/host/dmk_host --demo DIR [N...]  a phrase on each sound (or the ones named), dry and through the effects
 *   build/host/dmk_host --cpu          the engine's instructions a sample: idle, 12 voices dry, 12 with the effects */
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

/* the store, in RAM: a sector erased to 0xFF and written, as the flash; host_store_on(0) takes it away */
static uint8_t host_store[65536];
static int hs_read(uint32_t at, void *p, uint32_t n) { if (at + n > sizeof host_store) return -1; memcpy(p, host_store + at, n); return 0; }
static int hs_write(uint32_t at, const void *p, uint32_t n)
{
    if (at & 4095u || n > 4096u || at + 4096u > sizeof host_store) return -1;
    memset(host_store + at, 0xFF, 4096); memcpy(host_store + at, p, n);
    return 0;
}
static void host_store_on(int on, int fresh)
{
    if (fresh) memset(host_store, 0xFF, sizeof host_store);
    dmk_store_read = on ? hs_read : NULL; dmk_store_write = on ? hs_write : NULL;
}

static int shots;
static void shot_fr(const char *dir, const char *name, const uint8_t *fr)
{
    char path[512];
    static uint8_t rgb[DMK_W * DMK_H * 3];
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
static void shot(const char *dir, const char *name) { shot_fr(dir, name, dmk_frame(now_ms)); }

static void tour(const char *dir)
{
    host_store_on(1, 1);                                /* a store, empty: the tour saves a song */
    dmk_init(1);
    now_ms = 0;
    ui.hinted = 0xFFFFu;                                /* the first-time hints stay off the pictures */
    wait_ms(700); shot(dir, "boot_land");              /* the boot: a landscape, full screen */
    wait_ms(1900); shot(dir, "boot");                   /* the word held, lit */
    wait_ms(1000); shot(dir, "home");
    static const struct { int b; const char *n; } P[] = {{B_SEQ, "roll"}, {B_EDIT, "sound"}, {B_SEL, "filter"},
        {B_ENV, "amp"}, {B_FX, "fx"}, {B_ARP, "chord"}, {B_GLO, "global"}, {B_LFO, "lfo"}};
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) { tap(P[i].b); shot(dir, P[i].n); }
    tap(B_FX); tap(B_FX); shot(dir, "fx_space");         /* FX again: the next row built */
    tap(B_GLO); tap(B_GLO); shot(dir, "global_echo"); tap(B_GLO); shot(dir, "global_setup"); tap(B_GLO); tap(B_GLO);
    tap(B_EDIT); tap(B_EDIT); shot(dir, "sound_layer");   /* EDIT again: the layer, off */
    for (int i = 0; i < 70; i++) dmk_encoder(E_K1, 1);
    for (int i = 0; i < 9; i++) { dmk_encoder(E_ALGO, 1); wait_ms(4); }
    dmk_key(7, 1); wait_ms(400); shot(dir, "sound_layer_on"); dmk_key(7, 0);
    tap(B_EDIT);                                        /* the voice: glide, velocity, the key's tracking, grit */
    for (int i = 0; i < 50; i++) dmk_encoder(E_K1, 1);
    for (int i = 0; i < 90; i++) dmk_encoder(E_K2, 1);
    for (int i = 0; i < 64; i++) dmk_encoder(E_K3, 1);
    dmk_encoder(E_K4, 1);
    dmk_key(9, 1); wait_ms(300); shot(dir, "sound_voice"); dmk_key(9, 0);
    tap(B_EDIT);                                        /* back to the first row */
    tap(B_LFO);                                         /* the LFO: deep on the cutoff, a fade */
    for (int i = 0; i < 80; i++) dmk_encoder(E_K2, 1);
    for (int i = 0; i < 40; i++) dmk_encoder(E_K4, 1);
    shot(dir, "lfo_on");
    tap(B_LFO); dmk_encoder(E_K1, 1); dmk_encoder(E_K1, 1); dmk_encoder(E_K2, 1); shot(dir, "lfo_sync");   /* synced, a note restarts it */
    tap(B_LFO);
    sound_init(ui.synth);                               /* the first song's sound again */
    tap(B_SEL); for (int i = 0; i < 30; i++) dmk_encoder(E_K1, -1); for (int i = 0; i < 40; i++) dmk_encoder(E_K2, 1);
    shot(dir, "filter_turned");
    tap(B_HOME); dmk_encoder(E_SELECT, 1); shot(dir, "home_synth2");
    dmk_button(B_HOME, 1); wait_ms(500); dmk_key(4, 1); dmk_key(4, 0); dmk_key(3, 1); dmk_key(3, 0); wait_ms(60); shot(dir, "home_held");
    dmk_key(0, 1); dmk_key(0, 0); dmk_key(3, 1); dmk_key(3, 0); dmk_button(B_HOME, 0); wait_ms(60);   /* pattern 3 made, synth 2 muted and back */
    tap(B_OCTU); wait_ms(300); shot(dir, "notice");
    tap(B_SAVE); wait_ms(100); shot(dir, "saved"); wait_ms(1500);   /* SAVE: the song into its slot */
    dmk_button(B_SAVE, 1); wait_ms(1200); dmk_button(B_SAVE, 0); wait_ms(60); dmk_encoder(E_K1, 1); wait_ms(60); shot(dir, "discs");
    tap(B_GLO); tap(B_GLO);                             /* to SETUP */
    for (int t = 1; t < DMK_THEMES; t++) {
        dmk_encoder(E_K1, 1);
        char n[24], lc[12]; int i;
        for (i = 0; i < 11 && THEME_NAME[theme][i]; i++) lc[i] = (char)(THEME_NAME[theme][i] | 0x20);   /* a file's name: lowercase */
        lc[i] = 0;
        tap(B_HOME); snprintf(n, sizeof n, "home_%s", lc); shot(dir, n);
        tap(B_SEQ); snprintf(n, sizeof n, "roll_%s", lc); shot(dir, n);
        tap(B_GLO);
    }
    theme = 0;
    tap(B_SEQ); dmk_encoder(E_SELECT, 1); wait_ms(150); dmk_encoder(E_SELECT, 1); shot(dir, "roll_synth3");   /* its own pattern */
    dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_SEQ, 1); dmk_button(B_SEQ, 0); dmk_button(B_HOME, 0); wait_ms(30);   /* Arrange */
    ui.synth = 1; dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0); seq_add(1, 10, 2, 74, 100);   /* a variation for the piano */
    wait_ms(1600); shot(dir, "arrange_scene");
    dmk_encoder(E_ALGO, 1);                             /* the Song row */
    dmk_key(0, 1); dmk_key(0, 0); dmk_key(4, 1); dmk_key(4, 0); dmk_encoder(E_K1, -1); dmk_encoder(E_K3, 1); dmk_encoder(E_K3, 1);
    dmk_encoder(E_K1, 1); dmk_key(0, 1); dmk_key(0, 0); dmk_key(4, 1); dmk_key(4, 0); dmk_encoder(E_K1, -1); dmk_encoder(E_K1, -1);
    wait_ms(60); shot(dir, "arrange");
    dmk_button(B_OCTD, 1); dmk_button(B_OCTU, 1); wait_ms(700); dmk_button(B_OCTU, 0); dmk_button(B_OCTD, 0); wait_ms(1600);
    shot(dir, "landscape"); dmk_encoder(E_K1, -1);       /* the landscape full screen, and back */
    dmk_encoder(E_ALGO, 1);
    tap(B_ARP); tap(B_ARP); tap(B_ARP); dmk_encoder(E_K2, 1); dmk_encoder(E_K2, 1); wait_ms(60); shot(dir, "chord_keys");   /* the Keys row */
    dmk_encoder(E_K2, -1); dmk_encoder(E_K2, -1); tap(B_SEQ);
    dmk_button(B_SEQ, 1); wait_ms(500); shot(dir, "roll_seq_held");                               /* SEQ: OCT- clears */
    dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); wait_ms(100); shot(dir, "roll_clear_armed"); dmk_button(B_SEQ, 0); wait_ms(2100);
    ta_reset(); boot(1650); shot_fr(dir, "boot_arrive", fb);
    ta_reset(); boot(2200); shot_fr(dir, "boot_heat", fb);
    for (int k = 0; k < DMK_SCENES; k++) { char nm[16]; boot_sc = (uint8_t)k; ta_reset(); boot(700); snprintf(nm, sizeof nm, "boot_land%d", k); shot_fr(dir, nm, fb); }
    ui.boot_done = 1; ui.synth = 0; tap(B_HOME);
    for (int k = 4; k < DMK_SCENES; k++) {              /* Home on the room and on the desk */
        for (int i = 0; i < DMK_SAMPLES_N; i++) if (DMK_SAMPLES[i].scene == k) { ui.val[0][PG_SOUND][0][0] = (uint8_t)i; break; }
        wait_ms(1600); shot(dir, k == 4 ? "home_room" : "home_desk");
    }
    shot_fr(dir, "update", dmk_update_frame(3, 0));
    shot_fr(dir, "update_write", dmk_update_frame(5, 0));
    shot_fr(dir, "update_stopped", dmk_update_frame(9, -3));
    host_store_on(0, 0);
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
    for (int k = 0; k < 600; k++) dmk_render(b, 512);  /* 7 s: the release over, every voice silent */
    for (int v = 0; v < DMK_VOICES; v++) if (vc[v].stage) { fails++; printf("selftest: a released voice still plays\n"); break; }
    for (int k = 0; k < 1400; k++) dmk_render(b, 512); /* 23 s in all: the room's and the echo's tails gone too */
    e = 0; for (int i = 0; i < 1024; i++) e += b[i] < 0 ? -b[i] : b[i];
    if (e > 1024 * 2) { fails++; printf("selftest: a released key still sounds (%lld)\n", (long long)e); }
    for (int k = 0; k < DMK_KEYS; k++) dmk_key(k, 1);       /* every key at once: 12 voices, no clip past full scale */
    for (int k = 0; k < 20; k++) dmk_render(b, 512);
    for (int k = 0; k < DMK_KEYS; k++) dmk_key(k, 0);
    /* a tap on a page button opens it, a second moves its row */
    wait_ms(1600);
    tap(B_FX); if (ui.page != PG_FX || ui.row[PG_FX] != 1) { fails++; printf("selftest: FX does not open FX on Media\n"); }
    tap(B_FX); if (ui.row[PG_FX] != 2) { fails++; printf("selftest: FX again is not the next row\n"); }
    dmk_encoder(E_ALGO, 1); if (ui.row[PG_FX] != 3) { fails++; printf("selftest: ALGORITHM does not move the rows\n"); }
    dmk_encoder(E_ALGO, -1); dmk_encoder(E_ALGO, -1); dmk_encoder(E_ALGO, -1);
    if (ui.row[PG_FX] != 0) { fails++; printf("selftest: ALGORITHM does not reach the first row\n"); }
    dmk_button(B_FX, 1); dmk_encoder(E_ALGO, 1); dmk_button(B_FX, 0); wait_ms(30);
    if (syn[ui.synth].colour != FC_SAT || ui.row[PG_FX] != 0) { fails++; printf("selftest: FX held and ALGORITHM do not pick the effect\n"); }
    dmk_button(B_FX, 1); dmk_encoder(E_ALGO, -1); dmk_button(B_FX, 0); wait_ms(30);
    dmk_encoder(E_ALGO, 1);
    /* the rolls: every synth its own; a key writes at the cursor, OCT- takes a held note away, PLAY sounds them */
    {
        int n0 = sq[0].count, n1 = sq[1].count, n2 = sq[2].count;
        if (sq[0].steps == sq[1].steps && sq[1].steps == sq[2].steps) { fails++; printf("selftest: the rolls share a length\n"); }
        tap(B_SEQ); dmk_encoder(E_SELECT, 1);
        ui.roll_cur[1] = 1;
        dmk_key(4, 1); wait_ms(30);
        if (sq[1].count != n1 + 1 || sq[0].count != n0 || sq[2].count != n2) { fails++; printf("selftest: a key does not write to its own roll\n"); }
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_key(4, 0); wait_ms(30);
        if (sq[1].count != n1) { fails++; printf("selftest: OCT- does not take the held note away\n"); }
        if (ui.octave != 0) { fails++; printf("selftest: OCT- with a note held moved the octave\n"); }
        dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
        if (ui.roll_cur[1] != 1 + ui.roll_pen[1]) { fails++; printf("selftest: a key released does not move the cursor on\n"); }
        seq_del(1, seq_find(1, 1, snap(53 + 4)));
        tap(B_PLAY);
        for (int k = 0; k < 10; k++) dmk_render(b, 512);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on; if (!dmk_playing() || !on) { fails++; printf("selftest: PLAY sounds nothing\n"); } }
        tap(B_PLAY);
        if (dmk_playing()) { fails++; printf("selftest: PLAY again does not stop\n"); }
        dmk_encoder(E_SELECT, -1);
        tap(B_FX);                                      /* back where the next check starts */
    }
    {   /* SELECT: a synth a click; a second step the same way at once is the same click (the owner's FM-1: 1, then 3) */
        int s0 = ui.synth;
        wait_ms(150);
        dmk_encoder(E_SELECT, 1); wait_ms(9); dmk_encoder(E_SELECT, 1);
        if (ui.synth != s0 + 1) { fails++; printf("selftest: one click of SELECT moved %d synths\n", ui.synth - s0); }
        wait_ms(150); dmk_encoder(E_SELECT, 1);
        if (ui.synth != s0 + 2) { fails++; printf("selftest: SELECT's next click did not reach the next synth\n"); }
        wait_ms(150); dmk_encoder(E_SELECT, -1); wait_ms(150); dmk_encoder(E_SELECT, -1);
        if (ui.synth != s0) { fails++; printf("selftest: SELECT back did not return\n"); }
        wait_ms(150);
    }
    /* the chords: a key on CHORD plays the mode's chord on it; PLAY changes them on the clock; KEY keeps a melody in */
    {
        int on = 0, slot0;
        tap(B_ARP);
        dmk_key(4, 1); wait_ms(400);                    /* A3: Am7 */
        for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == ui.synth;
        if (on < 3 || strncmp(ch_name, "Am", 2)) { fails++; printf("selftest: a key on CHORD is not its chord (%d voices, %s)\n", on, ch_name); }
        dmk_key(4, 0); wait_ms(60);
        on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on;
        if (on) { fails++; printf("selftest: a released chord still holds %d notes\n", on); }
        tap(B_PLAY);
        for (int k = 0; k < 5; k++) dmk_render(b, 512);
        on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 0;
        slot0 = ch_shown;
        if (on < 3) { fails++; printf("selftest: PLAY does not sound the chords on the pad (%d)\n", on); }
        for (int k = 0; k < 300; k++) dmk_render(b, 512);   /* ~3.5 s: past the first slot's bar at 72 */
        if (ch_shown == slot0) { fails++; printf("selftest: the progression does not move on\n"); }
        for (int v = 0; v < DMK_VOICES; v++) if (vc[v].on && vc[v].sy == 1 && !(pc_bent() >> (vc[v].note % 12) & 1u)) { fails++; printf("selftest: a note following the key is off it\n"); break; }
        tap(B_PLAY);
        /* writing your own: REC armed, stopped, keys into a USER progression's slots; OCT- with one held takes it away;
         * ARP held + OCT+ prints it into the synth's roll */
        tap(B_REC);
        dmk_key(4, 1); wait_ms(60); dmk_key(4, 0); wait_ms(60);
        dmk_key(0, 1); wait_ms(60); dmk_key(0, 0); wait_ms(60);
        if (ch.prog < 16 || cur_prog()->n < 2 || ch_cur != 2) { fails++; printf("selftest: keys with REC do not write slots (%d, %d)\n", cur_prog()->n, ch_cur); }
        {   /* the preset was copied in (four slots), the keys wrote over the first two; one more, then OCT- takes it */
            int nb = cur_prog()->n;
            dmk_key(2, 1); wait_ms(60); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_key(2, 0); wait_ms(60);
            if (cur_prog()->n != nb - 1) { fails++; printf("selftest: OCT- with a written chord held does not take it away\n"); }
        }
        tap(B_REC);
        dmk_button(B_ARP, 1); wait_ms(30); dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0); dmk_button(B_ARP, 0); wait_ms(60);
        if (!sq[ui.synth].count || ch.follow[ui.synth] != FW_FREE) { fails++; printf("selftest: ARP + OCT+ does not print to the roll\n"); }
        if (ui.page != PG_CHORD) { fails++; printf("selftest: printing left the page\n"); }
        tap(B_FX);
    }
    /* clearing: SEQ held and OCT- twice, this synth's roll; HOME held and OCT- twice, all three; once arms only */
    {
        int other = (ui.synth + 1) % DMK_SYNTHS, n = sq[other].count;
        if (!sq[ui.synth].count) seq_add(ui.synth, 0, 2, 60, 100);
        dmk_button(B_SEQ, 1); wait_ms(30); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); wait_ms(30);
        if (!sq[ui.synth].count) { fails++; printf("selftest: one OCT- with SEQ held cleared the roll\n"); }
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_SEQ, 0); wait_ms(30);
        if (sq[ui.synth].count || sq[other].count != n) { fails++; printf("selftest: SEQ + OCT- twice does not clear this roll alone\n"); }
        if (ui.page == PG_ROLL && ui.octave) { fails++; printf("selftest: clearing moved the octave\n"); }
        dmk_button(B_HOME, 1); wait_ms(400); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); wait_ms(30);
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_HOME, 0); wait_ms(30);
        for (int s = 0; s < DMK_SYNTHS; s++) if (sq[s].count) { fails++; printf("selftest: HOME + OCT- twice leaves notes in a roll\n"); break; }
        if (ui.page == PG_HOME) { fails++; printf("selftest: a used HOME opened Home on release\n"); }
        seq_init();
    }
    /* the store: SAVE keeps the song; a start loads the newest; SONGS loads, saves over (twice), empties; EDIT + SAVE
     * puts the sound in the library, PRESETS reaches it, EDIT + OCT- is INIT */
    {
        int notes, cut;
        host_store_on(1, 1);
        dmk_init(9); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        seq_add(0, 5, 3, 62, 90); notes = sq[0].count;
        ui.val[0][PG_FILTER][0][0] = 33; cut = 33; sp_sync();
        tap(B_SAVE);
        if (strncmp(ui.note, "Saved in song 01", 16)) { fails++; printf("selftest: SAVE does not save (%s)\n", ui.note); }
        seq_play(1); tap(B_SAVE); seq_play(0);
        if (strcmp(ui.note, "Stop to save")) { fails++; printf("selftest: SAVE while playing is not refused\n"); }
        dmk_init(10); now_ms = 0; wait_ms(60);
        if (sq[0].count != notes || ui.val[0][PG_FILTER][0][0] != cut || seq_find(0, 5, 62) < 0) { fails++; printf("selftest: a start does not load the saved song\n"); }
        ui.boot_done = 1;
        ui.val[1][PG_AMP][0][0] = 5; sq[2].count = 0;   /* a second song, in slot 2 */
        dmk_button(B_SAVE, 1); wait_ms(1100); dmk_button(B_SAVE, 0); wait_ms(30);
        if (ui.page != PG_DISCS) { fails++; printf("selftest: SAVE held does not open SONGS\n"); }
        dmk_encoder(E_K1, 1); tap(B_SAVE);
        if (!song_used(1) || ui.song != 1) { fails++; printf("selftest: SAVE on SONGS does not save into the slot picked\n"); }
        dmk_encoder(E_K1, -1); tap(B_PLAY);             /* slot 1: the first song back */
        if (ui.page != PG_HOME || ui.val[1][PG_AMP][0][0] == 5 || sq[0].count != notes) { fails++; printf("selftest: PLAY on SONGS does not load\n"); }
        dmk_button(B_SAVE, 1); wait_ms(1100); dmk_button(B_SAVE, 0); wait_ms(30); dmk_encoder(E_K1, 1);
        tap(B_SAVE);                                    /* over song 2, once: asks */
        if (strcmp(ui.note, "SAVE again: over it")) { fails++; printf("selftest: saving over another song does not ask\n"); }
        tap(B_OCTD); tap(B_OCTD);
        if (song_used(1)) { fails++; printf("selftest: OCT- twice on SONGS does not empty the slot\n"); }
        dmk_encoder(E_K1, 1); tap(B_PLAY);              /* an empty slot: a new song */
        if (sq[0].count == notes && seq_find(0, 5, 62) >= 0) { fails++; printf("selftest: an empty slot does not start a new song\n"); }
        ui.synth = 1; ui.val[1][PG_FILTER][0][1] = 99; sp_sync();
        dmk_button(B_EDIT, 1); wait_ms(30); dmk_button(B_SAVE, 1); dmk_button(B_SAVE, 0); dmk_button(B_EDIT, 0); wait_ms(30);
        if (lib_count() != 1) { fails++; printf("selftest: EDIT + SAVE does not save the sound (%s)\n", ui.note); }
        dmk_button(B_EDIT, 1); wait_ms(30); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_EDIT, 0); wait_ms(30);
        if (ui.val[1][PG_FILTER][0][1] == 99) { fails++; printf("selftest: EDIT + OCT- is not INIT\n"); }
        ui.preset[1] = DMK_SAMPLES_N - 1; ui.val[1][PG_SOUND][0][0] = DMK_SAMPLES_N - 1;
        dmk_encoder(E_PRESET, 1);
        if (ui.val[1][PG_FILTER][0][1] != 99 || strcmp(ui.note, "User 01")) { fails++; printf("selftest: PRESETS does not reach the library (%s)\n", ui.note); }
        dmk_init(11);
        if (lib_count() != 1) { fails++; printf("selftest: the library is not kept\n"); }
        host_store_on(0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* scenes and patterns: HOME held and a white key, a scene (an empty one a copy of this one, its patterns
     * shared); on Arrange's Scene row OCT+ copies the synth's pattern into a new one, the scene's now (a variation);
     * K1-K3 a synth's pattern, at the bar when playing; a black key mutes; a song keeps all of it */
    {
        int n0, bar = 16 * 60000 / 72 / 4;
        dmk_init(12); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        n0 = sq[1].count;
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(2, 1); dmk_key(2, 0); wait_ms(30);
        if (scn_cur != 1 || !scn[1].used || sq[1].count != n0 || syn_pat(1) != 0) { fails++; printf("selftest: an empty scene is not a copy of this one\n"); }
        dmk_button(B_SEQ, 1); dmk_button(B_SEQ, 0); dmk_button(B_HOME, 0); wait_ms(30);
        if (ui.page != PG_ARR || ui.row[PG_ARR] != 0) { fails++; printf("selftest: HOME + SEQ does not open Arrange on its Scene row\n"); }
        ui.synth = 1; dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0);
        if (syn_pat(1) != 1 || !spat[1][1].used) { fails++; printf("selftest: OCT+ on the Scene row does not make a variation\n"); }
        seq_add(1, 3, 1, 70, 100);                      /* the variation has a note more */
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(0, 1); dmk_key(0, 0); wait_ms(30);
        if (scn_cur != 0 || sq[1].count != n0 || syn_pat(1) != 0) { fails++; printf("selftest: back to scene 1, its patterns are not its own\n"); }
        dmk_button(B_HOME, 0); wait_ms(30);
        seq_play(1); wait_ms(100);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(2, 1); dmk_key(2, 0); dmk_button(B_HOME, 0);
        if (scn_cur != 0 || scn_next != 1) { fails++; printf("selftest: playing, a scene does not wait for the bar\n"); }
        wait_ms(bar + 150);
        if (scn_cur != 1 || sq[1].count != n0 + 1) { fails++; printf("selftest: the scene does not come on the bar\n"); }
        dmk_encoder(E_K2, -1);                          /* synth 2 back to its pattern 1, at the bar */
        if (syn_pat(1) != 1 || spat_next[1] != 0) { fails++; printf("selftest: playing, a synth's pattern does not wait for the bar\n"); }
        wait_ms(bar + 150);
        if (syn_pat(1) != 0 || sq[1].count != n0) { fails++; printf("selftest: a synth's pattern does not change on the bar\n"); }
        dmk_encoder(E_K2, 1); wait_ms(bar + 150);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(1, 1); dmk_key(1, 0); dmk_button(B_HOME, 0); wait_ms(1500);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 0; if (!(pat_mute & 1u) || on) { fails++; printf("selftest: a muted synth still plays (%d)\n", on); } }
        seq_play(0);
        host_store_on(1, 1);
        tap(B_SAVE); dmk_init(13); now_ms = 0; wait_ms(60);
        if (!scn[0].used || !scn[1].used || scn[2].used || scn_cur != 1 || syn_pat(1) != 1 || sq[1].count != n0 + 1 || !(pat_mute & 1u))
            { fails++; printf("selftest: a song does not keep its scenes and patterns\n"); }
        host_store_on(0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the song mode: Arrange's Song row typed from the scene keys; PLAY there plays it, a step a bar count; the end
     * stops or fades; HOME + PLAY switches the mode; a take recorded from the HOME switches; the scene copy; saved */
    {
        int bar = 16 * 60000 / 72 / 4;                  /* a bar at 72, in ms */
        dmk_init(15); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(2, 1); dmk_key(2, 0); dmk_key(0, 1); dmk_key(0, 0); dmk_button(B_HOME, 0); wait_ms(30);   /* scene 2 made */
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_SEQ, 1); dmk_button(B_SEQ, 0); dmk_button(B_HOME, 0); wait_ms(30);
        dmk_encoder(E_ALGO, 1);                         /* the Song row */
        if (ui.page != PG_ARR || ui.row[PG_ARR] != 1) { fails++; printf("selftest: ALGORITHM does not reach Arrange's Song row\n"); }
        ui.arr_bars = 1;
        dmk_key(0, 1); dmk_key(0, 0); dmk_key(2, 1); dmk_key(2, 0); dmk_key(0, 1); dmk_key(0, 0); wait_ms(30);   /* 1 2 1 */
        if (song_n != 3 || song[1].scene != 1 || ui.arr_cur != 3) { fails++; printf("selftest: the scene keys do not write the steps (%d)\n", song_n); }
        ui.arr_cur = 1; dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0);   /* 1 2 2 1 */
        if (song_n != 4 || song[2].scene != 1) { fails++; printf("selftest: OCT+ on the Song row does not copy the step\n"); }
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0);   /* 1 2 1 */
        if (song_n != 3) { fails++; printf("selftest: OCT- on the Song row does not delete the step\n"); }
        dmk_encoder(E_K1, -1); dmk_encoder(E_K1, -1); dmk_encoder(E_K4, 1);   /* the cursor home; the end: Stop */
        if (song_end != SE_STOP) { fails++; printf("selftest: K4 is not the song's end\n"); }
        tap(B_PLAY);
        if (!song_playing() || scn_cur != 0) { fails++; printf("selftest: PLAY on Arrange does not play the song\n"); }
        wait_ms(bar + 60);
        if (scn_cur != 1) { fails++; printf("selftest: the song's second step does not come on its bar (%d)\n", scn_cur); }
        wait_ms(2 * bar + 700);                         /* (the host's ms run 3% ahead of its sound) */
        if (seq_on) { fails++; printf("selftest: the song does not stop at its end\n"); }
        dmk_encoder(E_K4, 1);                           /* Fade out */
        tap(B_PLAY); wait_ms(2 * bar + bar / 2);
        if (mix_g >= 32768 || mix_g <= 0) { fails++; printf("selftest: the last step does not fade (%d)\n", (int)mix_g); }
        wait_ms(bar);
        if (seq_on || mix_g != 32768) { fails++; printf("selftest: after the fade the song is not stopped and whole\n"); }
        tap(B_HOME);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_PLAY, 1); dmk_button(B_PLAY, 0); dmk_button(B_HOME, 0); wait_ms(30);
        if (song_mode) { fails++; printf("selftest: HOME + PLAY does not leave song mode\n"); }
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_PLAY, 1); dmk_button(B_PLAY, 0); dmk_button(B_HOME, 0); wait_ms(30);
        if (!song_mode) { fails++; printf("selftest: HOME + PLAY does not enter song mode\n"); }
        tap(B_PLAY); wait_ms(60);
        if (!song_playing()) { fails++; printf("selftest: PLAY in song mode on Home does not play the song\n"); }
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(2, 1); dmk_key(2, 0); dmk_button(B_HOME, 0); wait_ms(30);
        if (song_mode) { fails++; printf("selftest: a scene picked while the song plays does not leave it\n"); }
        tap(B_PLAY);
        /* the copy: HOME, OCT+, a scene key; over one in use twice */
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(0, 1); dmk_key(0, 0); wait_ms(30);   /* scene 1 open */
        scn[0].pat[2] = 3;
        dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0); dmk_key(4, 1); dmk_key(4, 0);   /* to 3, empty */
        if (!scn[2].used || scn[2].pat[2] != 3) { fails++; printf("selftest: HOME + OCT+ and a key does not copy the scene\n"); }
        dmk_button(B_OCTU, 1); dmk_button(B_OCTU, 0); dmk_key(2, 1); dmk_key(2, 0);   /* over 2: asks */
        if (strcmp(ui.note, "Again: over it")) { fails++; printf("selftest: a copy over a scene does not ask (%s)\n", ui.note); }
        dmk_key(2, 1); dmk_key(2, 0);
        if (scn[1].pat[2] != 3) { fails++; printf("selftest: the second press does not copy over\n"); }
        dmk_button(B_HOME, 0); wait_ms(30);
        scn[0].pat[2] = scn[1].pat[2] = scn[2].pat[2] = 0; scn_load(0);
        /* a take: Arrange, REC, PLAY, the HOME switches; stopped, the song */
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_SEQ, 1); dmk_button(B_SEQ, 0); dmk_button(B_HOME, 0); wait_ms(30);
        ui.arr_cur = 0; seq_rec = 1; tap(B_PLAY); wait_ms(bar + 60);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_key(4, 1); dmk_key(4, 0); dmk_button(B_HOME, 0);
        wait_ms(2 * bar); tap(B_PLAY); seq_rec = 0;
        if (song_n != 2 || song[0].scene != 0 || song[1].scene != 2 || !song_mode) { fails++; printf("selftest: a take is not the song (%d steps)\n", song_n); }
        host_store_on(1, 1);
        tap(B_SAVE); dmk_init(16); now_ms = 0; wait_ms(60);
        if (song_n != 2 || song[1].scene != 2 || !song_mode) { fails++; printf("selftest: the song is not saved\n"); }
        host_store_on(0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* harmony: synth 3 in harmony with synth 2, a third above: its roll left, each of 2's notes moved on the scale;
     * a key played on 2 sounds on 3 too, and lets go there too */
    {
        int seen = 0, own = 0, bad = 0;
        dmk_init(17); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ch.follow[2] = FW_HARM2; ch.hint[2] = 2;
        seq_play(1);
        for (int t = 0; t < 400; t++) {
            wait_ms(10);
            for (int v = 0; v < DMK_VOICES; v++) if (vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ROLL) {
                int lead = 0;
                for (int u = 0; u < DMK_VOICES; u++) if (vc[u].on && vc[u].sy == 1 && harm_note(2, vc[u].note) == vc[v].note) lead = 1;
                seen++; if (!lead) bad++;
                for (int i = 0; i < sq[2].count; i++) if (sq[2].n[i].note == vc[v].note) own++;
            }
        }
        seq_play(0);
        if (!seen || bad > seen / 10) { fails++; printf("selftest: a synth in harmony does not move the lead's notes (%d of %d)\n", bad, seen); }
        ui.synth = 1; tap(B_HOME); dmk_key(4, 1); wait_ms(30);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2; if (!on) { fails++; printf("selftest: a key on the lead is not harmonized\n"); } }
        dmk_key(4, 0); wait_ms(30);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2; if (on) { fails++; printf("selftest: a harmonized key leaves its harmony held\n"); } }
        (void)own;
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the landscape full screen: OCT- and OCT+ held half a second, the octave left as it was; the keys still play; a
     * knob or a page button, or the two again, back */
    {
        dmk_init(18); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_HOME);
        dmk_button(B_OCTU, 1); wait_ms(40); dmk_button(B_OCTD, 1); wait_ms(90); dmk_button(B_OCTU, 0); dmk_button(B_OCTD, 0); wait_ms(30);
        if (ui.octave != 0 || ui.full || !strncmp(ui.note, "Octave", 6)) { fails++; printf("selftest: the two tapped together moved the octave (%d, %s)\n", ui.octave, ui.note); }
        tap(B_OCTU);
        if (ui.octave != 1) { fails++; printf("selftest: OCT+ tapped does not raise the octave\n"); }
        tap(B_OCTD);
        dmk_button(B_OCTD, 1); dmk_button(B_OCTU, 1); wait_ms(700); dmk_button(B_OCTU, 0); dmk_button(B_OCTD, 0); wait_ms(30);
        if (!ui.full || ui.octave != 0) { fails++; printf("selftest: OCT- and OCT+ held do not show the landscape (octave %d)\n", ui.octave); }
        dmk_key(4, 1); wait_ms(30);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on; if (!on) { fails++; printf("selftest: the keys do not play over the landscape\n"); } }
        dmk_key(4, 0); wait_ms(30);
        dmk_button(B_OCTU, 1); dmk_button(B_OCTD, 1); wait_ms(700); dmk_button(B_OCTD, 0); dmk_button(B_OCTU, 0); wait_ms(30);
        if (ui.full) { fails++; printf("selftest: the two held again do not leave the landscape\n"); }
        dmk_button(B_OCTD, 1); dmk_button(B_OCTU, 1); wait_ms(700); dmk_button(B_OCTU, 0); dmk_button(B_OCTD, 0); wait_ms(30);
        dmk_encoder(E_K1, 1);
        if (ui.full) { fails++; printf("selftest: a knob does not leave the landscape\n"); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* MIDI: the synths' notes out on their channels; the clock out, 6 a step, with start and stop; in by channel; an
     * outside clock's start, its steps, its stop */
    {
        uint8_t st, a, b;
        int ons = 0, offs = 0, clocks = 0, start = 0, s1 = 0;
        dmk_init(19); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        while (dmk_midi_take(&st, &a, &b)) {}
        ui.synth = 1; tap(B_HOME); dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
        while (dmk_midi_take(&st, &a, &b)) { if (st == 0x91) ons++; if (st == 0x81) offs++; }
        if (!ons || !offs) { fails++; printf("selftest: a key's note does not go out on its synth's channel (%d on, %d off)\n", ons, offs); }
        ui.val[0][PG_GLOBAL][2][1] = choice_val(3, 4); sp_sync();   /* out: notes and the clock */
        seq_play(1); wait_ms(16 * 60000 / 72 / 4 / 16 * 4);   /* four steps */
        seq_play(0);
        while (dmk_midi_take(&st, &a, &b)) { clocks += st == 0xF8; start += st == 0xFA; }
        if (!start || clocks < 20 || clocks > 28) { fails++; printf("selftest: the clock out is not 6 a step (%d in 4 steps)\n", clocks); }
        ui.val[0][PG_GLOBAL][2][0] = choice_val(1, 2); sp_sync();   /* in: by channel */
        dmk_midi(0x92, 64, 100); wait_ms(30);
        for (int v = 0; v < DMK_VOICES; v++) s1 += vc[v].on && vc[v].sy == 2;
        dmk_midi(0x82, 64, 0); wait_ms(30);
        if (!s1) { fails++; printf("selftest: MIDI channel 3 does not play synth 3\n"); }
        ui.val[0][PG_GLOBAL][2][2] = choice_val(1, 2); sp_sync();   /* the clock outside */
        dmk_midi(0xFA, 0, 0);
        if (!seq_on) { fails++; printf("selftest: an outside start does not play\n"); }
        { uint32_t t0 = seq_ticks; wait_ms(500); if (seq_ticks != t0) { fails++; printf("selftest: with the clock outside the steps run on their own\n"); }
          for (int k = 0; k < 12; k++) { dmk_midi(0xF8, 0, 0); wait_ms(20); }
          if (seq_ticks != t0 + 2) { fails++; printf("selftest: 12 clocks are not 2 steps (%u)\n", (unsigned)(seq_ticks - t0)); } }
        dmk_midi(0xFC, 0, 0);
        if (seq_on) { fails++; printf("selftest: an outside stop does not stop\n"); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the Keys row: chords everywhere (the roll's keys still notes), of the type it picks; released whole */
    {
        int on;
        dmk_init(14); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_CHORD][2][0] = choice_val(1, 2); ui.val[0][PG_CHORD][2][1] = choice_val(KT_TRIAD, KT_COUNT); sp_sync();
        tap(B_HOME); ui.synth = 1;
        dmk_key(4, 1); wait_ms(30);
        on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 1;
        if (on != 3) { fails++; printf("selftest: Keys Everywhere, Triads: a key on Home is not a triad (%d)\n", on); }
        dmk_key(4, 0); wait_ms(30);
        on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 1;
        if (on) { fails++; printf("selftest: a chord key on Home leaves %d notes held\n", on); }
        if (!(dmk_leds()[DMK_KEYS + B_ARP])) { fails++; printf("selftest: ARP is not lit with the keys on chords\n"); }
        tap(B_SEQ); { int n0 = sq[1].count, at = 5, i, c = 0; uint8_t nt[8]; char cn[20] = "";
          /* the roll, the keys on chords: a key writes its chord at the cursor (the owner's ask), the cursor after it */
          ui.roll_cur[1] = (uint8_t)at; dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
          if (sq[1].count != n0 + 3) { fails++; printf("selftest: on the roll a chord key does not write its triad (%d)\n", sq[1].count - n0); }
          if (ui.roll_cur[1] != at + ui.roll_pen[1]) { fails++; printf("selftest: the roll's cursor does not move past the chord\n"); }
          for (i = 0; i < sq[1].count; i++) if (sq[1].n[i].step == at) nt[c++] = sq[1].n[i].note;
          if (c != 3 || !chord_of(nt, c, cn)) { fails++; printf("selftest: the chord written is not named (%d notes)\n", c); }
          ui.synth = 0; ui.roll_cur[0] = 7; dmk_key(7, 1); wait_ms(30); dmk_key(7, 0); wait_ms(30);   /* the pad played the chords */
          if (ch.follow[0] != FW_FREE) { fails++; printf("selftest: writing to a chord player's roll does not let it play its roll\n"); } }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* a modifier that was used does nothing on release */
    tap(B_FX);
    dmk_button(B_GLO, 1); wait_ms(30); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_GLO, 0); wait_ms(30);
    if (ui.page != PG_FX) { fails++; printf("selftest: a used GLO opened its page on release\n"); }
    tour(NULL);
    /* the shared spaces off (the owner asked): none of them run, nothing returns; back on, from silence */
    {
        static int16_t ob[512 * 2];
        int i, v, pk = 0;
        dmk_init(25); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_FX][3][0] = 127; ui.val[0][PG_FX][3][1] = 127;   /* a lot sent */
        ui.val[0][PG_GLOBAL][0][4] = choice_val(0, 7); ui.val[0][PG_GLOBAL][1][4] = choice_val(0, 3); sp_sync();
        note_on_s(0, 60);
        for (i = 0; i < 40; i++) dmk_render(ob, 512);
        if (v_cur != VB_OFF || e_cur != ET_OFF) { fails++; printf("selftest: the verb and the echo do not go off (%d %d)\n", v_cur, e_cur); }
        for (v = 0; v < DMK_VOICES; v++) { vc[v].stage = 0; vc[v].on = 0; }
        for (i = 0; i < 20; i++) dmk_render(ob, 512);
        for (v = 0; v < 1024; v++) { int a = ob[v] < 0 ? -ob[v] : ob[v]; if (a > pk) pk = a; }
        if (pk > 64) { fails++; printf("selftest: with the spaces off a note leaves a tail (%d)\n", pk); }
        ui.val[0][PG_GLOBAL][0][4] = choice_val(2, 7); ui.val[0][PG_GLOBAL][1][4] = choice_val(1, 3); sp_sync();
        for (i = 0; i < 4; i++) dmk_render(ob, 512);
        if (v_cur != VB_HALL || e_cur != ET_TAPE) { fails++; printf("selftest: the verb and the echo do not come back\n"); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the chord keys: a lower key never a higher chord, in every voicing (the owner heard B under C go up) */
    {
        int vo, k, lo0 = 0;
        for (vo = 0; vo < 5; vo++) {
            ui.val[0][PG_CHORD][0][0] = choice_val(vo, 5); sp_sync();
            for (k = 0; k < DMK_KEYS; k++) {
                uint8_t nt[8];
                int c = chord_key(53 + k, 0, nt), lo = 127, i;
                for (i = 0; i < c; i++) if (nt[i] < lo) lo = nt[i];
                if (k && lo < lo0) { fails++; printf("selftest: voicing %d, key %d's chord starts under key %d's (%d < %d)\n", vo, k, k - 1, lo, lo0); break; }
                lo0 = lo;
            }
        }
        ui.val[0][PG_CHORD][0][0] = 2 * 128 / 5 + 12; sp_sync();
    }
    /* PRESET on a page of types turns the row's type, not the sound */
    {
        int smp = ui.val[ui.synth][PG_SOUND][0][0];
        tap(B_FX); while (ui.row[PG_FX] != 0) dmk_encoder(E_ALGO, -1);
        dmk_encoder(E_PRESET, 1); dmk_encoder(E_PRESET, 1);
        if (syn[ui.synth].colour != FC_CRUSH || ui.val[ui.synth][PG_SOUND][0][0] != smp) { fails++; printf("selftest: PRESET on FX does not turn the effect (%d)\n", syn[ui.synth].colour); }
        dmk_encoder(E_PRESET, -1); dmk_encoder(E_PRESET, -1); dmk_encoder(E_PRESET, -1);
        if (syn[ui.synth].colour != FC_OFF) { fails++; printf("selftest: PRESET on FX does not stop at Off\n"); }
        tap(B_SEL); dmk_encoder(E_PRESET, 1);
        if (syn[ui.synth].ftype != FT_LP12) { fails++; printf("selftest: PRESET on the filter does not turn its type\n"); }
        dmk_encoder(E_PRESET, -1);
        tap(B_HOME); dmk_encoder(E_PRESET, 1);
        if (ui.val[ui.synth][PG_SOUND][0][0] == smp) { fails++; printf("selftest: PRESET on Home does not step the sound\n"); }
        dmk_encoder(E_PRESET, -1);
    }
    /* clearing: the rolls cleared while playing leave nothing held, the chords a synth played included (the owner
     * heard the first song's pad hold on) */
    {
        int on = 0, v;
        dmk_init(24); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_PLAY); wait_ms(1500);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); wait_ms(30);
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_button(B_HOME, 0); wait_ms(3000);
        for (v = 0; v < DMK_VOICES; v++) on += vc[v].on;
        if (on || strcmp(ui.note, "Rolls cleared, chords off") || ch.follow[0] != FW_FREE) { fails++; printf("selftest: the rolls cleared, %d notes still held (%s)\n", on, ui.note); }
        tap(B_PLAY);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the sound's own controls (0.4): velocity heard, the layer, glide, the LFO's tremolo, grit; the store keeps
     * them, and a song or sound of before loads with them off */
    {
        static int16_t ob[512 * 2];
        int32_t pk[2], lo = 0x7FFFFFFF, hi = 0;
        uint32_t h0 = 0, h1 = 0;
        int k, i, v;
        #define QUIET() do { for (v = 0; v < DMK_VOICES; v++) { vc[v].stage = 0; vc[v].on = 0; } dsp_reset(); } while (0)
        #define PEAK(n, out) do { out = 0; for (i = 0; i < (n); i++) { dmk_render(ob, 512); for (v = 0; v < 1024; v++) { int a_ = ob[v] < 0 ? -ob[v] : ob[v]; if (a_ > out) out = a_; } } } while (0)
        dmk_init(21); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        for (k = 0; k < DMK_SYNTHS; k++) { ui.val[k][PG_FX][1][4] = 0; ui.val[k][PG_FX][3][0] = 0; ui.val[k][PG_FX][3][1] = 0; }
        ui.val[0][PG_SOUND][2][1] = 127;                /* VELOCITY all the way: a soft note is a quiet one */
        sp_sync();
        for (k = 0; k < 2; k++) { QUIET(); note_on_vel(0, 60, SRC_KEY, k ? 127 : 20); PEAK(40, pk[k]); }
        if (pk[0] * 3 > pk[1]) { fails++; printf("selftest: a soft note is not quieter with Velocity up (%d, %d)\n", (int)pk[0], (int)pk[1]); }
        ui.val[0][PG_SOUND][2][1] = 0; sp_sync();
        for (k = 0; k < 2; k++) {                       /* the LAYER: blended in, the voice is another */
            QUIET();
            ui.val[0][PG_SOUND][1][0] = (uint8_t)(k ? 127 : 0); ui.val[0][PG_SOUND][1][4] = (uint8_t)((ui.val[0][PG_SOUND][0][0] + 7) % DMK_SAMPLES_N);
            sp_sync();
            note_on_s(0, 60);
            for (i = 0; i < 20; i++) { dmk_render(ob, 512); for (v = 0; v < 1024; v++) *(k ? &h1 : &h0) = *(k ? &h1 : &h0) * 31u + (uint16_t)ob[v]; }
            if (k && !vc[0].l.on) { fails++; printf("selftest: the layer does not play\n"); }
        }
        if (h0 == h1) { fails++; printf("selftest: the layer blended in changes nothing\n"); }
        ui.val[0][PG_SOUND][1][0] = 0;
        ui.val[0][PG_SOUND][2][0] = 80; sp_sync();      /* GLIDE: from the last note, up to its own pitch */
        QUIET(); syn_last[0] = 0;
        note_on_s(0, 48); dmk_render(ob, 512); note_off_s(0, 48);
        note_on_s(0, 72);
        for (v = 0; v < DMK_VOICES && !(vc[v].on && vc[v].note == 72); v++) {}
        dmk_render(ob, 128);
        if (v == DMK_VOICES || vc[v].m.inc * 3 > vc[v].m.inc0 * 2) { fails++; printf("selftest: a note does not glide up from the last\n"); }
        for (i = 0; i < 400; i++) dmk_render(ob, 512);
        if (v < DMK_VOICES && vc[v].m.inc != vc[v].m.inc0) { fails++; printf("selftest: the glide does not arrive (%d, stage %d, %u %u)\n", (int)vc[v].gl, vc[v].stage, vc[v].m.inc, vc[v].m.inc0); }
        ui.val[0][PG_SOUND][2][0] = 0;
        ui.val[0][PG_LFO][0][0] = 100; ui.val[0][PG_LFO][0][1] = 127;   /* the LFO: a fast, deep tremolo */
        ui.val[0][PG_LFO][0][4] = choice_val(LT_LEVEL, 5); sp_sync();
        QUIET(); note_on_s(0, 60); PEAK(30, k);
        for (k = 0; k < 40; k++) { int32_t b; PEAK(1, b); if (b < lo) lo = b; if (b > hi) hi = b; }
        if (lo * 2 > hi) { fails++; printf("selftest: the LFO on the level does not swing it (%d..%d)\n", (int)lo, (int)hi); }
        ui.val[0][PG_LFO][1][0] = choice_val(3, 8); sp_sync(); dmk_render(ob, 128);   /* synced: a bar a cycle */
        if (!syn[0].lsync || lfo[0].sstep * (165375u * 16u / (72u * 4u)) < 0xF0000000u) { fails++; printf("selftest: the synced LFO's rate is not a bar's\n"); }
        ui.val[0][PG_LFO][1][0] = choice_val(0, 8); ui.val[0][PG_LFO][0][1] = 0;
        ui.val[0][PG_SOUND][2][3] = choice_val(2, 3); sp_sync();   /* GRIT at 11 kHz: it plays, held in fours */
        QUIET(); note_on_s(0, 60); PEAK(20, k);
        if (!k) { fails++; printf("selftest: a voice under grit is silent\n"); }
        ui.val[0][PG_SOUND][2][3] = choice_val(0, 3); sp_sync();
        QUIET();
        /* the store: a song keeps the new controls; one of before 0.4 (version 4) loads with them off */
        host_store_on(1, 1);
        ui.val[0][PG_LFO][0][1] = 64; ui.val[0][PG_SOUND][1][0] = 50; ui.val[0][PG_SOUND][2][0] = 33; ui.val[0][PG_FILTER][0][3] = 20;
        tap(B_SAVE); dmk_init(22); now_ms = 0; wait_ms(60);
        if (ui.val[0][PG_LFO][0][1] != 64 || ui.val[0][PG_SOUND][1][0] != 50 || ui.val[0][PG_SOUND][2][0] != 33 || ui.val[0][PG_FILTER][0][3] != 20)
            { fails++; printf("selftest: a song does not keep the layer, the voice, the drive or the LFO\n"); }
        for (k = 0; k < ST_SONGS; k++) if (host_store[0x2000 + k * 4096] == 'D') host_store[0x2000 + k * 4096 + 12] = 4;   /* as 0.3 wrote it */
        dmk_init(23); now_ms = 0; wait_ms(60);
        if (ui.val[0][PG_LFO][0][1] || ui.val[0][PG_SOUND][1][0] || ui.val[0][PG_SOUND][2][0] || ui.val[0][PG_FILTER][0][3] || !song_used(0))
            { fails++; printf("selftest: a song of 0.3 loads with its new controls on\n"); }
        sound_put(0, &lib[0][2]); lib[0][0] = 1; lib[0][1] = 0; lib[0][2 + 8 * 5 + 1] = 64;   /* a sound of 0.3, its LFO's depth 64 */
        lib_load(0, 0);
        if (ui.val[0][PG_LFO][0][1]) { fails++; printf("selftest: a sound of 0.3 loads with its LFO on\n"); }
        lib[0][0] = 0;
        host_store_on(0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        #undef QUIET
        #undef PEAK
    }
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

static void wav_write(const char *path, const int16_t *b, int n)
{
    FILE *f = fopen(path, "wb");
    uint8_t h[45] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0....\0\0\0\0\x04\0\x10\0data....";
    uint32_t v = 36 + (uint32_t)n * 4;
    if (!f) { perror(path); exit(2); }
    memcpy(h + 4, &v, 4);
    v = DMK_SR; memcpy(h + 24, &v, 4); v = DMK_SR * 4; memcpy(h + 28, &v, 4);
    v = (uint32_t)n * 4; memcpy(h + 40, &v, 4);
    fwrite(h, 1, 44, f); fwrite(b, 4, (size_t)n, f); fclose(f);
}

/* a phrase on one sound, for the owner to hear: Am(add9) held, then a line over it, then the tail. dry: no tape,
 * no sends; else the first synth's defaults (tape, room, echo) */
static void demo_one(const char *dir, int idx, int dry)
{
    static const struct { float t, len; uint8_t n; } PH[] = {
        {0.0f, 3.2f, 57}, {0.0f, 3.2f, 60}, {0.0f, 3.2f, 64}, {0.0f, 3.2f, 71},
        {3.4f, 0.5f, 64}, {3.9f, 0.5f, 67}, {4.4f, 0.5f, 69}, {4.9f, 0.9f, 72}, {5.8f, 0.5f, 71}, {6.3f, 0.5f, 69},
        {6.8f, 1.6f, 64}, {6.8f, 1.6f, 57}};
    int n = DMK_SR * 11, i, k, np = (int)(sizeof PH / sizeof PH[0]);
    int16_t *b = malloc((size_t)n * 4);
    char path[512], name[16];
    dmk_init(1);
    ui.val[0][PG_SOUND][0][0] = (uint8_t)idx;
    if (dry) { ui.val[0][PG_FX][1][4] = 0; ui.val[0][PG_FX][3][0] = 0; ui.val[0][PG_FX][3][1] = 0; }
    sp_sync();
    for (i = 0; i < n; i += DMK_SUB) {
        for (k = 0; k < np; k++) {
            int on = (int)(PH[k].t * DMK_SR), off = (int)((PH[k].t + PH[k].len) * DMK_SR);
            if (on >= i && on < i + DMK_SUB) note_on_s(0, PH[k].n);
            if (off >= i && off < i + DMK_SUB) note_off_s(0, PH[k].n);
        }
        dmk_render(b + i * 2, DMK_SUB);
    }
    for (i = 0; i < 15 && DMK_SAMPLES[idx].name[i]; i++) name[i] = DMK_SAMPLES[idx].name[i] == ' ' ? '_' : DMK_SAMPLES[idx].name[i];
    name[i] = 0;
    snprintf(path, sizeof path, "%s/%02d_%s_%s.wav", dir, idx, name, dry ? "dry" : "fx");
    wav_write(path, b, n);
    free(b);
}

/* the effects, each on a sound that shows it: 11 s of the same chord and line, the knobs set where the effect is
 * plain to hear; a filter's cutoff swept so its type is heard; a WAV each */
static void fx_one(const char *dir, const char *name, const char *sound, int page, int row, int type, const uint8_t *k, int verb, int sweep)
{
    static const struct { float t, len; uint8_t n; } PH[] = {
        {0.0f, 3.2f, 57}, {0.0f, 3.2f, 60}, {0.0f, 3.2f, 64}, {0.0f, 3.2f, 71},
        {3.4f, 0.5f, 64}, {3.9f, 0.5f, 67}, {4.4f, 0.5f, 69}, {4.9f, 0.9f, 72}, {5.8f, 0.5f, 71}, {6.3f, 0.5f, 69},
        {6.8f, 1.6f, 64}, {6.8f, 1.6f, 57}};
    int n = DMK_SR * 11, i, j, np = (int)(sizeof PH / sizeof PH[0]);
    int16_t *b = malloc((size_t)n * 4);
    char path[512];
    dmk_init(1);
    ui.val[0][PG_SOUND][0][0] = (uint8_t)smp_named(sound);
    ui.val[0][PG_FX][1][4] = 0;                         /* only the effect heard: no tape */
    ui.val[0][PG_FX][3][0] = verb ? 100 : 30;           /* a little verb, or a lot for the verbs */
    ui.val[0][PG_FX][3][1] = 0;
    if (page >= 0) {
        int nch = page == PG_GLOBAL ? (row ? 3 : 7) : page == PG_FILTER ? 8 : row == 0 ? 5 : 4;
        if (page == PG_GLOBAL) type++;                  /* (Off first) */
        ui.val[0][page][row][4] = (uint8_t)(type * 128 / nch + 128 / nch / 2);
        for (j = 0; j < 4; j++) if (k[j] != 255) ui.val[0][page][row][j] = k[j];
    }
    if (page != PG_GLOBAL || row) ui.val[0][PG_GLOBAL][0][4] = choice_val(2, 7);   /* the hall under the others */
    if (page == PG_GLOBAL && row == 1) { ui.val[0][PG_FX][3][1] = 110; ui.val[0][PG_FX][3][0] = 70; }   /* grains: a lot sent */
    sp_sync();
    for (i = 0; i < n; i += DMK_SUB) {
        for (j = 0; j < np; j++) {
            int on = (int)(PH[j].t * DMK_SR), off = (int)((PH[j].t + PH[j].len) * DMK_SR);
            if (on >= i && on < i + DMK_SUB) note_on_s(0, PH[j].n);
            if (off >= i && off < i + DMK_SUB) note_off_s(0, PH[j].n);
        }
        if (sweep && !(i & 1023)) { ui.val[0][PG_FILTER][0][0] = (uint8_t)(20 + (int64_t)i * 100 / n); sp_sync(); }
        dmk_render(b + i * 2, DMK_SUB);
    }
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    wav_write(path, b, n);
    free(b);
}

static void fx_demo(const char *dir)
{
    static const uint8_t VB[4] = {100, 60, 100, 100}, SAT[4] = {96, 70, 50, 127}, CR[4] = {85, 90, 70, 40};
    static const uint8_t LO[4] = {95, 80, 0, 70}, NO[4] = {60, 50, 70, 25}, TA[4] = {70, 70, 60, 64};
    static const uint8_t CA[4] = {80, 60, 60, 80}, VH[4] = {90, 50, 50, 90}, CH[4] = {90, 100, 40, 100};
    static const uint8_t VI[4] = {60, 255, 70, 40}, DR[4] = {110, 90, 40, 255}, FI[4] = {255, 100, 64, 255};
    static const char *const VN[6] = {"verb_1_room", "verb_2_hall", "verb_3_space", "verb_4_haze", "verb_5_bloom", "verb_6_lofi"};
    static const uint8_t VL[4] = {100, 60, 124, 100}, GR[4] = {110, 70, 70, 60};
    static const char *const FN[3] = {"filter_notch", "filter_peak", "filter_vowel"};
    int t;
    for (t = 0; t < 6; t++) fx_one(dir, VN[t], t == 4 ? "Lunaria" : "Nymphaea", PG_GLOBAL, 0, t, t == 1 || t == 2 || t == 3 ? VL : VB, 1, 0);
    fx_one(dir, "echo_grains", "Nymphaea", PG_GLOBAL, 1, ET_GRAINS, GR, 1, 0);
    fx_one(dir, "colour_1_saturate", "Nymphaea", PG_FX, 0, FC_SAT, SAT, 0, 0);
    fx_one(dir, "colour_2_crush", "Nymphaea", PG_FX, 0, FC_CRUSH, CR, 0, 0);
    fx_one(dir, "colour_3_lossy", "Nymphaea", PG_FX, 0, FC_LOSSY, LO, 0, 0);
    fx_one(dir, "colour_4_noise", "Nymphaea", PG_FX, 0, FC_NOISE, NO, 0, 0);
    fx_one(dir, "media_1_tape", "Lunaria", PG_FX, 1, FM_TAPE, TA, 0, 0);
    fx_one(dir, "media_2_cassette", "Lunaria", PG_FX, 1, FM_CASS, CA, 0, 0);
    fx_one(dir, "media_3_vhs", "Lunaria", PG_FX, 1, FM_VHS, VH, 0, 0);
    fx_one(dir, "motion_1_chorus", "Lithops", PG_FX, 2, FO_CHORUS, CH, 0, 0);
    fx_one(dir, "motion_2_vibrato", "Nymphaea", PG_FX, 2, FO_VIB, VI, 0, 0);
    fx_one(dir, "motion_3_drift", "Lunaria", PG_FX, 2, FO_DRIFT, DR, 0, 0);
    for (t = 0; t < 3; t++) fx_one(dir, FN[t], "Lunaria", PG_FILTER, 0, FT_NOTCH + t, FI, 0, 1);
    printf("fx demo: %s\n", dir);
}

/* the progressions: each one 16 bars at 72, the pad playing its chords, the piano's line following the key */
static void chord_demo(const char *dir)
{
    int n = DMK_SR * 26, i, p;
    int16_t *b = malloc((size_t)n * 4);
    char path[512];
    for (p = 0; p < 16; p++) {
        dmk_init(1);
        ui.val[0][PG_CHORD][0][4] = choice_val(p, PROG_N);
        ui.val[0][PG_CHORD][1][1] = (uint8_t)(PROG[p].mode * 128 / 6 + 10);
        ui.val[0][PG_CHORD][0][0] = (uint8_t)(PROG[p].voicing * 128 / 5 + 12);
        sp_sync();
        seq_play(1);
        for (i = 0; i < n; i += 128) { if (i == DMK_SR * 22) seq_play(0); dmk_render(b + i * 2, 128); }
        {
            char nm[32]; int k;
            for (k = 0; PROG[p].name[k] && k < 30; k++) nm[k] = PROG[p].name[k] == ' ' ? '_' : (char)(PROG[p].name[k] | 0x20);
            nm[k] = 0;
            snprintf(path, sizeof path, "%s/%02d_%s.wav", dir, p + 1, nm);
        }
        wav_write(path, b, n);
    }
    free(b);
    printf("chord demo: %s\n", dir);
}

static void demo(const char *dir, int argc, char **argv)
{
    int i;
    if (argc == 0) for (i = 0; i < DMK_SAMPLES_N; i++) { demo_one(dir, i, 1); demo_one(dir, i, 0); }
    else for (i = 0; i < argc; i++) { demo_one(dir, atoi(argv[i]), 1); demo_one(dir, atoi(argv[i]), 0); }
    printf("demo: %s\n", dir);
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

#ifdef __APPLE__
/* the engine's cost, counted as the FM-1's budget is (docs/plan.md, 1.1): build with -fno-builtin -fno-vectorize
 * -fno-slp-vectorize; 12 voices held across the three synths */
static double cpu_case(int fx)
{
    static int16_t b[128 * 2];
    int frames = DMK_SR * 10, i, s;
    uint64_t a, z;
    dmk_init(7);
    for (s = 0; s < DMK_SYNTHS; s++) {
        if (!fx) { ui.val[s][PG_FX][1][4] = 0; ui.val[s][PG_FX][3][0] = 0; ui.val[s][PG_FX][3][1] = 0; }
        else ui.val[s][PG_FX][1][4] = fx == 1 ? 48 : 127;   /* tape; or VHS */
        if (fx == 2) { ui.val[s][PG_FX][0][4] = 90; ui.val[s][PG_FX][2][4] = 40; }   /* lossy, chorus */
    }
    if (fx == 3) for (s = 0; s < DMK_SYNTHS; s++) {     /* and a layer under every voice, the LFO on the cutoff */
        ui.val[s][PG_SOUND][1][0] = 90; ui.val[s][PG_LFO][0][1] = 60; ui.val[s][PG_LFO][0][4] = choice_val(LT_CUTOFF, 5);
    }
    if (fx == 3) fx = 2;
    ui.val[0][PG_GLOBAL][0][4] = fx == 2 ? choice_val(4, 7) : choice_val(1, 7);   /* the heaviest: haze; else the room */
    ui.val[0][PG_GLOBAL][1][4] = fx == 2 ? choice_val(2, 3) : choice_val(1, 3);   /* and the grains; else the tape */
    sp_sync();
    for (i = 0; i < DMK_VOICES; i++) note_on_s(i % DMK_SYNTHS, 48 + i * 3);
    for (i = 0; i < 100; i++) dmk_render(b, 128);
    a = instructions();
    for (i = 0; i < frames; i += 128) dmk_render(b, 128);
    z = instructions();
    return (double)(z - a) / frames;
}
/* the two cores, counted: a stand-in for the second core runs each job in place and counts it, and counts what the
 * first does meanwhile; the first waits for the second when its own share is the shorter (the critical path) */
static uint64_t ins_c, job_ins, job_wait, par_t0;
static uint64_t ins_job;
static int host_par_begin(void (*job)(void))
{
    uint64_t a = instructions();
    job();
    ins_job = instructions() - a - ins_c;
    job_ins += ins_job;
    par_t0 = instructions();
    return 1;
}
static void host_par_end(void)
{
    uint64_t own = instructions() - par_t0 - ins_c;
    if (ins_job > own) job_wait += ins_job - own;
}
static void cpu_cores(double *c0, double *c1)
{
    static int16_t b[128 * 2];
    int frames = DMK_SR * 10, i;
    uint64_t a, z, t;
    ins_c = ~0ull;
    for (i = 0; i < 64; i++) { a = instructions(); z = instructions(); if (z - a < ins_c) ins_c = z - a; }
    for (i = 0; i < 100; i++) dmk_render(b, 128);
    a = instructions();                                 /* the whole, with no second core */
    for (i = 0; i < frames; i += 128) dmk_render(b, 128);
    t = instructions() - a;
    dmk_par_begin = host_par_begin; dmk_par_end = host_par_end;
    job_ins = job_wait = 0;
    for (i = 0; i < frames; i += 128) dmk_render(b, 128);
    dmk_par_begin = 0; dmk_par_end = 0;
    *c1 = (double)job_ins / frames;
    *c0 = (double)(t - job_ins + job_wait) / frames;
}
static void cpu(void)
{
    static int16_t b[128 * 2];
    int frames = DMK_SR * 10, i;
    uint64_t a, z;
    dmk_init(7);
    for (i = 0; i < 100; i++) dmk_render(b, 128);
    a = instructions();
    for (i = 0; i < frames; i += 128) dmk_render(b, 128);
    z = instructions();
    printf("cpu: idle %.0f, 12 voices dry %.0f, 12 voices with tape x3, room and echo %.0f instructions a sample\n",
           (double)(z - a) / frames, cpu_case(0), cpu_case(1));
    {   /* the heaviest: every synth lossy, VHS and chorus, the haze, the grains; each core's share */
        double all = cpu_case(2), c0, c1;
        cpu_cores(&c0, &c1);
        printf("cpu: the heaviest (lossy, VHS, chorus x3, haze, grains) %.0f; on the FM-1 CPU0 %.0f (its wait included), CPU1 %.0f\n",
               all, c0, c1);
        all = cpu_case(3);
        cpu_cores(&c0, &c1);
        printf("cpu: and a layer under every voice, the LFO on: %.0f; CPU0 %.0f, CPU1 %.0f\n", all, c0, c1);
    }
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest")) { selftest(); return fails ? 1 : 0; }
    if (argc > 2 && !strcmp(argv[1], "--wav")) { wav(argv[2]); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--bench")) { bench(); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--demo")) { demo(argv[2], argc - 3, argv + 3); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--fxdemo")) { fx_demo(argv[2]); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--chorddemo")) { chord_demo(argv[2]); return 0; }
#ifdef __APPLE__
    if (argc > 1 && !strcmp(argv[1], "--cpu")) { cpu(); return 0; }
#endif
    tour(argc > 1 ? argv[1] : "build/host");
    return fails ? 1 : 0;
}
