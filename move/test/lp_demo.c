/* SPDX-License-Identifier: GPL-3.0-only */
/* A loop as the owner will hear it (move/dmk_loop.c), in one WAV: two bars of Demake's first song recorded into Loop 1
 * on synth 4 (the mix of the other three, with their own effects), the song's synths then muted and the loop played:
 * as recorded, at half speed, reversed, its filter swept by its LFO, then overdubbed with a line on synth 1 (75% of the
 * old layer kept) and played back. Each part two bars.
 *   lp_demo FILE.wav      (built as move/test/ym_patches.c is: with move/dmk_ymfm.cpp's object, linked with c++) */
#include <stdio.h>
#include <stdlib.h>
#include "dmk_core.c"

#define V(s, pg, r, k) ui.val[s][pg][r][k]
enum { LD_SY = DMK_SYNTHS - 1, LD_RUN = 128, LD_PARTS = 7, LD_BARS = 2, LD_MAX_S = 120 };

static int16_t *out;
static int at, cap;
static int64_t bar_frames(void) { return (int64_t)LP_STEPS_BAR * seq_per() / 256; }
static void run_bars(int bars)
{
    const int64_t n = bars * bar_frames();
    for (int64_t i = 0; i < n && at + LD_RUN <= cap; i += LD_RUN, at += LD_RUN) dmk_render(&out[at * 2], LD_RUN);
}
static void wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t data = (uint32_t)at * 4u, sr = DMK_SR, br = DMK_SR * 4u, fmt = 16, riff = data + 36u;
    const uint16_t pcm = 1, chn = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(out, 4, (size_t)at, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "lp_demo FILE.wav\n"); return 2; }
    cap = LD_MAX_S * DMK_SR;
    out = calloc((size_t)cap * 2u, sizeof *out);
    dmk_init(7);
    V(LD_SY, PG_SOUND, 0, 0) = DMK_SAMPLES_N + YM_PATCHES;   /* synth 4 on Loop 1, its filter open */
    V(LD_SY, PG_FILTER, 0, 4) = choice_val(FT_LP24, 8); V(LD_SY, PG_FILTER, 0, 0) = 127; V(LD_SY, PG_FILTER, 0, 1) = 40;
    sp_sync();
    lpv[LD_SY][LV_BARS] = 1;                            /* two bars */
    sq[LD_SY].count = 0;
    seq_play(1);
    lp_rec_press(LD_SY);                                /* on the first bar */
    run_bars(LD_BARS);                                  /* 1: the song, recorded */
    pat_mute = (uint8_t)((1u << LD_SY) - 1u);           /* the song's synths muted, their voices let go */
    for (int v = 0; v < DMK_VOICES; v++) if (vc[v].sy != LD_SY) { vc[v].on = 0; if (vc[v].stage) vc[v].stage = 3; }
    run_bars(LD_BARS);                                  /* 2: the loop as recorded */
    lpv[LD_SY][LV_SPEED] = 1; run_bars(LD_BARS);        /* 3: half speed */
    lpv[LD_SY][LV_SPEED] = LP_SPEED_ONE; lpv[LD_SY][LV_REV] = 1; run_bars(LD_BARS);   /* 4: reversed */
    lpv[LD_SY][LV_REV] = 0;                             /* 5: its filter swept by its LFO */
    V(LD_SY, PG_FILTER, 0, 0) = 70; V(LD_SY, PG_FILTER, 0, 1) = 90;
    V(LD_SY, PG_LFO, 0, 0) = 70; V(LD_SY, PG_LFO, 0, 1) = 110; V(LD_SY, PG_LFO, 0, 4) = choice_val(LT_CUTOFF, 5);
    sp_sync();
    run_bars(LD_BARS);
    V(LD_SY, PG_FILTER, 0, 0) = 127; V(LD_SY, PG_FILTER, 0, 1) = 40; V(LD_SY, PG_LFO, 0, 1) = 0;
    sp_sync();
    pat_mute = (uint8_t)((1u << LD_SY) - 1u - 1u);      /* 6: synth 1 back with a line, overdubbed, 75% kept */
    lpv[LD_SY][LV_KEEP] = LP_KEEP_ALL * 3 / 4;
    lp_rec_press(LD_SY);
    {
        static const uint8_t LINE[8] = {69, 72, 76, 79, 81, 79, 76, 72};
        const int64_t step = bar_frames() / 4;
        for (int n = 0; n < 2 * 8; n++) {               /* (a quarter note each, the overdub starts on the next bar) */
            note_on_vel(0, LINE[n % 8], SRC_KEY, 100);
            for (int64_t i = 0; i < step && at + LD_RUN <= cap; i += LD_RUN, at += LD_RUN) dmk_render(&out[at * 2], LD_RUN);
            note_off_src(0, LINE[n % 8], SRC_KEY);
        }
    }
    pat_mute = (uint8_t)((1u << LD_SY) - 1u);           /* 7: the loop with its new layer */
    for (int v = 0; v < DMK_VOICES; v++) if (vc[v].sy != LD_SY) { vc[v].on = 0; if (vc[v].stage) vc[v].stage = 3; }
    run_bars(LD_BARS);
    wav(argv[1]);
    printf("%s: %.1f s, a bar %.2f s, loop 1 %u frames, state %d\n", argv[1], at / (double)DMK_SR, bar_frames() / (double)DMK_SR,
           lps[0].len, lps[0].state);
    return 0;
}
