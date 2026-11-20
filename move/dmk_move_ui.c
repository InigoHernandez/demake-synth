/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the controller. The Move's controls in, the core's edits out, and the page state for the screen
 * (dmk_move_view.h). Included by the glue after the core (one unit, as the core is), so a knob sets the core's page and
 * row and calls ui_encoder, and every special case (the roll, renames, lists) behaves as on the FM-1. The layout is the
 * owner's, move/PLAN.md and move/padmap.svg:
 *   pads 24..31    the pages: Home Roll Chord Sound Filter·Amp LFO FX Global; again, the page's next row
 *   pads 16..23    the chords in the key, I..vii and I again
 *   pads 0..15     the keyboard: a two-row piano from C4, or (Sample) in the key
 *   8 knobs        the page's eight, rows the FM-1 keeps apart side by side; Shift + knob 1 the tempo on every page
 *   16 steps       the roll of the synth in view on every page, each taking the pad last pressed, note or chord; Shift
 *                  + steps the 16 scenes; on Home's Song row the steps stay the scenes
 *   jog            the patterns' list, all three synths together as MONO2K's (a click loads one), the sound on Sound, the
 *                  projects on Save; Shift + jog the FM-1's ALGORITHM; a held step's pitch, Shift its velocity
 *   track buttons  the three synths; the fourth the Keys of the synth in view
 *   the rest       PLAN's table of controls. Modifiers go first: hold Shift, Mute, Copy or Delete, then press.
 * Everything here runs on the audio thread, between blocks (the queue is drained before each render). */

#define MV_TAP_MS 300                  /* a lit step tapped quicker loses its notes */
#define MV_FOCUS_MS 1200               /* a knob turned shows large so long */
#define MV_NOTE_MS 1500                /* a notice shows so long */
#define MV_UNDO_MS 1200                /* turns of one knob this close are one Undo */
#define MV_LIST_MS 3000                /* the patterns' list closes, left alone so long */

/* the Move's palette */
#define L_OFF     0
#define L_WHITE 120
#define L_KEY   123                    /* a white key, grey */
#define L_DARK  119                    /* a black key, near black; a beat */
#define L_BLUE   17                    /* the song's pages, a step with a note, a scene in use */
#define L_DBLUE  95
#define L_ROSE   26                    /* the sound's pages, the root, the scene next */
#define L_DROSE 115
#define L_PINK   52                    /* the mix's pages */
#define L_DPINK  35
#define L_VIOLET 109                   /* the chord row */
#define L_RED     1
#define W_OFF 0
#define W_DIM 16
#define W_MID 64
#define W_ON 124

/* a Move page: its name, the core's page the keys and the UI stay on, its rows; each row's eight knobs and Shift + jog
 * as the core's page, row and knob (4: its ALGORITHM) */
typedef struct { int8_t pg, row, k; } kref_t;
#define KR(p, r, c) {PG_##p, r, c}
#define KN {-1, 0, 0}
#define MP_ROWS 6                      /* a page's rows at most (the FM sound's: Main, the four operators, Chip) */
typedef struct {
    const char *name;
    uint8_t core, rows;
    const char *rname[MP_ROWS];
    kref_t k[MP_ROWS][8], jog[MP_ROWS];
} mpage_t;
enum { MP_HOME, MP_ROLL, MP_CHORD, MP_SOUND, MP_FILTAMP, MP_LFO, MP_FX, MP_GLOBAL, MP_SAVE, MP_N };
_Static_assert(DMK_SYNTHS == MV_SYNTHS, "the view (dmk_move_view.h) and the core count the synths alike");
static const mpage_t MP[MP_N] = {
    {"Home", PG_HOME, 3, {"Mix", "Pan", "Song"},
     {{KR(HOME, 0, 0), KR(HOME, 0, 1), KR(HOME, 0, 2), KR(HOME, 0, 3), KR(HOME, 1, 0), KR(HOME, 1, 1), KR(HOME, 1, 2), KR(HOME, 1, 3)},
      {KR(HOME, 2, 0), KR(HOME, 2, 1), KR(HOME, 2, 2), KR(HOME, 2, 3), KR(HOME, 3, 3), KR(GLOBAL, 1, 0), KN, KN},
      {KR(ARR, 0, 0), KR(ARR, 0, 1), KR(ARR, 0, 2), KR(ARR, 0, 3), KR(ARR, 1, 0), KR(ARR, 1, 1), KR(ARR, 1, 2), KR(ARR, 1, 3)}},
     {KN, KN, KN}},
    {"Roll", PG_ROLL, 2, {"Roll", "Arp", ""},
     {{KR(ROLL, 0, 0), KR(ROLL, 0, 1), KR(ROLL, 0, 2), KR(ROLL, 0, 3), KR(ROLL, 1, 0), KR(ROLL, 1, 1), KR(ROLL, 1, 2), KR(ROLL, 1, 3)},
      {KR(ROLL, 2, 0), KR(ROLL, 2, 1), KR(ROLL, 2, 2), KR(ROLL, 2, 3), KN, KN, KN, KN}},
     {KR(ROLL, 0, 4), KR(ROLL, 0, 4), KN}},
    {"Chord", PG_CHORD, 2, {"Voice", "Keys", ""},
     {{KR(CHORD, 0, 0), KR(CHORD, 0, 1), KR(CHORD, 0, 2), KR(CHORD, 0, 3), KR(CHORD, 1, 0), KR(CHORD, 1, 1), KR(CHORD, 1, 2), KR(CHORD, 1, 3)},
      {KR(CHORD, 2, 0), KR(CHORD, 2, 1), KN, KN, KN, KN, KN, KN}},
     {KR(CHORD, 0, 4), KR(CHORD, 0, 4), KN}},
    {"Sound", PG_SOUND, 2, {"Main", "Layer", ""},
     {{KR(SOUND, 0, 0), KR(SOUND, 0, 1), KR(SOUND, 0, 2), KR(SOUND, 0, 3), KR(SOUND, 0, 4), KR(SOUND, 3, 0), KR(SOUND, 3, 1), KR(SOUND, 2, 3)},
      {KR(SOUND, 1, 0), KR(SOUND, 1, 4), KR(SOUND, 1, 1), KR(SOUND, 1, 2), KR(SOUND, 1, 3), KR(SOUND, 2, 0), KR(SOUND, 2, 1), KR(SOUND, 2, 2)}},
     {KN, KN, KN}},
    {"Filter Amp", PG_FILTER, 1, {"", "", ""},
     {{KR(FILTER, 0, 0), KR(FILTER, 0, 1), KR(FILTER, 0, 2), KR(FILTER, 0, 3), KR(AMP, 0, 0), KR(AMP, 0, 1), KR(AMP, 0, 2), KR(AMP, 0, 3)}},
     {KR(FILTER, 0, 4), KN, KN}},
    {"LFO", PG_LFO, 1, {"", "", ""},
     {{KR(LFO, 0, 0), KR(LFO, 0, 1), KR(LFO, 0, 2), KR(LFO, 0, 3), KR(LFO, 0, 4), KR(LFO, 1, 0), KR(LFO, 1, 1), KN}},
     {KN, KN, KN}},
    {"FX", PG_FX, 3, {"Reverb", "Delay", "Motion"},
     {{KR(FX, 0, 4), KR(FX, 0, 0), KR(FX, 0, 1), KR(FX, 0, 2), KR(FX, 0, 3), KR(FX, 3, 0), KR(FX, 3, 2), KR(FX, 3, 3)},
      {KR(FX, 1, 4), KR(FX, 1, 0), KR(FX, 1, 1), KR(FX, 1, 2), KR(FX, 1, 3), KN, KN, KN},
      {KR(FX, 2, 4), KR(FX, 2, 0), KR(FX, 2, 1), KR(FX, 2, 2), KR(FX, 2, 3), KN, KN, KN}},
     {KN, KN, KN}},
    {"Global", PG_GLOBAL, 3, {"Space", "Colour", "Media"},
     {{KR(GLOBAL, 1, 4), KR(GLOBAL, 1, 0), KR(GLOBAL, 1, 1), KR(GLOBAL, 1, 2), KR(GLOBAL, 1, 3), KN, KN, KN},
      {KR(GLOBAL, 0, 4), KR(GLOBAL, 0, 0), KR(GLOBAL, 0, 1), KR(GLOBAL, 0, 2), KR(GLOBAL, 0, 3), KN, KN, KN},
      {KR(GLOBAL, 2, 4), KR(GLOBAL, 2, 0), KR(GLOBAL, 2, 1), KR(GLOBAL, 2, 2), KR(GLOBAL, 2, 3), KN, KN, KN}},
     {KN, KN, KN}},
    {"Save", PG_DISCS, 2, {"Projects", "Sounds", ""},
     {{KR(DISCS, 0, 0), KR(DISCS, 0, 1), KR(DISCS, 0, 2), KN, KN, KN, KN, KN},
      {KR(DISCS, 1, 0), KR(DISCS, 1, 1), KR(DISCS, 1, 2), KN, KN, KN, KN, KN}},
     {KR(DISCS, 0, 4), KR(DISCS, 0, 4), KN}},
};

/* the Sound page of a synth playing the YM2612 (move/dmk_ym.c; the owner's layout, 2026-11-18): Main its sound and the
 * macros over the patch, a row each operator, the Chip row. Its own knobs (PG_YM) turn the FM values (ymv) here; Sound,
 * Tune and Fine stay the core's */
#define PG_YM PG_COUNT                 /* (a page of the Move's alone: never the core's) */
#define YK(r, c) {PG_YM, r, c}
static const mpage_t MP_FM = {"Sound", PG_SOUND, 6, {"Main", "Op 1", "Op 2", "Op 3", "Op 4", "Chip"},
    {{KR(SOUND, 0, 0), YK(0, 1), YK(0, 2), YK(0, 3), YK(0, 4), YK(0, 5), YK(0, 6), YK(0, 7)},
     {YK(1, 0), YK(1, 1), YK(1, 2), YK(1, 3), YK(1, 4), YK(1, 5), YK(1, 6), YK(1, 7)},
     {YK(2, 0), YK(2, 1), YK(2, 2), YK(2, 3), YK(2, 4), YK(2, 5), YK(2, 6), YK(2, 7)},
     {YK(3, 0), YK(3, 1), YK(3, 2), YK(3, 3), YK(3, 4), YK(3, 5), YK(3, 6), YK(3, 7)},
     {YK(4, 0), YK(4, 1), YK(4, 2), YK(4, 3), YK(4, 4), YK(4, 5), YK(4, 6), YK(4, 7)},
     {YK(5, 0), YK(5, 1), KR(SOUND, 0, 3), KR(SOUND, 0, 4), YK(5, 4), YK(5, 5), YK(5, 6), KN}},
    {KN, KN, KN, KN, KN, KN}};

/* the pad in each column of the top row, and each page's colours lit and dim: the song blue, the sound rose, the mix pink */
static const uint8_t PAGE_LIT[MP_SAVE] = {L_BLUE, L_BLUE, L_BLUE, L_ROSE, L_ROSE, L_ROSE, L_PINK, L_PINK};
static const uint8_t PAGE_DIM[MP_SAVE] = {L_DBLUE, L_DBLUE, L_DBLUE, L_DROSE, L_DROSE, L_DROSE, L_DPINK, L_DPINK};

enum { PK_NONE, PK_KEY, PK_AUD };      /* a pad held: through the core as a key; only heard (a held step's) */
enum { CS_NONE, CS_STEP, CS_SCENE };   /* what Copy held picked first */

/* what a step takes: the pad last pressed on that synth, as it sounded. A chord has its notes; pad 0xFF none lit */
typedef struct { uint8_t n, note[7], vel, pad; } pen_t;

static struct {
    uint8_t page, prev, row[MP_N];     /* the page in view, the last before Save, each page's row */
    uint8_t shift, mute, copy, del, left, right;
    uint8_t used;                      /* the modifier held met another control */
    uint8_t in_key;                    /* the keyboard rows in the key (Sample) */
    int8_t touch, focus;               /* the knob touched (-1 none); the one shown large */
    uint32_t focus_t;
    uint8_t csrc;                      /* Copy's source (CS_*) */
    uint16_t cstep;                    /* its step, or scene */
    uint8_t bar[DMK_SYNTHS], follow;   /* the 16 steps' bar on each roll; it follows the playhead */
    pen_t pen[DMK_SYNTHS];
    uint8_t fset;                      /* a bit a synth: its Follow set by hand since the project opened */
    uint8_t scale_n;                   /* the scale's length, as the roll's views are in it */
    uint8_t plist, pat_sel;            /* the patterns' list (the jog's), its cursor */
    uint32_t plist_t;
    uint8_t pcur[DMK_SYNTHS];          /* each synth's pattern, as last seen */
    uint8_t sdown[16], sadded[16], sedit[16];   /* each step key: held, it placed the note, it was changed held */
    uint16_t sstep[16];
    uint32_t st0[16];
    uint8_t pkind[32], pnote[32];      /* each pad held: PK_*, an audition's note */
    uint8_t psyn[32];                  /*   its synth */
    uint8_t aud[8], naud, aud_s;       /* the notes a held step sounds, stopped; their synth */
    uint8_t base[sizeof ui.val];       /* the values as opened: Delete + a knob */
    uint8_t lit[16];                   /* notes sounding on the synth in view, a bit each */
    uint8_t style, spage, lprog;       /* chord mode: the style row 2 shows, which eight of it; the progression seen */
} mc;

