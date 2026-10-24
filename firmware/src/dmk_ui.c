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

enum { PG_HOME, PG_ROLL, PG_SOUND, PG_FILTER, PG_AMP, PG_FX, PG_CHORD, PG_GLOBAL, PG_LFO, PG_DISCS, PG_ARR, PG_COUNT };
#define PG_SAVED PG_ARR                                 /* the pages a song saves the values of (Arrange keeps its own) */

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
    {"Sound", 2, {"Main", "Layer"}, {"Fine", "Sound"},
     {{"Sound", "Start", "Loop", "Tune"}, {"Blend", "Tune", "Loop", "Tone"}}},
    {"Filter", 1, {"Filter"}, {"Type"}, {{"Cutoff", "Reso", "Env", "Drift"}}},
    {"Amp", 1, {"Amp"}, {"Level"}, {{"Attack", "Decay", "Sustain", "Release"}}},
    {"FX", 4, {"Colour", "Media", "Motion", "Space"}, {"Effect", "Effect", "Effect", ""},
     {{"Amount", "Tone", "Time", "Spice"}, {"Amount", "Tone", "Time", "Spice"},   /* an effect names its own */
      {"Amount", "Tone", "Time", "Spice"}, {"Verb", "Echo", "Pan", "Width"}}},
    {"Chord", 3, {"Voice", "Key", "Keys"}, {"Prog", "Prog", "Prog"},
     {{"Voicing", "Invert", "Spread", "Strum"}, {"Key", "Mode", "Change", "Rhythm"}, {"Keys", "Type", "", ""}}},
    {"Global", 4, {"Verb", "Echo", "Song", "Theme"}, {"Type", "Type", "", ""},
     {{"Return", "Tone", "Size", "Mod"}, {"Return", "Tone", "Time", "Regen"},
      {"Swing", "Switch", "Click", "Rec"}, {"Theme", "", "", ""}}},
    {"LFO", 1, {"LFO"}, {"Target"}, {{"Rate", "Depth", "Shape", "Fade"}}},
    {"Songs", 1, {"Songs"}, {""}, {{"Slot", "", "", ""}}},
    {"Song", 2, {"Scene", "Steps"}, {"", ""}, {{"Synth 1", "Synth 2", "Synth 3", "Chords"}, {"Step", "Scene", "Bars", "End"}}},
};

/* the page each button opens (-1: none) */
static const int8_t BTN_PAGE[B_COUNT] = {PG_FX, PG_FILTER, PG_AMP, PG_LFO, PG_SOUND, PG_GLOBAL, PG_HOME, -1, PG_CHORD,
                                         PG_ROLL, -1, -1, -1, -1};
static const char *const BTN_NAME[B_COUNT] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                              "PLAY", "REC", "OCT−", "OCT+"};
static const char *const LAYER_NAME[B_COUNT] = {"ALGORITHM, the effect", "help", "help", "help", "the sound", "ALGORITHM, the type",
                                                "", "songs", "the progression", "K2 the harmony, OCT\xE2\x88\x92 clears", "", "", "", ""};

/* a choice field: its names (none: a number 0..127) */
typedef struct { uint8_t page, row, k, n; const char *const *name; } choice_t;
static const char *const FILTERS[] = {"LP24", "LP12", "HP", "BP", "Notch", "Peak", "Vowel", "Off"};
static const char *const COLOURS[] = {"Off", "Saturate", "Crush", "Lossy", "Noise"};
static const char *const MEDIAS[] = {"Off", "Tape", "Cassette", "VHS"};
static const char *const MOTIONS[] = {"Off", "Chorus", "Vibrato", "Drift"};
/* each effect's four knobs, by row and type (the off ones: the row's own) */
static const char *const FXK[3][5][4] = {
    {{"Amount", "Tone", "Time", "Spice"}, {"Drive", "Tone", "Bias", "Mix"}, {"Bits", "Tone", "Rate", "Jitter"},
     {"Loss", "Band", "Frame", "Holes"}, {"Hiss", "Tone", "Crackle", "Hum"}},
    {{"Amount", "Tone", "Time", "Spice"}, {"Amount", "Tone", "Wow", "Flutter"}, {"Warble", "Tone", "Rate", "Dropout"},
     {"Wobble", "Band", "Rate", "Track"}},
    {{"Amount", "Tone", "Time", "Spice"}, {"Depth", "Tone", "Rate", "Mix"}, {"Depth", "", "Rate", "Fade"},
     {"Depth", "Detune", "Rate", ""}}};
static const char *const ZOOMS[] = {"16", "32", "64"};
static const char *const OFF[] = {"Off"};
static const char *const LFOS[] = {"Sine", "Triangle", "Ramp", "Square", "Random"};
static const char *const VOICINGS[] = {"Close", "Open", "Wide", "Shell", "Quartal"};
static const char *const INVERTS[] = {"Auto", "Root", "1st", "2nd", "3rd"};
static const char *const KEYS[] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G", "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
static const char *const MODENAMES[] = {"Aeolian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Ionian"};
static const char *const CHANGES[] = {"\xC3\x97\xC2\xBD", ("\xC3\x97" "1"), ("\xC3\x97" "2"), ("\xC3\x97" "4")};
static const char *const RHYTHMS[] = {"Hold", "Pulse 4", "Pulse 8", "Arp up", "Arp down"};
static const char *const KEYMODES[] = {"Chord", "All"};   /* the keys play chords on CHORD, or on every page but the roll */
static const char *const KTYPES[] = {"Sevenths", "Triads", "Ninths", "Add9", "Sus2", "Sus4", "6/9", "Power"};
static const char *const FOLLOWS[] = {"Free", "Follows the key", "Follows the chord", "Follows the root", "Plays chords",
                                      "Harmony of 1", "Harmony of 2", "Harmony of 3"};
static const char *const PROGS[20] = {"First snow", "Last train", "Fogbound", "Overpass", "Vending light", "Blue hour",
    "Power lines", "Tower beacon", "Slow thaw", "Candle", "Embers", "Low clouds", "Snowfield", "Static", "Mixtape", "Night drive",
    "User 1", "User 2", "User 3", "User 4"};
static uint8_t choice_val(int i, int n) { return (uint8_t)(i * 128 / n + 64 / n); }   /* a choice's place, its middle */
static const char *const TARGETS[] = {"Pitch", "Cutoff", "Level", "Pan"};
static const char *const LOOPS[] = {"Off", "Full"};
static const char *const VERBS[] = {"Room", "Hall", "Space", "Haze", "Bloom", "Lofi"};
static const char *const ECHOS[] = {"Tape", "Grains"};
static const choice_t CHOICE[] = {
    {PG_SOUND, 0, 2, 2, LOOPS}, {PG_FILTER, 0, 4, 8, FILTERS}, {PG_ROLL, 0, 4, 3, ZOOMS},
    {PG_FX, 0, 4, 5, COLOURS}, {PG_FX, 1, 4, 4, MEDIAS}, {PG_FX, 2, 4, 4, MOTIONS}, {PG_GLOBAL, 3, 0, DMK_THEMES, THEME_NAME},
    {PG_GLOBAL, 0, 4, 6, VERBS}, {PG_GLOBAL, 1, 4, 2, ECHOS}, {PG_LFO, 0, 2, 5, LFOS}, {PG_LFO, 0, 4, 4, TARGETS},
    {PG_CHORD, 0, 0, 5, VOICINGS}, {PG_CHORD, 0, 1, 5, INVERTS}, {PG_CHORD, 0, 4, 20, PROGS}, {PG_CHORD, 1, 4, 20, PROGS},
    {PG_CHORD, 1, 0, 12, KEYS}, {PG_CHORD, 1, 1, 6, MODENAMES}, {PG_CHORD, 1, 2, 4, CHANGES}, {PG_CHORD, 1, 3, 5, RHYTHMS},
    {PG_CHORD, 2, 0, 2, KEYMODES}, {PG_CHORD, 2, 1, 8, KTYPES}, {PG_CHORD, 2, 4, 20, PROGS},
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
    uint8_t keys_down[DMK_KEYS], key_sy[DMK_KEYS], key_note[DMK_KEYS], key_home[DMK_KEYS];
    uint8_t key_hn[DMK_KEYS][DMK_SYNTHS];               /* a key's harmony on each follower, + 1 (0: none) */
    /* the roll, per synth: the cursor, the lowest scale row shown, the pen's length, the zoom (16 << z steps) */
    uint8_t roll_cur[DMK_SYNTHS], roll_view[DMK_SYNTHS], roll_pen[DMK_SYNTHS], roll_zoom[DMK_SYNTHS];
    uint8_t key_roll[DMK_KEYS], key_step[DMK_KEYS];     /* a key's note: 1 written or held on the roll (at key_step),
                                                           2 being recorded, 3 deleted while held */
    uint32_t key_t0[DMK_KEYS];                          /* recording: where the playing was when it went down */
    uint16_t hinted;                                    /* the pages whose next row was shown how */
    uint8_t key_ch[DMK_KEYS][7], key_nch[DMK_KEYS];     /* the CHORD page: each key's chord, as it sounds */
    uint8_t clear_arm;                                  /* OCT- once: 1 this synth's roll, 2 all three, armed */
    uint32_t clear_at;
    uint8_t song, song_pick, song_arm;                  /* the song open's slot; the SONGS page's; a save over, armed */
    uint32_t song_arm_at;
    uint8_t preset[DMK_SYNTHS];                         /* PRESETS: 0-63 the recordings, then the library's sounds */
    uint8_t arr_cur, arr_bars;                          /* Arrange: the step cursor; the bars a key writes a step with */
    uint8_t copy_arm, copy_to;                          /* HOME held + OCT+: a pattern key next pastes into it */
    uint8_t full, full_latch;                           /* the landscape full screen (OCT- and OCT+ held); held once */
    int8_t oct_before;                                  /* the octave before an OCT press, for the two pressed together */
    uint32_t both_at;
    uint32_t copy_at;
    uint8_t key_slot[DMK_KEYS];                         /* writing: the slot a held key wrote, + 1 (0: none) */
} ui;

static uint8_t leds[DMK_KEYS + B_COUNT];

static void notice(const char *s)
{
    int i;
    for (i = 0; i < 31 && s[i]; i++) ui.note[i] = s[i];
    ui.note[i] = 0;
    ui.note_ms = ui.now ? ui.now : 1;
}

/* the keys play chords here: on CHORD, or anywhere but the roll and Arrange (whose keys write) when the Keys row
 * says All; there they light, write and play as on CHORD */
static int chords_here(void) { return ui.page == PG_CHORD || (ch.keys && ui.page != PG_ROLL && ui.page != PG_ARR); }

/* the patterns' progression is the CHORD page's (dmk_seq.c keeps the rest) */
static uint8_t pat_prog_get(void) { return ui.val[0][PG_CHORD][0][4]; }
static void pat_prog_put(uint8_t v) { ui.val[0][PG_CHORD][0][4] = v; ch.prog = (uint8_t)(v * PROG_N / 128); }
static void sp_sync(void);
static const choice_t *choice_of(int page, int row, int k);
static int choice_index(const choice_t *c, int v);
/* the progression stepped: its own mode and voicing come with it (CHORD's ALGORITHM, Arrange's Chords) */
static void prog_step(int d)
{
    int p = choice_index(choice_of(PG_CHORD, 0, 4), ui.val[0][PG_CHORD][0][4]) + (d > 0 ? 1 : -1);
    const prog_t *pr;
    if (p < 0 || p >= PROG_N) return;
    pr = p < 16 ? &PROG[p] : &uprog[p - 16];
    ui.val[0][PG_CHORD][0][4] = choice_val(p, PROG_N);
    if (pr->n) { ui.val[0][PG_CHORD][1][1] = choice_val(pr->mode, 6); ui.val[0][PG_CHORD][0][0] = choice_val(pr->voicing, 5); }
    ch_cur = 0;
    sp_sync();
    if (seq_on) { chord_quiet(); chord_start(); }       /* from its first chord, at the next step */
    chord_rest();
}

/* the keys' places: the sixteen white keys from F3 (the scenes, or a synth's eight patterns), and which white key a
 * scene is on */
static int white_of(int k)
{
    static const int8_t W[27] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
    return k >= 0 && k < 27 ? W[k] : -1;
}
static const uint8_t WKEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
/* HOME held, a white key: that scene, at the bar when playing; an empty one starts as a copy of this one */
static void scn_pick(int c)
{
    if (!scn[c].used) { scn_keep(); scn[c] = scn[scn_cur]; }
    if (c == scn_cur) { scn_next = 0xFF; return; }
    if (seq_on) scn_next = (uint8_t)c;
    else scn_open(c);
}
static void pat_pick_syn(int s, int p)                  /* a synth's pattern in the scene: at the bar when playing */
{
    if (p == syn_pat(s)) { spat_next[s] = 0xFF; return; }
    if (seq_on) spat_next[s] = (uint8_t)p;
    else syn_pat_set(s, p);
}
static void pat_mute_flip(int s)                        /* HOME held, a black key: that synth muted, or back */
{
    int i;
    pat_mute ^= (uint8_t)(1u << s);
    (void)i;
    if (pat_mute >> s & 1u) { seq_quiet(s); if (ch.follow[s] == FW_PLAYS) chord_off(s); }
}

/* the store (dmk_store.c, after this file) */
#define ST_SONGS 14
static int store_on(void);
static int song_save(int i);
static int song_load(int i);
static int song_used(int i);
static int song_erase(int i);
static void song_new(void);
static const char *song_name(int i);
static int sound_save(int s);
static void sound_init(int s);
static void sound_put(int s, uint8_t *o);
static int lib_count(void);
static int lib_nth(int n);
static void lib_load(int s, int slot);
static uint8_t ui_first[DMK_SYNTHS][45];                /* INIT: each synth's sound as the first song has it */

/* a control's value: a synth's own on its pages, the song's on Home's last column and Global; Home's rows are views of
 * each synth's level (AMP), sends and pan (FX, SPACE) */
