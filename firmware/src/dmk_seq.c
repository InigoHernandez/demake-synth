/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequences (docs/plan.md, 3.5): every synth its own roll, of its own length, so the three loop against each
 * other (a 12-step pluck over a 16-step piano over a 32-step pad). A note is a start and a length in steps (a
 * sixteenth each), a pitch and a velocity. One clock counts samples at the song's tempo and steps every synth on its
 * own; a note sounds from its step for its length, cut at its pattern's end. With SNAP (the default) a note is on the
 * key's scale, A minor until the chord machine sets the key (phase 2). Integer only */

#define SEQ_NOTES 64                                    /* notes a synth */
#define SEQ_STEPS 64                                    /* the longest pattern */
#define SEQ_LIVE 8                                      /* notes a synth sounds from its pattern at once */

/* off: how late in its step it starts (and ends), in 32nds of a step: the micro timing a note was played with, a
 * strum's spacing (the owner: both were lost to the nearest step); 0 for a note written by hand, NUDGE moves it */
typedef struct { uint8_t step, len, note, vel, off; } snote_t;
typedef struct {
    snote_t n[SEQ_NOTES];
    uint8_t count, steps, pos;                          /* notes, length in steps, the step that sounds next */
    uint8_t live[SEQ_LIVE], left[SEQ_LIVE];             /* the notes it sounds and the steps each has left */
    uint8_t from[SEQ_LIVE];                             /* a harmony's: its lead + 1 (0: the roll's own) */
    uint8_t lvel[SEQ_LIVE], loff[SEQ_LIVE];             /* its velocity, its 32nds late */
    int32_t wait[SEQ_LIVE];                             /* > 0: samples until it starts; < 0: its end on its way, as
                                                           late as it began; 0 sounding */
    uint16_t mut, chance;                               /* Vary (the owner's): a note's chance to move in the scale, to
                                                           play at all (Q15; 32768 always) */
    uint8_t mrange, keep;                               /*   how far it moves (scale steps); kept in the roll */
    uint32_t rng;
} seq_t;

static seq_t sq[DMK_SYNTHS];
static uint8_t seq_on, seq_rec;
static int seq_bpm = 72;
static uint8_t seq_stop_req;                            /* the song ended inside a step: stopped after it */
static uint32_t clk_acc;                                /* MIDI out: Q8 samples into the clock's 24th of a beat */
static uint32_t seq_frames;                             /* samples rendered: a MIDI clock in's pace is measured on it */
static uint32_t ext_last;                               /* when the last MIDI clock came (seq_frames), if one has */
static uint8_t ext_seen;
/* the outside clock steps the sequences only while it runs (a clock within half a second): set to Ext with none
 * coming, PLAY froze on its first step, its notes held (the owner's 0.2 song loaded as Ext) */
static int ext_running(void) { return clock_ext && ext_seen && seq_frames - ext_last < (uint32_t)DMK_SR / 2u; }
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
static void arp_restart(void);                          /* (the arps, on PLAY: in time) */
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
static void seq_quiet_from(int lead);
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
    seq_quiet_from(s);                                  /* and the harmonies of its notes on the others */
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
        if ((song_end == SE_FADE || (song_end == SE_FADES && song_n > 1)) && song_at == song_n) {   /* the last: the synths fade out */
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
        int32_t n = song_bar_samples(song[0].bars) / (song_n > 1 ? 1 : 2);   /* one step: in over its first half */
        mix_g = 0; mix_dg = 32768 / (n / DMK_SUB > 0 ? n / DMK_SUB : 1) + 1;
        if (song_n == 1) mix_next = -mix_dg;            /* and out over its second */
    }
}
static void song_init(void) { song_n = 0; song_end = SE_LOOP; song_mode = 0; song_at = 0; song_left = 0; song_rec = 0; }