static void mv_turn(kref_t r, int d);
static void mv_save_request(void);
static void ascii(char *o, const char *s, int n);
static void jog_sound(int dir);

static const mpage_t *mp_of(void) { return mc.page == MP_SOUND && syn[ui.synth].ym ? &MP_FM : &MP[mc.page]; }
static int mp_row(void) { return mc.row[mc.page] < mp_of()->rows ? mc.row[mc.page] : 0; }
static kref_t mp_knob(int i) { return mp_of()->k[mp_row()][i]; }
static int song_row(void) { return mc.page == MP_HOME && mp_row() == 2; }
static int steps_held(void) { int n = 0; for (int k = 0; k < 16; k++) n += mc.sdown[k]; return n; }
static int vsyn(void) { return ui.synth; }

/* ------------------------------------------------------------------------------------------------- Undo */

/* new on the Move, the FM-1 has none: the rolls open, every value and the mutes, before each edit. A snapshot is small
 * and the ring takes one per edit; a scene or a pattern switched or a project opened empties it (a snapshot's rolls
 * are that scene's). Notes sounding stop by their own count, so a roll put back while it plays cuts nothing */
#define UN_N 48
typedef struct {
    struct { snote_t n[SEQ_NOTES]; seq_n_t count; uint16_t steps; } r[DMK_SYNTHS];
    uint8_t val[sizeof ui.val], mute, follow[DMK_SYNTHS];
    uint8_t ymv[sizeof ymv];           /* the FM values (move/dmk_ym.c) */
} un_t;
static struct {
    un_t s[UN_N + 1];
    int head, n, at;                   /* the oldest; snapshots kept; where Undo is among them (n: at the tip) */
    uint32_t at_ms, what;              /* the last mark: when, of what */
    uint8_t gen[DMK_SYNTHS], scene;    /* the patterns and scene the ring is of */
} un;

static void un_take(un_t *u)
{
    for (int s = 0; s < DMK_SYNTHS; s++) {
        memcpy(u->r[s].n, sq[s].n, sq[s].count * sizeof u->r[s].n[0]);
        u->r[s].count = sq[s].count;
        u->r[s].steps = sq[s].steps;
        u->follow[s] = ch.follow[s];
    }
    memcpy(u->val, ui.val, sizeof u->val);
    memcpy(u->ymv, ymv, sizeof u->ymv);
    u->mute = pat_mute;
}
static void un_put(const un_t *u)
{
    for (int s = 0; s < DMK_SYNTHS; s++) {
        memcpy(sq[s].n, u->r[s].n, u->r[s].count * sizeof u->r[s].n[0]);
        sq[s].count = u->r[s].count;
        sq[s].steps = u->r[s].steps;
        if (sq[s].pos >= sq[s].steps) sq[s].pos = 0;
        psnd_follow(s, u->follow[s]);  /* (its chord let go or joined: audit I2) */
    }
    memcpy(ui.val, u->val, sizeof u->val);
    memcpy(ymv, u->ymv, sizeof u->ymv);
    pat_mute = u->mute;
    for (int s = 0; s < DMK_SYNTHS; s++) psnd_glide_stop(s);   /* (a switch's glide: the values put back win) */
    roll_cur_in();
    sp_sync();
}
static void un_reset(void)
{
    un.n = un.at = un.head = 0;
    un.what = 0;
    un.scene = scn_cur;
    for (int s = 0; s < DMK_SYNTHS; s++) un.gen[s] = spat_gen[s];
}
static void un_check(void)              /* each block */
{
    int same = un.scene == scn_cur;
    for (int s = 0; s < DMK_SYNTHS; s++) same &= un.gen[s] == spat_gen[s];
    if (!same) un_reset();
}
/* before an edit. what: 0 always a new step of Undo; else the same edit again within MV_UNDO_MS is part of the last */
static void undo_mark(uint32_t what)
{
    un_check();                        /* (a switch in this block: the ring is the new patterns', audit I4) */
    if (what && what == un.what && ui.now - un.at_ms < MV_UNDO_MS && un.at == un.n) { un.at_ms = ui.now; return; }
    un.what = what;
    un.at_ms = ui.now;
    un.n = un.at;                      /* (what could be redone goes) */
    if (un.n == UN_N) { un.head = (un.head + 1) % (UN_N + 1); un.n--; }
    un_take(&un.s[(un.head + un.n) % (UN_N + 1)]);
    un.n++;
    un.at = un.n;
}
static void undo_do(int redo)
{
    un_check();                        /* (a switch in this block: never the old patterns' rolls into the new, audit I4) */
    un.what = 0;
    if (redo) {
        if (un.at >= un.n) { notice("Nothing to redo"); return; }
        un.at++;
        un_put(&un.s[(un.head + un.at) % (UN_N + 1)]);
        notice("Redone");
        return;
    }
    if (!un.at) { notice("Nothing to undo"); return; }
    if (un.at == un.n) un_take(&un.s[(un.head + un.n) % (UN_N + 1)]);   /* (the tip: what Redo comes back to) */
    un.at--;
    un_put(&un.s[(un.head + un.at) % (UN_N + 1)]);
    notice("Undone");
}

/* ---------------------------------------------------------------------------------------------- the steps */

static int bar_of(int s) { return mc.bar[s]; }
static int bars_of(int s) { return (sq[s].steps + 15) / 16; }
static int step_has(int s, int st) { for (int i = 0; i < sq[s].count; i++) if (sq[s].n[i].step == st) return 1; return 0; }
static void step_clear(int s, int st) { for (int i = sq[s].count - 1; i >= 0; i--) if (sq[s].n[i].step == st) seq_del(s, i); }
/* written by hand: what was picked is what plays. One playing the chords would not sound it (roll_heard), one following
 * them would move it: it plays its roll from now, unless its Follow was set by hand (the Roll's knob 3) */
static void step_edit(int s)
{
    roll_heard(s);
    if (ch.follow[s] == FW_FREE || mc.fset >> s & 1u) return;
    ch.follow[s] = FW_FREE;
    char t[24];
    str_cat(fmt_int(str_cat(t, "Synth "), s + 1), " plays its roll");
    notice(t);
}

static void pen_take(int s, const uint8_t *notes, int n, int pad)
{
    pen_t *p = &mc.pen[s];
    if (n > 7) n = 7;
    p->n = (uint8_t)n;
    for (int i = 0; i < n; i++) p->note[i] = notes[i];
    p->vel = mv_vel;
    p->pad = (uint8_t)pad;
}
/* the pen's notes on a step, a chord's strum inside the step, so a tap takes all of it away. 0: the roll full */
static int step_pen(int s, int st)
{
    const pen_t *p = &mc.pen[s];
    for (int j = 0; j < p->n; j++) {
        const int i = seq_add(s, st, ui.roll_pen[s], p->note[j], p->vel);
        if (i < 0) { roll_full(); return 0; }
        const uint32_t d = strum_q8(s, j, p->n) >> 3;
        sq[s].n[i].off = (uint8_t)(d > 31u ? 31u : d);
    }
    return 1;
}
static int step_is_pen(int s, int st)   /* the step holds the pen's notes, and only those */
{
    const pen_t *p = &mc.pen[s];
    int n = 0;
    for (int i = 0; i < sq[s].count; i++) n += sq[s].n[i].step == st;
    if (n != p->n) return 0;
    for (int j = 0; j < p->n; j++) if (seq_find(s, st, p->note[j]) < 0) return 0;
    return 1;
}
static int pad_held(void) { for (int p = 0; p < 24; p++) if (mc.pkind[p] == PK_KEY) return 1; return 0; }

static void aud_off(void)
{
    for (int i = 0; i < mc.naud; i++) note_off_s(mc.aud_s, mc.aud[i]);
    mc.naud = 0;
}
static void aud_on(int s, int st)      /* stopped: a step held is heard */
{
    if (seq_on) return;
    if (mc.naud) aud_off();
    mc.aud_s = (uint8_t)s;
    for (int i = 0; i < sq[s].count && mc.naud < 8; i++)
        if (sq[s].n[i].step == st) { note_on_s(s, sq[s].n[i].note); mc.aud[mc.naud++] = sq[s].n[i].note; }
}

/* Copy's notes: the step picked first, into STEP */
static void step_paste(int s, int from, int to)
{
    if (from == to) return;
    undo_mark(0);
    step_clear(s, to);
    const int n = sq[s].count;
    for (int i = 0; i < n; i++)
        if (sq[s].n[i].step == from) {
            const snote_t t = sq[s].n[i];
            const int j = seq_add(s, to, t.len, t.note, t.vel);
            if (j >= 0) sq[s].n[j].off = t.off;
            else { roll_full(); break; }
        }
    step_edit(s);
}

static void mv_scene(int t)
{
    if (song_playing()) { song_mode = 0; mix_g = 32768; mix_dg = mix_next = 0; notice("Scene mode"); }
    scn_pick(t);
}

static void scene_key(int k)
{
    char m[28];
    if (mc.copy) {
        mc.used = 1;
        if (mc.csrc != CS_SCENE) { mc.csrc = CS_SCENE; mc.cstep = (uint16_t)k; fmt_int(str_cat(m, "Copy scene "), k + 1); notice(m); return; }
        if (k == mc.cstep) return;
        if (k == scn_cur) { notice("Another scene"); return; }   /* (the open one's rolls are loaded: kept, they would
                                                           land in the copy's patterns) */
        if (!scn[mc.cstep].used && mc.cstep != scn_cur) { notice("That scene is empty"); return; }
        if (scn[k].used && !(ui.copy_to == k && ui.now - ui.copy_at < 2000u)) {   /* over one in use: twice, as the FM-1 */
            ui.copy_to = (uint8_t)k; ui.copy_at = ui.now;
            notice("Again: over it");
            return;
        }
        scn_keep();
        scn[k] = scn[mc.cstep];
        fmt_int(str_cat(fmt_int(str_cat(m, "Scene "), mc.cstep + 1), " to "), k + 1);
        notice(m);
        return;
    }
    if (mc.del) {
        mc.used = 1;
        if (k == scn_cur) { notice("Open: pick another first"); return; }
        scn[k].used = 0;
        if (scn_next == k) scn_next = 0xFF;
        fmt_int(str_cat(m, "Deleted scene "), k + 1);
        notice(m);
        return;
    }
    if (song_row() && !mc.shift && !seq_on) {             /* the Song row, stopped: the FM-1's: a step of that scene written */
        const int pg = ui.page, r = ui.row[PG_ARR];
        ui.page = PG_ARR;
        ui.row[PG_ARR] = 1;
        dmk_key(WKEY[k], 1);
        dmk_key(WKEY[k], 0);
        ui.page = (uint8_t)pg;
        ui.row[PG_ARR] = (uint8_t)r;
        return;
    }
    mv_scene(k);
}

static void mv_step(int k, int down)
{
    if (k < 0 || k > 15) return;
    const int s = vsyn();
    if (!down) {
        if (!mc.sdown[k]) return;
        mc.sdown[k] = 0;
        if (!steps_held()) aud_off();
        if (!mc.sadded[k] && !mc.sedit[k] && ui.now - mc.st0[k] < MV_TAP_MS) {   /* a quick tap: its notes go */
            undo_mark(0);
            step_clear(s, mc.sstep[k]);
        }
        return;
    }
    if (mc.mute) { mc.used = 1; if (k < DMK_SYNTHS) { undo_mark(0); pat_mute_flip(k); } return; }
    if (mc.shift || song_row()) { if (mc.shift) mc.used = 1; scene_key(k); return; }
    const int st = bar_of(s) * 16 + k;
    if (st >= sq[s].steps) return;     /* (past the roll's end: nothing placed there unheard) */
    if (mc.del) { mc.used = 1; undo_mark(0); step_clear(s, st); return; }
    if (mc.copy) {
        mc.used = 1;
        if (mc.csrc != CS_STEP) { mc.csrc = CS_STEP; mc.cstep = (uint16_t)st; notice("Copy: now other steps"); return; }
        step_paste(s, mc.cstep, st);
        return;
    }
    mc.sdown[k] = 1;
    mc.sstep[k] = (uint16_t)st;
    mc.st0[k] = ui.now;
    mc.sedit[k] = 0;
    mc.sadded[k] = 0;
    const int picked = pad_held();     /* a pad held: its notes take the step; the same notes again come off */
    if (picked || !step_has(s, st)) {
        const int same = picked && step_is_pen(s, st);
        undo_mark(0);
        step_clear(s, st);
        if (same) { mc.sdown[k] = 0; return; }
        if (!step_pen(s, st)) { mc.sdown[k] = 0; return; }
        mc.sadded[k] = 1;
        step_edit(s);
        roll_show(s, mc.pen[s].note[0]);
    }
    if (!picked) aud_on(s, st);        /* (a pad held sounds them already) */
}

/* a held step's pad: that pitch on or off it, heard. The note a press placed goes at the first pad: the step takes the
 * pitches played on it */
