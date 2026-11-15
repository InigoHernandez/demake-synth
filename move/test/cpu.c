/* SPDX-License-Identifier: GPL-3.0-only */
/* The render's time on the device: a static aarch64 program (move/build.sh; move/cpu.sh runs it on the Move over SSH),
 * built as dsp.so is. Each of heavy.h's heavy cases rendered at full speed in 128-frame blocks, each block timed: mean,
 * p99 and the most, in microseconds and as a share of a block's real time (2902 us). It runs beside Move's own work, at
 * no special priority, so the mean and the p99 are what to read; the most can be another process's. Pinned to a core if
 * one is given (cpu [SECONDS] [CORE]). */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dmk_core.c"
#include "heavy.h"

static int cmp(const void *a, const void *b) { const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }

int main(int argc, char **argv)
{
    static int16_t b[256];
    const double sec = argc > 1 ? atof(argv[1]) : 20;
    if (argc > 2) {
        cpu_set_t s;
        CPU_ZERO(&s);
        CPU_SET(atoi(argv[2]), &s);
        if (sched_setaffinity(0, sizeof s, &s)) perror("sched_setaffinity");
    }
    const int n = (int)(sec * 44100 / 128);
    uint32_t *t = malloc((size_t)n * sizeof *t);
    printf("%-26s %8s %8s %8s %8s  of a block\n", "case", "mean us", "p99 us", "most us", "mean %");
    for (int i = 0; i < HC_HEAVY; i++) {
        const char *nm = heavy_case(i);
        for (int k = 0; k < 100; k++) dmk_render(b, 128);
        double sum = 0;
        for (int k = 0; k < n; k++) {
            struct timespec a, c;
            clock_gettime(CLOCK_MONOTONIC, &a);
            dmk_render(b, 128);
            clock_gettime(CLOCK_MONOTONIC, &c);
            t[k] = (uint32_t)((c.tv_sec - a.tv_sec) * 1000000000LL + (c.tv_nsec - a.tv_nsec));
            sum += t[k];
        }
        qsort(t, (size_t)n, sizeof *t, cmp);
        const double mean = sum / n / 1000;
        printf("%-26s %8.0f %8.0f %8.0f %7.1f%%\n", nm, mean, t[n * 99 / 100] / 1000.0, t[n - 1] / 1000.0, mean / 29.02);
        fflush(stdout);
    }
    return 0;
}