static int snap(int n)                                  /* the scale's note at or under n */
{
    int rel = ((n - SCALE_ROOT) % 12 + 12) % 12, i = 6;
    while (SCALE[i] > rel) i--;
    return n - (rel - SCALE[i]);
}
static int degree_of(int n)                             /* a scale note's place: 7 a octave, from A-1 (floored: a note
                                                           under it, a MIDI one, has its place too, under 0) */
{
    int rel = n - SCALE_ROOT + 120, o = rel / 12, i = 6;
    rel -= o * 12;
    while (SCALE[i] > rel) i--;
    return (o - 10) * 7 + i;
}
static int note_of(int d) { d += 70; return SCALE_ROOT + (d / 7 - 10) * 12 + SCALE[d % 7]; }   /* (d from -70) */

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
    q->n[i].off = 0;
    return i;
}
static void seq_del(int s, int i)
{
    seq_t *q = &sq[s];
    if (i < 0 || i >= q->count) return;
    q->n[i] = q->n[--q->count];
}

static int32_t roll_late(int off) { return (int32_t)((uint32_t)off * (seq_per_q8() >> 8) >> 5); }   /* 32nds: samples */
static void roll_off(int s, int i)                      /* slot i's note off now (one still to come: never started) */
{
    if (sq[s].wait[i] <= 0) note_off_src(s, sq[s].live[i], SRC_ROLL);
    sq[s].left[i] = 0; sq[s].wait[i] = 0;
}
static void roll_flush(int s, int note)                 /* a note's end on its way, now: before it starts again */
{
    int i;
    for (i = 0; i < SEQ_LIVE; i++) if (sq[s].left[i] && sq[s].wait[i] < 0 && sq[s].live[i] == note) roll_off(s, i);
}
static void roll_tick(int frames)                       /* the notes late in their step: on, and off, when they come */
{
    int s, i;
    for (s = 0; s < DMK_SYNTHS; s++)
        for (i = 0; i < SEQ_LIVE; i++) {
            int32_t *w = &sq[s].wait[i];
            if (!*w || !sq[s].left[i]) continue;
            if (*w > 0 && (*w -= frames) <= 0) { *w = 0; roll_flush(s, sq[s].live[i]); note_on_vel(s, sq[s].live[i], SRC_ROLL, sq[s].lvel[i]); }
            else if (*w < 0 && (*w += frames) >= 0) { *w = 0; roll_off(s, i); }
        }
}
static void seq_quiet(int s)                            /* every note the pattern sounds, off */
{
    int i;
    for (i = 0; i < SEQ_LIVE; i++) if (sq[s].left[i]) roll_off(s, i);
}
static void seq_quiet_from(int lead)                    /* the notes the synths in harmony with lead sound from it, off */
{
    int s, i;
    for (s = 0; s < DMK_SYNTHS; s++)
        for (i = 0; i < SEQ_LIVE; i++)
            if (sq[s].left[i] && sq[s].from[i] == lead + 1) roll_off(s, i);
}

/* a note of a roll on synth s for len steps, in its free slot (all sounding: the first gives way), off 32nds of a
 * step late (its end as late); from: the lead it harmonizes + 1, or 0 */
