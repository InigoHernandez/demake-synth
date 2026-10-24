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
    tap(B_EDIT); tap(B_EDIT); shot(dir, "sound_engine");   /* the layer is not built: EDIT again stays */
    tap(B_SEL); for (int i = 0; i < 30; i++) dmk_encoder(E_K1, -1); for (int i = 0; i < 40; i++) dmk_encoder(E_K2, 1);
    shot(dir, "filter_turned");
    tap(B_HOME); dmk_encoder(E_SELECT, 1); shot(dir, "home_synth2");
    dmk_button(B_HOME, 1); wait_ms(500); shot(dir, "home_held"); dmk_button(B_HOME, 0); wait_ms(60);
    tap(B_OCTU); wait_ms(300); shot(dir, "notice");
    tap(B_SAVE); wait_ms(100); shot(dir, "saved");     /* SAVE: the song into its slot */
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
    tap(B_SEQ); dmk_encoder(E_SELECT, 1); dmk_encoder(E_SELECT, 1); shot(dir, "roll_synth3");   /* its own pattern */
    dmk_button(B_SEQ, 1); wait_ms(500); shot(dir, "roll_seq_held");                               /* SEQ: OCT- clears */
    dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); wait_ms(100); shot(dir, "roll_clear_armed"); dmk_button(B_SEQ, 0); wait_ms(2100);
    ta_reset(); boot(1650); shot_fr(dir, "boot_arrive", fb);
    ta_reset(); boot(2200); shot_fr(dir, "boot_heat", fb);
    for (int k = 0; k < 4; k++) { char nm[16]; boot_sc = (uint8_t)k; ta_reset(); boot(700); snprintf(nm, sizeof nm, "boot_land%d", k); shot_fr(dir, nm, fb); }
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
    /* a modifier that was used does nothing on release */
    tap(B_FX);
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
        int nch = page == PG_GLOBAL ? (row ? 2 : 6) : page == PG_FILTER ? 8 : row == 0 ? 5 : 4;
        ui.val[0][page][row][4] = (uint8_t)(type * 128 / nch + 128 / nch / 2);
        for (j = 0; j < 4; j++) if (k[j] != 255) ui.val[0][page][row][j] = k[j];
    }
    if (page != PG_GLOBAL || row) ui.val[0][PG_GLOBAL][0][4] = 31;   /* the hall under the others */
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
    for (t = 0; t < 6; t++) fx_one(dir, VN[t], t == 4 ? "Snow pad" : "Felt piano", PG_GLOBAL, 0, t, t == 1 || t == 2 || t == 3 ? VL : VB, 1, 0);
    fx_one(dir, "echo_grains", "Felt piano", PG_GLOBAL, 1, ET_GRAINS, GR, 1, 0);
    fx_one(dir, "colour_1_saturate", "Felt piano", PG_FX, 0, FC_SAT, SAT, 0, 0);
    fx_one(dir, "colour_2_crush", "Felt piano", PG_FX, 0, FC_CRUSH, CR, 0, 0);
    fx_one(dir, "colour_3_lossy", "Felt piano", PG_FX, 0, FC_LOSSY, LO, 0, 0);
    fx_one(dir, "colour_4_noise", "Felt piano", PG_FX, 0, FC_NOISE, NO, 0, 0);
    fx_one(dir, "media_1_tape", "Snow pad", PG_FX, 1, FM_TAPE, TA, 0, 0);
    fx_one(dir, "media_2_cassette", "Snow pad", PG_FX, 1, FM_CASS, CA, 0, 0);
    fx_one(dir, "media_3_vhs", "Snow pad", PG_FX, 1, FM_VHS, VH, 0, 0);
    fx_one(dir, "motion_1_chorus", "Glass pluck", PG_FX, 2, FO_CHORUS, CH, 0, 0);
    fx_one(dir, "motion_2_vibrato", "Felt piano", PG_FX, 2, FO_VIB, VI, 0, 0);
    fx_one(dir, "motion_3_drift", "Snow pad", PG_FX, 2, FO_DRIFT, DR, 0, 0);
    for (t = 0; t < 3; t++) fx_one(dir, FN[t], "Snow pad", PG_FILTER, 0, FT_NOTCH + t, FI, 0, 1);
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
    ui.val[0][PG_GLOBAL][0][4] = fx == 2 ? 74 : 0;      /* the heaviest: haze; else the room */
    ui.val[0][PG_GLOBAL][1][4] = fx == 2 ? 100 : 0;     /* and the grains */
    sp_sync();
    for (i = 0; i < DMK_VOICES; i++) note_on_s(i % DMK_SYNTHS, 48 + i * 3);
    for (i = 0; i < 100; i++) dmk_render(b, 128);
    a = instructions();
    for (i = 0; i < frames; i += 128) dmk_render(b, 128);
    z = instructions();
    return (double)(z - a) / frames;
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
    {   /* the heaviest: every synth lossy, VHS and chorus, the haze; the voices' share, which half of goes to CPU1 */
        double all = cpu_case(2);
        for (i = 0; i < DMK_VOICES; i++) note_on_s(i % DMK_SYNTHS, 48 + i * 3);
        v_frames = 128;
        a = instructions();
        for (i = 0; i < frames; i += 128) { voices_run(0); voices_run(1); }
        z = instructions();
        {
            double vo = (double)(z - a) / frames, ve;
            uint32_t rr = 1;
            a = instructions();
            for (i = 0; i < frames; i++) { rr = rr * 1664525u + 1013904223u; vq[vq_w++ & (VQ_LEN - 1)] = (int32_t)(rr >> 20) - 2048; if ((i & 127) == 127) { verb_job(); wq_r = wq_w; } }
            z = instructions();
            ve = (double)(z - a) / frames;
            printf("cpu: the heaviest (lossy, VHS, chorus x3, haze, echo) %.0f: its voices %.0f, the haze %.0f; on the FM-1 CPU0 %.0f, CPU1 %.0f\n",
                   all, vo, ve, all - vo / 2 - ve, vo / 2 + ve);
        }
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
