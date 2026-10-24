/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake: an ambient lofi machine for the M-VAVE FM-1 (docs/plan.md). This core has no platform code: the FM-1 build
 * calls it from the main loop, the browser build from an AudioWorklet, the host build from tests. Integer only (the
 * build links no libgcc: no float, no 64-bit division). */
#ifndef DMK_INCLUDED
#define DMK_INCLUDED

#include <stdint.h>

#define DMK_SR        44100
#define DMK_SUB       16          /* control-rate block: envelopes, parameters, triggers */
#define DMK_SYNTHS    3
#define DMK_VOICES    12
#define DMK_W         240
#define DMK_H         240
#define DMK_FB_BYTES  (DMK_W * DMK_H / 2)   /* 4 bits a pixel, the left pixel in the high nibble */
#define DMK_KEYS      27          /* F3..G5: 16 lower ("white") and 11 upper ("black") keys */

/* big buffers go to the FM-1's pool RAM (Felucca's linker section); elsewhere they are plain statics */
#ifndef POOL
#ifdef DMK_FW
#define POOL __attribute__((section(".pool")))
#else
#define POOL
#endif
#endif

/* the FM-1's controls, in the order of the platform's input layer */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTD, B_OCTU, B_COUNT };
enum { E_SELECT, E_ALGO, E_PRESET, E_K1, E_K2, E_K3, E_K4, E_MASTER, E_COUNT };

void dmk_init(uint32_t seed);
void dmk_render(int16_t *lr, int frames);           /* interleaved stereo, frames a multiple of DMK_SUB */
void dmk_key(int k, int down);
void dmk_button(int b, int down);
void dmk_encoder(int e, int delta);
void dmk_midi(int status, int d1, int d2);
const uint8_t *dmk_frame(uint32_t ms);              /* draws the screen: 240 x 240, 4 bits a pixel (DMK_FB_BYTES) */
const uint8_t *dmk_update_frame(uint32_t step, int32_t code);   /* the screen while a firmware goes in: steps 1..5,
                                                                   9 the end (code 1: a trial run; else it stopped) */
const uint16_t *dmk_palette565(void);               /* the theme's 16 colours as RGB565, for the FM-1 */
const uint32_t *dmk_palette_rgb(void);              /* the same as 0xRRGGBB, for the browser and the host */
const uint8_t *dmk_leds(void);                      /* 27 key LEDs then 14 button LEDs, 0..255 */
int dmk_playing(void);
uint32_t dmk_samples(void);                         /* samples rendered since init */
/* a second core (the FM-1's): the platform sets these and the render runs some synths there, block by block.
 * dmk_par_begin(job) starts job on it (0: it could not, the core does it in place), dmk_par_end waits for it */
extern int (*dmk_par_begin)(void (*job)(void));
extern void (*dmk_par_end)(void);
/* a frame is many milliseconds of drawing on the FM-1: the platform may set dmk_yield, which the frame calls between
 * its parts, where nothing is half done, to render the sound ahead meanwhile (dmk_render from inside dmk_frame) */
extern void (*dmk_yield)(void);

#endif