static void held_note(int note)
{
    const int s = vsyn();
    for (int k = 0; k < 16; k++) {
        if (!mc.sdown[k]) continue;
        const int st = mc.sstep[k];
        if (mc.sadded[k] == 1) { step_clear(s, st); mc.sadded[k] = 2; }
        const int i = seq_find(s, st, note);
        if (i >= 0) seq_del(s, i);
        else if (seq_add(s, st, ui.roll_pen[s], note, KEY_VEL) < 0) roll_full();
        mc.sedit[k] = 1;
    }
    { const uint8_t n8 = (uint8_t)note; pen_take(s, &n8, 1, 0xFF); }
    step_edit(s);
}
static void held_pad(int p, int note)
{
    undo_mark(0);
    held_note(note);
    mc.pkind[p] = PK_AUD;
    mc.pnote[p] = (uint8_t)note;
    mc.psyn[p] = (uint8_t)vsyn();
    note_on_s(vsyn(), note);
}
static void held_chord(int note)        /* the chord row: every note of that chord on the held steps */
{
    uint8_t t[8];
    const int n = chord_key(note, ui.octave, t);
    undo_mark(0);
    for (int i = 0; i < n; i++) held_note(t[i]);
    pen_take(vsyn(), t, n, 0xFF);
}

/* ------------------------------------------------------------------- chord mode (the owner's Phase 5) */

/* the Chord page in view, or Keys on for the synth in view: row 2 picks the progression (Shift: its style), row 3 plays
 * it in order and then what can come next, row 4 each of those another way (dmk_chord.c's cm_*) */
static int chord_mode(void) { return mc.page == MP_CHORD || (ch.keys >> vsyn() & 1u); }
#define CM_ROW2 16                     /* row 2's first pad; row 3's is 8, row 4's 0 */
#define CM_MORE 7                      /* Shift + row 2's last pad: the style's next eight */
static void style_of_prog(void)        /* the style row 2 shows: the progression's, at its eight */
{
    uint8_t list[PROG_N];
    mc.style = (uint8_t)(ch.prog < PROG_PRE ? PROG_STYLE[ch.prog] : PS_USER);
    const int n = style_progs(mc.style, list);
    for (int i = 0; i < n; i++) if (list[i] == ch.prog) mc.spage = (uint8_t)(i / CM_PADS);
}
static void prog_pad(int i)            /* row 2 in chord mode: a progression; Shift: a style, or its next eight */
{
    uint8_t list[PROG_N];
    if (mc.shift) {
        mc.used = 1;
        if (i < PS_COUNT) { mc.style = (uint8_t)i; mc.spage = 0; notice(PSTYLES[i]); }
        else if (i == CM_MORE) {
            const int n = style_progs(mc.style, list);
            if (n > CM_PADS) mc.spage = (uint8_t)((mc.spage + 1) % ((n + CM_PADS - 1) / CM_PADS));
        }
        return;
    }
    const int n = style_progs(mc.style, list), at = mc.spage * CM_PADS + i;
    if (at >= n) return;
    undo_mark(0);
    prog_to(list[at]);
    notice(PROGS[list[at]]);
}
static void held_cm(int code)          /* steps held, a chord-mode pad: its chord on them */
{
    uint8_t t[8];
    const int n = cm_chord(code, ui.octave, t);
    undo_mark(0);
    for (int i = 0; i < n; i++) held_note(t[i]);
    pen_take(vsyn(), t, n, 0xFF);
}

/* a held step's knobs: Move, Pitch, Length, Nudge, on each of its notes (the FM-1's held note, a chord's whole). The
 * velocity on Shift + jog */
static void held_turn(int k, int d)
{
    const int s = vsyn(), n = sq[s].steps, dir = d > 0 ? 1 : -1;
    undo_mark(0x100u + (uint32_t)k);
    for (int h = 0; h < 16; h++) {
        if (!mc.sdown[h]) continue;
        const int st = mc.sstep[h];
        mc.sedit[h] = 1;
        if (k == 0) {
            const int t = (st + dir + n) % n;
            if (step_has(s, t)) continue;
            for (int i = 0; i < sq[s].count; i++) if (sq[s].n[i].step == st) sq[s].n[i].step = (uint8_t)t;
            mc.sstep[h] = (uint16_t)t;
            continue;
        }
        for (int i = 0; i < sq[s].count; i++) {
            snote_t *q = &sq[s].n[i];
            if (q->step != st) continue;
            if (k == 1) {
                const int p = dir < 0 && snap(q->note) != q->note ? snap(q->note) : note_of(degree_of(q->note) + dir);
                if (p < 24 || p > 108 || seq_find(s, st, p) >= 0) continue;
                for (int a = 0; a < mc.naud; a++) if (mc.aud[a] == q->note) { note_off_s(mc.aud_s, q->note); note_on_s(mc.aud_s, p); mc.aud[a] = (uint8_t)p; }
                q->note = (uint8_t)p;
                { const uint8_t n8 = (uint8_t)p; pen_take(s, &n8, 1, 0xFF); }
            }
            if (k == 2) {
                const int l = clampi(q->len + d, 1, SEQ_LEN);
                q->len = (uint8_t)l;
                ui.roll_pen[s] = (uint8_t)l;
            }
            if (k == 3) q->off = (uint8_t)clampi(q->off + d, 0, 31);
            if (k == 4) q->vel = (uint8_t)clampi(q->vel + d * 2, 1, 127);
        }
    }
}

/* -------------------------------------------------------------------------------------------- the capture */

/* what was just played on the keyboard, REC or not, playing or not, the latest CAP_N notes. A run is a stretch with no
 * gap over two bars. Played to the roll: each note lands where it was played; stopped: from the bar in view, the first
 * note on its first step, the roll grown to whole bars if it must. One Undo */
#define CAP_N 128
typedef struct { uint64_t at, end; uint32_t q8; uint8_t note, vel, syn, roll; } cap_t;
static struct { cap_t n[CAP_N]; uint32_t w; } cap;

static void cap_on(int p, int note, int vel)
{
    cap_t *c = &cap.n[cap.w % CAP_N];
    c->at = mv.samples;
    c->end = 0;
    c->note = (uint8_t)note;
    c->vel = (uint8_t)vel;
    c->syn = (uint8_t)vsyn();
    c->roll = seq_on;
    c->q8 = seq_on ? seq_rec_q8(vsyn()) : 0;
    (void)p;
    cap.w++;
}
static void cap_off(int note, int s)
{
    for (uint32_t i = 1; i <= CAP_N && i <= cap.w; i++) {
        cap_t *c = &cap.n[(cap.w - i) % CAP_N];
        if (!c->end && c->note == note && c->syn == s) { c->end = mv.samples; return; }
    }
}
static void capture(void)
{
    if (mc.page == MP_CHORD) { undo_mark(0); print_to_roll(vsyn()); notice("Printed to the roll"); return; }
    const int s = vsyn();
    const uint32_t per = seq_per();
    const uint64_t gap = (uint64_t)per * 32u >> 8;
    uint32_t first = 0, last = 0;
    int have = 0;
    for (uint32_t i = 1; i <= CAP_N && i <= cap.w; i++) {
        const uint32_t at = (cap.w - i) % CAP_N;
        const cap_t *c = &cap.n[at];
        if (c->syn != s) continue;
        if (have && (cap.n[first].at - c->at > gap || c->roll != cap.n[last].roll)) break;
        if (!have) { last = at; have = 1; }
        first = at;
    }
    if (!have) { notice("Nothing played yet"); return; }
    undo_mark(0);
    const int rolled = cap.n[last].roll, base = bar_of(s) * 16;
    const uint64_t t0 = cap.n[first].at;
    uint32_t top = 0;
    int n = 0;
    for (uint32_t i = first; ; i = (i + 1) % CAP_N) {
        const cap_t *c = &cap.n[i];
        if (c->syn == s) {
            const uint64_t end = c->end ? c->end : mv.samples;
            const uint32_t held = (uint32_t)((end - c->at) * 256u / (per >> 8));
            const uint32_t a = rolled ? c->q8 : (uint32_t)((c->at - t0) * 256u / (per >> 8)) + ((uint32_t)base << 8);
            if (!rolled && (a >> 8) + 1u > top) top = (a >> 8) + 1u;
            if (!rolled && top > sq[s].steps) {     /* the roll grown to whole bars */
                const uint32_t b = (top + 15u) / 16u * 16u;
                if (b <= SEQ_STEPS) sq[s].steps = (uint16_t)b;
            }
            if (roll_strum(s, a, a + held, c->note, c->vel, 0, 1, 0) < 0) { roll_full(); break; }
            n++;
        }
        if (i == last) break;
    }
    step_edit(s);
    char m[24];
    str_cat(fmt_int(str_cat(m, "Captured "), n), n == 1 ? " note" : " notes");
    notice(m);
}

/* ------------------------------------------------------------------------------------------------ the pads */

/* pad p's note on the keyboard rows (without the octave), -1 none */
static const int8_t PIANO[16] = {0, 2, 4, 5, 7, 9, 11, 12, 1, 3, -1, 6, 8, 10, -1, -1};
static int key_root(void) { const int r = 48 + seq_key; return r < 53 ? r + 12 : r; }
static int pad_note(int p)
{
    if (!mc.in_key) return PIANO[p] < 0 ? -1 : 60 + PIANO[p];
    return note_of(degree_of(snap(key_root())) + (p < 8 ? p : p - 1));
}

static void mv_page(int i)
{
    if (i == mc.page) { mc.row[i] = (uint8_t)((mp_row() + 1) % mp_of()->rows); }
    else mc.page = (uint8_t)i;
    mc.plist = 0;
    ui.page = MP[i].core;
    ui.full = 0;
    mc.focus = -1;
    mc.focus_t = 0;
}

static void mv_pad(int p, int vel, int down)
{
    if (p < 0 || p >= 32) return;
    if (p >= 24) { if (down) mv_page(p - 24); return; }
    const int s = vsyn();
    if (!down) {
        const int kind = mc.pkind[p];
        mc.pkind[p] = PK_NONE;
        if (kind == PK_KEY) {
            const int sy = ui.key_sy[p], note = ui.key_note[p];
            if (p >= 16 || mv_kcm[p] || ui.key_nch[p] > 0) { for (int j = 0; j < ui.key_nch[p]; j++) cap_off(ui.key_ch[p][j], sy); }
            else cap_off(note, sy);
            dmk_key(p, 0);
        }
        if (kind == PK_AUD) note_off_s(mc.psyn[p], mc.pnote[p]);
        return;
    }
    if (mc.pkind[p]) return;           /* (a note on twice) */
    int note;
    uint8_t cm = 0;                    /* chord mode's rows 3 and 4: the pad's code */
    if (chord_mode() && p >= CM_ROW2) { prog_pad(p - CM_ROW2); return; }
    if (chord_mode()) {
        cpad_t pads[CM_PADS];
        const int n = cm_pads(pads), col = p % CM_PADS;
        if (col >= n) return;
        cm = (uint8_t)(1 + col + (p < CM_PADS ? CM_VAR : 0));
        note = 48 + (seq_key + pads[col].root) % 12;   /* (its root: what the key is known by) */
    } else if (p >= 16) note = note_of(degree_of(snap(key_root())) + (p - 16));
    else note = pad_note(p);
    if (note < 0) return;
    mv_vel = (uint8_t)clampi(vel, 1, 127);
    if (steps_held()) {
        if (cm) held_cm(cm);
        else if (p < 16) held_pad(p, note + ui.octave * 12); else held_chord(note + ui.octave * 12);
        return;
    }
    mv_kvel[p] = mv_vel;
    mv_knote[p] = (uint8_t)note;
    mv_kchord[p] = p >= 16 || cm;
    mv_kcm[p] = cm;
    const int ready = ui.rec_ready && !seq_on && seq_rec;   /* this pad ends the wait: dmk_key starts it, recording */
    if (ready) ck_take_start();
    if (ready || (seq_on && seq_rec)) undo_mark(0x200u);
    const int stopped_rec = !seq_on && seq_rec && !ready;
    if (stopped_rec && ui.page == PG_ROLL) undo_mark(0);
    /* the chord row, stopped, REC lit: the FM-1 writes a progression on its Chord page alone */
    const uint8_t rec = seq_rec;
    if ((p >= 16 || cm) && stopped_rec && ui.page != PG_CHORD && ui.page != PG_ROLL) seq_rec = 0;
    dmk_key(p, 1);
    seq_rec = rec;
    mc.pkind[p] = PK_KEY;
    if (ui.key_nch[p] > 0) {
        for (int j = 0; j < ui.key_nch[p]; j++) cap_on(p, ui.key_ch[p][j], mv_vel);
        pen_take(s, ui.key_ch[p], ui.key_nch[p], p);
    } else { cap_on(p, ui.key_note[p], mv_vel); pen_take(s, &ui.key_note[p], 1, p); }
}

/* ----------------------------------------------------------------------------------------------- the knobs */

/* the FM page's own knobs (PG_YM): each a place in a list of n, kept in the sound's FM values as the chip takes them.
 * Turned right is more: louder, longer, slower to rise (the chip's rates are the other way round) */
enum { YK_PLAIN, YK_INV, YK_AR, YK_DT, YK_SIGNED, YK_ALG, YK_MUL, YK_NAMES, YK_OFF };
typedef struct { const char *name; uint8_t at, n, how; const char *const *names; } ymk_t;   /* at: YV_*, or an
                                                           operator's YO_* on its row */
