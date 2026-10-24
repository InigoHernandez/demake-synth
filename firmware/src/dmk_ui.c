/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's screen and controls, phase 0: the shell, under the design rules (docs/plan.md, 5). Minimal and big, cards
 * after the owner's references (AI OS: a two-line title in a card's corner, ink then grey; one big thing in it; tick
 * meters; captions): Geist for all text, Kiloy for the wordmark and for the shapes (corners of 14, pills, round
 * ends); sentence case, names as they are written (FX, LFO, Am(add9)); five sizes (FS 12, FM 20, FH 24, FN 36,
 * FL 40); a monotone UI (PAPER, LINE, LABEL, INK) and the thermal light for everything lit (HOLO: the value turning,
 * the cursor, what sounds, the chord now). The stages are placeholders until their pages are built. Included by
 * dmk_core.c.
 *
 * The layout: 240 x 240, four 60 px columns under K1-K4, no rules: the cards and the spacing are the structure.
 *   head    0-39    the page's title and its row, one size, ink then grey; the synths
 *   fields  40-101  four columns: label, value, meter
 *   stage   102-219 cards, 6 px from the edge and apart, corners of 14
 *   foot    224-239 the ALGORITHM encoder's value */

enum { PG_HOME, PG_ROLL, PG_SOUND, PG_FILTER, PG_AMP, PG_FX, PG_CHORD, PG_GLOBAL, PG_LFO, PG_DISCS, PG_COUNT };

#define Y_FIELDS 40
#define Y_STAGE  102
#define Y_FOOT   224
#define COL      60
#define PAD      8                                      /* text's inset in a cell */
#define M        6                                      /* the cards' margin to the screen's edge */
#define G        6                                      /* and between cards */
#define R_CARD   14                                     /* a card's corner: Kiloy's round */
#define DOT      6                                      /* a choice's dot */

typedef struct {
    const char *name;
    uint8_t rows;
    const char *row[4];
    const char *algo[4];
    const char *k[4][4];
} page_t;

static const page_t PAGE[PG_COUNT] = {
    {"Home", 4, {"Level", "Verb", "Echo", "Pan"}, {"Tempo", "Tempo", "Tempo", "Tempo"},
     {{"Level 1", "Level 2", "Level 3", "Verb"}, {"Verb 1", "Verb 2", "Verb 3", "Time"},
      {"Echo 1", "Echo 2", "Echo 3", "Regen"}, {"Pan 1", "Pan 2", "Pan 3", "Width"}}},
    {"Roll", 1, {"Roll"}, {"Zoom"}, {{"Cursor", "View", "Length", "Steps"}}},
    {"Sound", 2, {"Engine", "Layer"}, {"Engine", "Sample"},
     {{"Sample", "Start", "Loop", "Tune"}, {"Blend", "Tune", "Loop", "Tone"}}},
    {"Filter", 1, {"Filter"}, {"Type"}, {{"Cutoff", "Reso", "Env", "Drift"}}},
    {"Amp", 1, {"Amp"}, {"Level"}, {{"Attack", "Decay", "Sustain", "Release"}}},
    {"FX", 4, {"Colour", "Media", "Motion", "Space"}, {"Effect", "Effect", "Effect", ""},
     {{"Amount", "Tone", "Time", "Spice"}, {"Amount", "Tone", "Time", "Spice"},   /* an effect names its own */
      {"Amount", "Tone", "Time", "Spice"}, {"Verb", "Echo", "Pan", "Width"}}},
    {"Chord", 2, {"Voice", "Key"}, {"Prog", "Prog"},
     {{"Voicing", "Invert", "Spread", "Strum"}, {"Key", "Mode", "Change", "Rhythm"}}},
    {"Global", 4, {"Verb", "Echo", "Song", "Setup"}, {"Type", "Type", "", "Velocity"},
     {{"Return", "Tone", "Size", "Mod"}, {"Return", "Tone", "Time", "Regen"},
      {"Swing", "Switch", "Click", "Rec"}, {"Theme", "Keys", "MIDI", "Clock"}}},
    {"LFO", 1, {"LFO"}, {"Target"}, {{"Rate", "Depth", "Shape", "Fade"}}},
    {"Songs", 1, {"Songs"}, {""}, {{"Song", "Card", "", ""}}},
};

/* the page each button opens (-1: none) */
static const int8_t BTN_PAGE[B_COUNT] = {PG_FX, PG_FILTER, PG_AMP, PG_LFO, PG_SOUND, PG_GLOBAL, PG_HOME, -1, PG_CHORD,
                                         PG_ROLL, -1, -1, -1, -1};
static const char *const BTN_NAME[B_COUNT] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                              "PLAY", "REC", "OCT−", "OCT+"};
static const char *const LAYER_NAME[B_COUNT] = {"the four amounts", "help", "help", "help", "the sound", "the history",
                                                "", "songs", "the progression", "the sequence", "", "", "", ""};

/* a choice field: its names (none: a number 0..127) */
typedef struct { uint8_t page, row, k, n; const char *const *name; } choice_t;
static const char *const ENGINES[] = {"Sample", "Pad", "Drone", "Pluck"};
static const char *const FILTERS[] = {"LP24", "LP12", "BP", "HP", "Off"};
static const char *const ZOOMS[] = {"16", "32", "64"};
static const char *const OFF[] = {"Off"};
static const char *const LOOPS[] = {"Off", "Full"};
static const char *const MEDIA[] = {"Off", "Tape"};
static const char *const VERBS[] = {"Room"};
static const char *const ECHOS[] = {"Tape"};
static const choice_t CHOICE[] = {
    {PG_SOUND, 0, 4, 1, ENGINES}, {PG_SOUND, 0, 2, 2, LOOPS}, {PG_FILTER, 0, 4, 5, FILTERS}, {PG_ROLL, 0, 4, 3, ZOOMS},
    {PG_FX, 0, 4, 1, OFF}, {PG_FX, 1, 4, 2, MEDIA}, {PG_FX, 2, 4, 1, OFF}, {PG_GLOBAL, 3, 0, DMK_THEMES, THEME_NAME},
    {PG_GLOBAL, 0, 4, 1, VERBS}, {PG_GLOBAL, 1, 4, 1, ECHOS},
};

static struct {
    uint8_t page, row[PG_COUNT], synth, boot_done, boot_kept;
    int8_t octave;
    uint8_t val[DMK_SYNTHS][PG_COUNT][4][5];            /* [synth][page][row][K1..K4, ALGORITHM] 0..127 (the global
                                                           pages keep theirs in synth 0's) */
    uint32_t now, btn_down_at[B_COUNT], note_ms, touch_ms;
    int8_t touch_k;                                     /* the field turned last (lit for a second) */
    uint16_t btn_held, btn_used;                        /* bit per button */
    char note[32];                                      /* a notice, shown for 1.5 s from note_ms */
    uint8_t keys_down[DMK_KEYS], key_sy[DMK_KEYS], key_note[DMK_KEYS];
    /* the roll, per synth: the cursor, the lowest scale row shown, the pen's length, the zoom (16 << z steps) */
    uint8_t roll_cur[DMK_SYNTHS], roll_view[DMK_SYNTHS], roll_pen[DMK_SYNTHS], roll_zoom[DMK_SYNTHS];
    uint8_t key_roll[DMK_KEYS], key_step[DMK_KEYS];     /* a key's note: 1 written or held on the roll (at key_step),
                                                           2 being recorded, 3 deleted while held */
    uint32_t key_t0[DMK_KEYS];                          /* recording: where the playing was when it went down */
    uint16_t hinted;                                    /* the pages whose next row was shown how */
} ui;