static void roll_on(int s, int note, int len, int vel, int from, int off)
{
    int32_t wait = roll_late(off);
    seq_t *q = &sq[s];
    int k, slot = 0;
    for (k = 0; k < SEQ_LIVE; k++) if (!q->left[k]) { slot = k; break; }
    if (k == SEQ_LIVE) roll_off(s, 0);
    q->live[slot] = (uint8_t)note;
    q->left[slot] = (uint8_t)(len < 1 ? 1 : len);
    q->from[slot] = (uint8_t)from;
    q->lvel[slot] = (uint8_t)vel; q->loff[slot] = (uint8_t)off;
    q->wait[slot] = wait > 0 ? wait : 0;
    if (wait <= 0) { roll_flush(s, note); note_on_vel(s, note, SRC_ROLL, vel); }
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
    for (s = 0; s < DMK_SYNTHS; s++)                    /* every synth's notes counted down first: a harmony started
                                                           this step on a later synth is not counted the same step */
        for (i = 0; i < SEQ_LIVE; i++) {
            seq_t *q = &sq[s];
            if (!q->left[i] || q->wait[i] < 0) continue;   /* (its end on its way) */
            if (q->left[i] > 1) q->left[i]--;
            else if (!q->wait[i] && q->loff[i]) q->wait[i] = -roll_late(q->loff[i]);   /* late in, late out */
            else roll_off(s, i);
        }
    for (s = 0; s < DMK_SYNTHS; s++) {
        seq_t *q = &sq[s];
        for (i = 0; i < q->count; i++) {
            snote_t *n = &q->n[i];
            int len = n->len, note = n->note;
            if (n->step != q->pos || plays_chords(s) || harm_of(s) >= 0 || pat_mute >> s & 1u) continue;   /* a follower leaves its roll */
            if (n->step + len > q->steps) len = q->steps - n->step;   /* cut at the pattern's end */
            if (q->chance < 32768u && (xs32(&q->rng) >> 17) >= q->chance) continue;   /* Vary: this time it rests */
            if (q->mut && (xs32(&q->rng) >> 17) < q->mut) {   /* Vary: another note of the scale, up or down */
                uint32_t r = xs32(&q->rng);
                int d = degree_of(note) + (int)(1u + r % q->mrange) * (r >> 31 ? -1 : 1), m = note_of(d);
                if (m >= 24 && m <= 108) {
                    note = m;
                    if (q->keep && seq_find(s, n->step, m) < 0) n->note = (uint8_t)m;   /* kept: the roll evolves */
                }
            }
            note = follow_note(s, note);
            roll_on(s, note, len, n->vel, 0, n->off);
            for (k = 0; k < DMK_SYNTHS; k++)            /* the synths in harmony with this one: its note, moved */
                if (k != s && harm_of(k) == s && !(pat_mute >> k & 1u)) roll_on(k, harm_note(k, note), len, n->vel, s + 1, n->off);
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
    seq_stop_req = 0;                                   /* (one left by an outside clock would stop the next run) */
    if (!on && song_rec) { rec_close(); song_rec = 0; song_mode = (uint8_t)(song_n > 0); }   /* a take ended: the song */
    if (!on) { mix_g = 32768; mix_dg = 0; mix_next = 0; }
    if (midi_out & 2u) mq_put(on ? 0xFA : 0xFC, 0, 0);   /* MIDI out: start, stop */
    clk_acc = 0;
    seq_on = (uint8_t)on;
    for (s = 0; s < DMK_SYNTHS; s++) { seq_quiet(s); sq[s].pos = 0; }
    chord_quiet();
    chord_start();
    arp_restart();
    if (!on) chord_rest();                              /* stopped: no chord shown as sounding */
    seq_acc = 0;
    seq_ticks = 0;
    if (on) seq_step();                                 /* the first step sounds at once */
}

static uint32_t seq_now(void);
/* the LFOs synced to the tempo: their step a block from it; playing on the own clock, their phase drawn to where the
 * playing is, so a cycle starts on the bar (or, restarted by a phrase's first note, where that note began). Far off,
 * it jumps there (a new random value if that went round); near, it is drawn an eighth of the way a render, so the
 * LFO still sees each cycle end */
static void lfo_sync(void)
{
    int s;
    for (s = 0; s < DMK_SYNTHS; s++) {
        uint32_t n = syn[s].lsync, blocks;
        if (!syn[s].lretrig) lfo[s].poff = 0;
        if (!n) { lfo[s].rst = 0; continue; }
        blocks = 165375u * n / ((uint32_t)(seq_bpm > 0 ? seq_bpm : 120) * 4u);   /* 44100 * 60 / 16 / 4: a sixteenth's */
        lfo[s].sstep = 0xFFFFFFFFu / (blocks ? blocks : 1u);
        if (seq_on && !clock_ext) {
            uint32_t len = n << 8, want = seq_now() % len * (0xFFFFFFFFu / len), d;
            if (lfo[s].rst) lfo[s].poff = lfo[s].ph - want;   /* the restart: the cycle from where it was set going */
            want += lfo[s].poff;
            d = want - lfo[s].ph;
            if ((int32_t)d > (1 << 26) || (int32_t)d < -(1 << 26)) {
                if ((int32_t)d > 0 && want < lfo[s].ph) lfo[s].hold = (int32_t)(xs32(&lfo[s].rng) >> 16) - 32768;
                lfo[s].ph = want;
            } else lfo[s].ph += (uint32_t)((int32_t)d / 8);
        }
        lfo[s].rst = 0;
    }
    for (s = 0; s < DMK_SYNTHS; s++) {                  /* each synth's DELAY: its time a division of the tempo, halved
                                                           till its line holds it (so still in time) */
        static const uint8_t DIV24[9] = {6, 8, 9, 12, 16, 18, 24, 36, 48};   /* in 24ths of a beat (the screen's DIVS) */
        uint32_t t = 55125u * DIV24[syn[s].ddiv < 9 ? syn[s].ddiv : 6] / (2u * (uint32_t)(seq_bpm > 0 ? seq_bpm : 120));   /* 11025 * 60 / 24 */
        uint32_t most = syn[s].dtype == 2 ? DL_LEN / 2u - 2u : DL_LEN - 2u - DL_LEN / 64u;   /* (its right a hair, or twice, later) */
        while (t > most) t /= 2u;
        syn[s].dtime = t;
    }
    for (s = 0; s < DMK_SYNTHS; s++) {                  /* MOTION's tremolo: its step at the tempo, in time with the bars */
        uint32_t n3, blocks;
        if (syn[s].motion != FO_TREM) continue;
        n3 = TREM3[syn[s].ok[2] * TREM_N / 128];
        blocks = 165375u * n3 / ((uint32_t)(seq_bpm > 0 ? seq_bpm : 120) * 12u);   /* a third of a sixteenth's, times n3 */
        fx[s].tstep = 0xFFFFFFFFu / (blocks ? blocks : 1u);
        if (seq_on && !clock_ext) {                     /* glided to where the bars put it (a jump would click) */
            uint32_t len = n3 << 8, want = seq_now() % len * 3u % len * (0xFFFFFFFFu / len);
            fx[s].tph += (uint32_t)((int32_t)(want - fx[s].tph) / 4);
        }
    }
}

/* ---- the ARP, a synth's (the owner's: the roll's third row, SEQ again): what reaches the synth (its keys, its roll,
 * the chords it plays, MIDI) goes into its pool, and it plays them one by one on the tempo: Up, Down, Hill (up and
 * down, its ends once), Valley (down and up), Bounce (its ends twice), Fold (the outside in), Unfold (the middle out),
 * Dice (at random), Walk (a step up or down at random), Played (as they came), Chord (all at once), Thumb (the lowest
 * between each), Pinky (the highest between each); over OCTAVE, at RATE, each held GATE of the step. HOLD keeps the
 * notes let go, till a new chord replaces them */
enum { AR_OFF, AR_UP, AR_DOWN, AR_UPDOWN, AR_DOWNUP, AR_BOUNCE, AR_CONV, AR_DIV, AR_RAND, AR_WALK, AR_PLAYED, AR_CHORD, AR_THUMB,
       AR_PINKY, AR_N };
#define ARP_POOL 16
static struct {
    uint8_t mode, div, oct, gate, hold;                 /* (sp_sync: the row's values) */
    uint8_t n, held, non, gated;                        /* notes in the pool, keys holding them, notes sounding, released */
    uint8_t note[ARP_POOL], vel[ARP_POOL], on[ARP_POOL];
    int32_t k, idx;                                     /* the steps played, the place (Walk) */
    uint32_t ph, rng;                                   /* samples into the step */
} arp[DMK_SYNTHS];
static const uint8_t ARP24[9] = {3, 4, 6, 8, 9, 12, 16, 18, 24};   /* RATE in 24ths of a beat (the screen's ARPDIVS) */
static uint32_t arp_len(int s) { return 110250u * ARP24[arp[s].div < 9 ? arp[s].div : 2] / (uint32_t)(seq_bpm > 0 ? seq_bpm : 120); }
static void arp_release(int s)                          /* what it sounds, let go */
{
    int i;
    for (i = 0; i < arp[s].non; i++) note_off_src(s, arp[s].on[i], SRC_ARP);
    arp[s].non = 0; arp[s].gated = 1;
}
static void arp_clear(int s) { arp_release(s); arp[s].n = 0; arp[s].held = 0; arp[s].k = 0; arp[s].idx = 0; }
static int arp_take(int s, int n, int vel)
{
    int i;
    if (!arp[s].mode) return 0;
    if (arp[s].hold && !arp[s].held) arp[s].n = 0;      /* HOLD: a new chord after all were let go replaces it */
    for (i = 0; i < arp[s].n && arp[s].note[i] != n; i++) {}
    if (i == arp[s].n) {
        if (arp[s].n >= ARP_POOL) return 1;
        if (!arp[s].n) { arp[s].k = 0; arp[s].idx = 0; arp[s].ph = arp_len(s); }   /* the first: at once */
        arp[s].note[arp[s].n] = (uint8_t)n; arp[s].vel[arp[s].n] = (uint8_t)vel; arp[s].n++;
    }
    arp[s].held++;
    return 1;
}
static int arp_drop(int s, int n)
{
    int i;
    if (!arp[s].mode) return 0;
    for (i = 0; i < arp[s].n && arp[s].note[i] != n; i++) {}
    if (i == arp[s].n) return 0;                        /* not the arp's: a note of before, let go as it was */
    if (arp[s].held) arp[s].held--;
    if (!arp[s].hold) {
        for (; i + 1 < arp[s].n; i++) { arp[s].note[i] = arp[s].note[i + 1]; arp[s].vel[i] = arp[s].vel[i + 1]; }
        arp[s].n--;
        if (!arp[s].n) arp_release(s);
    }
    return 1;
}
/* the step's notes: from the pool, sorted (or as played), over its octaves; i the place in that line of m */
static int arp_pick(int s, uint8_t *out, uint8_t *ov)
{
    uint8_t srt[ARP_POOL], sv[ARP_POOL];
    int n = arp[s].n, m, oc = arp[s].oct + 1, i, j, k = arp[s].k, at = 0;
    for (i = 0; i < n; i++) { srt[i] = arp[s].note[i]; sv[i] = arp[s].vel[i]; }
    if (arp[s].mode != AR_PLAYED)
        for (i = 1; i < n; i++) for (j = i; j > 0 && srt[j - 1] > srt[j]; j--) { uint8_t t = srt[j]; srt[j] = srt[j - 1]; srt[j - 1] = t; t = sv[j]; sv[j] = sv[j - 1]; sv[j - 1] = t; }
    if (arp[s].mode == AR_CHORD) {                      /* all of it, an octave higher each step over OCTAVES */
        for (i = 0; i < n; i++) { out[i] = (uint8_t)clampi(srt[i] + 12 * (k % oc), 0, 127); ov[i] = sv[i]; }
        return n;
    }
    m = n * oc;
    switch (arp[s].mode) {
    case AR_UP: case AR_PLAYED: at = k % m; break;
    case AR_DOWN: at = m - 1 - k % m; break;
    case AR_UPDOWN: if (m < 2) at = 0; else { int p = k % (2 * m - 2); at = p < m ? p : 2 * m - 2 - p; } break;
    case AR_DOWNUP: if (m < 2) at = 0; else { int p = k % (2 * m - 2); at = p < m ? m - 1 - p : p - m + 1; } break;
    case AR_BOUNCE: { int p = k % (2 * m); at = p < m ? p : 2 * m - 1 - p; } break;
    case AR_CONV: { int p = k % m; at = p & 1 ? m - 1 - p / 2 : p / 2; } break;
    case AR_DIV: { int p = k % m, mid = (m - 1) / 2; at = p & 1 ? mid + (p + 1) / 2 : mid - p / 2; at = clampi(at, 0, m - 1); } break;
    case AR_RAND: at = (int)(xs32(&arp[s].rng) % (uint32_t)m); if (m > 1 && at == arp[s].idx) at = (at + 1) % m; break;
    case AR_WALK: at = clampi(arp[s].idx + (xs32(&arp[s].rng) & 1u ? 1 : -1), 0, m - 1); if (m > 1 && at == arp[s].idx) at = arp[s].idx ? arp[s].idx - 1 : 1; break;
    case AR_THUMB: if (m < 2) at = 0; else { int p = k % (2 * (m - 1)); at = p & 1 ? p / 2 + 1 : 0; } break;
    case AR_PINKY: if (m < 2) at = 0; else { int p = k % (2 * (m - 1)); at = p & 1 ? m - 1 : p / 2; } break;
    default: at = 0;
    }
    arp[s].idx = at;
    out[0] = (uint8_t)clampi(srt[at % n] + 12 * (at / n), 0, 127);
    ov[0] = sv[at % n];
    return 1;
}
static void arp_tick(int frames)                        /* each synth's, on the tempo, playing or not */
{
    int s, i;
    for (s = 0; s < DMK_SYNTHS; s++) {
        uint32_t len, gl;
        if (!arp[s].mode || (!arp[s].n && !arp[s].non)) continue;
        len = arp_len(s);
        gl = arp[s].gate >= 127 ? len : len * (8u + arp[s].gate) / 136u;   /* 6 % .. all of it */
        arp[s].ph += (uint32_t)frames;
        if (!arp[s].gated && arp[s].ph >= gl && gl < len) arp_release(s);
        if (arp[s].ph >= len) {
            uint8_t nt[ARP_POOL], nv[ARP_POOL];
            int c;
            arp[s].ph = arp[s].ph - len < len ? arp[s].ph - len : 0;
            arp_release(s);
            if (!arp[s].n) continue;
            c = arp_pick(s, nt, nv);
            for (i = 0; i < c; i++) { note_on_vel(s, nt[i], SRC_ARP, nv[i]); arp[s].on[i] = nt[i]; }
            arp[s].non = (uint8_t)c; arp[s].gated = 0;
            arp[s].k++;
        }
    }
}
static void arp_restart(void) { int s; for (s = 0; s < DMK_SYNTHS; s++) { arp[s].ph = arp_len(s); arp[s].k = 0; } }   /* PLAY: in time */

static void seq_render(int frames)
{
    uint32_t per;
    lfo_sync();
    seq_frames += (uint32_t)frames;
    later_tick(frames);                                 /* the strum's notes, playing or not */
    roll_tick(frames);                                  /* the rolls' notes late in their step */
    arp_tick(frames);                                   /* the arps, playing or not */
    if (!seq_on || ext_running()) return;               /* an outside clock running steps it instead (dmk_midi) */
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
/* a key recorded on synth s: where on its own roll it lands (its own playhead, which a pattern or a scene switched, or
 * its length, moved from the song's count), Q8 steps: as played, its micro timing kept (the owner) */
static uint32_t seq_rec_q8(int s) { return ((uint32_t)seq_playing_step(s) << 8) + (seq_now() & 255u); }

static void seq_init(void)
{
    /* the first song's patterns, A minor at 72: the pad's chords over two bars, the piano's line over one, the
     * pluck's figure over twelve steps, so it lands somewhere new each bar */
    static const uint8_t PAD[][3] = {{0, 16, 57}, {0, 16, 60}, {0, 16, 64}, {16, 16, 53}, {16, 16, 57}, {16, 16, 60}};
    static const uint8_t PIANO[][3] = {{0, 3, 69}, {4, 2, 72}, {6, 2, 71}, {8, 4, 67}, {12, 4, 64}};
    static const uint8_t PLUCK[][3] = {{0, 1, 76}, {3, 1, 69}, {6, 1, 72}, {9, 1, 74}};
    ext_seen = 0;                                       /* no outside clock heard yet */
    unsigned i;
    for (i = 0; i < DMK_SYNTHS; i++) { arp[i].n = arp[i].held = arp[i].non = 0; arp[i].gated = 1; arp[i].rng = 0x9E3779B9u * (i + 7u); }
    for (i = 0; i < DMK_SYNTHS; i++) { sq[i].count = 0; sq[i].pos = 0; sq[i].rng = 0x2545F491u * (i + 1u); }   /* (Vary's dice) */
    sq[0].steps = 32; sq[1].steps = 16; sq[2].steps = 12;
    for (i = 0; i < sizeof PAD / sizeof PAD[0]; i++) seq_add(0, PAD[i][0], PAD[i][1], PAD[i][2], 100);
    for (i = 0; i < sizeof PIANO / sizeof PIANO[0]; i++) seq_add(1, PIANO[i][0], PIANO[i][1], PIANO[i][2], 100);
    for (i = 0; i < sizeof PLUCK / sizeof PLUCK[0]; i++) seq_add(2, PLUCK[i][0], PLUCK[i][1], PLUCK[i][2], 100);
    seq_on = seq_rec = 0;
    for (i = 0; i < DMK_SYNTHS; i++) { unsigned k; for (k = 0; k < SEQ_LIVE; k++) sq[i].left[k] = 0, sq[i].wait[k] = 0; }
    pat_init();
    song_init();
}
