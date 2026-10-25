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
#define ST_VER 12u  /* 12: a reverb a track (FX's first row, where the colour was), the colour the mix's (GLO's first row,
                       where the shared verb was); 11: patterns to 256 steps, a note 5 bytes; 10: a note's place in its step (a strum recorded); 9: the rolls' Arp row; 8: five more verbs (its types' places moved); 7: the rolls' Vary row; a delay a synth for its media, one media for the mix, the MIDI row
                       a row down; 6: more effects in each FX
                       row (their types' places moved); 5: the layer, the voice, the filter's drive, the LFO heard; 4: each synth's patterns, the scenes, a song
                       of scenes; 3: patterns and their song; 2: patterns; 1: rolls */

#define OLD_VERBS 12                                    /* the shared verb's types, before 12: Off, Room, Hall, Space,
                                                           Haze, Bloom, Lofi, Plate, Spring, Shimmer, Reverse, Freeze */
static uint8_t st_buf[ST_SECTOR];                       /* a sector as it is written or was read (in RAM: the pool is
                                                           the sound's) */
static uint8_t lib[ST_USER][ST_SOUND];                  /* the library, as the flash holds it: [used, its form, the values]:
                                                           form 0, 45 values (before 0.4); 1, 55; 2, 55 with the
                                                           FX rows' later types; 3, the delay for the media (0.5); 4,
                                                           the reverb for the colour (0.4.17) */
static uint32_t st_gen;                                 /* the newest song's generation */
static uint32_t song_gen[ST_SONGS];                     /* each slot's (0: empty) */
static uint8_t song_prog[ST_SONGS];                     /* each slot's progression, for its name */
/* a new song's name (the owner: a name made up as it is first saved): two words, kept in the Songs page's values,
 * which nothing else uses (its row 1: the words' places and a mark; a song of before has none, its progression's) */
static const char *const NM_A[24] = {"Quiet", "Late", "Slow", "Pale", "Low", "Soft", "Blue", "Neon", "Paper", "Glass",
    "Silver", "Hazy", "Faded", "Warm", "Still", "Hollow", "Velvet", "Distant", "Sleepy", "Empty", "Golden", "Rainy", "Lunar", "Small"};
static const char *const NM_B[24] = {"harbour", "radio", "window", "tape", "motel", "garden", "station", "lantern", "signal",
    "orchard", "tide", "hallway", "rooftop", "meadow", "comet", "ferry", "arcade", "forest", "pool", "street", "screen",
    "cassette", "palms", "lights"};
static char song_nm[ST_SONGS][20];                      /* each slot's name, if it has one ("" : its progression's) */
static uint8_t song_nw[ST_SONGS];                       /* a slot written by a newer firmware: used, not opened (it
                                                           read as empty, and a save went over it) */
static uint8_t song_nmv[ST_SONGS][3];                   /* and its words' places and mark, to change it (Save's K2, K3) */
static const char *nm_word(int second, int i) { return (second ? NM_B : NM_A)[i % 24]; }
static void nm_of(char *o, const uint8_t *v) { o[0] = 0; if (v[2] == NM_MARK && v[0] < 24u && v[1] < 24u) str_cat(str_cat(str_cat(o, NM_A[v[0]]), " "), NM_B[v[1]]); }
static void nm_roll(uint8_t *v, uint32_t salt)          /* a name made up: two words, never the same twice running */
{
    uint32_t r = hsh(salt ^ (st_gen + 1u) * 40503u);
    uint8_t a = (uint8_t)(r % 24u), b = (uint8_t)((r >> 8) % 24u);
    if (v[2] == NM_MARK && a == v[0] && b == v[1]) b = (uint8_t)((b + 1u) % 24u);
    v[0] = a; v[1] = b; v[2] = NM_MARK;
}

/* a sound: the rows a synth owns (SOUND, LAYER, FILTER, AMP, the four FX rows (REVERB, DELAY, MOTION, SPACE), LFO;
 * then VOICE and the LFO's sync, after the first nine so a sound of before 0.4 is these nine), five values each */
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
/* st_buf's record of n bytes to at, of version ver (a rename keeps the one it was written in); 0: written */
static int st_write(uint32_t at, uint32_t magic, uint32_t n, uint32_t gen, uint32_t ver)
{
    if (!store_on() || n > ST_SECTOR - ST_HDR) return -1;
    st_put32(st_buf, magic);
    st_put32(st_buf + 4, n);
    st_put32(st_buf + 8, st_crc(st_buf + ST_HDR, n));
    st_put32(st_buf + 12, gen << 16 | ver);
    return dmk_store_write(at, st_buf, ST_HDR + n);
}
static int st_save(uint32_t at, uint32_t magic, uint32_t n, uint32_t gen) { return st_write(at, magic, n, gen, ST_VER); }

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
/* synth s's FX rows' types, stored when each row had fewer (5, 4, 4): the same type, at its place in the longer list */
static void fx_types_moved(int s)
{
    static const uint8_t OLD_N[3] = {5, 4, 4}, NEW_N[3] = {FX_COLOURS, FX_MEDIAS, FX_MOTIONS};
    int r;
    for (r = 0; r < 3; r++) {
        uint8_t *v = &ui.val[s][PG_FX][r][4];
        *v = *v * OLD_N[r] / 128 ? choice_val(*v * OLD_N[r] / 128, NEW_N[r]) : 0;   /* (off stays 0) */
    }
}
static void sound_get(int s, const uint8_t *v, int form)   /* form 0: a sound of before 0.4, its new controls off */
{
    int r, k;
    for (r = 0; r < (form ? SND_ROWS_N : 9); r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = v[r * 5 + k];
    if (!form) snd_new_defaults(s);                     /* its layer, voice, drive and LFO did nothing then: off */
    if (form < 2) fx_types_moved(s);
    if (form < 3) delay_defaults(s);                    /* its media: the mix's now, so not the sound's */
    if (form < 4) {                                     /* its colour: the mix's now (dropped); its verb's send its own
                                                           reverb's AMOUNT, a hall (the shared verb's first, at its
                                                           return of 84, its tone, size and sweep), none if it sent none */
        uint8_t *rv = ui.val[s][PG_FX][0], snd = ui.val[s][PG_FX][3][0];
        rv[0] = 64; rv[1] = 56; rv[2] = 96; rv[3] = 64; rv[4] = snd ? choice_val(RV_HALL, FX_REVERBS) : 0;
        ui.val[s][PG_FX][3][0] = (uint8_t)(snd * 84 / 127);
    }
    sp_sync();
}
/* synth s's sound into the library's first free slot: its number (1..32), 0 full, -1 not written */
static int sound_save(int s)
{
    int i, j;
    for (i = 0; i < ST_USER && lib[i][0]; i++) {}
    if (i == ST_USER) return 0;
    lib[i][0] = 1; lib[i][1] = 4;
    sound_put(s, &lib[i][2]);
    for (j = 0; j < (int)(ST_USER * ST_SOUND); j++) st_buf[ST_HDR + j] = lib[j / ST_SOUND][j % ST_SOUND];
    if (st_save(ST_LIB, ST_MAGIC_LIB, ST_USER * ST_SOUND, 0)) { lib[i][0] = 0; return -1; }
    return i + 1;
}
static void lib_load(int s, int slot) { if (slot >= 0 && slot < ST_USER && lib[slot][0]) sound_get(s, &lib[slot][2], lib[slot][1]); }

/* INIT: the synth's sound as the first song has it (its recording kept), with no effect at all (the owner): its
 * reverb, delay and motion off, nothing sent to the reverb or the echo, no layer, no LFO */
static void sound_init(int s)
{
    uint8_t keep = ui.val[s][PG_SOUND][0][0];
    int r, k;
    for (r = 0; r < SND_ROWS_N; r++) for (k = 0; k < 5; k++) ui.val[s][SND_ROWS[r][0]][SND_ROWS[r][1]][k] = ui_first[s][r * 5 + k];
    ui.val[s][PG_SOUND][0][0] = keep;
    ui.val[s][PG_SOUND][1][4] = keep;                   /* its layer starts as its own recording */
    ui.val[s][PG_SOUND][1][0] = 0;                      /* (the layer off) */
    for (r = 0; r < 3; r++) ui.val[s][PG_FX][r][4] = 0;
    ui.val[s][PG_FX][3][0] = ui.val[s][PG_FX][3][1] = 0;   /* (its sends) */
    ui.val[s][PG_LFO][0][1] = 0;                        /* (the LFO's depth) */
    sp_sync();
}

/* ---- songs */
static uint32_t slot_at(int i) { return ST_SONG0 + (uint32_t)i * ST_SECTOR; }

DMK_SMALL static int song_pack(uint8_t *o)                        /* the song open into o: its length, or -1 too long */
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
            if (p - o + (q->used ? 3 + q->count * 5 : 1) > (int)(ST_SECTOR - ST_HDR)) return -1;   /* (checked before a
                                                           byte of it: its head was written past the sector) */
            *p++ = q->used;
            if (!q->used) continue;
            *p++ = (uint8_t)(q->steps - 1); *p++ = q->count;   /* (1-256) */
            for (i = 0; i < q->count; i++) {
                const snote_t *m = &q->n[i];
                *p++ = m->step; *p++ = m->len; *p++ = m->note; *p++ = m->vel; *p++ = m->off;
            }
        }
    if (p - o + SCENES * 6 + 3 + song_n * 2 + DMK_SYNTHS > (int)(ST_SECTOR - ST_HDR)) return -1;   /* (the intervals too) */
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
    if (st_ver >= 11u) {                                /* 11: up to 256 steps, a note 5 bytes, its 32nds its own */
        q->steps = (uint16_t)(st + 1);
        q->count = 0;
        q->used = 1;
        for (i = 0; i < c && e - p >= 5; i++, p += 5) {
            snote_t m = {p[0], p[1], p[2], p[3], (uint8_t)(p[4] & 31u)};
            if (q->count < SEQ_NOTES && m.len) q->n[q->count++] = m;   /* (past its end too: kept, as the roll
                                                           keeps them while shorter) */
        }
        *pp = p;
        return 0;
    }
    q->steps = (uint16_t)(st < 1 ? 1 : st > 64 ? 64 : st);
    q->count = 0;
    q->used = 1;
    for (i = 0; i < c && e - p >= 4; i++, p += 4) {     /* (before 10 the spare bits were 0: on its step) */
        snote_t m = {(uint8_t)(p[0] & 63u), (uint8_t)(p[1] & 127u), (uint8_t)(p[2] & 127u), (uint8_t)(p[3] & 127u),
                     (uint8_t)(p[0] >> 6 | (p[2] >> 7) << 2 | (p[3] >> 7) << 3 | (p[1] >> 7) << 4)};
        if (q->count < SEQ_NOTES) q->n[q->count++] = m;
    }
    *pp = p;
    return 0;
}

DMK_SMALL static int song_unpack(const uint8_t *p, uint32_t n)    /* 0: the song open is the one in p */
{
    const uint8_t *e = p + n;
    int s, i, k, r, g;
    if (n < 1 || *p++ != PG_SAVED) return -1;
    if (n < 1u + DMK_SYNTHS * PG_SAVED * 20u + 1u + DMK_SYNTHS + 4u * (3u + PROG_SLOTS * 4u) + DMK_SYNTHS * 4u + DMK_SYNTHS * 2u) return -1;
    seq_play(0);
    arp_clear_all();                                    /* (the last project's arps: their pools went on in this one) */
    for (s = 0; s < DMK_SYNTHS; s++) ch.hint[s] = 2;    /* (before 4 none were kept: the last project's stayed) */
    for (s = 0; s < DMK_SYNTHS; s++) for (g = 0; g < PG_SAVED; g++) for (r = 0; r < 4; r++) for (k = 0; k < 5; k++) ui.val[s][g][r][k] = *p++;
    if (st_ver < 7u) for (k = 0; k < 3; k++) if (ui.val[0][PG_GLOBAL][2][k] == 64) {   /* the MIDI row never set (a song of 0.2, before
                                                           it was there; no choice is 64): In Synth, Out Notes, the
                                                           own clock. It loaded as Ch 1-3, Clock out, an outside
                                                           clock: PLAY waited for one */
        static const uint8_t MD[3] = {32, 48, 32};
        ui.val[0][PG_GLOBAL][2][k] = MD[k];
    }
    if (st_ver < 6u) for (s = 0; s < DMK_SYNTHS; s++) fx_types_moved(s);   /* before 0.5: fewer types a row */
    if (st_ver < 5u) {                                  /* before 0.4: its new controls off; Off first in the verb's
                                                           and the echo's types */
        for (s = 0; s < DMK_SYNTHS; s++) snd_new_defaults(s);
        ui.val[0][PG_GLOBAL][0][4] = choice_val(ui.val[0][PG_GLOBAL][0][4] * 6 / 128 + 1, 7);
        ui.val[0][PG_GLOBAL][1][4] = choice_val(ui.val[0][PG_GLOBAL][1][4] * 2 / 128 + 1, 3);
    }
    if (st_ver < 9u) for (s = 0; s < DMK_SYNTHS; s++) arp_defaults(s);   /* before 0.4.12: no Arp row, its 64s: off */
    if (st_ver < 8u)                                    /* before 0.4.11: the verb's type of seven at its place of twelve */
        ui.val[0][PG_GLOBAL][0][4] = choice_val(ui.val[0][PG_GLOBAL][0][4] * 7 / 128, OLD_VERBS);
    if (st_ver < 7u) {                                  /* before 0.5: the rolls' Vary off; the MIDI row a row down; the
                                                           media of the first synth that had one the mix's (its
                                                           knobs; none: off, the pad's knobs), the synths' delays off */
        uint8_t *m = ui.val[0][PG_GLOBAL][2], *g = ui.val[0][PG_GLOBAL][3];
        int from = 0;
        for (k = 0; k < 3; k++) g[k] = m[k];
        for (s = DMK_SYNTHS - 1; s >= 0; s--) if (ui.val[s][PG_FX][1][4] * FX_MEDIAS / 128) from = s;
        for (k = 0; k < 5; k++) m[k] = ui.val[from][PG_FX][1][k];
        for (s = 0; s < DMK_SYNTHS; s++) { roll_vary_defaults(s); delay_defaults(s); }
    }
    if (st_ver < 12u) {                                 /* before 0.4.17 (the owner's: the reverb per track, the colour
                                                           the mix's): each synth its own reverb from the shared one (its
                                                           type: a room, a plate, a spring as they were, every other a
                                                           hall; its AMOUNT the synth's send through the return; its tone,
                                                           size and sweep), none where it sent none; the mix's colour the
                                                           first synth's that had one on (its knobs), the others' gone */
        static const uint8_t TY[OLD_VERBS] = {RV_OFF, RV_ROOM, RV_HALL, RV_HALL, RV_HALL, RV_HALL, RV_HALL, RV_PLATE,
                                               RV_SPRING, RV_HALL, RV_HALL, RV_HALL};
        uint8_t *gv = ui.val[0][PG_GLOBAL][0], old[5], col[5] = {64, 64, 64, 64, 0};
        int t;
        for (k = 0; k < 5; k++) old[k] = gv[k];
        t = TY[old[4] * OLD_VERBS / 128 < OLD_VERBS ? old[4] * OLD_VERBS / 128 : OLD_VERBS - 1];
        for (s = DMK_SYNTHS - 1; s >= 0; s--) if (choice_index(choice_of(PG_GLOBAL, 0, 4), ui.val[s][PG_FX][0][4]))
            for (k = 0; k < 5; k++) col[k] = ui.val[s][PG_FX][0][k];
        for (s = 0; s < DMK_SYNTHS; s++) {
            uint8_t *rv = ui.val[s][PG_FX][0], *snd = &ui.val[s][PG_FX][3][0];
            rv[0] = 64; rv[1] = old[1]; rv[2] = old[2]; rv[3] = old[3];
            rv[4] = t && *snd ? choice_val(t, FX_REVERBS) : 0;
            *snd = (uint8_t)(*snd * old[0] / 127);
        }
        for (k = 0; k < 5; k++) gv[k] = col[k];
    }
    theme = *p++ % DMK_THEMES;
    ui.val[0][PG_GLOBAL][3][3] = choice_val(theme, DMK_THEMES);   /* (its dots) */
    for (s = 0; s < DMK_SYNTHS; s++) { ch.follow[s] = *p++; if (ch.follow[s] >= FW_COUNT) ch.follow[s] = FW_FREE; }
    for (i = 0; i < 4; i++) {
        uprog[i].mode = *p++; uprog[i].voicing = *p++; uprog[i].n = *p++;
        if (uprog[i].n > PROG_SLOTS) uprog[i].n = 0;
        for (k = 0; k < PROG_SLOTS; k++) {               /* each checked: a damaged one plays as Am, a bar */
            cslot_t *c = &uprog[i].s[k];
            c->root = *p++; c->q = *p++; c->half = *p++; c->pedal = *p++;
            if (c->root > 11) c->root = 0;
            if (c->q >= Q_COUNT) c->q = 0;
            if (c->half < 1 || c->half > 16) c->half = 2;   /* (to 16, as K3 writes them: 9-16 came back as 2) */
            c->pedal = c->pedal ? 1 : 0;
        }
        if (uprog[i].mode >= MO_COUNT) uprog[i].mode = MO_AEOL;
        if (uprog[i].voicing >= VO_COUNT) uprog[i].voicing = VO_WIDE;
    }
    for (s = 0; s < DMK_SYNTHS; s++) {                  /* (each checked: a zoom past 2 divided by nothing) */
        ui.roll_cur[s] = *p++; ui.roll_view[s] = *p++; ui.roll_pen[s] = *p++; ui.roll_zoom[s] = *p++;
        if (ui.roll_view[s] < 14 || ui.roll_view[s] > 43) ui.roll_view[s] = 28;
        if (!ui.roll_pen[s]) ui.roll_pen[s] = 1;
        if (ui.roll_zoom[s] > 2) ui.roll_zoom[s] = 0;
    }
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
        song_gen[i] = 0; song_nm[i][0] = 0; song_nmv[i][2] = 0; song_nw[i] = 0;
        if (st_load(slot_at(i), ST_MAGIC_SONG, &n, &g)) {
            if (!dmk_store_read(slot_at(i), st_buf, ST_HDR) && st_get32(st_buf) == ST_MAGIC_SONG && (st_get32(st_buf + 12) & 0xFFFFu) > ST_VER) {
                g = st_get32(st_buf + 12) >> 16;
                song_gen[i] = g ? g : 1; song_nw[i] = 1;
                str_cat(song_nm[i], "Newer firmware");
                if (g > st_gen) st_gen = g;
            }
            continue;
        }
        song_gen[i] = g ? g : 1;
        /* the progression's place, for the slot's name: the CHORD page's value, packed after the counts */
        song_prog[i] = st_buf[ST_HDR + 1 + PG_CHORD * 20 + 4];
        {                                               /* (synth 0's Save page, its row 1) */
            const uint8_t *v = &st_buf[ST_HDR + 1 + PG_DISCS * 20 + 5];
            nm_of(song_nm[i], v);
            song_nmv[i][0] = v[0]; song_nmv[i][1] = v[1]; song_nmv[i][2] = song_nm[i][0] ? NM_MARK : 0;
        }
        if (g > st_gen) st_gen = g;
    }
}
static int song_used(int i) { return i >= 0 && i < ST_SONGS && song_gen[i]; }
static int song_newer(int i) { return i >= 0 && i < ST_SONGS && song_nw[i]; }
/* the work open as last saved, opened or started: its packed bytes' check, to tell changes not saved (the owner: PLAY
 * twice on Projects lost a song unasked) */