static uint8_t leds[DMK_KEYS + B_COUNT];

static void notice(const char *s)
{
    int i;
    for (i = 0; i < 31 && s[i]; i++) ui.note[i] = s[i];
    ui.note[i] = 0;
    ui.note_ms = ui.now ? ui.now : 1;
}

/* a control's value: a synth's own on its pages, the song's on Home's last column and Global; Home's rows are views of
 * each synth's level (AMP), sends and pan (FX, SPACE) */
static int per_synth(int page) { return page == PG_SOUND || page == PG_FILTER || page == PG_AMP || page == PG_FX || page == PG_LFO; }
static uint8_t *vp(int page, int row, int k)
{
    if (page == PG_HOME && k == 4) return &ui.val[0][PG_HOME][0][4];   /* the tempo, on every row */
    if (page == PG_HOME && k < 3) {
        static const uint8_t PG[4] = {PG_AMP, PG_FX, PG_FX, PG_FX}, RW[4] = {0, 3, 3, 3}, KK[4] = {4, 0, 1, 2};
        return &ui.val[k][PG[row]][RW[row]][KK[row]];
    }
    if (page == PG_HOME && k == 3) {                    /* the song's: verb return, verb size, echo regen, width */
        static const uint8_t RW[4] = {0, 0, 1, 3}, KK[4] = {0, 2, 3, 0};
        return row == 3 ? &ui.val[0][PG_HOME][3][3] : &ui.val[0][PG_GLOBAL][RW[row]][KK[row]];
    }
    return &ui.val[per_synth(page) ? ui.synth : 0][page][row][k];
}
#define VS(sy, page, row, k) (ui.val[sy][page][row][k])

/* the controls to the sound's units (dmk_dsp.c): called on every change */
static void sp_sync(void)
{
    int s;
    for (s = 0; s < DMK_SYNTHS; s++) {
        synth_t *p = &syn[s];
        const dmk_sample_t *sm;
        int v;
        p->smp = (uint8_t)(VS(s, PG_SOUND, 0, 0) < DMK_SAMPLES_N ? VS(s, PG_SOUND, 0, 0) : 0);
        sm = &DMK_SAMPLES[p->smp];
        p->start = (uint16_t)(VS(s, PG_SOUND, 0, 1) * ((sm->loops ? sm->loop0 : sm->blocks) / 2) / 127);
        p->loops = (uint8_t)(VS(s, PG_SOUND, 0, 2) >= 64);
        p->tune = (int16_t)((VS(s, PG_SOUND, 0, 3) - 64) * 24 / 64);
        p->ftype = (uint8_t)(VS(s, PG_FILTER, 0, 4) * 5 / 128);
        p->cut = (int16_t)(VS(s, PG_FILTER, 0, 0) >= 127 ? 255 : VS(s, PG_FILTER, 0, 0) * 2);
        p->resq = T_RESQ[VS(s, PG_FILTER, 0, 1)];
        p->fenv = (int16_t)((VS(s, PG_FILTER, 0, 2) - 64) * 4);
        p->atk = T_ATK[VS(s, PG_AMP, 0, 0)];
        p->dec = T_EXP[VS(s, PG_AMP, 0, 1)];
        p->sus = (int32_t)((uint32_t)VS(s, PG_AMP, 0, 2) * (FULL / 127));
        p->rel = T_EXP[VS(s, PG_AMP, 0, 3)];
        v = VS(s, PG_AMP, 0, 4);
        p->level = v * v * 2;                           /* Q15 on a square law: 64 is -12 dB */
        p->tape = (uint8_t)(VS(s, PG_FX, 1, 4) >= 64);
        p->tdepth = VS(s, PG_FX, 1, 0) * 800;           /* the wow: up to ~40 cents at its slowest */
        p->tdrive = 256 + VS(s, PG_FX, 1, 0) * 3;
        p->thl = T_OP[64 + VS(s, PG_FX, 1, 1) / 2];     /* head loss: 600 Hz .. open */
        p->twow = 19480u + (uint32_t)VS(s, PG_FX, 1, 2) * 1500u;   /* 0.2 .. 2.2 Hz */
        p->tflut = 620000u + (uint32_t)VS(s, PG_FX, 1, 3) * 2000u; /* 6.3 .. 7.7 Hz */
        p->vsend = VS(s, PG_FX, 3, 0) * 258;
        p->esend = VS(s, PG_FX, 3, 1) * 258;
        v = VS(s, PG_FX, 3, 2);
        p->gl = T_PAN[127 - v]; p->gr = T_PAN[v];
    }
    gp.vret = VS(0, PG_GLOBAL, 0, 0) * 258;
    gp.vdamp = T_OPH[VS(0, PG_GLOBAL, 0, 1)];
    gp.vfb = 22938 + VS(0, PG_GLOBAL, 0, 2) * 72;
    gp.eret = VS(0, PG_GLOBAL, 1, 0) * 258;
    gp.elp = T_OPH[VS(0, PG_GLOBAL, 1, 1)];
    gp.etime = T_ETIME[VS(0, PG_GLOBAL, 1, 2)];
    gp.efb = VS(0, PG_GLOBAL, 1, 3) * 240;
    seq_bpm = VS(0, PG_HOME, 0, 4) < 20 ? 20 : VS(0, PG_HOME, 0, 4);
    cur_synth = ui.synth;
}

static int smp_named(const char *n)
{
    int i, j;
    for (i = 0; i < DMK_SAMPLES_N; i++) {
        for (j = 0; n[j] && DMK_SAMPLES[i].name[j] == n[j]; j++) {}
        if (!n[j] && !DMK_SAMPLES[i].name[j]) return i;
    }
    return 0;
}

