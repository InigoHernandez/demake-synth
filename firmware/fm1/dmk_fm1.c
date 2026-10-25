/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the M-VAVE FM-1: the core (firmware/src) on the open FM-1 platform (src/platform.c: CS-1's, after
 * Dinghy, SLOOP and Felucca, as MONO2K carries it; README.md says whose is what): this unit, the platform and the
 * glue; the core is its own unit.
 *
 * As MONO2K's glue: the core runs in the main loop alone, as it runs in the browser's worklet. The loop renders the
 * sound ahead into a ring and the audio interrupt only copies out of it, so no interrupt lands in the middle of an
 * edit. Between two blocks the loop takes the panel and MIDI, draws a frame when the last has gone out, and sends the
 * screen one changed strip at a time (a whole screen is 77 ms of SPI). The screen is 4 bits a pixel: each strip goes
 * out through the theme's 16 colours. */
#define APP_NAME "DEMAKE"
#ifndef APP_VER
#define APP_VER "0.0"
#endif
/* Demake's store (docs/plan.md, 2.6): 0xB0000..0xC0000, MONO2K's 0xC0000..0xE0000 left alone. The core lays it
 * out (dmk_store.c: songs and the sound library, each record with its magic and CRC); this writes it */
#define BANK_AT 0x000B0000u
#define BANK_LEN 0x00010000u
#define CV_MAX (240u * 18u)                     /* the platform's text: a line of FONT_S at a time (crash, update, rescue) */
/* the update's screen is Demake's (the core draws it, app_ota_show sends it); the rescue keeps the platform's */
#include <stdint.h>
static void app_ota_show(uint32_t step, int32_t code);
static void app_ota_idle(void);
#define APP_OTA_SHOW app_ota_show
#define APP_OTA_IDLE app_ota_idle
#include "platform.c"
#include "dmk.h"                                /* the core: its own unit (core.o), its API alone */

/* ---- the sound: the ring the main loop fills and the audio interrupt empties */
#define BLK 128u                                /* frames a render */
#define RING 6u                                 /* blocks: 17 ms ahead of the interrupt's own 5.8 ms */
static int16_t ring[RING][BLK * 2u];
static volatile uint32_t ring_w, ring_r;        /* blocks written / read, free-running */
static volatile uint32_t ring_short;            /* halves the interrupt found the ring short for */

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, f = 0;
    while (f < n) {
        if (ring_w == ring_r) {                 /* the loop fell behind: silence, counted */
            ring_short++;
            return;
        }
        const int16_t *b = ring[ring_r % RING];
        for (i = 0; i < BLK * 2u; i++)
            out[f * 2u + i] = b[i];
        ring_r++;
        f += BLK;
    }
}

/* at most half the ring a pass: were the core slower than the sound, the loop still reaches the panel and the
 * watchdog (the sound would stutter, the FM-1 would not reset) */
static uint32_t load_ticks;

static void render_ahead(void)
{
    uint32_t k;
    for (k = 0; k < RING / 2u && ring_w - ring_r < RING; k++) {
        uint32_t t0 = fm1_ticks();
        dmk_render(ring[ring_w % RING], (int)BLK);
        load_ticks += fm1_ticks() - t0;
        RING_PUBLISH();
        ring_w++;
    }
}

/* ---- the store: the core's records in Demake's 64 KiB, a 4 KiB sector a write, the record's header written last
 * so a cut save never reads as a record. Called from the panel (the loop), never inside a render: the second core
 * is idle and reads no flash; the erase stops the sound ~50 ms, so the core saves only while stopped */
static int store_read(uint32_t at, void *p, uint32_t n)
{
    if (at > BANK_LEN || n > BANK_LEN - at) return -1;
    return st_read(BANK_AT + at, p, n);
}
static int store_write(uint32_t at, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t took;
    int rc;
    if (!flash_ok || (at & 0xFFFu) || n > 0x1000u || at + 0x1000u > BANK_LEN) return -1;
    { uint32_t i; for (i = 0; i < 2u * HALF_WORDS; i++) abuf[i] = 0; }   /* the DMA loops its two halves while the
                                                   erase holds the interrupts: silence, not the last 11 ms buzzing */
    if ((rc = fl_erase4k(BANK_AT + at, &took)) != 0) return rc;
    if (n > 16u && (rc = fl_write(BANK_AT + at + 16u, b + 16, n - 16u)) != 0) return rc;
    return fl_write(BANK_AT + at, b, n < 16u ? n : 16u);
}