static const char *const YK_CHIP[2] = {"Clean", "Grit"}, *const YK_RATE[2] = {"Smooth", "Raw"};
static const uint8_t YK_DT_CODE[7] = {7, 6, 5, 0, 1, 2, 3};   /* the detune -3..+3 as the chip's codes */
static const ymk_t YMK_MAIN[8] = {
    {0}, {"Algorithm", YV_ALG, 8, YK_ALG, 0}, {"Feedback", YV_FB, 8, YK_PLAIN, 0},
    {"Bright", YV_BRIGHT, 2 * YM_BRIGHT_MID, YK_SIGNED, 0}, {"Ratio", YV_RATIO, 2 * YM_RATIO_MID + 1, YK_SIGNED, 0},
    {"Detune", YV_DETUNE, YM_DETUNE_MAX + 1, YK_PLAIN, 0}, {"Decay", YV_DECAY, 2 * YM_DECAY_MID, YK_SIGNED, 0},
    {"Release", YV_RELEASE, 2 * YM_REL_MID, YK_SIGNED, 0}};
static const ymk_t YMK_OP[8] = {
    {"Level", YO_TL, YM_TL_MAX + 1, YK_INV, 0}, {"Ratio", YO_MUL, YM_MUL_MAX + 1, YK_MUL, 0},
    {"Detune", YO_DT, 7, YK_DT, 0}, {"Attack", YO_AR, YM_RATE_MAX, YK_AR, 0},
    {"Decay", YO_D1R, YM_RATE_MAX + 1, YK_INV, 0}, {"Sustain", YO_SL, 16, YK_INV, 0},
    {"Decay 2", YO_D2R, YM_RATE_MAX + 1, YK_INV, 0}, {"Release", YO_RR, YM_RR_MAX + 1, YK_INV, 0}};
static const ymk_t YMK_CHIP[8] = {
    {"Chip", YV_GRIT, 2, YK_NAMES, YK_CHIP}, {"Rate", YV_RAW, 2, YK_NAMES, YK_RATE}, {0}, {0},
    {"LFO", YV_LFO, YM_LFO_RATES + 1, YK_OFF, 0}, {"Vibrato", YV_VIB, YM_VIB_MAX + 1, YK_PLAIN, 0},
    {"Tremolo", YV_TREM, YM_TREM_MAX + 1, YK_PLAIN, 0}, {0}};
enum { YR_MAIN, YR_OP1, YR_OP4 = YR_OP1 + 3, YR_CHIP };   /* the FM page's rows */

static const ymk_t *ymk_of(kref_t r)
{
    const ymk_t *d = r.row == YR_MAIN ? &YMK_MAIN[r.k] : r.row == YR_CHIP ? &YMK_CHIP[r.k] : &YMK_OP[r.k];
    return d->name ? d : 0;
}
static int ymk_at(kref_t r, const ymk_t *d) { return r.row >= YR_OP1 && r.row <= YR_OP4 ? YV_OP + (r.row - YR_OP1) * YO_N + d->at : d->at; }
static int ymk_u(const ymk_t *d, int raw)               /* the chip's value as the knob's place */
{
    switch (d->how) {
    case YK_INV: return d->n - 1 - raw;
    case YK_AR: return YM_RATE_MAX - (raw ? raw : 1);
    case YK_DT: for (int i = 0; i < 7; i++) if (YK_DT_CODE[i] == raw) return i; return 3;   /* (4, "down by 0": none) */
    default: return raw;
    }
}
static int ymk_raw(const ymk_t *d, int u)               /* and back */
{
    u = clampi(u, 0, d->n - 1);
    switch (d->how) {
    case YK_INV: return d->n - 1 - u;
    case YK_AR: return YM_RATE_MAX - u;
    case YK_DT: return YK_DT_CODE[u];
    default: return u;
    }
}
static void ymk_text(char *o, const ymk_t *d, int u)
{
    switch (d->how) {
    case YK_ALG: fmt_int(o, u + 1); break;
    case YK_SIGNED: { const int x = u - d->n / 2; if (x > 0) *o++ = '+'; fmt_int(o, x); break; }
    case YK_DT: { const int x = u - 3; if (x > 0) *o++ = '+'; fmt_int(o, x); break; }
    case YK_MUL: if (u) fmt_int(o, u); else str_cat(o, "0.5"); break;
    case YK_NAMES: str_cat(o, d->names[u]); break;
    case YK_OFF: if (u) fmt_int(o, u); else str_cat(o, "Off"); break;
    default: fmt_int(o, u); break;
    }
}
static void ym_turn(kref_t r, int d)
{
    const ymk_t *k = ymk_of(r);
    if (!k) return;
    uint8_t *v = &ymv[vsyn()][ymk_at(r, k)];
    *v = (uint8_t)ymk_raw(k, ymk_u(k, *v) + d);
    sp_sync();
}
static void ym_knob_reset(kref_t r)                     /* Delete + an FM knob: the patch's own value, a macro at rest */
{
    const ymk_t *k = ymk_of(r);
    const int s = vsyn();
    uint8_t t[YV_N];
    if (!k || !ymv[s][YV_FROM]) return;
    ym_vals_of(t, ymv[s][YV_FROM] - 1);
    ymv[s][ymk_at(r, k)] = t[ymk_at(r, k)];
    sp_sync();
}
static void ym_knob_view(mv_knob_t *o, kref_t r)
{
    const ymk_t *k = ymk_of(r);
    char v[24];
    if (!k) { o->on = 0; return; }
    const int u = ymk_u(k, ymv[vsyn()][ymk_at(r, k)]);
    ascii(o->name, k->name, sizeof o->name);
    v[0] = 0;
    ymk_text(v, k, u);
    ascii(o->val, v, sizeof o->val);
    o->n = (uint8_t)(k->n <= 6 ? k->n : 0);             /* (a short list as dots) */
    o->idx = (uint8_t)u;
    o->raw = (uint8_t)(k->n > 1 ? u * 127 / (k->n - 1) : 0);
}

/* one list step at a time: the core's lists and the roll's move a step a call */
static int turn_steps(kref_t r)
{
    return r.pg == PG_ARR || r.pg == PG_DISCS || (r.pg == PG_ROLL && r.row == 0) || choice_of(r.pg, r.row, r.k)
        || (r.pg == PG_SOUND && ((r.row == 0 && r.k == 0) || (r.row == 1 && r.k == 4))) || (r.pg == PG_CHORD && r.k == 4);
}
static void mv_turn(kref_t r, int d)
{
    if (r.pg < 0 || !d) return;
    if (r.pg == PG_YM) { ym_turn(r, d); return; }
    const int pg = ui.page, row = ui.row[r.pg], enc = r.k == 4 ? E_ALGO : E_K1 + r.k;
    ui.page = (uint8_t)r.pg;
    ui.row[r.pg] = (uint8_t)r.row;
    if (turn_steps(r)) { const int dir = d > 0 ? 1 : -1; for (int n = d > 0 ? d : -d; n > 0 && n <= 16; n--) ui_encoder(enc, dir); }
    else ui_encoder(enc, d);
    ui.page = (uint8_t)pg;
    ui.row[r.pg] = (uint8_t)row;
}
static int is_tempo(int i)
{
    const kref_t r = mp_knob(i);
    (void)r;
    return i == 0 && mc.shift;                          /* (Home's knob 4 is synth 4's level) */
}
static void focus_on(int i) { mc.focus = (int8_t)i; mc.focus_t = ui.now | 1u; }

static void mv_knob(int i, int d)
{
    if (i < 0 || i > 7 || !d) return;
    if (steps_held()) { if (i < 4) { held_turn(i, d); focus_on(i); } return; }
    const kref_t r = mp_knob(i);
    if (r.pg < 0) return;
    undo_mark(0x10u + (uint32_t)(r.pg * 64 + r.row * 8 + r.k));
    mv_turn(r, d);
    if (r.pg == PG_ROLL && r.row == 0 && r.k == 2) mc.fset |= (uint8_t)(1u << vsyn());
    focus_on(i);
}

static void mv_tempo_knob(int i, int d10, int coarse)
{
    if (i < 0 || i > 7 || !d10) return;
    if (!is_tempo(i)) { mv_knob(i, d10 > 0 ? 1 : -1); return; }   /* (ui.js a frame behind a page change) */
    focus_on(i);
    if (i == 0 && mc.shift) mc.used = 1;
    if (mc.del) {
        mc.used = 1;
        if (ck.sync == MV_SYNC_MOVE && tq.state != MV_TQ_NONE) { tq_cancel(); notice("Tempo: Move's again"); }
        return;
    }
    if (ck.sync == MV_SYNC_MOVE) { tq_ask(d10, coarse); return; }
    int whole;
    if (coarse) { tq.acc10 = 0; whole = d10 / 10; }
    else { tq.acc10 += d10; whole = tq.acc10 / 10; tq.acc10 -= whole * 10; }
    ui.val[0][PG_HOME][0][4] = (uint8_t)clampi(ui.val[0][PG_HOME][0][4] + whole, 20, 255);
    sp_sync();
}

static void mv_touch(int i, int on)
{
    if (i < 0 || i > 7) return;
    if (!on) { if (mc.touch == i) mc.touch = -1; return; }
    mc.touch = (int8_t)i;
    mc.plist = 0;
    if (is_tempo(i) || steps_held()) { focus_on(i); return; }
    const kref_t r = mp_knob(i);
    if (r.pg < 0) return;
    focus_on(i);
    if (mc.del && r.pg == PG_YM) { mc.used = 1; undo_mark(0); ym_knob_reset(r); return; }
    if (mc.del && r.pg != PG_ARR && r.pg != PG_DISCS && !(r.pg == PG_ROLL && r.row == 0)) {   /* the value back to its start */
        uint8_t *v = vp(r.pg, r.row, r.k);
        const size_t at = (size_t)(v - &ui.val[0][0][0][0]);
        mc.used = 1;
        if (at < sizeof mc.base) { undo_mark(0); *v = mc.base[at]; sp_sync(); }
    }
}

/* ------------------------------------------------------------------------------------- the jog and the buttons */

static void plist_open(void)
{
    const int s = vsyn();
    mc.plist = 1;
    mc.pat_sel = (uint8_t)(spat_next[s] < SYN_PATS ? spat_next[s] : syn_pat(s));
    mc.plist_t = ui.now | 1u;
    mc.focus = -1;
    mc.focus_t = 0;
}
/* the patterns' list, a click, all three synths: stopped, the one under the cursor now; playing, at the bar, the list open
 * on it, and a second click makes it now, each in its place in the bar */
static void plist_click(void)
{
    const int p = mc.pat_sel;
    int same = 1, waiting = 1;
    for (int s = 0; s < DMK_SYNTHS; s++) {
        same &= syn_pat(s) == p;
        waiting &= syn_pat(s) == p || spat_next[s] == p;
    }
    if (!same && seq_on && !waiting) {
        for (int s = 0; s < DMK_SYNTHS; s++) pat_pick_syn(s, p);
        mc.plist_t = ui.now | 1u;
        return;
    }
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const int pos = sq[s].pos;
        spat_next[s] = 0xFF;
        if (syn_pat(s) == p) continue;
        syn_pat_set(s, p);
        if (seq_on) sq[s].pos = (uint8_t)(pos % sq[s].steps);
    }
    mc.plist = 0;
}

/* a synth's pattern switched, by the list, a scene or at the bar. A pattern plays as it was left, its sound and how it
 * follows with it (the core's psnd: an empty one never played follows nothing, so it is silent); a new one is a bar long */
static void pat_changed(int s)
{
    const int now = syn_pat(s);
    mc.pcur[s] = (uint8_t)now;
    if (!sq[s].count && !spat[s][now].used) { sq[s].steps = 16; sq[s].pos %= 16; }
}

/* the jog: a held step's pitch; the projects on Save; the sound on Sound; else, as MONO2K's, the patterns' list: a turn
 * opens it at the pattern playing and moves the cursor, nothing loads until a click */
static void mv_jog(int d)
{
    if (!d) return;
    if (steps_held()) {
        if (mc.shift) { mc.used = 1; held_turn(4, d); } else held_turn(1, d);
        return;
    }
    if (mc.plist && !mc.shift) {
        mc.pat_sel = (uint8_t)clampi(mc.pat_sel + d, 0, SYN_PATS - 1);
        mc.plist_t = ui.now | 1u;
        return;
    }
    if (mc.page == MP_SAVE) {
        if (mc.shift) { mc.used = 1; ren_flush(); ui.row[PG_DISCS] = (uint8_t)!ui.row[PG_DISCS]; mc.row[MP_SAVE] = ui.row[PG_DISCS]; return; }
        const kref_t r = {PG_DISCS, ui.row[PG_DISCS], ui.row[PG_DISCS] ? 1 : 0};
        mv_turn(r, d);
        return;
    }
    if (mc.shift) {
        mc.used = 1;
        const kref_t r = mp_of()->jog[mp_row()];
        if (r.pg >= 0) { undo_mark(0x8000u); mv_turn(r, d); }
        return;
    }
    if (mc.page != MP_SOUND) {
        plist_open();
        mc.pat_sel = (uint8_t)clampi(mc.pat_sel + d, 0, SYN_PATS - 1);
        return;
    }
    /* the synth's sound, loading at once: the recordings, the YM2612's patches (when no other synth plays the chip),
     * then the library's (PRESETS on Home's rule, with the patches between) */
    undo_mark(0x8001u);
    for (int n = d > 0 ? d : -d; n > 0 && n <= 8; n--) jog_sound(d > 0 ? 1 : -1);
}