static void ui_init(void)
{
    /* the three synths' first sounds: a pad, keys, a pluck; [sample, attack, decay, sustain, release, cutoff, reso,
     * env, level, tape, wow, verb, echo, pan] */
    static const char *const FIRST[DMK_SYNTHS] = {"Snow pad", "Felt piano", "Glass pluck"};
    static const uint8_t SET[DMK_SYNTHS][13] = {
        {48, 72, 112, 88, 104, 16, 72, 100, 1, 36, 84, 44, 52},
        {0, 80, 56, 72, 96, 12, 76, 96, 1, 24, 60, 52, 72},
        {0, 60, 0, 64, 112, 24, 80, 92, 0, 0, 72, 76, 40}};
    int s, p, r, k;
    for (s = 0; s < DMK_SYNTHS; s++)
        for (p = 0; p < PG_COUNT; p++)
            for (r = 0; r < 4; r++)
                for (k = 0; k < 5; k++) ui.val[s][p][r][k] = 64;
    for (s = 0; s < DMK_SYNTHS; s++) {
        const uint8_t *t = SET[s];
        ui.val[s][PG_SOUND][0][0] = (uint8_t)smp_named(FIRST[s]);
        ui.val[s][PG_SOUND][0][1] = 0;
        ui.val[s][PG_SOUND][0][2] = 127;                /* the loop on */
        ui.val[s][PG_SOUND][0][4] = 0;
        ui.val[s][PG_AMP][0][0] = t[0]; ui.val[s][PG_AMP][0][1] = t[1]; ui.val[s][PG_AMP][0][2] = t[2];
        ui.val[s][PG_AMP][0][3] = t[3]; ui.val[s][PG_AMP][0][4] = t[7];
        ui.val[s][PG_FILTER][0][0] = t[4]; ui.val[s][PG_FILTER][0][1] = t[5]; ui.val[s][PG_FILTER][0][2] = t[6];
        ui.val[s][PG_FILTER][0][4] = 0;                 /* LP24 */
        ui.val[s][PG_FX][1][4] = t[8] ? 96 : 0;         /* tape */
        ui.val[s][PG_FX][1][0] = t[9]; ui.val[s][PG_FX][1][1] = 90;
        ui.val[s][PG_FX][3][0] = t[10]; ui.val[s][PG_FX][3][1] = t[11]; ui.val[s][PG_FX][3][2] = t[12];
    }
    ui.val[0][PG_HOME][0][4] = 72;                      /* tempo 72 */
    ui.val[0][PG_GLOBAL][0][0] = 84; ui.val[0][PG_GLOBAL][0][1] = 56; ui.val[0][PG_GLOBAL][0][2] = 96;   /* the room */
    ui.val[0][PG_GLOBAL][1][0] = 64; ui.val[0][PG_GLOBAL][1][1] = 52; ui.val[0][PG_GLOBAL][1][2] = 76;
    ui.val[0][PG_GLOBAL][1][3] = 64;                    /* the echo: ~0.4 s, a few repeats */
    for (s = 0; s < DMK_SYNTHS; s++) {                  /* each roll on its own pattern: its notes in view */
        ui.roll_cur[s] = 0;
        ui.roll_pen[s] = 2;
        ui.roll_zoom[s] = (uint8_t)(sq[s].steps > 16);
    }
    ui.roll_view[0] = (uint8_t)degree_of(53); ui.roll_view[1] = (uint8_t)degree_of(64); ui.roll_view[2] = (uint8_t)degree_of(69);
    ui.row[PG_FX] = 1;                                  /* Media: the first effect built */
    ui.page = PG_HOME;
    ui.touch_k = -1;
    sp_sync();
}

static const choice_t *choice_of(int page, int row, int k)
{
    unsigned i;
    for (i = 0; i < sizeof CHOICE / sizeof CHOICE[0]; i++)
        if (CHOICE[i].page == page && CHOICE[i].row == row && CHOICE[i].k == k) return &CHOICE[i];
    return 0;
}

static int choice_index(const choice_t *c, int v) { int i = v * c->n / 128; return i < c->n ? i : c->n - 1; }

static void val_text(char *o, int page, int row, int k)
{
    const choice_t *c = choice_of(page, row, k);
    int v = *vp(page, row, k);
    if (page == PG_GLOBAL && row == 3 && k == 0) { str_cat(o, THEME_NAME[theme]); return; }
    if (page == PG_SOUND && row == 0 && k == 0) {       /* the sound's number; its card under it has the name */
        if (v < 9) *o++ = '0';
        fmt_int(o, v + 1);
        return;
    }
    if (page == PG_SOUND && row == 0 && k == 3) {       /* the tune, in semitones */
        int t = (v - 64) * 24 / 64;
        if (t > 0) *o++ = '+';
        fmt_int(o, t);
        return;
    }
    if (c) { str_cat(o, c->name[choice_index(c, v)]); return; }
    fmt_int(o, v);
}

/* ---- the roll (dmk_seq.c): the current synth's own pattern, a window of steps across, seven scale rows up */
static int roll_z(void) { return 16 << ui.roll_zoom[ui.synth]; }
static int roll_at(void) { return seq_on ? seq_playing_step(ui.synth) : ui.roll_cur[ui.synth]; }
static int roll_held(void)                              /* a key holding a note on the roll, or -1 */
{
    int k;
    for (k = 0; k < DMK_KEYS; k++) if (ui.keys_down[k] && ui.key_roll[k] == 1) return k;
    return -1;
}
static void note_name(char *o, int n)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    fmt_int(str_cat(o, NM[n % 12]), n / 12 - 1);
}

static int lit(int k) { return ui.touch_k == k && ui.now - ui.touch_ms < 1000u; }
/* a row not built yet (COLOUR and MOTION in phase 4, the LAYER, the song): shown, marked soon, passed over */
static int row_soon(int page, int row)
{
    return (page == PG_FX && (row == 0 || row == 2)) || (page == PG_SOUND && row == 1) || (page == PG_GLOBAL && row == 2);
}
static int row_next(int page, int row)                  /* the page's next row that is built */
{
    int i;
    for (i = 1; i <= PAGE[page].rows; i++) { int r = (row + i) % PAGE[page].rows; if (!row_soon(page, r)) return r; }
    return row;
}
static int rows_built(int page) { int r, n = 0; for (r = 0; r < PAGE[page].rows; r++) n += !row_soon(page, r); return n; }

/* a row with nothing behind it: those not built, MEDIA while its effect is off */
static int row_off(int page, int row)
{
    if (page == PG_FX) return row == 0 || row == 2 || (row == 1 && *vp(PG_FX, 1, 4) < 64);
    return (page == PG_SOUND && row == 1) || (page == PG_GLOBAL && row == 2);
}

/* ---- the parts */

/* the stage's cards: n across the span from x, gap G, the remainder shared so every edge lands on a pixel */
static int cell_x(int x, int span, int n, int k) { return x + k * (span - (n - 1) * G) / n + k * G; }
static int cell_w(int x, int span, int n, int k) { return (k == n - 1 ? x + span : cell_x(x, span, n, k + 1) - G) - cell_x(x, span, n, k); }

/* a card: its outline (1 px LINE, or 2 px in c when it is the one chosen) */
static void card(int x, int y, int w, int h, int c, int t) { round_line(x, y, w, h, R_CARD, t, c); }

/* a card's title: what (ink), and which (grey), two lines of the small size in its top-left corner */
static void card_title(int x, int y, int w, const char *what, const char *which, int dim)
{
    text_fit(x + PAD + 1, y + 8, w - 2 * PAD, what, &FS, dim ? C_LABEL : C_INK);
    if (which && which[0]) text_fit(x + PAD + 1, y + 22, w - 2 * PAD, which, &FS, C_LABEL);
}

/* text in the medium size, or the small one when it does not fit */
static void text_mfit(int x, int y, int w, const char *s, int c)
{
    if (text_w(s, &FM) <= w) text(x, y, s, &FM, c);
    else text_fit(x, y + 4, w, s, &FS, c);
}

/* a value's meter: a row of ticks, INK up to the value, LINE after, the value's own tick tall (lit while it turns);
 * a choice is a row of dots, the chosen one filled */
static void meter(int x, int y, int w, int v, const choice_t *c, int on, int off)
{
    int ink = on ? HOLO : C_INK, k;
    if (c && c->n > 1 && !off) {
        int sel = choice_index(c, v);
        for (k = 0; k < c->n; k++) {
            int dx = x + k * (DOT + 3);
            if (k == sel) round_fill(dx, y + 2, DOT, DOT, DOT / 2, ink);
            else round_line(dx, y + 2, DOT, DOT, DOT / 2, 1, C_LINE);
        }
        return;
    }
    {
        int n = w / 3 + 1, at = off ? -1 : v * (n - 1) / 127;
        for (k = 0; k < n; k++) {
            if (k == at) round_fill(x + k * 3, y, 2, 10, 1, ink);
            else vline(x + k * 3, y + 3, 6, k < at ? C_INK : C_LINE);
        }
    }
}

