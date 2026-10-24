/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's core: a unity build of the parts below, reached only through dmk.h. C99, integer only, static everything. */
#include "dmk.h"
#include "dmk_tables.h"
#include "dmk_font.h"
#include "dmk_samples.h"

int (*dmk_par_begin)(void (*job)(void));
void (*dmk_par_end)(void);
void (*dmk_yield)(void);
int (*dmk_store_read)(uint32_t at, void *p, uint32_t n);
int (*dmk_store_write)(uint32_t at, const void *p, uint32_t n);
static void ui_yield(void) { if (dmk_yield) dmk_yield(); }

#include "dmk_gfx.c"
#include "dmk_dsp.c"
#include "dmk_seq.c"
#include "dmk_chord.c"
#include "dmk_ui.c"
#include "dmk_store.c"

static uint32_t samples;

void dmk_init(uint32_t seed)
{
    int i;
    boot_sc = (uint8_t)(((seed * 2654435761u) >> 16) % DMK_SCENES);   /* the boot's scene, by the start's clock */
    for (i = 0; i < DMK_VOICES; i++) { vc[i].on = 0; vc[i].env = 0; vc[i].stage = 0; }
    dsp_reset();
    samples = 0;
    theme = 0;
    seq_init();
    chord_init();
    ui_init();
    store_boot();
}

void dmk_render(int16_t *lr, int frames)
{
    seq_render(frames);
    dsp_render(lr, frames);
    samples += (uint32_t)frames;
}

void dmk_key(int k, int down) { ui_key(k, down); }
void dmk_button(int b, int down) { ui_button(b, down); }
void dmk_encoder(int e, int delta) { ui_encoder(e, delta); }
void dmk_midi(int status, int d1, int d2) { midi_in(status, d1, d2); }
void dmk_status(int cpu, int core2, int screen, uint32_t shorts)
{
    sys.cpu = (uint8_t)(cpu > 255 ? 255 : cpu); sys.core2 = (uint8_t)(core2 > 255 ? 255 : core2);
    sys.screen = (uint8_t)(screen > 255 ? 255 : screen); sys.shorts = shorts; sys.on = 1;
}
int dmk_midi_take(uint8_t *status, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *status = mq[mq_r % MQ_LEN][0]; *d1 = mq[mq_r % MQ_LEN][1]; *d2 = mq[mq_r % MQ_LEN][2];
    mq_r++;
    return 1;
}
const uint8_t *dmk_frame(uint32_t ms) { return ui_frame(ms); }
const uint8_t *dmk_update_frame(uint32_t step, int32_t code) { return ui_update(step, code); }
const uint16_t *dmk_palette565(void) { return T_PAL565[theme]; }
const uint32_t *dmk_palette_rgb(void) { return T_PALRGB[theme]; }
const uint8_t *dmk_leds(void) { return ui_leds(); }
int dmk_playing(void) { return seq_on; }
uint32_t dmk_samples(void) { return samples; }
#ifdef DMK_BENCH
int dmk_bench_level(void) { return bench_level; }   /* the FM-1's meter shows it (dmk_fm1.c) */
#endif