/* the Sound page's jog, a step: where the synth's sound is in the list (recordings, patches, library), then the next */
static void jog_sound(int dir)
{
    const int s = vsyn(), o = ym_owner(), fm = o < 0 || o == s ? YM_PATCHES : 0, lib = lib_count();
    const int tot = DMK_SAMPLES_N + fm + lib;
    uint8_t *sv = &ui.val[s][PG_SOUND][0][0], *pr = &ui.preset[s];
    int at = *pr >= DMK_SAMPLES_N ? DMK_SAMPLES_N + fm + (*pr - DMK_SAMPLES_N) : *sv < DMK_SAMPLES_N + fm ? *sv : DMK_SAMPLES_N - 1;
    at = wrap(at + dir, tot);
    if (at < DMK_SAMPLES_N + fm) {
        *sv = (uint8_t)at;
        *pr = (uint8_t)(at < DMK_SAMPLES_N ? at : 0);    /* (an FM sound: no library mark) */
        notice(at < DMK_SAMPLES_N ? DMK_SAMPLES[at].name : YM_PATCH[at - DMK_SAMPLES_N].name);
    } else {
        char t[16];
        const int nth = at - DMK_SAMPLES_N - fm, slot = lib_nth(nth);
        lib_load(s, slot);
        *pr = (uint8_t)(DMK_SAMPLES_N + nth);
        fmt_int(str_cat(t, slot < 9 ? "User 0" : "User "), slot + 1);
        notice(t);
    }
    sp_sync();
}

static void mv_opened(void)             /* a project opened or new: what Undo and Delete + a knob go back to */
{
    un_reset();
    memcpy(mc.base, ui.val, sizeof mc.base);
    mc.scale_n = SCALE_N;
    mc.fset = 0;
    mc.plist = 0;
    for (int s = 0; s < DMK_SYNTHS; s++) mc.pcur[s] = (uint8_t)syn_pat(s);
    style_of_prog();
    for (int s = 0; s < DMK_SYNTHS; s++) {             /* (an FM sound's number is past the recordings: never a library mark) */
        mc.bar[s] = 0;
        if (ui.preset[s] >= DMK_SAMPLES_N) ui.preset[s] = ui.val[s][PG_SOUND][0][0] < DMK_SAMPLES_N ? ui.val[s][PG_SOUND][0][0] : 0;
    }
}

static void jog_click(void)
{
    if (mc.plist) { plist_click(); return; }
    if (mc.page != MP_SAVE) { plist_open(); return; }
    if (ui.row[PG_DISCS]) return;
    if (seq_on) { notice("Stop to open"); return; }
    dmk_button(B_PLAY, 1);             /* Projects, stopped: the FM-1's PLAY twice opens the one picked */
    dmk_button(B_PLAY, 0);
    if (ui.page == PG_HOME) {          /* (opened; a new, empty one leaves Move's tempo alone) */
        mc.page = MP_HOME;
        mv_opened();
        if (song_used(ui.song)) tq_song();
    }
}

static void mv_save_project(void)       /* the project open into its slot (Shift + Rec, and closing) */
{
    if (!store_on()) { notice("No store here"); return; }
    const int at = ui.song;
    ren_flush();
    if (song_newer(at)) { notice("Made by a newer Demake"); return; }
    uint8_t *nv = ui.val[0][PG_DISCS][1];
    const uint8_t k0 = nv[0], k1 = nv[1], k2 = nv[2];
    if (ck.sync == MV_SYNC_MOVE) ui.val[0][PG_HOME][0][4] = (uint8_t)clampi(seq_bpm, 20, 255);   /* (the tempo it plays at) */
    if (!song_used(at)) nm_roll(nv, ++ui.nm_n * 2654435761u ^ ui.now ^ (uint32_t)at * 977u);
    const int r = song_save(at);
    if (r) { nv[0] = k0; nv[1] = k1; nv[2] = k2; notice(r == -2 ? "Too many notes to save" : "Not saved"); return; }
    char m[32];
    str_cat(str_cat(m, "Saved: "), song_name(at));
    notice(m);
    mv_save_request();
}
static void mv_save(void)               /* Shift + Rec: the project open, or the sound on Save's Sounds row */
{
    if (mc.page == MP_SAVE && ui.row[PG_DISCS]) { dmk_button(B_SAVE, 1); dmk_button(B_SAVE, 0); return; }
    mv_save_project();
}

static void tap_core(int b, int pg, int row)   /* a modifier tapped: the FM-1's button on PG's row */
{
    const int was = ui.page, r = ui.row[pg];
    ui.page = (uint8_t)pg;
    ui.row[pg] = (uint8_t)row;
    dmk_button(b, 1);
    dmk_button(b, 0);
    ui.page = (uint8_t)was;
    ui.row[pg] = (uint8_t)r;
}

static void octave(int d)
{
    char t[16];
    ui.octave = (int8_t)clampi(ui.octave + d, -2, 2);
    fmt_int(str_cat(t, "Octave "), ui.octave);
    notice(t);
}

static void bars(int d)
{
    const int s = vsyn(), n = bars_of(s);
    mc.follow = 0;
    mc.bar[s] = (uint8_t)clampi(mc.bar[s] + d, 0, n - 1);
}

static void mv_track(int i)
{
    if (mc.shift && !mc.mute && !mc.copy && !mc.del) {   /* Shift + a track: that synth's Keys, on or off */
        uint8_t *kv = &ui.val[i][PG_CHORD][2][0];
        const int on = !choice_index(choice_of(PG_CHORD, 2, 0), *kv);
        char m[24];
        mc.used = 1;
        undo_mark(0);
        *kv = choice_val(on, 2);
        sp_sync();
        fmt_int(str_cat(m, on ? "Keys on: synth " : "Keys off: synth "), i + 1);
        notice(m);
        return;
    }
    if (mc.mute) { mc.used = 1; undo_mark(0); pat_mute_flip(i); return; }
    if (mc.del) {
        const int was = ui.synth;
        mc.used = 1;
        undo_mark(0);
        ui.synth = (uint8_t)i;
        notice(roll_clear(i) ? "Roll cleared, chords off" : "Roll cleared");
        ui.synth = (uint8_t)was;
        return;
    }
    if (mc.copy) {                     /* that synth's pattern into a new one */
        char m[28];
        const int from = syn_pat(i), q = spat_dup(i);
        mc.used = 1;
        if (q < 0) { notice("Eight patterns: all in use"); return; }
        fmt_int(str_cat(fmt_int(str_cat(m, "Pattern "), from + 1), " to "), q + 1);
        notice(m);
        return;
    }
    ui.synth = (uint8_t)i;
    mc.plist = 0;
    sp_sync();
}

static void mv_button(int cc, int v)
{
    const int down = v > 0;
    switch (cc) {
    case 49: mc.shift = (uint8_t)down; if (down) mc.used = 0; return;
    case 88:                           /* Mute: held a layer, a tap the synth in view */
        if (down) { mc.mute = 1; mc.used = 0; return; }
        if (mc.mute && !mc.used) { undo_mark(0); pat_mute_flip(vsyn()); }
        mc.mute = 0;
        return;
    case 60:                           /* Copy */
        if (down) { mc.copy = 1; mc.used = 0; mc.csrc = CS_NONE; return; }
        if (mc.copy && !mc.used) {
            if (song_row()) tap_core(B_OCTU, PG_ARR, 1);   /* the Song row: the step at its cursor, copied after */
            else notice("Copy: hold it, then a step");
        }
        mc.copy = 0;
        mc.csrc = CS_NONE;
        return;
    case 119:                          /* Delete */
        if (down) { mc.del = 1; mc.used = 0; return; }
        if (mc.del && !mc.used) {
            if (song_row()) {                               /* the Song row: the step at its cursor, the last from New */
                if (ui.arr_cur >= song_n && song_n) ui.arr_cur = (uint8_t)(song_n - 1);
                tap_core(B_OCTD, PG_ARR, 1);
            }
            else if (mc.page == MP_SAVE) tap_core(B_OCTD, PG_DISCS, ui.row[PG_DISCS]);
            else notice("Delete: hold it, then a step");
        }
        mc.del = 0;
        return;
    case 62: case 63:                  /* Left, Right: the bars; both, the playhead; Shift the synth's pattern */
        if (cc == 62) mc.left = (uint8_t)down; else mc.right = (uint8_t)down;
        if (!down) return;
        if (mc.left && mc.right) { mc.follow = 1; notice("The steps follow"); return; }
        if (mc.shift) {
            char m[16];
            const int p = wrap((spat_next[vsyn()] < SYN_PATS ? spat_next[vsyn()] : syn_pat(vsyn())) + (cc == 63 ? 1 : -1), SYN_PATS);
            mc.used = 1;
            pat_pick_syn(vsyn(), p);
            fmt_int(str_cat(m, "Pattern "), p + 1);
            notice(m);
            return;
        }
        bars(cc == 63 ? 1 : -1);
        return;
    default: break;
    }
    if (!down) return;
    switch (cc) {
    case 3: jog_click(); break;
    case 85: mv_play_press(); break;   /* Play: Move's too (passed through) */
    case 43: case 42: case 41: case 40: mv_track(43 - cc); break;
    case 50:                           /* Menu: the list closed; Save; again, back */
        if (mc.plist) { mc.plist = 0; break; }
        if (mc.page == MP_SAVE) { ren_flush(); mc.page = mc.prev; ui.page = MP[mc.page].core; }
        else { mc.prev = mc.page; mc.page = MP_SAVE; save_open(); ui.row[PG_DISCS] = 0; mc.row[MP_SAVE] = 0; }
        mc.focus = -1;
        break;
    case 86:                           /* Rec; Shift + Rec saves */
        if (mc.shift) { mc.used = 1; mv_save(); break; }
        dmk_button(B_REC, 1);
        dmk_button(B_REC, 0);
        break;
    case 56:                           /* Undo; Shift + Undo redoes */
        if (mc.shift) mc.used = 1;
        undo_do(mc.shift);
        break;
    case 118:                          /* Sample: the keyboard, piano <-> in key */
        mc.in_key ^= 1;
        notice(mc.in_key ? "Keys: in key" : "Keys: piano");
        break;
    case 52: capture(); break;         /* Capture (Shift + it: the host's) */
    case 55: octave(1); break;
    case 54: octave(-1); break;
    case 58: {                         /* Loop: the arp's hold */
        const kref_t r = {PG_ROLL, 2, 4};
        mv_turn(r, 1);
        notice(choice_index(choice_of(PG_ROLL, 2, 4), *vp(PG_ROLL, 2, 4)) ? "Arp hold on" : "Arp hold off");
        break;
    }
    default: break;
    }
}

/* each block: what the core's frame keeps, which the Move never calls; the steps' bar; the knob shown large */
/* a scale of another length: the roll's view is in degrees, so the same notes stay in it */
static void scale_view(void)
{
    if (mc.scale_n == SCALE_N) return;
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const int v = ui.roll_view[s] * SCALE_N / mc.scale_n;
        ui.roll_view[s] = (uint8_t)(v < VIEW_LO ? VIEW_LO : v > VIEW_HI ? VIEW_HI : v);
    }
    mc.scale_n = SCALE_N;
}

static void mv_tick(void)
{
    scale_view();
    roll_cur_in();
    if (ui.ren_slot < ST_SONGS && ui.now - ui.ren_at > 900u && !seq_on) ren_flush();
    un_check();
    if (mc.follow && seq_on) {
        const int s = vsyn();
        mc.bar[s] = (uint8_t)(seq_playing_step(s) / 16);
    }
    for (int s = 0; s < DMK_SYNTHS; s++) if (mc.bar[s] >= bars_of(s)) mc.bar[s] = 0;
    if (mc.focus_t && mc.touch < 0 && ui.now - (mc.focus_t & ~1u) > MV_FOCUS_MS) { mc.focus = -1; mc.focus_t = 0; }
    if (mc.touch >= 0) mc.focus = mc.touch;
    if (mc.plist && ui.now - (mc.plist_t & ~1u) > MV_LIST_MS) mc.plist = 0;
    for (int s = 0; s < DMK_SYNTHS; s++) if (syn_pat(s) != mc.pcur[s]) pat_changed(s);
    if (ch.prog != mc.lprog) { mc.lprog = ch.prog; style_of_prog(); }   /* (a knob or a scene changed it) */
}

static void mv_ctl_init(void)
{
    memset(&mc, 0, sizeof mc);
    mc.touch = mc.focus = -1;
    mc.follow = 1;
    for (int s = 0; s < DMK_SYNTHS; s++) { const uint8_t c4 = 60; pen_take(s, &c4, 1, 0xFF); }
    memset(&cap, 0, sizeof cap);
    memset(ui.key_nch, 0, sizeof ui.key_nch);   /* (no key is down: a chord's count outlives the instance that held it) */
    ui.page = PG_HOME;
    mv_opened();
}

/* ------------------------------------------------------------------------------------------- the page state */

