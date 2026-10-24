/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequences (docs/plan.md, 3.5): every synth its own roll, of its own length, so the three loop against each
 * other (a 12-step pluck over a 16-step piano over a 32-step pad). A note is a start and a length in steps (a
 * sixteenth each), a pitch and a velocity. One clock counts samples at the song's tempo and steps every synth on its
 * own; a note sounds from its step for its length, cut at its pattern's end. With SNAP (the default) a note is on the
 * key's scale, A minor until the chord machine sets the key (phase 2). Integer only */

#define SEQ_NOTES 64                                    /* notes a synth */
#define SEQ_STEPS 64                                    /* the longest pattern */
#define SEQ_LIVE 8                                      /* notes a synth sounds from its pattern at once */

typedef struct { uint8_t step, len, note, vel; } snote_t;
typedef struct {
    snote_t n[SEQ_NOTES];
    uint8_t count, steps, pos;                          /* notes, length in steps, the step that sounds next */
    uint8_t live[SEQ_LIVE], left[SEQ_LIVE];             /* the notes it sounds and the steps each has left */
} seq_t;

static seq_t sq[DMK_SYNTHS];
static uint8_t seq_on, seq_rec;
static int seq_bpm = 72;
static uint8_t seq_stop_req;                            /* the song ended inside a step: stopped after it */
static uint32_t clk_acc;                                /* MIDI out: Q8 samples into the clock's 24th of a beat */
static uint32_t seq_frames;                             /* samples rendered: a MIDI clock in's pace is measured on it */
static uint32_t seq_per_q8(void);
static uint32_t seq_acc, seq_ticks;                     /* Q8 samples into the step; steps since PLAY */

/* the scale: the song's key and mode (the CHORD page), its degrees in semitones above the key; A Aeolian at first */
static const uint8_t MODES[6][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
                                    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}};
static uint8_t seq_key = 9, seq_mode = 0;
#define SCALE MODES[seq_mode]
#define SCALE_ROOT seq_key
/* the chord machine (dmk_chord.c, after this file): its clock and what each synth does with it */
static void chord_step(void);
static void chord_start(void);
static void chord_quiet(void);
static int follow_note(int s, int n);
static int plays_chords(int s);
static int harm_of(int s);
static int harm_note(int f, int n);
static void later_tick(int frames);
static void chord_rest(void);

/* patterns and scenes, after the way a pocket studio arranges (docs/sound-design.md, 3): each synth has eight
 * patterns of its own (its rolls); a SCENE says which pattern each synth plays, with the progression and the synths
 * muted; sixteen scenes, on the white keys with HOME held. A switch waits for the bar (16 steps since PLAY), then
 * every roll and the progression start again from their beginnings; an empty scene picked starts as a copy of the one
 * playing, its patterns shared, so a variation is the scene's one pattern changed. The sounds, effects, key and tempo
 * are the song's: a switch changes the notes and the harmony, never the timbre. The open patterns are sq[]; the others
 * wait in spat[]. A synth muted plays nothing of its roll or of the chords (the keys still sound) */
#define SYN_PATS 8                                      /* patterns a synth */
#define SCENES 16
typedef struct { snote_t n[SEQ_NOTES]; uint8_t count, steps, used; } pseq_t;
static pseq_t spat[DMK_SYNTHS][SYN_PATS] POOL;
typedef struct { uint8_t pat[DMK_SYNTHS], prog, mute, used; } scene_t;
static scene_t scn[SCENES];
static uint8_t scn_cur, scn_next = 0xFF, pat_mute;      /* the scene open, the one waiting for the bar, the mutes */
static uint8_t spat_next[DMK_SYNTHS] = {0xFF, 0xFF, 0xFF};   /* a synth's pattern waiting for the bar */
static uint8_t pat_prog_get(void);                      /* the CHORD page's progression (dmk_ui.c) */
static void pat_prog_put(uint8_t v);
static void seq_quiet(int s);
static void chord_quiet(void);
static void chord_start(void);

