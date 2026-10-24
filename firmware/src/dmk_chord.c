/* SPDX-License-Identifier: GPL-3.0-only */
/* The chord machine (docs/plan.md, 4): the song's key and mode; a progression (one of sixteen presets, each its own
 * mode and voicing) whose chords change on the sequences' clock; the chords voiced (CLOSE, OPEN, WIDE, SHELL, QUARTAL),
 * inverted for the least motion (AUTO) or as asked, spread, strummed, held or pulsed or arpeggiated. A synth that
 * PLAYS CHORDS plays them; the others FOLLOW: KEY (their notes on the scale as the chord bends it), CHORD (on the
 * chord's tones), ROOT (moved with the chord's root), FREE. On the CHORD page the keys play chords: a key, the
 * mode's chord built on it. Integer only. Included by dmk_core.c after dmk_seq.c */

enum { Q_M, Q_MAJ, Q_M7, Q_MAJ7, Q_7, Q_M7B5, Q_MADD9, Q_SUS2, Q_M9, Q_MAJ9, Q_6, Q_M11, Q_ADD9, Q_MMAJ7, Q_M6, Q_7SUS4, Q_9, Q_DIM, Q_COUNT };
static const int8_t QI[Q_COUNT][7] = {          /* a quality's tones, semitones above the root, -1 ends */
    {0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 10, -1}, {0, 4, 7, 11, -1}, {0, 4, 7, 10, -1}, {0, 3, 6, 10, -1},
    {0, 3, 7, 14, -1}, {0, 2, 7, -1}, {0, 3, 7, 10, 14, -1}, {0, 4, 7, 11, 14, -1}, {0, 4, 7, 9, -1},
    {0, 3, 7, 10, 14, 17, -1}, {0, 4, 7, 14, -1}, {0, 3, 7, 11, -1}, {0, 3, 7, 9, -1}, {0, 5, 7, 10, -1},
    {0, 4, 7, 10, 14, -1}, {0, 3, 6, -1}};
static const char *const QN[Q_COUNT] = {"m", "", "m7", "maj7", "7", ("m7\xE2\x99\xAD" "5"), "m(add9)", "sus2", "m9",
                                        "maj9", "6", "m11", "add9", "m(maj7)", "m6", "7sus4", "9", "dim"};

enum { MO_AEOL, MO_DOR, MO_PHRY, MO_LYD, MO_MIXO, MO_ION, MO_COUNT };
enum { VO_CLOSE, VO_OPEN, VO_WIDE, VO_SHELL, VO_QUART, VO_COUNT };
enum { FW_FREE, FW_KEY, FW_CHORD, FW_ROOT, FW_PLAYS, FW_COUNT };
enum { RH_HOLD, RH_PULSE4, RH_PULSE8, RH_ARPUP, RH_ARPDN, RH_COUNT };

/* the presets (plan 4.2): a chord a slot, its root in semitones above the key, its quality, its length in half-bars,
 * a pedal (the key's tonic under it) */