static void field(int i, const char *label, int page, int row)
{
    int x = i * COL + PAD, w = COL - 2 * PAD, on = lit(i);
    char v[16];
    if (!label[0]) return;
    text_fit(x, Y_FIELDS, w, label, &FS, C_LABEL);
    if (row_off(page, row)) {
        text(x, Y_FIELDS + 16, "—", &FM, C_LINE);
        meter(x, Y_FIELDS + 44, w, 0, 0, 0, 1);
        return;
    }
    val_text(v, page, row, i);
    text_mfit(x, Y_FIELDS + 16, w, v, on ? HOLO : C_INK);
    meter(x, Y_FIELDS + 44, w, page == PG_SOUND && row == 0 && i == 0 ? *vp(page, row, i) * 127 / (DMK_SAMPLES_N - 1)
          : *vp(page, row, i), choice_of(page, row, i), on, 0);
}

/* the title, its row as the grey half (one size, one line), and the synths at the right: the current one filled */
static void head(void)
{
    const page_t *pg = &PAGE[ui.page];
    int x = text(PAD + 1, 6, pg->name, &FH, C_INK), i;
    if (pg->rows > 1) text(x + 7, 6, pg->row[ui.row[ui.page]], &FH, C_LABEL);
    for (i = 0; i < DMK_SYNTHS; i++) {
        int sx = DMK_W - PAD - 1 - (DMK_SYNTHS - i) * 13 + 4;
        if (i == ui.synth) round_fill(sx, 14, 9, 9, 4, C_INK);
        else round_line(sx, 14, 9, 9, 4, 1, C_LINE);
    }
}

static void field_show(int i, const char *label, const char *v, int m)
{
    int x = i * COL + PAD, w = COL - 2 * PAD, on = lit(i);
    text_fit(x, Y_FIELDS, w, label, &FS, C_LABEL);
    text_mfit(x, Y_FIELDS + 16, w, v, on ? HOLO : C_INK);
    meter(x, Y_FIELDS + 44, w, m < 0 ? 0 : m > 127 ? 127 : m, 0, on, 0);
}

static void roll_fields(void)                           /* the cursor's, or a held note's */
{
    static const char *const HELD[4] = {"Move", "Pitch", "Length", "Nudge"};
    const seq_t *q = &sq[ui.synth];
    int h = roll_held(), st = q->steps > 1 ? q->steps - 1 : 1, i;
    char v[16];
    if (h >= 0) {
        int j = seq_find(ui.synth, ui.key_step[h], ui.key_note[h]);
        const snote_t *n = &q->n[j < 0 ? 0 : j];
        for (i = 0; i < 4; i++) {
            v[0] = 0;
            if (j < 0 || i == 3) {                      /* NUDGE comes with the fine timing */
                int x = i * COL + PAD;
                text_fit(x, Y_FIELDS, COL - 2 * PAD, HELD[i], &FS, C_LABEL);
                text(x, Y_FIELDS + 16, "—", &FM, C_LINE);
                meter(x, Y_FIELDS + 44, COL - 2 * PAD, 0, 0, 0, 1);
                continue;
            }
            if (i == 0) fmt_int(v, n->step + 1);
            if (i == 1) note_name(v, n->note);
            if (i == 2) fmt_int(v, n->len);
            field_show(i, HELD[i], v, i == 0 ? n->step * 127 / st : i == 1 ? (degree_of(n->note) - 14) * 127 / 35 : n->len * 8);
        }
        return;
    }
    fmt_int(v, ui.roll_cur[ui.synth] + 1);
    field_show(0, "Cursor", v, ui.roll_cur[ui.synth] * 127 / st);
    v[0] = 0;
    note_name(v, note_of(ui.roll_view[ui.synth]));
    field_show(1, "View", v, (ui.roll_view[ui.synth] - 14) * 127 / 29);
    fmt_int(v, ui.roll_pen[ui.synth]);
    field_show(2, "Length", v, ui.roll_pen[ui.synth] * 8);
    fmt_int(v, q->steps);
    field_show(3, "Steps", v, q->steps * 2);
}

static void fields_band(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], k;
    if (ui.page == PG_ROLL) { roll_fields(); return; }
    for (k = 0; k < 4; k++) field(k, pg->k[r][k], ui.page, r);
}

static void foot(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page];
    if (ui.page == PG_ROLL) {                           /* the zoom, or a held note's velocity */
        char v[16];
        int h = roll_held(), j = h >= 0 ? seq_find(ui.synth, ui.key_step[h], ui.key_note[h]) : -1;
        int x = text(PAD + 1, Y_FOOT + 2, j >= 0 ? "Velocity" : "Zoom", &FS, C_LABEL);
        fmt_int(v, j >= 0 ? sq[ui.synth].n[j].vel : roll_z());
        text_fit(x + 6, Y_FOOT + 2, 200 - x, v, &FS, lit(4) ? HOLO : C_INK);
        return;
    }
    if (pg->algo[r][0]) {                               /* the ALGORITHM encoder's value, under the cards */
        char v[16];
        int x = text(PAD + 1, Y_FOOT + 2, pg->algo[r], &FS, C_LABEL);
        val_text(v, ui.page, r, 4);
        text_fit(x + 6, Y_FOOT + 2, 200 - x, v, &FS, lit(4) ? HOLO : C_INK);
    }
}

/* ---- the stages: cards */
#define SX M                                            /* the stage: x, y, width, height */
#define SY Y_STAGE
#define SW (DMK_W - 2 * M)
#define SH (Y_FOOT - Y_STAGE - 4)

static void stage_home(void)                            /* the synths: a card each, its initial lit while it sounds */
{
    int i;
    for (i = 0; i < DMK_SYNTHS; i++) {
        int x = cell_x(SX, SW, DMK_SYNTHS, i), w = cell_w(SX, SW, DMK_SYNTHS, i), cx = x + w / 2;
        int sounding = syn_peak[i] > 200 || i == ui.synth;   /* the one played lit, and any that sound */
        char a[16], *b;
        str_cat(a, DMK_SAMPLES[VS(i, PG_SOUND, 0, 0)].name);
        for (b = a; *b && *b != ' '; b++) {}
        if (*b) *b++ = 0;                               /* "Snow pad": what, which */
        if (i == ui.synth) card(x, SY, w, SH, C_INK, 2);
        else card(x, SY, w, SH, C_LINE, 1);
        card_title(x, SY, w, a, "", !sounding);
        {                                               /* its mark: the sound's initial in Kiloy, lit while it sounds */
            char ini[2] = {(char)(a[0] | 0x20), 0};
            text_c(cx, SY + 64 - FK.ascent + FK.cap / 2, ini, &FK, sounding ? HOLO : C_LINE);
        }
        text_fit(x + PAD + 1, SY + SH - 20, w - 2 * PAD, b, &FS, C_LABEL);   /* "pad", "piano": which */
    }
}

static void stage_patterns(void)                        /* HOME held: the song's patterns, a card each */
{
    int i;
    fill(SX, SY, SW, SH, C_PAPER);
    ta_cover(0, SY, DMK_W, Y_FOOT - SY);
    for (i = 0; i < 8; i++) {
        char n[2] = {(char)('1' + i), 0};
        int x = cell_x(SX, SW, 4, i % 4), w = cell_w(SX, SW, 4, i % 4), h = (SH - G) / 2, y = SY + (i / 4) * (h + G);
        int ty = y + (h - 20) / 2 - 1;
        if (i == 0) {                                   /* playing: a lit card, its number dark on the core */
            round_fill(x, y, w, h, R_CARD, HOLO);
            text_c(x + w / 2, ty, n, &FM, THEME_DARK[theme] ? C_PAPER : C_INK);
        } else if (i == 4) { card(x, y, w, h, HOLO, 2); text_c(x + w / 2, ty, n, &FM, HOLO); }   /* next */
        else { card(x, y, w, h, C_LINE, 1); text_c(x + w / 2, ty, n, &FM, C_LABEL); }
    }
}