static uint32_t song_kept;
static uint32_t song_check(void)                        /* (packs into st_buf: only between store calls) */
{
    int n = song_pack(st_buf + ST_HDR);
    return n < 0 ? 0 : st_crc(st_buf + ST_HDR, (uint32_t)n) ^ (uint32_t)n;
}
static void song_mark(void) { song_kept = song_check(); }
static int song_changed(void) { uint32_t c = song_check(); return !c || c != song_kept; }
static const char *song_name(int i) { return song_nm[i][0] ? song_nm[i] : PROGS[choice_index(choice_of(PG_CHORD, 0, 4), song_prog[i])]; }

/* the song open into slot i: 0 saved, -1 not (no store, the write failed), -2 too many notes for its sector */
static int song_save(int i)
{
    int n;
    if (ui.val[0][PG_DISCS][1][2] != NM_MARK)           /* a song without a name: one made up for it */
        nm_roll(ui.val[0][PG_DISCS][1], ui.now * 2654435761u ^ (uint32_t)i);
    n = song_pack(st_buf + ST_HDR);
    if (n < 0) return -2;
    if (st_save(slot_at(i), ST_MAGIC_SONG, (uint32_t)n, (st_gen + 1u) & 0xFFFFu)) return -1;
    st_gen = (st_gen + 1u) & 0xFFFFu;
    song_gen[i] = st_gen ? st_gen : 1;
    song_kept = st_crc(st_buf + ST_HDR, (uint32_t)n) ^ (uint32_t)n;   /* (what was written) */
    song_prog[i] = ui.val[0][PG_CHORD][0][4];
    nm_of(song_nm[i], ui.val[0][PG_DISCS][1]);
    for (n = 0; n < 3; n++) song_nmv[i][n] = ui.val[0][PG_DISCS][1][n];
    return 0;
}
/* slot i renamed where it is: its record as it was (its version, how new it is), only its name's three bytes; the
 * song open, if it is that one, takes the name too: 0, or -1 */
