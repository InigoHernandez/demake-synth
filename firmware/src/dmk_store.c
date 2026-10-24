/* SPDX-License-Identifier: GPL-3.0-only */
/* The store (docs/plan.md, 2.6): the platform's 64 KiB, sixteen sectors of 4 KiB. Sector 0 is kept for settings;
 * sector 1 is the library, 32 USER sounds of 64 bytes; sectors 2-15 are 14 songs, one a sector. A record is a header
 * (magic, length, CRC-32 of what follows, its generation) and its bytes; the platform writes the header last, so a
 * save cut short leaves the sector unreadable as a record, never half of one. A song is the whole of what plays:
 * every control (the sounds, the effects, the key and the chords, the tempo), the three rolls, the four USER
 * progressions, how each synth follows the chords, the rolls' views, the theme. At start the newest song loads.
 * Erasing a sector stops the sound for ~50 ms (the FM-1), so the UI saves only while stopped. Integer only */

#define ST_SECTOR 4096u
#define ST_HDR 16u
#define ST_LIB 0x1000u                                  /* the library's sector */
#define ST_SONG0 0x2000u                                /* the first song's */
#define ST_SONGS 14
#define ST_USER 32                                      /* the library's sounds */
#define ST_SOUND 64u                                    /* bytes a sound */
#define ST_MAGIC_LIB 0x4C4B4D44u                        /* "DMKL" */
#define ST_MAGIC_SONG 0x4E4B4D44u                       /* "DMKN" */
#define ST_VER 1u

static uint8_t st_buf[ST_SECTOR] POOL;                  /* a sector as it is written or was read */
static uint8_t lib[ST_USER][ST_SOUND] POOL;             /* the library, as the flash holds it: [used, 0, 45 values] */
static uint32_t st_gen;                                 /* the newest song's generation */
static uint32_t song_gen[ST_SONGS];                     /* each slot's (0: empty) */
static uint8_t song_prog[ST_SONGS];                     /* each slot's progression, for its name */

/* a sound: the rows a synth owns (SOUND, LAYER, FILTER, AMP, the four FX rows, LFO), five values each */
static const uint8_t SND_ROWS[9][2] = {{PG_SOUND, 0}, {PG_SOUND, 1}, {PG_FILTER, 0}, {PG_AMP, 0}, {PG_FX, 0},
                                       {PG_FX, 1}, {PG_FX, 2}, {PG_FX, 3}, {PG_LFO, 0}};

static int store_on(void) { return dmk_store_read && dmk_store_write; }

