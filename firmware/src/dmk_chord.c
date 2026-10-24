/* SPDX-License-Identifier: GPL-3.0-only */
/* The chord machine (docs/plan.md, 4): the song's key and mode; a progression (one of sixteen presets, each its own
 * mode and voicing) whose chords change on the sequences' clock; the chords voiced (CLOSE, OPEN, WIDE, SHELL, QUARTAL),
 * inverted for the least motion (AUTO) or as asked, spread, strummed, held or pulsed or arpeggiated. A synth that
 * PLAYS CHORDS plays them; the others FOLLOW: KEY (their notes on the scale as the chord bends it), CHORD (on the
 * chord's tones), ROOT (moved with the chord's root), FREE. On the CHORD page the keys play chords: a key, the
 * mode's chord built on it. Integer only. Included by dmk_core.c after dmk_seq.c */

/* the qualities; the later ones appended, so a song saved keeps its numbers */
enum { Q_M, Q_MAJ, Q_M7, Q_MAJ7, Q_7, Q_M7B5, Q_MADD9, Q_SUS2, Q_M9, Q_MAJ9, Q_6, Q_M11, Q_ADD9, Q_MMAJ7, Q_M6, Q_7SUS4, Q_9, Q_DIM,
       Q_69, Q_M69, Q_SUS4, Q_AUG, Q_5, Q_MAJ7S11, Q_13, Q_MADD11, Q_ADD11, Q_DIM7, Q_COUNT };
static const int8_t QI[Q_COUNT][7] = {          /* a quality's tones, semitones above the root, -1 ends */
    {0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 10, -1}, {0, 4, 7, 11, -1}, {0, 4, 7, 10, -1}, {0, 3, 6, 10, -1},
    {0, 3, 7, 14, -1}, {0, 2, 7, -1}, {0, 3, 7, 10, 14, -1}, {0, 4, 7, 11, 14, -1}, {0, 4, 7, 9, -1},
    {0, 3, 7, 10, 14, 17, -1}, {0, 4, 7, 14, -1}, {0, 3, 7, 11, -1}, {0, 3, 7, 9, -1}, {0, 5, 7, 10, -1},
    {0, 4, 7, 10, 14, -1}, {0, 3, 6, -1},
    {0, 4, 7, 9, 14, -1}, {0, 3, 7, 9, 14, -1}, {0, 5, 7, -1}, {0, 4, 8, -1}, {0, 7, -1}, {0, 4, 7, 11, 18, -1},
    {0, 4, 7, 10, 14, 21, -1}, {0, 3, 7, 17, -1}, {0, 4, 7, 17, -1}, {0, 3, 6, 9, -1}};
static const char *const QN[Q_COUNT] = {"m", "", "m7", "maj7", "7", ("m7\xE2\x99\xAD" "5"), "m(add9)", "sus2", "m9",
                                        "maj9", "6", "m11", "(add9)", "m(maj7)", "m6", "7sus4", "9", "dim",
                                        "6/9", "m6/9", "sus4", "aug", "5", ("maj7\xE2\x99\xAF" "11"), "13", "m(add11)",
                                        "(add11)", "dim7"};

enum { MO_AEOL, MO_DOR, MO_PHRY, MO_LYD, MO_MIXO, MO_ION, MO_COUNT };
enum { VO_CLOSE, VO_OPEN, VO_WIDE, VO_SHELL, VO_QUART, VO_COUNT };
enum { FW_FREE, FW_KEY, FW_CHORD, FW_ROOT, FW_PLAYS, FW_HARM1, FW_HARM2, FW_HARM3, FW_COUNT };   /* HARMn: synth n's notes, moved */
enum { RH_HOLD, RH_PULSE4, RH_PULSE8, RH_ARPUP, RH_ARPDN, RH_COUNT };

/* the presets (plan 4.2): a chord a slot, its root in semitones above the key, its quality, its length in half-bars,
 * a pedal (the key's tonic under it) */
typedef struct { uint8_t root, q, half, pedal; } cslot_t;
#define PROG_SLOTS 8
typedef struct { const char *name; uint8_t mode, voicing, n; cslot_t s[PROG_SLOTS]; } prog_t;
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

/* the four USER progressions (plan 4.4): written from the keys on CHORD, stopped, with REC armed; a preset's first
 * edit copies it into the first empty one */