static void stage_roll(void)
{
    const seq_t *q = &sq[ui.synth];
    int z = roll_z(), cw = 192 / z, gx = SX + 30, gy = SY + 7, rh = 15, at = roll_at(), x0 = at / z * z;
    int view = ui.roll_view[ui.synth], end = q->steps - x0, i, k;
    if (end > z) end = z;
    card(SX, SY, SW, SH, C_LINE, 1);
    for (k = 0; k <= end; k += 4) vline(gx + k * cw, gy, rh * 7, C_LINE);   /* the beats: rules; the steps: dots */
    if (cw >= 6) for (i = 0; i <= 7; i++) for (k = 0; k <= end; k++) if (k % 4) px_put(gx + k * cw, gy + i * rh, C_LINE);
    if (end < z) vline(gx + end * cw, gy - 3, rh * 7 + 6, C_INK);          /* where this synth's pattern ends */
    for (i = 0; i < 7; i++) {
        char nm[8];
        int d = view + 6 - i;
        nm[0] = 0;
        note_name(nm, note_of(d));
        text(SX + PAD + 1, gy + i * rh + 2, nm, &FS, d % 7 == 0 ? C_INK : C_LABEL);
    }
    for (i = 0; i < q->count; i++) {                    /* the notes; those at the cursor lit */
        const snote_t *n = &q->n[i];
        int row = 6 - (degree_of(n->note) - view), s0 = n->step - x0, len = n->len, w;
        if (row < 0 || row > 6 || s0 < 0 || s0 >= end) continue;
        if (s0 + len > end) len = end - s0;
        w = len * cw - 2 < 2 ? 2 : len * cw - 2;
        round_fill(gx + s0 * cw + 1, gy + row * rh + 3, w, rh - 6, (rh - 6) / 2, !seq_on && n->step == at ? HOLO : C_INK);
    }
    round_fill(gx + (at - x0) * cw - 2, gy - 3, 4, rh * 7 + 6, 2, HOLO);   /* the cursor, or the playhead */
}

/* a graphic's body, lit: the band between ya[] and yb[] as one shape, the thermal light from its edges in; the ink
 * line goes over it */
static void lit_band(int x0, int w, const int16_t *ya, const int16_t *yb)
{
    int i;
    for (i = 0; i < w; i++) if (yb[i] > ya[i]) vline(x0 + i, ya[i], yb[i] - ya[i], HOLO);
    holo_done();
}

static void ink_line(int x0, int w, const int16_t *ys)
{
    int i, yp = ys[0];
    for (i = 0; i < w; i++) {
        int y = ys[i];
        vline(x0 + i, y < yp ? y : yp, (y < yp ? yp - y : y - yp) + 2, C_INK);
        yp = y;
    }
}

static void stage_sound(void)
{
    int16_t ya[DMK_W], yb[DMK_W];
    const dmk_sample_t *sm = &DMK_SAMPLES[*vp(PG_SOUND, 0, 0) < DMK_SAMPLES_N ? *vp(PG_SOUND, 0, 0) : 0];
    int i, mid = SY + 78, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, n = sm->blocks;
    card(SX, SY, SW, SH, C_LINE, 1);
    if (ui.row[PG_SOUND]) card_title(SX, SY, SW, "No layer", "Layer", 0);
    else {                                              /* the sound's name, and its mark in the corner, lit */
        char ini[2] = {(char)(sm->name[0] | 0x20), 0};
        card_title(SX, SY, SW, sm->name, sm->loops ? "Sample, looped" : "Sample, one-shot", 0);
        text_r(SX + SW - PAD - 2, SY + 8, ini, &FK, HOLO);
    }
    for (i = 0; i < w; i++) {                           /* the sample's own shape: its body lit, its edge in ink */
        int h = sm->env[i * 48 / w] * 26 / 255;
        if (h < 1) h = 1;
        ya[i] = (int16_t)(mid - h); yb[i] = (int16_t)(mid + h + 1);
    }
    lit_band(x0, w, ya, yb);
    for (i = 0; i < w; i++) { px_put(x0 + i, ya[i], C_INK); px_put(x0 + i, yb[i] - 1, C_INK); }
    if (sm->loops && *vp(PG_SOUND, 0, 2) >= 64)         /* the loop's ends, as tall as the sample is there */
        for (i = 0; i < 2; i++) {
            int lx = (i ? sm->loop1 : sm->loop0) * (w - 1) / n, lh = mid - ya[lx] + 6;
            round_fill(x0 + lx - 2, mid - lh, 4, 2 * lh, 2, C_INK);
        }
    if (syn[ui.synth].start) {                          /* where it starts */
        int sx = syn[ui.synth].start * (w - 1) / n;
        vline(x0 + sx, mid - 30, 61, C_INK);
    }
}

static void stage_curve(int amp)
{
    int16_t ys[DMK_W], yb[DMK_W];
    int i, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, base = SY + SH - 14, top = SY + 14;
    card(SX, SY, SW, SH, C_LINE, 1);
    for (i = 0; i < w; i++) yb[i] = (int16_t)(base + 1);
    if (!amp) {                                         /* the response: its body lit under the curve */
        int fc = x0 + *vp(PG_FILTER, 0, 0) * w / 127, q = *vp(PG_FILTER, 0, 1) / 4, ly = top + 40;
        for (i = 0; i < w; i++) {
            int x = x0 + i, y;
            if (x < fc - 12) y = ly;
            else if (x < fc) y = ly - q * (x - fc + 12) / 12;
            else y = ly - q + (x - fc) * 3;
            if (y > base) y = base;
            if (y < top) y = top;
            ys[i] = (int16_t)y;
        }
        lit_band(x0, w, ys, yb);
        ink_line(x0, w, ys);
        round_fill(fc - 4, base - 4, 9, 9, 4, lit(0) ? HOLO : C_INK);
    } else {                                            /* the envelope: its body lit under the line */
        int a = *vp(PG_AMP, 0, 0) / 3 + 4, d = *vp(PG_AMP, 0, 1) / 3 + 4, s = *vp(PG_AMP, 0, 2);
        int r = *vp(PG_AMP, 0, 3) / 3 + 4, hold = w - a - d - r, sy = base - (s * (base - top)) / 127;
        int pts[5][2] = {{x0, base}, {x0 + a, top}, {x0 + a + d, sy}, {x0 + a + d + hold, sy}, {x0 + w - 1, base}};
        for (i = 0; i < 4; i++) {
            int k, n = pts[i + 1][0] - pts[i][0];
            for (k = 0; k <= n && pts[i][0] + k - x0 < w; k++)
                ys[pts[i][0] + k - x0] = (int16_t)(pts[i][1] + (n ? (pts[i + 1][1] - pts[i][1]) * k / n : 0));
        }
        lit_band(x0, w, ys, yb);
        ink_line(x0, w, ys);
        for (i = 1; i < 4; i++) round_fill(pts[i][0] - 4, pts[i][1] - 4, 9, 9, 4, lit(i - 1) ? HOLO : C_INK);
    }
}

