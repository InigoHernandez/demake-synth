/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: what moves on Home, over the landscape as drawn (move/screen/lands.py, its coordinates). Small and
 * sparse: water laps, stars and windows blink, snow falls, a beam runs out, reels turn. A few things answer the music:
 * the cabin's three windows are the synths, the road runs and the speakers beat while it plays. */

/* water: the end of each dash of two or more pixels comes and goes, one by one (a single pixel is a post, a trunk: left) */
static void ms_lap(uint8_t *o, int x, int y, int w, int h, uint32_t t)
{
    for (int r = y; r < y + h; r++)
        for (int c = x + w - 1; c > x; c--)
            if (ms_get(o, c, r) && ms_get(o, c - 1, r) && !ms_get(o, c + 1, r)) {
                const uint32_t k = ms_hash((uint32_t)(c + r * 128), t / 4u + (uint32_t)c);
                if (!(k & 3u)) ms_set(o, c, r, 0);
                else if (!(k >> 2 & 3u)) ms_set(o, c + 1, r, 1);
            }
}

/* stars: a few go out for a moment, each at its own time. Those lit in the box, walked to from a place that changes */
static void ms_blink(uint8_t *o, int x, int y, int w, int h, uint32_t t, int n)
{
    for (int i = 0; i < n; i++) {
        const uint32_t k = ms_hash((uint32_t)i, (t + (uint32_t)i * 7u) / 16u);
        int c = x + (int)(k % (uint32_t)w), r = y + (int)((k >> 8) % (uint32_t)h);
        for (int tries = 0; tries < w * h / 2; tries++) {
            if (ms_get(o, c, r)) { ms_set(o, c, r, 0); break; }
            if (++c >= x + w) { c = x; if (++r >= y + h) r = y; }
        }
    }
}

/* windows: a few of a building's places go dark or come on. Its places as lands.py lays them: from (ox, oy), dx and dy
 * apart, to ex from its right and one from its foot, each tall high */
typedef struct { uint8_t x, y, w, h; } ms_box_t;
static void ms_windows(uint8_t *o, const ms_box_t *b, int n, int ox, int oy, int ex, int dx, int dy, int tall, uint32_t t, int count)
{
    for (int i = 0; i < count; i++) {
        const uint32_t k = ms_hash((uint32_t)i, (t + (uint32_t)i * 11u) / 40u);
        const ms_box_t *s = &b[k % (uint32_t)n];
        const int cols = (s->w - ox - ex + dx - 1) / dx, rows = (s->h - 1 - oy + dy - 1) / dy;
        if (cols < 1 || rows < 1) continue;
        ms_rect(o, s->x + ox + (int)((k >> 8) % (uint32_t)cols) * dx, s->y + oy + (int)((k >> 16) % (uint32_t)rows) * dy, 1, tall, (int)(k >> 24 & 1u));
    }
}

/* snow: sparse, slow, each flake with a little dark around it */
static void ms_snow(uint8_t *o, uint32_t t, int n)
{
    for (int i = 0; i < n; i++) {
        const uint32_t k = ms_hash((uint32_t)i, 31u);
        const int y = (int)((t * (2u + (k & 1u)) / 3u + (k >> 4)) % 64u);
        const int x = (int)((k >> 12) % 128u) + ms_wave((int)(t * 2u + k)) * 3 / 128;
        ms_rect(o, x - 1, y - 1, 3, 3, 0);
        ms_set(o, x, y, 1);
    }
}

/* the road's dashes, running towards you */
static void ms_road(uint8_t *o, uint32_t t)
{
    ms_rect(o, 61, 32, 7, 32, 0);
    for (int i = 0; i < 6; i++) {
        const int p = (i * 16 + (int)(t % 16u)) * 6;    /* 0..~570 */
        const int d = p * p / 3600, y = 33 + d, h = 2 + d / 7, w = 1 + d / 12;
        ms_rect(o, 64 - w / 2, y, w, h, 1);
    }
}

