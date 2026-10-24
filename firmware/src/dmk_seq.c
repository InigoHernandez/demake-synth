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
static void later_tick(int frames);

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
    for (i = 0; i < SEQ_LIVE; i++) if (sq[s].left[i]) { note_off_s(s, sq[s].live[i]); sq[s].left[i] = 0; }
}

static void seq_step(void)                              /* every synth one step on */
{
    int s, i, k;
    chord_step();                                       /* the harmony first: the notes follow the chord now */
    for (s = 0; s < DMK_SYNTHS; s++) {
        seq_t *q = &sq[s];
        for (i = 0; i < SEQ_LIVE; i++) if (q->left[i] && !--q->left[i]) note_off_s(s, q->live[i]);
        for (i = 0; i < q->count; i++) {
            const snote_t *n = &q->n[i];
            int len = n->len, slot = 0;
            if (n->step != q->pos || plays_chords(s)) continue;   /* a synth that plays the chords leaves its roll */
            if (n->step + len > q->steps) len = q->steps - n->step;   /* cut at the pattern's end */
            for (k = 0; k < SEQ_LIVE; k++) if (!q->left[k]) { slot = k; break; }
            if (k == SEQ_LIVE) { note_off_s(s, q->live[0]); slot = 0; }   /* all sounding: the first gives way */
            q->live[slot] = (uint8_t)follow_note(s, n->note);
            q->left[slot] = (uint8_t)(len < 1 ? 1 : len);
            note_on_s(s, q->live[slot]);
        }
        q->pos = (uint8_t)((q->pos + 1) % q->steps);
    }
    seq_ticks++;
}

static uint32_t seq_per(void) { return (uint32_t)DMK_SR * 60u * 64u / (uint32_t)seq_bpm; }   /* Q8 samples a step */

static void seq_play(int on)
{
    int s;
    if (on == seq_on) return;
    seq_on = (uint8_t)on;
    for (s = 0; s < DMK_SYNTHS; s++) { seq_quiet(s); sq[s].pos = 0; }
    chord_quiet();
    chord_start();
    seq_acc = 0;
    seq_ticks = 0;
    if (on) seq_step();                                 /* the first step sounds at once */
}

static void seq_render(int frames)
{
    uint32_t per;
    later_tick(frames);                                 /* the strum's notes, playing or not */
    if (!seq_on) return;
    per = seq_per();
    seq_acc += (uint32_t)frames * 256u;
    while (seq_acc >= per) { seq_acc -= per; seq_step(); }
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
}