static int syn_pat(int s) { return scn[scn_cur].pat[s]; }   /* the pattern synth s plays */
static void spat_keep(int s)                            /* synth s's roll into its pattern */
{
    pseq_t *q = &spat[s][syn_pat(s)];
    int i;
    q->count = sq[s].count; q->steps = sq[s].steps; q->used = 1;
    for (i = 0; i < sq[s].count; i++) q->n[i] = sq[s].n[i];
}
static void spat_load(int s, int p)                     /* pattern p into synth s's roll (what was open not kept) */
{
    const pseq_t *q = &spat[s][p & (SYN_PATS - 1)];
    int i;
    seq_quiet(s);
    sq[s].count = q->used ? q->count : 0;
    sq[s].steps = q->used && q->steps ? q->steps : sq[s].steps ? sq[s].steps : 16;   /* a new one: the length it had */
    for (i = 0; i < sq[s].count; i++) sq[s].n[i] = q->n[i];
    sq[s].pos = 0;
}
static void scn_keep(void)                              /* the scene open, its rolls into their patterns */
{
    int s;
    for (s = 0; s < DMK_SYNTHS; s++) spat_keep(s);
    scn[scn_cur].prog = pat_prog_get();
    scn[scn_cur].mute = pat_mute;
    scn[scn_cur].used = 1;
}
static void scn_load(int c)                             /* scene c open (what was open not kept) */
{
    int s;
    scn_cur = (uint8_t)c;
    for (s = 0; s < DMK_SYNTHS; s++) { spat_load(s, scn[c].pat[s]); spat_next[s] = 0xFF; }
    pat_mute = scn[c].mute;
    pat_prog_put(scn[c].prog);
    chord_quiet();
    chord_start();
}
static void scn_open(int c) { scn_keep(); scn_load(c); }   /* scene c open: the last kept first */
static void syn_pat_set(int s, int p)                   /* synth s plays pattern p in this scene, now */
{
    spat_keep(s);
    scn[scn_cur].pat[s] = (uint8_t)p;
    spat_load(s, p);
}
static int spat_dup(int s)                              /* synth s's pattern copied into its next empty one, now its: -1 full */
{
    int q, from = syn_pat(s);
    spat_keep(s);
    for (q = 1; q < SYN_PATS && spat[s][(from + q) % SYN_PATS].used; q++) {}
    if (q == SYN_PATS) return -1;
    q = (from + q) % SYN_PATS;
    spat[s][q] = spat[s][from];
    scn[scn_cur].pat[s] = (uint8_t)q;
    return q;
}
static int scn_bars(int c)                              /* a scene's length: its longest pattern, in bars (1-8) */
{
    int s, b = 1;
    for (s = 0; s < DMK_SYNTHS; s++) {
        const pseq_t *q = &spat[s][scn[c].pat[s]];
        int st = c == scn_cur ? sq[s].steps : q->used ? q->steps : 16, n = (st + 15) / 16;
        if (n > b) b = n;
    }
    return b > 8 ? 8 : b;
}
static void pat_init(void)                              /* the first song: scene 1, each synth its pattern 1 */
{
    int s, p;
    for (s = 0; s < DMK_SYNTHS; s++) for (p = 0; p < SYN_PATS; p++) spat[s][p].used = 0;
    for (p = 0; p < SCENES; p++) { scn[p].used = 0; scn[p].mute = 0; for (s = 0; s < DMK_SYNTHS; s++) scn[p].pat[s] = 0; }
    scn[0].used = 1;
    scn_cur = 0; scn_next = 0xFF; pat_mute = 0;
    for (s = 0; s < DMK_SYNTHS; s++) spat_next[s] = 0xFF;
}

/* the song mode: the scenes arranged. A step is a scene for 1-8 bars; a song is up to 128 of them and an end. In
 * song mode PLAY plays them in order, each switch on its bar; in scene mode PLAY loops the scene open. Recorded live
 * (Arrange, REC, PLAY): each bar is counted to the scene playing, a change closes the step (a step longer than 8 bars
 * goes on in another) */
#define SONG_STEPS 128
enum { SE_LOOP, SE_STOP, SE_FADE, SE_FADES, SE_COUNT };   /* the end: loop; stop; fade out; fade in and out */
typedef struct { uint8_t scene, bars; } sstep_t;
static sstep_t song[SONG_STEPS];
static uint8_t song_n, song_end, song_mode, song_at, song_left, song_rec;
static uint8_t rec_scn, rec_bars, rec_from;
static int song_playing(void) { return seq_on && song_mode && song_n && !song_rec; }