/* s as the screen's type has it: its few marks spelled, the FM-1's button names the Move's */
static void ascii(char *o, const char *s, int n)
{
    static const struct { const char *from, *to; } SAY[] = {
        {"OCT\xE2\x88\x92 again", "Delete again"}, {"PLAY again", "Click again"}, {"SAVE again", "Shift Rec again"},
        {"REC: the keys write", "Rec: the keys write"}, {"Ready: a key starts", "Ready"},
        {"ALGO: projects, sounds", "Shift + jog: Sounds"},
    };
    for (unsigned i = 0; i < sizeof SAY / sizeof SAY[0]; i++) {
        const char *a = SAY[i].from, *b = s;
        while (*a && *a == *b) { a++; b++; }
        if (!*a) { const char *t = SAY[i].to; while (*t && n > 1) { *o++ = *t++; n--; } s = b; break; }
    }
    while (*s && n > 1) {
        const uint8_t c = (uint8_t)*s;
        if (c < 128) { *o++ = *s++; n--; continue; }
        const uint8_t c1 = (uint8_t)s[1], c2 = c >= 0xE0 && c1 ? (uint8_t)s[2] : 0;
        char r = 0;
        if (c == 0xC3 && c1 == 0x97) r = 'x';
        if (c == 0xE2 && c1 == 0x88 && c2 == 0x92) r = '-';
        if (c == 0xE2 && c1 == 0x80 && c2 == 0x94) r = '-';
        if (c == 0xE2 && c1 == 0x99 && c2 == 0xAF) r = '#';
        if (c == 0xE2 && c1 == 0x99 && c2 == 0xAD) r = 'b';
        if (c == 0xE2 && c1 == 0x86 && c2 == 0x92) r = '>';
        if (c == 0xC2 && c1 == 0xBD) { if (n > 4) { *o++ = '1'; *o++ = '/'; n -= 2; } r = '2'; }
        if (r) { *o++ = r; n--; }
        s += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    }
    *o = 0;
}

/* a knob's name: the FM-1's field, an effect's own (its fields_band) */
static const char *knob_label(kref_t r)
{
    const int pg = r.pg, row = r.row, k = r.k;
    if (k == 4) return PAGE[pg].algo[row];
    if (pg == PG_CHORD && row == 1 && k == 1) return "Scale";
    if (pg == PG_HOME && row == 3) return "Width";
    if (pg == PG_ARR && row == 0 && k < DMK_SYNTHS) {   /* the Song row: a synth's pattern a knob (the chords: Chord's) */
        static const char *const SK[DMK_SYNTHS] = {"Synth 1", "Synth 2", "Synth 3", "Synth 4"};
        return SK[k];
    }
    if (pg == PG_HOME && k < DMK_SYNTHS) {              /* four synths: the FM-1's labels end at three, then the song's */
        static const char *const HK[3][DMK_SYNTHS] = {{"Level 1", "Level 2", "Level 3", "Level 4"},
                                                       {"Space 1", "Space 2", "Space 3", "Space 4"},
                                                       {"Pan 1", "Pan 2", "Pan 3", "Pan 4"}};
        return HK[row % 3][k];
    }
    const char *l = PAGE[pg].k[row][k];
    if (pg == PG_FX && row == 2) l = FXK[2][choice_index(choice_of(PG_FX, 2, 4), *vp(PG_FX, 2, 4))][k];
    if (pg == PG_FX && row == 0 && k == 3) l = RVK4[choice_index(choice_of(PG_FX, 0, 4), *vp(PG_FX, 0, 4))];
    if (pg == PG_GLOBAL && row == 0) l = FXK[0][choice_index(choice_of(PG_GLOBAL, 0, 4), *vp(PG_GLOBAL, 0, 4))][k];
    if (pg == PG_GLOBAL && row == 2) l = FXK[1][choice_index(choice_of(PG_GLOBAL, 2, 4), *vp(PG_GLOBAL, 2, 4))][k];
    if (pg == PG_FX && row == 1 && k && dl_type(ui.synth) == DT_GRAINS) { static const char *const GK[3] = {"Size", "Density", "Texture"}; l = GK[k - 1]; }
    if (pg == PG_GLOBAL && row == 1 && k >= 2) {
        const int t = choice_index(choice_of(PG_GLOBAL, 1, 4), *vp(PG_GLOBAL, 1, 4));
        if (k == 2 && t == SP_REVERSE) l = "Length";
        if (k == 2 && t == SP_FREEZE) l = "Hold";
        if (k == 3 && t == SP_SHIMMER) l = "Shine";
    }
    return l;
}

/* the knobs that hold a recording's number (0 to DMK_SAMPLES_N - 1), not a value from 0 to 127: Sound, and the layer's
 * Sound (the core's VS(s, PG_SOUND, 0, 0) and VS(s, PG_SOUND, 1, 4)) */
static int holds_rec(kref_t r) { return r.pg == PG_SOUND && ((r.row == 0 && r.k == 0) || (r.row == 1 && r.k == 4)); }

/* a meter's place, 0 at its first and 127 at its last: a list's by its place in the list (a choice keeps the middle of
 * its place, short of either end), a recording's by its number, anything else its value (the owner, 2026-11-18: Sound's
 * meter went only to the middle) */
static uint8_t meter_raw(kref_t r, const choice_t *c, int idx)
{
    const int x = *vp(r.pg, r.row, r.k);
    if (c) return (uint8_t)(c->n > 1 ? idx * 127 / (c->n - 1) : 0);
    if (holds_rec(r)) {                                 /* (Sound's list goes on into the YM2612's patches) */
        const int top = r.row == 0 ? DMK_SAMPLES_N + YM_PATCHES - 1 : DMK_SAMPLES_N - 1;
        return (uint8_t)(clampi(x, 0, top) * 127 / top);
    }
    return (uint8_t)clampi(x, 0, 127);
}

static void knob_view(mv_knob_t *o, kref_t r, int i)
{
    char v[24];
    const int s = vsyn();
    v[0] = 0;
    if (r.pg < 0) { o->on = 0; return; }
    o->on = 1;
    if (r.pg == PG_YM) { ym_knob_view(o, r); return; }
    const choice_t *c = r.pg == PG_ARR || r.pg == PG_DISCS ? 0 : choice_of(r.pg, r.row, r.k);
    o->n = c ? c->n : 0;
    o->idx = c ? (uint8_t)choice_index(c, *vp(r.pg, r.row, r.k)) : 0;
    ascii(o->name, knob_label(r), sizeof o->name);
    if (r.pg == PG_ROLL && r.row == 0 && r.k < 4) {
        static const char *const FW_SHORT[FW_COUNT] = {"Free", "Key", "Chord", "Root", "Plays", "With 1", "With 2", "With 3", "With 4"};
        const int st = sq[s].steps > 1 ? sq[s].steps - 1 : 1, f = ch.follow[s];
        if (r.k == 0) { fmt_int(v, ui.roll_cur[s] + 1); o->raw = (uint8_t)(ui.roll_cur[s] * 127 / st); }
        if (r.k == 1) { note_name(v, note_of(ui.roll_view[s])); o->raw = (uint8_t)((ui.roll_view[s] - VIEW_LO) * 127 / (VIEW_HI - VIEW_LO)); }
        if (r.k == 2) { str_cat(v, FW_SHORT[f]); o->raw = (uint8_t)(f * 127 / (FW_COUNT - 1)); }
        if (r.k == 3) { fmt_int(v, sq[s].steps); o->raw = (uint8_t)(sq[s].steps * 127 / SEQ_STEPS); }
    } else if (r.pg == PG_ARR) {
        arr_text(v, r.row, r.k);
        o->raw = (uint8_t)clampi(arr_meter(r.row, r.k), 0, 127);
    } else if (r.pg == PG_DISCS) {
        int at = ui.song_pick;
        if (!r.row) {
            const uint8_t *nv = save_nmv(at);
            const int named = song_used(at) && nv[2] == NM_MARK;
            if (r.k == 0) { fmt_int(v, at + 1); o->raw = (uint8_t)(at * 127 / (ST_SONGS - 1)); }
            else { str_cat(v, !named ? "-" : nm_word(r.k == 2, nv[r.k - 1])); o->raw = named ? (uint8_t)(nv[r.k - 1] * 127 / 23) : 0; }
        } else {
            const int rec = VS(s, PG_SOUND, 0, 0), user = ui.preset[s] >= DMK_SAMPLES_N ? lib_nth(ui.preset[s] - DMK_SAMPLES_N) : -1;
            if (r.k == 0) { fmt_int(v, s + 1); o->raw = (uint8_t)(s * 127 / (DMK_SYNTHS - 1)); }
            if (r.k == 1) { fmt_int(v, rec + 1); o->raw = (uint8_t)(rec * 127 / (DMK_SAMPLES_N - 1)); }
            if (r.k == 2) { if (user >= 0) fmt_int(v, user + 1); else str_cat(v, "-"); o->raw = user >= 0 ? (uint8_t)(user * 127 / (ST_USER_N - 1)) : 0; }
        }
    } else if (is_tempo(i)) {
        const uint32_t b = mv_bpm10();
        char *e = fmt_int(v, (int)(b / 10));
        if (b % 10) { *e++ = '.'; fmt_int(e, (int)(b % 10)); }
        o->raw = (uint8_t)clampi((int)b / 25, 0, 127);
    } else {
        val_text(v, r.pg, r.row, r.k);
        o->raw = meter_raw(r, c, o->idx);
    }
    ascii(o->val, v, sizeof o->val);
}

/* the kind of a knob, for ui.js's curve */
static int knob_kind(kref_t r, int i)
{
    if (r.pg < 0) return MV_KK_NONE;
    if (is_tempo(i)) return MV_KK_TEMPO;
    if (r.pg == PG_YM) { const ymk_t *k = ymk_of(r); return !k ? MV_KK_NONE : k->n <= 32 ? MV_KK_CHOICE : MV_KK_RANGE; }
    if (r.pg == PG_ARR || r.pg == PG_DISCS || (r.pg == PG_ROLL && r.row == 0 && r.k >= 2)) return MV_KK_CHOICE;
    const choice_t *c = choice_of(r.pg, r.row, r.k);
    return c && c->n <= 32 ? MV_KK_CHOICE : MV_KK_RANGE;
}

static void held_view(mv_view_t *v)
{
    static const char *const HELD[4] = {"Move", "Pitch", "Length", "Nudge"};
    const int s = vsyn();
    int h = 0, j = -1;
    while (h < 15 && !mc.sdown[h]) h++;
    const int st = mc.sstep[h];
    for (int i = 0; i < sq[s].count; i++) if (sq[s].n[i].step == st) j = i;
    char t[sizeof v->sound], nn[8];                     /* the step and as many of its notes as fit, whole */
    char *e = fmt_int(str_cat(t, "Step "), st + 1);
    int shown = 0;
    for (int i = 0; i < sq[s].count && shown < 3; i++)
        if (sq[s].n[i].step == st) {
            nn[0] = 0;
            note_name(nn, sq[s].n[i].note);
            if ((size_t)(e - t) + 1 + strlen(nn) >= sizeof t) break;
            *e++ = ' ';
            e = str_cat(e, nn);
            shown++;
        }
    ascii(v->sound, t, sizeof v->sound);
    for (int k = 0; k < 8; k++) {
        mv_knob_t *o = &v->knob[k];
        memset(o, 0, sizeof *o);
        v->kkind[k] = k < 4 ? (k == 1 ? MV_KK_CHOICE : MV_KK_RANGE) : MV_KK_NONE;
        if (k >= 4) continue;
        o->on = 1;
        str_cat(o->name, HELD[k]);
        if (j < 0) { str_cat(o->val, "-"); continue; }
        const snote_t *n = &sq[s].n[j];
        char t[16];
        t[0] = 0;
        if (k == 0) { fmt_int(t, st + 1); o->raw = (uint8_t)(st * 127 / (sq[s].steps > 1 ? sq[s].steps - 1 : 1)); }
        if (k == 1) { note_name(t, n->note); o->raw = n->note; }
        if (k == 2) { fmt_int(t, n->len); o->raw = (uint8_t)clampi(n->len * 8, 0, 127); }
        if (k == 3) { fmt_int(n->off ? str_cat(t, "+") : t, n->off * 15000 / seq_bpm / 32); o->raw = (uint8_t)(n->off * 127 / 31); }
        ascii(o->val, t, sizeof o->val);
    }
}

