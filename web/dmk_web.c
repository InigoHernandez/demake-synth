/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake in the browser: the core and the exports web/worklet.js calls (no JS glue: STANDALONE_WASM). */
#include "dmk_core.c"

static int16_t audio[128 * 2];

/* the store, in this instance's memory: the worklet sends it to the page after each write (w_store_gen), the page
 * keeps it in its storage and gives it back before w_init, which loads the newest song */
static uint8_t store[65536];
static uint32_t store_gen;
static int ws_read(uint32_t at, void *p, uint32_t n)
{
    uint32_t i;
    if (at > sizeof store || n > sizeof store - at) return -1;
    for (i = 0; i < n; i++) ((uint8_t *)p)[i] = store[at + i];
    return 0;
}
static int ws_write(uint32_t at, const void *p, uint32_t n)
{
    uint32_t i;
    if (at & 4095u || n > 4096u || at + 4096u > sizeof store) return -1;
    for (i = 0; i < 4096u; i++) store[at + i] = i < n ? ((const uint8_t *)p)[i] : 0xFF;
    store_gen++;
    return 0;
}
uint8_t *w_store(void) { return store; }
uint32_t w_store_gen(void) { return store_gen; }

int16_t *w_audio(void) { return audio; }
void w_render(void) { dmk_render(audio, 128); }
void w_init(uint32_t seed) { dmk_store_read = ws_read; dmk_store_write = ws_write; dmk_init(seed); }
const uint8_t *w_frame(uint32_t ms) { return dmk_frame(ms); }
const uint32_t *w_palette(void) { return dmk_palette_rgb(); }
const uint8_t *w_leds(void) { return dmk_leds(); }
void w_key(int k, int d) { dmk_key(k, d); }
void w_button(int b, int d) { dmk_button(b, d); }
void w_encoder(int e, int d) { dmk_encoder(e, d); }
void w_midi(int s, int a, int b) { dmk_midi(s, a, b); }