/* ---- the second core: the core hands it some synths of each block (dmk_par_begin), CS-1's audio worker runs them */
#if DINGHY_DUAL_CORE
static void (*par_fn)(void);
static void par_tramp(void *c) { (void)c; par_fn(); }
static int par_begin(void (*job)(void)) { par_fn = job; return audio_worker_submit(par_tramp, 0); }
static uint32_t job_ticks;                      /* the second core's time on jobs, for the meter */
static void par_end(void) { audio_worker_join(); job_ticks += audio_worker.last_job_ticks; }
#endif

/* ---- the panel: the platform's ids (platform.c) to the core's (dmk.h) */
static int button_of(uint32_t id) { return id == BTN_OCT_DOWN ? B_OCTD : id == BTN_OCT_UP ? B_OCTU : (int)id - 2; }
static const uint8_t ENC_OF[7] = {E_SELECT, E_ALGO, E_K1, E_K2, E_K3, E_K4, E_PRESET};
static uint32_t notes_prev;

static void take_panel(void)
{
    uint32_t rel, pr = fm1_input_edges(&rel), notes = fm1_in.notes, ch = notes ^ notes_prev, i;
    for (i = 0; i < 14u; i++) {
        if (pr >> i & 1u) dmk_button(button_of(i), 1);
        if (rel >> i & 1u) dmk_button(button_of(i), 0);
    }
    for (i = 0; i < DMK_KEYS; i++)
        if (ch >> i & 1u) dmk_key((int)i, (int)(notes >> i & 1u));
    notes_prev = notes;
    for (i = 0; i < 7u; i++) {
        int32_t d = fm1_enc_take(i);
        if (d) dmk_encoder(ENC_OF[i], d);
    }
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) dmk_midi(st, d1, d2);
    while (dmk_midi_take(&st, &d1, &d2)) midi_send(st, d1, d2);   /* MIDI out, over USB: the synths' notes, the clock */
}

/* ---- a test build's meter (DMK_FM1_DIAG): over the top 16 lines, in red, shares of real time in %: CPU the first
 * core rendering the sound (waiting for the second included: what must stay under 100), W the second core's work,
 * F drawing the screen; B the bench's level on a bench build; a block at the top right while the sound ran short */
#ifndef DMK_FM1_DIAG
#define DMK_FM1_DIAG 0
#endif
static uint32_t diag_short_ms, diag_last_short, frame_ticks;
#if DMK_FM1_DIAG
static uint32_t diag_t0;
#endif
static char diag_txt[48];

static void diag_num(char *o, uint32_t v)
{
    char b[8];
    int n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 7);
    while (n) *o++ = b[--n];
    *o = 0;
}

#ifdef DMK_BENCH
extern int dmk_bench_level(void);
#endif

/* every half second, in every build: the load to the core, which shows it on GLO (the owner heard it overload) */
static uint32_t load_t0;
static void load_tick(void)
{
    uint32_t now = fm1_ticks(), span = now - load_t0;
    if (span >= 500u * 1000u * FM1_TICKS_PER_US) {
        uint32_t w = 0;
#if DINGHY_DUAL_CORE
        w = job_ticks;
#endif
        dmk_status((int)(load_ticks / (span / 100u)), (int)(w / (span / 100u)), (int)(frame_ticks / (span / 100u)), ring_short);
        if (!DMK_FM1_DIAG) {
            load_ticks = frame_ticks = 0;
#if DINGHY_DUAL_CORE
            job_ticks = 0;
#endif
        }
        load_t0 = now;
    }
}

static void diag_tick(void)
{
#if DMK_FM1_DIAG
    uint32_t now = fm1_ticks(), span = now - diag_t0;
    if (span >= 500u * 1000u * FM1_TICKS_PER_US) {      /* every half second */
        uint32_t w = 0;
#if DINGHY_DUAL_CORE
        w = job_ticks;
        job_ticks = 0;
#endif
        char *o = diag_txt;
        *o++ = 'C'; *o++ = 'P'; *o++ = 'U'; *o++ = ' ';
        diag_num(o, load_ticks / (span / 100u)); while (*o) o++;
        *o++ = ' '; *o++ = ' '; *o++ = 'W'; *o++ = ' ';
        diag_num(o, w / (span / 100u)); while (*o) o++;
        *o++ = ' '; *o++ = ' '; *o++ = 'F'; *o++ = ' ';
        diag_num(o, frame_ticks / (span / 100u)); while (*o) o++;
#ifdef DMK_BENCH
        *o++ = ' '; *o++ = ' '; *o++ = 'B'; *o++ = ' ';
        diag_num(o, (uint32_t)dmk_bench_level()); while (*o) o++;
#endif
        load_ticks = frame_ticks = 0;
        diag_t0 = now;
    }
#endif
    if (ring_short != diag_last_short) { diag_last_short = ring_short; diag_short_ms = fm1_ms; }
}

