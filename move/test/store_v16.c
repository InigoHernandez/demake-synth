/* SPDX-License-Identifier: GPL-3.0-only */
/* A store as the Move's builds before four synths wrote it (format 16: three synths, 256 KiB a song): proof.sh builds
 * this file against commit c6b82ab's move/ and firmware/ (git archive), for move/test/inside.c to open. Each of the
 * three synths gets 40 notes, synth s's at 40 + 12 s + step % 12.
 *   store_v16 DIR */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include "../dmk_move.c"

int main(int argc, char **argv)
{
    static int16_t out[256];
    char js[600];
    if (argc < 2) return 2;
    snprintf(js, sizeof js, "{\"seed\": 7, \"data_dir\": \"%s\", \"save_on_close\": 0, \"draw_thread\": 0}", argv[1]);
    void *i = v2_create("", js);
    for (int s = 0; s < DMK_SYNTHS; s++) {
        sq[s].count = 0;
        for (int t = 0; t < 40; t++) seq_add(s, t, 2, 40 + s * 12 + t % 12, 90);
    }
    if (song_save(0)) { puts("store_v16: not saved"); return 1; }
    for (int k = 0; k < 400 && !atomic_load(&sv.done); k++) { v2_render(i, out, 128); usleep(2000); }
    v2_destroy(i);
    return 0;
}