static void mv_leds(mv_view_t *v)
{
    mv_leds_t *l = &v->led;
    const int s = vsyn(), bar = bar_of(s), scenes = mc.shift || song_row();
    uint8_t lit[16];
    memset(lit, 0, sizeof lit);
    for (int i = 0; i < DMK_VOICES; i++)
        if (vc[i].on && vc[i].sy == s) lit[vc[i].note >> 3 & 15] |= (uint8_t)(1u << (vc[i].note & 7));
    uint16_t has = 0;
    for (int i = 0; i < sq[s].count; i++) if (sq[s].n[i].step >> 4 == bar) has |= (uint16_t)(1u << (sq[s].n[i].step & 15));
    for (int k = 0; k < 16; k++) {
        uint8_t c;
        if (mc.mute) c = k < DMK_SYNTHS ? (pat_mute >> k & 1u ? L_DARK : L_WHITE) : L_OFF;
        else if (scenes) c = k == scn_cur ? L_WHITE : k == scn_next ? L_ROSE : scn[k].used ? L_BLUE : L_OFF;
        else {
            const int st = bar * 16 + k;
            c = st >= sq[s].steps ? L_OFF : k == v->step ? L_WHITE : has >> k & 1u ? L_BLUE : k % 4 ? L_OFF : L_DARK;
            if (mc.sdown[k]) c = L_ROSE;
        }
        l->step[k] = c;
    }
    for (int p = 0; p < 16; p++) {
        const int n = pad_note(p);
        if (n < 0) { l->pad[p] = L_OFF; continue; }
        const int sounding = n + ui.octave * 12;
        const int on = sounding >= 0 && sounding < 128 && lit[sounding >> 3] >> (sounding & 7) & 1u;
        l->pad[p] = mc.pkind[p] || on ? L_WHITE : p == mc.pen[s].pad ? L_BLUE : (n % 12 == seq_key) ? L_ROSE : p < 8 || mc.in_key ? L_KEY : L_DARK;
    }
    for (int p = 16; p < 24; p++) l->pad[p] = mc.pkind[p] ? L_WHITE : p == mc.pen[s].pad ? L_BLUE : L_VIOLET;
    if (chord_mode()) {                /* rows 3 and 4 by function (tonic blue, subdominant rose, dominant pink, borrowed
                                          violet; row 4 dimmer), the chord sounding white; row 2 the style's eight, the
                                          one playing white (Shift: the styles, the one shown white; the next eight blue) */
        static const uint8_t FN[4] = {L_BLUE, L_ROSE, L_PINK, L_VIOLET}, FN_DIM[4] = {L_DBLUE, L_DROSE, L_DPINK, L_VIOLET};
        cpad_t pads[CM_PADS];
        uint8_t list[PROG_N];
        const int n = cm_pads(pads), np = style_progs(mc.style, list);
        for (int p = 0; p < 16; p++) {
            const int col = p % CM_PADS, f = col < n ? cm_function(pads[col].root) : 0;
            const int playing = p >= CM_PADS && seq_on && ch_shown == col && !pads[col].next;
            l->pad[p] = col >= n ? L_OFF : mc.pkind[p] || playing ? L_WHITE : p >= CM_PADS ? FN[f] : FN_DIM[f];
        }
        for (int i = 0; i < CM_PADS; i++) {
            const int at = mc.spage * CM_PADS + i;
            uint8_t c = at < np ? (list[at] == ch.prog ? L_WHITE : L_VIOLET) : L_OFF;
            if (mc.shift) c = i < PS_COUNT ? (i == mc.style ? L_WHITE : L_VIOLET) : i == CM_MORE && np > CM_PADS ? L_BLUE : L_OFF;
            l->pad[CM_ROW2 + i] = c;
        }
    }
    for (int i = 0; i < MP_SAVE; i++) l->pad[24 + i] = i == mc.page ? PAGE_LIT[i] : PAGE_DIM[i];
    for (int t = 0; t < DMK_SYNTHS; t++)
        l->trk[t] = mc.shift ? (ch.keys >> t & 1u ? L_WHITE : L_OFF) : t == s ? L_WHITE : pat_mute >> t & 1u ? L_OFF : L_DARK;
    l->btn[MB_REC] = seq_rec && !(v->ready && ui.now / 250u & 1u) ? L_RED : L_OFF;
    l->btn[MB_MUTE] = pat_mute >> s & 1u ? W_ON : W_DIM;
    l->btn[MB_UNDO] = un.at ? W_MID : W_DIM;
    l->btn[MB_COPY] = mc.copy ? W_ON : W_DIM;
    l->btn[MB_DEL] = mc.del ? W_ON : W_DIM;
    l->btn[MB_LEFT] = bar > 0 ? W_MID : W_DIM;
    l->btn[MB_RIGHT] = bar + 1 < bars_of(s) ? W_MID : W_DIM;
    l->btn[MB_UP] = ui.octave > 0 ? W_ON : W_DIM;
    l->btn[MB_DOWN] = ui.octave < 0 ? W_ON : W_DIM;
    l->btn[MB_MENU] = mc.page == MP_SAVE || mc.plist ? W_ON : W_DIM;
    l->btn[MB_LOOP] = choice_index(choice_of(PG_ROLL, 2, 4), *vp(PG_ROLL, 2, 4)) ? W_ON : W_DIM;
    l->btn[MB_SAMPLE] = mc.in_key ? L_ROSE : L_DARK;
    l->btn[MB_CAPTURE] = cap.w ? W_MID : W_DIM;
}

/* the roll as drawn: the bar the steps show (zoomed: the 32 or 64 steps from it), seven scale rows, each note on its
 * cells: the take's micro timing is heard, not drawn */
static void roll_view(mv_view_t *v)
{
    mv_roll_t *w = &v->roll;
    const int s = vsyn(), z = 16 << ui.roll_zoom[s], x0 = bar_of(s) * 16 / z * z, view = ui.roll_view[s];
    const int at = seq_on ? seq_playing_step(s) : ui.roll_cur[s];
    const seq_t *q = &sq[s];
    w->cw8 = (uint8_t)(128 * 8 / z);
    for (int i = 0; i < q->count && w->count < MV_NOTES; i++) {
        const snote_t *n = &q->n[i];
        const int a = n->step - x0;
        if (a < 0 || a >= z) continue;
        const int row = 6 - (degree_of(n->note) - view);
        if (row < 0 || row > 6) continue;
        int len = n->len;
        if (a + len > z) len = z - a;
        mv_note_t *o = &w->n[w->count++];
        const int xa = a * w->cw8 / 8, xb = (a + len) * w->cw8 / 8;
        o->x = (uint8_t)xa;
        o->w = (uint8_t)(xb - xa - 1 < 2 ? 2 : xb - xa - 1);
        o->row = (int8_t)row;
        o->lit = (uint8_t)(seq_on && at >= n->step && at < n->step + n->len);
    }
    w->head = (int16_t)(at >= x0 && at < x0 + z ? (at - x0) * w->cw8 / 8 : -1);
    w->end = (int16_t)(q->steps - x0 < z ? (q->steps - x0) * w->cw8 / 8 : -1);
}

/* a synth's sound: its recording's name, or its place in the library */
static void sound_name(char *o, int t, int n)
{
    if (syn[t].ym) { ascii(o, YM_PATCH[syn[t].ym - 1].name, n); return; }
    if (ui.preset[t] >= DMK_SAMPLES_N) {
        const int slot = lib_nth(ui.preset[t] - DMK_SAMPLES_N);
        o[0] = 0;
        fmt_int(str_cat(o, slot < 9 ? "User 0" : "User "), slot + 1);
    } else ascii(o, DMK_SAMPLES[VS(t, PG_SOUND, 0, 0) < DMK_SAMPLES_N ? VS(t, PG_SOUND, 0, 0) : 0].name, n);
}

static void pic_arp(mv_pic_t *p)
{
    const int s = vsyn();
    char t[16];
    p->kind = PIC_ARP;
    p->type = (uint8_t)choice_index(choice_of(PG_ROLL, 2, 0), *vp(PG_ROLL, 2, 0));
    p->p[0] = (uint8_t)(1 + choice_index(choice_of(PG_ROLL, 2, 2), *vp(PG_ROLL, 2, 2)));
    p->p[1] = (uint8_t)clampi(*vp(PG_ROLL, 2, 3), 0, 127);
    t[0] = 0; val_text(t, PG_ROLL, 2, 0); ascii(p->name, t, sizeof p->name);
    t[0] = 0; val_text(t, PG_ROLL, 2, 1); ascii(p->tag, t, sizeof p->tag);
    p->live[0] = (int16_t)(arp_on(s) && arp[s].n ? arp[s].k % 16 : -1);
}

static void pic_pan(mv_view_t *v)
{
    mv_pic_t *p = &v->pic;
    p->kind = PIC_PAN;
    for (int t = 0; t < DMK_SYNTHS; t++) { sound_name(p->line[t], t, sizeof p->line[t]); p->p[t] = v->knob[t].raw; }
}

/* the song: its steps, five in view around the cursor, and its length */
static void pic_song(mv_pic_t *p)
{
    int bars = 0, playing = song_playing() ? song_at - 1 : -1, first = ui.arr_cur - 2;
    char *o = p->name;
    p->kind = PIC_SONG;
    for (int i = 0; i < song_n; i++) bars += song[i].bars;
    if (song_n) {
        const int secs = bars * 4 * 60 / (seq_bpm ? seq_bpm : 72);
        char *e = p->tag;
        o = fmt_int(o, song_n); o = str_cat(o, song_n == 1 ? " step, " : " steps, ");
        o = fmt_int(o, bars); str_cat(o, bars == 1 ? " bar" : " bars");
        e = fmt_int(e, secs / 60); *e++ = ':'; *e++ = (char)('0' + secs % 60 / 10); *e++ = (char)('0' + secs % 10); *e = 0;
    } else str_cat(o, "A scene adds a step");
    if (first > song_n + 1 - MV_CELLS) first = song_n + 1 - MV_CELLS;
    if (first < 0) first = 0;
    p->now = p->cur = 0xFF;
    for (int i = 0; i < MV_CELLS && first + i <= song_n; i++) {
        const int j = first + i;
        mv_cell_t *c = &p->cell[i];
        p->cells = (uint8_t)(i + 1);
        if (j == ui.arr_cur) p->cur = (uint8_t)i;
        if (j == song_n) { c->name[0] = '+'; c->name[1] = 0; continue; }
        if (j == playing) p->now = (uint8_t)i;
        fmt_int(c->name, song[j].scene + 1);
        c->bars = song[j].bars;
        c->mutes = scn[song[j].scene].mute;
    }
}

/* the progression: its chords as cells, the one now lit and named large */
static void pic_chord(mv_pic_t *p)
{
    const prog_t *pr = cur_prog();
    const int n = pr->n, writing = !seq_on && seq_rec, now = ch_shown >= 0 ? ch_shown : 0;
    const int focus = writing ? ch_cur : now, cells = n + (writing && n < PROG_SLOTS), show = cells > MV_CELLS - 1 ? MV_CELLS - 1 : cells;
    int first = 0;
    char t[24], *e;
    p->kind = PIC_CHORD;
    ascii(p->name, pr->name, sizeof p->name);
    /* the scale beside the progression's name, shorter where the two would meet */
    static const char *const TAG[MO_COUNT] = {"Aeolian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Ionian", "Locrian",
        "Harm min", "Mel min", "Harm maj", "Phryg dom", "Lyd dom", "Hung min", "Dbl harm", "Min penta", "Maj penta", "Blues",
        "Hirajoshi", "In sen", "Iwato", "Yo", "Whole tone", "Diminished"};
    e = str_cat(t, KEYS[seq_key]); e = str_cat(e, " "); str_cat(e, TAG[seq_mode]);
    ascii(p->tag, t, sizeof p->tag);
    t[0] = 0;
    if (ch_shown == -1 && n) chord_name(t, seq_key + pr->s[0].root, pr->s[0].q);
    else str_cat(t, ch_name);
    ascii(p->big, t, sizeof p->big);
    if (cells > show) { first = focus - 1; if (first > cells - show) first = cells - show; if (first < 0) first = 0; }
    p->cells = (uint8_t)show;
    p->now = p->cur = 0xFF;
    for (int i = 0; i < show; i++) {
        const int j = first + i;
        if (j >= n) { p->cell[i].name[0] = '+'; p->cell[i].name[1] = 0; }
        else { t[0] = 0; chord_name_q(t, seq_key + pr->s[j].root, pr->s[j].q, QS); ascii(p->cell[i].name, t, sizeof p->cell[i].name); }
        if (!writing && ch_shown != -2 && j == now) p->now = (uint8_t)i;
        if (writing && j == ch_cur) p->cur = (uint8_t)i;
    }
}

/* an FM sound: its name, its algorithm, each operator's level as the chip has it (Bright on the modulators), the ones
 * heard marked; on an operator's row, that one */
static void pic_fm(mv_pic_t *p)
{
    const int s = vsyn(), alg = ymv[s][YV_ALG] & 7, bright = ymv[s][YV_BRIGHT] - YM_BRIGHT_MID, row = mp_row();
    p->kind = PIC_FM;
    p->on = 1;
    ascii(p->name, YM_PATCH[syn[s].ym - 1].name, sizeof p->name);
    fmt_int(str_cat(p->tag, "Algorithm "), alg + 1);
    p->p[4] = YM_CARRIERS[alg];
    for (int o = 0; o < 4; o++) {
        int tl = ymv[s][YV_OP + o * YO_N + YO_TL];
        if (!(YM_CARRIERS[alg] >> o & 1)) tl = clampi(tl - bright, 0, YM_TL_MAX);
        p->p[o] = (uint8_t)(YM_TL_MAX - tl);
    }
    p->cur = (uint8_t)(row >= YR_OP1 && row <= YR_OP4 ? row - YR_OP1 : 0xFF);
}