/* four rows, a pill each: its name, its switch (off), the open one framed in ink */
static void stage_rows(int page, const char *const *names, int n, int open)
{
    int i, h = (SH - (n - 1) * G) / n;
    for (i = 0; i < n; i++) {
        int y = SY + i * (h + G), tx = SX + SW - PAD - 22;
        if (i == open) round_line(SX, y, SW, h, h / 2, 2, C_INK);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 20) / 2 - 1, names[i], &FM, row_soon(page, i) ? C_LABEL : C_INK);
        if (row_soon(page, i)) { text_r(SX + SW - PAD - 4, y + (h - 12) / 2, "Soon", &FS, C_LABEL); continue; }
        if (page != PG_FX && i >= 2) continue;              /* the setup is not an effect: no switch */
        if (row_off(page, i)) {                             /* the switch: off */
            round_line(tx, y + h / 2 - 6, 22, 12, 6, 1, C_LABEL);
            round_fill(tx + 3, y + h / 2 - 3, 6, 6, 3, C_LABEL);
        } else {                                            /* on: lit, the knob to the right */
            round_fill(tx, y + h / 2 - 6, 22, 12, 6, HOLO);
            round_fill(tx + 13, y + h / 2 - 3, 6, 6, 3, C_PAPER);
        }
    }
}

static void stage_chord(void)
{
    static const char *const CH[4] = {"i add9", "VI maj7", "III maj7", "VII sus2"};   /* A minor */
    int i, y = SY + SH - 30;
    card(SX, SY, SW, SH, C_LINE, 1);
    card_title(SX, SY, SW, "A minor", "Key", 0);
    text_c(DMK_W / 2, SY + 40, "Am(add9)", &FN, HOLO);
    for (i = 0; i < 4; i++) {                           /* the progression: now, a pill */
        int x = cell_x(SX + 4, SW - 8, 4, i), w = cell_w(SX + 4, SW - 8, 4, i), tw = text_w(CH[i], &FS);
        if (i == 0) round_fill(x, y, w, 24, 12, C_INK);
        if (tw <= w - 8) text_c(x + w / 2, y + 6, CH[i], &FS, i == 0 ? C_PAPER : C_LABEL);
        else text_fit(x + 4, y + 6, w - 8, CH[i], &FS, i == 0 ? C_PAPER : C_LABEL);
    }
}

static void stage_songs(void)
{
    static const char *const SONG[3] = {"First snow", "Embers", "Night drive"};
    int i, h = (SH - 3 * G) / 4;
    for (i = 0; i < 4; i++) {
        int y = SY + i * (h + G);
        char n[3] = {'0', (char)('1' + i), 0};
        round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 12) / 2, n, &FS, C_LABEL);
        text(SX + 40, y + (h - 20) / 2 - 1, i < 3 ? SONG[i] : "Empty", &FM, i < 3 ? C_INK : C_LABEL);
    }
}

/* the boot, as the site's splash: on the dark ground the word stacks in Kiloy, "dem" over "ake"; its bits arrive
 * through the dither, sweeping in, and heat from the blue rim to the pale core; it holds while the bits step and a
 * meter of ticks fills; then the screen leaves in 16 px blocks by the same dither, Home under it. A key or a button
 * ends it */
#define BOOT_ARRIVE 650u
#define BOOT_HEAT0 150u
#define BOOT_HEAT1 1150u
#define BOOT_LEAVE 1750u
#define BOOT_END 2300u
static uint8_t boot_fb[DMK_FB_BYTES] POOL;             /* the held frame, while Home comes through it */

static void boot(uint32_t t)
{
    int dark = THEME_DARK[theme], gnd = dark ? C_PAPER : C_INK, lit = dark ? C_INK : C_PAPER;
    int i, at, y1 = 50, y2 = y1 + FK.ascent + 12;
    fill(0, 0, DMK_W, DMK_H, gnd);
    holo_veil = t >= BOOT_ARRIVE ? 256 : (int)(t * 256u / BOOT_ARRIVE);
    holo_heat = t <= BOOT_HEAT0 ? 0 : t >= BOOT_HEAT1 ? 255 : (int)((t - BOOT_HEAT0) * 112u / (BOOT_HEAT1 - BOOT_HEAT0));
    holo_bg = gnd;
    text_c(DMK_W / 2, y1, "dem", &FK, HOLO);
    text_c(DMK_W / 2, y2, "ake", &FK, HOLO);
    holo_veil = 256;
    holo_heat = 255;
    at = (int)((t < BOOT_LEAVE ? t : BOOT_LEAVE) * 40u / BOOT_LEAVE);
    for (i = 0; i <= 40; i++) {                         /* the meter: ticks lit up to where it has reached */
        int x = 59 + i * 3;
        if (i == at) round_fill(x - 1, 176, 3, 16, 1, HOLO);
        else fill(x, 180, 1, 8, i < at ? lit : C_LABEL);
    }
}

static void boot_leave(uint32_t ms)                     /* the held frame's blocks not yet gone, over Home */
{
    int q = (int)((ms - BOOT_LEAVE) * 16u / (BOOT_END - BOOT_LEAVE)), bx, by, r, k;
    for (by = 0; by < DMK_H / 16; by++)
        for (bx = 0; bx < DMK_W / 16; bx++)
            if (T_BAYER6[0][(by & 3) * 4 + (bx & 3)] >= q)
                for (r = 0; r < 16; r++) {
                    int o = ((by * 16 + r) * DMK_W + bx * 16) >> 1;
                    for (k = 0; k < 8; k++) fb[o + k] = boot_fb[o + k];
                }
}

/* ---- the overlays: a card in ink, over the rest */
static void box(int x, int y, int w, int h)
{
    ta_cover(x, y, w, h);
    round_fill(x, y, w, h, h / 2, C_PAPER);
    round_line(x, y, w, h, h / 2, 1, C_INK);
}

static void overlays(void)
{
    int b;
    if (ui.note[0] && ui.now - ui.note_ms < 1500u) {
        int w = 160, h = 30, x = (DMK_W - w) / 2, y = Y_FOOT - h - 10;
        box(x, y, w, h);
        text_c(x + w / 2, y + 9, ui.note, &FS, C_INK);
    }
    for (b = 0; b < B_COUNT; b++)
        if (ui.btn_held >> b & 1u && !(ui.btn_used >> b & 1u) && ui.now - ui.btn_down_at[b] > 350u && LAYER_NAME[b][0]) {
            char t[40];
            int w, x;
            str_cat(str_cat(str_cat(t, BTN_NAME[b]), ": "), LAYER_NAME[b]);
            w = text_w(t, &FS) + 2 * PAD + 8;
            x = (DMK_W - w) / 2;
            box(x, Y_FOOT - 40, w, 30);
            text(x + PAD + 4, Y_FOOT - 31, t, &FS, C_INK);
            break;
        }
}