typedef struct { uint8_t root, q, half, pedal; } cslot_t;
typedef struct { const char *name; uint8_t mode, voicing, n; cslot_t s[5]; } prog_t;
static const prog_t PROG[16] = {
    {"First snow", MO_AEOL, VO_WIDE, 4, {{0, Q_MADD9, 2, 0}, {8, Q_MAJ7, 2, 0}, {3, Q_MAJ7, 2, 0}, {10, Q_SUS2, 2, 0}}},
    {"Last train", MO_AEOL, VO_OPEN, 4, {{0, Q_M9, 2, 0}, {5, Q_M9, 2, 0}, {8, Q_MAJ7, 2, 0}, {7, Q_M7, 2, 0}}},
    {"Fogbound", MO_AEOL, VO_WIDE, 3, {{8, Q_MAJ9, 2, 0}, {10, Q_6, 2, 0}, {0, Q_M9, 4, 0}}},
    {"Overpass", MO_AEOL, VO_CLOSE, 4, {{8, Q_MAJ7, 2, 0}, {10, Q_7, 2, 0}, {7, Q_M7, 2, 0}, {0, Q_M7, 2, 0}}},
    {"Vending light", MO_AEOL, VO_SHELL, 5, {{8, Q_MAJ7, 2, 0}, {7, Q_7, 2, 0}, {0, Q_M7, 2, 0}, {7, Q_M7, 1, 0}, {0, Q_7, 1, 0}}},
    {"Blue hour", MO_DOR, VO_OPEN, 2, {{0, Q_M9, 4, 0}, {5, Q_9, 4, 0}}},
    {"Power lines", MO_DOR, VO_QUART, 4, {{0, Q_M11, 2, 0}, {10, Q_MAJ7, 2, 0}, {3, Q_MAJ7, 2, 0}, {5, Q_ADD9, 2, 0}}},
    {"Tower beacon", MO_PHRY, VO_CLOSE, 4, {{0, Q_M7, 2, 0}, {1, Q_MAJ7, 2, 0}, {0, Q_M7, 2, 0}, {10, Q_M7, 2, 0}}},
    {"Slow thaw", MO_LYD, VO_WIDE, 4, {{0, Q_MAJ9, 2, 0}, {2, Q_ADD9, 2, 0}, {4, Q_M7, 2, 0}, {2, Q_ADD9, 2, 0}}},
    {"Candle", MO_AEOL, VO_CLOSE, 4, {{0, Q_M, 2, 0}, {0, Q_MMAJ7, 2, 0}, {0, Q_M7, 2, 0}, {0, Q_M6, 2, 0}}},
    {"Embers", MO_AEOL, VO_OPEN, 4, {{0, Q_M9, 2, 0}, {5, Q_M9, 2, 0}, {0, Q_M9, 2, 0}, {8, Q_MAJ7, 2, 0}}},
    {"Low clouds", MO_AEOL, VO_WIDE, 4, {{0, Q_MADD9, 2, 0}, {8, Q_MAJ7, 2, 1}, {10, Q_MAJ, 2, 1}, {0, Q_MADD9, 2, 0}}},
    {"Snowfield", MO_AEOL, VO_WIDE, 2, {{8, Q_MAJ7, 4, 0}, {0, Q_MADD9, 4, 0}}},
    {"Static", MO_AEOL, VO_OPEN, 4, {{0, Q_SUS2, 2, 0}, {10, Q_SUS2, 2, 0}, {8, Q_SUS2, 2, 0}, {10, Q_SUS2, 2, 0}}},
    {"Mixtape", MO_ION, VO_OPEN, 4, {{0, Q_MAJ9, 2, 0}, {4, Q_M7, 2, 0}, {5, Q_MAJ7, 2, 0}, {5, Q_M6, 2, 0}}},
    {"Night drive", MO_AEOL, VO_WIDE, 5, {{0, Q_MADD9, 2, 0}, {10, Q_6, 2, 0}, {8, Q_MAJ7, 2, 0}, {7, Q_7SUS4, 1, 0}, {7, Q_7, 1, 0}}}};

/* the song's harmony, set from the CHORD page (sp_sync) */
static struct {
    uint8_t prog, voicing, invert, spread, change, rhythm;   /* invert: 0 AUTO, then 1..4 the inversions 0..3 */
    int16_t strum;                                       /* ms between voices, + up, - down */
    uint8_t follow[DMK_SYNTHS];
} ch = {0, VO_WIDE, 0, 0, 1, RH_HOLD, 0, {FW_PLAYS, FW_KEY, FW_ROOT}};

/* what sounds: the slot, its notes as voiced (the last ones too, for AUTO), its tones as a mask */
static uint8_t ch_slot, ch_notes[7], ch_n, ch_prev[7], ch_pn, ch_arp, ch_play[7], ch_np;
static uint16_t ch_left, ch_cmask = 0, ch_root;
static int ch_shown = -1;                               /* the chord on the page: a slot, or -2 a key's */
static char ch_name[16];

/* ---- the strum's queue: notes to start a few ms from now, as the render runs */
#define LATER_N 16
static struct { int32_t at; uint8_t sy, note, used; } later[LATER_N];
static void later_on(int sy, int note, int32_t samples)
{
    int i;
    if (samples <= 0) { note_on_s(sy, note); return; }
    for (i = 0; i < LATER_N && later[i].used; i++) {}
    if (i == LATER_N) { note_on_s(sy, note); return; }
    later[i].at = samples; later[i].sy = (uint8_t)sy; later[i].note = (uint8_t)note; later[i].used = 1;
}
static void later_off(int sy, int note)                 /* off, and never started if it was still to come */
{
    int i;
    for (i = 0; i < LATER_N; i++) if (later[i].used && later[i].sy == sy && later[i].note == note) later[i].used = 0;
    note_off_s(sy, note);
}
static void later_tick(int frames)
{
    int i;
    for (i = 0; i < LATER_N; i++)
        if (later[i].used && (later[i].at -= frames) <= 0) { later[i].used = 0; note_on_s(later[i].sy, later[i].note); }
}