static int per_synth(int page) { return page == PG_SOUND || page == PG_FILTER || page == PG_AMP || page == PG_FX || page == PG_LFO; }
static uint8_t *vp(int page, int row, int k)
{
    if (page == PG_HOME && k == 4) return &ui.val[0][PG_HOME][0][4];   /* the tempo, on every row */
    if (page == PG_CHORD && k == 4) return &ui.val[0][PG_CHORD][0][4];  /* the progression, on both rows */
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
        p->fine = (int8_t)((VS(s, PG_SOUND, 0, 4) - 64) * 50 / 64);
        p->ftype = (uint8_t)(VS(s, PG_FILTER, 0, 4) * 8 / 128);
        p->cut = (int16_t)(VS(s, PG_FILTER, 0, 0) >= 127 ? 255 : VS(s, PG_FILTER, 0, 0) * 2);
        p->resq = T_RESQ[VS(s, PG_FILTER, 0, 1)];
        p->fenv = (int16_t)((VS(s, PG_FILTER, 0, 2) - 64) * 4);
        p->atk = T_ATK[VS(s, PG_AMP, 0, 0)];
        p->dec = T_EXP[VS(s, PG_AMP, 0, 1)];
        p->sus = (int32_t)((uint32_t)VS(s, PG_AMP, 0, 2) * (FULL / 127));
        p->rel = T_EXP[VS(s, PG_AMP, 0, 3)];
        v = VS(s, PG_AMP, 0, 4);
        p->level = v * v * 2;                           /* Q15 on a square law: 64 is -12 dB */
        p->colour = (uint8_t)(VS(s, PG_FX, 0, 4) * 5 / 128);
        p->media = (uint8_t)(VS(s, PG_FX, 1, 4) * 4 / 128);
        p->motion = (uint8_t)(VS(s, PG_FX, 2, 4) * 4 / 128);
        for (v = 0; v < 4; v++) { p->ck[v] = VS(s, PG_FX, 0, v); p->mk[v] = VS(s, PG_FX, 1, v); p->ok[v] = VS(s, PG_FX, 2, v); }
        p->tape = (uint8_t)(p->media == FM_TAPE);
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
    {   /* the VERB's type: its loop's gain from SIZE (Q15, a ninth of a dB a pass at the most for HAZE), its sweep's
         * depth at full MOD (Q8 samples) and its rate (0.3, 0.13, 0.2, 0.5 Hz at 22.05 kHz) */
        static const uint16_t G0[6] = {0, 24000, 26000, 25000, 25000, 19000}, G1[6] = {0, 31900, 32600, 32450, 32300, 30500};
        static const uint16_t MD[6] = {0, 4096, 8192, 12288, 3072, 15360};
        static const uint32_t RT[6] = {0, 58435, 31165, 25322, 38957, 97392};
        int t = VS(0, PG_GLOBAL, 0, 4) * 6 / 128, sz = VS(0, PG_GLOBAL, 0, 2);
        gp.vtype = (uint8_t)t;
        gp.vfbq = (G0[t] + (G1[t] - G0[t]) * sz / 127) * 11585 >> 15;   /* the Hadamard's sqrt(8) taken out */
        gp.vmod = MD[t] * VS(0, PG_GLOBAL, 0, 3) / 127;
        gp.vrate = RT[t];
    }
    gp.vret = VS(0, PG_GLOBAL, 0, 0) * 258;
    gp.vdamp = T_OPH[VS(0, PG_GLOBAL, 0, 1)];
    gp.vfb = 22938 + VS(0, PG_GLOBAL, 0, 2) * 72;
    gp.eret = VS(0, PG_GLOBAL, 1, 0) * 258;
    gp.elp = T_OPH[VS(0, PG_GLOBAL, 1, 1)];
    gp.etime = T_ETIME[VS(0, PG_GLOBAL, 1, 2)];
    gp.efb = VS(0, PG_GLOBAL, 1, 3) * 240;
    gp.etype = (uint8_t)(VS(0, PG_GLOBAL, 1, 4) * 2 / 128);
    gp.glen = 441 + VS(0, PG_GLOBAL, 1, 1) * 62;        /* GRAINS: 20..380 ms */
    gp.gdens = 6 + VS(0, PG_GLOBAL, 1, 2) * 113 / 127;  /* 2..40 a second */
    gp.gtex = VS(0, PG_GLOBAL, 1, 3);
    seq_bpm = VS(0, PG_HOME, 0, 4) < 20 ? 20 : VS(0, PG_HOME, 0, 4);
    ch.voicing = (uint8_t)(VS(0, PG_CHORD, 0, 0) * 5 / 128);
    ch.invert = (uint8_t)(VS(0, PG_CHORD, 0, 1) * 5 / 128);
    ch.spread = (uint8_t)(VS(0, PG_CHORD, 0, 2) * 4 / 128);
    ch.strum = (int16_t)((VS(0, PG_CHORD, 0, 3) - 64) * 250 / 64);
    ch.prog = (uint8_t)(VS(0, PG_CHORD, 0, 4) * 20 / 128);
    seq_key = (uint8_t)(VS(0, PG_CHORD, 1, 0) * 12 / 128);
    seq_mode = (uint8_t)(VS(0, PG_CHORD, 1, 1) * 6 / 128);
    ch.change = (uint8_t)(VS(0, PG_CHORD, 1, 2) * 4 / 128);
    ch.rhythm = (uint8_t)(VS(0, PG_CHORD, 1, 3) * 5 / 128);
    ch.keys = (uint8_t)(VS(0, PG_CHORD, 2, 0) * 2 / 128);
    ch.ktype = (uint8_t)(VS(0, PG_CHORD, 2, 1) * KT_COUNT / 128);
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
    static const uint8_t FIRST[DMK_SYNTHS] = {DMK_FIRST_PAD, DMK_FIRST_KEY, DMK_FIRST_PLUCK};   /* a pad, keys, a pluck */
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
        ui.val[s][PG_SOUND][0][0] = FIRST[s] < DMK_SAMPLES_N ? FIRST[s] : 0;
        ui.val[s][PG_SOUND][0][1] = 0;
        ui.val[s][PG_SOUND][0][2] = 127;                /* the loop on */
        ui.val[s][PG_SOUND][0][4] = 64;                 /* in tune */
        ui.val[s][PG_AMP][0][0] = t[0]; ui.val[s][PG_AMP][0][1] = t[1]; ui.val[s][PG_AMP][0][2] = t[2];
        ui.val[s][PG_AMP][0][3] = t[3]; ui.val[s][PG_AMP][0][4] = t[7];
        ui.val[s][PG_FILTER][0][0] = t[4]; ui.val[s][PG_FILTER][0][1] = t[5]; ui.val[s][PG_FILTER][0][2] = t[6];
        ui.val[s][PG_FILTER][0][4] = 0;                 /* LP24 */
        ui.val[s][PG_FX][1][4] = t[8] ? 48 : 0;         /* Media: tape, or off */
        ui.val[s][PG_FX][0][4] = 0;                     /* Colour, Motion: off */
        ui.val[s][PG_FX][2][4] = 0;
        ui.val[s][PG_FX][1][0] = t[9]; ui.val[s][PG_FX][1][1] = 90;
        ui.val[s][PG_FX][3][0] = t[10]; ui.val[s][PG_FX][3][1] = t[11]; ui.val[s][PG_FX][3][2] = t[12];
    }
    ui.val[0][PG_HOME][0][4] = 72;                      /* tempo 72 */
    ui.val[0][PG_CHORD][0][0] = 2 * 128 / 5 + 12; ui.val[0][PG_CHORD][0][1] = 12; ui.val[0][PG_CHORD][0][2] = 0;
    ui.val[0][PG_CHORD][0][3] = 64; ui.val[0][PG_CHORD][0][4] = 4;            /* wide, auto, no spread or strum; First snow */
    ui.val[0][PG_CHORD][1][0] = 9 * 128 / 12 + 5; ui.val[0][PG_CHORD][1][1] = 10;   /* A, Aeolian */
    ui.val[0][PG_CHORD][1][2] = 48; ui.val[0][PG_CHORD][1][3] = 12;           /* x1, hold */
    ui.val[0][PG_CHORD][2][0] = choice_val(0, 2); ui.val[0][PG_CHORD][2][1] = choice_val(KT_SEVENTH, KT_COUNT);   /* keys: chords on CHORD, sevenths */
    ui.val[0][PG_GLOBAL][0][0] = 84; ui.val[0][PG_GLOBAL][0][1] = 56; ui.val[0][PG_GLOBAL][0][2] = 96;   /* the verb: a hall */
    ui.val[0][PG_GLOBAL][0][4] = 31; ui.val[0][PG_GLOBAL][0][3] = 64;
    ui.val[0][PG_GLOBAL][1][4] = 0;                     /* the echo: tape */
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
    for (s = 0; s < DMK_SYNTHS; s++) { sound_put(s, ui_first[s]); ui.preset[s] = ui.val[s][PG_SOUND][0][0]; }
    ui.arr_cur = 0; ui.arr_bars = 4; ui.copy_arm = 0; ui.full = 0;
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

/* Arrange's values. The Scene row: each synth's pattern in the scene open, its progression. The Song row: the
 * cursor's step (its scene, its bars), the song's end */
static const char *const ENDS[SE_COUNT] = {"Loop", "Stop", "Fade out", "Fades"};
static void arr_text(char *o, int row, int k)
{
    const sstep_t *t = ui.arr_cur < song_n ? &song[ui.arr_cur] : 0;
    if (row == 0) {
        if (k < DMK_SYNTHS) { int p = spat_next[k] < SYN_PATS ? spat_next[k] : syn_pat(k); fmt_int(o, p + 1); return; }
        str_cat(o, PROGS[choice_index(choice_of(PG_CHORD, 0, 4), ui.val[0][PG_CHORD][0][4])]);
        return;
    }
    if (k == 3 || k == 4) { str_cat(o, ENDS[song_end % SE_COUNT]); return; }
    if (k == 0) {
        if (!t) { str_cat(o, "New"); return; }
        o = fmt_int(o, ui.arr_cur + 1); *o++ = '/'; fmt_int(o, song_n);
        return;
    }
    if (k == 2) { fmt_int(o, t ? t->bars : ui.arr_bars); return; }   /* past the last: the bars a key writes */
    if (!t) { str_cat(o, "\xE2\x80\x94"); return; }
    fmt_int(o, t->scene + 1);
}
static int arr_meter(int row, int k)
{
    const sstep_t *t = ui.arr_cur < song_n ? &song[ui.arr_cur] : 0;
    if (row == 0) return k < DMK_SYNTHS ? syn_pat(k) * 127 / (SYN_PATS - 1) : ui.val[0][PG_CHORD][0][4];
    if (k == 0) return song_n ? ui.arr_cur * 127 / song_n : 0;
    if (k == 2) return ((t ? t->bars : ui.arr_bars) - 1) * 127 / 7;
    if (k == 3) return song_end * 127 / (SE_COUNT - 1);
    return t ? t->scene * 127 / (SCENES - 1) : 0;
}

static void val_text(char *o, int page, int row, int k)
{
    if (page == PG_ARR) { arr_text(o, row, k); return; }
    const choice_t *c = choice_of(page, row, k);
    int v = *vp(page, row, k);
    if (page == PG_GLOBAL && row == 3 && k == 0) { str_cat(o, THEME_NAME[theme]); return; }
    if (page == PG_DISCS && k == 0) { if (ui.song_pick < 9) *o++ = '0'; fmt_int(o, ui.song_pick + 1); return; }
    if (page == PG_SOUND && row == 0 && k == 0) {       /* the sound's number; its card under it has the name */
        if (v < 9) *o++ = '0';
        fmt_int(o, v + 1);
        return;
    }
    if (page == PG_CHORD && row == 0 && k == 2) { fmt_int(o, v * 4 / 128); return; }   /* spread: octaves */
    if (page == PG_CHORD && row == 0 && k == 3) {       /* strum: ms between voices */
        int t = (v - 64) * 250 / 64;
        if (t > 0) *o++ = '+';
        fmt_int(o, t);
        return;
    }
    if (page == PG_SOUND && row == 0 && k == 4) {       /* the fine tune, in cents */
        int t = (v - 64) * 50 / 64;
        if (t > 0) *o++ = '+';
        fmt_int(o, t);
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
    return (page == PG_SOUND && row == 1) || (page == PG_GLOBAL && row == 2);
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
    if (page == PG_FX && row < 3) return choice_index(choice_of(PG_FX, row, 4), *vp(PG_FX, row, 4)) == 0;
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
          : page == PG_DISCS && i == 0 ? ui.song_pick * 127 / (ST_SONGS - 1) : page == PG_ARR ? arr_meter(row, i) : *vp(page, row, i),
          choice_of(page, row, i), on, 0);
}

/* the title, its row as the grey half (one size, one line), and the synths at the right: the current one filled */
static void head(void)
{
    const page_t *pg = &PAGE[ui.page];
    int x = text(PAD + 1, 6, pg->name, &FH, C_INK), i;
    if (pg->rows > 1) text(x + 7, 6, pg->row[ui.row[ui.page]], &FH, C_LABEL);
    else if (ui.page == PG_ROLL) {                      /* which of the synth's patterns */
        char t[12];
        fmt_int(str_cat(t, "Pattern "), syn_pat(ui.synth) + 1);
        text(x + 7, 6, t, &FH, C_LABEL);
    }
    {                                                   /* the synths by their sounds' initials, S F G: the current boxed */
        int rx = DMK_W - PAD - 1, bw = 17, y = 6 + FH.ascent - FS.ascent - 3;
        for (i = DMK_SYNTHS - 1; i >= 0; i--) {
            char ini[2] = {DMK_SAMPLES[VS(i, PG_SOUND, 0, 0)].name[0], 0};
            int bx = rx - bw;
            if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
            if (i == ui.synth) round_line(bx, y - 3, bw, bw, 5, 1, C_INK);
            text_c(bx + bw / 2 + 1, y, ini, &FS, i == ui.synth ? C_INK : C_LABEL);
            rx = bx - 3;
        }
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
    if (ui.page == PG_CHORD) {                          /* a written chord held: what its knobs change */
        int h;
        for (h = 0; h < DMK_KEYS && !(ui.keys_down[h] && ui.key_slot[h]); h++) {}
        if (h < DMK_KEYS) {
            const prog_t *p = cur_prog();
            const cslot_t *c = &p->s[ui.key_slot[h] - 1];
            char v[16];
            fmt_int(v, ui.key_slot[h]);
            field_show(0, "Slot", v, (ui.key_slot[h] - 1) * 127 / (PROG_SLOTS - 1));
            v[0] = 0; str_cat(v, c->q == Q_MAJ ? "major" : QS[c->q]);
            field_show(1, "Quality", v, c->q * 127 / (Q_COUNT - 1));
            {                                           /* its length in bars, halves as a half */
                char *o = v;
                v[0] = 0;
                if (c->half >= 2) o = fmt_int(o, c->half / 2);
                if (c->half & 1) str_cat(o, "\xC2\xBD");
            }
            field_show(2, "Bars", v, c->half * 8);
            field_show(3, "Bass", c->pedal ? "Pedal" : "Root", c->pedal ? 127 : 0);
            return;
        }
    }
    for (k = 0; k < 4; k++) {
        const char *l = pg->k[r][k];
        if (ui.page == PG_FX && r < 3) l = FXK[r][choice_index(choice_of(PG_FX, r, 4), *vp(PG_FX, r, 4))][k];
        if (ui.page == PG_GLOBAL && r == 1 && *vp(PG_GLOBAL, 1, 4) >= 64) { static const char *const GK[4] = {"Return", "Size", "Density", "Texture"}; l = GK[k]; }
        field(k, l, ui.page, r);
    }
}

static void foot(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page];
    if (ui.page == PG_ARR) {                            /* ARRANGE: the row's keys; the rows */
        text(PAD + 1, Y_FOOT + 2, "ALGO: the rows", &FS, C_LABEL);
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, r == 0 ? "Keys: patterns" : song_rec ? "Recording" : seq_on ? "PLAY: stop"
               : seq_rec ? "PLAY records" : "Keys write steps", &FS, C_LABEL);
        return;
    }
    if (ui.page == PG_DISCS) {                          /* SONGS: what PLAY and SAVE do here */
        text(PAD + 1, Y_FOOT + 2, store_on() ? "PLAY: load" : "No store here", &FS, C_LABEL);
        if (store_on()) text_r(DMK_W - PAD - 1, Y_FOOT + 2, "SAVE: save here", &FS, C_LABEL);
        return;
    }
    if (ui.btn_held >> B_HOME & 1u && ui.now - ui.btn_down_at[B_HOME] > 350u) {   /* the patterns; the synths muted */
        char t[16];
        int s;
        char *o = str_cat(t, pat_mute ? "Muted" : "Scenes");
        for (s = 0; s < DMK_SYNTHS; s++) if (pat_mute >> s & 1u) { *o++ = ' '; *o++ = (char)('1' + s); *o = 0; }
        text(PAD + 1, Y_FOOT + 2, t, &FS, C_LABEL);
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, ui.copy_arm ? "Paste: a scene key" : "OCT+ copy, OCT\xE2\x88\x92 clear", &FS, ui.copy_arm ? C_INK : C_LABEL);
        return;
    }
    if (ui.page == PG_ROLL) {                           /* the zoom, or a held note's velocity */
        char v[16];
        int h = roll_held(), j = h >= 0 ? seq_find(ui.synth, ui.key_step[h], ui.key_note[h]) : -1;
        int x = text(PAD + 1, Y_FOOT + 2, j >= 0 ? "Velocity" : "Zoom", &FS, C_LABEL);
        fmt_int(v, j >= 0 ? sq[ui.synth].n[j].vel : roll_z());
        text_fit(x + 6, Y_FOOT + 2, 100 - x, v, &FS, lit(4) ? HOLO : C_INK);
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, FOLLOWS[ch.follow[ui.synth]], &FS, C_LABEL);   /* how it follows (SEQ + ALGORITHM) */
        return;
    }
    if (pg->algo[r][0]) {                               /* the ALGORITHM encoder's value, under the cards */
        char v[16];
        int x = text(PAD + 1, Y_FOOT + 2, pg->algo[r], &FS, C_LABEL), typed = (ui.page == PG_FX && r < 3) || (ui.page == PG_GLOBAL && r < 2) || ui.page == PG_FILTER;
        val_text(v, ui.page, r, 4);
        text_fit(x + 6, Y_FOOT + 2, 124 - x, v, &FS, lit(4) ? HOLO : C_INK);
        if (typed) text_r(DMK_W - PAD - 1, Y_FOOT + 2, ui.page == PG_FX ? "Hold FX to change" : ui.page == PG_FILTER ? "Hold SEL to change" : "Hold GLO to change", &FS, C_LABEL);
        if (ui.page == PG_CHORD) text_r(DMK_W - PAD - 1, Y_FOOT + 2, !seq_on && seq_rec ? "Keys write chords" : "REC: write", &FS, C_LABEL);

        if (ui.page == PG_HOME) {                       /* where the music is: the pattern, or the song */
            char t[20], *o = t;
            if (song_mode && song_playing()) { o = fmt_int(str_cat(o, "Song "), song_at); *o++ = '/'; fmt_int(o, song_n); }
            else if (song_mode) str_cat(o, "Song mode");
            else fmt_int(str_cat(o, "Scene "), scn_cur + 1);
            text_r(DMK_W - PAD - 1, Y_FOOT + 2, t, &FS, song_mode ? C_INK : C_LABEL);
        }
    }
}