static const uint8_t *ui_frame(uint32_t ms)
{
    {                                                   /* the bits: a new arrangement every 140 ms, at random */
        uint32_t k = ms / 140u, h = k * 2654435761u;
        int p = (int)((h >> 16) % 5u);
        holo_pat = (uint8_t)(p >= holo_pat ? p + 1 : p);   /* never the same twice running */
        if (holo_pat > 5) holo_pat = 0;
    }
    ui.now = ms;
    clip_all();
#ifdef DMK_TEXT_AUDIT
    ta_reset();
#endif
    if (!ui.boot_done) {
        if (ms < BOOT_LEAVE) { boot(ms); return fb; }
        if (ms >= BOOT_END) ui.boot_done = 1;
        else if (!ui.boot_kept) {
            int i;
            boot(BOOT_LEAVE);
            for (i = 0; i < DMK_FB_BYTES; i++) boot_fb[i] = fb[i];
            ui.boot_kept = 1;
        }
    }
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    head();
    fields_band();
    if (ui.btn_held >> B_HOME & 1u && ui.now - ui.btn_down_at[B_HOME] > 350u) stage_patterns();
    else switch (ui.page) {
    case PG_HOME: stage_home(); break;
    case PG_ROLL: stage_roll(); break;
    case PG_SOUND: stage_sound(); break;
    case PG_FILTER: stage_curve(0); break;
    case PG_AMP: stage_curve(1); break;
    case PG_FX: stage_rows(PG_FX, PAGE[PG_FX].row, 4, ui.row[PG_FX]); break;
    case PG_CHORD: stage_chord(); break;
    case PG_GLOBAL: stage_rows(PG_GLOBAL, PAGE[PG_GLOBAL].row, 4, ui.row[PG_GLOBAL]); break;
    case PG_LFO: stage_curve(1); break;
    default: stage_songs(); break;
    }
    foot();
    overlays();
    if (!ui.boot_done) boot_leave(ms);
    return fb;
}

/* the update (dmk_update_frame; the FM-1 shows it while it takes a new firmware): in the boot's light, the word lit,
 * what happens now, the five steps as dots, the done ones lit. Its bits step a frame at a time */
static const uint8_t *ui_update(uint32_t step, int32_t code)
{
    static const char *const SAY[6] = {"", "Reading the package", "Checking it", "Staging the loader",
                                       "Waiting for the computer", "Restarting to write it"};
    static uint8_t pat;
    int dark = THEME_DARK[theme], gnd = dark ? C_PAPER : C_INK, lit = dark ? C_INK : C_PAPER, i;
    int y1 = 30, y2 = y1 + FK.ascent + 12;
    clip_all();
#ifdef DMK_TEXT_AUDIT
    ta_reset();
#endif
    holo_pat = (uint8_t)(pat++ % 6u);
    fill(0, 0, DMK_W, DMK_H, gnd);
    text_c(DMK_W / 2, y1, "dem", &FK, HOLO);
    text_c(DMK_W / 2, y2, "ake", &FK, HOLO);
    if (step < 9u) {
        text_c(DMK_W / 2, 142, step >= 5u ? "Writing Demake" : "Installing", &FM, lit);
        text_c(DMK_W / 2, 168, step >= 5u ? "About a minute: leave the cable in" : SAY[step < 6u ? step : 0], &FS, C_LABEL);
        for (i = 0; i < 5; i++) {                       /* the steps: done lit, now a ring, to come grey */
            int x = DMK_W / 2 - 36 + i * 16;
            if (i + 1 < (int)step) round_fill(x, 202, 9, 9, 4, HOLO);
            else if (i + 1 == (int)step) round_line(x, 202, 9, 9, 4, 2, lit);
            else round_line(x, 202, 9, 9, 4, 1, C_LABEL);
        }
    } else {
        text_c(DMK_W / 2, 142, code == 1 ? "Checked" : "Stopped", &FM, lit);
        text_c(DMK_W / 2, 168, code == 1 ? "A trial run: nothing written" : "The firmware is as it was", &FS, C_LABEL);
    }
    return fb;
}

/* ---- the controls */

/* the roll's knobs (docs/plan.md, 3.5): a held note's MOVE, PITCH, LENGTH, VELOCITY; else CURSOR, VIEW, the pen's
 * LENGTH, the synth's STEPS, ZOOM */
static void roll_turn(int k, int d)
{
    int s = ui.synth, h = roll_held(), st = sq[s].steps, dir = d > 0 ? 1 : -1;
    if (h >= 0) {
        int j = seq_find(s, ui.key_step[h], ui.key_note[h]);
        snote_t *n;
        if (j < 0) return;
        n = &sq[s].n[j];
        if (k == 0) {
            int t = (n->step + dir + st) % st;
            if (seq_find(s, t, n->note) >= 0) return;
            n->step = (uint8_t)t; ui.key_step[h] = (uint8_t)t; ui.roll_cur[s] = (uint8_t)t;
        }
        if (k == 1) {
            int p = note_of(degree_of(n->note) + dir);
            if (p < 24 || p > 108 || seq_find(s, n->step, p) >= 0) return;
            note_off_s(s, n->note);
            n->note = (uint8_t)p; ui.key_note[h] = (uint8_t)p;
            note_on_s(s, p);
        }
        if (k == 2) { int l = n->len + dir; n->len = (uint8_t)(l < 1 ? 1 : l > SEQ_STEPS ? SEQ_STEPS : l); }
        if (k == 4) { int v = n->vel + dir * 4; n->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); }
        return;
    }
    if (k == 0) ui.roll_cur[s] = (uint8_t)((ui.roll_cur[s] + dir + st) % st);
    if (k == 1) { int v = ui.roll_view[s] + dir; ui.roll_view[s] = (uint8_t)(v < 14 ? 14 : v > 43 ? 43 : v); }
    if (k == 2) { int p = ui.roll_pen[s] + dir; ui.roll_pen[s] = (uint8_t)(p < 1 ? 1 : p > 16 ? 16 : p); }
    if (k == 3) {                                       /* this synth's length: the three loop against each other */
        int t = st + dir;
        t = t < 1 ? 1 : t > SEQ_STEPS ? SEQ_STEPS : t;
        sq[s].steps = (uint8_t)t;
        if (ui.roll_cur[s] >= t) ui.roll_cur[s] = (uint8_t)(t - 1);
        if (sq[s].pos >= t) sq[s].pos = 0;
    }
    if (k == 4) { int z = ui.roll_zoom[s] + dir; ui.roll_zoom[s] = (uint8_t)(z < 0 ? 0 : z > 2 ? 2 : z); }
}

static void roll_oct(int dup)                           /* OCT- deletes the held notes; OCT+ copies each after itself */
{
    int s = ui.synth, k;
    for (k = 0; k < DMK_KEYS; k++) {
        int j;
        if (!ui.keys_down[k] || ui.key_roll[k] != 1) continue;
        j = seq_find(s, ui.key_step[k], ui.key_note[k]);
        if (j < 0) continue;
        if (!dup) { seq_del(s, j); ui.key_roll[k] = 3; continue; }
        {
            snote_t n = sq[s].n[j];
            int t = (n.step + n.len) % sq[s].steps;
            if (seq_add(s, t, n.len, n.note, n.vel) >= 0) { ui.key_step[k] = (uint8_t)t; ui.roll_cur[s] = (uint8_t)t; }
        }
    }
}