/* ---- pitch-class sets: the scale, the chord, the scale as the chord bends it (plan 4.6) */
static uint16_t pc_scale(void) { int i; uint16_t m = 0; for (i = 0; i < 7; i++) m |= (uint16_t)(1u << ((seq_key + SCALE[i]) % 12)); return m; }
static uint16_t pc_bent(void)
{
    uint16_t s = pc_scale(), c = ch_cmask;
    int t;
    for (t = 0; t < 12; t++)
        if (c >> t & 1u && !(s >> t & 1u)) {            /* a chord tone outside: it takes its neighbour's place */
            if (s >> ((t + 11) % 12) & 1u && !(c >> ((t + 11) % 12) & 1u)) s &= (uint16_t)~(1u << ((t + 11) % 12));
            else if (s >> ((t + 1) % 12) & 1u && !(c >> ((t + 1) % 12) & 1u)) s &= (uint16_t)~(1u << ((t + 1) % 12));
            s |= (uint16_t)(1u << t);
        }
    return s;
}
static int snap_set(int p, uint16_t set, uint16_t pref)  /* the nearest pitch in the set; a tie to a chord tone, else down */
{
    int d;
    if (!set) return p;
    for (d = 0; d < 7; d++) {
        int lo = p - d, hi = p + d, a = set >> ((lo % 12 + 12) % 12) & 1u, b = set >> (hi % 12) & 1u;
        if (a && b) return pref >> (hi % 12) & 1u && !(pref >> ((lo % 12 + 12) % 12) & 1u) ? hi : lo;
        if (a) return lo;
        if (b) return hi;
    }
    return p;
}

static int plays_chords(int s) { return ch.follow[s] == FW_PLAYS; }

/* a synth's note as it plays, by its FOLLOW */
static int follow_note(int s, int n)
{
    switch (ch.follow[s]) {
    case FW_KEY: return snap_set(n, pc_bent(), ch_cmask);
    case FW_CHORD: return snap_set(n, ch_cmask ? ch_cmask : pc_scale(), ch_cmask);
    case FW_ROOT: {
        int first = seq_key + PROG[ch.prog].s[0].root, d = degree_of(snap(48 + ch_root % 12)) - degree_of(snap(48 + first % 12));
        return snap_set(note_of(degree_of(snap(n)) + d), pc_bent(), ch_cmask);
    }
    default: return n;
    }
}