/* ---- the stages: cards */
#define SX M                                            /* the stage: x, y, width, height */
#define SY Y_STAGE
#define SW (DMK_W - 2 * M)
#define SH (Y_FOOT - Y_STAGE - 4)

/* Home: a scene in the screen's own light (the owner's idea, 2026-10-24), one of four by the current synth's sound
 * (its scene, tools/gen_samples.py): PEAKS for the bright pads (a snowy mountain in the last light, snow falling),
 * LAKE for the keys (a cabin by a lake at dusk), COAST for the plucks and the warm pads (sea stacks under a low sun,
 * the sea glittering), NIGHT for the basses and the dark pads (a forest under a big moon, fireflies). The cabin's three
 * windows, or three lights in each scene, are the synths, lit while each sounds. Drawn every 140 ms, held between */
#define SC_W SW
#define SC_H SH
/* the still parts are built a pixel a byte (0xFF where the sky shows) in the light's scratch, which nothing lit
 * needs meanwhile, then packed as the screen with a mask: a frame of the scene lays them over the sky by the byte */
#define sc_fg th_d
typedef char sc_fits[SC_W * SC_H <= TH_MAX ? 1 : -1];
static uint8_t sc_fgp[SC_W / 2 * SC_H] POOL;            /* the still parts, packed; 0 where the sky shows */
static uint8_t sc_fgm[SC_W / 2 * SC_H] POOL;            /* their mask: 0xF where a pixel is theirs */
static uint8_t sc_out[SC_W / 2 * SC_H] POOL;            /* the scene as last drawn, packed as the screen */
static uint8_t sc_key = 0xFF;                           /* the theme and the scene it was built for */
static uint32_t sc_tick = 0xFFFFFFFFu;

static int tri(int x, int p, int a) { int m = ((x % p) + p) % p; return a * (m * 2 > p ? 2 * p - m * 2 : m * 2) / p; }
static uint32_t hsh(uint32_t a) { a ^= a >> 16; a *= 0x7FEB352Du; a ^= a >> 15; a *= 0x846CA68Bu; return a ^ (a >> 16); }
static int bay(int x, int y) { return T_BAYER6[0][(y & 3) * 4 + (x & 3)]; }

/* each scene's sky: colours from the top (-1 the dark), where each is whole, dithered between */
static const struct { uint8_t n, at[8]; int8_t c[8]; } SKY[DMK_SCENES] = {
    {6, {0, 14, 30, 46, 64, 84}, {-1, C_HOLO0, C_HOLO1, C_HOLO2, C_HOLO5, C_HOLO6}},
    {8, {0, 12, 26, 38, 49, 58, 66, 80}, {-1, C_HOLO0, C_HOLO1, C_HOLO2, C_HOLO3, C_HOLO4, C_HOLO5, C_HOLO6}},
    {7, {0, 10, 24, 38, 50, 60, 72}, {-1, C_HOLO2, C_HOLO3, C_HOLO4, C_HOLO5, C_HOLO6, C_HOLO7}},
    {4, {0, 40, 66, 76}, {-1, -1, C_HOLO0, C_HOLO1}},
    {5, {0, 14, 34, 54, 72}, {-1, C_HOLO0, C_HOLO1, C_HOLO2, C_HOLO3}},   /* ROOM: the city's night through the window */
    {3, {0, 64, 96}, {-1, -1, C_HOLO0}}};                                  /* DESK: the wall, dark, a glow low by the screen */
static int sky_at(int sc, int x, int y, int dk)
{
    int i = 0, t, a, b, n = SKY[sc].n;
    while (i < n - 1 && y >= SKY[sc].at[i + 1]) i++;
    if (i == n - 1) return SKY[sc].c[i] < 0 ? dk : SKY[sc].c[i];
    t = (y - SKY[sc].at[i]) * 16 / (SKY[sc].at[i + 1] - SKY[sc].at[i]);
    a = SKY[sc].c[i] < 0 ? dk : SKY[sc].c[i];
    b = SKY[sc].c[i + 1] < 0 ? dk : SKY[sc].c[i + 1];
    return bay(x, y) < t ? b : a;
}

static void fg_put(int x, int y, int c) { if (x >= 0 && x < SC_W && y >= 0 && y < SC_H) sc_fg[y * SC_W + x] = (uint8_t)c; }
static void fg_rect(int x, int y, int w, int h, int c) { int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) fg_put(x + i, y + j, c); }
static void fg_disc(int cx, int cy, int r, int c)       /* a disc, its edge stepped */
{
    int i, j;
    for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) if (i * i + j * j <= r * r + r / 2) fg_put(cx + i, cy + j, c);
}
/* the ROOM's window and the DESK's things, where the frame draws what moves on them */
#define RW_X0 40
#define RW_X1 176
#define RW_Y0 8
#define RW_Y1 80
#define RW_MX 106                                       /* its mullions: a cross */
#define RW_MY 42
#define RT_X 150                                        /* the tower, its beacon at RT_Y */
#define RT_Y 22
static int room_glass(int x, int y)                     /* a pane of the ROOM's window, not its frame or the plant */
{
    return x >= RW_X0 && x < RW_X1 && y >= RW_Y0 && y < RW_Y1 && (x < RW_MX || x > RW_MX + 2) && (y < RW_MY || y > RW_MY + 1) &&
           !(x >= 44 && x < 72 && y >= 54) && x >= 47 && x < 170;
}
static void fg_pine(int x, int base, int h, int dk)     /* a pine: stepped, a row a step */
{
    int y, i;
    for (y = 0; y < h; y++) {
        int half = y / 3 + (y % 6 < 2 ? 0 : 1);
        for (i = -half; i <= half; i++) fg_put(x + i, base - h + y, dk);
    }
}
static void fg_mirror(int sc, int sea, int dk)          /* water: what is above it, mirrored, darker, rippled */
{
    int x, y;
    for (y = sea; y < SC_H; y++) {
        int d = y - sea, sy = sea - 1 - d * 2, sh = d % 5 == 0 ? 2 : 0;
        if (sy < 0) sy = 0;
        for (x = 0; x < SC_W; x++) {
            int sx = x + sh < SC_W ? x + sh : x, c = sc_fg[sy * SC_W + sx] != 0xFF ? sc_fg[sy * SC_W + sx] : sky_at(sc, sx, sy, dk);
            sc_fg[y * SC_W + x] = (uint8_t)(bay(x, y) < 9 ? dk : c);
        }
    }
}

