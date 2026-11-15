/* SPDX-License-Identifier: GPL-3.0-only */
/* The core's render as bits: each case of heavy.h for 10 s in 128-frame blocks, FNV-1a over the PCM, printed. move/proof.sh
 * builds it on the Mac and in the Move's container and compares, and holds dsp.so's first song to the Mac's, built for the
 * Move (its 40 voices let tails ring that the FM-1's twelve would take) */
#include <stdio.h>
#include <stdlib.h>
#include "dmk_core.c"
#include "heavy.h"

int main(int argc, char **argv)
{
    static int16_t b[256];
    uint32_t all = 2166136261u;
    const int cases = argc > 1 ? atoi(argv[1]) : 1 << 20;   /* (the first few: the Move's build, against dsp.so) */
    for (int i = 0; i < cases; i++) {
        const char *nm = heavy_case(i);
        if (!nm) break;
        uint32_t h = 2166136261u;
        for (int k = 0; k < 10 * 44100 / 128; k++) {
            dmk_render(b, 128);
            for (int j = 0; j < 512; j++) h = (h ^ ((uint8_t *)b)[j]) * 16777619u;
        }
        printf("%-32s %08x\n", nm, h);
        fflush(stdout);
        all = (all ^ h) * 16777619u;
    }
    printf("%-32s %08x\n", "all", all);
    return 0;
}