/* ---- voicing: a root and its tones to at most six notes (plan 4.3) */
static void sort_notes(uint8_t *a, int n) { int i, j; for (i = 1; i < n; i++) for (j = i; j > 0 && a[j - 1] > a[j]; j--) { uint8_t t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; } }
static int motion(const uint8_t *a, int n, const uint8_t *b, int m)   /* how far a chord moves from the last */
{
    int i, j, sum = 0;
    for (i = 0; i < n; i++) {
        int best = 99;
        for (j = 0; j < m; j++) { int d = a[i] > b[j] ? a[i] - b[j] : b[j] - a[i]; if (d < best) best = d; }
        sum += m ? best : 0;
    }
    return sum;
}
static int voice(int root, const int8_t *iv, int pedal, uint8_t *out)
{
    int n = 0, i, k;
    uint8_t t[7];
    for (i = 0; i < 6 && iv[i] >= 0; i++) t[n++] = (uint8_t)(root + iv[i]);
    if (ch.voicing == VO_CLOSE || ch.voicing == VO_OPEN) {
        for (i = 0; i < n; i++) t[i] = (uint8_t)(root + (t[i] - root) % 12);
        sort_notes(t, n);
        if (ch.invert || ch_pn) {                       /* the inversion: as asked, or the one that moves least */
            uint8_t best[7], cand[7];
            int bm = 9999, from = ch.invert ? ch.invert - 1 : 0, to = ch.invert ? ch.invert - 1 : n - 1;
            for (k = from; k <= to && k < n; k++) {
                int m;
                for (i = 0; i < n; i++) cand[i] = (uint8_t)(t[i] + (i < k ? 12 : 0));
                sort_notes(cand, n);
                m = ch.invert ? 0 : motion(cand, n, ch_prev, ch_pn);
                if (m < bm) { bm = m; for (i = 0; i < n; i++) best[i] = cand[i]; }
            }
            for (i = 0; i < n; i++) t[i] = best[i];
        }
        if (ch.voicing == VO_OPEN && n >= 3) { t[n - 2] = (uint8_t)(t[n - 2] - 12); sort_notes(t, n); }   /* drop-2 */
    } else if (ch.voicing == VO_WIDE) {                 /* the root low, the fifth, the colours an octave up */
        uint8_t w[7];
        int m = 0;
        w[m++] = (uint8_t)(root - 12);
        for (i = 1; i < n; i++) w[m++] = (uint8_t)(iv[i] % 12 == 7 ? root + 7 : root + 12 + iv[i] % 12 + (iv[i] >= 12 ? 0 : 0));
        for (i = 0; i < m; i++) t[i] = w[i];
        n = m;
        sort_notes(t, n);
    } else if (ch.voicing == VO_SHELL) {                /* the root, the seventh (or sixth), the third above */
        int third = -1, sev = -1, ninth = -1;
        for (i = 1; iv[i] >= 0; i++) {
            if (iv[i] == 3 || iv[i] == 4 || iv[i] == 2 || iv[i] == 5) third = iv[i];
            if (iv[i] == 9 || iv[i] == 10 || iv[i] == 11) sev = iv[i];
            if (iv[i] == 14) ninth = 14;
        }
        n = 0;
        t[n++] = (uint8_t)root;
        if (sev >= 0) t[n++] = (uint8_t)(root + sev);
        if (third >= 0) t[n++] = (uint8_t)(root + 12 + third);
        if (ninth >= 0) t[n++] = (uint8_t)(root + 12 + ninth);
        if (n < 3) t[n++] = (uint8_t)(root + 7);
        sort_notes(t, n);
    } else {                                            /* fourths from the root, each on the mode */
        n = 4;
        for (i = 0; i < n; i++) t[i] = (uint8_t)snap(root + i * 5);
    }
    for (i = 1; i < n && ch.spread; i++) t[i] = (uint8_t)(t[i] + 12 * (ch.spread * i / (n > 1 ? n - 1 : 1)));   /* SPREAD */
    if (pedal && n < 6) { for (i = n; i > 0; i--) t[i] = t[i - 1]; t[0] = (uint8_t)(36 + seq_key); n++; }   /* the tonic under it */
    while (t[n - 1] > 96 && t[0] >= 36) for (i = 0; i < n; i++) t[i] = (uint8_t)(t[i] - 12);
    while (t[0] < 33) for (i = 0; i < n; i++) t[i] = (uint8_t)(t[i] + 12);
    for (i = 0; i < n; i++) out[i] = t[i];
    for (i = 0; i < n; i++) ch_prev[i] = t[i];
    ch_pn = (uint8_t)n;
    return n;
}

/* the short names, for the progression's pills */
static const char *const QS[Q_COUNT] = {"m", "", "m7", "maj7", "7", ("m7\xE2\x99\xAD" "5"), "madd9", "sus2", "m9",
                                        "maj9", "6", "m11", "add9", "mM7", "m6", "7sus4", "9", "dim"};
static void chord_name_q(char *o, int root, int q, const char *const *names);
static void chord_name(char *o, int root, int q) { chord_name_q(o, root, q, QN); }
static void chord_name_q(char *o, int root, int q, const char *const *names)
{
    static const char *const NM[12] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G",
                                       "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
    str_cat(str_cat(o, NM[root % 12]), names[q]);
}

/* the mode's chord on a scale tone: its seventh, named by what it turns out to be */
static int diatonic(int n, int8_t *iv)
{
    int d = degree_of(snap(n)), r = note_of(d), q;
    int a = note_of(d + 2) - r, b = note_of(d + 4) - r, c = note_of(d + 6) - r;
    iv[0] = 0; iv[1] = (int8_t)a; iv[2] = (int8_t)b; iv[3] = (int8_t)c; iv[4] = -1;
    q = a == 3 ? (b == 6 ? Q_M7B5 : c == 11 ? Q_MMAJ7 : Q_M7) : c == 11 ? Q_MAJ7 : Q_7;
    return q;
}