static void scene_build(int sc, int dk, int lt)
{
    int x, y, k;
    for (k = 0; k < SC_W * SC_H; k++) sc_fg[k] = 0xFF;
    if (sc == 0) {                                      /* PEAKS: the mountain, its snow in the last light */
        for (x = 0; x < SC_W; x++) {
            int peak = 14 + (x < 114 ? (114 - x) * 3 / 4 : (x - 114) * 2 / 3) + tri(x, 11, 3), lo = 62 + tri(x + 9, 40, 8);
            for (y = peak; y < 96; y++) {
                int lit = x > 114 - (y - peak) / 3, snow = y < 52 + tri(x * 3, 17, 10), c;
                if (snow) c = lit ? (bay(x, y) < 10 ? C_HOLO6 : C_HOLO5) : (bay(x, y) < 6 ? lt : C_A_LABEL);
                else c = lit ? (bay(x, y) < 8 ? C_HOLO1 : C_HOLO0) : (bay(x, y) < 10 ? dk : C_HOLO0);
                fg_put(x, y, c);
            }
            for (y = lo; y < 96; y++) if (y > peak) fg_put(x, y, bay(x, y) < 12 ? dk : C_HOLO0);   /* the near ridge */
        }
        for (k = 0; k < 14; k++) fg_pine(k * 17 + (int)(hsh((uint32_t)k) % 9u), 104, 18 + (int)(hsh((uint32_t)k + 40u) % 16u), dk);
        for (y = 96; y < SC_H; y++) for (x = 0; x < SC_W; x++) fg_put(x, y, y > 100 ? dk : (bay(x, y) < 10 ? dk : C_HOLO0));
    } else if (sc == 1) {                               /* LAKE: violet hills, nearer ones, pines, the cabin, the lake */
        static const struct { uint8_t x, h; } PINE[] = {{4, 34}, {13, 26}, {21, 40}, {62, 22}, {176, 30}, {186, 42}, {196, 28}, {207, 36}, {218, 30}, {226, 24}};
        for (x = 0; x < SC_W; x++) {
            int dv = x - 151 < 0 ? 151 - x : x - 151;
            int yf = 72 - tri(x + 20, 96, 22) - tri(x + 5, 37, 7) - tri(x, 13, 2) + (dv < 26 ? (26 - dv) / 2 : 0), ym = 79 - tri(x + 50, 70, 9) - tri(x + 11, 29, 4);
            for (y = yf; y < 80; y++) fg_put(x, y, y == yf && x > 110 && x < 190 ? C_HOLO5 : bay(x, y) < 8 ? C_HOLO1 : C_HOLO2);
            for (y = ym; y < 80; y++) fg_put(x, y, bay(x, y) < 6 ? C_HOLO0 : dk);
        }
        for (k = 0; k < (int)(sizeof PINE / sizeof PINE[0]); k++) fg_pine(PINE[k].x, 82, PINE[k].h, dk);
        for (y = 66; y < 82; y++) for (x = 34; x < 60; x++)
            if ((y < 72 && x >= 34 + (72 - y) * 2 - 2 && x < 60 - (72 - y) * 2 + 2) || y >= 72) fg_put(x, y, dk);
        for (y = 80; y < 83; y++) for (x = 0; x < SC_W; x++) fg_put(x, y, dk);
        fg_mirror(1, 83, dk);
    } else if (sc == 2) {                               /* COAST: the sea, two stacks, a cliff with grass */
        for (y = 72; y < SC_H; y++) for (x = 0; x < SC_W; x++) {
            int d = y - 72, c = d < 6 ? (bay(x, y) < 8 ? C_HOLO4 : C_HOLO3) : d < 16 ? (bay(x, y) < 6 ? C_HOLO3 : C_HOLO1) : (bay(x, y) < 5 ? C_HOLO1 : C_HOLO0);
            fg_put(x, y, c);
        }
        for (k = 0; k < 2; k++) {                       /* the stacks, their sunward edges lit */
            int cx = k ? 176 : 118, h = k ? 26 : 18, w = k ? 22 : 14;
            for (y = 0; y < h; y++) {                   /* rounded on top, ragged on the sides */
                int half = w / 2 * (y + 6) / (h + 6) - (y < 3 ? 3 - y : 0) + tri(y * 5 + k * 3, 9, 2), l = -half + tri(y * 3 + 4, 7, 2);
                for (x = l; x <= half; x++) fg_put(cx + x, 78 - h + y, x <= l + 1 || y < 2 ? (bay(x, y) < 10 ? C_HOLO4 : C_HOLO3) : dk);
            }
        }
        for (x = 0; x < 96; x++) {                      /* the cliff, the grass on it */
            int top = 52 + x / 3 + tri(x, 23, 6) + (x > 70 ? (x - 70) * 2 : 0);
            for (y = top; y < SC_H; y++) fg_put(x, y, y < top + 2 ? (bay(x, y) < 8 ? C_HOLO4 : C_HOLO2) : bay(x, y) < 13 ? dk : C_HOLO0);
            if (!(x % 4)) for (y = 1; y < 4; y++) fg_put(x, top - y, dk);
        }
    } else if (sc == 3) {                               /* NIGHT: hills, a forest, mist */
        for (x = 0; x < SC_W; x++) {
            int h1 = 66 - tri(x + 30, 110, 16) - tri(x, 31, 5), h2 = 82 - tri(x + 70, 64, 10) - tri(x + 3, 17, 3);
            for (y = h1; y < SC_H; y++) fg_put(x, y, bay(x, y) < 5 ? C_HOLO0 : dk);
            for (y = h2; y < SC_H; y++) fg_put(x, y, dk);
        }
        for (k = 0; k < 18; k++) fg_pine(k * 13 + (int)(hsh((uint32_t)k + 7u) % 7u), 108, 14 + (int)(hsh((uint32_t)k + 99u) % 22u), dk);
    } else if (sc == 4) {                               /* ROOM: a city at night through a bedroom's window */
        for (y = 0; y < SC_H; y++) for (x = 0; x < SC_W; x++) {   /* the walls, warmed by the lamp in the corner */
            int dx = x - 211, dy = y - 80, d = dx * dx + dy * dy;
            if (x >= RW_X0 && x < RW_X1 && y >= RW_Y0 && y < RW_Y1) continue;
            fg_put(x, y, d < 260 ? (bay(x, y) * 16 < 260 - d + 40 ? C_HOLO4 : C_HOLO3) : d < 1300 ? (bay(x, y) * 90 < 1300 - d ? C_HOLO3 : dk)
                         : d < 2600 ? (bay(x, y) * 260 < 2600 - d ? C_HOLO2 : dk) : bay(x, y) < 2 ? C_HOLO0 : dk);   /* the lamp's light, warm, small */
        }
        for (x = RW_X0, k = 0; x < RW_X1; k++) {        /* the city: far towers, then near blocks with their windows lit */
            int w = 7 + (int)(hsh((uint32_t)k * 3u + 1u) % 9u), h = 18 + (int)(hsh((uint32_t)k + 70u) % 26u);
            fg_rect(x, RW_Y1 - h, w - 1, h, C_HOLO1);
            x += w;
        }
        for (x = RW_X0, k = 0; x < RW_X1; k++) {
            int w = 10 + (int)(hsh((uint32_t)k * 5u + 9u) % 12u), h = 8 + (int)(hsh((uint32_t)k + 31u) % 22u), i, j;
            if (x <= RT_X && x + w > RT_X) h = RW_Y1 - RT_Y - 8;   /* the tower */
            fg_rect(x, RW_Y1 - h, w - 1, h, dk);
            for (j = RW_Y1 - h + 3; j < RW_Y1 - 2; j += 5)
                for (i = x + 2; i < x + w - 3; i += 4)
                    if (hsh((uint32_t)(i * 131 + j)) % 5u < 2u) fg_rect(i, j, 1, 2, hsh((uint32_t)(i + j * 7)) & 1u ? C_HOLO5 : C_HOLO6);
            x += w;
        }
        fg_rect(RT_X, RT_Y, 1, 10, dk);                 /* the tower's mast */
        fg_rect(RW_X0 - 3, RW_Y0 - 3, RW_X1 - RW_X0 + 6, 3, C_LABEL);   /* the frame, the cross */
        fg_rect(RW_X0 - 3, RW_Y1, RW_X1 - RW_X0 + 6, 3, C_LABEL);
        fg_rect(RW_X0 - 3, RW_Y0, 3, RW_Y1 - RW_Y0, C_LABEL);
        fg_rect(RW_X1, RW_Y0, 3, RW_Y1 - RW_Y0, C_LABEL);
        fg_rect(RW_MX, RW_Y0, 3, RW_Y1 - RW_Y0, C_LABEL);
        fg_rect(RW_X0, RW_MY, RW_X1 - RW_X0, 2, C_LABEL);
        fg_rect(30, RW_Y1 + 3, 158, 3, C_LINE);         /* the sill */
        fg_rect(30, RW_Y1 + 6, 158, 2, C_LABEL);
        for (k = 0; k < 2; k++)                         /* the curtains, gathered, their folds */
            for (y = 3; y < 98; y++) {
                int x0 = k ? 168 : 26, w = 20 - (y > 60 ? (y - 60) / 6 : 0) + (y < 10 ? 2 : 0);
                for (x = 0; x < w; x++) { int f = (x + (y > 60 ? (y - 60) / 8 : 0)) % 6; fg_put(k ? x0 + 20 - w + x : x0 + x, y, f == 0 ? C_LABEL : f == 1 ? (bay(x, y) < 8 ? C_A_INK2 : dk) : dk); }   /* dark cloth, its folds lit */
            }
        fg_rect(20, 2, 180, 1, C_LABEL);                /* the rod */
        fg_rect(52, 75, 12, 8, C_LABEL);                /* the plant on the sill, its pot */
        fg_rect(51, 74, 14, 2, C_LINE);
        for (k = 0; k < 6; k++) {                       /* its leaves, split, lit at the edge by the city */
            int cx = 50 + (int)(hsh((uint32_t)k + 5u) % 18u), cy = 58 + (int)(hsh((uint32_t)k + 11u) % 12u), r = 4 + (int)(hsh((uint32_t)k) % 3u);
            fg_disc(cx, cy, r, dk);
            fg_put(cx - r + 1, cy - 1, C_HOLO2); fg_put(cx, cy - r + 1, C_HOLO1);
            fg_put(cx + 1, cy, C_HOLO0); fg_put(cx + 2, cy + 1, C_HOLO0);   /* the holes of its split leaves */
        }
        fg_rect(57, 66, 1, 9, dk);
        fg_rect(122, 77, 16, 2, C_LINE); fg_rect(123, 79, 14, 2, C_HOLO4); fg_rect(121, 81, 18, 2, C_LABEL);   /* books */
        fg_rect(146, 76, 7, 7, lt); fg_rect(153, 78, 2, 3, lt);   /* a mug */
        for (y = 92; y < SC_H; y++) for (x = 0; x < 150; x++) {   /* the bed, its quilt in squares */
            int top = 98 + tri(x, 40, 3);
            if (y < top) continue;
            fg_put(x, y, ((x / 8 + y / 6) & 1) ? (bay(x, y) < 6 ? C_HOLO0 : dk) : (bay(x, y) < 3 ? C_A_INK2 : dk));
        }
        fg_rect(8, 92, 34, 8, C_LINE); fg_rect(9, 91, 32, 1, C_LABEL);   /* its pillow */
        fg_rect(196, 92, 30, 26, dk); fg_rect(196, 92, 30, 2, C_LABEL);  /* the nightstand, the lamp on it */
        fg_rect(210, 84, 3, 8, C_LABEL); fg_rect(205, 90, 13, 2, C_LABEL);
        for (y = 0; y < 9; y++) fg_rect(206 - y / 2, 75 + y, 11 + y, 1, C_HOLO5);   /* its shade, lit */
    } else {                                            /* DESK: a 2000s desk, a screen, cassettes, a CD-ROM */
        for (y = 90; y < SC_H; y++) for (x = 0; x < SC_W; x++)   /* the desk, its grain */
            fg_put(x, y, y < 92 ? C_LABEL : (y - 90) % 7 == 0 ? C_HOLO0 : bay(x, y) < 2 ? C_HOLO0 : dk);
        for (y = 92; y < 104; y++) for (x = 8; x < 120; x++)   /* the screen's light on it */
            if (bay(x, y) * 4 < 40 - (y - 92) * 3) fg_put(x, y, C_HOLO1);
        for (y = 14; y < 88; y++) for (x = 16; x < 112; x++) {   /* the screen: a beige box, rounded, darker to the right */
            int cx = x < 22 ? 22 - x : x > 105 ? x - 105 : 0, cy = y < 20 ? 20 - y : y > 81 ? y - 81 : 0;
            if (cx * cx + cy * cy > 36) continue;
            fg_put(x, y, x > 103 || y > 84 ? C_LABEL : C_LINE);   /* the light from the left: its right and foot in shade */
        }
        for (y = 21; y < 74; y++) for (x = 26; x < 102; x++) {   /* its glass, dark, its rim */
            int edge = y == 21 || y == 73 || x == 26 || x == 101;
            fg_put(x, y, edge ? C_LABEL : dk);
        }
        for (k = 0; k < 5; k++) fg_rect(30 + k * 4, 80, 2, 3, C_LABEL);   /* the vents */
        fg_rect(50, 88, 28, 2, C_LABEL); fg_rect(46, 90, 36, 2, C_LINE);  /* the stand */
        fg_rect(14, 96, 96, 8, C_LABEL);                /* the keyboard, its keys */
        for (y = 97; y < 103; y += 2) for (x = 16; x < 108; x += 4) fg_rect(x, y, 3, 1, C_LINE);
        fg_rect(2, 80, 9, 10, lt); fg_rect(11, 82, 2, 4, lt);   /* a mug */
        fg_rect(113, 84, 7, 6, C_LABEL);                /* a cactus, its pot */
        fg_rect(115, 74, 3, 10, dk); fg_rect(113, 77, 2, 4, dk); fg_rect(118, 76, 2, 4, dk);
        fg_put(114, 76, C_HOLO1); fg_put(118, 75, C_HOLO1);
        for (k = 0; k < 9; k++) {                       /* the tapes on their spines, standing */
            static const uint8_t TC[5] = {C_LABEL, C_HOLO2, C_LINE, C_HOLO4, C_A_LABEL};
            int x0 = 172 + k * 4, top = 48 + (int)(hsh((uint32_t)k + 3u) % 6u);
            fg_rect(x0, top, 3, 90 - top, TC[k % 5]);
            fg_rect(x0, top + 14, 3, 6, lt);
        }
        fg_disc(194, 74, 16, C_LINE);                   /* a CD, leaning on them */
        fg_disc(194, 74, 15, C_LABEL);
        fg_disc(194, 74, 13, C_LINE);
        fg_disc(194, 74, 5, lt);
        fg_disc(194, 74, 2, dk);
        fg_rect(120, 64, 46, 26, C_LABEL);              /* a cassette, standing in front */
        fg_rect(120, 64, 46, 1, C_LINE);
        fg_rect(124, 67, 38, 9, lt);                    /* its label, written on */
        fg_rect(127, 70, 22, 1, dk); fg_rect(127, 73, 15, 1, C_A_INK2);
        fg_rect(131, 78, 24, 8, dk);                    /* its window, the tape */
        fg_disc(135, 82, 3, C_LINE); fg_disc(151, 82, 3, C_LINE);
        fg_rect(139, 81, 8, 3, C_A_INK2);
    }
}

static void cloud(int x, int y, int w, int lt)          /* a cloud: puffs on a flat base, its underside lit pink */
{
    int k, n = 3 + w / 14;
    for (k = 0; k <= n; k++) { int r = 4 + (int)(hsh((uint32_t)(w * 7 + k)) % 4u) + (k && k < n ? 3 : 0); round_fill(SX + x + k * w / n - r, SY + y - r * 2 + 2, r * 2, r * 2, r, C_HOLO6); }
    round_fill(SX + x - 3, SY + y - 3, w + 6, 6, 3, C_HOLO6);
    for (k = 0; k <= n; k++) { int r = 4 + (int)(hsh((uint32_t)(w * 7 + k)) % 4u) + (k && k < n ? 3 : 0); round_fill(SX + x + k * w / n - r, SY + y - r * 2, r * 2, r * 2 - 1, r, lt); }
    round_fill(SX + x - 3, SY + y - 5, w + 6, 4, 2, lt);
}

