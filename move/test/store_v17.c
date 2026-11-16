/* SPDX-License-Identifier: GPL-3.0-only */
/* A store as the Move's builds before each pattern kept its sound wrote it (format 17: four synths, the sound the
 * song's): proof.sh builds this file against commit 982fde6's move/ and firmware/ (git archive), for
 * move/test/inside.c to open. Synth 2: notes on its patterns 1 and 3, its pattern 4 kept empty, cutoff 33 and level
 * 99; synth 1 on the chords with its roll empty.
 *   store_v17 DIR */
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
    syn_pat_set(1, 2);
    sq[1].count = 0;
    for (int t = 0; t < 8; t++) seq_add(1, t, 2, 50 + t, 90);
    syn_pat_set(1, 3);
    sq[1].count = 0;
    syn_pat_set(1, 0);
    sq[1].count = 0;
    for (int t = 0; t < 8; t++) seq_add(1, t, 2, 60 + t % 5, 90);
    ui.val[1][PG_FILTER][0][0] = 33; ui.val[1][PG_AMP][0][4] = 99;
    ch.follow[0] = FW_PLAYS; sq[0].count = 0;
    if (song_save(0)) { puts("store_v17: not saved"); return 1; }
    for (int k = 0; k < 400 && !atomic_load(&sv.done); k++) { v2_render(i, out, 128); usleep(2000); }
    v2_destroy(i);
    return 0;
}
