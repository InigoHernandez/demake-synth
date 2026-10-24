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
#define ST_VER 5u   /* 5: the layer, the voice, the filter's drive, the LFO heard; 4: each synth's patterns, the scenes, a song
                       of scenes; 3: patterns and their song; 2: patterns; 1: rolls */

static uint8_t st_buf[ST_SECTOR] POOL;                  /* a sector as it is written or was read */
static uint8_t lib[ST_USER][ST_SOUND] POOL;             /* the library, as the flash holds it: [used, its form, the values]:
                                                           form 0, 45 values (before 0.4); 1, 55 */
static uint32_t st_gen;                                 /* the newest song's generation */
static uint32_t song_gen[ST_SONGS];                     /* each slot's (0: empty) */
static uint8_t song_prog[ST_SONGS];                     /* each slot's progression, for its name */

/* a sound: the rows a synth owns (SOUND, LAYER, FILTER, AMP, the four FX rows, LFO; then VOICE and the LFO's sync,
 * after the first nine so a sound of before 0.4 is these nine), five values each */
#define SND_ROWS_N 11
static const uint8_t SND_ROWS[SND_ROWS_N][2] = {{PG_SOUND, 0}, {PG_SOUND, 1}, {PG_FILTER, 0}, {PG_AMP, 0}, {PG_FX, 0},
                                                {PG_FX, 1}, {PG_FX, 2}, {PG_FX, 3}, {PG_LFO, 0}, {PG_SOUND, 2}, {PG_LFO, 1}};

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
static uint32_t st_ver;                                 /* the last record read's version */
static int st_load(uint32_t at, uint32_t magic, uint32_t *len, uint32_t *gen)
{
    uint32_t n;
    if (!store_on() || dmk_store_read(at, st_buf, ST_HDR) || st_get32(st_buf) != magic) return -1;
    n = st_get32(st_buf + 4);
    st_ver = st_get32(st_buf + 12) & 0xFFFFu;
    if (n > ST_SECTOR - ST_HDR || st_ver < 1u || st_ver > ST_VER) return -1;
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
static void sound_put(int s, uint8_t *o)                /* synth s's sound into SND_BYTES */
{
    int r, k;
    for (r = 0; r < SND_ROWS_N; r++) for (k = 0; k < 5; k++) o[r * 5 + k] = ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k];
}
static void sound_get(int s, const uint8_t *v, int form)   /* form 0: a sound of before 0.4, its new controls off */
{
    int r, k;
    for (r = 0; r < (form ? SND_ROWS_N : 9); r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = v[r * 5 + k];
    if (!form) snd_new_defaults(s);                     /* its layer, voice, drive and LFO did nothing then: off */
    sp_sync();
}
/* synth s's sound into the library's first free slot: its number (1..32), 0 full, -1 not written */
static int sound_save(int s)
{
    int i, j;
    for (i = 0; i < ST_USER && lib[i][0]; i++) {}
    if (i == ST_USER) return 0;
    lib[i][0] = 1; lib[i][1] = 1;
    sound_put(s, &lib[i][2]);
    for (j = 0; j < (int)(ST_USER * ST_SOUND); j++) st_buf[ST_HDR + j] = lib[j / ST_SOUND][j % ST_SOUND];
    if (st_save(ST_LIB, ST_MAGIC_LIB, ST_USER * ST_SOUND, 0)) { lib[i][0] = 0; return -1; }
    return i + 1;
}
static void lib_load(int s, int slot) { if (slot >= 0 && slot < ST_USER && lib[slot][0]) sound_get(s, &lib[slot][2], lib[slot][1]); }

/* INIT: the synth's sound as the first song has it (its recording kept), every effect but its tape off */
static void sound_init(int s)
{
    uint8_t keep = ui.val[s][PG_SOUND][0][0];
    int r, k;
    for (r = 0; r < SND_ROWS_N; r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = ui_first[s][r * 5 + k];
    ui.val[s][PG_SOUND][0][0] = keep;
    ui.val[s][PG_SOUND][1][4] = keep;                   /* its layer starts as its own recording */
    sp_sync();
}

/* ---- songs */
static uint32_t slot_at(int i) { return ST_SONG0 + (uint32_t)i * ST_SECTOR; }

static int song_pack(uint8_t *o)                        /* the song open into o: its length, or -1 too long */
{
    uint8_t *p = o;
    int s, i, k, r, g;
    *p++ = PG_SAVED;
    for (s = 0; s < DMK_SYNTHS; s++) for (g = 0; g < PG_SAVED; g++) for (r = 0; r < 4; r++) for (k = 0; k < 5; k++) *p++ = ui.val[s][g][r][k];
    *p++ = theme;
    for (s = 0; s < DMK_SYNTHS; s++) *p++ = ch.follow[s];
    for (i = 0; i < 4; i++) {
        *p++ = uprog[i].mode; *p++ = uprog[i].voicing; *p++ = uprog[i].n;
        for (k = 0; k < PROG_SLOTS; k++) { *p++ = uprog[i].s[k].root; *p++ = uprog[i].s[k].q; *p++ = uprog[i].s[k].half; *p++ = uprog[i].s[k].pedal; }
    }
    for (s = 0; s < DMK_SYNTHS; s++) { *p++ = ui.roll_cur[s]; *p++ = ui.roll_view[s]; *p++ = ui.roll_pen[s]; *p++ = ui.roll_zoom[s]; }
    scn_keep();                                         /* the open scene with the others */
    *p++ = scn_cur;
    for (s = 0; s < DMK_SYNTHS; s++)                    /* each synth's patterns */
        for (g = 0; g < SYN_PATS; g++) {
            const pseq_t *q = &spat[s][g];
            *p++ = q->used;
            if (!q->used) continue;
            *p++ = q->steps; *p++ = q->count;
            if (p - o + q->count * 4 > (int)(ST_SECTOR - ST_HDR)) return -1;
            for (i = 0; i < q->count; i++) { *p++ = q->n[i].step; *p++ = q->n[i].len; *p++ = q->n[i].note; *p++ = q->n[i].vel; }
        }
    if (p - o + SCENES * 6 + 3 + song_n * 2 > (int)(ST_SECTOR - ST_HDR)) return -1;
    for (g = 0; g < SCENES; g++) {                      /* the scenes */
        *p++ = scn[g].used;
        if (!scn[g].used) continue;
        for (s = 0; s < DMK_SYNTHS; s++) *p++ = scn[g].pat[s];
        *p++ = scn[g].prog; *p++ = scn[g].mute;
    }
    *p++ = song_n; *p++ = song_end; *p++ = song_mode;   /* the song: its steps, scenes for bars */
    for (i = 0; i < song_n; i++) { *p++ = song[i].scene; *p++ = song[i].bars; }
    for (s = 0; s < DMK_SYNTHS; s++) *p++ = (uint8_t)ch.hint[s];   /* the harmonies' intervals */
    return (int)(p - o);
}

static int seq_unpack(pseq_t *q, const uint8_t **pp, const uint8_t *e)   /* a roll: its length, its notes */
{
    const uint8_t *p = *pp;
    int st, c, i;
    if (e - p < 2) return -1;
    st = *p++; c = *p++;
    q->steps = (uint8_t)(st < 1 ? 1 : st > SEQ_STEPS ? SEQ_STEPS : st);
    q->count = 0;
    q->used = 1;
    for (i = 0; i < c && e - p >= 4; i++, p += 4) if (q->count < SEQ_NOTES && p[0] < q->steps) q->n[q->count++] = (snote_t){p[0], p[1], p[2], p[3]};
    *pp = p;
    return 0;
}

static int song_unpack(const uint8_t *p, uint32_t n)    /* 0: the song open is the one in p */
{
    const uint8_t *e = p + n;
    int s, i, k, r, g;
    if (n < 1 || *p++ != PG_SAVED) return -1;
    if (n < 1u + DMK_SYNTHS * PG_SAVED * 20u + 1u + DMK_SYNTHS + 4u * (3u + PROG_SLOTS * 4u) + DMK_SYNTHS * 4u + DMK_SYNTHS * 2u) return -1;
    seq_play(0);
    for (s = 0; s < DMK_SYNTHS; s++) for (g = 0; g < PG_SAVED; g++) for (r = 0; r < 4; r++) for (k = 0; k < 5; k++) ui.val[s][g][r][k] = *p++;
    if (st_ver < 5u) {                                  /* before 0.4: its new controls off; Off first in the verb's
                                                           and the echo's types */
        for (s = 0; s < DMK_SYNTHS; s++) snd_new_defaults(s);
        ui.val[0][PG_GLOBAL][0][4] = choice_val(ui.val[0][PG_GLOBAL][0][4] * 6 / 128 + 1, 7);
        ui.val[0][PG_GLOBAL][1][4] = choice_val(ui.val[0][PG_GLOBAL][1][4] * 2 / 128 + 1, 3);
    }
    theme = *p++ % DMK_THEMES;
    for (s = 0; s < DMK_SYNTHS; s++) { ch.follow[s] = *p++; if (ch.follow[s] >= FW_COUNT) ch.follow[s] = FW_FREE; }
    for (i = 0; i < 4; i++) {
        uprog[i].mode = *p++; uprog[i].voicing = *p++; uprog[i].n = *p++;
        if (uprog[i].n > PROG_SLOTS) uprog[i].n = 0;
        for (k = 0; k < PROG_SLOTS; k++) {               /* each checked: a damaged one plays as Am, a bar */
            cslot_t *c = &uprog[i].s[k];
            c->root = *p++; c->q = *p++; c->half = *p++; c->pedal = *p++;
            if (c->root > 11) c->root = 0;
            if (c->q >= Q_COUNT) c->q = 0;
            if (c->half < 1 || c->half > 8) c->half = 2;
            c->pedal = c->pedal ? 1 : 0;
        }
        if (uprog[i].mode >= MO_COUNT) uprog[i].mode = MO_AEOL;
        if (uprog[i].voicing >= VO_COUNT) uprog[i].voicing = VO_WIDE;
    }
    for (s = 0; s < DMK_SYNTHS; s++) { ui.roll_cur[s] = *p++; ui.roll_view[s] = *p++; ui.roll_pen[s] = *p++; ui.roll_zoom[s] = *p++; }
    pat_init();
    song_init();
    if (st_ver < 2u) {                                  /* version 1: the rolls: each synth's first pattern, scene 1 */
        for (s = 0; s < DMK_SYNTHS; s++) if (seq_unpack(&spat[s][0], &p, e)) return -1;
        scn[0].prog = ui.val[0][PG_CHORD][0][4];
    } else if (st_ver < 4u) {                           /* versions 2, 3: a pattern of three rolls became a scene */
        int cur;
        if (e - p < 1) return -1;
        cur = *p++;
        for (g = 0; g < 8; g++) {
            int used, prog;
            if (e - p < 2) return -1;
            used = *p++; prog = *p++;
            if (!used) continue;
            for (s = 0; s < DMK_SYNTHS; s++) { if (seq_unpack(&spat[s][g], &p, e)) return -1; scn[g].pat[s] = (uint8_t)g; }
            scn[g].used = 1; scn[g].prog = (uint8_t)prog; scn[g].mute = 0;
        }
        scn_cur = (uint8_t)(cur < 8 && scn[cur].used ? cur : 0);
        if (st_ver >= 3u && e - p >= 3) {               /* its song: a step's pattern is now the scene of that number */
            int c = *p++;
            song_end = (uint8_t)(*p++ % SE_COUNT); song_mode = (uint8_t)(*p++ ? 1 : 0);
            for (i = 0; i < c && i < SONG_STEPS && e - p >= 3; i++, p += 3) song_put(i, p[0] & 7, p[1]);
            song_n = (uint8_t)i;
        }
    } else {
        int cur;
        if (e - p < 1) return -1;
        cur = *p++;
        for (s = 0; s < DMK_SYNTHS; s++)
            for (g = 0; g < SYN_PATS; g++) {
                if (e - p < 1) return -1;
                spat[s][g].used = 0;
                if (*p++ && seq_unpack(&spat[s][g], &p, e)) return -1;
            }
        for (g = 0; g < SCENES; g++) {
            if (e - p < 1) return -1;
            scn[g].used = *p++ ? 1 : 0;
            if (!scn[g].used) continue;
            if (e - p < 5) return -1;
            for (s = 0; s < DMK_SYNTHS; s++) scn[g].pat[s] = (uint8_t)(*p++ & (SYN_PATS - 1));
            scn[g].prog = *p++; scn[g].mute = (uint8_t)(*p++ & 7);
        }
        scn_cur = (uint8_t)(cur < SCENES && scn[cur].used ? cur : 0);
        if (e - p >= 3) {
            int c = *p++;
            song_end = (uint8_t)(*p++ % SE_COUNT); song_mode = (uint8_t)(*p++ ? 1 : 0);
            for (i = 0; i < c && i < SONG_STEPS && e - p >= 2; i++, p += 2) song_put(i, p[0], p[1]);
            song_n = (uint8_t)i;
        }
        for (s = 0; s < DMK_SYNTHS; s++) {               /* the harmonies' intervals (a song without them: a third) */
            int h = e - p >= 1 ? (int8_t)*p++ : 2;
            ch.hint[s] = (int8_t)(h < -7 || h > 7 ? 2 : h);
        }
    }
    scn[scn_cur].used = 1;
    if (!song_n) song_mode = 0;
    scn_load(scn_cur);
    for (s = 0; s < DMK_SYNTHS; s++) if (ui.roll_cur[s] >= sq[s].steps) ui.roll_cur[s] = 0;
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
        if (st_load(slot_at(i), ST_MAGIC_SONG, &n, &g)) continue;
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
    if (n < 0 || st_save(slot_at(i), ST_MAGIC_SONG, (uint32_t)n, (st_gen + 1u) & 0xFFFFu)) return -1;
    st_gen = (st_gen + 1u) & 0xFFFFu;
    song_gen[i] = st_gen ? st_gen : 1;
    song_prog[i] = ui.val[0][PG_CHORD][0][4];
    return 0;
}
static int song_load(int i)
{
    uint32_t n;
    if (i < 0 || i >= ST_SONGS || st_load(slot_at(i), ST_MAGIC_SONG, &n, 0)) return -1;
    return song_unpack(st_buf + ST_HDR, n);
}
static int song_erase(int i)                            /* an empty record: the slot reads as empty */
{
    if (!store_on()) return -1;
    st_put32(st_buf, 0); st_put32(st_buf + 4, 0); st_put32(st_buf + 8, 0); st_put32(st_buf + 12, 0);
    if (dmk_store_write(slot_at(i), st_buf, ST_HDR)) return -1;
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