static int scene_of(int s) { int c = DMK_SAMPLES[VS(s, PG_SOUND, 0, 0)].scene; return c < DMK_SCENES ? c : 0; }   /* a synth's landscape */
/* a frame of scene sc at its card's place (the boot shows it full screen), drawn every 140 ms and held in sc_out */
static void scene_frame(int sc, uint32_t tick)
{
    int dark = THEME_DARK[theme], dk = dark ? C_PAPER : C_INK, lt = dark ? C_INK : C_PAPER, x, y, i;
    if (sc_key != (uint8_t)(theme * 8 + sc)) {
        ui_yield();
        scene_build(sc, dk, lt);
        ui_yield();
        for (i = 0; i < SC_W / 2 * SC_H; i++) {
            int a = sc_fg[i * 2], b = sc_fg[i * 2 + 1];
            sc_fgp[i] = (uint8_t)((a != 0xFF ? a << 4 : 0) | (b != 0xFF ? b : 0));
            sc_fgm[i] = (uint8_t)((a != 0xFF ? 0xF0 : 0) | (b != 0xFF ? 0x0F : 0));
        }
        sc_key = (uint8_t)(theme * 8 + sc); sc_tick = 0xFFFFFFFFu;
    }
    if (tick != sc_tick) {                              /* a new frame of the scene: drawn, then held */
        sc_tick = tick;
        for (y = 0; y < SC_H; y++) {                    /* the sky: a row is two bytes over and over (its dither's period) */
            uint8_t *b = &fb[((SY + y) * DMK_W + SX) >> 1];
            uint8_t p0 = (uint8_t)(sky_at(sc, 0, y, dk) << 4 | sky_at(sc, 1, y, dk)), p1 = (uint8_t)(sky_at(sc, 2, y, dk) << 4 | sky_at(sc, 3, y, dk));
            for (x = 0; x < SC_W / 2; x += 2) { b[x] = p0; b[x + 1] = p1; }
        }
        for (i = 0; i < (sc == 3 ? 34 : sc == 5 ? 0 : 14); i++) {   /* the stars, twinkling (the night has many; the desk none) */
            uint32_t h = hsh((uint32_t)i * 7919u + (uint32_t)sc);
            if (hsh(h + tick / 3u) & 3u) px_put(SX + (int)(h % (uint32_t)SC_W), SY + 3 + (int)((h >> 12) % (sc == 3 ? 54u : 22u)), lt);
        }
        if (sc == 0) { round_fill(SX + 22, SY + 18, 12, 12, 6, lt); }   /* the moon over the peaks, early */
        if (sc == 1) round_fill(SX + 140, SY + 50, 22, 22, 11, HOLO);    /* the sun in the valley */
        if (sc == 2) round_fill(SX + 128, SY + 56, 30, 30, 15, HOLO);    /* the sun, low on the sea */
        if (sc == 3) { round_fill(SX + 150, SY + 14, 30, 30, 15, HOLO); round_fill(SX + 156, SY + 20, 18, 18, 9, lt); }   /* the moon, haloed */
        if (sc == 4) round_fill(SX + 158, SY + 12, 10, 10, 5, lt);   /* the moon over the city, in the window's corner */
        for (i = 0; i < (sc == 3 || sc == 4 ? 1 : sc == 5 ? 0 : 3); i++) {   /* clouds, drifting (in the room's window one) */
            int w = i == 0 ? 36 : i == 1 ? 26 : 20, cy = i == 0 ? 46 : i == 1 ? 34 : 40;
            int cx = (int)((i == 0 ? 30u : i == 1 ? 150u : 95u) + tick / (3u + (uint32_t)i)) % (SC_W + 80) - 40;
            if (sc == 3) cy = 60;
            if (sc == 4) cy = 30;
            cloud(cx, cy, w, lt);
        }
        ui_yield();
        for (y = 0, i = 0; y < SC_H; y++) {             /* the still world over the sky */
            uint8_t *b = &fb[((SY + y) * DMK_W + SX) >> 1];
            for (x = 0; x < SC_W / 2; x++, i++) if (sc_fgm[i]) b[x] = (uint8_t)((b[x] & ~sc_fgm[i]) | sc_fgp[i]);
        }
        if (sc == 1 || sc == 2) {                       /* the sun on the water: glints that move */
            int sea = sc == 1 ? 84 : 74, cx = sc == 1 ? 151 : 143;
            for (y = sea; y < SC_H; y += 2) {
                int w = 4 + (y - sea) * (sc == 2 ? 2 : 1) / 2, n = 2 + (y - sea) / 6, k;
                for (k = 0; k < n; k++) {
                    uint32_t h = hsh((uint32_t)(y * 131 + k) + tick);
                    px_put(SX + cx - w / 2 + (int)(h % (uint32_t)w), SY + y, h & 0x100u ? C_HOLO7 : C_HOLO6);
                }
            }
        }
        if (sc == 4) {                                  /* ROOM: the city's windows going on and off, the beacon, rain, steam */
            for (i = 0; i < 60; i++) {
                uint32_t h = hsh((uint32_t)i * 977u + tick / 6u);
                x = RW_X0 + (int)(h % (uint32_t)(RW_X1 - RW_X0)); y = RW_Y0 + 20 + (int)((h >> 9) % (uint32_t)(RW_Y1 - RW_Y0 - 20));
                if ((sc_fg[y * SC_W + x] == C_HOLO5 || sc_fg[y * SC_W + x] == C_HOLO6) && !(h >> 20 & 3u)) px_put(SX + x, SY + y, dk);
            }
            if (tick % 8u < 2u) { px_put(SX + RT_X, SY + RT_Y - 1, C_HOLO7); px_put(SX + RT_X - 1, SY + RT_Y, C_HOLO5); px_put(SX + RT_X + 1, SY + RT_Y, C_HOLO5); }
            for (i = 0; i < 28; i++) {                  /* rain on the glass: streaks running down, a little aslant */
                uint32_t h = hsh((uint32_t)i * 2654435761u);
                int len = 2 + (int)(h % 3u), rx = RW_X0 + (int)((h >> 4) % (uint32_t)(RW_X1 - RW_X0));
                int ry = RW_Y0 + (int)(((h >> 12) % (uint32_t)(RW_Y1 - RW_Y0)) + tick * (2u + (h >> 28 & 1u))) % (RW_Y1 - RW_Y0), j;
                for (j = 0; j < len; j++) { int px = rx - (ry + j) / 9 % 3, py = ry + j; if (room_glass(px, py)) px_put(SX + px, SY + py, j ? C_LABEL : C_LINE); }
            }
            for (i = 0; i < 4; i++) {                   /* the mug's steam */
                int sy = 74 - (int)((tick + (uint32_t)i * 3u) % 10u), sx = 148 + i % 2 * 2 + tri((int)tick + i * 3, 6, 2);
                px_put(SX + sx, SY + sy, (tick + (uint32_t)i) % 10u < 6u ? C_LINE : C_LABEL);
            }
        }
        if (sc == 5) {                                  /* DESK: the screen playing, the reels turning, the disc's glint, steam */
            for (y = 23; y < 72; y += 2) for (x = 28; x < 100; x += 1) if (bay(x, y) < 2) px_put(SX + x, SY + y, C_HOLO0);   /* scanlines */
            fill(SX + 30, SY + 25, 40, 3, C_HOLO4);     /* a player: its title, its time, a bar moving */
            fill(SX + 74, SY + 25, 12, 3, C_HOLO2);
            fill(SX + 30, SY + 31, 68, 1, C_HOLO1);
            fill(SX + 30 + (int)(tick % 68u), SY + 30, 2, 3, C_HOLO6);
            for (i = 0; i < 12; i++) {                  /* its bars: the three synths, four each, rising with them */
                int sy = i / 4, lvl = syn_peak[sy] * 30 / 32768 + (int)(hsh((uint32_t)i + tick) % 4u), hb = lvl < 2 ? 2 : lvl > 30 ? 30 : lvl;
                fill(SX + 32 + i * 5 + sy * 4, SY + 68 - hb, 4, hb, hb > 20 ? C_HOLO6 : hb > 10 ? C_HOLO4 : C_HOLO2);
            }
            if ((tick / 4u) & 1u) fill(SX + 92, SY + 25, 3, 4, C_HOLO5);   /* a cursor, blinking */
            for (i = 0; i < 2; i++) {                   /* the reels: spokes turning */
                static const int8_t SPX[6] = {2, 1, -1, -2, -1, 1}, SPY[6] = {0, 2, 2, 0, -2, -2};
                int a = (int)((tick + (uint32_t)i) % 6u), cx = i ? 151 : 135;
                px_put(SX + cx + SPX[a], SY + 82 + SPY[a], dk); px_put(SX + cx - SPX[a], SY + 82 - SPY[a], dk);
            }
            for (i = 0; i < 5; i++) {                   /* the disc's light going round */
                static const int8_t GX[12] = {0, 5, 9, 10, 9, 5, 0, -5, -9, -10, -9, -5}, GY[12] = {-10, -9, -5, 0, 5, 9, 10, 9, 5, 0, -5, -9};
                int a = (int)((tick + (uint32_t)i) % 12u);
                px_put(SX + 194 + GX[a], SY + 74 + GY[a], i == 2 ? C_HOLO7 : i == 1 || i == 3 ? C_HOLO5 : C_HOLO3);
            }
            for (i = 0; i < 4; i++) {                   /* the mug's steam */
                int sy = 78 - (int)((tick + (uint32_t)i * 3u) % 10u), sx = 5 + i % 2 * 2 + tri((int)tick + i * 3, 6, 2);
                px_put(SX + sx, SY + sy, (tick + (uint32_t)i) % 10u < 6u ? C_LINE : C_LABEL);
            }
        }
        if (sc == 0)                                    /* snow, falling slow */
            for (i = 0; i < 26; i++) {
                uint32_t h = hsh((uint32_t)i * 2654435761u);
                int fx0 = (int)(h % (uint32_t)SC_W) + tri((int)tick + i * 5, 24, 4), fy = (int)((h >> 10) % (uint32_t)SC_H + tick / 2u) % SC_H;
                if (fx0 < SC_W) px_put(SX + fx0, SY + fy, lt);
            }
        for (i = 0; i < DMK_SYNTHS; i++) {              /* the synths: three lights, lit while each sounds */
            int on = syn_peak[i] > 200 || i == ui.synth, lx, ly;
            if (sc == 1) { lx = 39 + i * 6; ly = 74; fill(SX + lx, SY + ly, 3, 3, on ? C_HOLO6 : C_A_INK2);
                if (on) for (y = 0; y < 8; y += 2) if (hsh((uint32_t)(i * 17 + y) + tick) & 1u) px_put(SX + lx + 1, SY + 89 + y, C_HOLO4); }
            else if (sc == 4) {                         /* fairy lights along the curtains' rod */
                lx = 56 + i * 52; ly = 4;
                fill(SX + lx, SY + ly, 3, 3, on ? C_HOLO7 : C_A_INK2);
                if (on) px_put(SX + lx + 1, SY + ly + 3, C_HOLO5);
            } else if (sc == 5) {                       /* the screen's LEDs */
                lx = 88 + i * 5; ly = 81;
                fill(SX + lx, SY + ly, 3, 2, on ? C_HOLO6 : C_LABEL);
            } else if (sc == 3) {                       /* fireflies, wandering over the forest */
                lx = 40 + i * 64 + tri((int)tick + i * 11, 40, 14); ly = 80 + tri((int)tick * 2 + i * 7, 26, 8);
                if (on || (hsh(tick + (uint32_t)i) & 1u)) fill(SX + lx, SY + ly, 2, 2, on ? C_HOLO7 : C_HOLO4);
            } else {                                    /* lanterns along the shore, or the treeline */
                lx = sc == 0 ? 60 + i * 50 : 30 + i * 10; ly = sc == 0 ? 92 : 50 + i * 3 + 18 + (i * 7 % 5);
                fill(SX + lx, SY + ly, 2, 3, on ? C_HOLO7 : C_A_INK2);
            }
        }
        for (y = 0; y < R_CARD; y++)                    /* the card's corners: the paper outside them */
            for (x = 0; x < R_CARD; x++) {
                int dx = R_CARD - x, dy = R_CARD - y;
                if (dx * dx + dy * dy > R_CARD * R_CARD) {
                    px_put(SX + x, SY + y, C_PAPER); px_put(SX + SW - 1 - x, SY + y, C_PAPER);
                    px_put(SX + x, SY + SH - 1 - y, C_PAPER); px_put(SX + SW - 1 - x, SY + SH - 1 - y, C_PAPER);
                }
            }
        for (y = 0; y < SC_H; y++)                      /* held for the frames between */
            for (x = 0; x < SC_W; x += 2) sc_out[y * (SC_W / 2) + x / 2] = fb[((SY + y) * DMK_W + SX + x) >> 1];
    } else
        for (y = 0; y < SC_H; y++)
            for (x = 0; x < SC_W; x += 2) fb[((SY + y) * DMK_W + SX + x) >> 1] = sc_out[y * (SC_W / 2) + x / 2];
}

static void stage_home(void)
{
    int lt = THEME_DARK[theme] ? C_INK : C_PAPER;
    scene_frame(scene_of(ui.synth), ui.now / 140u);
    card(SX, SY, SW, SH, C_LINE, 1);
    text(SX + PAD + 2, SY + 7, DMK_SAMPLES[VS(ui.synth, PG_SOUND, 0, 0)].name, &FS, lt);   /* the sound, on the dark */
}