static int32_t song_bar_samples(int bars) { return (int32_t)((uint32_t)bars * 16u * (seq_per_q8() >> 8)); }
static void song_put(int at, int c, int bars)
{
    if (at >= SONG_STEPS) return;
    song[at].scene = (uint8_t)(c & (SCENES - 1)); song[at].bars = (uint8_t)(bars < 1 ? 1 : bars > 8 ? 8 : bars);
}
static void rec_close(void)                             /* the step recorded so far written; long ones split by 8 */
{
    while (rec_bars && rec_from < SONG_STEPS) {
        int b = rec_bars > 8 ? 8 : rec_bars;
        song_put(rec_from++, rec_scn, b);
        rec_bars = (uint8_t)(rec_bars - b);
    }
    song_n = rec_from;
}
/* each bar of the clock: the song's next step when the last has had its bars; 1 when the song has ended (stopped) */
static int song_bar(void)
{
    if (song_rec) {                                     /* recording: the bar counted to the scene that sounds */
        if (seq_ticks && scn_cur != rec_scn) { rec_close(); rec_scn = scn_cur; }
        else if (!seq_ticks) rec_scn = scn_cur;
        rec_bars++;
        return 0;
    }
    if (!song_playing()) return 0;
    if (!song_left) {
        const sstep_t *t;
        if (song_at >= song_n) {
            if (song_end == SE_LOOP) song_at = 0;
            else return 1;                              /* the end: stopped (a fade has already come down) */
        }
        t = &song[song_at++];
        if (t->scene != scn_cur) scn_open(t->scene);
        song_left = t->bars;
        if ((song_end == SE_FADE || song_end == SE_FADES) && song_at == song_n) {   /* the last: the synths fade out */
            int32_t n = song_bar_samples(t->bars);
            mix_dg = -(32768 / (n / DMK_SUB > 0 ? n / DMK_SUB : 1)) - 1;
        }
    }
    song_left--;
    return 0;
}
/* PLAY in song mode: from step at; the fade-in on the first step when the song has one */
static void song_start(int at)
{
    song_at = (uint8_t)(at < song_n ? at : 0);
    song_left = 0;
    mix_g = 32768; mix_dg = 0;
    if (song_end == SE_FADES && song_at == 0 && song_n) {
        int32_t n = song_bar_samples(song[0].bars);
        mix_g = 0; mix_dg = 32768 / (n / DMK_SUB > 0 ? n / DMK_SUB : 1) + 1;
    }
}
static void song_init(void) { song_n = 0; song_end = SE_LOOP; song_mode = 0; song_at = 0; song_left = 0; song_rec = 0; }

static int snap(int n)                                  /* the scale's note at or under n */
{
    int rel = ((n - SCALE_ROOT) % 12 + 12) % 12, i = 6;
    while (SCALE[i] > rel) i--;
    return n - (rel - SCALE[i]);
}
static int degree_of(int n)                             /* a scale note's place: 7 a octave, from A-1 */
{
    int rel = n - SCALE_ROOT, o = rel / 12, i = 6;
    rel -= o * 12;
    while (SCALE[i] > rel) i--;
    return o * 7 + i;
}
static int note_of(int d) { return SCALE_ROOT + d / 7 * 12 + SCALE[d % 7]; }

static int seq_find(int s, int step, int note)
{
    int i;
    for (i = 0; i < sq[s].count; i++) if (sq[s].n[i].step == step && sq[s].n[i].note == note) return i;
    return -1;
}
static int seq_add(int s, int step, int len, int note, int vel)
{
    seq_t *q = &sq[s];
    int i = seq_find(s, step, note);
    if (i >= 0) { q->n[i].len = (uint8_t)len; return i; }
    if (q->count >= SEQ_NOTES) return -1;
    i = q->count++;
    q->n[i].step = (uint8_t)step; q->n[i].len = (uint8_t)len; q->n[i].note = (uint8_t)note; q->n[i].vel = (uint8_t)vel;
    return i;
}
static void seq_del(int s, int i)
{
    seq_t *q = &sq[s];
    if (i < 0 || i >= q->count) return;
    q->n[i] = q->n[--q->count];
}

static void seq_quiet(int s)                            /* every note the pattern sounds, off */
{
    int i;
    for (i = 0; i < SEQ_LIVE; i++) if (sq[s].left[i]) { note_off_src(s, sq[s].live[i], SRC_ROLL); sq[s].left[i] = 0; }
}

/* a note of a roll on synth s for len steps, in its free slot (all sounding: the first gives way) */
static void roll_on(int s, int note, int len)
{
    seq_t *q = &sq[s];
    int k, slot = 0;
    for (k = 0; k < SEQ_LIVE; k++) if (!q->left[k]) { slot = k; break; }
    if (k == SEQ_LIVE) note_off_src(s, q->live[0], SRC_ROLL);
    q->live[slot] = (uint8_t)note;
    q->left[slot] = (uint8_t)(len < 1 ? 1 : len);
    note_on_src(s, note, SRC_ROLL);
}

static void seq_step(void)                              /* every synth one step on */
{
    int s, i, k;
    if (!(seq_ticks % 16u)) {                           /* on the bar: a pattern switch, the song's step */
        int x;
        if (scn_next < SCENES) { scn_open(scn_next); scn_next = 0xFF; }
        for (x = 0; x < DMK_SYNTHS; x++) if (spat_next[x] < SYN_PATS) { syn_pat_set(x, spat_next[x]); spat_next[x] = 0xFF; }
        if (song_bar()) { seq_stop_req = 1; return; }
    }
    chord_step();                                       /* the harmony first: the notes follow the chord now */
    for (s = 0; s < DMK_SYNTHS; s++) {
        seq_t *q = &sq[s];
        for (i = 0; i < SEQ_LIVE; i++) if (q->left[i] && !--q->left[i]) note_off_src(s, q->live[i], SRC_ROLL);
        for (i = 0; i < q->count; i++) {
            const snote_t *n = &q->n[i];
            int len = n->len, note;
            if (n->step != q->pos || plays_chords(s) || harm_of(s) >= 0 || pat_mute >> s & 1u) continue;   /* a follower leaves its roll */
            if (n->step + len > q->steps) len = q->steps - n->step;   /* cut at the pattern's end */
            note = follow_note(s, n->note);
            roll_on(s, note, len);
            for (k = 0; k < DMK_SYNTHS; k++)            /* the synths in harmony with this one: its note, moved */
                if (k != s && harm_of(k) == s && !(pat_mute >> k & 1u)) roll_on(k, harm_note(k, note), len);
        }
        q->pos = (uint8_t)((q->pos + 1) % q->steps);
    }
    seq_ticks++;
}