/* strike notes on a synth, strummed */
static void strike(int sy, const uint8_t *notes, int n)
{
    int i, ms = ch.strum < 0 ? -ch.strum : ch.strum;
    for (i = 0; i < n; i++) {
        int k = ch.strum < 0 ? n - 1 - i : i;
        later_on(sy, notes[k], (int32_t)ms * i * (DMK_SR / 1000));
    }
}

/* the sounding chord off on the synths that play chords */
static void chord_quiet(void)
{
    int s, i;
    for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) for (i = 0; i < ch_np; i++) later_off(s, ch_play[i]);
    ch_np = 0;
}

static void chord_start(void) { ch_slot = 0; ch_left = 0; ch_arp = 0; ch_pn = 0; }
static void chord_init(void)                            /* the first song's: the pad plays the chords, the piano follows */
{
    int i;
    ch.follow[0] = FW_PLAYS; ch.follow[1] = FW_KEY; ch.follow[2] = FW_ROOT;
    ch_np = 0; ch_cmask = 0; ch_shown = -1; ch_name[0] = 0;
    for (i = 0; i < LATER_N; i++) later[i].used = 0;
    chord_start();
}

/* each step of the sequences' clock: a new slot when the last has had its length; the rhythm in between */
static void chord_step(void)
{
    const prog_t *pr = &PROG[ch.prog];
    int s, i, newslot = !ch_left;
    if (newslot) {
        const cslot_t *c = &pr->s[ch_slot % pr->n];
        int root = 48 + (seq_key + c->root) % 12, len = c->half * 8;
        chord_quiet();
        ch_n = (uint8_t)voice(root, QI[c->q], c->pedal, ch_notes);
        ch_root = (uint16_t)root;
        ch_cmask = 0;
        for (i = 0; i < ch_n; i++) ch_cmask |= (uint16_t)(1u << (ch_notes[i] % 12));
        ch_name[0] = 0;
        chord_name(ch_name, root, c->q);
        ch_shown = ch_slot % pr->n;
        len = ch.change == 0 ? len / 2 : ch.change == 2 ? len * 2 : ch.change == 3 ? len * 4 : len;
        ch_left = (uint16_t)(len < 1 ? 1 : len);
        ch_slot = (uint8_t)((ch_slot + 1) % pr->n);
        ch_arp = 0;
    }
    if (ch.rhythm == RH_HOLD) {
        if (newslot) {
            for (i = 0; i < ch_n; i++) ch_play[i] = ch_notes[i];
            ch_np = ch_n;
            for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) strike(s, ch_notes, ch_n);
        }
    } else if (ch.rhythm == RH_PULSE4 || ch.rhythm == RH_PULSE8) {
        int every = ch.rhythm == RH_PULSE4 ? 4 : 2;
        if (newslot || !(ch_left % every)) {
            chord_quiet();
            for (i = 0; i < ch_n; i++) ch_play[i] = ch_notes[i];
            ch_np = ch_n;
            for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) strike(s, ch_notes, ch_n);
        }
    } else {                                            /* the arpeggio: a note a step */
        int k = ch_arp % ch_n, note = ch.rhythm == RH_ARPUP ? ch_notes[k] : ch_notes[ch_n - 1 - k];
        chord_quiet();
        ch_play[0] = (uint8_t)note;
        ch_np = 1;
        for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) note_on_s(s, note);
        ch_arp++;
    }
    ch_left--;
}

/* the CHORD page's keys: a key, the mode's chord on it, voiced as the page says, on the current synth */
static int chord_key(int n, uint8_t *notes)
{
    int8_t iv[6];
    int q = diatonic(n, iv), r = note_of(degree_of(snap(n))), c, i;
    c = voice(r, iv, 0, notes);
    ch_name[0] = 0;
    chord_name(ch_name, r, q);
    ch_shown = -2;
    ch_cmask = 0;
    for (i = 0; i < c; i++) ch_cmask |= (uint16_t)(1u << (notes[i] % 12));
    return c;
}