static void stage_patterns(void)                        /* HOME held: the song's sixteen scenes, a card each */
{
    int i;
    fill(SX, SY, SW, SH, C_PAPER);
    ta_cover(0, SY, DMK_W, Y_FOOT - SY);
    for (i = 0; i < SCENES; i++) {
        char n[3];
        int x = cell_x(SX, SW, 8, i % 8), w = cell_w(SX, SW, 8, i % 8), h = (SH - G) / 2, y = SY + (i / 8) * (h + G);
        int ty = y + (h - 12) / 2 - 1;
        n[0] = (char)(i < 9 ? '1' + i : '1'); n[1] = (char)(i < 9 ? 0 : '0' + i - 9); n[2] = 0;
        if (i == scn_cur) {                             /* playing: a lit card, its number dark on the core */
            round_fill(x, y, w, h, 10, HOLO);
            text_c(x + w / 2, ty, n, &FS, THEME_DARK[theme] ? C_PAPER : C_INK);
        } else if (i == scn_next) { round_line(x, y, w, h, 10, 2, HOLO); text_c(x + w / 2, ty, n, &FS, HOLO); }   /* next */
        else { round_line(x, y, w, h, 10, 1, C_LINE); text_c(x + w / 2, ty, n, &FS, scn[i].used ? C_INK : C_LABEL); }   /* empty: grey */
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
    int i, y;
    for (i = 0; i < w; i++) {                           /* the marks, a column at a time (a graph is 13 000 of them) */
        int x = x0 + i, y0 = ya[i] < cy0 ? cy0 : ya[i], y1 = yb[i] > cy1 ? cy1 : yb[i];
        uint8_t *b = &fb[(y0 * DMK_W + x) >> 1];
        if (x < cx0 || x >= cx1 || y1 <= y0) continue;
        if (x < hx0) hx0 = x;
        if (x > hx1) hx1 = x;
        if (y0 < hy0) hy0 = y0;
        if (y1 - 1 > hy1) hy1 = y1 - 1;
        for (y = y0; y < y1; y++, b += DMK_W / 2) *b = x & 1 ? (uint8_t)((*b & 0xF0) | C_HMARK) : (uint8_t)((*b & 0x0F) | C_HMARK << 4);
    }
    ui_yield();
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
    int i, mid = SY + 76, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, n = sm->blocks;
    card(SX, SY, SW, SH, C_LINE, 1);
    if (ui.row[PG_SOUND]) card_title(SX, SY, SW, "No layer", "Layer", 0);
    else card_title(SX, SY, SW, sm->name, sm->loops && *vp(PG_SOUND, 0, 2) >= 64 ? "Looped" : "One-shot", 0);
    for (i = 0; i < w; i++) {                           /* the sound's own shape: its body lit, its edge in ink */
        int h = sm->env[i * 48 / w] * 34 / 255;
        if (h < 1) h = 1;
        ya[i] = (int16_t)(mid - h); yb[i] = (int16_t)(mid + h + 1);
    }
    lit_band(x0, w, ya, yb);
    for (i = 0; i < w; i++) { px_put(x0 + i, ya[i], C_INK); px_put(x0 + i, yb[i] - 1, C_INK); }
    if (sm->loops && *vp(PG_SOUND, 0, 2) >= 64)         /* the loop's ends, as tall as the sample is there */
        for (i = 0; i < 2; i++) {
            int lx = (i ? sm->loop1 : sm->loop0) * (w - 1) / n, lh = mid - ya[lx] + 5;
            round_fill(x0 + lx - 2, mid - lh, 4, 2 * lh, 2, C_INK);
        }
    if (syn[ui.synth].start) {                          /* where it starts */
        int sx = syn[ui.synth].start * (w - 1) / n;
        vline(x0 + sx, mid - 38, 77, C_INK);
    }
}

/* the filter's response: the passband lit, the cutoff where K1 puts it (a log axis across the card), the resonance a
 * peak there, the slope of its type (ALGORITHM: LP24 steep, LP12 gentle, BP both sides, HP mirrored, Off flat); the
 * envelope's reach a ring along the floor (K3, either way) */
static void stage_filter(void)
{
    int16_t ys[DMK_W], yb[DMK_W];
    int x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, base = SY + SH - 14, top = SY + 12, ly = top + 34, i;
    int cut = *vp(PG_FILTER, 0, 0), q = *vp(PG_FILTER, 0, 1) * 30 / 127, env = *vp(PG_FILTER, 0, 2) - 64;
    int type = *vp(PG_FILTER, 0, 4) * 8 / 128, fc = x0 + cut * (w - 1) / 127, ex, f1 = fc, f2 = fc;
    card(SX, SY, SW, SH, C_LINE, 1);
    if (type == FT_VOWEL) {                             /* the vowel's two formants, where the cutoff puts them */
        int pos = (cut * 2 > 255 ? 255 : cut * 2) * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
        f1 = x0 + ((VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8) * (w - 1) / 255;
        f2 = x0 + ((VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8) * (w - 1) / 255;
    }
    for (i = 0; i < w; i++) {
        int x = x0 + i, dx = x - fc, ad = dx < 0 ? -dx : dx, y = ly;
        if (type == FT_LP24 || type == FT_LP12 || type == FT_BP) { if (dx > 0) y = ly + dx * (type == FT_LP24 ? 4 : type == FT_LP12 ? 2 : 3); }
        if ((type == FT_HP || type == FT_BP) && dx < 0) y = ly + ad * (type == FT_HP ? 4 : 3);
        if (type <= FT_BP && ad < 16) y -= q * (16 - ad) / 16;   /* the resonance: a peak at the cutoff */
        if (type == FT_NOTCH && ad < 22 - q / 2) y = ly + (22 - q / 2 - ad) * 4;
        if (type == FT_PEAK && ad < 20) y = ly - (20 - ad) * (8 + q) / 20;
        if (type == FT_VOWEL) {
            int a1 = x - f1 < 0 ? f1 - x : x - f1, a2 = x - f2 < 0 ? f2 - x : x - f2;
            y = ly + 26;
            if (a1 < 14) y -= (14 - a1) * (30 + q) / 14;
            if (a2 < 14) y -= (14 - a2) * (22 + q) / 14;
        }
        ys[i] = (int16_t)(y < top ? top : y > base ? base : y);
        yb[i] = (int16_t)(base + 1);
    }
    lit_band(x0, w, ys, yb);
    ink_line(x0, w, ys);
    ex = fc + env * (w - 1) / 127;                      /* where the envelope takes the cutoff */
    if (ex < x0) ex = x0;
    if (ex > x0 + w - 1) ex = x0 + w - 1;
    if (env && type != FT_OFF) { hline(ex < fc ? ex : fc, base - 2, (ex < fc ? fc - ex : ex - fc) + 1, C_INK); round_line(ex - 4, base - 6, 9, 9, 4, 1, lit(2) ? HOLO : C_INK); }
    if (type != FT_OFF) round_fill(fc - 4, ys[fc - x0] - 4, 9, 9, 4, lit(0) || lit(1) ? HOLO : C_INK);
}

/* the envelope: attack, decay, sustain, release as their own widths (each up to a quarter of the card), the peak as
 * high as the level (ALGORITHM); its body lit under the line, a point at each knob's corner */
static void stage_amp(void)
{
    int16_t ys[DMK_W], yb[DMK_W];
    int x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, base = SY + SH - 14, top = SY + 14, i;
    int peak = base - (base - top) * *vp(PG_AMP, 0, 4) / 127;
    int a = 2 + *vp(PG_AMP, 0, 0) * 56 / 127, d = 2 + *vp(PG_AMP, 0, 1) * 56 / 127, r = 2 + *vp(PG_AMP, 0, 3) * 56 / 127;
    int sy = base - (base - peak) * *vp(PG_AMP, 0, 2) / 127, hold = w - 1 - a - d - r;
    int pts[5][2];
    pts[0][0] = x0; pts[0][1] = base; pts[1][0] = x0 + a; pts[1][1] = peak; pts[2][0] = x0 + a + d; pts[2][1] = sy;
    pts[3][0] = x0 + a + d + hold; pts[3][1] = sy; pts[4][0] = x0 + w - 1; pts[4][1] = base;
    card(SX, SY, SW, SH, C_LINE, 1);
    for (i = 0; i < w; i++) { ys[i] = (int16_t)base; yb[i] = (int16_t)(base + 1); }
    for (i = 0; i < 4; i++) {
        int k, n = pts[i + 1][0] - pts[i][0];
        for (k = 0; k <= n && pts[i][0] + k - x0 < w; k++)
            ys[pts[i][0] + k - x0] = (int16_t)(pts[i][1] + (n ? (pts[i + 1][1] - pts[i][1]) * k / n : 0));
    }
    lit_band(x0, w, ys, yb);
    ink_line(x0, w, ys);
    for (i = 1; i < 4; i++) round_fill(pts[i][0] - 4, pts[i][1] - 4, 9, 9, 4, lit(i - 1) || (i == 3 && lit(3)) ? HOLO : C_INK);
}

/* the LFO: its wave across the card, as many cycles as the rate (1..8), as tall as the depth, of its shape (sine,
 * triangle, ramp, square, random), rising from nothing over the fade; its body lit from the middle to the wave */
static void stage_lfo(void)
{
    int16_t ya[DMK_W], yb[DMK_W], ys[DMK_W];
    int x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, mid = SY + SH / 2, i;
    int cyc = 1 + *vp(PG_LFO, 0, 0) * 7 / 127, amp = 3 + *vp(PG_LFO, 0, 1) * 44 / 127;
    int shape = *vp(PG_LFO, 0, 2) * 5 / 128, fade = *vp(PG_LFO, 0, 3) * (w - 1) / 127;
    card(SX, SY, SW, SH, C_LINE, 1);
    for (i = 0; i < w; i++) {
        int ph = i * cyc * 256 / w, p = ph & 255, v, g;   /* v: -128..128 */
        if (shape == 0) { int t = p & 127; v = 4 * t * (128 - t) / 128; if (p >= 128) v = -v; }   /* sine, near enough */
        else if (shape == 1) v = p < 128 ? -128 + p * 2 : 384 - p * 2;
        else if (shape == 2) v = 128 - p;
        else if (shape == 3) v = p < 128 ? 128 : -128;
        else { uint32_t hsh = (uint32_t)(ph >> 8) * 2654435761u; v = (int)(hsh >> 24) - 128; }
        g = fade && i < fade ? i * 256 / fade : 256;    /* the fade: from nothing to its depth */
        ys[i] = (int16_t)(mid - v * amp / 128 * g / 256);
        ya[i] = ys[i] < mid ? ys[i] : (int16_t)mid;
        yb[i] = (int16_t)((ys[i] > mid ? ys[i] : mid) + 1);
    }
    hline(x0, mid, w, C_LINE);
    lit_band(x0, w, ya, yb);
    ink_line(x0, w, ys);
    if (fade) round_fill(x0 + fade - 4, mid - 4, 9, 9, 4, lit(3) ? HOLO : C_INK);
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
        if ((page == PG_FX && i < 3) || page == PG_GLOBAL) {   /* the row's effect, by name, left of its switch */
            char v[16];
            val_text(v, page, i, 4);
            text_r(tx - 8, y + (h - 12) / 2, v, &FS, row_off(page, i) ? C_LABEL : C_INK);
        }
        if (row_off(page, i)) {                             /* the switch: off */
            round_line(tx, y + h / 2 - 6, 22, 12, 6, 1, C_LABEL);
            round_fill(tx + 3, y + h / 2 - 3, 6, 6, 3, C_LABEL);
        } else {                                            /* on: lit, the knob to the right */
            round_fill(tx, y + h / 2 - 6, 22, 12, 6, HOLO);
            round_fill(tx + 13, y + h / 2 - 3, 6, 6, 3, C_PAPER);
        }
    }
}

static void stage_chord(void)                           /* the progression: its chords as pills, the one now lit, named */
{
    const prog_t *pr = cur_prog();
    char sub[32], nm[16];
    int i, y = SY + SH - 30, n = pr->n, writing = !seq_on && seq_rec, now = ch_shown >= 0 ? ch_shown : 0;
    int focus = writing ? ch_cur : now, cells = n + (writing && n < PROG_SLOTS), show = cells > 5 ? 4 : cells, first = 0, widest = 0, s, on_n = 0;
    char *o;
    for (i = 0; i < n; i++) { char t[16]; t[0] = 0; chord_name_q(t, seq_key + pr->s[i].root, pr->s[i].q, QS); if (text_w(t, &FS) > widest) widest = text_w(t, &FS); }
    while (show > 3 && widest > cell_w(SX + 4, SW - 8, show, 0) - 8) show--;   /* long names: fewer pills, in a window */
    card(SX, SY, SW, SH, C_LINE, 1);
    o = str_cat(str_cat(str_cat(sub, KEYS[seq_key]), " "), MODENAMES[seq_mode]);   /* and which synths play it */
    for (s = 0; s < DMK_SYNTHS; s++) if (ch.follow[s] == FW_PLAYS) { o = str_cat(o, on_n++ ? (char[]){'+', (char)('1' + s), 0} : (char[]){',', ' ', 'o', 'n', ' ', (char)('1' + s), 0}); }
    if (!on_n) str_cat(o, ", on none");
    card_title(SX, SY, SW, pr->name, n ? sub : "Empty: REC, then the keys", 0);
    nm[0] = 0;
    if (ch_shown == -1 && n) chord_name(nm, seq_key + pr->s[0].root, pr->s[0].q);
    else str_cat(nm, ch_name);
    if (nm[0]) text_c(DMK_W / 2, SY + 40, nm, &FN, C_INK);
    if (cells > show) { first = focus - 1; if (first > cells - show) first = cells - show; if (first < 0) first = 0; }
    for (i = 0; i < show; i++) {
        int j = first + i, x = cell_x(SX + 4, SW - 8, show, i), w = cell_w(SX + 4, SW - 8, show, i);
        int on = !writing && ch_shown != -2 && j == now;
        char t[16];
        if (j >= n) { round_line(x, y, w, 24, 12, 1, C_LINE); text_c(x + w / 2, y + 6, "+", &FS, C_LABEL); }   /* the next, empty */
        else {
            t[0] = 0;
            chord_name_q(t, seq_key + pr->s[j].root, pr->s[j].q, QS);
            if (on) round_fill(x, y, w, 24, 12, HOLO);
            text_fit(x + 4 + (text_w(t, &FS) < w - 8 ? (w - 8 - text_w(t, &FS)) / 2 : 0), y + 6, w - 8, t, &FS, on ? C_INK : C_LABEL);
        }
        if (writing && j == ch_cur) round_line(x - 1, y - 1, w + 2, 26, 13, 2, C_INK);   /* the cursor */
    }
}

/* ARRANGE (HOME held + SEQ): the song as a strip of steps, five in view around the cursor (its ring); the one playing
 * lit; under its pattern's number its bars as ticks and the synths it plays as dots (an empty ring: muted); a "+"
 * card after the last, where a key adds one. The card's title: the mode, and the song's length */
static void arr_scene(void)                             /* the scene open: each synth a lane, its pattern and its notes */
{
    char sub[40], *o = sub;
    int s, b = scn_bars(scn_cur), y0 = SY + 44, lh = (SH - 50) / 3;
    o = fmt_int(o, b); o = str_cat(o, b == 1 ? " bar, " : " bars, ");
    str_cat(o, PROGS[choice_index(choice_of(PG_CHORD, 0, 4), ui.val[0][PG_CHORD][0][4])]);
    { char t[12]; fmt_int(str_cat(t, "Scene "), scn_cur + 1); card(SX, SY, SW, SH, C_LINE, 1); card_title(SX, SY, SW, t, sub, 0); }
    for (s = 0; s < DMK_SYNTHS; s++) {
        int y = y0 + s * lh, p = syn_pat(s), muted = pat_mute >> s & 1u, sel = s == ui.synth, i, x0 = SX + 70, w = SW - 70 - 10;
        int steps = sq[s].steps ? sq[s].steps : 16;
        char n[2] = {(char)('1' + p), 0};
        {                                               /* the sound's first word */
            char w[12];
            const char *nm = DMK_SAMPLES[VS(s, PG_SOUND, 0, 0)].name;
            int i2;
            for (i2 = 0; i2 < 11 && nm[i2] && nm[i2] != ' '; i2++) w[i2] = nm[i2];
            w[i2] = 0;
            text_fit(SX + PAD + 2, y + 4, 32, w, &FS, muted ? C_LINE : C_LABEL);
        }
        if (sel) round_fill(SX + 46, y + 1, 18, lh - 4, 6, HOLO);
        else round_line(SX + 46, y + 1, 18, lh - 4, 6, 1, C_LINE);
        text_c(SX + 55, y + 4, n, &FS, sel ? (THEME_DARK[theme] ? C_PAPER : C_INK) : muted ? C_LINE : C_INK);
        if (harm_of(s) >= 0) {                          /* in harmony: the lead's notes, not its own */
            char t[16];
            fmt_int(str_cat(t, "Harmony of "), harm_of(s) + 1);
            text(x0, y + 4, t, &FS, C_LABEL);
            continue;
        }
        hline(x0, y + lh / 2 - 1, w, C_LINE);           /* the pattern's length, its notes on it */
        for (i = 0; i < sq[s].count; i++) {
            int nx = x0 + sq[s].n[i].step * (w - 3) / steps, nw = sq[s].n[i].len * w / steps;
            fill(nx, y + lh / 2 - 3, nw < 2 ? 2 : nw > w - (nx - x0) ? w - (nx - x0) : nw, 4, muted ? C_LINE : C_INK);
        }
        if (spat_next[s] < SYN_PATS) { char q[3] = {(char)(0x31 + spat_next[s]), 0, 0}; text_r(SX + SW - 6, y + 4, q, &FS, HOLO); }   /* waiting */
    }
}
static void arr_song(void)                              /* the song: its steps, five in view around the cursor */
{
    int i, cells = 5, first, bars = 0, secs, playing = song_playing() ? song_at - 1 : -1;
    char sub[40], *o = sub;
    for (i = 0; i < song_n; i++) bars += song[i].bars;
    secs = bars * 4 * 60 / (seq_bpm ? seq_bpm : 72);
    if (song_n) {
        o = fmt_int(o, song_n); o = str_cat(o, song_n == 1 ? " step, " : " steps, ");
        o = fmt_int(o, bars); o = str_cat(o, bars == 1 ? " bar, " : " bars, ");
        o = fmt_int(o, secs / 60); *o++ = ':'; *o++ = (char)('0' + secs % 60 / 10); *o++ = (char)('0' + secs % 10); *o = 0;
    } else str_cat(o, "Empty: a scene key adds a step");
    card(SX, SY, SW, SH, C_LINE, 1);
    card_title(SX, SY, SW, song_mode ? "Song mode" : "Scene mode", sub, 0);
    first = ui.arr_cur - 2;
    if (first > song_n + 1 - cells) first = song_n + 1 - cells;
    if (first < 0) first = 0;
    for (i = 0; i < cells && first + i <= song_n; i++) {
        int j = first + i, x = cell_x(SX + 6, SW - 12, cells, i), w = cell_w(SX + 6, SW - 12, cells, i), y = SY + 50, h = 58, k;
        if (j == song_n) { round_line(x, y, w, h, 12, 1, C_LINE); text_c(x + w / 2, y + 18, "+", &FM, C_LABEL); }
        else {
            const sstep_t *t = &song[j];
            char n[3];
            int lit_ = j == playing, ink = lit_ ? (THEME_DARK[theme] ? C_PAPER : C_INK) : C_INK, m = scn[t->scene].mute;
            n[0] = (char)(t->scene < 9 ? '1' + t->scene : '1'); n[1] = (char)(t->scene < 9 ? 0 : '0' + t->scene - 9); n[2] = 0;
            if (lit_) round_fill(x, y, w, h, 12, HOLO);
            else round_line(x, y, w, h, 12, 1, C_LINE);
            text_c(x + w / 2, y + 6, n, &FM, ink);
            for (k = 0; k < t->bars; k++) fill(x + (w - (t->bars * 4 - 2)) / 2 + k * 4, y + 36, 2, 5, lit_ ? ink : C_LABEL);   /* its bars */
            for (k = 0; k < DMK_SYNTHS; k++) {          /* its scene's synths: a dot each, a ring when muted */
                int dx = x + w / 2 - 9 + k * 8, dy = y + 46;
                if (m >> k & 1u) round_line(dx, dy, 5, 5, 2, 1, lit_ ? ink : C_LABEL);
                else round_fill(dx, dy, 5, 5, 2, ink);
            }
        }
        if (j == ui.arr_cur) round_line(x - 2, y - 2, w + 4, h + 4, 14, 2, C_INK);   /* the cursor */
    }
}
static void stage_arrange(void) { if (ui.row[PG_ARR]) arr_song(); else arr_scene(); }

/* SONGS (SAVE held 1 s): the store's fourteen slots, four in view around the one K1 picks (its outline lit); the song
 * open has a dot; an empty slot is grey. PLAY loads the slot picked (an empty one starts the first song again), SAVE
 * saves there, OCT- twice empties it */
static void stage_songs(void)
{
    int i, h = (SH - 3 * G) / 4, first = ui.song_pick < 1 ? 0 : ui.song_pick - 1;
    if (first > ST_SONGS - 4) first = ST_SONGS - 4;
    for (i = 0; i < 4; i++) {
        int n = first + i, y = SY + i * (h + G), used = song_used(n), pick = n == ui.song_pick;
        char t[3] = {(char)('0' + (n + 1) / 10), (char)('0' + (n + 1) % 10), 0};
        if (pick) round_line(SX, y, SW, h, h / 2, 2, HOLO);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 12) / 2, t, &FS, C_LABEL);
        text_fit(SX + 40, y + (h - 20) / 2 - 1, SW - 70, used ? song_name(n) : "Empty", &FM, used ? C_INK : C_LABEL);
        if (n == ui.song && used) round_fill(SX + SW - 22, y + h / 2 - 3, 6, 6, 3, C_INK);   /* the song open */
    }
}

/* the boot: a landscape, full screen and twice the size (its bits plain), the clouds drifting and the lights on;
 * then it goes out bit by bit, by the dither, to the dark ground while the word arrives through the same dither,
 * stacked in Kiloy ("dem" over "ake"), and heats from the blue rim to the pale core; it holds while a meter of ticks
 * fills; then the screen leaves in 16 px blocks, Home under it. A start shows one of the four scenes (dmk_init's
 * seed). A key or a button ends it */
#define BOOT_NAME 1300u                                 /* the landscape alone until here */
#define BOOT_ARRIVE 2100u                               /* the word whole, the landscape gone */
#define BOOT_HEAT0 1450u
#define BOOT_HEAT1 2500u
#define BOOT_LEAVE 2900u
#define BOOT_END 3450u
static uint8_t boot_fb[DMK_FB_BYTES] POOL;             /* the held frame, while Home comes through it */
static uint8_t boot_sc;                                 /* the scene this start shows */