/* a 3 x 5 font, doubled: digits, the parts' letters and a space (each glyph: 5 rows of 3 bits) */
static const uint16_t *diag_glyph(char c)
{
    static const uint16_t D[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},
                                      {7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    static const uint16_t A[][5] = {{7,4,4,4,7} /* C */, {7,5,7,4,4} /* P */, {5,5,5,5,7} /* U */, {6,5,6,5,6} /* B */,
                                    {7,4,6,4,4} /* F */, {5,5,7,7,5} /* W */};
    static const uint16_t SP[5] = {0, 0, 0, 0, 0};
    if (c >= '0' && c <= '9') return D[c - '0'];
    switch (c) { case 'C': return A[0]; case 'P': return A[1]; case 'U': return A[2]; case 'B': return A[3];
                 case 'F': return A[4]; case 'W': return A[5]; }
    return SP;
}

/* ---- the screen: the core's 4-bit frame through the theme's 16 colours (the panel takes RGB565 big-endian), sent
 * in strips of ROWS lines where they changed since the last sent */
#define ROWS 8u
#define NSTRIP (240u / ROWS)
#define ROWB (240u / 2u)                        /* bytes a line */
static uint8_t shown[DMK_FB_BYTES] __attribute__((section(".pool")));   /* what the panel shows */
static uint16_t strip[2][240u * ROWS];          /* two: one fills while the other goes out */
static uint16_t lut[256][2];                    /* a byte (two pixels) to their two colours, big-endian */
static uint16_t pal_shown[16];
static uint8_t todo[NSTRIP], pending, sbuf;
static const uint8_t *frame;

static uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static uint32_t frame_ms;

/* a frame is several milliseconds of drawing (the lit graphs, the landscape): between its parts the core calls this,
 * and the ring is topped up, so a long frame never lets it run dry (a click) */
static uint32_t yield_ticks;
static void frame_yield(void)
{
    uint32_t t0 = fm1_ticks();
    render_ahead();
    render_ahead();                             /* to the top: the frame goes on drawing after */
    yield_ticks += fm1_ticks() - t0;
}

static void screen_frame(void)
{
    uint32_t s, i, t0 = fm1_ticks();
    const uint16_t *pal;
    frame_ms = fm1_ms;
    yield_ticks = 0;
    frame = dmk_frame(fm1_ms);
    pal = dmk_palette565();
    frame_ticks += fm1_ticks() - t0 - yield_ticks;
    for (i = 0; i < 16u && pal[i] == pal_shown[i]; i++)
        ;
    if (i < 16u) {                              /* a new theme: every strip goes again */
        for (i = 0; i < 16u; i++) pal_shown[i] = pal[i];
        for (i = 0; i < 256u; i++) { lut[i][0] = be16(pal[i >> 4]); lut[i][1] = be16(pal[i & 15u]); }
        for (s = 0; s < NSTRIP; s++) todo[s] = 1;
        pending = NSTRIP;
        return;
    }
    for (s = 0; s < NSTRIP; s++) {
        const uint8_t *a = frame + s * ROWS * ROWB, *b = shown + s * ROWS * ROWB;
        if (todo[s]) continue;
        for (i = 0; i < ROWS * ROWB && a[i] == b[i]; i++)
            ;
        todo[s] = i < ROWS * ROWB;
        pending += todo[s];
    }
}

static void screen_send(void)                   /* one strip, if the last has gone out */
{
    uint32_t s, x, y;
    if (!pending || !lcd_idle())
        return;
    for (s = 0; s < NSTRIP && !todo[s]; s++)
        ;
    if (s == NSTRIP) { pending = 0; return; }
    lcd_sync();
    uint16_t *px = strip[sbuf];
    const uint8_t *src = frame + s * ROWS * ROWB;
    for (y = 0; y < ROWS * ROWB; y++) { px[y * 2u] = lut[src[y]][0]; px[y * 2u + 1u] = lut[src[y]][1]; }
    memcpy(shown + s * ROWS * ROWB, src, ROWS * ROWB);
    if (DMK_FM1_DIAG && s < 2u) {               /* the meter over the first two strips (resent each frame) */
        uint16_t red = be16(RGB(255, 0, 0));
        uint32_t c, gx = 2;
        for (c = 0; diag_txt[c] && gx + 6u < 230u; c++, gx += 8u) {
            const uint16_t *g = diag_glyph(diag_txt[c]);
            for (y = 0; y < ROWS; y++) {
                uint32_t row = (s * ROWS + y) / 2u - 1u;   /* glyph rows from line 2, doubled */
                if (s * ROWS + y < 2u || row >= 5u) continue;
                for (x = 0; x < 6u; x++) if (g[row] >> (2u - x / 2u) & 1u) px[y * 240u + gx + x] = red;
            }
        }
        if (fm1_ms - diag_short_ms < 1000u && diag_last_short)
            for (y = 0; y < ROWS; y++) for (x = 232; x < 240u; x++) px[y * 240u + x] = red;
        shown[s * ROWS * ROWB] ^= 0x01u;        /* so the next frame sends this strip again */
    }
    lcd_blit(0, s * ROWS, 240, ROWS, px);
    sbuf ^= 1u;
    todo[s] = 0;
    pending--;
}

/* ---- the update's screen: the core's frame for the step, the whole screen sent now (the update waits for it); while
 * it waits for the computer its bits step, at most every 200 ms, only the word's strips (~25 ms of SPI) */
static uint32_t ota_step, ota_code, ota_shown_ms;

static void ota_send(const uint8_t *f, uint32_t s0, uint32_t s1)
{
    const uint16_t *pal = dmk_palette565();
    uint32_t s, y, i;
    for (i = 0; i < 256u; i++) { lut[i][0] = be16(pal[i >> 4]); lut[i][1] = be16(pal[i & 15u]); }
    for (i = 0; i < 16u; i++) pal_shown[i] = pal[i];
    for (s = s0; s < s1; s++) {
        uint16_t *px = strip[sbuf];
        const uint8_t *src = f + s * ROWS * ROWB;
        lcd_sync();
        for (y = 0; y < ROWS * ROWB; y++) { px[y * 2u] = lut[src[y]][0]; px[y * 2u + 1u] = lut[src[y]][1]; }
        memcpy(shown + s * ROWS * ROWB, src, ROWS * ROWB);
        lcd_blit(0, s * ROWS, 240, ROWS, px);
        sbuf ^= 1u;
        todo[s] = 0;
    }
    lcd_sync();
    pending = 0;
}

static void app_ota_show(uint32_t step, int32_t code)
{
    dmk_yield = 0;                              /* no sound while the flash is written: a frame draws only */
    ota_step = step;
    ota_code = (uint32_t)code;
    ota_shown_ms = fm1_ms;
    ota_send(dmk_update_frame(step, code), 0, NSTRIP);
}

static void app_ota_idle(void)
{
    dmk_yield = 0;
    if (!ota_step || ota_step >= 5u || fm1_ms - ota_shown_ms < 200u) return;
    ota_shown_ms = fm1_ms;
    ota_send(dmk_update_frame(ota_step, (int32_t)ota_code), 3, 17);   /* rows 24..135: the word */
}

/* ---- the lights: the core's 27 keys and 14 buttons, 0..255, on from a quarter up (the panel's LEDs are on / off) */
static void lights(void)
{
    const uint8_t *l = dmk_leds();
    uint32_t i;
    for (i = 0; i < DMK_KEYS; i++) fm1_led_key(KEY_LED(i), l[i] >= 64u);
    for (i = 0; i < 14u; i++) fm1_led_key(i, l[DMK_KEYS + button_of(i)] >= 64u);
}

static void app_init(void)
{
#if DINGHY_DUAL_CORE
    if (audio_worker_online) { dmk_par_begin = par_begin; dmk_par_end = par_end; }
#endif
    dmk_yield = frame_yield;
    if (flash_ok) { dmk_store_read = store_read; dmk_store_write = store_write; }   /* before init: it loads the newest song */
    dmk_init(fm1_ticks());
    dmk_latency(1280);                          /* a key to its sound: the render ring (6 x 128), the I2S half (256) and
                                                   the keys' debounce (~390): a take lands where it was heard */
    lcd_fill(0, 0, 240, 240, 0x0000u);
    render_ahead();
    screen_frame();
}

static void app_loop(void)
{
    dmk_yield = frame_yield;                    /* again after an update that stopped */
    if (DMK_FM1_DIAG) diag_tick();
    load_tick();
    render_ahead();
    take_panel();
    render_ahead();
    if (!pending && fm1_ms - frame_ms >= 33u) {   /* 30 frames a second at most, as the browser draws them */
        lights();
        screen_frame();
    }
    screen_send();
}