static prog_t uprog[4] = {{"User 1", MO_AEOL, VO_WIDE, 0, {{0}}}, {"User 2", MO_AEOL, VO_WIDE, 0, {{0}}},
                          {"User 3", MO_AEOL, VO_WIDE, 0, {{0}}}, {"User 4", MO_AEOL, VO_WIDE, 0, {{0}}}};
#define PROG_N 20                                       /* the sixteen presets, then the four of the user's */

/* the song's harmony, set from the CHORD page (sp_sync) */
static struct {
    uint8_t prog, voicing, invert, spread, change, rhythm;   /* invert: 0 AUTO, then 1..4 the inversions 0..3 */
    int16_t strum;                                       /* ms between voices, + up, - down */
    uint8_t follow[DMK_SYNTHS];
    uint8_t keys, ktype;                                 /* the keys play chords: on CHORD (0) or everywhere (1); their type */
    int8_t hint[DMK_SYNTHS];                             /* HARMONY: the interval over the lead, in scale steps (-7..7) */
} ch = {0, VO_WIDE, 0, 0, 1, RH_HOLD, 0, {FW_PLAYS, FW_KEY, FW_ROOT}, 0, 0, {2, 2, 2}};
enum { KT_SEVENTH, KT_TRIAD, KT_NINTH, KT_ADD9, KT_SUS2, KT_SUS4, KT_69, KT_POWER, KT_COUNT };

/* what sounds: the slot, its notes as voiced (the last ones too, for AUTO), its tones as a mask */
static uint8_t ch_slot, ch_notes[7], ch_n, ch_prev[7], ch_pn, ch_arp, ch_play[7], ch_np;
static uint8_t ch_cur, ch_len = 2;                     /* writing: the slot cursor, the last length (half-bars) */
static uint16_t ch_left, ch_cmask = 0, ch_root;
static uint8_t ch_q;                                    /* the sounding chord's quality */
static int ch_shown = -1;                               /* the chord on the page: a slot, or -2 a key's */
static char ch_name[16];

/* ---- the strum's queue: notes to start a few ms from now, as the render runs */
#define LATER_N 16
static struct { int32_t at; uint8_t sy, note, src, used; } later[LATER_N];
static void later_on(int sy, int note, int32_t samples, int src)
{
    int i;
    if (samples <= 0) { note_on_src(sy, note, src); return; }
    for (i = 0; i < LATER_N && later[i].used; i++) {}
    if (i == LATER_N) { note_on_src(sy, note, src); return; }
    later[i].at = samples; later[i].sy = (uint8_t)sy; later[i].note = (uint8_t)note; later[i].src = (uint8_t)src; later[i].used = 1;
}
static void later_off(int sy, int note, int src)        /* off, and never started if it was still to come */
{
    int i;
    for (i = 0; i < LATER_N; i++) if (later[i].used && later[i].sy == sy && later[i].note == note && later[i].src == src) later[i].used = 0;
    note_off_src(sy, note, src);
}
static void later_tick(int frames)
{
    int i;
    for (i = 0; i < LATER_N; i++)
        if (later[i].used && (later[i].at -= frames) <= 0) { later[i].used = 0; note_on_src(later[i].sy, later[i].note, later[i].src); }
}