static uint32_t st_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static void st_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t st_get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* a record at at into st_buf: 0 and its length, or -1 (none there, another firmware's, or damaged) */
static int st_load(uint32_t at, uint32_t magic, uint32_t *len, uint32_t *gen)
{
    uint32_t n;
    if (!store_on() || dmk_store_read(at, st_buf, ST_HDR) || st_get32(st_buf) != magic) return -1;
    n = st_get32(st_buf + 4);
    if (n > ST_SECTOR - ST_HDR || (st_get32(st_buf + 12) & 0xFFFFu) != ST_VER) return -1;
    if (dmk_store_read(at + ST_HDR, st_buf + ST_HDR, n) || st_crc(st_buf + ST_HDR, n) != st_get32(st_buf + 8)) return -1;
    *len = n;
    if (gen) *gen = st_get32(st_buf + 12) >> 16;
    return 0;
}
/* st_buf's record of n bytes to at; 0: written */
static int st_save(uint32_t at, uint32_t magic, uint32_t n, uint32_t gen)
{
    if (!store_on() || n > ST_SECTOR - ST_HDR) return -1;
    st_put32(st_buf, magic);
    st_put32(st_buf + 4, n);
    st_put32(st_buf + 8, st_crc(st_buf + ST_HDR, n));
    st_put32(st_buf + 12, gen << 16 | ST_VER);
    return dmk_store_write(at, st_buf, ST_HDR + n);
}

/* ---- the library: USER sounds */
static int lib_count(void) { int i, n = 0; for (i = 0; i < ST_USER; i++) n += lib[i][0]; return n; }
static int lib_nth(int n)                               /* the slot of the n-th sound saved, or -1 */
{
    int i;
    for (i = 0; i < ST_USER; i++) if (lib[i][0] && !n--) return i;
    return -1;
}
static void lib_read(void)
{
    uint32_t n, i;
    for (i = 0; i < ST_USER; i++) lib[i][0] = 0;
    if (st_load(ST_LIB, ST_MAGIC_LIB, &n, 0) || n != ST_USER * ST_SOUND) return;
    for (i = 0; i < ST_USER * ST_SOUND; i++) lib[i / ST_SOUND][i % ST_SOUND] = st_buf[ST_HDR + i];
}
static void sound_put(int s, uint8_t *o)                /* synth s's sound into 45 bytes */
{
    int r, k;
    for (r = 0; r < 9; r++) for (k = 0; k < 5; k++) o[r * 5 + k] = ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k];
}
static void sound_get(int s, const uint8_t *v)
{
    int r, k;
    for (r = 0; r < 9; r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = v[r * 5 + k];
    sp_sync();
}
/* synth s's sound into the library's first free slot: its number (1..32), 0 full, -1 not written */
static int sound_save(int s)
{
    int i, j;
    for (i = 0; i < ST_USER && lib[i][0]; i++) {}
    if (i == ST_USER) return 0;
    lib[i][0] = 1; lib[i][1] = 0;
    sound_put(s, &lib[i][2]);
    for (j = 0; j < (int)(ST_USER * ST_SOUND); j++) st_buf[ST_HDR + j] = lib[j / ST_SOUND][j % ST_SOUND];
    if (st_save(ST_LIB, ST_MAGIC_LIB, ST_USER * ST_SOUND, 0)) { lib[i][0] = 0; return -1; }
    return i + 1;
}
static void lib_load(int s, int slot) { if (slot >= 0 && slot < ST_USER && lib[slot][0]) sound_get(s, &lib[slot][2]); }

/* INIT: the synth's sound as the first song has it (its recording kept), every effect but its tape off */
static void sound_init(int s)
{
    uint8_t keep = ui.val[s][PG_SOUND][0][0];
    int r, k;
    for (r = 0; r < 9; r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = ui_first[s][r * 5 + k];
    ui.val[s][PG_SOUND][0][0] = keep;
    sp_sync();
}

/* ---- songs */
static uint32_t song_at(int i) { return ST_SONG0 + (uint32_t)i * ST_SECTOR; }

static int song_pack(uint8_t *o)                        /* the song open into o: its length, or -1 too long */
{
    uint8_t *p = o;
    int s, i, k, r, g;
    *p++ = PG_COUNT;
    for (s = 0; s < DMK_SYNTHS; s++) for (g = 0; g < PG_COUNT; g++) for (r = 0; r < 4; r++) for (k = 0; k < 5; k++) *p++ = ui.val[s][g][r][k];
    *p++ = theme;
    for (s = 0; s < DMK_SYNTHS; s++) *p++ = ch.follow[s];
    for (i = 0; i < 4; i++) {
        *p++ = uprog[i].mode; *p++ = uprog[i].voicing; *p++ = uprog[i].n;
        for (k = 0; k < PROG_SLOTS; k++) { *p++ = uprog[i].s[k].root; *p++ = uprog[i].s[k].q; *p++ = uprog[i].s[k].half; *p++ = uprog[i].s[k].pedal; }
    }
    for (s = 0; s < DMK_SYNTHS; s++) { *p++ = ui.roll_cur[s]; *p++ = ui.roll_view[s]; *p++ = ui.roll_pen[s]; *p++ = ui.roll_zoom[s]; }
    for (s = 0; s < DMK_SYNTHS; s++) {
        *p++ = sq[s].steps; *p++ = sq[s].count;
        if (p - o + sq[s].count * 4 > (int)(ST_SECTOR - ST_HDR)) return -1;
        for (i = 0; i < sq[s].count; i++) { *p++ = sq[s].n[i].step; *p++ = sq[s].n[i].len; *p++ = sq[s].n[i].note; *p++ = sq[s].n[i].vel; }
    }
    return (int)(p - o);
}

static int song_unpack(const uint8_t *p, uint32_t n)    /* 0: the song open is the one in p */
{
    const uint8_t *e = p + n;
    int s, i, k, r, g;
    if (n < 1 || *p++ != PG_COUNT) return -1;
    if (n < 1u + DMK_SYNTHS * PG_COUNT * 20u + 1u + DMK_SYNTHS + 4u * (3u + PROG_SLOTS * 4u) + DMK_SYNTHS * 4u + DMK_SYNTHS * 2u) return -1;
    seq_play(0);
    for (s = 0; s < DMK_SYNTHS; s++) for (g = 0; g < PG_COUNT; g++) for (r = 0; r < 4; r++) for (k = 0; k < 5; k++) ui.val[s][g][r][k] = *p++;
    theme = *p++ % DMK_THEMES;
    for (s = 0; s < DMK_SYNTHS; s++) { ch.follow[s] = *p++; if (ch.follow[s] >= FW_COUNT) ch.follow[s] = FW_FREE; }
    for (i = 0; i < 4; i++) {
        uprog[i].mode = *p++; uprog[i].voicing = *p++; uprog[i].n = *p++;
        if (uprog[i].n > PROG_SLOTS) uprog[i].n = 0;
        for (k = 0; k < PROG_SLOTS; k++) { uprog[i].s[k].root = *p++; uprog[i].s[k].q = *p++; uprog[i].s[k].half = *p++; uprog[i].s[k].pedal = *p++; }
    }
    for (s = 0; s < DMK_SYNTHS; s++) { ui.roll_cur[s] = *p++; ui.roll_view[s] = *p++; ui.roll_pen[s] = *p++; ui.roll_zoom[s] = *p++; }
    for (s = 0; s < DMK_SYNTHS; s++) {
        int st, c;
        if (e - p < 2) return -1;
        st = *p++; c = *p++;
        sq[s].steps = (uint8_t)(st < 1 ? 1 : st > SEQ_STEPS ? SEQ_STEPS : st);
        sq[s].count = 0;
        for (i = 0; i < c && e - p >= 4; i++, p += 4) if (sq[s].count < SEQ_NOTES && p[0] < sq[s].steps) sq[s].n[sq[s].count++] = (snote_t){p[0], p[1], p[2], p[3]};
        sq[s].pos = 0;
        if (ui.roll_cur[s] >= sq[s].steps) ui.roll_cur[s] = 0;
    }
    for (s = 0; s < DMK_SYNTHS; s++) {
        if (ui.val[s][PG_SOUND][0][0] >= DMK_SAMPLES_N) ui.val[s][PG_SOUND][0][0] = 0;
    }
    chord_quiet();
    chord_start();
    sp_sync();
    return 0;
}

static void songs_scan(void)                            /* which slots hold a song, and how new each is */
{
    int i;
    uint32_t n, g;
    st_gen = 0;
    for (i = 0; i < ST_SONGS; i++) {
        song_gen[i] = 0;
        if (st_load(song_at(i), ST_MAGIC_SONG, &n, &g)) continue;
        song_gen[i] = g ? g : 1;
        /* the progression's place, for the slot's name: the CHORD page's value, packed after the counts */
        song_prog[i] = st_buf[ST_HDR + 1 + PG_CHORD * 20 + 4];
        if (g > st_gen) st_gen = g;
    }
}
static int song_used(int i) { return i >= 0 && i < ST_SONGS && song_gen[i]; }
static const char *song_name(int i) { return PROGS[choice_index(choice_of(PG_CHORD, 0, 4), song_prog[i])]; }

/* the song open into slot i: 0 saved, -1 not (no store, too long, the write failed) */
static int song_save(int i)
{
    int n = song_pack(st_buf + ST_HDR);
    if (n < 0 || st_save(song_at(i), ST_MAGIC_SONG, (uint32_t)n, (st_gen + 1u) & 0xFFFFu)) return -1;
    st_gen = (st_gen + 1u) & 0xFFFFu;
    song_gen[i] = st_gen ? st_gen : 1;
    song_prog[i] = ui.val[0][PG_CHORD][0][4];
    return 0;
}
static int song_load(int i)
{
    uint32_t n;
    if (i < 0 || i >= ST_SONGS || st_load(song_at(i), ST_MAGIC_SONG, &n, 0)) return -1;
    return song_unpack(st_buf + ST_HDR, n);
}
static int song_erase(int i)                            /* an empty record: the slot reads as empty */
{
    if (!store_on()) return -1;
    st_put32(st_buf, 0); st_put32(st_buf + 4, 0); st_put32(st_buf + 8, 0); st_put32(st_buf + 12, 0);
    if (dmk_store_write(song_at(i), st_buf, ST_HDR)) return -1;
    song_gen[i] = 0;
    return 0;
}
static void song_new(void)                              /* the first song: its rolls, its chords, its controls */
{
    int i, th = theme;
    seq_play(0);
    seq_init();
    for (i = 0; i < 4; i++) uprog[i].n = 0;
    chord_init();
    ui_init();
    theme = (uint8_t)th;
    sp_sync();
}

static void store_boot(void)                            /* at start: the library, and the newest song */
{
    int i, best = -1;
    if (!store_on()) return;
    lib_read();
    songs_scan();
    for (i = 0; i < ST_SONGS; i++) if (song_gen[i] && (best < 0 || song_gen[i] > song_gen[best])) best = i;
    if (best >= 0 && !song_load(best)) ui.song = (uint8_t)best;
}