static void land_full(int sc, uint32_t t)               /* a scene, its middle 120 x 118 doubled over the screen */
{
    static const uint8_t CROP[DMK_SCENES] = {54, 42, 90, 90, 56, 40};   /* the peak; the cabin, the sun; the stacks; the moon;
                                                                     * the window; the screen and the tape */
    int x, y, cx;
    sc = sc < DMK_SCENES ? sc : 0;
    cx = CROP[sc];
    scene_frame(sc, t / 140u);
    for (y = 0; y < DMK_H; y++) {
        const uint8_t *src = &sc_out[(y / 2 < SC_H ? y / 2 : SC_H - 1) * (SC_W / 2)];
        uint8_t *b = &fb[y * (DMK_W / 2)];
        for (x = 0; x < DMK_W / 2; x++) {               /* a byte: two screen pixels, one scene pixel */
            int sx = cx + x, n = sx & 1 ? src[sx >> 1] & 15 : src[sx >> 1] >> 4;
            b[x] = (uint8_t)(n << 4 | n);
        }
    }
}
static void boot_land(uint32_t t) { land_full(boot_sc, t); }

static void boot(uint32_t t)
{
    int dark = THEME_DARK[theme], gnd = dark ? C_PAPER : C_INK, lit = dark ? C_INK : C_PAPER;
    int i, at, y1 = 50, y2 = y1 + FK.ascent + 12;
    if (t < BOOT_NAME) { boot_land(t); return; }
    if (t < BOOT_ARRIVE) {                              /* the landscape going out in 2 px bits, to the ground */
        int q = (int)((t - BOOT_NAME) * 17u / (BOOT_ARRIVE - BOOT_NAME)), x, y;
        boot_land(t);
        for (y = 0; y < DMK_H; y += 2)
            for (x = 0; x < DMK_W; x += 2)
                if (T_BAYER6[0][((y >> 1) & 3) * 4 + ((x >> 1) & 3)] < q) { fb[(y * DMK_W + x) >> 1] = (uint8_t)(gnd << 4 | gnd); fb[((y + 1) * DMK_W + x) >> 1] = (uint8_t)(gnd << 4 | gnd); }
        for (i = 0; i < DMK_FB_BYTES; i++) boot_fb[i] = fb[i];   /* what the word's bits not yet here show */
        holo_under = boot_fb;
    } else
        fill(0, 0, DMK_W, DMK_H, gnd);
    holo_veil = t >= BOOT_ARRIVE ? 256 : (int)((t - BOOT_NAME) * 256u / (BOOT_ARRIVE - BOOT_NAME));
    holo_heat = t <= BOOT_HEAT0 ? 0 : t >= BOOT_HEAT1 ? 255 : (int)((t - BOOT_HEAT0) * 112u / (BOOT_HEAT1 - BOOT_HEAT0));
    holo_bg = gnd;
    text_c(DMK_W / 2, y1, "dem", &FK, HOLO);
    text_c(DMK_W / 2, y2, "ake", &FK, HOLO);
    holo_veil = 256;
    holo_heat = 255;
    holo_under = 0;
    at = (int)(((t < BOOT_LEAVE ? t : BOOT_LEAVE) - BOOT_NAME) * 40u / (BOOT_LEAVE - BOOT_NAME));
    for (i = 0; i <= 40; i++) {                         /* the meter: ticks lit up to where it has reached */
        int x = 59 + i * 3;
        if (t < BOOT_ARRIVE && T_BAYER6[0][(i & 3) * 4 + 2] * 16 >= (int)((t - BOOT_NAME) * 256u / (BOOT_ARRIVE - BOOT_NAME))) continue;
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
    if (ui.btn_held >> B_OCTD & 1u && ui.btn_held >> B_OCTU & 1u) {   /* both held half a second: the landscape, or back */
        if (!ui.full_latch && ms - ui.both_at > 500u) {
            ui.full = (uint8_t)!ui.full; ui.full_latch = 1;
            if (ui.full) notice("OCT\xE2\x88\x92 OCT+ held: back");
        }
    } else ui.full_latch = 0;
    if (ui.full) {                                      /* the landscape full screen, the current synth's, moving */
        land_full(scene_of(ui.synth), ms);
        ta_cover(0, 0, DMK_W, DMK_H);
        overlays();
        return fb;
    }
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    head();
    ui_yield();
    fields_band();
    ui_yield();
    if (ui.btn_held >> B_HOME & 1u && ui.now - ui.btn_down_at[B_HOME] > 350u) stage_patterns();
    else switch (ui.page) {
    case PG_HOME: stage_home(); break;
    case PG_ROLL: stage_roll(); break;
    case PG_SOUND: stage_sound(); break;
    case PG_FILTER: stage_filter(); break;
    case PG_AMP: stage_amp(); break;
    case PG_FX: stage_rows(PG_FX, PAGE[PG_FX].row, 4, ui.row[PG_FX]); break;
    case PG_CHORD: stage_chord(); break;
    case PG_GLOBAL: stage_rows(PG_GLOBAL, PAGE[PG_GLOBAL].row, 4, ui.row[PG_GLOBAL]); break;
    case PG_LFO: stage_lfo(); break;
    case PG_ARR: stage_arrange(); break;
    default: stage_songs(); break;
    }
    ui_yield();
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
        if ((b == B_OCTD || b == B_OCTU) && ui.page == PG_CHORD) {
            int h;
            for (h = 0; h < DMK_KEYS && !(ui.keys_down[h] && ui.key_slot[h]); h++) {}
            if (h < DMK_KEYS) {                         /* a written chord held: OCT- takes it away, OCT+ copies it after */
                if (b == B_OCTD) { ch_cur = (uint8_t)(ui.key_slot[h] - 1); slot_delete(ch_cur); ui.key_slot[h] = 0; }   /* the cursor stays */
                else ui.key_slot[h] = (uint8_t)(slot_copy(ui.key_slot[h] - 1) + 1);
                ui.btn_used |= bit;
                return;
            }
            if (b == B_OCTU && ui.btn_held >> B_ARP & 1u) {   /* ARP held, OCT+: printed into this synth's roll */
                print_to_roll(ui.synth);
                notice("Printed to the roll");
                ui.btn_used |= bit | (uint16_t)(1u << B_ARP);
                return;
            }
        }
        if ((b == B_OCTD || b == B_OCTU) && ui.page == PG_ROLL && roll_held() >= 0) {
            roll_oct(b == B_OCTU);                      /* a held note: OCT- takes it away, OCT+ adds a copy */
            ui.btn_used |= bit;
            return;
        }
        if ((b == B_OCTD || b == B_OCTU) && ui.btn_held >> (b == B_OCTD ? B_OCTU : B_OCTD) & 1u) {
            /* OCT- and OCT+ together: held, the landscape full screen (ui_frame); the octave left as it was */
            ui.octave = ui.oct_before;
            ui.btn_used |= (uint16_t)(1u << B_OCTD | 1u << B_OCTU);
            ui.both_at = ui.now;
            return;
        }
        if (b == B_OCTD || b == B_OCTU) ui.oct_before = ui.octave;
        if (b != B_HOME && ui.btn_held >> B_HOME & 1u && (b == B_SEQ || b == B_PLAY || b == B_OCTU)) {
            /* HOME held: SEQ opens Arrange; PLAY switches pattern mode and song mode; OCT+, then a pattern key, copies */
            ui.btn_used |= (uint16_t)(bit | 1u << B_HOME);
            if (b == B_SEQ) { ui.page = PG_ARR; return; }
            if (b == B_PLAY) {
                song_mode ^= 1u;
                if (song_mode && seq_on) song_start(0);    /* playing: the song from its first step, at the next bar */
                notice(!song_mode ? "Scene mode" : song_n ? "Song mode" : "Song mode: an empty song");
                return;
            }
            ui.copy_arm = 1; ui.copy_to = 0xFF;
            notice("Copy: a scene key");
            return;
        }
        if ((b == B_OCTD || b == B_OCTU) && ui.page == PG_ARR && !ui.row[PG_ARR]) {
            /* the Scene row: OCT+ copies the synth's pattern into its next empty one, now the scene's (a variation);
             * OCT- twice clears it */
            char m[28];
            ui.btn_used |= bit;
            if (b == B_OCTU) {
                int from = syn_pat(ui.synth), q = spat_dup(ui.synth);
                if (q < 0) { notice("Eight patterns: all in use"); return; }
                fmt_int(str_cat(fmt_int(str_cat(m, "Pattern "), from + 1), " to "), q + 1);
                notice(m);
                return;
            }
            if (ui.clear_arm == 4 && ui.now - ui.clear_at < 2000u) { seq_quiet(ui.synth); sq[ui.synth].count = 0; ui.clear_arm = 0; notice("Pattern cleared"); }
            else { ui.clear_arm = 4; ui.clear_at = ui.now; notice("OCT\xE2\x88\x92 again: clear"); }
            return;
        }
        if ((b == B_OCTD || b == B_OCTU) && ui.page == PG_ARR) {   /* the Song row: OCT- deletes the step, OCT+ copies it after */
            int at = ui.arr_cur, i;
            ui.btn_used |= bit;
            if (at >= song_n) return;
            if (b == B_OCTD) { for (i = at; i < song_n - 1; i++) song[i] = song[i + 1]; song_n--; }
            else if (song_n < SONG_STEPS) { for (i = song_n; i > at + 1; i--) song[i] = song[i - 1]; song[at + 1] = song[at]; song_n++; ui.arr_cur = (uint8_t)(at + 1); }
            else notice("128 steps at most");
            return;
        }
        if (b == B_SAVE && ui.btn_held >> B_EDIT & 1u) {   /* EDIT held, SAVE: the sound into the library */
            char t[24];
            int n;
            ui.btn_used |= bit;
            if (!store_on()) { notice("No store here"); return; }
            if (seq_on) { notice("Stop to save"); return; }
            n = sound_save(ui.synth);
            if (n <= 0) { notice(n ? "Not saved" : "The library is full"); return; }
            fmt_int(str_cat(t, n < 10 ? "Saved as User 0" : "Saved as User "), n);
            notice(t);
            ui.preset[ui.synth] = (uint8_t)(DMK_SAMPLES_N + lib_count() - 1);
            return;
        }
        if (b == B_OCTD && ui.btn_held >> B_EDIT & 1u) {   /* EDIT held, OCT-: INIT, the recording kept */
            sound_init(ui.synth);
            ui.preset[ui.synth] = ui.val[ui.synth][PG_SOUND][0][0];
            ui.btn_used |= bit;
            notice("Init");
            return;
        }
        if (b == B_OCTD && ui.page == PG_DISCS) {       /* SONGS, OCT- twice: the slot emptied */
            if (!song_used(ui.song_pick)) { notice("Empty already"); return; }
            if (ui.clear_arm == 3 && ui.now - ui.clear_at < 2000u) {
                notice(song_erase(ui.song_pick) ? "Not emptied" : "Slot emptied");
                ui.clear_arm = 0;
            } else { ui.clear_arm = 3; ui.clear_at = ui.now; notice("OCT\xE2\x88\x92 again: empty it"); }
            return;
        }
        if (b == B_OCTD && (ui.btn_held >> B_SEQ & 1u || ui.btn_held >> B_HOME & 1u)) {
            /* SEQ held, OCT- twice: this synth's roll cleared; HOME held (the patterns), all three. Twice, as
             * nothing is saved yet to bring them back */
            int all = (int)(ui.btn_held >> B_HOME & 1u), what = 1 + all, s;
            ui.btn_used |= (uint16_t)(bit | 1u << (all ? B_HOME : B_SEQ));
            if (ui.clear_arm == what && ui.now - ui.clear_at < 2000u) {
                for (s = 0; s < DMK_SYNTHS; s++) if (all || s == ui.synth) { seq_quiet(s); sq[s].count = 0; }
                notice(all ? "Rolls cleared" : "Roll cleared");
                ui.clear_arm = 0;
            } else {
                ui.clear_arm = (uint8_t)what;
                ui.clear_at = ui.now;
                notice(all ? "OCT\xE2\x88\x92 again: all three" : "OCT\xE2\x88\x92 again: clear");
            }
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
    if (b == B_HOME) ui.copy_arm = 0;                   /* the copy waits only while HOME is held */
    if (ui.btn_used & bit) return;                      /* a modifier that was used does nothing on release */
    if (ui.now - ui.btn_down_at[b] > 1000u && b == B_SAVE) { ui.page = PG_DISCS; ui.song_pick = ui.song; return; }
    if (ui.now - ui.btn_down_at[b] > 350u) return;      /* a hold showed its layer: no tap */
    if (b == B_SAVE) {                                  /* the song into its slot (SONGS: the slot picked) */
        char t[24];
        int at = ui.page == PG_DISCS ? ui.song_pick : ui.song;
        if (!store_on()) { notice("No store here"); return; }
        if (seq_on) { notice("Stop to save"); return; }
        if (ui.page == PG_DISCS && song_used(at) && at != ui.song && !(ui.song_arm && ui.now - ui.song_arm_at < 2000u)) {
            ui.song_arm = 1; ui.song_arm_at = ui.now;   /* over another song: twice */
            notice("SAVE again: over it");
            return;
        }
        ui.song_arm = 0;
        if (song_save(at)) { notice("Not saved"); return; }
        ui.song = (uint8_t)at;
        fmt_int(str_cat(t, at < 9 ? "Saved in song 0" : "Saved in song "), at + 1);
        notice(t);
        return;
    }
    if (b == B_PLAY && ui.page == PG_DISCS) {           /* SONGS, PLAY: the slot picked loaded (empty: a new song) */
        int at = ui.song_pick, s;
        if (song_used(at)) { if (song_load(at)) { notice("Not loaded"); return; } notice("Loaded"); }
        else { song_new(); notice("A new song"); }
        ui.song = (uint8_t)at;
        for (s = 0; s < DMK_SYNTHS; s++) ui.preset[s] = ui.val[s][PG_SOUND][0][0];
        ui.page = PG_HOME;
        return;
    }
    if (b == B_PLAY) {
        if (!seq_on && ui.page == PG_ARR) {             /* Arrange: the song from the cursor; REC armed, recorded */
            if (seq_rec) {
                song_rec = 1; rec_from = (uint8_t)(ui.arr_cur < song_n ? ui.arr_cur : song_n); rec_bars = 0;
                notice("Recording the song");
            } else {
                if (!song_n) { notice("The song is empty"); return; }
                song_mode = 1;
                song_start(ui.arr_cur);
            }
        } else if (!seq_on && song_mode && song_n) song_start(0);   /* song mode: from the first step */
        seq_play(!seq_on);
        return;
    }
    if (b == B_REC) { seq_rec ^= 1u; notice(seq_rec ? "Recording on" : "Recording off"); return; }
    if (BTN_PAGE[b] >= 0 && ui.full) { ui.full = 0; return; }   /* the landscape: a page button, back to the page */
    if (BTN_PAGE[b] >= 0) {
        int p = BTN_PAGE[b];
        if (ui.page == p) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);   /* again: the next row built */
        else {
            ui.page = (uint8_t)p;
            if (row_soon(p, ui.row[p])) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);
            if (rows_built(p) > 1 && !(ui.hinted >> p & 1u)) {   /* the first time: how to reach the next row */
                char t[32];
                if (p == PG_FX || p == PG_GLOBAL) str_cat(t, "ALGORITHM: the rows");
                else str_cat(str_cat(t, BTN_NAME[b]), " again: the next row");
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
    ui.full = 0;                                        /* the landscape: a knob turned, back to the page it turns */
    if (ui.btn_held) {                                  /* a held button's layer: it was used */
        int b;
        for (b = 0; b < B_COUNT; b++) if (ui.btn_held >> b & 1u) ui.btn_used |= (uint16_t)(1u << b);
    }
    if (e == E_SELECT) { int s = ui.synth + (d > 0 ? 1 : -1); ui.synth = (uint8_t)(s < 0 ? 0 : s >= DMK_SYNTHS ? DMK_SYNTHS - 1 : s); sp_sync(); return; }
    if (e == E_PRESET) {                                /* the synth's sound: the next recording, then the library's */
        uint8_t *sv = &ui.val[ui.synth][PG_SOUND][0][0], *pr = &ui.preset[ui.synth];
        int tot = DMK_SAMPLES_N + lib_count(), n;
        if (*pr < DMK_SAMPLES_N || *pr >= tot) *pr = *sv;   /* from the recording it plays, if not one of the library's */
        n = *pr + (d > 0 ? 1 : -1);
        *pr = (uint8_t)(n < 0 ? tot - 1 : n >= tot ? 0 : n);
        if (*pr < DMK_SAMPLES_N) { *sv = *pr; notice(DMK_SAMPLES[*sv].name); }
        else {
            char t[16];
            int slot = lib_nth(*pr - DMK_SAMPLES_N);
            lib_load(ui.synth, slot);
            fmt_int(str_cat(t, slot < 9 ? "User 0" : "User "), slot + 1);
            notice(t);
        }
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
    if (k == 1 && ui.btn_held >> B_SEQ & 1u) {          /* SEQ held, K2: the harmony's interval, in scale steps */
        static const char *const IV[8] = {"Unison", "A 2nd", "A 3rd", "A 4th", "A 5th", "A 6th", "A 7th", "An octave"};
        char t[24];
        int h = ch.hint[ui.synth] + (d > 0 ? 1 : -1);
        ch.hint[ui.synth] = (int8_t)(h < -7 ? -7 : h > 7 ? 7 : h);
        h = ch.hint[ui.synth];
        str_cat(str_cat(t, IV[h < 0 ? -h : h]), h > 0 ? " above" : h < 0 ? " below" : "");
        notice(t);
        return;
    }
    if (k == 4 && ui.btn_held >> B_SEQ & 1u) {          /* SEQ held, ALGORITHM: how this synth follows the chords */
        char t[32];
        int dir = d > 0 ? 1 : -1, f = ch.follow[ui.synth] + dir;
        if (f == FW_HARM1 + ui.synth) f += dir;          /* not in harmony with itself */
        if (f < 0 || f >= FW_COUNT) return;
        ch.follow[ui.synth] = (uint8_t)f;
        if (ch.follow[ui.synth] != FW_PLAYS) chord_off(ui.synth);
        str_cat(t, FOLLOWS[ch.follow[ui.synth]]);
        notice(t);
        return;
    }
    if (ui.page == PG_ROLL) { roll_turn(k, d); return; }
    if (ui.page == PG_ARR) {
        /* ARRANGE: ALGORITHM the rows. The Scene row: K1-K3 each synth's pattern, K4 the progression. The Song row: K1
         * the step, K2 its scene, K3 its bars, K4 the end */
        int dir = d > 0 ? 1 : -1, c;
        sstep_t *t = ui.arr_cur < song_n ? &song[ui.arr_cur] : 0;
        if (k == 4) { ui.row[PG_ARR] = (uint8_t)!ui.row[PG_ARR]; return; }
        if (!ui.row[PG_ARR]) {
            if (k < DMK_SYNTHS) {
                c = (spat_next[k] < SYN_PATS ? spat_next[k] : syn_pat(k)) + dir;
                if (c >= 0 && c < SYN_PATS) pat_pick_syn(k, c);
            } else prog_step(dir);
            return;
        }
        if (k == 0) { c = ui.arr_cur + dir; ui.arr_cur = (uint8_t)(c < 0 ? 0 : c > song_n ? song_n : c); }
        else if (k == 3) song_end = (uint8_t)((song_end + SE_COUNT + dir) % SE_COUNT);
        else if (k == 2) {                              /* a step's bars; past the last, the next one's */
            c = (t ? t->bars : ui.arr_bars) + dir;
            c = c < 1 ? 1 : c > 8 ? 8 : c;
            if (t) t->bars = (uint8_t)c;
            ui.arr_bars = (uint8_t)c;
        } else if (t && k == 1) t->scene = (uint8_t)((t->scene + SCENES + dir) % SCENES);
        return;
    }
    if (ui.page == PG_DISCS) {                          /* SONGS: K1 picks the slot */
        int n = ui.song_pick + (d > 0 ? 1 : -1);
        if (k == 0) ui.song_pick = (uint8_t)(n < 0 ? 0 : n >= ST_SONGS ? ST_SONGS - 1 : n);
        return;
    }
    if (ui.page == PG_CHORD) {
        int h, i;
        for (h = 0; h < DMK_KEYS && !(ui.keys_down[h] && ui.key_slot[h]); h++) {}
        if (h < DMK_KEYS && k < 4) {                    /* a written chord held: K1 its slot, K2 quality, K3 length, K4 bass */
            prog_t *p = edit_prog();
            int at = ui.key_slot[h] - 1, dir = d > 0 ? 1 : -1;
            cslot_t *c = &p->s[at];
            if (k == 0 && at + dir >= 0 && at + dir < p->n) { cslot_t t = p->s[at + dir]; p->s[at + dir] = *c; *c = t; ui.key_slot[h] = (uint8_t)(at + dir + 1); }
            if (k == 1) c->q = (uint8_t)((c->q + Q_COUNT + dir) % Q_COUNT);
            if (k == 2) { int l = c->half + dir; c->half = (uint8_t)(l < 1 ? 1 : l > 16 ? 16 : l); ch_len = c->half; }
            if (k == 3) c->pedal = (uint8_t)(dir > 0);
            for (i = 0; i < ui.key_nch[h]; i++) later_off(ui.key_sy[h], ui.key_ch[h][i], SRC_KEY);   /* heard as it is now */
            ch_pn = 0;
            ui.key_nch[h] = (uint8_t)voice(48 + (seq_key + c->root) % 12, QI[c->q], c->pedal, ui.key_ch[h]);
            ch_name[0] = 0; chord_name(ch_name, seq_key + c->root, c->q);
            strike(ui.key_sy[h], ui.key_ch[h], ui.key_nch[h]);
            ch_shown = ui.key_slot[h] - 1;
            return;
        }
        if (k == 0 && ui.btn_held >> B_ARP & 1u) {      /* ARP held, K1: the slot cursor */
            int c = ch_cur + (d > 0 ? 1 : -1), n = cur_prog()->n;
            ch_cur = (uint8_t)(c < 0 ? 0 : c > n ? n : c);
            return;
        }
    }
    if (ui.page == PG_CHORD && k == 4) { prog_step(d); return; }   /* a progression: its own mode and voicing with it */
    if (k == 4 && (ui.page == PG_FX || ui.page == PG_GLOBAL) && !(ui.btn_held >> (ui.page == PG_FX ? B_FX : B_GLO) & 1u)) {
        int n = r, dir = d > 0 ? 1 : -1;                /* ALGORITHM moves through the rows; held, it is the type */
        do { n += dir; } while (n >= 0 && n < PAGE[ui.page].rows && row_soon(ui.page, n));
        if (n >= 0 && n < PAGE[ui.page].rows) ui.row[ui.page] = (uint8_t)n;
        ui.touch_k = -1;
        return;
    }
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
    if (down && ui.btn_held >> B_HOME & 1u) {           /* HOME held: the white keys the sixteen scenes, black 1-3 the mutes */
        int t = white_of(k);
        ui.btn_used |= (uint16_t)(1u << B_HOME);
        ui.key_home[k] = 1;
        if (t >= 0) {
            if (ui.copy_arm) {                          /* the copy: the scene open into this one */
                char m[24];
                if (t == scn_cur) { notice("Another scene"); return; }
                if (scn[t].used && !(ui.copy_to == t && ui.now - ui.copy_at < 2000u)) {   /* over one in use: twice */
                    ui.copy_to = (uint8_t)t; ui.copy_at = ui.now;
                    notice("Again: over it");
                    return;
                }
                scn_keep();
                scn[t] = scn[scn_cur];
                ui.copy_arm = 0;
                fmt_int(str_cat(fmt_int(str_cat(m, "Scene "), scn_cur + 1), " to "), t + 1);
                notice(m);
                return;
            }
            if (song_playing()) { song_mode = 0; notice("Scene mode"); }   /* a scene picked: the song left */
            scn_pick(t);
        } else if (k == 1 || k == 3 || k == 5) pat_mute_flip(k / 2);
        return;
    }
    if (!down && ui.key_home[k]) { ui.key_home[k] = 0; return; }   /* its release: nothing sounded */
    if (ui.page == PG_ARR && (!ui.row[PG_ARR] || !seq_on)) {
        /* Arrange: on the Scene row a white key 1-8 is the selected synth's pattern (at the bar, playing), a black key
         * 1-3 mutes; on the Song row, stopped, a white key writes a step of that scene and moves on */
        int t = white_of(k);
        if (!down) return;
        ui.key_home[k] = 1;
        if (!ui.row[PG_ARR]) {
            if (t >= 0 && t < SYN_PATS) pat_pick_syn(ui.synth, t);
            else if (k == 1 || k == 3 || k == 5) pat_mute_flip(k / 2);
            return;
        }
        if (t >= 0) {
            int at = ui.arr_cur < song_n ? ui.arr_cur : song_n;
            if (at < song_n) song[at].scene = (uint8_t)t;   /* over a step: its scene, its bars kept */
            else if (song_n < SONG_STEPS) { song_put(at, t, ui.arr_bars); song_n++; }
            else { notice("128 steps at most"); return; }
            if (!scn[t].used) { scn_keep(); scn[t] = scn[scn_cur]; }   /* an empty scene: a copy of this one */
            ui.arr_cur = (uint8_t)(at + 1);
        }
        return;
    }
    ui.keys_down[k] = (uint8_t)down;
    /* a key, a chord: on CHORD, or everywhere but the roll when the Keys row says so; released as it began */
    if (down ? chords_here() : ui.key_nch[k] > 0) {
        int j;
        if (down) {
            s = ui.synth;
            ui.key_sy[k] = (uint8_t)s;
            ui.key_slot[k] = 0;
            if (!seq_on && seq_rec) {                   /* stopped, REC armed: the chord into the slot at the cursor */
                ui.key_slot[k] = (uint8_t)(slot_write(n) + 1);
                if (!ui.key_slot[k]) notice(ch.prog < 16 ? "User 1-4 are full" : "Eight chords at most");
                ui.val[0][PG_CHORD][0][4] = choice_val(ch.prog, PROG_N);
            }
            ui.key_nch[k] = (uint8_t)chord_key(n, ui.octave, ui.key_ch[k]);
            if (ui.key_slot[k]) ch_shown = ui.key_slot[k] - 1;
            strike(s, ui.key_ch[k], ui.key_nch[k]);
            ui.key_roll[k] = seq_on && seq_rec ? 2 : 0;
            ui.key_t0[k] = seq_now();
            return;
        }
        s = ui.key_sy[k];
        if (ui.key_slot[k]) { ch_cur = ui.key_slot[k]; ui.key_slot[k] = 0; }   /* the cursor to the next slot */
        for (j = 0; j < ui.key_nch[k]; j++) {
            later_off(s, ui.key_ch[k][j], SRC_KEY);
            if (ui.key_roll[k] == 2 && seq_on) {        /* recorded: each of its notes into the synth's roll */
                uint32_t t0 = ui.key_t0[k], t1 = seq_now();
                int st = (int)(((t0 + 128u) >> 8) % sq[s].steps), len = (int)((t1 - t0 + 128u) >> 8);
                seq_add(s, st, len < 1 ? 1 : len > SEQ_STEPS ? SEQ_STEPS : len, ui.key_ch[k][j], 100);
            }
        }
        ui.key_nch[k] = 0;
        ui.key_roll[k] = 0;
        return;
    }
    if (down) {
        s = ui.synth;
        ui.key_sy[k] = (uint8_t)s;
        ui.key_roll[k] = 0;
        if (ui.page == PG_ROLL && !seq_on) {
            int c = ui.roll_cur[s];
            n = snap(n);
            if (seq_find(s, c, n) >= 0 || seq_add(s, c, ui.roll_pen[s], n, 100) >= 0) { ui.key_roll[k] = 1; ui.key_step[k] = (uint8_t)c; }
        } else if (seq_on && seq_rec && !song_rec) {   /* a song's take records the switches, not the keys */
            n = snap(n);
            ui.key_roll[k] = 2;
            ui.key_t0[k] = seq_now();
        }
        ui.key_note[k] = (uint8_t)n;
        note_on_s(s, n);
        {                                               /* the synths in harmony with this one play along */
            int f;
            for (f = 0; f < DMK_SYNTHS; f++) {
                ui.key_hn[k][f] = 0;
                if (f != s && harm_of(f) == s) { int h = harm_note(f, n); note_on_s(f, h); ui.key_hn[k][f] = (uint8_t)(h + 1); }
            }
        }
        return;
    }
    s = ui.key_sy[k];
    note_off_s(s, ui.key_note[k]);                      /* the synth and the note it started */
    {
        int f;
        for (f = 0; f < DMK_SYNTHS; f++) if (ui.key_hn[k][f]) { note_off_s(f, ui.key_hn[k][f] - 1); ui.key_hn[k][f] = 0; }
    }
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
        if (!ui.keys_down[i] && chords_here() && ch_cmask >> (n % 12) & 1u) leds[i] = 255;   /* the chord now */
    }
    if (ui.btn_held >> B_HOME & 1u) {                   /* HOME held: the scenes in use lit (the next blinking), the synths playing */
        for (i = 0; i < DMK_KEYS; i++) leds[i] = 0;
        for (i = 0; i < SCENES; i++) leds[WKEY[i]] = i == scn_cur || (scn[i].used && i != scn_next) || (i == scn_next && (ui.now / 250u) & 1u) ? 255 : 0;
        for (i = 0; i < DMK_SYNTHS; i++) leds[1 + 2 * i] = pat_mute >> i & 1u ? 0 : 255;
    }
    for (i = 0; i < B_COUNT; i++) leds[DMK_KEYS + i] = BTN_PAGE[i] == ui.page ? 255 : 0;
    leds[DMK_KEYS + B_PLAY] = seq_on ? 255 : 0;
    if (ch.keys) leds[DMK_KEYS + B_ARP] = 255;          /* the keys play chords everywhere: ARP lit */
    if (song_mode) leds[DMK_KEYS + B_HOME] = !seq_on || seq_ticks % 16u < 2u || ui.page == PG_HOME ? 255 : 0;   /* the bar */
    if (ui.page == PG_ARR && !(ui.btn_held >> B_HOME & 1u)) {   /* the Scene row: the synth's patterns; the Song row: the step's scene */
        for (i = 0; i < DMK_KEYS; i++) leds[i] = ui.keys_down[i] ? 255 : 0;
        if (!ui.row[PG_ARR]) {
            for (i = 0; i < SYN_PATS; i++) leds[WKEY[i]] = i == syn_pat(ui.synth) ? ((ui.now / 250u) & 1u ? 255 : 0) : spat[ui.synth][i].used ? 255 : 0;
            for (i = 0; i < DMK_SYNTHS; i++) leds[1 + 2 * i] = pat_mute >> i & 1u ? 0 : 255;
        } else if (ui.arr_cur < song_n) leds[WKEY[song[ui.arr_cur].scene & (SCENES - 1)]] = 255;
    }
    leds[DMK_KEYS + B_OCTD] = ui.octave < 0 ? 255 : 0;
    leds[DMK_KEYS + B_OCTU] = ui.octave > 0 ? 255 : 0;
    return leds;
}