/* the sound's own shape, the loop's ends, where it starts, each voice playing it */
static void pic_sound(mv_pic_t *p)
{
    const int s = vsyn(), lay = mp_row() == 1, i = *vp(PG_SOUND, lay, lay ? 4 : 0);
    const dmk_sample_t *sm = &DMK_SAMPLES[i < DMK_SAMPLES_N ? i : 0];
    const int n = sm->blocks ? sm->blocks : 1;
    const int loops = lay ? sm->loops && *vp(PG_SOUND, 1, 2) >= 64 : syn[s].loops == 2 ? 2 : sm->loops && syn[s].loops;
    const int la = loops == 2 ? syn[s].la : sm->loop0, lb = loops == 2 ? syn[s].lb : sm->loop1;
    int heads = 0;
    p->kind = PIC_SOUND;
    p->on = (uint8_t)(!lay || *vp(PG_SOUND, 1, 0));
    ascii(p->name, sm->name, sizeof p->name);
    if (lay && p->on) str_cat(fmt_int(str_cat(p->tag, "Layer "), *vp(PG_SOUND, 1, 0) * 100 / 127), "%");
    else str_cat(p->tag, lay ? "Layer off" : loops == 2 ? "Loop set" : loops ? "Looped" : "One-shot");
    memcpy(p->env, sm->env, sizeof p->env);
    p->p[0] = (uint8_t)(loops != 0);
    p->p[1] = (uint8_t)clampi(la * 127 / n, 0, 127);
    p->p[2] = (uint8_t)clampi((lb - (lb >= n)) * 127 / n, 0, 127);
    p->p[3] = (uint8_t)(!lay && syn[s].start ? clampi(syn[s].start * 127 / n, 1, 127) : 0);
    if (p->on)
        for (int v = 0; v < DMK_VOICES && heads < 4; v++) {
            const strm_t *t = lay ? &vc[v].l : &vc[v].m;
            if (vc[v].stage && vc[v].sy == s && t->s == sm && (!lay || t->on))
                p->head[heads++] = (uint8_t)(1 + clampi(t->blk * 127 / n, 0, 127));
        }
}

/* Filter and Amp: the numbers of both shapes; a note playing, where its envelope has the cutoff (live 0) and where it
 * is along the envelope: its stage x 32, its share of that stage (live 1) */
static void pic_shapes(mv_pic_t *p)
{
    const int s = vsyn(), type = *vp(PG_FILTER, 0, 4) * 8 / 128, nv = voice_newest(s);
    char t[16];
    p->kind = PIC_SHAPES;
    p->type = (uint8_t)type;
    for (int k = 0; k < 3; k++) p->p[k] = (uint8_t)clampi(*vp(PG_FILTER, 0, k), 0, 127);
    for (int k = 0; k < 4; k++) p->p[4 + k] = (uint8_t)clampi(*vp(PG_AMP, 0, k), 0, 127);
    p->p[3] = (uint8_t)clampi(*vp(PG_AMP, 0, 4), 0, 127);
    t[0] = 0; val_text(t, PG_FILTER, 0, 4); ascii(p->name, t, sizeof p->name);
    p->live[0] = p->live[1] = -1;
    if (nv >= 0) {
        const voice_t *o = &vc[nv];
        const int32_t sus = syn[s].sus >> 8, e = o->env >> 8, full = FULL >> 8;   /* (Q16: 32-bit products) */
        int stage, f;
        if (o->stage == 1) { stage = 0; f = e * 31 / full; }
        else if (o->stage == 2 && e > sus + full / 64) { stage = 1; f = 31 * (full - e) / (full - sus > 0 ? full - sus : 1); }
        else if (o->stage == 2) { stage = 2; f = (int)(o->blocks / 8u % 32u); }
        else { stage = 3; f = sus > 0 ? 31 * (sus - (e < sus ? e : sus)) / sus : 31; }
        p->live[1] = (int16_t)(stage * 32 + clampi(f, 0, 31));
        if (type != FT_OFF && type != FT_VOWEL && syn[s].fenv)
            p->live[0] = (int16_t)(clampi(syn[s].cut + mul24(syn[s].fenv, o->env), 0, 255) / 2);
    }
}

static void pic_lfo(mv_pic_t *p)
{
    const int s = vsyn();
    char t[16];
    p->kind = PIC_LFO;
    p->p[0] = (uint8_t)clampi(*vp(PG_LFO, 0, 0), 0, 127);
    p->p[1] = (uint8_t)clampi(*vp(PG_LFO, 0, 1), 0, 127);
    p->type = (uint8_t)(*vp(PG_LFO, 0, 2) * 5 / 128);
    p->p[3] = (uint8_t)clampi(*vp(PG_LFO, 0, 3), 0, 127);
    t[0] = 0; val_text(t, PG_LFO, 0, 4); ascii(p->name, t, sizeof p->name);
    t[0] = 0; val_text(t, PG_LFO, 1, 0); ascii(p->tag, t, sizeof p->tag);
    p->live[0] = (int16_t)(lfo[s].ph >> 24);
}

/* an effect: its type's name and the four numbers of its row */
static void pic_effect(mv_pic_t *p, int kind, int pg, int row)
{
    char t[16];
    const int ty = choice_index(choice_of(pg, row, 4), *vp(pg, row, 4));
    p->kind = (uint8_t)kind;
    p->on = (uint8_t)(ty != 0);
    p->type = (uint8_t)ty;
    if (kind == PIC_TAIL)
        p->type = (uint8_t)(pg != PG_GLOBAL ? TL_PLAIN : ty == SP_BLOOM ? TL_BLOOM : ty == SP_SHIMMER ? TL_SHIMMER :
                            ty == SP_REVERSE ? TL_REVERSE : ty == SP_FREEZE ? TL_FREEZE : TL_PLAIN);
    for (int k = 0; k < 4; k++) p->p[k] = (uint8_t)clampi(*vp(pg, row, k), 0, 127);
    t[0] = 0; val_text(t, pg, row, 4); ascii(p->name, t, sizeof p->name);
}

/* Save: four projects around the cursor, the three tracks' sounds */
static void pic_projects(mv_pic_t *p)
{
    p->kind = PIC_PROJECTS;
    p->open = 0xFF;
    for (int i = 0; i < 4; i++) {
        const int n = ui.list_top + i;
        char nm[24];
        if (n >= ST_SONGS) break;
        p->cells = (uint8_t)(i + 1);
        p->num[i] = (uint8_t)(n + 1);
        if (n == ui.song_pick) p->cur = (uint8_t)i;
        if (n == ui.song) p->open = (uint8_t)i;
        nm[0] = 0;
        if (song_used(n)) {
            if (ui.ren_slot == n) nm_of(nm, ui.ren_v);
            ascii(p->line[i], nm[0] ? nm : song_name(n), sizeof p->line[i]);
        } else str_cat(p->line[i], n == ui.song ? "Not saved" : "Empty");
    }
}

static void pic_sounds(mv_pic_t *p)
{
    p->kind = PIC_SOUNDS;
    p->cells = DMK_SYNTHS;
    p->cur = (uint8_t)vsyn();
    for (int t = 0; t < DMK_SYNTHS; t++) {
        const int v = VS(t, PG_SOUND, 0, 0), user = ui.preset[t] >= DMK_SAMPLES_N ? lib_nth(ui.preset[t] - DMK_SAMPLES_N) : -1;
        p->num[t] = (uint8_t)(t + 1);
        ascii(p->line[t], syn[t].ym ? YM_PATCH[syn[t].ym - 1].name : DMK_SAMPLES[v < DMK_SAMPLES_N ? v : 0].name, sizeof p->line[t]);
        if (user >= 0) fmt_int(str_cat(p->cell[t].name, user < 9 ? "User 0" : "User "), user + 1);
    }
}

/* a synth's notes on pattern p: the open one's are in its roll */
static const snote_t *pat_notes(int s, int p, int *count, int *steps)
{
    const pseq_t *q = &spat[s][p];
    const int open = p == syn_pat(s);
    *count = open ? sq[s].count : q->used ? q->count : 0;
    *steps = open ? sq[s].steps : q->used && q->steps ? q->steps : 16;
    return open ? sq[s].n : q->n;
}

/* the patterns' list: the eight, those with notes on any synth marked, the notes of the one under the cursor drawn small */
static void pic_pats(mv_view_t *v)
{
    mv_pic_t *p = &v->pic;
    mv_roll_t *w = &v->roll;
    const int sel = mc.pat_sel, inview = vsyn();
    int lo = 127, hi = 0, any = 0, count, steps;
    p->kind = PIC_PATS;
    p->cells = SYN_PATS;
    p->now = (uint8_t)syn_pat(inview);
    p->cur = (uint8_t)sel;
    p->open = spat_next[inview] < SYN_PATS ? spat_next[inview] : 0xFF;
    for (int i = 0; i < SYN_PATS; i++)
        for (int s = 0; s < DMK_SYNTHS; s++) { pat_notes(s, i, &count, &steps); if (count) p->p[0] |= (uint8_t)(1u << i); }
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const snote_t *n = pat_notes(s, sel, &count, &steps);
        any += count;
        for (int i = 0; i < count; i++) { if (n[i].note < lo) lo = n[i].note; if (n[i].note > hi) hi = n[i].note; }
    }
    fmt_int(str_cat(p->name, "Pattern "), sel + 1);
    str_cat(p->tag, p->open == sel ? "At the bar" : !any ? "Empty" : "");
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const snote_t *n = pat_notes(s, sel, &count, &steps);
        for (int i = 0; i < count && w->count < MV_NOTES; i++) {
            mv_note_t *o = &w->n[w->count++];
            const int b = (int)n[i].step * 124 / steps, l = n[i].len * 124 / steps;
            o->x = (uint8_t)b;
            o->w = (uint8_t)(l < 1 ? 1 : l);
            o->row = (int8_t)(hi > lo ? (hi - n[i].note) * 13 / (hi - lo) : 6);
        }
    }
}

static void pic_build(mv_view_t *v)
{
    mv_pic_t *p = &v->pic;
    const int row = mp_row();
    p->live[0] = p->live[1] = -1;
    p->now = p->cur = p->open = 0xFF;
    if (mc.plist) { pic_pats(v); return; }
    switch (mc.page) {
    case MP_HOME: if (row == 1) pic_pan(v); else if (row == 2) pic_song(p); break;
    case MP_ROLL: if (row) pic_arp(p); else { p->kind = PIC_ROLL; roll_view(v); } break;
    case MP_CHORD: pic_chord(p); break;
    case MP_SOUND: if (syn[vsyn()].ym) pic_fm(p); else pic_sound(p); break;
    case MP_FILTAMP: pic_shapes(p); break;
    case MP_LFO: pic_lfo(p); break;
    case MP_FX: pic_effect(p, row == 0 ? PIC_TAIL : row == 1 ? PIC_ECHO : PIC_MOTION, PG_FX, row); break;
    case MP_GLOBAL: pic_effect(p, row == 0 ? PIC_TAIL : row == 1 ? PIC_COLOUR : PIC_MEDIA, PG_GLOBAL, row == 0 ? 1 : row == 1 ? 0 : 2); break;
    case MP_SAVE: if (row) pic_sounds(p); else pic_projects(p); break;
    default: break;
    }
}

static void mv_view_build(mv_view_t *v)
{
    const int s = vsyn();
    memset(v, 0, sizeof *v);
    v->t = (uint16_t)(ui.now / 100u);
    v->mp = mc.page;
    v->mrow = (uint8_t)mp_row();
    v->rows = mp_of()->rows;
    v->land = (uint8_t)scene_of(s);
    for (int t = 0; t < DMK_SYNTHS; t++) {
        int i;
        v->letters[t] = syn[t].ym ? YM_PATCH[syn[t].ym - 1].name[0]
                                  : DMK_SAMPLES[VS(t, PG_SOUND, 0, 0) < DMK_SAMPLES_N ? VS(t, PG_SOUND, 0, 0) : 0].name[0];
        for (i = 0; i < DMK_VOICES; i++) if (vc[i].on && vc[i].sy == t) v->lights |= (uint8_t)(1u << t);
    }
    ascii(v->page, mp_of()->name, sizeof v->page);
    if (!(mc.page == MP_ROLL && !mp_row())) ascii(v->row, mp_of()->rname[mp_row()], sizeof v->row);   /* (no "Roll Roll") */
    sound_name(v->sound, s, sizeof v->sound);
    if (ui.note_ms && ui.now - ui.note_ms < MV_NOTE_MS) ascii(v->note, ui.note, sizeof v->note);
    for (int k = 0; k < 8; k++) {
        const kref_t r = mp_knob(k);
        knob_view(&v->knob[k], r, k);
        v->kkind[k] = (uint8_t)knob_kind(r, k);
    }
    pic_build(v);
    if (mc.shift) {                    /* knob 1 is the tempo on every page */
        mv_knob_t *o = &v->knob[0];
        char t[12];
        memset(o, 0, sizeof *o);
        o->on = 1;
        fmt_int(t, seq_bpm);
        ascii(o->name, "Tempo", sizeof o->name);
        ascii(o->val, t, sizeof o->val);
        o->raw = (uint8_t)clampi((seq_bpm - 20) * 127 / 235, 0, 127);
        v->kkind[0] = MV_KK_TEMPO;
    }
    if (steps_held()) { held_view(v); v->held = 1; }
    v->focus = mc.focus;
    v->playing = seq_on;
    v->rec = seq_rec;
    v->ready = (uint8_t)(ui.rec_ready && seq_rec && !seq_on);
    v->sync = (uint8_t)mv_sync();
    v->synth = ui.synth;
    v->mutes = pat_mute;
    v->scene = scn_cur;
    v->pat = (uint8_t)syn_pat(s);
    v->bar = (uint8_t)bar_of(s);
    v->bars = (uint8_t)bars_of(s);
    v->in_key = mc.in_key;
    v->oct = ui.octave;
    {
        const int st = seq_on ? seq_playing_step(s) : -1;
        v->step = (int8_t)(st >= 0 && st / 16 == bar_of(s) ? st % 16 : -1);
    }
    v->bpm10 = (uint16_t)mv_bpm10();
    v->tq_seq = tq.seq;
    v->tq_b10 = tq.b10;
    mv_leds(v);
}
