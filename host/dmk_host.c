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
#define DMK_VER "0.0"                                   /* (the FM-1's build: the release's; here, to see it placed) */
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

/* the FM-1 Firmware Hub's picture (docs/hub): the first song playing, a frame every 100 ms at 1x, as the screen
 * draws it: Home on synth 3's landscape (the owner: the picture is Home with its landscape), then its roll, then the
 * chord machine (tools/hub_art.py makes the GIFs and the 4x picture) */
static void anim(const char *dir)
{
    static uint8_t rgb[DMK_W * DMK_H * 3];
    static const struct { int b, n; } PART[3] = {{-1, 50}, {B_SEQ, 40}, {B_ARP, 40}};
    char path[512];
    int p, i, f = 0;
    host_store_on(0, 0);
    dmk_init(1);
    now_ms = 0;
    ui.hinted = 0xFFFFu;                                /* no first-time hints on the pictures */
    wait_ms(3700);                                      /* past the boot, on Home */
    dmk_encoder(E_SELECT, 1); wait_ms(200); dmk_encoder(E_SELECT, 1);   /* synth 3, Washingtonia: California's palms */
    wait_ms(1600);
    tap(B_PLAY);
    for (p = 0; p < 3; p++) {
        if (PART[p].b >= 0) tap(PART[p].b);
        for (i = 0; i < PART[p].n; i++) {
            wait_ms(100);
            screen_rgb(dmk_frame(now_ms), rgb);
            snprintf(path, sizeof path, "%s/f_%03d.png", dir, f++);
            png_rgb(path, rgb, DMK_W, DMK_H, 1);
        }
    }
    printf("anim: %d frames in %s\n", f, dir);
}
/* Home with each landscape (the first synth on each scene's first sound), playing, at 1x: the hub's picture picked
 * from these */