static int song_rename(int i, const uint8_t *v)
{
    uint32_t n, g;
    uint8_t *nm = st_buf + ST_HDR + 1 + PG_DISCS * 20 + 5;
    if (!song_used(i) || st_load(slot_at(i), ST_MAGIC_SONG, &n, &g) || st_buf[ST_HDR] != PG_SAVED) return -1;
    nm[0] = v[0]; nm[1] = v[1]; nm[2] = v[2];
    if (st_write(slot_at(i), ST_MAGIC_SONG, n, g, st_ver)) return -1;
    song_nmv[i][0] = v[0]; song_nmv[i][1] = v[1]; song_nmv[i][2] = v[2];
    nm_of(song_nm[i], v);
    if (i == ui.song) {                                 /* (renamed where it is: still as saved) */
        int changed = song_changed();
        ui.val[0][PG_DISCS][1][0] = v[0]; ui.val[0][PG_DISCS][1][1] = v[1]; ui.val[0][PG_DISCS][1][2] = v[2];
        if (!changed) song_mark();
    }
    return 0;
}
static int song_load(int i)
{
    uint32_t n;
    if (i < 0 || i >= ST_SONGS || st_load(slot_at(i), ST_MAGIC_SONG, &n, 0) || song_unpack(st_buf + ST_HDR, n)) return -1;
    song_mark();
    return 0;
}
static int song_erase(int i)                            /* an empty record: the slot reads as empty */
{
    if (!store_on()) return -1;
    st_put32(st_buf, 0); st_put32(st_buf + 4, 0); st_put32(st_buf + 8, 0); st_put32(st_buf + 12, 0);
    if (dmk_store_write(slot_at(i), st_buf, ST_HDR)) return -1;
    song_nm[i][0] = 0; song_nmv[i][2] = 0; song_nw[i] = 0;
    song_gen[i] = 0;
    return 0;
}
static void song_new(void)                              /* the first song: its rolls, its chords, its controls */
{
    int i, th = theme;
    seq_play(0);
    arp_clear_all();                                    /* (its arps let go: a held one sounded on, stuck) */
    seq_init();
    for (i = 0; i < 4; i++) uprog[i].n = 0;
    chord_init();
    ui_init();
    theme = (uint8_t)th;
    sp_sync();
}
/* PLAY twice on an empty slot (the owner): an empty project with no sequence and no effect, its sounds at INIT (no
 * reverb), nothing following the chords (so PLAY is silent until something is written), the mix's colour, echo and
 * media off, no name until it is saved */
