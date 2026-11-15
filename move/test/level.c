/* SPDX-License-Identifier: GPL-3.0-only */
/* What 40 voices do to the level: the same chords, more and more notes, a synth each or all on one, the first song's sound,
 * the loudest recording, the heaviest effects. The output's RMS and peak, how much of it sat at the top, how long the
 * limiter's knee was passed on the way (a synth's chain, the mix), how long the rider held it down.
 *   level                      (gcc -std=gnu11 -O2 -DDMK_MOVE=1 -DDMK_METER -fsigned-char -fwrapv -Ifirmware/src -Ifirmware/gen -Imove/test move/test/level.c -lm) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmk_core.c"
#include "heavy.h"

enum { F_PLAIN, F_HEAVY };
#define SEC 4
static int16_t b[256];

static void setup(int fx, int sample)
{
    if (fx == F_HEAVY) heavy_base(); else dmk_init(7);
    sustain_all();
    if (sample >= 0) for (int s = 0; s < DMK_SYNTHS; s++) V(s, PG_SOUND, 0, 0) = (uint8_t)sample;
    sp_sync();
}
static void chord(int n, int one) { for (int i = 0; i < n; i++) note_on_s(one ? 0 : i % DMK_SYNTHS, 36 + i * 7 % 60); }

typedef struct { double rms, peak, top, ride, knee, bend; int sounding; } res_t;
static res_t run(int fx, int sample, int n, int one)
{
    res_t r;
    setup(fx, sample);
    chord(n, one);
    for (int k = 0; k < 400; k++) dmk_render(b, 128);
    memset(&dmeter, 0, sizeof dmeter);
    const uint32_t l0 = gm_loud[0];
    const int blocks = SEC * 44100 / 128;
    double sum = 0, pk = 0;
    uint32_t top = 0;
    for (int k = 0; k < blocks; k++) {
        dmk_render(b, 128);
        for (int i = 0; i < 256; i++) {
            const double a = fabs((double)b[i]);
            sum += a * a;
            if (a > pk) pk = a;
            top += a >= 32000;
        }
    }
    const double total = blocks * 256.0;
    r.rms = 10 * log10(sum / total / (32768.0 * 32768.0) + 1e-12);
    r.peak = 20 * log10(pk / 32768.0 + 1e-9);
    r.top = 100 * top / total;
    r.ride = 100.0 * (gm_loud[0] - l0) / (total / 2);
    r.knee = 100.0 * dmeter.knee / total;
    r.bend = 100.0 * dmeter.bend / (total / 2);
    r.sounding = 0;
    for (int v = 0; v < DMK_VOICES; v++) r.sounding += vc[v].stage != 0;
    return r;
}
/* what the limiters add: the same notes 18 dB down at each voice, past them untouched, against the notes as they are. All
 * that does not scale is theirs (dB under the sound) */
#define TAKE (2 * 44100 * 2)
static void take(float *o, int fx, int sample, int n, int one, int quiet)
{
    setup(fx, sample);
    chord(n, one);
    if (quiet) for (int v = 0; v < DMK_VOICES; v++) if (vc[v].stage) vc[v].vg = 4096;
    for (int k = 0; k < 200; k++) dmk_render(b, 128);
    for (int at = 0; at < TAKE; at += 256) { dmk_render(b, 128); for (int i = 0; i < 256; i++) o[at + i] = b[i]; }
}
static double added(int fx, int sample, int n, int one)
{
    static float x[TAKE + 256], y[TAKE + 256];
    take(x, fx, sample, n, one, 0);
    take(y, fx, sample, n, one, 1);
    double xy = 0, yy = 0, xx = 0, rr = 0;
    for (int i = 0; i < TAKE; i++) { xy += x[i] * (double)y[i]; yy += y[i] * (double)y[i]; xx += x[i] * (double)x[i]; }
    const double g = xy / yy;
    for (int i = 0; i < TAKE; i++) { const double e = x[i] - g * y[i]; rr += e * e; }
    return 10 * log10(rr / xx + 1e-12);
}
static void row(const char *what, int fx, int sample, int one)
{
    static const int N[] = {12, 24, 40};
    printf("%s\n  notes  sounding   RMS dB  peak dB  at top %%  rider %%  mix knee %%  chain knee %%  added dB\n", what);
    for (unsigned k = 0; k < sizeof N / sizeof N[0]; k++) {
        const res_t r = run(fx, sample, N[k], one);
        printf("  %5d  %8d  %7.1f  %7.1f  %8.2f  %7.1f  %10.1f  %12.2f  %8.1f\n", N[k], r.sounding, r.rms, r.peak, r.top, r.ride, r.knee, r.bend, added(fx, sample, N[k], one));
        fflush(stdout);
    }
}

int main(void)
{
    int loud = 0;
    double most = -200;
    for (int i = 0; i < DMK_SAMPLES_N; i++) {
        const res_t r = run(F_PLAIN, i, 12, 0);
        if (r.rms > most) { most = r.rms; loud = i; }
    }
    printf("the loudest recording at 12 voices: %d, %s, %.1f dB\n\n", loud, DMK_SAMPLES[loud].name, most);
    row("the first song's sounds, a synth each", F_PLAIN, -1, 0);
    row("the first song's sounds, all on synth 1", F_PLAIN, -1, 1);
    row("the loudest recording, a synth each", F_PLAIN, loud, 0);
    row("the loudest recording, all on synth 1", F_PLAIN, loud, 1);
    row("the heaviest effects, a synth each", F_HEAVY, loud, 0);
    return 0;
}
