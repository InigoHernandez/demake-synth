/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's core: a unity build of the parts below, reached only through dmk.h. C99, integer only, static everything. */
#include "dmk.h"
#include "dmk_tables.h"
#include "dmk_font.h"
#include "dmk_samples.h"

int (*dmk_par_begin)(void (*job)(void));
void (*dmk_par_end)(void);

#include "dmk_gfx.c"
#include "dmk_dsp.c"
#include "dmk_ui.c"

static uint32_t samples;

void dmk_init(uint32_t seed)
{
    int i;
    (void)seed;
    for (i = 0; i < DMK_VOICES; i++) { vc[i].on = 0; vc[i].env = 0; vc[i].stage = 0; }
    dsp_reset();
    samples = 0;
    theme = 0;
    ui_init();
}

void dmk_render(int16_t *lr, int frames)
{
    dsp_render(lr, frames);
    samples += (uint32_t)frames;
}

void dmk_key(int k, int down) { ui_key(k, down); }
void dmk_button(int b, int down) { ui_button(b, down); }
void dmk_encoder(int e, int delta) { ui_encoder(e, delta); }
void dmk_midi(int status, int d1, int d2)
{
    if ((status & 0xF0) == 0x90 && d2) note_on(d1);
    else if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) note_off(d1);
}
const uint8_t *dmk_frame(uint32_t ms) { return ui_frame(ms); }
const uint16_t *dmk_palette565(void) { return T_PAL565[theme]; }
const uint32_t *dmk_palette_rgb(void) { return T_PALRGB[theme]; }
const uint8_t *dmk_leds(void) { return ui_leds(); }
int dmk_playing(void) { return 0; }
uint32_t dmk_samples(void) { return samples; }
#ifdef DMK_BENCH
int dmk_bench_level(void) { return bench_level; }   /* the FM-1's meter shows it (dmk_fm1.c) */
#endif
