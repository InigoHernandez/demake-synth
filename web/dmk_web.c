/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake in the browser: the core and the exports web/worklet.js calls (no JS glue: STANDALONE_WASM). */
#include "dmk_core.c"

static int16_t audio[128 * 2];

int16_t *w_audio(void) { return audio; }
void w_render(void) { dmk_render(audio, 128); }
void w_init(uint32_t seed) { dmk_init(seed); }
const uint8_t *w_frame(uint32_t ms) { return dmk_frame(ms); }
const uint32_t *w_palette(void) { return dmk_palette_rgb(); }
const uint8_t *w_leds(void) { return dmk_leds(); }
void w_key(int k, int d) { dmk_key(k, d); }
void w_button(int b, int d) { dmk_button(b, d); }
void w_encoder(int e, int d) { dmk_encoder(e, d); }
void w_midi(int s, int a, int b) { dmk_midi(s, a, b); }