static void project_empty(void)
{
    int s;
    song_new();
    for (s = 0; s < DMK_SYNTHS; s++) { sq[s].count = 0; ch.follow[s] = FW_FREE; sound_init(s); ui.preset[s] = ui.val[s][PG_SOUND][0][0]; }
    for (s = 0; s < 3; s++) ui.val[0][PG_GLOBAL][s][4] = 0;   /* (the colour, the echo, the media: Off) */
    ui.val[0][PG_DISCS][1][2] = 0;
    sp_sync();
    song_mark();
}

static void store_boot(void)                            /* at start: the library, and the newest song */
{
    int i, best = -1;
    if (!store_on()) return;
    lib_read();
    songs_scan();
    for (;;) {                                          /* the newest that opens (one that does not: the next; a
                                                           half-read one left the work a mix: the first song again) */
        int tried = best;
        best = -1;
        for (i = 0; i < ST_SONGS; i++)
            if (song_gen[i] && !song_nw[i] && (tried < 0 || song_gen[i] < song_gen[tried]) && (best < 0 || song_gen[i] > song_gen[best])) best = i;
        if (best < 0) break;
        if (!song_load(best)) { ui.song = (uint8_t)best; return; }
    }
    song_new();
    for (i = 0; i < ST_SONGS && song_gen[i]; i++) {}   /* (the work open goes to an empty slot, not over one) */
    ui.song = (uint8_t)(i < ST_SONGS ? i : 0);
    song_mark();                                        /* (the first song: as it starts) */
}