/* ---- pitch-class sets: the scale, the chord, the scale as the chord bends it (plan 4.6) */
static uint16_t pc_scale(void) { int i; uint16_t m = 0; for (i = 0; i < 7; i++) m |= (uint16_t)(1u << ((seq_key + SCALE[i]) % 12)); return m; }
static uint16_t pc_bent(void)
{
    uint16_t s = pc_scale(), c = ch_cmask;
    int t;
    for (t = 0; t < 12; t++)
        if (c >> t & 1u && !(s >> t & 1u)) {            /* a chord tone outside: it takes its own degree's place */
            int iv = (t - (int)(ch_root % 12) + 12) % 12, up = !(iv == 2 || iv == 4 || iv == 9 || iv == 11);
            int a = up ? (t + 1) % 12 : (t + 11) % 12, b = up ? (t + 11) % 12 : (t + 1) % 12;   /* lowered: the tone above */
            if (s >> a & 1u && !(c >> a & 1u)) s &= (uint16_t)~(1u << a);
            else if (s >> b & 1u && !(c >> b & 1u)) s &= (uint16_t)~(1u << b);
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
/* HARMONY (after the sequencers that harmonize one track from another): a synth that follows another plays each note
 * the lead plays, its roll left, moved by its interval in scale steps on the scale as the chord bends it (S') */
static int harm_of(int s) { return ch.follow[s] >= FW_HARM1 && ch.follow[s] <= FW_HARM3 ? ch.follow[s] - FW_HARM1 : -1; }
static int harm_note(int f, int n)
{
    int h = note_of(degree_of(snap(n)) + ch.hint[f]);
    return snap_set(h, pc_bent(), ch_cmask);
}
static const prog_t *cur_prog(void) { return ch.prog < 16 ? &PROG[ch.prog] : &uprog[ch.prog - 16]; }

/* a synth's note as it plays, by its FOLLOW */
static int follow_note(int s, int n)
{
    switch (ch.follow[s]) {
    case FW_KEY: return snap_set(n, pc_bent(), ch_cmask);
    case FW_CHORD: return snap_set(n, ch_cmask ? ch_cmask : pc_scale(), ch_cmask);
    case FW_ROOT: {                                     /* the roll moved by the root's steps from the first chord's,
                                                           * counted from the key's tonic, the shortest way */
        int t = 48 + seq_key, first = cur_prog()->s[0].root % 12, now = ((int)(ch_root % 12) - seq_key + 12) % 12;
        int d = degree_of(snap(t + now)) - degree_of(snap(t + first));
        if (d > 3) d -= 7;
        if (d < -3) d += 7;
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
static void invert_up(uint8_t *t, int n, int k)        /* the lowest k notes an octave up */
{
    int i;
    for (i = 0; i < k && i < n; i++) t[i] = (uint8_t)(t[i] + 12);
    sort_notes(t, n);
}
/* a chord voiced: lead set, it is the progression's and AUTO's next inversion is measured from it */
static int voice_x(int root, const int8_t *iv, int pedal, uint8_t *out, int lead)
{
    int n = 0, i, k;
    uint8_t t[8];
    for (i = 0; i < 6 && iv[i] >= 0; i++) t[n++] = (uint8_t)(root + iv[i]);
    if (ch.voicing == VO_CLOSE || ch.voicing == VO_OPEN) {
        for (i = 0; i < n; i++) t[i] = (uint8_t)(root + (t[i] - root) % 12);
        sort_notes(t, n);
        if (ch.invert || ch_pn) {                       /* the inversion: as asked, or the one that moves least */
            uint8_t best[8], cand[8];
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
    } else {
        if (ch.voicing == VO_WIDE) {                    /* the root low, the fifth, the colours an octave up */
            uint8_t w[8];
            int m = 0;
            w[m++] = (uint8_t)(root - 12);
            for (i = 1; i < n; i++) w[m++] = (uint8_t)(iv[i] % 12 == 7 ? root + 7 : root + 12 + iv[i] % 12);
            for (i = 0; i < m; i++) t[i] = w[i];
            n = m;
            sort_notes(t, n);
        } else if (ch.voicing == VO_SHELL) {            /* the root, the seventh (or sixth), the third, an altered fifth,
                                                         * the highest colour */
            int third = -1, sev = -1, fifth = -1, ext = -1;
            for (i = 1; iv[i] >= 0; i++) {
                int v = iv[i];
                if ((v == 3 || v == 4) || ((v == 2 || v == 5) && third < 0)) third = v;
                if (v == 9 || v == 10 || v == 11) sev = v;
                if (v == 6 || v == 8) fifth = v;
                if (v >= 12 && v > ext) ext = v;
            }
            if (fifth == 6 && sev == 9 && third == 3) sev = 9;   /* dim7: its sixth is its seventh */
            n = 0;
            t[n++] = (uint8_t)root;
            if (sev >= 0) t[n++] = (uint8_t)(root + sev);
            if (third >= 0) t[n++] = (uint8_t)(root + 12 + third);
            if (fifth >= 0) t[n++] = (uint8_t)(root + 12 + fifth);
            if (ext >= 0) t[n++] = (uint8_t)(root + 12 + ext);
            if (n < 3 && fifth < 0) t[n++] = (uint8_t)(root + 7);
            sort_notes(t, n);
        } else {                                        /* fourths from the root, each on the mode */
            n = 4;
            for (i = 0; i < n; i++) t[i] = (uint8_t)snap(root + i * 5);
        }
        if (ch.invert > 1) invert_up(t, n, ch.invert - 1);   /* an inversion asked (AUTO leaves these voicings as built) */
    }
    for (i = 1; i < n && ch.spread; i++) t[i] = (uint8_t)(t[i] + 12 * (ch.spread * i / (n > 1 ? n - 1 : 1)));   /* SPREAD */
    for (i = 0; i < n; i++) while (t[i] > 96) t[i] = (uint8_t)(t[i] - 12);   /* each note under C7, its own octave down */
    sort_notes(t, n);
    for (i = 1, k = 1; i < n; i++) if (t[i] != t[k - 1]) t[k++] = t[i];   /* folded onto another: once */
    n = k;
    while (t[0] < 28) for (i = 0; i < n; i++) t[i] = (uint8_t)(t[i] + 12);   /* nothing under E1 (the keys' lowest
                                                                                  bass, wide from F3: F1) */
    if (pedal && n < 7 && (36 + seq_key) % 12 != t[0] % 12) {   /* the tonic under it: the lowest, whatever the voicing */
        int pd = 36 + seq_key;
        while (pd >= t[0]) pd -= 12;
        if (pd < 24) { for (i = 0; i < n; i++) t[i] = (uint8_t)(t[i] + 12); pd += 12; }
        for (i = n; i > 0; i--) t[i] = t[i - 1];
        t[0] = (uint8_t)pd;
        n++;
    }
    for (i = 0; i < n; i++) out[i] = t[i];
    if (lead) { for (i = 0; i < n; i++) ch_prev[i] = t[i]; ch_pn = (uint8_t)n; }
    return n;
}
static int voice(int root, const int8_t *iv, int pedal, uint8_t *out) { return voice_x(root, iv, pedal, out, 1); }

/* the short names, for the progression's pills */
static const char *const QS[Q_COUNT] = {"m", "", "m7", "maj7", "7", ("m7\xE2\x99\xAD" "5"), "madd9", "sus2", "m9",
                                        "maj9", "6", "m11", "add9", "mM7", "m6", "7sus4", "9", "dim",
                                        "6/9", "m6/9", "sus4", "aug", "5", ("M7\xE2\x99\xAF" "11"), "13", "madd11",
                                        "add11", "dim7"};
/* a note's name in the key's spelling: the flat keys' flats (F, B-flat ... as the relative major), else sharps; one
 * outside the key named as a lowered or raised neighbour of its own */
static const char *pc_name(int pc)
{
    static const char *const SH[12] = {"C", "C\xE2\x99\xAF", "D", "D\xE2\x99\xAF", "E", "F", "F\xE2\x99\xAF", "G",
                                       "G\xE2\x99\xAF", "A", "A\xE2\x99\xAF", "B"};
    static const char *const FL[12] = {"C", "D\xE2\x99\xAD", "D", "E\xE2\x99\xAD", "E", "F", "G\xE2\x99\xAD", "G",
                                       "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
    static const uint8_t TO_MAJOR[MO_COUNT] = {3, 10, 8, 7, 5, 0};   /* the mode's tonic to its relative major's */
    int major = (seq_key + TO_MAJOR[seq_mode % MO_COUNT]) % 12, in = pc_scale() >> (pc % 12) & 1u;
    int flats = major == 5 || major == 10 || major == 3 || major == 8 || major == 1;
    if (!in) flats = pc_scale() >> ((pc + 1) % 12) & 1u;   /* outside the key: lowered, a flat; raised, a sharp */
    return (flats ? FL : SH)[pc % 12];
}
static void chord_name_q(char *o, int root, int q, const char *const *names);
static void chord_name(char *o, int root, int q) { chord_name_q(o, root, q, QN); }
static void chord_name_q(char *o, int root, int q, const char *const *names)
{
    str_cat(str_cat(o, pc_name(root)), names[q < Q_COUNT ? q : 0]);
}
static void chord_name_bass(char *o, int root, int q, int pedal)   /* and its bass, when the tonic is under it */
{
    chord_name(o, root, q);
    if (pedal && (int)(seq_key % 12) != root % 12) { while (*o) o++; str_cat(str_cat(o, "/"), pc_name(seq_key)); }
}

/* the chord some notes make, by its name (its bass after a slash when another tone is under it); 0: none. The more
 * common reading first (Am7 before C6) */
static int chord_of(const uint8_t *notes, int n, char *o)
{
    uint16_t m = 0;
    int i, q, r, lo = 127;
    if (n < 2) return 0;
    for (i = 0; i < n; i++) { m |= (uint16_t)(1u << (notes[i] % 12)); if (notes[i] < lo) lo = notes[i]; }
    for (q = 0; q < Q_COUNT; q++)
        for (r = 0; r < 12; r++) {
            uint16_t qm = 0;
            if (!(m >> r & 1u)) continue;
            for (i = 0; i < 7 && QI[q][i] >= 0; i++) qm |= (uint16_t)(1u << ((r + QI[q][i]) % 12));
            if (qm != m) continue;
            chord_name(o, r, q);
            if (lo % 12 != r) { while (*o) o++; str_cat(str_cat(o, "/"), pc_name(lo % 12)); }
            return 1;
        }
    return 0;
}

/* the mode's chord on a scale tone, of the keys' type (the CHORD page's Keys row), named by what it turns out to be:
 * its seventh, its triad, its ninth (the seventh where the mode's ninth has no name here), the triad and its ninth, or
 * a fixed shape on the scale tone (sus2, sus4, 6/9 by the mode's third, the fifth alone) */
static int diatonic(int n, int8_t *iv)
{
    int d = degree_of(snap(n)), r = note_of(d), q, k = 0;
    int a = note_of(d + 2) - r, b = note_of(d + 4) - r, c = note_of(d + 6) - r, e = note_of(d + 8) - r;
    int q3 = a == 3 ? (b == 6 ? Q_DIM : Q_M) : b == 8 ? Q_AUG : Q_MAJ;
    int q7 = a == 3 ? (b == 6 ? Q_M7B5 : c == 11 ? Q_MMAJ7 : Q_M7) : c == 11 ? Q_MAJ7 : Q_7;
    iv[k++] = 0;
    switch (ch.ktype) {
    case KT_TRIAD: iv[k++] = (int8_t)a; iv[k++] = (int8_t)b; q = q3; break;
    case KT_NINTH:
        q = a == 3 && b == 7 && c == 10 && e == 14 ? Q_M9 : a == 4 && b == 7 && c == 11 && e == 14 ? Q_MAJ9 : a == 4 && b == 7 && c == 10 && e == 14 ? Q_9 : -1;
        iv[k++] = (int8_t)a; iv[k++] = (int8_t)b; iv[k++] = (int8_t)c;
        if (q >= 0) iv[k++] = (int8_t)e; else q = q7;
        break;
    case KT_ADD9:
        q = e == 14 && b == 7 ? (a == 3 ? Q_MADD9 : Q_ADD9) : q3;
        iv[k++] = (int8_t)a; iv[k++] = (int8_t)b;
        if (q == Q_MADD9 || q == Q_ADD9) iv[k++] = 14;
        break;
    case KT_SUS2: iv[k++] = 2; iv[k++] = 7; q = Q_SUS2; break;
    case KT_SUS4: iv[k++] = 5; iv[k++] = 7; q = Q_SUS4; break;
    case KT_69: iv[k++] = (int8_t)(a == 3 ? 3 : 4); iv[k++] = 7; iv[k++] = 9; iv[k++] = 14; q = a == 3 ? Q_M69 : Q_69; break;
    case KT_POWER: iv[k++] = 7; q = Q_5; break;
    default: iv[k++] = (int8_t)a; iv[k++] = (int8_t)b; iv[k++] = (int8_t)c; q = q7; break;
    }
    iv[k] = -1;
    return q;
}

/* strike notes on a synth, strummed */
static void strike_src(int sy, const uint8_t *notes, int n, int src, int most_ms)
{
    int i, ms = ch.strum < 0 ? -ch.strum : ch.strum;
    if (most_ms > 0 && n > 1 && ms * (n - 1) > most_ms) ms = most_ms / (n - 1);   /* all in before the next pulse */
    for (i = 0; i < n; i++) {
        int k = ch.strum < 0 ? n - 1 - i : i;
        later_on(sy, notes[k], (int32_t)ms * i * (DMK_SR / 1000), src);
    }
}
static void strike(int sy, const uint8_t *notes, int n) { strike_src(sy, notes, n, SRC_KEY, 0); }   /* the keys' */

/* the sounding chord off on the synths that play chords */
static void chord_quiet(void)
{
    int s, i;
    for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) for (i = 0; i < ch_np; i++) later_off(s, ch_play[i], SRC_CHORD);
    ch_np = 0;
}
static void chord_off(int s)                            /* the sounding chord off on synth s (its FOLLOW changing) */
{
    int i;
    for (i = 0; i < ch_np; i++) later_off(s, ch_play[i], SRC_CHORD);
}
static void chord_rest(void)                            /* stopped, or a new progression: nothing shown as sounding */
{
    ch_shown = -1; ch_cmask = 0; ch_name[0] = 0;
}

static void chord_start(void) { ch_slot = 0; ch_left = 0; ch_arp = 0; ch_pn = 0; }
static void chord_init(void)                            /* the first song's: the pad plays the chords, the piano follows */
{
    int i;
    ch.follow[0] = FW_PLAYS; ch.follow[1] = FW_KEY; ch.follow[2] = FW_ROOT;
    ch.hint[0] = ch.hint[1] = ch.hint[2] = 2;           /* a harmony: a third above */
    ch_np = 0; ch_cmask = 0; ch_shown = -1; ch_name[0] = 0;
    for (i = 0; i < LATER_N; i++) later[i].used = 0;
    chord_start();
}

/* each step of the sequences' clock: a new slot when the last has had its length; the rhythm in between */
static void chord_step(void)
{
    const prog_t *pr = cur_prog();
    int s, i, newslot = !ch_left;
    if (!pr->n) { if (ch_np) chord_quiet(); ch_cmask = 0; return; }   /* an empty one: silence */
    if (newslot) {
        const cslot_t *c = &pr->s[ch_slot % pr->n];
        int root = 48 + (seq_key + c->root) % 12, len = c->half * 8;
        chord_quiet();
        ch_n = (uint8_t)voice(root, QI[c->q < Q_COUNT ? c->q : 0], c->pedal, ch_notes);
        ch_root = (uint16_t)root;
        ch_cmask = 0;
        for (i = 0; i < ch_n; i++) ch_cmask |= (uint16_t)(1u << (ch_notes[i] % 12));
        ch_q = (uint8_t)(c->q < Q_COUNT ? c->q : 0);
        ch_name[0] = 0;
        chord_name_bass(ch_name, root, ch_q, c->pedal);
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
            for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS && !(pat_mute >> s & 1u)) strike_src(s, ch_notes, ch_n, SRC_CHORD, 0);
        }
    } else if (ch.rhythm == RH_PULSE4 || ch.rhythm == RH_PULSE8) {
        int every = ch.rhythm == RH_PULSE4 ? 4 : 2;
        if (newslot || !(ch_left % every)) {
            chord_quiet();
            for (i = 0; i < ch_n; i++) ch_play[i] = ch_notes[i];
            ch_np = ch_n;
            for (s = 0; s < DMK_SYNTHS; s++)
                if (ch.follow[s] == FW_PLAYS && !(pat_mute >> s & 1u)) strike_src(s, ch_notes, ch_n, SRC_CHORD, every * 15000 / seq_bpm / 2);
        }
    } else {                                            /* the arpeggio: a note a step */
        int k = ch_arp % ch_n, note = ch.rhythm == RH_ARPUP ? ch_notes[k] : ch_notes[ch_n - 1 - k];
        chord_quiet();
        ch_play[0] = (uint8_t)note;
        ch_np = 1;
        for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS && !(pat_mute >> s & 1u)) note_on_src(s, note, SRC_CHORD);
        ch_arp++;
    }
    ch_left--;
}

/* the CHORD page's keys: a key, the mode's chord on it, voiced as the page says, on the current synth, its root an
 * octave under the key: a lower key a lower chord (the owner heard B under C go up, when every root was folded into
 * C3-B3). The middle octave (C4-B4) is the progression's own register, so a chord written from it plays back as it
 * sounded. Voiced as built, not led from the last key's. Stopped, the page shows it; playing, the progression keeps
 * the page, the followers and its voice leading */
static int chord_key(int n, int oct, uint8_t *notes)
{
    int8_t iv[6];
    int q = diatonic(n, iv), r = note_of(degree_of(snap(n))) - 12, c, i, pn = ch_pn;
    (void)oct;                                          /* (n has the octave in it) */
    ch_pn = 0;                                          /* as it is built: no inversion that moves least from the last */
    c = voice_x(r, iv, 0, notes, 0);
    ch_pn = (uint8_t)pn;
    if (!seq_on) {
        ch_name[0] = 0;
        chord_name(ch_name, r, q);
        ch_shown = -2;
        ch_root = (uint16_t)r; ch_q = (uint8_t)q;
        ch_cmask = 0;
        for (i = 0; i < c; i++) ch_cmask |= (uint16_t)(1u << (notes[i] % 12));
    }
    return c;
}

/* ---- writing your own (plan 4.4): a USER progression to write in, the preset copied into it the first time */
static prog_t *edit_prog(void)                          /* 0: the four are full */
{
    int i;
    if (ch.prog >= 16) return &uprog[ch.prog - 16];
    for (i = 0; i < 4 && uprog[i].n; i++) {}
    if (i == 4) return 0;
    {
        const char *nm = uprog[i].name;
        uprog[i] = PROG[ch.prog];
        uprog[i].name = nm;
    }
    ch.prog = (uint8_t)(16 + i);
    return &uprog[i];
}

/* a key's chord into the slot at the cursor (a new one at the end); its slot back */
static int slot_write(int n)                            /* -1: not written (the four full, or eight chords already) */
{
    prog_t *p = edit_prog();
    int8_t iv[6];
    int q = diatonic(n, iv), r = note_of(degree_of(snap(n))), at = ch_cur;
    if (!p) return -1;
    if (at > p->n) at = p->n;
    if (at >= PROG_SLOTS) return -1;
    if (at == p->n) p->n++;
    p->s[at].root = (uint8_t)(((r - seq_key) % 12 + 12) % 12);
    p->s[at].q = (uint8_t)q;
    p->s[at].half = ch_len;
    p->s[at].pedal = 0;
    p->mode = seq_mode;
    p->voicing = ch.voicing;
    return at;
}
static void slot_delete(int at)
{
    prog_t *p = edit_prog();
    int i;
    if (!p || at >= p->n) return;
    for (i = at; i < p->n - 1; i++) p->s[i] = p->s[i + 1];
    p->n--;
    if (ch_cur > p->n) ch_cur = p->n;
}
static int slot_copy(int at)                            /* a copy right after itself */
{
    prog_t *p = edit_prog();
    int i;
    if (!p || at >= p->n || p->n >= PROG_SLOTS) return at;
    for (i = p->n; i > at + 1; i--) p->s[i] = p->s[i - 1];
    p->s[at + 1] = p->s[at];
    p->n++;
    return at + 1;
}

/* ARP held + OCT+: the progression into a synth's roll as notes, voiced, to edit by hand; it then plays them */
static void print_to_roll(int s)
{
    const prog_t *pr = cur_prog();
    uint8_t notes[7];
    int i, k, at = 0, total = 0, pn = ch_pn;
#define CHG_LEN(h) (ch.change == 0 ? (h) * 4 : ch.change == 2 ? (h) * 16 : ch.change == 3 ? (h) * 32 : (h) * 8)
    for (i = 0; i < pr->n; i++) total += CHG_LEN(pr->s[i].half);
    if (!pr->n) return;
    if (ch.follow[s] == FW_PLAYS) chord_off(s);
    sq[s].count = 0;
    sq[s].steps = (uint8_t)(total > SEQ_STEPS ? SEQ_STEPS : total);
    ch_pn = 0;
    for (i = 0; i < pr->n && at < SEQ_STEPS; i++) {
        int c = voice(48 + (seq_key + pr->s[i].root) % 12, QI[pr->s[i].q < Q_COUNT ? pr->s[i].q : 0], pr->s[i].pedal, notes), len = CHG_LEN(pr->s[i].half);
        if (at + len > SEQ_STEPS) len = SEQ_STEPS - at;
        for (k = 0; k < c; k++) seq_add(s, at, len, notes[k], 100);
        at += CHG_LEN(pr->s[i].half);
    }
    ch_pn = (uint8_t)pn;
    ch.follow[s] = FW_FREE;
}