static uint32_t seq_per(void) { return (uint32_t)DMK_SR * 60u * 64u / (uint32_t)seq_bpm; }   /* Q8 samples a step */
static uint32_t seq_per_q8(void) { return seq_per(); }

static void seq_play(int on)
{
    int s;
    if (on == seq_on) return;
    if (!on && song_rec) { rec_close(); song_rec = 0; song_mode = (uint8_t)(song_n > 0); }   /* a take ended: the song */
    if (!on) { mix_g = 32768; mix_dg = 0; }
    if (midi_out & 2u) mq_put(on ? 0xFA : 0xFC, 0, 0);   /* MIDI out: start, stop */
    clk_acc = 0;
    seq_on = (uint8_t)on;
    for (s = 0; s < DMK_SYNTHS; s++) { seq_quiet(s); sq[s].pos = 0; }
    chord_quiet();
    chord_start();
    if (!on) chord_rest();                              /* stopped: no chord shown as sounding */
    seq_acc = 0;
    seq_ticks = 0;
    if (on) seq_step();                                 /* the first step sounds at once */
}

static void seq_render(int frames)
{
    uint32_t per;
    seq_frames += (uint32_t)frames;
    later_tick(frames);                                 /* the strum's notes, playing or not */
    if (!seq_on || clock_ext) return;                   /* an outside clock steps it instead (dmk_midi) */
    per = seq_per();
    if (midi_out & 2u)                                  /* MIDI out: the clock, 24 a beat, 6 a step */
        for (clk_acc += (uint32_t)frames * 256u; clk_acc >= per / 6u; clk_acc -= per / 6u) mq_put(0xF8, 0, 0);
    seq_acc += (uint32_t)frames * 256u;
    while (seq_acc >= per && seq_on) {
        seq_acc -= per; seq_step();
        if (seq_stop_req) { seq_stop_req = 0; seq_play(0); }   /* the song's end */
    }
}

/* where the playing is, in steps since PLAY, Q8: a recorded note starts at its nearest step */
static uint32_t seq_now(void) { return seq_ticks ? ((seq_ticks - 1u) << 8) + seq_acc * 256u / seq_per() : 0; }
static int seq_playing_step(int s) { return (sq[s].pos + sq[s].steps - 1) % sq[s].steps; }

static void seq_init(void)
{
    /* the first song's patterns, A minor at 72: the pad's chords over two bars, the piano's line over one, the
     * pluck's figure over twelve steps, so it lands somewhere new each bar */
    static const uint8_t PAD[][3] = {{0, 16, 57}, {0, 16, 60}, {0, 16, 64}, {16, 16, 53}, {16, 16, 57}, {16, 16, 60}};
    static const uint8_t PIANO[][3] = {{0, 3, 69}, {4, 2, 72}, {6, 2, 71}, {8, 4, 67}, {12, 4, 64}};
    static const uint8_t PLUCK[][3] = {{0, 1, 76}, {3, 1, 69}, {6, 1, 72}, {9, 1, 74}};
    unsigned i;
    for (i = 0; i < DMK_SYNTHS; i++) { sq[i].count = 0; sq[i].pos = 0; }
    sq[0].steps = 32; sq[1].steps = 16; sq[2].steps = 12;
    for (i = 0; i < sizeof PAD / sizeof PAD[0]; i++) seq_add(0, PAD[i][0], PAD[i][1], PAD[i][2], 100);
    for (i = 0; i < sizeof PIANO / sizeof PIANO[0]; i++) seq_add(1, PIANO[i][0], PIANO[i][1], PIANO[i][2], 100);
    for (i = 0; i < sizeof PLUCK / sizeof PLUCK[0]; i++) seq_add(2, PLUCK[i][0], PLUCK[i][1], PLUCK[i][2], 100);
    seq_on = seq_rec = 0;
    for (i = 0; i < DMK_SYNTHS; i++) { unsigned k; for (k = 0; k < SEQ_LIVE; k++) sq[i].left[k] = 0; }
    pat_init();
    song_init();
}