static void homes(const char *dir)
{
    static uint8_t rgb[DMK_W * DMK_H * 3];
    char path[512];
    int k, i;
    host_store_on(0, 0);
    dmk_init(1);
    now_ms = 0;
    ui.hinted = 0xFFFFu;
    wait_ms(3700);
    tap(B_PLAY);
    for (k = 0; k < DMK_SCENES; k++) {
        for (i = 0; i < DMK_SAMPLES_N; i++) if (DMK_SAMPLES[i].scene == k) { ui.val[0][PG_SOUND][0][0] = (uint8_t)i; break; }
        sp_sync();
        wait_ms(1600);
        screen_rgb(dmk_frame(now_ms), rgb);
        snprintf(path, sizeof path, "%s/home_%02d.png", dir, k);
        png_rgb(path, rgb, DMK_W, DMK_H, 1);
    }
    printf("homes: %d in %s\n", DMK_SCENES, dir);
}

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
    tap(B_SEQ); tap(B_SEQ); tap(B_SEQ);                 /* the roll's Arp row, Converge on */
    ui.val[ui.synth][PG_ROLL][2][0] = choice_val(AR_CONV, AR_N); sp_sync(); shot(dir, "roll_arp");
    for (int m = 0; m < AR_N; m++) for (int g = 0; g < 2; g++) {   /* every way, every rate's end, audited (not kept) */
        ui.val[ui.synth][PG_ROLL][2][0] = choice_val(m, AR_N); ui.val[ui.synth][PG_ROLL][2][1] = g ? 127 : 0;
        ui.val[ui.synth][PG_ROLL][2][3] = g ? 127 : 0; ui.val[ui.synth][PG_ROLL][2][4] = g ? 127 : 0; sp_sync(); shot(0, "arp");
    }
    arp_defaults(ui.synth); sp_sync(); tap(B_SEQ);
    tap(B_ARP); tap(B_ARP); shot(dir, "chord_key"); tap(B_ARP); tap(B_ARP);   /* the Key row: Mode's ticks, a click each */
    {                                                   /* every effect's knobs and values, at both ends, audited (not
                                                           kept as pictures): the labels change with the type */
        static const int NT[3] = {FX_COLOURS, 3, FX_MOTIONS};   /* (the delay's: off, delay, ping-pong) */
        uint8_t keep[3][5], row = ui.row[PG_FX];
        int r, t, k, e;
        memcpy(keep, ui.val[ui.synth][PG_FX], sizeof keep);
        for (r = 0; r < 3; r++)
            for (t = 1; t < NT[r]; t++)
                for (e = 0; e < 2; e++) {
                    char nm[24];
                    ui.row[PG_FX] = (uint8_t)r;
                    ui.val[ui.synth][PG_FX][r][4] = choice_val(t, NT[r]);
                    for (k = 0; k < 4; k++) ui.val[ui.synth][PG_FX][r][k] = (uint8_t)(e ? 127 : 0);
                    sp_sync();
                    snprintf(nm, sizeof nm, "fx_%d_%d_%s", r, t, e ? "max" : "min");
                    shot(0, nm);
                }
        memcpy(ui.val[ui.synth][PG_FX], keep, sizeof keep);
        ui.row[PG_FX] = 1;                              /* the delay, on: a ping-pong */
        ui.val[ui.synth][PG_FX][1][4] = choice_val(2, 3); ui.val[ui.synth][PG_FX][1][0] = 70; sp_sync();
        shot(dir, "fx_delay");
        memcpy(ui.val[ui.synth][PG_FX], keep, sizeof keep);
        ui.row[PG_FX] = row;
        sp_sync();
    }
    tap(B_GLO); tap(B_GLO); shot(dir, "global_echo"); tap(B_GLO); shot(dir, "global_media");
    {                                                   /* the mix's media: every type's knobs at both ends, audited */
        uint8_t keep[5];
        int t, k, e;
        memcpy(keep, ui.val[0][PG_GLOBAL][2], sizeof keep);
        for (t = 1; t < FX_MEDIAS; t++)
            for (e = 0; e < 2; e++) {
                char nm[24];
                ui.val[0][PG_GLOBAL][2][4] = choice_val(t, FX_MEDIAS);
                for (k = 0; k < 4; k++) ui.val[0][PG_GLOBAL][2][k] = (uint8_t)(e ? 127 : 0);
                sp_sync();
                snprintf(nm, sizeof nm, "media_%d_%s", t, e ? "max" : "min");
                shot(0, nm);
            }
        memcpy(ui.val[0][PG_GLOBAL][2], keep, sizeof keep);
        sp_sync();
    }
    tap(B_GLO); shot(dir, "global_setup"); tap(B_GLO);
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
    tap(B_SAVE); wait_ms(100); shot(dir, "save");        /* SAVE: Projects, the cursor on the one open (not saved yet) */
    tap(B_SAVE); wait_ms(100); shot(dir, "saved"); wait_ms(1500);   /* again: saved there, a name made up */
    dmk_encoder(E_K3, 1); wait_ms(60); shot(dir, "save_name"); wait_ms(1600);   /* its name's second word turned */
    shot(dir, "save_projects");                          /* (the manual's: saved, at rest) */
    for (int i = 0; i < 6; i++) dmk_encoder(E_K1, 1);
    wait_ms(60); shot(dir, "save_list");                 /* the cursor past the rows in view: the list moved */
    dmk_encoder(E_ALGO, 1); wait_ms(60); shot(dir, "save_sounds");   /* Sounds: the three tracks */
    {   /* every recording's name beside a kept sound's number, audited (not kept) */
        uint8_t r0 = ui.val[0][PG_SOUND][0][0], p0 = ui.preset[0], l0 = lib[0][0];
        lib[0][0] = 1;
        for (int r = 0; r < DMK_SAMPLES_N; r++) { ui.val[0][PG_SOUND][0][0] = (uint8_t)r; ui.preset[0] = DMK_SAMPLES_N; shot(0, "snd"); }
        lib[0][0] = l0; ui.val[0][PG_SOUND][0][0] = r0; ui.preset[0] = p0; sp_sync();
    }
    dmk_encoder(E_ALGO, 1);
    {   /* every name a project can have, on its row and across the fields, audited (not kept) */
        save_pick(0);
        for (int a = 0; a < 24; a++) for (int b2 = 0; b2 < 24; b2++) {
            ui.ren_slot = 0; ui.ren_v[0] = (uint8_t)a; ui.ren_v[1] = (uint8_t)b2; ui.ren_v[2] = NM_MARK; ui.ren_at = now_ms;
            shot(0, "name");
        }
        ui.ren_slot = 0xFF;
    }
    tap(B_HOME);
    {   /* every choice of every field, on its own page, audited (not kept): no name cut or shortened (the owner) */
        uint8_t pg0 = ui.page, rw[PG_COUNT];
        for (int p = 0; p < PG_COUNT; p++) rw[p] = ui.row[p];
        for (unsigned c = 0; c < sizeof CHOICE / sizeof CHOICE[0]; c++) {
            const choice_t *ch_ = &CHOICE[c];
            uint8_t *v = vp(ch_->page, ch_->row, ch_->k), was = *v, ty = 0, *tv = 0;
            if (ch_->page == PG_GLOBAL && ch_->row == 3 && ch_->k == 3) continue;   /* (the themes: their own screens) */
            if (ch_->page == PG_FX && ch_->row < 3 && ch_->k != 4) { tv = vp(PG_FX, ch_->row, 4); ty = *tv; *tv = choice_val(1, choice_of(PG_FX, ch_->row, 4)->n); }
            ui.page = ch_->page; ui.row[ch_->page] = ch_->row;
            for (int i = 0; i < ch_->n; i++) {
                int b0 = ta_bad;
                *v = choice_val(i, ch_->n); sp_sync(); shot(0, "choice");
                if (ta_bad != b0) printf("  (page %d row %d K%d: %s)\n", ch_->page, ch_->row, ch_->k + 1, ch_->name[i]);
            }
            *v = was; if (tv) *tv = ty; sp_sync();
        }
        ui.page = PG_ARR; ui.row[PG_ARR] = 0;           /* the Song page's Scene row: every progression, every sound */
        { uint8_t was = ui.val[0][PG_CHORD][0][4];
          for (int i = 0; i < PROG_N; i++) { ui.val[0][PG_CHORD][0][4] = choice_val(i, PROG_N); sp_sync(); shot(0, "choice"); }
          ui.val[0][PG_CHORD][0][4] = was; }
        for (int q = 0; q < DMK_SYNTHS; q++) { uint8_t was = ui.val[q][PG_SOUND][0][0];
          for (int i = 0; i < DMK_SAMPLES_N; i++) { ui.val[q][PG_SOUND][0][0] = (uint8_t)i; sp_sync(); shot(0, "choice"); }
          ui.val[q][PG_SOUND][0][0] = was; }
        sp_sync();
        ui.page = pg0; for (int p = 0; p < PG_COUNT; p++) ui.row[p] = rw[p];
    }
    tap(B_GLO); tap(B_GLO); tap(B_GLO); tap(B_GLO);     /* to SETUP: its K4 the theme */
    for (int t = 1; t < DMK_THEMES; t++) {
        dmk_encoder(E_K4, 1);
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
    {   /* recorded as played: a strummed chord, late in its steps; a held note's NUDGE; REC and PLAY: ready */
        static const uint8_t CH[4] = {57, 60, 64, 67};
        if (seq_on) tap(B_PLAY);
        ui.val[0][PG_CHORD][2][0] = choice_val(0, 2); sp_sync();   /* the keys: notes on the roll */
        ui.synth = 1; sq[1].count = 0; ui.roll_cur[1] = 0;
        for (int i = 0; i < 4; i++) { int j = seq_add(1, i * 15 / 32, i < 3 ? 6 : 5, CH[i], 100); sq[1].n[j].off = (uint8_t)(i * 15 % 32); }
        roll_show(1, 67); roll_show(1, 57); wait_ms(60); shot(dir, "roll_strum");
        ui.octave = 0; dmk_key(64 - 53, 1); wait_ms(60); if (roll_held() < 0) { fails++; printf("tour: no note held for NUDGE\n"); } shot(dir, "roll_nudge"); dmk_key(64 - 53, 0); wait_ms(30);
        tap(B_REC); tap(B_PLAY); wait_ms(60); shot(dir, "roll_ready"); tap(B_REC);
        sq[1].count = 0; ui.roll_cur[1] = 0;            /* a chromatic run: the notes off the scale between its rows */
        for (int i = 0; i < 9; i++) seq_add(1, i * 2, 2, 57 + i, 100);
        ui.roll_view[1] = (uint8_t)degree_of(57); wait_ms(60); shot(dir, "roll_chromatic");
        sq[1].steps = 256; ui.roll_zoom[1] = 2; ui.roll_cur[1] = 200;   /* sixteen bars, the fourth page of 64 */
        for (int i = 0; i < 6; i++) seq_add(1, 194 + i * 8, 6, CH[i % 4] + (i / 4) * 12, 100);
        wait_ms(60); shot(dir, "roll_256");
        sq[1].steps = 16; ui.roll_zoom[1] = 0; ui.roll_cur[1] = 0;
    }
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
    /* SELECT scrolls the rows (the owner: the most intuitive), round past the ends (the owner: a list goes round);
     * ALGORITHM is the row's type; HOME held + SELECT is the synth, on any page */
    { int s0 = ui.synth;
    wait_ms(150); dmk_encoder(E_SELECT, 1); if (ui.row[PG_FX] != 3 || ui.synth != s0) { fails++; printf("selftest: SELECT does not scroll FX's rows\n"); }
    wait_ms(150); dmk_encoder(E_SELECT, 1); if (ui.row[PG_FX] != 0) { fails++; printf("selftest: SELECT past the last row is not the first\n"); }
    wait_ms(150); dmk_encoder(E_SELECT, -1); if (ui.row[PG_FX] != 3) { fails++; printf("selftest: SELECT back past the first row is not the last\n"); }
    wait_ms(150); dmk_encoder(E_SELECT, 1);
    if (ui.row[PG_FX] != 0) { fails++; printf("selftest: SELECT does not reach the first row\n"); }
    dmk_encoder(E_ALGO, 1); wait_ms(30);
    if (syn[ui.synth].colour != FC_SAT || ui.row[PG_FX] != 0) { fails++; printf("selftest: ALGORITHM does not pick the effect\n"); }
    dmk_encoder(E_ALGO, -1); wait_ms(150);              /* (past SELECT's 120 ms: a click, not the last one's twin) */
    dmk_button(B_HOME, 1); wait_ms(30); dmk_encoder(E_SELECT, 1); dmk_button(B_HOME, 0); wait_ms(30);
    if (ui.synth != (s0 + 1) % DMK_SYNTHS || ui.page != PG_FX) { fails++; printf("selftest: HOME + SELECT does not pick the synth on FX\n"); }
    wait_ms(150); dmk_button(B_HOME, 1); wait_ms(30); dmk_encoder(E_SELECT, -1); dmk_button(B_HOME, 0); wait_ms(30);
    tap(B_FX); tap(B_HOME); tap(B_FX);                  /* (back on FX, its second row: as the rolls' check starts) */
    while (ui.row[PG_FX] != 1) { wait_ms(150); dmk_encoder(E_SELECT, ui.row[PG_FX] > 1 ? -1 : 1); }
    if (ui.synth != s0) { fails++; printf("selftest: SELECT on FX moved the synth\n"); } }
    /* the rolls: every synth its own; stopped, a key only plays until REC is lit (the owner: it recorded without REC),
     * then writes at the cursor, OCT- takes a held note away, PLAY sounds them */
    {
        int n0 = sq[0].count, n1 = sq[1].count, n2 = sq[2].count;
        if (sq[0].steps == sq[1].steps && sq[1].steps == sq[2].steps) { fails++; printf("selftest: the rolls share a length\n"); }
        tap(B_SEQ); dmk_encoder(E_SELECT, 1);
        ui.roll_cur[1] = 1;
        dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
        if (sq[1].count != n1) { fails++; printf("selftest: a key on a stopped roll writes without REC\n"); }
        tap(B_REC);
        dmk_key(4, 1); wait_ms(30);
        if (sq[1].count != n1 + 1 || sq[0].count != n0 || sq[2].count != n2) { fails++; printf("selftest: a key does not write to its own roll\n"); }
        dmk_button(B_OCTD, 1); dmk_button(B_OCTD, 0); dmk_key(4, 0); wait_ms(30);
        if (sq[1].count != n1) { fails++; printf("selftest: OCT- does not take the held note away\n"); }
        if (ui.octave != 0) { fails++; printf("selftest: OCT- with a note held moved the octave\n"); }
        dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
        if (ui.roll_cur[1] != 1 + ui.roll_pen[1]) { fails++; printf("selftest: a key released does not move the cursor on\n"); }
        seq_del(1, seq_find(1, 1, snap(53 + 4)));
        {                                               /* a note written above the view: the view goes up to it */
            int v0 = ui.roll_view[1] = 20, c0 = sq[1].count;
            ui.roll_cur[1] = 3; dmk_key(24, 1); wait_ms(30); dmk_key(24, 0); wait_ms(30);
            if (sq[1].count != c0 + 1 || ui.roll_view[1] == v0 || degree_of(snap(53 + 24)) > ui.roll_view[1] + 6)
                { fails++; printf("selftest: a note written above the roll's view is not shown\n"); }
            seq_del(1, seq_find(1, 3, snap(53 + 24)));
        }
        tap(B_REC);
        tap(B_PLAY);
        for (int k = 0; k < 10; k++) dmk_render(b, 512);
        { int on = 0; for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on; if (!dmk_playing() || !on) { fails++; printf("selftest: PLAY sounds nothing\n"); } }
        tap(B_PLAY);
        if (dmk_playing()) { fails++; printf("selftest: PLAY again does not stop\n"); }
        dmk_encoder(E_SELECT, -1);
        tap(B_FX);                                      /* back where the next check starts */
    }
    {   /* SELECT: a synth a click; a second step the same way at once is the same click (the owner's FM-1: 1, then 3) */
        int s0;
        tap(B_HOME);                                    /* (on Home SELECT is the synth; on FX, the rows) */
        s0 = ui.synth;
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
    /* the store, SAVE's pages (the owner): SAVE opens Projects on the project open; SAVE there saves into an empty slot
     * (a name made up) or over the open one, not over another; a start opens the newest; PLAY twice opens one, on an
     * empty slot a new empty project; OCT- twice deletes; the list moves only past the rows in view; Sounds: SAVE
     * keeps the track's sound, OCT- twice is INIT, K3 brings the kept one back; EDIT + SAVE, PRESETS as before */
    {
        int notes, cut;
        host_store_on(1, 1);
        dmk_init(9); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        seq_add(0, 5, 3, 62, 90); notes = sq[0].count;
        ui.val[0][PG_FILTER][0][0] = 33; cut = 33; sp_sync();
        tap(B_SAVE);
        if (ui.page != PG_DISCS || ui.row[PG_DISCS] || ui.song_pick != ui.song || song_used(0)) { fails++; printf("selftest: SAVE does not open Projects on the project open\n"); }
        tap(B_SAVE);
        if (strncmp(ui.note, "Saved: ", 7) || !song_used(0) || !strchr(song_name(0), ' ') || ui.page != PG_DISCS) { fails++; printf("selftest: SAVE there does not save it, named (%s)\n", ui.note); }
        seq_play(1); tap(B_SAVE); seq_play(0);
        if (strcmp(ui.note, "Stop to save")) { fails++; printf("selftest: SAVE while playing is not refused\n"); }
        dmk_init(10); now_ms = 0; wait_ms(60);
        if (sq[0].count != notes || ui.val[0][PG_FILTER][0][0] != cut || seq_find(0, 5, 62) < 0) { fails++; printf("selftest: a start does not open the saved project\n"); }
        ui.boot_done = 1;
        ui.val[1][PG_AMP][0][0] = 5; sq[2].count = 0;   /* changed: a second project, in slot 2 */
        tap(B_SAVE); dmk_encoder(E_K1, 1); tap(B_SAVE);
        if (!song_used(1) || ui.song != 1 || !strcmp(song_name(0), song_name(1))) { fails++; printf("selftest: SAVE into an empty slot does not make a new project\n"); }
        dmk_encoder(E_K1, -1); tap(B_SAVE);
        if (strcmp(ui.note, "Pick an empty slot")) { fails++; printf("selftest: SAVE over another project is not refused (%s)\n", ui.note); }
        tap(B_PLAY);
        if (ui.page != PG_DISCS || strcmp(ui.note, "PLAY again: open it")) { fails++; printf("selftest: one PLAY on Projects opens without asking\n"); }
        tap(B_PLAY);
        if (ui.page != PG_HOME || ui.val[1][PG_AMP][0][0] == 5 || sq[0].count != notes || ui.song != 0) { fails++; printf("selftest: PLAY twice on Projects does not open it\n"); }
        tap(B_SAVE); seq_play(1); tap(B_PLAY);         /* playing: PLAY there stops, as anywhere (the owner lost a song) */
        if (seq_on || ui.page != PG_DISCS || sq[0].count != notes) { fails++; printf("selftest: PLAY on Projects while playing does not stop\n"); }
        seq_add(0, 9, 1, 64, 100); dmk_encoder(E_K1, 1); tap(B_PLAY);   /* a change not saved: said first */
        if (strcmp(ui.note, "Unsaved: PLAY again to lose it")) { fails++; printf("selftest: opening over changes not saved does not say so (%s)\n", ui.note); }
        ui.clear_arm = 0; seq_del(0, seq_find(0, 9, 64)); dmk_encoder(E_K1, -1);
        dmk_encoder(E_K1, 1);
        tap(B_OCTD); tap(B_OCTD);
        if (song_used(1)) { fails++; printf("selftest: OCT- twice on Projects does not delete\n"); }
        tap(B_PLAY); tap(B_PLAY);                       /* an empty slot: a new, empty project */
        if (sq[0].count || sq[1].count || sq[2].count || ch.follow[0] != FW_FREE || ui.song != 1 || ui.val[0][PG_DISCS][1][2] == NM_MARK)
            { fails++; printf("selftest: PLAY twice on an empty slot is not an empty project\n"); }
        {                                               /* and with no effect (the owner): none on the synths, none on the mix */
            int fx = 0;
            for (int q = 0; q < DMK_SYNTHS; q++) fx += ui.val[q][PG_FX][0][4] + ui.val[q][PG_FX][1][4] + ui.val[q][PG_FX][2][4] + ui.val[q][PG_FX][3][0]
                                                     + ui.val[q][PG_FX][3][1] + ui.val[q][PG_LFO][0][1] + ui.val[q][PG_SOUND][1][0];
            fx += ui.val[0][PG_GLOBAL][0][4] + ui.val[0][PG_GLOBAL][1][4] + ui.val[0][PG_GLOBAL][2][4];
            if (fx) { fails++; printf("selftest: a new project has effects on\n"); }
            ui.val[2][PG_FX][2][4] = choice_val(FO_CHORUS, FX_MOTIONS); ui.val[2][PG_FX][3][0] = 90; sp_sync();
            sound_init(2);
            if (ui.val[2][PG_FX][2][4] || ui.val[2][PG_FX][3][0]) { fails++; printf("selftest: INIT leaves an effect on\n"); }
        }
        {                                               /* the list: still from 01 till the cursor passes 04, then by one */
            tap(B_SAVE); save_pick(0);
            for (int i = 0; i < 3; i++) dmk_encoder(E_K1, 1);
            if (ui.list_top != 0) { fails++; printf("selftest: the list moved before the cursor passed its rows\n"); }
            dmk_encoder(E_K1, 1);
            if (ui.list_top != 1 || ui.song_pick != 4) { fails++; printf("selftest: the list does not follow the cursor past its rows\n"); }
            for (int i = 0; i < 20; i++) dmk_encoder(E_K1, 1);
            if (ui.song_pick != ST_SONGS - 1 || ui.list_top != ST_SONGS - SAVE_ROWS) { fails++; printf("selftest: the list's end\n"); }
            for (int i = 0; i < 3; i++) dmk_encoder(E_K1, -1);
            if (ui.list_top != ST_SONGS - SAVE_ROWS) { fails++; printf("selftest: the list moved back before the cursor passed its rows\n"); }
        }
        {                                               /* a project's name changed: written once the knob rests */
            char was[24];
            save_pick(0); strcpy(was, song_name(0));
            dmk_encoder(E_K2, 1); dmk_encoder(E_K3, 1);
            if (strcmp(was, song_name(0))) { fails++; printf("selftest: a name is written while it is turned\n"); }
            wait_ms(1200);
            if (!strcmp(was, song_name(0))) { fails++; printf("selftest: a turned name is not written\n"); }
            strcpy(was, song_name(0)); songs_scan();
            if (strcmp(was, song_name(0)) || song_load(0)) { fails++; printf("selftest: a renamed project does not come back as it was\n"); }
            ui.song = 1;
        }
        dmk_encoder(E_ALGO, 1);                         /* Sounds */
        if (ui.row[PG_DISCS] != 1) { fails++; printf("selftest: ALGO does not turn Save to Sounds\n"); }
        ui.synth = 0; dmk_encoder(E_K1, 1);
        ui.val[1][PG_FILTER][0][1] = 77; sp_sync();
        tap(B_SAVE);
        if (ui.synth != 1 || lib_count() != 1 || strncmp(ui.note, "Kept as User", 12)) { fails++; printf("selftest: SAVE on Sounds does not keep the track's sound (%s)\n", ui.note); }
        tap(B_OCTD); tap(B_OCTD);
        if (ui.val[1][PG_FILTER][0][1] == 77) { fails++; printf("selftest: OCT- twice on Sounds is not INIT\n"); }
        dmk_encoder(E_K3, 1);
        if (ui.val[1][PG_FILTER][0][1] != 77) { fails++; printf("selftest: K3 on Sounds does not bring the kept sound back\n"); }
        { int r0 = ui.val[1][PG_SOUND][0][0]; dmk_encoder(E_K2, 1);
          if (ui.val[1][PG_SOUND][0][0] != (r0 + 1) % DMK_SAMPLES_N) { fails++; printf("selftest: K2 on Sounds is not the next recording\n"); } }
        tap(B_HOME);
        while (lib_count()) { lib[lib_nth(0)][0] = 0; }   /* (the library as it was for what follows) */
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
        tap(B_SAVE); tap(B_SAVE); dmk_init(13); now_ms = 0; wait_ms(60);
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
        tap(B_SAVE); tap(B_SAVE); dmk_init(16); now_ms = 0; wait_ms(60);
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
        ui.val[0][PG_GLOBAL][3][1] = choice_val(3, 4); sp_sync();   /* out: notes and the clock */
        seq_play(1); wait_ms(16 * 60000 / 72 / 4 / 16 * 4);   /* four steps */
        seq_play(0);
        while (dmk_midi_take(&st, &a, &b)) { clocks += st == 0xF8; start += st == 0xFA; }
        if (!start || clocks < 20 || clocks > 28) { fails++; printf("selftest: the clock out is not 6 a step (%d in 4 steps)\n", clocks); }
        ui.val[0][PG_GLOBAL][3][0] = choice_val(1, 2); sp_sync();   /* in: by channel */
        dmk_midi(0x92, 64, 100); wait_ms(30);
        for (int v = 0; v < DMK_VOICES; v++) s1 += vc[v].on && vc[v].sy == 2;
        dmk_midi(0x82, 64, 0); wait_ms(30);
        if (!s1) { fails++; printf("selftest: MIDI channel 3 does not play synth 3\n"); }
        ui.val[0][PG_GLOBAL][3][2] = choice_val(1, 2); sp_sync();   /* the clock outside */
        dmk_midi(0xFA, 0, 0);
        if (!seq_on) { fails++; printf("selftest: an outside start does not play\n"); }
        { uint32_t t0 = seq_ticks, t1;                  /* none coming: its own tempo (it froze, its notes held) */
          wait_ms(500); if (seq_ticks == t0) { fails++; printf("selftest: the clock set outside but none coming, PLAY does not run\n"); }
          t1 = seq_ticks;                               /* then the clock: it steps them */
          for (int k = 0; k < 12; k++) { dmk_midi(0xF8, 0, 0); wait_ms(20); }
          if (seq_ticks != t1 + 2) { fails++; printf("selftest: 12 clocks are not 2 steps (%u)\n", (unsigned)(seq_ticks - t1)); } }
        dmk_midi(0xFC, 0, 0);
        if (seq_on) { fails++; printf("selftest: an outside stop does not stop\n"); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* a song of 0.2 (the MIDI row not there yet, its values 64) loads with the MIDI defaults, not an outside clock;
     * REC armed lights REC */
    {
        host_store_on(1, 1);
        dmk_init(28); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_GLOBAL][2][0] = ui.val[0][PG_GLOBAL][2][1] = ui.val[0][PG_GLOBAL][2][2] = 64;   /* (its row then) */
        for (int q = 0; q < DMK_SYNTHS; q++) sq[q].count = 0;   /* (no notes: a note's bytes are 11's) */
        tap(B_SAVE); tap(B_SAVE);
        for (int k = 0; k < ST_SONGS; k++) if (host_store[0x2000 + k * 4096] == 'D') host_store[0x2000 + k * 4096 + 12] = 6;
        dmk_init(29); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        if (clock_ext || midi_chans || midi_out != 1) { fails++; printf("selftest: an old song's MIDI row loads as set (clock %d, in %d, out %d)\n", clock_ext, midi_chans, midi_out); }
        host_store_on(0, 0);
        tap(B_REC);
        if (!dmk_leds()[DMK_KEYS + B_REC]) { fails++; printf("selftest: REC armed is not lit\n"); }
        tap(B_REC);
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
          tap(B_REC); ui.roll_cur[1] = (uint8_t)at; dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
          if (sq[1].count != n0 + 3) { fails++; printf("selftest: on the roll a chord key does not write its triad (%d)\n", sq[1].count - n0); }
          if (ui.roll_cur[1] != at + ui.roll_pen[1]) { fails++; printf("selftest: the roll's cursor does not move past the chord\n"); }
          for (i = 0; i < sq[1].count; i++) if (sq[1].n[i].step == at) nt[c++] = sq[1].n[i].note;
          if (c != 3 || !chord_of(nt, c, cn)) { fails++; printf("selftest: the chord written is not named (%d notes)\n", c); }
          ui.synth = 0; ui.roll_cur[0] = 7; dmk_key(7, 1); wait_ms(30); dmk_key(7, 0); wait_ms(30);   /* the pad played the chords */
          if (ch.follow[0] != FW_FREE) { fails++; printf("selftest: writing to a chord player's roll does not let it play its roll\n"); }
          tap(B_REC); }
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
        ui.val[0][PG_GLOBAL][0][4] = choice_val(0, FX_VERBS); ui.val[0][PG_GLOBAL][1][4] = choice_val(0, FX_ECHOS); sp_sync();
        note_on_s(0, 60);
        for (i = 0; i < 40; i++) dmk_render(ob, 512);
        if (v_cur != VB_OFF || e_cur != ET_OFF) { fails++; printf("selftest: the verb and the echo do not go off (%d %d)\n", v_cur, e_cur); }
        for (v = 0; v < DMK_VOICES; v++) { vc[v].stage = 0; vc[v].on = 0; }
        for (i = 0; i < 20; i++) dmk_render(ob, 512);
        for (v = 0; v < 1024; v++) { int a = ob[v] < 0 ? -ob[v] : ob[v]; if (a > pk) pk = a; }
        if (pk > 64) { fails++; printf("selftest: with the spaces off a note leaves a tail (%d)\n", pk); }
        ui.val[0][PG_GLOBAL][0][4] = choice_val(2, FX_VERBS); ui.val[0][PG_GLOBAL][1][4] = choice_val(1, FX_ECHOS); sp_sync();
        for (i = 0; i < 4; i++) dmk_render(ob, 512);
        if (v_cur != VB_HALL || e_cur != ET_TAPE) { fails++; printf("selftest: the verb and the echo do not come back\n"); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* the chord keys: a lower key never a higher chord, in every voicing (the owner heard B under C go up) */
    {
        int vo, k, lo0 = 0, oc;
        for (oc = -2; oc <= 2; oc++)                    /* at every octave, so OCT- is a lower keyboard too */
            for (vo = 0; vo < 5; vo++) {
                ui.val[0][PG_CHORD][0][0] = choice_val(vo, 5); sp_sync();
                for (k = 0; k < DMK_KEYS; k++) {
                    uint8_t nt[8];
                    int c = chord_key(53 + k + oc * 12, oc, nt), lo = 127, i;
                    for (i = 0; i < c; i++) if (nt[i] < lo) lo = nt[i];
                    if (k && lo < lo0) { fails++; printf("selftest: OCT %d, voicing %d, key %d's chord starts under key %d's (%d < %d)\n", oc, vo, k, k - 1, lo, lo0); break; }
                    lo0 = lo;
                }
            }
        {   /* and OCT- lowers them (the bottom octave's chords, at OCT 0 and -1) */
            uint8_t a[8], b[8];
            ui.val[0][PG_CHORD][0][0] = 2 * 128 / 5 + 12; sp_sync();
            chord_key(64, 0, a); chord_key(64 - 12, -1, b);
            if (b[1] >= a[1]) { fails++; printf("selftest: OCT- does not lower a chord key (%d, %d)\n", a[1], b[1]); }
        }
        ui.val[0][PG_CHORD][0][0] = 2 * 128 / 5 + 12; sp_sync();
    }
    /* the full screen comes half a second after both are held, even when the second press did something else; a
     * harmony follower's roll written to is heard; a key held into Arrange lets go */
    {
        int on = 0, v;
        dmk_init(26); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_SEQ); tap(B_REC); ui.roll_cur[0] = 2;
        dmk_key(4, 1); wait_ms(30); dmk_button(B_OCTU, 1); wait_ms(30); dmk_button(B_OCTD, 1); wait_ms(40);
        if (ui.full) { fails++; printf("selftest: the full screen came at once (the second OCT deleted a held note)\n"); }
        dmk_button(B_OCTD, 0); dmk_button(B_OCTU, 0); dmk_key(4, 0); wait_ms(30);
        ui.synth = 1; ch.follow[1] = FW_HARM1; ui.roll_cur[1] = 9;
        dmk_key(6, 1); wait_ms(30); dmk_key(6, 0); wait_ms(30);
        if (ch.follow[1] != FW_FREE) { fails++; printf("selftest: a harmony follower's roll written to is not heard\n"); }
        tap(B_REC);
        tap(B_HOME);
        dmk_key(9, 1); wait_ms(30);
        dmk_button(B_HOME, 1); wait_ms(30); dmk_button(B_SEQ, 1); dmk_button(B_SEQ, 0); dmk_button(B_HOME, 0); wait_ms(30);
        dmk_key(9, 0); wait_ms(200);
        for (v = 0; v < DMK_VOICES; v++) on += vc[v].on;
        if (on || ui.keys_down[9]) { fails++; printf("selftest: a key held into Arrange keeps sounding (%d)\n", on); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* from the review: a low MIDI note with a synth in harmony (the scale's place walked off its table); a MIDI note
     * ends on its own synth after SELECT; letting one chord go keeps the other's shared notes; the outside clock
     * stops at the song's end */
    {
        int on = 0, v;
        dmk_init(27); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ch.follow[1] = FW_HARM1;
        dmk_midi(0x90, 3, 100); dmk_midi(0x80, 3, 0); wait_ms(30);   /* (it crashed) */
        ch.follow[1] = FW_KEY;
        dmk_midi(0x90, 60, 100); dmk_encoder(E_SELECT, 1); wait_ms(200); dmk_midi(0x80, 60, 0); wait_ms(30);
        for (v = 0; v < DMK_VOICES; v++) on += vc[v].on;
        if (on) { fails++; printf("selftest: a MIDI note held over SELECT does not end (%d)\n", on); }
        dmk_encoder(E_SELECT, -1); wait_ms(200);
        ui.val[0][PG_CHORD][0][0] = choice_val(0, 5); sp_sync();   /* close: Am7 and Cmaj7 share three notes */
        tap(B_ARP); dmk_key(4, 1); wait_ms(30); dmk_key(7, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30);
        on = 0; for (v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == ui.synth;
        if (on != ui.key_nch[7]) {
            fails++; printf("selftest: one chord let go cut the other's notes (%d of %d; synth %d page %d)\n", on, ui.key_nch[7], ui.synth, ui.page);
            for (v = 0; v < DMK_VOICES; v++) if (vc[v].stage) printf("  v%d sy%d n%d src%d on%d st%d\n", v, vc[v].sy, vc[v].note, vc[v].src, vc[v].on, vc[v].stage);
        }
        dmk_key(7, 0); wait_ms(30);
        ui.val[0][PG_GLOBAL][3][2] = choice_val(1, 2); sp_sync();   /* the outside clock; a one-step song that stops */
        song_n = 0; song_put(0, 0, 1); song_n = 1; song_end = SE_STOP; song_mode = 1;
        dmk_midi(0xFA, 0, 0);
        for (v = 0; v < 24 * 4 * 2 && seq_on; v++) { dmk_midi(0xF8, 0, 0); wait_ms(9); }
        if (seq_on) { fails++; printf("selftest: the outside clock plays on past the song's end\n"); }
        dmk_midi(0xFC, 0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    /* PRESET on a page of types turns the row's type, not the sound */
    {
        int smp = ui.val[ui.synth][PG_SOUND][0][0];
        tap(B_FX); while (ui.row[PG_FX] != 0) { wait_ms(150); dmk_encoder(E_SELECT, -1); }
        dmk_encoder(E_PRESET, 1); dmk_encoder(E_PRESET, 1);
        if (syn[ui.synth].colour != FC_CRUSH || ui.val[ui.synth][PG_SOUND][0][0] != smp) { fails++; printf("selftest: PRESET on FX does not turn the effect (%d)\n", syn[ui.synth].colour); }
        dmk_encoder(E_PRESET, -1); dmk_encoder(E_PRESET, -1); dmk_encoder(E_PRESET, -1);
        if (ui.val[ui.synth][PG_FX][0][4] != choice_val(FX_COLOURS - 1, FX_COLOURS)) { fails++; printf("selftest: PRESET on FX back past Off is not the last type\n"); }
        dmk_encoder(E_PRESET, 1);
        if (ui.val[ui.synth][PG_FX][0][4] != choice_val(FC_OFF, FX_COLOURS)) { fails++; printf("selftest: PRESET on FX past the last type is not Off\n"); }
        for (int i = 0; i < 140; i++) { dmk_encoder(E_K1, 1); dmk_encoder(E_K2, -1); }   /* a number stops at its ends */
        if (ui.val[ui.synth][PG_FX][0][0] != 127 || ui.val[ui.synth][PG_FX][0][1] != 0) { fails++; printf("selftest: a number went round\n"); }
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
        for (k = 0; k < DMK_SYNTHS; k++) { ui.val[k][PG_FX][3][0] = 0; ui.val[k][PG_FX][3][1] = 0; }
        ui.val[0][PG_GLOBAL][2][4] = 0;                 /* (no tape on the mix) */
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
        for (k = 0; k < DMK_SYNTHS; k++) sq[k].count = 0;   /* (no notes: a note's bytes are 11's) */
        tap(B_SAVE); tap(B_SAVE); dmk_init(22); now_ms = 0; wait_ms(60);
        if (ui.val[0][PG_LFO][0][1] != 64 || ui.val[0][PG_SOUND][1][0] != 50 || ui.val[0][PG_SOUND][2][0] != 33 || ui.val[0][PG_FILTER][0][3] != 20)
            { fails++; printf("selftest: a song does not keep the layer, the voice, the drive or the LFO\n"); }
        for (k = 0; k < ST_SONGS; k++) if (host_store[0x2000 + k * 4096] == 'D') host_store[0x2000 + k * 4096 + 12] = 4;   /* as 0.3 wrote it */
        dmk_init(23); now_ms = 0; wait_ms(60);
        if (ui.val[0][PG_LFO][0][1] || ui.val[0][PG_SOUND][1][0] || ui.val[0][PG_SOUND][2][0] || ui.val[0][PG_FILTER][0][3] || !song_used(0))
            { fails++; printf("selftest: a song of 0.3 loads with its new controls on\n"); }
        sound_put(0, &lib[0][2]); lib[0][0] = 1; lib[0][1] = 0; lib[0][2 + 8 * 5 + 1] = 64;   /* a sound of 0.3, its LFO's depth 64 */
        lib_load(0, 0);
        if (ui.val[0][PG_LFO][0][1]) { fails++; printf("selftest: a sound of 0.3 loads with its LFO on\n"); }
        /* a song and a sound of before 0.5 (fewer types a row: 5, 4, 4) keep their effects; one of now keeps the later */
        ui.val[0][PG_FX][0][4] = choice_val(FC_LOSSY, 5); ui.val[0][PG_FX][1][4] = choice_val(FM_VHS, 4);
        ui.val[0][PG_FX][2][4] = choice_val(FO_CHORUS, 4); ui.val[1][PG_FX][1][4] = 0;
        for (k = 0; k < DMK_SYNTHS; k++) sq[k].count = 0;   /* (no notes: a note's bytes are 11's) */
        tap(B_SAVE); tap(B_SAVE);
        for (k = 0; k < ST_SONGS; k++) if (host_store[0x2000 + k * 4096] == 'D') host_store[0x2000 + k * 4096 + 12] = 5;   /* as 0.4 wrote it */
        dmk_init(24); now_ms = 0; wait_ms(60);
        if (syn[0].colour != FC_LOSSY || gmed.media != FM_VHS || syn[0].motion != FO_CHORUS || syn[0].dtype || syn[1].dtype)
            { fails++; printf("selftest: a song of 0.4 loads with other effects (%d %d %d, %d)\n", syn[0].colour, gmed.media, syn[0].motion, syn[0].dtype); }
        ui.val[0][PG_FX][0][4] = choice_val(FC_DIST, FX_COLOURS); ui.val[0][PG_GLOBAL][2][4] = choice_val(FM_DICTA, FX_MEDIAS);
        ui.val[0][PG_FX][2][4] = choice_val(FO_TREM, FX_MOTIONS); ui.val[1][PG_FX][1][4] = choice_val(2, 3); sp_sync();
        tap(B_SAVE); tap(B_SAVE); dmk_init(25); now_ms = 0; wait_ms(60);
        if (syn[0].colour != FC_DIST || gmed.media != FM_DICTA || syn[0].motion != FO_TREM || syn[1].dtype != 2 || syn[0].dtype)
            { fails++; printf("selftest: a song does not keep the later effects, the media or the delays\n"); }
        sound_put(0, &lib[0][2]); lib[0][0] = 1; lib[0][1] = 1;   /* a sound of 0.4: its types where 0.4 put them */
        lib[0][2 + 4 * 5 + 4] = choice_val(FC_NOISE, 5); lib[0][2 + 5 * 5 + 4] = choice_val(FM_CASS, 4); lib[0][2 + 6 * 5 + 4] = choice_val(FO_DRIFT, 4);
        lib_load(0, 0);
        if (syn[0].colour != FC_NOISE || syn[0].dtype || syn[0].motion != FO_DRIFT || gmed.media != FM_DICTA)
            { fails++; printf("selftest: a sound of 0.4 loads with other effects, a delay, or the mix's media changed\n"); }
        ui.val[0][PG_FX][2][4] = choice_val(FO_FLANGER, FX_MOTIONS); ui.val[0][PG_FX][1][4] = choice_val(1, 3); sp_sync();
        k = sound_save(0);
        if (k < 1) { fails++; printf("selftest: a sound not saved\n"); }
        else { ui.val[0][PG_FX][2][4] = 0; ui.val[0][PG_FX][1][4] = 0; sp_sync(); lib_load(0, k - 1);
               if (syn[0].motion != FO_FLANGER || syn[0].dtype != 1) { fails++; printf("selftest: a sound does not keep the later effects or its delay\n"); } }
        /* a song of 0.4.6 (version 6): its echo where it was, its rolls' Vary off (not the 64s), its MIDI row a row
         * down, the media of its first synth with one (here the second's) the mix's, the delays off */
        ui.val[0][PG_GLOBAL][1][4] = choice_val(2, 3);
        for (k = 0; k < DMK_SYNTHS; k++) for (int q = 0; q < 5; q++) ui.val[k][PG_ROLL][1][q] = 64;
        ui.val[0][PG_GLOBAL][2][0] = choice_val(1, 2); ui.val[0][PG_GLOBAL][2][1] = choice_val(2, 4); ui.val[0][PG_GLOBAL][2][2] = choice_val(0, 2);
        ui.val[0][PG_FX][1][4] = 0; ui.val[1][PG_FX][1][4] = choice_val(FM_VHS, FX_MEDIAS); ui.val[1][PG_FX][1][0] = 99;
        ui.val[2][PG_FX][1][4] = choice_val(FM_TAPE, FX_MEDIAS);
        for (k = 0; k < DMK_SYNTHS; k++) sq[k].count = 0;   /* (no notes: a note's bytes are 11's) */
        tap(B_SAVE); tap(B_SAVE);
        for (k = 0; k < ST_SONGS; k++) if (host_store[0x2000 + k * 4096] == 'D') host_store[0x2000 + k * 4096 + 12] = 6;
        dmk_init(27); now_ms = 0; wait_ms(60);
        if (gp.etype != ET_GRAINS || ui.val[1][PG_ROLL][1][0] || ui.val[1][PG_ROLL][1][2] != 127 || sq[1].chance != 32768u)
            { fails++; printf("selftest: a song of 0.4.6 loads with another echo or its rolls varied (%d)\n", gp.etype); }
        if (!midi_chans || midi_out != 2 || clock_ext || gmed.media != FM_VHS || gmed.mk[0] != 99 || syn[0].dtype || syn[1].dtype || syn[2].dtype)
            { fails++; printf("selftest: a song of 0.4.6 loads with its MIDI (%d %d %d) or its media (%d %d) elsewhere, or a delay on\n", midi_chans, midi_out, clock_ext, gmed.media, gmed.mk[0]); }
        lib[0][0] = 0;
        host_store_on(0, 0);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        #undef QUIET
        #undef PEAK
    }
    {   /* Save's Projects: the cursor over empty slots makes no names (the owner: names appeared as it passed); a project
         * gets one as it is first saved, kept when saved over; Save stays open after a save */
        char a1[24];
        host_store_on(1, 1);
        dmk_init(34); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_SEQ); tap(B_SAVE);
        for (int i = 0; i < 5; i++) dmk_encoder(E_K1, 1);
        for (int i = 0; i < 5; i++) dmk_encoder(E_K1, -1);
        if (ui.page != PG_DISCS || ui.val[0][PG_DISCS][1][2] == NM_MARK || song_used(ui.song_pick)) { fails++; printf("selftest: passing empty slots made a name\n"); }
        tap(B_SAVE);
        strcpy(a1, song_used(0) ? song_name(0) : "");
        if (!strchr(a1, ' ') || ui.page != PG_DISCS) { fails++; printf("selftest: a first save has no name, or Save closed (%s)\n", a1); }
        dmk_encoder(E_K1, 2); dmk_encoder(E_K1, -2); tap(B_SAVE);
        if (strcmp(a1, song_name(0))) { fails++; printf("selftest: saved over, it was renamed (%s)\n", song_name(0)); }
        tap(B_HOME);
        host_store_on(0, 0);
    }
    {   /* Vary is each synth's: the Vary row's knobs change the synth picked, not the first (the owner) */
        dmk_init(35); now_ms = 0; wait_ms(60); ui.boot_done = 1; ui.synth = 0;
        tap(B_SEQ); tap(B_SEQ);                         /* the roll's Vary row */
        dmk_encoder(E_SELECT, 1); wait_ms(200); dmk_encoder(E_SELECT, 1); wait_ms(200);   /* synth 3 */
        for (int i = 0; i < 40; i++) dmk_encoder(E_K1, 1);
        for (int i = 0; i < 40; i++) dmk_encoder(E_K3, -1);
        if (ui.synth != 2 || !sq[2].mut || sq[0].mut || sq[1].mut || sq[2].chance == 32768u || sq[0].chance != 32768u)
            { fails++; printf("selftest: Vary's knobs are not the synth's own (synth %d, mut %u %u %u)\n", ui.synth, sq[0].mut, sq[1].mut, sq[2].mut); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* the ARP: Up plays the held notes in order on the tempo; let go, it stops; HOLD keeps them, a new chord replaces
         * them; Chord strikes them together; off, nothing left sounding; a note from before it was on lets go as ever */
        static int16_t ab[256 * 2];
        uint8_t seq_[64];
        int got, i, v, on;
        uint32_t seen;
        #define ARP_RUN(ms) do { uint32_t end_ = now_ms + (ms); while (now_ms < end_) { dmk_render(ab, 128); now_ms += 3; \
            for (v = 0; v < DMK_VOICES; v++) if (vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ARP && vc[v].age >= seen) { \
                if (got < 64) seq_[got++] = vc[v].note; seen = vc[v].age + 1; } } } while (0)
        dmk_init(36); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_HOME][0][4] = 120;
        ui.val[2][PG_ROLL][2][0] = choice_val(AR_UP, AR_N); ui.val[2][PG_ROLL][2][1] = choice_val(2, 9); sp_sync();   /* Up, sixteenths */
        got = 0; seen = vc_clock;
        note_on_s(2, 64); note_on_s(2, 57); note_on_s(2, 60);
        ARP_RUN(900);
        if (got < 6 || seq_[0] != 57 || seq_[1] != 60 || seq_[2] != 64 || seq_[3] != 57) { fails++; printf("selftest: the arp's Up is not 57 60 64 57 (%d notes: %d %d %d %d)\n", got, seq_[0], seq_[1], seq_[2], seq_[3]); }
        if (got > 9) { fails++; printf("selftest: the arp plays %d sixteenths in 0.9 s at 120\n", got); }
        note_off_s(2, 64); note_off_s(2, 57); note_off_s(2, 60);
        got = 0; ARP_RUN(400);
        for (on = 0, v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2;
        if (got || on) { fails++; printf("selftest: the arp plays on after the keys are let go (%d, %d on)\n", got, on); }
        ui.val[2][PG_ROLL][2][4] = choice_val(1, 2); sp_sync();   /* HOLD */
        note_on_s(2, 62); note_on_s(2, 69); note_off_s(2, 62); note_off_s(2, 69);
        got = 0; ARP_RUN(600);
        if (got < 3) { fails++; printf("selftest: HOLD does not keep the arp going (%d)\n", got); }
        note_on_s(2, 50); note_off_s(2, 50);             /* a new chord after all were let go: it alone */
        ARP_RUN(150); got = 0; ARP_RUN(600);
        for (i = 0; i < got; i++) if (seq_[i] != 50) break;
        if (!got || i < got) { fails++; printf("selftest: HOLD's new chord does not replace the old (%d notes, %d first)\n", got, got ? seq_[0] : -1); }
        ui.val[2][PG_ROLL][2][0] = choice_val(AR_CHORD, AR_N); sp_sync();
        note_on_s(2, 53); note_on_s(2, 57); note_on_s(2, 60);
        ARP_RUN(200);
        for (on = 0, v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ARP;
        if (on < 3) { fails++; printf("selftest: the arp's Chord does not strike them together (%d)\n", on); }
        ui.val[2][PG_ROLL][2][0] = 0; sp_sync();          /* off: nothing left */
        ARP_RUN(100);
        for (on = 0, v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ARP;
        if (on) { fails++; printf("selftest: the arp off leaves %d notes sounding\n", on); }
        note_off_s(2, 53); note_off_s(2, 57); note_off_s(2, 60);
        note_on_s(2, 66);                                /* a note before the arp, let go after it is on */
        ui.val[2][PG_ROLL][2][0] = choice_val(AR_UP, AR_N); ui.val[2][PG_ROLL][2][4] = 0; sp_sync();
        note_off_s(2, 66); wait_ms(20);
        for (on = 0, v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2 && vc[v].note == 66;
        if (on) { fails++; printf("selftest: a note from before the arp sticks\n"); }
        /* a pitch held twice (a key and the roll), let go twice: nothing left held (the owner: it left a count up, and
         * HOLD then never let go); HOLD off lets go only what is no longer held */
        ui.val[2][PG_ROLL][2][4] = 0; sp_sync(); ARP_RUN(100);
        note_on_s(2, 60); note_on_vel(2, 60, SRC_ROLL, 100); ARP_RUN(200);
        note_off_s(2, 60); note_off_src(2, 60, SRC_ROLL); ARP_RUN(200);
        if (arp[2].held || arp[2].n) { fails++; printf("selftest: a pitch held twice and let go leaves the arp holding (%d held, %d notes)\n", arp[2].held, arp[2].n); }
        ui.val[2][PG_ROLL][2][4] = choice_val(1, 2); sp_sync();   /* HOLD: a chord kept, then a new one replaces it */
        note_on_s(2, 62); note_on_s(2, 65); note_off_s(2, 62); note_off_s(2, 65);
        note_on_s(2, 69); note_off_s(2, 69);
        if (arp[2].n != 1 || arp[2].note[0] != 69) { fails++; printf("selftest: HOLD's new chord does not replace the last (%d notes)\n", arp[2].n); }
        note_on_s(2, 72);                                /* one held as HOLD goes off: it alone plays on */
        ui.val[2][PG_ROLL][2][4] = 0; sp_sync();
        if (arp[2].n != 1 || arp[2].note[0] != 72) { fails++; printf("selftest: HOLD off keeps what was let go (%d notes)\n", arp[2].n); }
        note_off_s(2, 72); got = 0; ARP_RUN(500);
        for (on = 0, v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ARP;
        if (got || on || arp[2].n) { fails++; printf("selftest: the arp plays on after HOLD off and the keys let go (%d, %d on)\n", got, on); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        #undef ARP_RUN
    }
    {   /* a new song saved: a name made up for it (two words), shown on Songs; kept through a load */
        host_store_on(1, 1);
        dmk_init(33); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.song = 3; tap(B_SAVE); tap(B_SAVE);
        if (!song_used(3) || !strchr(song_name(3), ' ') || !strcmp(song_name(3), "First snow")) { fails++; printf("selftest: a new song has no name of its own (%s)\n", song_used(3) ? song_name(3) : "-"); }
        else {
            char was[24]; strcpy(was, song_name(3));
            songs_scan();
            if (strcmp(was, song_name(3))) { fails++; printf("selftest: a song's name changed in the store (%s, %s)\n", was, song_name(3)); }
            tap(B_SAVE); tap(B_SAVE);                   /* saved again over itself: its name kept */
            if (strcmp(was, song_name(3))) { fails++; printf("selftest: a song saved again was renamed\n"); }
        }
        host_store_on(0, 0);
    }
    {   /* a synth's length to 256 steps (the owner): K4 a step a click to 16, a beat to 64, then a bar; a note far
         * in plays; the song keeps the length and a long note; K3 is no longer the pen, a written note as long as the
         * last a held note was given */
        static uint8_t pk[4096];
        static const int16_t T[][3] = {{14, 1, 15}, {15, 1, 16}, {16, 1, 20}, {17, 1, 20}, {60, 1, 64}, {63, 1, 64}, {64, 1, 80},
                                       {240, 1, 256}, {256, 1, 256}, {256, -1, 240}, {80, -1, 64}, {65, -1, 64}, {64, -1, 60},
                                       {20, -1, 16}, {17, -1, 16}, {16, -1, 15}, {1, -1, 1}};
        int k, v, i, heard = 0;
        dmk_init(35); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_SEQ); ui.synth = 1; ui.row[PG_ROLL] = 0;  /* (the roll's own row) */
        for (k = 0; k < (int)(sizeof T / sizeof T[0]); k++) {
            sq[1].steps = (uint16_t)T[k][0]; dmk_encoder(E_K4, T[k][1]);
            if (sq[1].steps != T[k][2]) { fails++; printf("selftest: Steps %d turned %+d reads %d, not %d\n", T[k][0], T[k][1], sq[1].steps, T[k][2]); break; }
        }
        for (i = 0; i < DMK_SYNTHS; i++) sq[i].count = 0;
        sq[1].steps = 256; ui.roll_cur[1] = 0;
        i = seq_add(1, 200, 255, 72, 100); sq[1].n[i].off = 7;
        seq_play(1);
        while (seq_ticks < 202) { dmk_render(b, 128); for (v = 0; v < DMK_VOICES; v++) heard |= vc[v].on && vc[v].sy == 1 && vc[v].note == 72 && seq_ticks == 201; }
        seq_play(0);
        if (!heard) { fails++; printf("selftest: a note on step 201 of 256 does not play\n"); }
        st_ver = ST_VER;
        if (song_unpack(pk, (uint32_t)song_pack(pk)) || sq[1].steps != 256 || sq[1].count != 1 || sq[1].n[0].step != 200 || sq[1].n[0].len != 255 || sq[1].n[0].off != 7)
            { fails++; printf("selftest: a 256-step pattern comes back as %d steps, %d notes\n", sq[1].steps, sq[1].count); }
        ui.synth = 1; ui.row[PG_ROLL] = 0; seq_play(0);
        { int c0 = sq[1].count, pen = ui.roll_pen[1];
          dmk_encoder(E_K3, 1);
          if (ui.roll_pen[1] != pen) { fails++; printf("selftest: K3 still sets the pen\n"); }
          ui.roll_cur[1] = 200; ui.octave = 0; dmk_key(72 - 53, 1); wait_ms(30);   /* the note held: K3 its length */
          dmk_encoder(E_K3, -1); dmk_key(72 - 53, 0); wait_ms(30);
          if (sq[1].n[0].len != 254 || ui.roll_pen[1] != 254) { fails++; printf("selftest: a held note's length is not the next one's (%d, %d)\n", sq[1].n[0].len, ui.roll_pen[1]); }
          tap(B_REC); ui.roll_cur[1] = 10; dmk_key(4, 1); wait_ms(30); dmk_key(4, 0); wait_ms(30); tap(B_REC);
          if (sq[1].count != c0 + 1 || sq[1].n[c0].len != 254) { fails++; printf("selftest: a written note is not as long as the last one set\n"); } }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* FX's Space, a synth's sends: its ALGO and PRESET the verb all share, Off first (the owner found no way to set
         * or stop it there); a send at 0 says Off */
        char t[16];
        dmk_init(38); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        tap(B_FX); for (int i = 0; i < 6 && ui.row[PG_FX] != 3; i++) tap(B_FX);
        { int t0 = choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[0][PG_GLOBAL][0][4]);
          dmk_encoder(E_ALGO, -1);
          if (choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[0][PG_GLOBAL][0][4]) != t0 - 1) { fails++; printf("selftest: Space's ALGO is not the shared verb\n"); }
          for (int i = 0; i < 20 && choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[0][PG_GLOBAL][0][4]); i++) dmk_encoder(E_ALGO, -1);
          if (gp.vtype != 0 && choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[0][PG_GLOBAL][0][4])) { fails++; printf("selftest: the verb is not off from Space\n"); }
          dmk_encoder(E_PRESET, 1);
          if (choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[0][PG_GLOBAL][0][4]) != 1) { fails++; printf("selftest: PRESET on Space is not the shared verb\n"); } }
        ui.val[ui.synth][PG_FX][3][0] = 0; t[0] = 0; val_text(t, PG_FX, 3, 0);
        if (strcmp(t, "Off")) { fails++; printf("selftest: a send at 0 reads %s\n", t); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* the roll's K3, Follow (the owner): a chord written on a synth that follows the root moves with the
         * progression; turned to Free, it plays as written */
        int k, v, moved[2] = {0, 0};
        for (int f = 0; f < 2; f++) {
            dmk_init(37); now_ms = 0; wait_ms(60); ui.boot_done = 1;
            tap(B_SEQ); ui.synth = 2; ui.row[PG_ROLL] = 0;
            sq[2].count = 0; sq[2].steps = 16;
            seq_add(2, 0, 16, 57, 100); seq_add(2, 0, 16, 60, 100); seq_add(2, 0, 16, 64, 100);
            if (f) { while (ch.follow[2] != FW_FREE) dmk_encoder(E_K3, -1); }
            seq_play(1);
            for (k = 0; k < 1200; k++) {                /* ~7 s: the progression's next chords */
                dmk_render(b, 256);
                for (v = 0; v < DMK_VOICES; v++) if (vc[v].on && vc[v].sy == 2 && vc[v].src == SRC_ROLL && vc[v].note != 57 && vc[v].note != 60 && vc[v].note != 64) moved[f]++;
            }
            seq_play(0);
        }
        if (!moved[0] || moved[1]) { fails++; printf("selftest: Follow on K3: following the root moved %d, Free moved %d\n", moved[0], moved[1]); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* Vary on what a synth plays last (the owner): the chords it plays rest at no chance and move in the scale at
         * full mutation, none doubled; its arp on, the arp's notes vary and the roll feeding it stays as written */
        int k, v, on = 0, moved = 0, out = 0, dup = 0, arp_on_n = 0, arp_moved = 0;
        uint8_t was[SEQ_NOTES];
        dmk_init(34); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_ROLL][1][2] = 0; sp_sync();        /* the pad, playing the chords: no chance */
        tap(B_PLAY);
        for (k = 0; k < 120; k++) { dmk_render(b, 256); for (v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 0 && vc[v].src == SRC_CHORD; }
        tap(B_PLAY);
        if (on) { fails++; printf("selftest: the chords a synth plays at no chance still sound\n"); }
        ui.val[0][PG_ROLL][1][2] = 127; ui.val[0][PG_ROLL][1][0] = 127; sp_sync();
        tap(B_PLAY);
        for (k = 0; k < 600; k++) {                     /* ~3.5 s: a few chords */
            dmk_render(b, 256);
            for (int i = 0; i < ch_sn[0]; i++) {
                int in = 0;
                for (int j = 0; j < ch_np; j++) in |= ch_play[j] == ch_sp[0][i];
                for (int j = 0; j < i; j++) dup += ch_sp[0][j] == ch_sp[0][i];
                moved += !in; out += snap(ch_sp[0][i]) != ch_sp[0][i];
            }
        }
        tap(B_PLAY);
        if (!moved || out || dup) { fails++; printf("selftest: the chords' Vary: %d moved, %d out of the scale, %d doubled\n", moved, out, dup); }
        ui.val[0][PG_ROLL][1][0] = 0; sp_sync();
        for (k = 0; k < sq[1].count; k++) was[k] = sq[1].n[k].note;
        ui.val[1][PG_ROLL][2][0] = choice_val(AR_UP, AR_N);   /* the piano's arp on, Vary at no chance */
        ui.val[1][PG_ROLL][1][2] = 0; ui.val[1][PG_ROLL][1][0] = 127; ui.val[1][PG_ROLL][1][3] = choice_val(1, 2); sp_sync();
        tap(B_PLAY);
        for (k = 0; k < 200; k++) { dmk_render(b, 256); for (v = 0; v < DMK_VOICES; v++) arp_on_n += vc[v].on && vc[v].sy == 1 && vc[v].src == SRC_ARP; }
        tap(B_PLAY);
        if (arp_on_n) { fails++; printf("selftest: an arp at no chance still plays\n"); }
        ui.val[1][PG_ROLL][1][2] = 127; sp_sync();
        tap(B_PLAY);
        for (k = 0; k < 400; k++) {
            dmk_render(b, 256);
            for (v = 0; v < DMK_VOICES; v++) if (vc[v].on && vc[v].sy == 1 && vc[v].src == SRC_ARP) {
                int in = 0;
                for (int j = 0; j < arp[1].n; j++) in |= arp[1].note[j] == vc[v].note || arp[1].note[j] + 12 == vc[v].note;
                arp_on_n++; arp_moved += !in && arp[1].n;
            }
        }
        tap(B_PLAY);
        for (k = 0, on = 0; k < sq[1].count; k++) on += sq[1].n[k].note != was[k];
        if (!arp_on_n || !arp_moved || on) { fails++; printf("selftest: an arp's Vary: %d notes, %d moved, the roll fed changed %d\n", arp_on_n, arp_moved, on); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* the roll's Vary: no chance, no notes; mutation kept, the notes moved and still in the scale; the delay in time */
        int s, k, on, moved = 0, out = 0;
        uint8_t was[SEQ_NOTES];
        dmk_init(31); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[2][PG_ROLL][1][2] = 0; sp_sync();       /* (the pluck: it plays its roll; the pad plays the chords) */
        tap(B_PLAY);
        for (on = 0, k = 0; k < 40; k++) { dmk_render(b, 256); for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 2; }
        tap(B_PLAY);
        if (on) { fails++; printf("selftest: a roll at no chance still plays\n"); }
        s = 1;
        for (k = 0; k < sq[s].count; k++) was[k] = sq[s].n[k].note;
        ui.val[s][PG_ROLL][1][0] = 127; ui.val[s][PG_ROLL][1][3] = choice_val(1, 2); sp_sync();
        tap(B_PLAY);
        for (k = 0; k < 120; k++) dmk_render(b, 256);
        tap(B_PLAY);
        for (k = 0; k < sq[s].count; k++) { moved += sq[s].n[k].note != was[k]; out += snap(sq[s].n[k].note) != sq[s].n[k].note; }
        if (!moved || out) { fails++; printf("selftest: Vary kept moved %d notes, %d out of the scale\n", moved, out); }
        ui.val[0][PG_HOME][0][4] = 120; ui.val[2][PG_FX][1][4] = choice_val(1, 3);   /* synth 3's delay: a quarter at 120 */
        ui.val[2][PG_FX][1][2] = choice_val(6, DIV_N); sp_sync(); dmk_render(b, 256);
        if (syn[2].dtype != 1 || syn[2].dtime != 5512u) { fails++; printf("selftest: the delay's quarter at 120 is %u samples\n", (unsigned)syn[2].dtime); }
        ui.val[2][PG_FX][1][4] = choice_val(2, 3); ui.val[2][PG_FX][1][2] = choice_val(8, DIV_N); ui.val[0][PG_HOME][0][4] = 30; sp_sync(); dmk_render(b, 256);
        if (syn[2].dtype != 2 || syn[2].dtime > DL_LEN / 2u - 2u || syn[2].dtime < 1000u) { fails++; printf("selftest: the ping-pong's half at 30 is %u samples\n", (unsigned)syn[2].dtime); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* the delay's and the mix's media's types changed as a chord holds: faded, never a jump past the held sound's */
        static const uint8_t DT[6] = {1, 2, 0, 2, 1, 0}, MT[6] = {FM_VHS, FM_VINYL, FM_TAPE, FM_DICTA, 0, FM_CASS};
        int i, j, v, m0 = 0, m1 = 0, prev = 0;
        dmk_init(33); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        ui.val[0][PG_FX][1][0] = 110; ui.val[0][PG_FX][1][3] = 80;
        ui.val[0][PG_GLOBAL][2][2] = 0; sp_sync();       /* (no vinyl crackle: its pops are its own, not a switch's) */
        note_on_s(0, 57); note_on_s(0, 64); note_on_s(0, 69);
        for (i = 0; i < 40; i++) dmk_render(b, 256);
        for (j = 0; j < 13; j++) {
            if (j) {
                if (j & 1) ui.val[0][PG_FX][1][4] = choice_val(DT[j / 2], 3);
                else ui.val[0][PG_GLOBAL][2][4] = choice_val(MT[j / 2 - 1], FX_MEDIAS);
                sp_sync();
            }
            for (i = 0; i < 20; i++) {
                dmk_render(b, 256);
                for (v = 0; v < 512; v += 2) { int d = b[v] - prev; d = d < 0 ? -d : d; prev = b[v]; if (j) { if (d > m1) m1 = d; } else if (d > m0) m0 = d; }
            }
        }
        if (m1 > m0 * 2 + 400) { fails++; printf("selftest: a delay's or the media's type changed jumps %d (held, %d)\n", m1, m0); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* a synth's delay repeats a note after it has gone, the others' do not; off, nothing after it */
        int i, v, t, pk[2] = {0, 0};
        for (t = 0; t < 2; t++) {
            dmk_init(32); now_ms = 0; wait_ms(60); ui.boot_done = 1;
            ui.val[0][PG_GLOBAL][0][4] = ui.val[0][PG_GLOBAL][1][4] = ui.val[0][PG_GLOBAL][2][4] = 0;   /* no verb, echo, media */
            ui.val[0][PG_HOME][0][4] = 120;
            ui.val[2][PG_FX][1][4] = t ? choice_val(1, 3) : 0; ui.val[2][PG_FX][1][0] = 127; ui.val[2][PG_FX][1][3] = 64;
            ui.val[2][PG_FX][1][2] = choice_val(0, DIV_N); sp_sync();   /* a sixteenth: 125 ms */
            note_on_s(2, 60);
            for (i = 0; i < 4; i++) dmk_render(b, 256);
            for (v = 0; v < DMK_VOICES; v++) { vc[v].stage = 0; vc[v].on = 0; }
            for (i = 0; i < 20; i++) dmk_render(b, 256);   /* the first repeats */
            for (i = 0; i < 2; i++) dmk_render(b, 256);
            for (v = 0; v < 512; v++) { int a = b[v] < 0 ? -b[v] : b[v]; if (a > pk[t]) pk[t] = a; }
        }
        if (pk[0] > 64 || pk[1] < 300) { fails++; printf("selftest: a delay's repeats after a note: off %d, on %d\n", pk[0], pk[1]); }
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
    }
    {   /* recording as played (the owner): REC and PLAY wait for the first key, which starts it on the first step; a
         * strummed chord keeps its notes' spacing and a key its micro timing, both played back late in their steps
         * and saved; NUDGE moves a held note by 32nds, on into the next step */
        static uint8_t pk[4096];
        snote_t kept[SEQ_NOTES];
        uint32_t f = 0, at[4] = {0, 0, 0, 0}, seen[DMK_VOICES];
        int i, j, n, v, got = 0, sp = 100 * DMK_SR / 1000, kc;
        dmk_init(33); now_ms = 0; wait_ms(60); ui.boot_done = 1;
        for (i = 0; i < DMK_SYNTHS; i++) sq[i].count = 0;
        ui.synth = 1;
        tap(B_ARP);
        ui.val[0][PG_CHORD][0][3] = (uint8_t)(64 + 100 * 64 / 250); sp_sync();   /* a strum, up */
        sp = ch.strum * (DMK_SR / 1000);
        tap(B_REC); tap(B_PLAY); wait_ms(300);
        if (seq_on || !ui.rec_ready) { fails++; printf("selftest: REC and PLAY do not wait for a key\n"); }
        dmk_key(4, 1);
        if (!seq_on || seq_ticks != 1) { fails++; printf("selftest: the first key does not start it\n"); }
        wait_ms(1500); dmk_key(4, 0); wait_ms(60);
        kc = n = sq[1].count;
        for (i = 0, j = 0; i < n; i++) if (sq[1].n[i].step * 32 + sq[1].n[i].off > j) j = sq[1].n[i].step * 32 + sq[1].n[i].off;
        if (n < 3 || sq[1].n[0].step || sq[1].n[0].off || abs(j - (n - 1) * ch.strum * 32 * seq_bpm / 15000) > 1)
            { fails++; printf("selftest: a strummed chord recorded loses its spacing (%d notes, the last at %d/32)\n", n, j); }
        tap(B_SEQ);                                     /* the roll: a key alone, played ~0.4 into a step */
        do dmk_render(b, 32); while ((seq_now() & 255u) < 96u || (seq_now() & 255u) > 112u);
        dmk_key(9, 1); wait_ms(300); dmk_key(9, 0); wait_ms(30);
        if (sq[1].count != n + 1 || sq[1].n[n].off < 11 || sq[1].n[n].off > 15)
            { fails++; printf("selftest: a key recorded loses its micro timing (%d/32)\n", sq[1].count > n ? sq[1].n[n].off : -1); }
        tap(B_REC); tap(B_PLAY);
        for (v = 0; v < DMK_VOICES; v++) seen[v] = vc[v].age;
        dmk_button(B_PLAY, 1); wait_ms(30); dmk_button(B_PLAY, 0);   /* (PLAY on its release) */
        for (f = 0; f < (uint32_t)DMK_SR && got < 4; f += 32) {
            dmk_render(b, 32);
            for (v = 0; v < DMK_VOICES; v++)
                if (vc[v].on && vc[v].sy == 1 && vc[v].src == SRC_ROLL && vc[v].age != seen[v]) { seen[v] = vc[v].age; if (got < 4) at[got++] = f; }
        }
        for (i = 1; i < got && i < kc; i++) if (abs((int)(at[i] - at[i - 1]) - sp) > DMK_SR / 100)
            { fails++; printf("selftest: a recorded strum plays back %d samples apart, not %d\n", (int)(at[i] - at[i - 1]), sp); break; }
        if (got < 3) { fails++; printf("selftest: a recorded strum plays %d notes back\n", got); }
        tap(B_PLAY);
        n = sq[1].count;
        for (i = 0; i < n; i++) kept[i] = sq[1].n[i];
        st_ver = ST_VER;
        if (song_unpack(pk, (uint32_t)song_pack(pk)) || sq[1].count != n) { fails++; printf("selftest: a song with late notes does not come back\n"); }
        else for (i = 0; i < n; i++) if (memcmp(&kept[i], &sq[1].n[i], sizeof kept[i])) { fails++; printf("selftest: a late note saved comes back as another\n"); break; }
        for (i = 0; i < DMK_SYNTHS; i++) sq[i].count = 0;
        i = seq_add(1, 5, 2, 60, 100); sq[1].n[i].off = 31; ui.roll_cur[1] = 5;
        dmk_key(7, 1); wait_ms(30);
        dmk_encoder(E_K4, 1);
        if (sq[1].n[0].step != 6 || sq[1].n[0].off) { fails++; printf("selftest: NUDGE does not move a note on into the next step\n"); }
        dmk_encoder(E_K4, -1);
        if (sq[1].n[0].step != 5 || sq[1].n[0].off != 31) { fails++; printf("selftest: NUDGE back does not return\n"); }
        dmk_key(7, 0); wait_ms(30);
        dmk_init(7); now_ms = 0; wait_ms(60); ui.boot_done = 1;
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
    if (dry) { ui.val[0][PG_GLOBAL][2][4] = 0; ui.val[0][PG_FX][3][0] = 0; ui.val[0][PG_FX][3][1] = 0; }
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
    ui.val[0][PG_GLOBAL][2][4] = 0;                     /* only the effect heard: no tape on the mix */
    ui.val[0][PG_FX][3][0] = verb ? 100 : 30;           /* a little verb, or a lot for the verbs */
    ui.val[0][PG_FX][3][1] = 0;
    if (page >= 0) {
        int nch = page == PG_GLOBAL ? (row == 2 ? FX_MEDIAS : row ? FX_ECHOS : FX_VERBS) : page == PG_FILTER ? 8 : row == 0 ? FX_COLOURS : row == 1 ? 3 : FX_MOTIONS;
        if (page == PG_GLOBAL && row < 2) type++;       /* (Off first) */
        ui.val[0][page][row][4] = (uint8_t)(type * 128 / nch + 128 / nch / 2);
        for (j = 0; j < 4; j++) if (k[j] != 255) ui.val[0][page][row][j] = k[j];
    }
    if (page != PG_GLOBAL || row) ui.val[0][PG_GLOBAL][0][4] = choice_val(2, FX_VERBS);   /* the hall under the others */
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

/* the ARP's ways, each for the owner to hear: Am7 held on the third synth (the pluck), two octaves, sixteenths at 100 */
static void arp_demo(const char *dir)
{
    static const char *const NM[AR_N] = {"", "up", "down", "hill", "valley", "bounce", "fold", "unfold", "dice", "walk",
                                         "played", "chord", "thumb", "pinky"};
    static const uint8_t CH[4] = {57, 67, 60, 64};      /* (played in this order: Played shows it) */
    int m, n = DMK_SR * 7, i, k;
    int16_t *b = malloc((size_t)n * 4);
    char path[512];
    for (m = 1; m < AR_N; m++) {
        dmk_init(1);
        ui.val[0][PG_HOME][0][4] = 100;
        ui.val[2][PG_ROLL][2][0] = choice_val(m, AR_N); ui.val[2][PG_ROLL][2][1] = choice_val(2, 9);
        ui.val[2][PG_ROLL][2][2] = choice_val(1, 4); ui.val[2][PG_ROLL][2][3] = 72;
        sp_sync();
        for (k = 0; k < 4; k++) note_on_s(2, CH[k]);
        for (i = 0; i < n; i += DMK_SUB) {
            if (i == DMK_SR * 5) for (k = 0; k < 4; k++) note_off_s(2, CH[k]);
            dmk_render(b + i * 2, DMK_SUB);
        }
        snprintf(path, sizeof path, "%s/arp_%02d_%s.wav", dir, m, NM[m]);
        wav_write(path, b, n);
    }
    free(b);
    printf("arp demo: %s\n", dir);
}

static void fx_demo(const char *dir)
{
    static const uint8_t VB[4] = {100, 60, 100, 100}, SAT[4] = {96, 70, 50, 127}, CR[4] = {85, 90, 70, 40};
    static const uint8_t LO[4] = {95, 80, 0, 70}, NO[4] = {60, 50, 70, 25}, TA[4] = {70, 70, 60, 64};
    static const uint8_t CA[4] = {80, 60, 60, 80}, VH[4] = {90, 50, 50, 90}, CH[4] = {90, 100, 40, 100};
    static const uint8_t VI[4] = {60, 255, 70, 40}, DR[4] = {110, 90, 40, 255}, FI[4] = {255, 100, 64, 255};
    static const char *const VN[11] = {"verb_1_room", "verb_2_hall", "verb_3_space", "verb_4_haze", "verb_5_bloom", "verb_6_lofi",
                                       "verb_7_plate", "verb_8_spring", "verb_9_shimmer", "verb_10_reverse", "verb_11_freeze"};
    static const uint8_t VL[4] = {100, 60, 124, 100}, GR[4] = {110, 70, 70, 60};
    static const char *const FN[3] = {"filter_notch", "filter_peak", "filter_vowel"};
    int t;
    for (t = 0; t < 11; t++) fx_one(dir, VN[t], t == 4 || t == 9 ? "Lunaria" : "Nymphaea", PG_GLOBAL, 0, t, t == 1 || t == 2 || t == 3 || t == 8 ? VL : VB, 1, 0);
    fx_one(dir, "echo_grains", "Nymphaea", PG_GLOBAL, 1, ET_GRAINS, GR, 1, 0);
    fx_one(dir, "colour_1_saturate", "Nymphaea", PG_FX, 0, FC_SAT, SAT, 0, 0);
    fx_one(dir, "colour_2_crush", "Nymphaea", PG_FX, 0, FC_CRUSH, CR, 0, 0);
    fx_one(dir, "colour_3_lossy", "Nymphaea", PG_FX, 0, FC_LOSSY, LO, 0, 0);
    fx_one(dir, "colour_4_noise", "Nymphaea", PG_FX, 0, FC_NOISE, NO, 0, 0);
    fx_one(dir, "media_1_tape", "Lunaria", PG_GLOBAL, 2, FM_TAPE, TA, 0, 0);
    fx_one(dir, "media_2_cassette", "Lunaria", PG_GLOBAL, 2, FM_CASS, CA, 0, 0);
    fx_one(dir, "media_3_vhs", "Lunaria", PG_GLOBAL, 2, FM_VHS, VH, 0, 0);
    fx_one(dir, "motion_1_chorus", "Washingtonia", PG_FX, 2, FO_CHORUS, CH, 0, 0);
    fx_one(dir, "motion_2_vibrato", "Nymphaea", PG_FX, 2, FO_VIB, VI, 0, 0);
    fx_one(dir, "motion_3_drift", "Lunaria", PG_FX, 2, FO_DRIFT, DR, 0, 0);
    {                                                   /* stage 4's (docs/sound-design.md, 5) */
        static const uint8_t WA[4] = {90, 56, 70, 127}, DI[4] = {80, 70, 30, 110}, DF[4] = {70, 80, 120, 110};
        static const uint8_t VY[4] = {50, 60, 70, 50}, RA[4] = {60, 64, 50, 80}, DC[4] = {70, 64, 50, 90};
        static const uint8_t EN[4] = {90, 100, 50, 100}, PS[4] = {100, 50, 30, 70}, FL[4] = {90, 90, 25, 90};
        static const uint8_t TR[4] = {100, 40, 55, 0}, TP[4] = {110, 20, 20, 127};
        fx_one(dir, "dry_nymphaea", "Nymphaea", -1, 0, 0, WA, 0, 0);   /* each sound as it is, to hear them against */
        fx_one(dir, "dry_lunaria", "Lunaria", -1, 0, 0, WA, 0, 0);
        fx_one(dir, "dry_washingtonia", "Washingtonia", -1, 0, 0, WA, 0, 0);
        fx_one(dir, "colour_5_warm", "Nymphaea", PG_FX, 0, FC_WARM, WA, 0, 0);
        fx_one(dir, "colour_6_distort", "Washingtonia", PG_FX, 0, FC_DIST, DI, 0, 0);
        fx_one(dir, "colour_6_distort_fold", "Washingtonia", PG_FX, 0, FC_DIST, DF, 0, 0);
        fx_one(dir, "media_4_vinyl", "Lunaria", PG_GLOBAL, 2, FM_VINYL, VY, 0, 0);
        fx_one(dir, "media_5_radio", "Lunaria", PG_GLOBAL, 2, FM_RADIO, RA, 0, 0);
        fx_one(dir, "media_6_dictaphone", "Lunaria", PG_GLOBAL, 2, FM_DICTA, DC, 0, 0);
        {                                               /* each synth's delay (the owner's): in time at 72, a dotted
                                                           eighth; a ping-pong of eighths */
            const uint8_t DL[4] = {80, 80, choice_val(5, DIV_N), 70}, PP[4] = {80, 96, choice_val(3, DIV_N), 76};
            fx_one(dir, "delay_1_delay", "Washingtonia", PG_FX, 1, 1, DL, 0, 0);
            fx_one(dir, "delay_2_pingpong", "Nymphaea", PG_FX, 1, 2, PP, 0, 0);
        }
        fx_one(dir, "motion_4_ensemble", "Nymphaea", PG_FX, 2, FO_ENS, EN, 0, 0);
        fx_one(dir, "motion_5_phaser", "Nymphaea", PG_FX, 2, FO_PHASER, PS, 0, 0);
        fx_one(dir, "motion_6_flanger", "Lunaria", PG_FX, 2, FO_FLANGER, FL, 0, 0);
        fx_one(dir, "motion_7_tremolo", "Nymphaea", PG_FX, 2, FO_TREM, TR, 0, 0);
        fx_one(dir, "motion_7_tremolo_pan", "Nymphaea", PG_FX, 2, FO_TREM, TP, 0, 0);
    }
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
    ui.val[0][PG_GLOBAL][2][4] = fx ? choice_val(fx == 1 ? FM_TAPE : FM_VHS, FX_MEDIAS) : 0;   /* the mix's tape; or VHS */
    for (s = 0; s < DMK_SYNTHS; s++) {
        if (!fx) { ui.val[s][PG_FX][3][0] = 0; ui.val[s][PG_FX][3][1] = 0; }
        if (fx == 2) { ui.val[s][PG_FX][0][4] = choice_val(FC_NOISE, FX_COLOURS); ui.val[s][PG_FX][1][4] = choice_val(2, 3);
                       ui.val[s][PG_FX][2][4] = choice_val(FO_FLANGER, FX_MOTIONS); }   /* each row's heaviest */
    }
    if (fx == 3) for (s = 0; s < DMK_SYNTHS; s++) {     /* and a layer under every voice, the LFO on the cutoff */
        ui.val[s][PG_SOUND][1][0] = 90; ui.val[s][PG_LFO][0][1] = 60; ui.val[s][PG_LFO][0][4] = choice_val(LT_CUTOFF, 5);
    }
    if (fx == 3) fx = 2;
    ui.val[0][PG_GLOBAL][0][4] = fx == 2 ? choice_val(VB_HEAVY + 1, FX_VERBS) : choice_val(1, FX_VERBS);   /* the heaviest; else the room */
    ui.val[0][PG_GLOBAL][1][4] = fx == 2 ? choice_val(2, FX_ECHOS) : choice_val(1, FX_ECHOS);   /* and the grains; else the tape */
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
    printf("cpu: idle %.0f, 12 voices dry %.0f, 12 voices with the mix's tape, room and echo %.0f instructions a sample\n",
           (double)(z - a) / frames, cpu_case(0), cpu_case(1));
    {   /* the heaviest: every synth noise, a ping-pong and flanger (each row's heaviest), the freeze, the grains, VHS on the
         * mix; each core's share */
        double all = cpu_case(2), c0, c1;
        cpu_cores(&c0, &c1);
        printf("cpu: the heaviest (noise, ping-pong, flanger x3, freeze, grains, VHS) %.0f; on the FM-1 CPU0 %.0f (its wait included), CPU1 %.0f\n",
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
    if (argc > 2 && !strcmp(argv[1], "--arpdemo")) { arp_demo(argv[2]); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--anim")) { anim(argv[2]); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--homes")) { homes(argv[2]); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--chorddemo")) { chord_demo(argv[2]); return 0; }
#ifdef __APPLE__
    if (argc > 1 && !strcmp(argv[1], "--cpu")) { cpu(); return 0; }
#endif
    tour(argc > 1 ? argv[1] : "build/host");
    return fails ? 1 : 0;
}