/* the deck on the desk, the record beside it */
static void ms_spin(uint8_t *o, int cx, int cy, int r, uint32_t t)
{
    const int a = (int)t * 24;
    ms_set(o, cx + ms_wave(a + 64) * r / 128, cy + ms_wave(a) * r / 128, 1);
}

static void ms_motion_land(uint8_t *o, const mv_view_t *v)
{
    static const ms_box_t ROOM_CITY[8] = {{24, 31, 8, 11}, {33, 25, 6, 17}, {40, 33, 9, 9}, {50, 21, 6, 21},
                                          {57, 29, 9, 13}, {67, 24, 7, 18}, {75, 34, 8, 8}, {84, 28, 12, 14}};
    static const ms_box_t CITY[6] = {{5, 20, 14, 42}, {26, 12, 11, 50}, {47, 22, 14, 40}, {70, 8, 12, 54}, {92, 16, 14, 46}, {114, 26, 11, 36}};
    const uint32_t t = v->t;
    const int play = v->playing, beat = v->step >= 0 && !(v->step & 3);
    switch (v->land % 13) {
    case 0: ms_snow(o, t, 12); break;                   /* Peaks */
    case 1:                                             /* Lake: the cabin's windows, a synth each */
        ms_lap(o, 60, 37, 40, 10, t);
        ms_lap(o, 0, 47, 128, 17, t);
        for (int s = 0; s < 3; s++) ms_rect(o, 30 + s * 3, 36, 2, 2, v->lights >> s & 1);
        break;
    case 2: ms_lap(o, 52, 37, 48, 15, t); break;        /* Coast */
    case 3: ms_blink(o, 0, 0, 80, 14, t, 4); ms_blink(o, 105, 0, 23, 30, t + 5, 2); break;   /* Night */
    case 4:                                             /* Room: the city in the window */
        ms_windows(o, ROOM_CITY, 8, 2, 2, 1, 2, 3, 1, t, 5);
        ms_blink(o, 23, 4, 37, 15, t, 2);
        break;
    case 5:                                             /* Desk: the synths' bars, the deck, the record */
        if (v->lights) {
            static const uint8_t BAR[12] = {5, 9, 12, 7, 4, 8, 13, 10, 6, 3, 7, 11};
            ms_rect(o, 20, 19, 36, 15, 0);
            for (int i = 0; i < 12; i++) {
                const int h = BAR[i] - 2 + (int)(ms_hash((uint32_t)i, t) & 3u);
                ms_rect(o, 20 + i * 3, 33 - h, 2, h, 1);
            }
        }
        if (play) { ms_spin(o, 79, 34, 1, t); ms_spin(o, 91, 34, 1, t); ms_spin(o, 112, 32, 6, t); }
        break;
    case 6: ms_blink(o, 0, 56, 128, 8, t, 8); break;    /* Forest: the meadow's flowers */
    case 7:                                             /* Living: the set's picture, the speakers on the beat */
        ms_lap(o, 44, 32, 32, 8, t);
        if (play && beat) for (int x = 20; x <= 110; x += 90) { ms_ring(o, x, 44, 3, 0); ms_ring(o, x, 44, 4, 1); }
        break;
    case 8: ms_lap(o, 0, 38, 92, 26, t); break;         /* Villa: the pool */
    case 9: if (play) ms_road(o, t); break;             /* California */
    case 10: ms_lap(o, 0, 41, 104, 23, t); break;       /* Harbour */
    case 11:                                            /* Lighthouse: the beam runs out, the sea laps */
        ms_lap(o, 0, 44, 70, 20, t);
        {
            const int gap = 98 - (int)(t % 80u);        /* a dark stretch runs out along the beam, the moon left be; the lamp winks */
            for (int x = gap; x < gap + 10 && x < 98; x++) if (x >= 40) ms_rect(o, x, 0, 1, 34, 0);
            if (t % 80u < 4u) ms_rect(o, 105, 7, 5, 4, 0);
        }
        break;
    case 12:                                            /* City */
        ms_windows(o, CITY, 6, 2, 3, 2, 2, 3, 2, t, 7);
        ms_blink(o, 0, 0, 104, 8, t, 3);
        break;
    default: break;
    }
}