/* ---- the controls */
static void ui_button(int b, int down)
{
    uint16_t bit = (uint16_t)(1u << b);
    if (b < 0 || b >= B_COUNT) return;
    ui.boot_done = 1;
    if (down) {
        int o;
        ui.btn_held |= bit;
        ui.btn_down_at[b] = ui.now;
        ui.btn_used &= (uint16_t)~bit;
        for (o = 0; o < B_COUNT; o++)                   /* another button held: it was used as a modifier */
            if (o != b && ui.btn_held >> o & 1u) ui.btn_used |= (uint16_t)(1u << o);
        if ((b == B_OCTD || b == B_OCTU) && ui.page == PG_ROLL && roll_held() >= 0) {
            roll_oct(b == B_OCTU);                      /* a held note: OCT- takes it away, OCT+ adds a copy */
            ui.btn_used |= bit;
            return;
        }
        if (b == B_OCTD || b == B_OCTU) {
            char t[16];
            if (b == B_OCTD && ui.octave > -2) ui.octave--;
            if (b == B_OCTU && ui.octave < 2) ui.octave++;
            fmt_int(str_cat(t, "Octave "), ui.octave);
            notice(t);
        }
        return;
    }
    ui.btn_held &= (uint16_t)~bit;
    if (ui.btn_used & bit) return;                      /* a modifier that was used does nothing on release */
    if (ui.now - ui.btn_down_at[b] > 1000u && b == B_SAVE) { ui.page = PG_DISCS; return; }
    if (ui.now - ui.btn_down_at[b] > 350u) return;      /* a hold showed its layer: no tap */
    if (b == B_SAVE) { notice("Nothing to save yet"); return; }
    if (b == B_PLAY) { seq_play(!seq_on); return; }
    if (b == B_REC) { seq_rec ^= 1u; notice(seq_rec ? "Recording on" : "Recording off"); return; }
    if (BTN_PAGE[b] >= 0) {
        int p = BTN_PAGE[b];
        if (ui.page == p) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);   /* again: the next row built */
        else {
            ui.page = (uint8_t)p;
            if (row_soon(p, ui.row[p])) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);
            if (rows_built(p) > 1 && !(ui.hinted >> p & 1u)) {   /* the first time: how to reach the next row */
                char t[32];
                str_cat(str_cat(t, BTN_NAME[b]), " again: the next row");
                notice(t);
                ui.hinted |= (uint16_t)(1u << p);
            }
        }
        ui.touch_k = -1;
    }
}

static void ui_encoder(int e, int d)
{
    int r = ui.row[ui.page], k;
    uint8_t *v;
    ui.boot_done = 1;
    if (ui.btn_held) {                                  /* a held button's layer: it was used */
        int b;
        for (b = 0; b < B_COUNT; b++) if (ui.btn_held >> b & 1u) ui.btn_used |= (uint16_t)(1u << b);
    }
    if (e == E_SELECT) { int s = ui.synth + (d > 0 ? 1 : -1); ui.synth = (uint8_t)(s < 0 ? 0 : s >= DMK_SYNTHS ? DMK_SYNTHS - 1 : s); sp_sync(); return; }
    if (e == E_PRESET) {                                /* the synth's sound: the next recording */
        uint8_t *sv = &ui.val[ui.synth][PG_SOUND][0][0];
        int n = *sv + (d > 0 ? 1 : -1);
        *sv = (uint8_t)(n < 0 ? DMK_SAMPLES_N - 1 : n >= DMK_SAMPLES_N ? 0 : n);
        notice(DMK_SAMPLES[*sv].name);
        sp_sync();
        return;
    }
    k = e == E_ALGO ? 4 : e >= E_K1 && e <= E_K4 ? e - E_K1 : -1;
    if (k < 0) return;
#ifdef DMK_BENCH
    if (k == 0) { bench_level += d * 10; if (bench_level < 0) bench_level = 0; if (bench_level > 1000) bench_level = 1000; }
#endif
    ui.touch_k = (int8_t)k;
    ui.touch_ms = ui.now;
    if (ui.page == PG_ROLL) { roll_turn(k, d); return; }
    if (ui.page == PG_GLOBAL && r == 3 && k == 0) {    /* theme */
        int t = theme + (d > 0 ? 1 : -1);
        theme = (uint8_t)(t < 0 ? DMK_THEMES - 1 : t >= DMK_THEMES ? 0 : t);
        return;
    }
    v = vp(ui.page, r, k);
    if (ui.page == PG_SOUND && r == 0 && k == 0) {      /* the sample: a step a detent */
        int n = *v + (d > 0 ? 1 : -1);
        *v = (uint8_t)(n < 0 ? 0 : n >= DMK_SAMPLES_N ? DMK_SAMPLES_N - 1 : n);
        sp_sync();
        return;
    }
    {
        const choice_t *c = choice_of(ui.page, r, k);
        int n = c && c->n > 1 ? (choice_index(c, *v) + (d > 0 ? 1 : -1)) * 128 / c->n + 128 / c->n / 2 : *v + d;
        if (c && c->n > 1 && (n < 0 || n > 127)) return;   /* a choice stops at its ends */
        *v = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    }
    sp_sync();
}

/* a key: on the roll, stopped, it writes at the cursor (or holds the note of its pitch there); playing with REC, it
 * is recorded from its nearest step for its length; else it plays. With SNAP the roll's notes are on the scale */
static void ui_key(int k, int down)
{
    int n, s;
    if (k < 0 || k >= DMK_KEYS) return;
    ui.boot_done = 1;
    n = 53 + k + ui.octave * 12;                        /* F3 = 53, chromatic */
    ui.keys_down[k] = (uint8_t)down;
    if (down) {
        s = ui.synth;
        ui.key_sy[k] = (uint8_t)s;
        ui.key_roll[k] = 0;
        if (ui.page == PG_ROLL && !seq_on) {
            int c = ui.roll_cur[s];
            n = snap(n);
            if (seq_find(s, c, n) >= 0 || seq_add(s, c, ui.roll_pen[s], n, 100) >= 0) { ui.key_roll[k] = 1; ui.key_step[k] = (uint8_t)c; }
        } else if (seq_on && seq_rec) {
            n = snap(n);
            ui.key_roll[k] = 2;
            ui.key_t0[k] = seq_now();
        }
        ui.key_note[k] = (uint8_t)n;
        note_on_s(s, n);
        return;
    }
    s = ui.key_sy[k];
    note_off_s(s, ui.key_note[k]);                      /* the synth and the note it started */
    if (ui.key_roll[k] == 1) {                          /* step entry: the cursor to the note's end */
        int j = seq_find(s, ui.key_step[k], ui.key_note[k]);
        if (j >= 0) ui.roll_cur[s] = (uint8_t)((sq[s].n[j].step + sq[s].n[j].len) % sq[s].steps);
    } else if (ui.key_roll[k] == 2 && seq_on) {
        uint32_t t0 = ui.key_t0[k], t1 = seq_now();
        int st = (int)(((t0 + 128u) >> 8) % sq[s].steps), len = (int)((t1 - t0 + 128u) >> 8);
        seq_add(s, st, len < 1 ? 1 : len > SEQ_STEPS ? SEQ_STEPS : len, ui.key_note[k], 100);
    }
    ui.key_roll[k] = 0;
}

static const uint8_t *ui_leds(void)
{
    int i;
    for (i = 0; i < DMK_KEYS; i++) {                    /* on the roll, the keys of the notes at the cursor blink */
        int n = 53 + i + ui.octave * 12;
        leds[i] = ui.keys_down[i] ? 255 : 0;
        if (!ui.keys_down[i] && ui.page == PG_ROLL && !seq_on && seq_find(ui.synth, ui.roll_cur[ui.synth], n) >= 0)
            leds[i] = (ui.now / 250u) & 1u ? 255 : 0;
    }
    for (i = 0; i < B_COUNT; i++) leds[DMK_KEYS + i] = BTN_PAGE[i] == ui.page ? 255 : 0;
    leds[DMK_KEYS + B_PLAY] = seq_on ? 255 : 0;
    leds[DMK_KEYS + B_REC] = seq_rec && (seq_on || (ui.now / 250u) & 1u) ? 255 : 0;   /* armed: it blinks */
    leds[DMK_KEYS + B_OCTD] = ui.octave < 0 ? 255 : 0;
    leds[DMK_KEYS + B_OCTU] = ui.octave > 0 ? 255 : 0;
    return leds;
}
