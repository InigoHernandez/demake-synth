/* SPDX-License-Identifier: GPL-3.0-only */
/* A store as the FM-1's build writes it (compiled without DMK_MOVE, so the format is the real one, not a copy of it):
 * the file the Move's first builds kept, for move/test/inside.c and the bench to open.
 *   old_store FILE */
#include <stdio.h>
#include <string.h>
#include "dmk_core.c"

static uint8_t st[65536];
static int rd(uint32_t at, void *p, uint32_t n) { memcpy(p, st + at, n); return 0; }
static int wr(uint32_t at, const void *p, uint32_t n) { memset(st + at, 0xFF, 4096); memcpy(st + at, p, n); return 0; }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    memset(st, 0xFF, sizeof st);
    dmk_store_read = rd;
    dmk_store_write = wr;
    dmk_init(7);
    ui.val[0][PG_CHORD][0][4] = choice_val(15, 20);     /* Night drive */
    ui.val[0][PG_CHORD][1][1] = choice_val(4, 6);       /* Mixolydian */
    ui.val[0][PG_CHORD][2][1] = choice_val(5, 8);       /* Sus4 */
    sp_sync();
    sq[0].count = 0;
    for (int i = 0; i < 23; i++) seq_add(0, i, 1, 60 + i % 12, 100);
    scn[1].used = 1; scn[1].pat[0] = 1; scn[1].prog = choice_val(17, 20); scn[1].mute = 0;   /* User 2 */
    if (song_save(0)) { puts("old_store: not saved"); return 1; }
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(st, sizeof st, 1, f) != 1) return 1;
    return fclose(f) != 0;
}
