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
    {"Roll", 3, {"Roll", "Vary", "Arp"}, {"Zoom", "Zoom", "Hold"},
     {{"Cursor", "View", "Follow", "Steps"}, {"Mutate", "Range", "Chance", "Keep"}, {"Mode", "Rate", "Octave", "Gate"}}},
    {"Sound", 3, {"Main", "Layer", "Voice"}, {"Fine", "Sound", ""},
     {{"Sound", "Start", "Loop", "Tune"}, {"Blend", "Tune", "Loop", "Tone"}, {"Glide", "Velocity", "Track", "Grit"}}},
    {"Filter", 1, {"Filter"}, {"Type"}, {{"Cutoff", "Reso", "Env", "Drive"}}},
    {"Amp", 1, {"Amp"}, {"Level"}, {{"Attack", "Decay", "Sustain", "Release"}}},
    {"FX", 4, {"Colour", "Delay", "Motion", "Space"}, {"Effect", "Type", "Effect", "Verb, all"},   /* (Space: the sends; its
                                                           ALGO the verb they share: the owner found no way to set it) */
     {{"Amount", "Tone", "Time", "Spice"}, {"Mix", "Tone", "Time", "Regen"},   /* an effect names its own */
      {"Amount", "Tone", "Time", "Spice"}, {"Verb", "Echo", "Pan", "Width"}}},
    {"Chord", 3, {"Voice", "Key", "Keys"}, {"Prog", "Prog", "Prog"},
     {{"Voicing", "Invert", "Spread", "Strum"}, {"Key", "Mode", "Change", "Rhythm"}, {"Keys", "Type", "", ""}}},
    {"Global", 4, {"Verb", "Echo", "Media", "Setup"}, {"Type", "Type", "Type", ""},
     {{"Return", "Tone", "Size", "Mod"}, {"Return", "Tone", "Time", "Regen"},
      {"Amount", "Tone", "Time", "Spice"}, {"MIDI in", "MIDI out", "Clock", "Theme"}}},
    {"LFO", 2, {"Wave", "Sync"}, {"Target", "Target"}, {{"Rate", "Depth", "Shape", "Fade"}, {"Sync", "Restart", "", ""}}},
    {"Save", 2, {"Projects", "Sounds"}, {"", ""}, {{"Project", "Name", "", ""}, {"Track", "Sound", "User", "Library"}}},
    {"Song", 2, {"Scene", "Steps"}, {"", ""}, {{"Synth 1", "Synth 2", "Synth 3", "Chords"}, {"Step", "Scene", "Bars", "End"}}},
};

/* the page each button opens (-1: none) */
static const int8_t BTN_PAGE[B_COUNT] = {PG_FX, PG_FILTER, PG_AMP, PG_LFO, PG_SOUND, PG_GLOBAL, PG_HOME, -1, PG_CHORD,
                                         PG_ROLL, -1, -1, -1, -1};
static const char *const BTN_NAME[B_COUNT] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                              "PLAY", "REC", "OCT−", "OCT+"};
static const char *const LAYER_NAME[B_COUNT] = {"ALGORITHM, the effect", "help", "help", "help", "the sound", "ALGORITHM, the type",
                                                "", "projects, sounds", "the progression", "K2 the harmony, OCT\xE2\x88\x92 clears", "", "", "", ""};

/* a choice field: its names (none: a number 0..127) */
typedef struct { uint8_t page, row, k, n; const char *const *name; } choice_t;
static const char *const FILTERS[] = {"LP24", "LP12", "HP", "BP", "Notch", "Peak", "Vowel", "Off"};
/* each row's types (the later ones after, so a stored type keeps its place: dmk_store's version 6) */
#define FX_COLOURS 7
#define FX_MEDIAS 7
#define FX_MOTIONS 8
static const char *const COLOURS[FX_COLOURS] = {"Off", "Saturate", "Crush", "Lossy", "Noise", "Warm", "Distort"};
static const char *const MEDIAS[FX_MEDIAS] = {"Off", "Tape", "Cassette", "VHS", "Vinyl", "Radio", "Dictaphone"};
static const char *const MOTIONS[FX_MOTIONS] = {"Off", "Chorus", "Vibrato", "Drift", "Ensemble", "Phaser", "Flanger", "Tremolo"};
static const char *const TREMS[] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"};   /* (dmk_dsp's TREM3) */
/* each effect's four knobs, by row and type (the off ones: the row's own; MEDIA's: the whole mix's, GLO's third row) */
static const char *const FXK[3][8][4] = {
    {{"Amount", "Tone", "Time", "Spice"}, {"Drive", "Tone", "Bias", "Mix"}, {"Bits", "Tone", "Rate", "Jitter"},
     {"Loss", "Band", "Frame", "Holes"}, {"Hiss", "Tone", "Crackle", "Hum"}, {"Drive", "Tilt", "Body", "Mix"},
     {"Drive", "Tone", "Shape", "Mix"}},
    {{"Amount", "Tone", "Time", "Spice"}, {"Amount", "Tone", "Wow", "Flutter"}, {"Warble", "Tone", "Rate", "Dropout"},
     {"Wobble", "Band", "Rate", "Track"}, {"Wow", "Tone", "Crackle", "Rumble"}, {"Band", "Tone", "Static", "Drift"},
     {"Warble", "Tone", "Speed", "Squash"}},
    {{"Amount", "Tone", "Time", "Spice"}, {"Depth", "Tone", "Rate", "Mix"}, {"Depth", "", "Rate", "Fade"},
     {"Depth", "Detune", "Rate", ""}, {"Depth", "Tone", "Rate", "Width"}, {"Depth", "Tone", "Rate", "Reso"},
     {"Depth", "Tone", "Rate", "Reso"}, {"Depth", "Shape", "Rate", "Pan"}}};
static const char *const ZOOMS[] = {"16", "32", "64"};
static const char *const OFF[] = {"Off"};
static const char *const LFOS[] = {"Sine", "Triangle", "Ramp", "Square", "Random"};
static const char *const VOICINGS[] = {"Close", "Open", "Wide", "Shell", "Quartal"};
static const char *const INVERTS[] = {"Auto", "Root", "1st", "2nd", "3rd"};
static const char *const KEYS[] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G", "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
static const char *const MODENAMES[] = {"Aeolian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Ionian"};
static const char *const CHANGES[] = {"\xC3\x97\xC2\xBD", ("\xC3\x97" "1"), ("\xC3\x97" "2"), ("\xC3\x97" "4")};
static const char *const RHYTHMS[] = {"Hold", "Pulse 4", "Pulse 8", "Up", "Down"};   /* (the last two: arpeggios, as the
                                                           arp's own; "Arp down" was cut) */
static const char *const KEYMODES[] = {"Chord", "All"};
static const char *const MIDI_INS[] = {"Synth", "Ch 1-3"};   /* MIDI in: the synth selected, or synth n on channel n */
static const char *const MIDI_OUTS[] = {"Off", "Notes", "Clock", "All"};
static const char *const CLOCKS[] = {"Own", "Ext"};   /* the keys play chords on CHORD, or on every page but the roll */
static const char *const KTYPES[] = {"7ths", "Triads", "9ths", "Add9", "Sus2", "Sus4", "6/9", "Power"};   /* (whole in a field) */
static const char *const FOLLOWS[] = {"Free", "Follows the key", "Follows the chord", "Follows the root", "Plays chords",
                                      "Harmony of 1", "Harmony of 2", "Harmony of 3"};
static const char *const PROGS[20] = {"First snow", "Last train", "Fogbound", "Overpass", "Vending light", "Blue hour",
    "Power lines", "Tower beacon", "Slow thaw", "Candle", "Embers", "Low clouds", "Snowfield", "Static", "Mixtape", "Night drive",
    "User 1", "User 2", "User 3", "User 4"};
static uint8_t choice_val(int i, int n) { return (uint8_t)(i * 128 / n + 64 / n); }   /* a choice's place, its middle */
/* a list goes round: past its last, its first, and back (the owner: no turning back to reach the start). Numbers stop
 * at their ends: a cutoff or a level going round would jump */
static int wrap(int i, int n) { return n > 0 ? (i % n + n) % n : 0; }
static const char *const TARGETS[] = {"Pitch", "Cutoff", "Level", "Pan", "Start"};
static const char *const SYNCS[] = {"Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"};
static const uint8_t SYNC16[8] = {0, 64, 32, 16, 8, 4, 2, 1};   /* sixteenths a cycle */
static const char *const RESTARTS[] = {"Free", "A note"};   /* the LFO from its start on a phrase's first note */
static const char *const LTUNES[] = {"\xE2\x88\x92" "2 oct", "\xE2\x88\x92" "1 oct", "\xE2\x88\x92" "5th", "Unison", "+5th", "+1 oct", "+2 oct"};
static const int8_t LTUNE[7] = {-24, -12, -7, 0, 7, 12, 24};
static const char *const GRITS[] = {"Off", "22 kHz", "11 kHz"};
static const char *const LOOPS[] = {"Off", "Full"};
static const char *const VERBS[] = {"Off", "Room", "Hall", "Space", "Haze", "Bloom", "Lofi", "Plate", "Spring", "Shimmer",
                                     "Reverse", "Freeze"};   /* (the owner: off too; more, the owner's, later: dmk_store's 8) */
#define FX_VERBS 12
static const char *const ECHOS[] = {"Off", "Tape", "Grains"};   /* (the delays: each synth's, FX's second row) */
#define FX_ECHOS 3
static const char *const DELAYS[] = {"Off", "Delay", "Ping-pong"};
static const char *const KEEPS[] = {"Off", "On"};      /* the roll's Vary: mutations heard, or written in */
static const char *const RANGES[] = {"1", "2", "3", "4", "5", "6", "7"};   /* its reach, scale steps (a click a step) */
static const char *const SPREADS[] = {"0", "1", "2", "3"};   /* the chord's spread, octaves */
/* the ARP's (dmk_seq.c AR_, ARP24): its ways, its rates, its octaves, HOLD */
static const char *const ARPS[] = {"Off", "Up", "Down", "Hill", "Valley", "Bounce", "Fold", "Unfold", "Dice", "Walk",
                                   "Played", "Chord", "Thumb", "Pinky"};   /* (each within a field's 48 px, measured) */
static const char *const ARPDIVS[] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4"};
static const char *const ARPOCTS[] = {"1", "2", "3", "4"};
static const char *const HOLDS[] = {"Off", "On"};
static const char *const DIVS[] = {"1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2"};   /* the delay's (dmk_seq: DIV24) */
#define DIV_N 9
static const choice_t CHOICE[] = {
    {PG_SOUND, 0, 2, 2, LOOPS}, {PG_FILTER, 0, 4, 8, FILTERS}, {PG_ROLL, 0, 4, 3, ZOOMS},
    {PG_FX, 0, 4, FX_COLOURS, COLOURS}, {PG_FX, 1, 4, 3, DELAYS}, {PG_FX, 1, 2, DIV_N, DIVS}, {PG_FX, 2, 4, FX_MOTIONS, MOTIONS},
    {PG_GLOBAL, 3, 3, DMK_THEMES, THEME_NAME}, {PG_GLOBAL, 0, 4, FX_VERBS, VERBS}, {PG_GLOBAL, 1, 4, FX_ECHOS, ECHOS},
    {PG_GLOBAL, 2, 4, FX_MEDIAS, MEDIAS}, {PG_FX, 3, 4, FX_VERBS, VERBS}, {PG_ROLL, 1, 3, 2, KEEPS}, {PG_ROLL, 1, 1, 7, RANGES}, {PG_CHORD, 0, 2, 4, SPREADS},
    {PG_ROLL, 2, 0, AR_N, ARPS}, {PG_ROLL, 2, 1, 9, ARPDIVS}, {PG_ROLL, 2, 2, 4, ARPOCTS}, {PG_ROLL, 2, 4, 2, HOLDS}, {PG_LFO, 0, 2, 5, LFOS}, {PG_LFO, 0, 4, 5, TARGETS},
    {PG_LFO, 1, 0, 8, SYNCS}, {PG_LFO, 1, 1, 2, RESTARTS}, {PG_LFO, 1, 4, 5, TARGETS}, {PG_SOUND, 1, 1, 7, LTUNES}, {PG_SOUND, 1, 2, 2, LOOPS},
    {PG_SOUND, 2, 3, 3, GRITS},
    {PG_CHORD, 0, 0, 5, VOICINGS}, {PG_CHORD, 0, 1, 5, INVERTS}, {PG_CHORD, 0, 4, 20, PROGS}, {PG_CHORD, 1, 4, 20, PROGS},
    {PG_CHORD, 1, 0, 12, KEYS}, {PG_CHORD, 1, 1, 6, MODENAMES}, {PG_CHORD, 1, 2, 4, CHANGES}, {PG_CHORD, 1, 3, 5, RHYTHMS},
    {PG_CHORD, 2, 0, 2, KEYMODES}, {PG_CHORD, 2, 1, 8, KTYPES}, {PG_CHORD, 2, 4, 20, PROGS},
    {PG_GLOBAL, 3, 0, 2, MIDI_INS}, {PG_GLOBAL, 3, 1, 4, MIDI_OUTS}, {PG_GLOBAL, 3, 2, 2, CLOCKS},
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
    uint32_t key_q8[DMK_KEYS];                          /*   and where on its synth's roll, Q8 steps */
    uint8_t rec_ready;                                  /* REC lit, PLAY tapped stopped: the first key starts it, at
                                                           the first step, recording */
    uint16_t hinted;                                    /* the pages whose next row was shown how */
    uint8_t key_ch[DMK_KEYS][7], key_nch[DMK_KEYS];     /* the CHORD page: each key's chord, as it sounds */
    uint8_t clear_arm;                                  /* OCT- once: 1 this synth's roll, 2 all three, armed */
    uint32_t clear_at;
    uint8_t song, song_pick, list_top;                  /* the project open's slot; Save's cursor; the first row in view */
    uint8_t ren_slot, ren_v[3];                         /* a name being changed: its slot (0xFF: none), its words */
    uint32_t ren_at, nm_n;                              /* (when it was turned: written once it rests) */
    uint8_t preset[DMK_SYNTHS];                         /* PRESETS: 0-63 the recordings, then the library's sounds */
    uint8_t arr_cur, arr_bars;                          /* Arrange: the step cursor; the bars a key writes a step with */
    uint8_t copy_arm, copy_to;                          /* HOME held + OCT+: a pattern key next pastes into it */
    uint8_t full, full_latch, both_on, full_note;       /* the landscape full screen (OCT- and OCT+ held); held once;
                                                           a notice was over it */
    uint32_t both_at;
    uint32_t copy_at;
    uint8_t key_slot[DMK_KEYS];                         /* writing: the slot a held key wrote, + 1 (0: none) */
    uint32_t loud_ms, loud_mark, loud_seen;             /* GLO's meter: the mix past full in the last half second */
    int8_t sel_dir;                                     /* SELECT's last step and when (one synth a click) */
    uint32_t sel_at;
} ui;

static uint8_t leds[DMK_KEYS + B_COUNT];

static void notice(const char *s)
{
    int i;
    for (i = 0; i < 31 && s[i]; i++) ui.note[i] = s[i];
    ui.note[i] = 0;
    ui.note_ms = ui.now ? ui.now : 1;
}

/* the keys play chords here: on CHORD, or anywhere but Arrange (whose keys write its steps) when the Keys row says
 * All; there they light and play as on CHORD, and on the roll they write the chord (the owner's ask) */
static int chords_here(void) { return ui.page == PG_CHORD || (ch.keys && ui.page != PG_ARR); }
/* a roll written to: a synth that plays the chords, or harmonizes another, leaves its roll, so what was written
 * would not be heard; it plays its roll from now (SEQ held + ALGORITHM puts it back) */
static void roll_heard(int s)
{
    char t[24];
    if (ch.follow[s] != FW_PLAYS && harm_of(s) < 0) return;
    if (ch.follow[s] == FW_PLAYS) chord_off(s);
    ch.follow[s] = FW_FREE;
    str_cat(fmt_int(str_cat(t, "Synth "), s + 1), " plays its roll");
    notice(t);
}

/* a note written where the roll's seven rows don't reach: the view moved to it (the owner: a note sounded and wasn't
 * there); the roll full, said (a note past 64 is not kept) */
static void roll_show(int s, int note)
{
    int d = degree_of(note), v = ui.roll_view[s];
    if (d < v) v = d; else if (d > v + 6) v = d - 6;
    ui.roll_view[s] = (uint8_t)(v < 14 ? 14 : v > 43 ? 43 : v);
}
static void roll_full(void) { notice("The roll is full: 64 notes"); }
static void roll_unarmed(void)                          /* the first time keys play a stopped roll: how to write */
{
    if (ui.hinted >> 15 & 1u) return;
    ui.hinted |= 0x8000u;
    notice("REC: the keys write");
}

/* the patterns' progression is the CHORD page's (dmk_seq.c keeps the rest) */
static uint8_t pat_prog_get(void) { return ui.val[0][PG_CHORD][0][4]; }
static void keys_unslot(void) { int k; for (k = 0; k < DMK_KEYS; k++) ui.key_slot[k] = 0; }   /* a progression changed:
                                                           a held key's written slot was the last one's */
static void pat_prog_put(uint8_t v) { ui.val[0][PG_CHORD][0][4] = v; ch.prog = (uint8_t)(v * PROG_N / 128); keys_unslot(); }
static void sp_sync(void);
static const choice_t *choice_of(int page, int row, int k);
static int choice_index(const choice_t *c, int v);
/* the progression stepped: its own mode and voicing come with it (CHORD's ALGORITHM, Arrange's Chords) */
static void prog_step(int d)
{
    int p = wrap(choice_index(choice_of(PG_CHORD, 0, 4), ui.val[0][PG_CHORD][0][4]) + (d > 0 ? 1 : -1), PROG_N);
    const prog_t *pr;
    pr = p < 16 ? &PROG[p] : &uprog[p - 16];
    ui.val[0][PG_CHORD][0][4] = choice_val(p, PROG_N);
    keys_unslot();
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
/* a roll cleared: its notes off, and the synth silent. One that plays the chords leaves its roll, so it would hold
 * the progression on (the owner heard it): its chords go off too. 1: they did */
static int roll_clear(int s)
{
    int chords = ch.follow[s] == FW_PLAYS;
    seq_quiet(s);
    seq_quiet_from(s);                                  /* and its harmonies on the others */
    sq[s].count = 0;
    if (chords) { chord_off(s); ch.follow[s] = FW_FREE; }
    return chords;
}

static void pat_mute_flip(int s)                        /* HOME held, a black key: that synth muted, or back */
{
    int i;
    pat_mute ^= (uint8_t)(1u << s);
    (void)i;
    if (pat_mute >> s & 1u) { seq_quiet(s); seq_quiet_from(s); if (ch.follow[s] == FW_PLAYS) chord_off(s); }
    else chord_join(s);                                 /* back: the chord sounding now, not the next */
}

/* the store (dmk_store.c, after this file) */
#define ST_SONGS 14
#define ST_USER_N 32                                    /* the library's sounds (dmk_store.c: ST_USER) */
static int store_on(void);
static int song_save(int i);
static int song_load(int i);
static int song_used(int i);
static int song_erase(int i);
#define NM_MARK 0xA5u                                   /* a name's mark: its words' places are set */
static void nm_roll(uint8_t *v, uint32_t salt);         /* (dmk_store.c: a song's name, made up; written out) */
static void nm_of(char *o, const uint8_t *v);
static const char *nm_word(int second, int i);
static uint8_t song_nmv[ST_SONGS][3];
static int song_rename(int i, const uint8_t *v);
static void project_empty(void);
static int song_changed(void);
static void song_new(void);
static const char *song_name(int i);
static int sound_save(int s);
static void sound_init(int s);
static void sound_put(int s, uint8_t *o);
static int lib_count(void);
static int lib_nth(int n);
static void lib_load(int s, int slot);
#define SND_BYTES 55                                    /* a sound's values (dmk_store.c: SND_ROWS) */
static uint8_t ui_first[DMK_SYNTHS][SND_BYTES];         /* INIT: each synth's sound as the first song has it */

/* a control's value: a synth's own on its pages, the song's on Home's last column and Global; Home's rows are views of
 * each synth's level (AMP), sends and pan (FX, SPACE) */
static int per_synth(int page) { return page == PG_SOUND || page == PG_FILTER || page == PG_AMP || page == PG_FX || page == PG_LFO; }
static uint8_t *vp(int page, int row, int k)
{
    if (page == PG_HOME && k == 4) return &ui.val[0][PG_HOME][0][4];   /* the tempo, on every row */
    if (page == PG_CHORD && k == 4) return &ui.val[0][PG_CHORD][0][4];  /* the progression, on both rows */
    if (page == PG_LFO && k == 4) return &ui.val[ui.synth][PG_LFO][0][4];   /* the LFO's target, on both rows */
    if (page == PG_FX && row == 3 && k == 4) return &ui.val[0][PG_GLOBAL][0][4];   /* Space's ALGO: the verb all share */
    if (page == PG_ROLL && row == 1 && k < 4) return &ui.val[ui.synth][PG_ROLL][1][k];   /* Vary: each synth's own (it
                                                           was the first synth's whichever was picked: the owner) */
    if (page == PG_ROLL && row == 2) return &ui.val[ui.synth][PG_ROLL][2][k];   /* the Arp: each synth's, HOLD too */
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
        sq[s].mut = (uint16_t)(VS(s, PG_ROLL, 1, 0) * 258);   /* the roll's Vary: a note's chance to move, how far, to play */
        sq[s].mrange = (uint8_t)(1 + choice_index(choice_of(PG_ROLL, 1, 1), VS(s, PG_ROLL, 1, 1)));   /* (a choice: a click a step) */
        sq[s].chance = (uint16_t)(VS(s, PG_ROLL, 1, 2) >= 127 ? 32768 : VS(s, PG_ROLL, 1, 2) * 258);
        sq[s].keep = (uint8_t)(VS(s, PG_ROLL, 1, 3) >= 64);
        {                                               /* the ARP: off, its notes let go; HOLD off, the held ones too */
            int am = choice_index(choice_of(PG_ROLL, 2, 0), VS(s, PG_ROLL, 2, 0)), ah = VS(s, PG_ROLL, 2, 4) >= 64;
            if (!am && arp[s].mode) arp_clear(s);
            else if (arp[s].hold && !ah) arp_unheld(s);  /* (only those let go: the held ones play on) */
            arp[s].mode = (uint8_t)am; arp[s].hold = (uint8_t)ah;
            arp[s].div = (uint8_t)choice_index(choice_of(PG_ROLL, 2, 1), VS(s, PG_ROLL, 2, 1));
            arp[s].oct = (uint8_t)choice_index(choice_of(PG_ROLL, 2, 2), VS(s, PG_ROLL, 2, 2));
            arp[s].gate = VS(s, PG_ROLL, 2, 3);
        }
        p->fenv = (int16_t)((VS(s, PG_FILTER, 0, 2) - 64) * 4);
        p->atk = T_ATK[VS(s, PG_AMP, 0, 0)];
        p->dec = T_EXP[VS(s, PG_AMP, 0, 1)];
        p->sus = (int32_t)((uint32_t)VS(s, PG_AMP, 0, 2) * (FULL / 127));
        p->rel = T_EXP[VS(s, PG_AMP, 0, 3)];
        v = VS(s, PG_AMP, 0, 4);
        p->level = v * v * 2;                           /* Q15 on a square law: 64 is -12 dB */
        p->colour = (uint8_t)(VS(s, PG_FX, 0, 4) * FX_COLOURS / 128);
        p->motion = (uint8_t)(VS(s, PG_FX, 2, 4) * FX_MOTIONS / 128);
        for (v = 0; v < 4; v++) { p->ck[v] = VS(s, PG_FX, 0, v); p->ok[v] = VS(s, PG_FX, 2, v); }
        p->dtype = (uint8_t)choice_index(choice_of(PG_FX, 1, 4), VS(s, PG_FX, 1, 4));   /* its DELAY: off, delay, ping-pong */
        p->dmix = VS(s, PG_FX, 1, 0) * 258;
        p->dtone = T_OPH[64 + VS(s, PG_FX, 1, 1) / 2];  /* (at a quarter of the rate: 325 Hz .. 5 kHz) */
        p->ddiv = (uint8_t)choice_index(choice_of(PG_FX, 1, 2), VS(s, PG_FX, 1, 2));
        p->dfb = VS(s, PG_FX, 1, 3) * 240;
        p->vsend = VS(s, PG_FX, 3, 0) * 258;
        p->esend = VS(s, PG_FX, 3, 1) * 258;
        v = VS(s, PG_FX, 3, 2);
        p->gl = T_PAN[127 - v]; p->gr = T_PAN[v]; p->pan = v;
        p->width = VS(s, PG_FX, 3, 3) * 4;              /* 64: as it is */
        /* the LAYER: a second recording under the sound, through its filter and envelope */
        p->lsmp = (uint8_t)(VS(s, PG_SOUND, 1, 4) < DMK_SAMPLES_N ? VS(s, PG_SOUND, 1, 4) : 0);
        p->lblend = VS(s, PG_SOUND, 1, 0) * 258;
        p->ltune = LTUNE[choice_index(choice_of(PG_SOUND, 1, 1), VS(s, PG_SOUND, 1, 1))];
        p->lloops = (uint8_t)(VS(s, PG_SOUND, 1, 2) >= 64);
        p->ltone = T_OP[64 + VS(s, PG_SOUND, 1, 3) / 2];
        /* VOICE: glide (30 ms .. 18 s to a hair's breadth), velocity, the key's tracking (127: the cutoff moves with
         * the note), the rate; FILTER's drive, to 4 times into the curve, given back about as loud */
        v = VS(s, PG_SOUND, 2, 0); p->glide = v ? T_EXP[v] : 0;
        p->vel = VS(s, PG_SOUND, 2, 1);
        p->track = (int16_t)(VS(s, PG_SOUND, 2, 2) * 722 / 127);
        p->grit = (uint8_t)choice_index(choice_of(PG_SOUND, 2, 3), VS(s, PG_SOUND, 2, 3));
        p->drive = 256 + VS(s, PG_FILTER, 0, 3) * 6;
        p->dcomp = 65536 / (160 + p->drive / 3);
        /* the LFO: 0.05 .. 20 Hz, or a bar's division; its depth straight (the pitch squares it); its fade to 8 s */
        p->lrate = T_LFO[VS(s, PG_LFO, 0, 0)];
        p->ldepth = VS(s, PG_LFO, 0, 1) * 258;          /* the owner: it should be felt, early in the knob */
        p->lshape = (uint8_t)choice_index(choice_of(PG_LFO, 0, 2), VS(s, PG_LFO, 0, 2));
        v = VS(s, PG_LFO, 0, 3); p->lfade = (uint16_t)(v * v * 11 / 8);
        p->ltarget = (uint8_t)choice_index(choice_of(PG_LFO, 0, 4), VS(s, PG_LFO, 0, 4));
        p->lsync = SYNC16[choice_index(choice_of(PG_LFO, 1, 0), VS(s, PG_LFO, 1, 0))];
        p->lretrig = (uint8_t)choice_index(choice_of(PG_LFO, 1, 1), VS(s, PG_LFO, 1, 1));
    }
    {   /* the VERB's type: its loop's gain from SIZE (Q15, a ninth of a dB a pass at the most for HAZE), its sweep's
         * depth at full MOD (Q8 samples) and its rate (0.3, 0.13, 0.2, 0.5 Hz at 22.05 kHz) */
        /* the later five (the owner's): PLATE the lines at half length, bright, still; SPRING at a quarter, through a
         * chain of all-passes, thin; SHIMMER HALL's with its sound an octave up fed back in (MOD, as much); REVERSE
         * the sound read backwards in segments (SIZE: as long) into a mid hall; FREEZE nearly lossless, only so much
         * new sound let in (SIZE is HOLD: all of it held at the top) */
        static const uint16_t G0[VB_OFF + 1] = {0, 24000, 26000, 25000, 25000, 19000, 23000, 27000, 25000, 27500, 30000, 0};
        static const uint16_t G1[VB_OFF + 1] = {0, 31900, 32600, 32450, 32300, 30500, 30400, 31500, 31900, 30500, 32700, 0};
        static const uint16_t MD[VB_OFF + 1] = {0, 4096, 8192, 12288, 3072, 15360, 0, 2048, 4096, 4096, 6144, 0};
        static const uint32_t RT[VB_OFF + 1] = {0, 58435, 31165, 25322, 38957, 97392, 0, 77913, 31165, 31165, 19478, 0};
        static const uint16_t GN[VB_OFF + 1] = {256, 256, 256, 256, 256, 256, 230, 256, 215, 360, 256, 256};   /* (levels: each as
                                                           loud as HALL at the demos' settings, REVERSE's swell lifted) */
        int t = choice_index(choice_of(PG_GLOBAL, 0, 4), VS(0, PG_GLOBAL, 0, 4)), sz = VS(0, PG_GLOBAL, 0, 2), md = VS(0, PG_GLOBAL, 0, 3);
        t = t ? t - 1 : VB_OFF;                         /* the list's first is Off */
        gp.vtype = (uint8_t)t;
        gp.vfbq = (G0[t] + (G1[t] - G0[t]) * (t == VB_REVERSE ? 64 : sz) / 127) * 11585 >> 15;   /* the Hadamard's sqrt(8) taken out */
        gp.vmod = t == VB_SHIMMER ? MD[t] : MD[t] * md / 127;
        gp.vrate = RT[t];
        gp.vgin = t == VB_FREEZE ? (127 - sz) * 258 + 512 : 32768;
        gp.vsh = t == VB_SHIMMER ? md * 90 : 0;         /* (the octave's share of the loop: under its runaway) */
        gp.vrlen = 2048 + sz * 48;                      /* 93 .. 370 ms */
        gp.vgain = GN[t];
    }
    {   /* the whole mix's MEDIA (GLO's third row): its type, its knobs (the tape's in its own units) */
        synth_t *p = &gmed;
        int v;
        p->media = (uint8_t)choice_index(choice_of(PG_GLOBAL, 2, 4), VS(0, PG_GLOBAL, 2, 4));
        for (v = 0; v < 4; v++) p->mk[v] = VS(0, PG_GLOBAL, 2, v);
        p->tape = (uint8_t)(p->media == FM_TAPE);
        p->tdepth = VS(0, PG_GLOBAL, 2, 0) * 800;       /* the wow: up to ~40 cents at its slowest */
        p->tdrive = 256 + VS(0, PG_GLOBAL, 2, 0) * 3;
        p->tcomp = (256 << 15) / p->tdrive;
        p->thl = T_OP[64 + VS(0, PG_GLOBAL, 2, 1) / 2]; /* head loss: 600 Hz .. open */
        p->twow = 19480u + (uint32_t)VS(0, PG_GLOBAL, 2, 2) * 1500u;   /* 0.2 .. 2.2 Hz */
        p->tflut = 620000u + (uint32_t)VS(0, PG_GLOBAL, 2, 3) * 2000u; /* 6.3 .. 7.7 Hz */
    }
    gp.width = VS(0, PG_HOME, 3, 3) * 4;                /* the whole mix's width: 64 as it is */
    gp.vret = VS(0, PG_GLOBAL, 0, 0) * 258;
    gp.vdamp = T_OPH[VS(0, PG_GLOBAL, 0, 1)];
    gp.vfb = 22938 + VS(0, PG_GLOBAL, 0, 2) * 72;
    gp.eret = VS(0, PG_GLOBAL, 1, 0) * 258;
    gp.elp = T_OPH[VS(0, PG_GLOBAL, 1, 1)];
    gp.etime = T_ETIME[VS(0, PG_GLOBAL, 1, 2)];
    gp.efb = VS(0, PG_GLOBAL, 1, 3) * 240;
    { int t = choice_index(choice_of(PG_GLOBAL, 1, 4), VS(0, PG_GLOBAL, 1, 4)); gp.etype = (uint8_t)(t ? t - 1 : ET_OFF); }   /* Off first */
    gp.glen = 441 + VS(0, PG_GLOBAL, 1, 1) * 62;        /* GRAINS: 20..380 ms */
    gp.gdens = 6 + VS(0, PG_GLOBAL, 1, 2) * 113 / 127;  /* 2..40 a second */
    gp.gtex = VS(0, PG_GLOBAL, 1, 3);
    midi_chans = (uint8_t)(VS(0, PG_GLOBAL, 3, 0) * 2 / 128);   /* the setup */
    midi_out = (uint8_t)(VS(0, PG_GLOBAL, 3, 1) * 4 / 128);
    clock_ext = (uint8_t)(VS(0, PG_GLOBAL, 3, 2) * 2 / 128);
    if (!clock_ext) seq_bpm = VS(0, PG_HOME, 0, 4) < 20 ? 20 : VS(0, PG_HOME, 0, 4);   /* outside, the clock's tempo */
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

/* the controls 0.4 added (the layer, the voice, the filter's drive, the LFO): off, and what older songs and sounds
 * load with, since none of them sounded before */
static void roll_vary_defaults(int s)                  /* the roll's Vary: no mutation, two steps, every note, heard only */
{
    uint8_t *v = ui.val[s][PG_ROLL][1];
    v[0] = 0; v[1] = 32; v[2] = 127; v[3] = 0; v[4] = 64;
}
static void arp_defaults(int s)                         /* the ARP: off, sixteenths, an octave, a short gate, no hold */
{
    uint8_t *v = ui.val[s][PG_ROLL][2];
    v[0] = 0; v[1] = choice_val(2, 9); v[2] = choice_val(0, 4); v[3] = 72; v[4] = 0;
}
static void delay_defaults(int s)                       /* a synth's DELAY: off, a dotted eighth, a few repeats */
{
    uint8_t *v = ui.val[s][PG_FX][1];
    v[0] = 48; v[1] = 80; v[2] = choice_val(5, DIV_N); v[3] = 56; v[4] = 0;
}
static void snd_new_defaults(int s)
{
    uint8_t *l = ui.val[s][PG_SOUND][1], *vo = ui.val[s][PG_SOUND][2], *f = ui.val[s][PG_LFO][0], *y = ui.val[s][PG_LFO][1];
    l[0] = 0; l[1] = choice_val(3, 7); l[2] = choice_val(1, 2); l[3] = 127; l[4] = ui.val[s][PG_SOUND][0][0];
    vo[0] = 0; vo[1] = 0; vo[2] = 0; vo[3] = choice_val(0, 3); vo[4] = 64;
    ui.val[s][PG_FILTER][0][3] = 0;
    f[0] = 64; f[1] = 0; f[2] = choice_val(0, 5); f[3] = 0; f[4] = choice_val(LT_CUTOFF, 5);
    y[0] = choice_val(0, 8); y[1] = choice_val(0, 2); y[2] = y[3] = y[4] = 64;
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

DMK_SMALL static void ui_init(void)
{
    /* the three synths' first sounds: a pad, keys, a pluck; [sample, attack, decay, sustain, release, cutoff, reso,
     * env, level, tape, wow, verb, echo, pan] */
    static const uint8_t FIRST[DMK_SYNTHS] = {DMK_FIRST_PAD, DMK_FIRST_KEY, DMK_FIRST_PLUCK};   /* a pad, keys, a pluck */
    static const uint8_t SET[DMK_SYNTHS][13] = {         /* (tape, wow: the mix's since 0.5, the pad's) */
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
        ui.val[s][PG_FX][0][4] = 0;                     /* Colour, Delay, Motion: off */
        ui.val[s][PG_FX][2][4] = 0;
        delay_defaults(s);
        roll_vary_defaults(s);
        arp_defaults(s);
        ui.val[s][PG_FX][3][0] = t[10]; ui.val[s][PG_FX][3][1] = t[11]; ui.val[s][PG_FX][3][2] = t[12];
        snd_new_defaults(s);
    }
    ui.val[0][PG_HOME][0][4] = 72;                      /* tempo 72 */
    ui.val[0][PG_CHORD][0][0] = 2 * 128 / 5 + 12; ui.val[0][PG_CHORD][0][1] = 12; ui.val[0][PG_CHORD][0][2] = 0;
    ui.val[0][PG_CHORD][0][3] = 64; ui.val[0][PG_CHORD][0][4] = 4;            /* wide, auto, no spread or strum; First snow */
    ui.val[0][PG_CHORD][1][0] = 9 * 128 / 12 + 5; ui.val[0][PG_CHORD][1][1] = 10;   /* A, Aeolian */
    ui.val[0][PG_CHORD][1][2] = 48; ui.val[0][PG_CHORD][1][3] = 12;           /* x1, hold */
    ui.val[0][PG_CHORD][2][0] = choice_val(0, 2); ui.val[0][PG_CHORD][2][1] = choice_val(KT_SEVENTH, KT_COUNT);   /* keys: chords on CHORD, sevenths */
    ui.val[0][PG_GLOBAL][0][0] = 84; ui.val[0][PG_GLOBAL][0][1] = 56; ui.val[0][PG_GLOBAL][0][2] = 96;   /* the verb: a hall */
    ui.val[0][PG_GLOBAL][0][4] = choice_val(2, FX_VERBS); ui.val[0][PG_GLOBAL][0][3] = 64;
    ui.val[0][PG_GLOBAL][1][4] = choice_val(1, FX_ECHOS);   /* the echo: tape */
    ui.val[0][PG_GLOBAL][1][0] = 64; ui.val[0][PG_GLOBAL][1][1] = 52; ui.val[0][PG_GLOBAL][1][2] = 76;
    ui.val[0][PG_GLOBAL][1][3] = 64;                    /* the echo: ~0.4 s, a few repeats */
    ui.val[0][PG_GLOBAL][2][4] = choice_val(FM_TAPE, FX_MEDIAS);   /* the mix's media: tape, a little wow */
    ui.val[0][PG_GLOBAL][2][0] = SET[0][9]; ui.val[0][PG_GLOBAL][2][1] = 90;
    ui.val[0][PG_GLOBAL][3][0] = choice_val(0, 2); ui.val[0][PG_GLOBAL][3][1] = choice_val(1, 4);   /* MIDI: the synth, notes out */
    ui.val[0][PG_GLOBAL][3][2] = choice_val(0, 2);      /* its own clock */
    ui.val[0][PG_GLOBAL][3][3] = choice_val(theme, DMK_THEMES);
    for (s = 0; s < DMK_SYNTHS; s++) {                  /* each roll on its own pattern: its notes in view */
        ui.roll_cur[s] = 0;
        ui.roll_pen[s] = 1;
        ui.roll_zoom[s] = (uint8_t)(sq[s].steps > 16);
    }
    ui.roll_view[0] = (uint8_t)degree_of(53); ui.roll_view[1] = (uint8_t)degree_of(64); ui.roll_view[2] = (uint8_t)degree_of(69);
    ui.row[PG_FX] = 1;                                  /* the delay */
    ui.page = PG_HOME;
    ui.octave = 0;
    ui.touch_k = -1;
    for (s = 0; s < DMK_SYNTHS; s++) { sound_put(s, ui_first[s]); ui.preset[s] = ui.val[s][PG_SOUND][0][0]; }
    ui.arr_cur = 0; ui.arr_bars = 4; ui.copy_arm = 0; ui.full = 0;
    ui.ren_slot = 0xFF;                                 /* (no name being turned) */
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
        {                                               /* its progression's number (its name in the card's title: it
                                                           was cut here); the users' U1-U4 */
            int pi = choice_index(choice_of(PG_CHORD, 0, 4), ui.val[0][PG_CHORD][0][4]);
            if (pi >= 16) o = str_cat(o, "U");
            fmt_int(o, pi >= 16 ? pi - 15 : pi + 1);
        }
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
    if (page == PG_GLOBAL && row == 3 && k == 3) { str_cat(o, THEME_NAME[theme]); return; }
    if (page == PG_FX && row == 3 && k < 2 && !v) { str_cat(o, "Off"); return; }   /* a send at 0: none */
    if (page == PG_CHORD && row == 1 && k == 1) {       /* the mode: Phrygian, Mixolydian short in the field (cut), whole
                                                           in the card's title */
        static const char *const MODEFIELD[6] = {"Aeolian", "Dorian", "Phryg", "Lydian", "Mixo", "Ionian"};
        str_cat(o, MODEFIELD[choice_index(c, v) % 6]);
        return;
    }
    if (page == PG_SOUND && row == 0 && k == 0) {       /* the sound's number; its card under it has the name */
        if (v < 9) *o++ = '0';
        fmt_int(o, v + 1);
        return;
    }
    if (page == PG_HOME && k == 4) { fmt_int(o, seq_bpm); return; }   /* the tempo as it plays (at least 20; Ext: the clock's) */
    if (page == PG_SOUND && row == 1 && k == 4) { str_cat(o, DMK_SAMPLES[v < DMK_SAMPLES_N ? v : 0].name); return; }   /* the layer's */
    if (page == PG_SOUND && row == 1 && k == 0 && !v) { str_cat(o, "Off"); return; }
    if (page == PG_SOUND && row == 2 && k == 0) {       /* the glide: off, or its time in ms or s */
        if (!v) { str_cat(o, "Off"); return; }
        { int ms = T_EXPMS[v]; if (ms < 1000) str_cat(fmt_int(o, ms), " ms"); else { o = fmt_int(o, ms / 1000); *o++ = '.'; fmt_int(o, ms / 100 % 10); str_cat(o, " s"); } }
        return;
    }
    if (page == PG_SOUND && row == 2 && (k == 1 || k == 2)) { if (!v) str_cat(o, "Off"); else str_cat(fmt_int(o, v * 100 / 127), "%"); return; }
    if (page == PG_FILTER && k == 3 && !v) { str_cat(o, "Off"); return; }
    if (page == PG_ROLL && row == 1 && (k == 0 || k == 2)) {   /* the roll's Vary: how often a note moves, plays (Range: a choice) */
        if (!k && !v) { str_cat(o, "Off"); return; }
        str_cat(fmt_int(o, v * 100 / 127), "%");
        return;
    }
    if (page == PG_FX && row == 1 && k == 0 && !v) { str_cat(o, "Off"); return; }
    if (page == PG_ROLL && row == 2 && k == 3) { if (v >= 127) str_cat(o, "Legato"); else str_cat(fmt_int(o, (8 + v) * 100 / 136), "%"); return; }   /* the gate */
    if (page == PG_FX && row == 2 && k == 2 && choice_index(choice_of(PG_FX, 2, 4), *vp(PG_FX, 2, 4)) == FO_TREM) {
        str_cat(o, TREMS[v * TREM_N / 128]);            /* the tremolo's rate: its division of the tempo */
        return;
    }
    if (page == PG_LFO && row == 0 && k == 0) {         /* the rate, Hz; synced: the division */
        const choice_t *sc = choice_of(PG_LFO, 1, 0);
        int sy = choice_index(sc, *vp(PG_LFO, 1, 0));
        if (sy) { str_cat(o, SYNCS[sy]); return; }
        {
            uint32_t mhz = T_LFO[v] / 1558u;            /* the step a block is 1558 a millihertz */
            if (mhz < 1000u) { *o++ = '0'; *o++ = '.'; *o++ = (char)('0' + mhz / 100u); *o++ = (char)('0' + mhz / 10u % 10u); *o = 0; }
            else { o = fmt_int(o, (int)(mhz / 1000u)); if (mhz < 10000u) { *o++ = '.'; o = fmt_int(o, (int)(mhz / 100u % 10u)); } }
            str_cat(o, " Hz");
        }
        return;
    }
    if (page == PG_LFO && row == 0 && (k == 1 || k == 3) && !v) { str_cat(o, "Off"); return; }
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
    for (k = 0; k < DMK_KEYS; k++) if (ui.keys_down[k] && ui.key_roll[k] == 1 && ui.key_sy[k] == ui.synth) return k;   /* (SELECT since: not this roll's) */
    return -1;
}
static void note_name(char *o, int n)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    fmt_int(str_cat(o, NM[n % 12]), n / 12 - 1);
}

static int lit(int k) { return ui.touch_k == k && ui.now - ui.touch_ms < 1000u; }
/* ---- SAVE (the owner's two pages): Projects, the fourteen slots in a list; Sounds, the three tracks' */
#define SAVE_ROWS 4                                     /* the projects in view */
static void ren_flush(void)                             /* a name changed: written now */
{
    if (ui.ren_slot >= ST_SONGS) return;
    if (song_rename(ui.ren_slot, ui.ren_v)) notice("Not renamed");
    ui.ren_slot = 0xFF;
}
/* the cursor to project at; the list moves only when it passes the rows in view (the owner: not every click) */
static void save_pick(int at)
{
    ren_flush();
    ui.song_pick = (uint8_t)(at < 0 ? 0 : at >= ST_SONGS ? ST_SONGS - 1 : at);
    if (ui.song_pick < ui.list_top) ui.list_top = ui.song_pick;
    if (ui.song_pick >= ui.list_top + SAVE_ROWS) ui.list_top = (uint8_t)(ui.song_pick - SAVE_ROWS + 1);
}
static const uint8_t *save_nmv(int i) { return ui.ren_slot == i ? ui.ren_v : song_nmv[i]; }   /* as shown: the one turned */
static void save_open(void)                             /* SAVE from another page: Projects, the cursor on the one open */
{
    ui.page = PG_DISCS; ui.row[PG_DISCS] = 0;
    save_pick(ui.song);
    if (!(ui.hinted >> PG_DISCS & 1u)) { notice("ALGO: projects, sounds"); ui.hinted |= (uint16_t)(1u << PG_DISCS); }
}
/* a row not built yet: shown, marked soon, passed over (none now) */
static int row_soon(int page, int row)
{
    (void)page; (void)row;
    return 0;
}
static int row_next(int page, int row)                  /* the page's next row that is built */
{
    int i;
    for (i = 1; i <= PAGE[page].rows; i++) { int r = (row + i) % PAGE[page].rows; if (!row_soon(page, r)) return r; }
    return row;
}
static int rows_built(int page) { int r, n = 0; for (r = 0; r < PAGE[page].rows; r++) n += !row_soon(page, r); return n; }

/* SELECT scrolls a page's rows where it has more than one (Home's are views of the synths, and there SELECT picks
 * the synth, as on the roll, the filter and the amp), and the songs' slots */
static int sel_scrolls(int page)
{
    return PAGE[page].rows > 1 && page != PG_HOME && page != PG_ROLL && page != PG_DISCS;   /* (the roll's: SEQ again; Save's:
                                                           ALGO, its SELECT the project or the track) */
}

/* a row with nothing behind it: those not built, MEDIA while its effect is off */
static int row_off(int page, int row)
{
    if (page == PG_FX && row < 3) return choice_index(choice_of(PG_FX, row, 4), *vp(PG_FX, row, 4)) == 0;
    if (page == PG_GLOBAL && row < 3) return choice_index(choice_of(PG_GLOBAL, row, 4), *vp(PG_GLOBAL, row, 4)) == 0;
    return 0;
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
    if (c && c->n > 1 && !off && c->n * (DOT + 3) - 3 > w) {   /* too many dots: a tick a choice, so a click is a tick (the
                                                           owner: the bar jumped, its ticks more than the choices) */
        int sel = choice_index(c, v);
        for (k = 0; k < c->n; k++) {
            int tx = x + k * (w - 2) / (c->n - 1);
            if (k == sel) round_fill(tx, y, 2, 10, 1, ink);
            else vline(tx, y + 3, 6, k < sel ? C_INK : C_LINE);
        }
        return;
    } else if (c && c->n > 1 && !off) {
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
          : page == PG_ARR ? arr_meter(row, i) : *vp(page, row, i), choice_of(page, row, i), on, 0);
}

/* the title, its row as the grey half (one size, one line), and the synths at the right: the current one filled */
static void head(void)
{
    const page_t *pg = &PAGE[ui.page];
    int x = text(PAD + 1, 6, pg->name, &FH, C_INK), i;
    if (pg->rows > 1 && !(ui.page == PG_ROLL && !ui.row[PG_ROLL])) text(x + 7, 6, pg->row[ui.row[ui.page]], &FH, C_LABEL);
    else if (ui.page == PG_ROLL) {                      /* which of the synth's patterns (its Vary row: named) */
        char t[12];
        fmt_int(str_cat(t, "Pattern "), syn_pat(ui.synth) + 1);
        text(x + 7, 6, t, &FH, C_LABEL);
    }
    {                                                   /* the synths by their sounds' initials, S F G: the current lit */
        int rx = DMK_W - PAD - 1, bw = 17, y = 6 + FH.ascent - FS.ascent - 3;
        for (i = DMK_SYNTHS - 1; i >= 0; i--) {
            char ini[2] = {DMK_SAMPLES[VS(i, PG_SOUND, 0, 0)].name[0], 0};
            int bx = rx - bw;
            if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
            if (i == ui.synth) {                        /* the current one lit, its white letter on the light: the
                                                           light held under its pale core, so the letter stays clear */
                holo_heat = 72; round_fill(bx, y - 3, bw, bw, 5, HOLO); holo_heat = 255;
            }
            text_c(bx + bw / 2 + 1, y, ini, &FS, i == ui.synth ? (THEME_DARK[theme] ? C_INK : C_PAPER) : C_LABEL);   /* (white on it) */
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
            if (j < 0) {
                int x = i * COL + PAD;
                text_fit(x, Y_FIELDS, COL - 2 * PAD, HELD[i], &FS, C_LABEL);
                text(x, Y_FIELDS + 16, "—", &FM, C_LINE);
                meter(x, Y_FIELDS + 44, COL - 2 * PAD, 0, 0, 0, 1);
                continue;
            }
            if (i == 0) fmt_int(v, n->step + 1);
            if (i == 1) note_name(v, n->note);
            if (i == 2) fmt_int(v, n->len);
            if (i == 3) fmt_int(n->off ? str_cat(v, "+") : v, n->off * 15000 / seq_bpm / 32);   /* how late in its step, ms */
            field_show(i, HELD[i], v, i == 0 ? n->step * 127 / st : i == 1 ? (degree_of(n->note) - 14) * 127 / 35 : i == 2 ? n->len * 8 : n->off * 127 / 31);
        }
        return;
    }
    if (ui.row[PG_ROLL]) {                              /* its Vary, its Arp: the page's own fields */
        int k, r = ui.row[PG_ROLL];
        for (k = 0; k < 4; k++) field(k, PAGE[PG_ROLL].k[r][k], PG_ROLL, r);
        return;
    }
    fmt_int(v, ui.roll_cur[ui.synth] + 1);
    field_show(0, "Cursor", v, ui.roll_cur[ui.synth] * 127 / st);
    v[0] = 0;
    note_name(v, note_of(ui.roll_view[ui.synth]));
    field_show(1, "View", v, (ui.roll_view[ui.synth] - 14) * 127 / 29);
    {                                                   /* K3 (where the pen's length was): how it follows the chords */
        static const char *const FW_SHORT[FW_COUNT] = {"Free", "Key", "Chord", "Root", "Plays", "With 1", "With 2", "With 3"};
        int f = ch.follow[ui.synth];
        field_show(2, "Follow", FW_SHORT[f], f * 127 / (FW_COUNT - 1));
    }
    fmt_int(v, q->steps);
    field_show(3, "Steps", v, q->steps * 127 / SEQ_STEPS);
}

/* SAVE's fields. Projects: K1 the project, K2 K3 its name's two words (a project saved: written once they rest).
 * Sounds: K1 the track, K2 a recording, K3 a sound kept in the library, the library's count */
static void save_field(int i, const char *label, const char *v, int m, int off)
{
    int x = i * COL + PAD, w = COL - 2 * PAD, on = lit(i);
    text_fit(x, Y_FIELDS, w, label, &FS, C_LABEL);
    text_mfit(x, Y_FIELDS + 16, w, v, off ? C_LINE : on ? HOLO : C_INK);
    meter(x, Y_FIELDS + 44, w, m < 0 ? 0 : m > 127 ? 127 : m, 0, on && !off, off);
}
static void save_fields(void)
{
    char v[16];
    v[0] = 0;
    if (!ui.row[PG_DISCS]) {
        int at = ui.song_pick;
        const uint8_t *nv = save_nmv(at);
        int named = song_used(at) && nv[2] == NM_MARK;
        if (at < 9) str_cat(v, "0");
        fmt_int(v + (at < 9), at + 1);
        save_field(0, "Project", v, at * 127 / (ST_SONGS - 1), 0);
        {                                               /* its name whole across K2-K4 (never cut), a word a knob */
            int x = COL + PAD, w = 3 * COL - 2 * PAD;
            char nm[24];
            nm[0] = 0;
            if (named) nm_of(nm, nv);
            text_fit(x, Y_FIELDS, w, "Name", &FS, C_LABEL);
            text_mfit(x, Y_FIELDS + 16, w, named ? nm : "\xE2\x80\x94", !named ? C_LINE : lit(1) || lit(2) ? HOLO : C_INK);
            meter(x, Y_FIELDS + 44, COL - 2 * PAD, named ? nv[0] * 127 / 23 : 0, 0, named && lit(1), !named);
            meter(x + COL, Y_FIELDS + 44, COL - 2 * PAD, named ? nv[1] * 127 / 23 : 0, 0, named && lit(2), !named);
        }
        return;
    }
    {
        int s = ui.synth, rec = VS(s, PG_SOUND, 0, 0), user = ui.preset[s] >= DMK_SAMPLES_N ? lib_nth(ui.preset[s] - DMK_SAMPLES_N) : -1;
        fmt_int(v, s + 1);
        save_field(0, "Track", v, s * 127 / (DMK_SYNTHS - 1), 0);
        v[0] = 0; if (rec < 9) str_cat(v, "0"); fmt_int(v + (rec < 9), rec + 1);
        save_field(1, "Sound", v, rec * 127 / (DMK_SAMPLES_N - 1), 0);
        v[0] = 0;
        if (user >= 0) { if (user < 9) str_cat(v, "0"); fmt_int(v + (user < 9), user + 1); }
        save_field(2, "User", user >= 0 ? v : "\xE2\x80\x94", user >= 0 ? user * 127 / (ST_USER_N - 1) : 0, user < 0);
        fmt_int(v, lib_count());                        /* (of 32: its meter) */
        save_field(3, "Library", v, lib_count() * 127 / ST_USER_N, 0);
    }
}

static void fields_band(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], k;
    if (ui.page == PG_ROLL) { roll_fields(); return; }
    if (ui.page == PG_DISCS) { save_fields(); return; }
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
        if (ui.page == PG_FX && r != 1 && r < 3) l = FXK[r][choice_index(choice_of(PG_FX, r, 4), *vp(PG_FX, r, 4))][k];
        if (ui.page == PG_GLOBAL && r == 2) l = FXK[1][choice_index(choice_of(PG_GLOBAL, 2, 4), *vp(PG_GLOBAL, 2, 4))][k];
        if (ui.page == PG_GLOBAL && r == 1 && choice_index(choice_of(PG_GLOBAL, 1, 4), *vp(PG_GLOBAL, 1, 4)) == 2) { static const char *const GK[4] = {"Return", "Size", "Density", "Texture"}; l = GK[k]; }
        if (ui.page == PG_GLOBAL && r == 0) {           /* the later verbs' own knobs */
            int t = choice_index(choice_of(PG_GLOBAL, 0, 4), *vp(PG_GLOBAL, 0, 4));
            if (t == 9 && k == 3) l = "Shine";               /* (Shimmer's: its octave up; "Shimmer" was cut) */
            if (t == 10 && k == 2) l = "Length";
            if (t == 11 && k == 2) l = "Hold";
        }
        field(k, l, ui.page, r);
    }
}

/* the page's rows, as a pagination on the foot's line (the owner: subtle dots, centred in it): the row open a short
 * pill in ink, the others dots; in the middle, or where the foot's words leave room nearest it (4 px of air each
 * side); none where they leave none */
static int dots_w(int n) { return n < 2 ? 0 : n * 3 + (n - 1) * 3 + 5; }   /* (the open one 5 px longer) */
static int dots_at = -1;                                /* where they were drawn (the host's checks) */
static void page_dots(void)
{
    int p = ui.page, n = rows_built(p), w = dots_w(n), i, x, y, x0, best = -1, bd = DMK_W;
    uint8_t used[DMK_W];
    if (n < 2) return;
    for (x = 0; x < DMK_W; x++) {
        used[x] = 0;
        for (y = Y_FOOT + 3; y < Y_FOOT + 12 && !used[x]; y++) used[x] = (uint8_t)(px_get(x, y) != C_PAPER);
    }
    for (x0 = PAD; x0 + w <= DMK_W - PAD; x0++) {
        int ok = 1, d = x0 + w / 2 - DMK_W / 2;
        for (x = x0 - 4; x < x0 + w + 4 && ok; x++) if (x >= 0 && x < DMK_W && used[x]) ok = 0;
        if (d < 0) d = -d;
        if (ok && d < bd) { bd = d; best = x0; }
    }
    dots_at = best;
    if (best < 0) return;
    for (i = 0, x = best; i < PAGE[p].rows; i++) {
        int open = i == ui.row[p];
        if (row_soon(p, i)) continue;
        round_fill(x, Y_FOOT + 6, open ? 8 : 3, 3, 1, open ? C_INK : C_LABEL);   /* (the capitals' middle) */
        x += (open ? 8 : 3) + 3;
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
    if (ui.page == PG_DISCS) {                          /* SAVE: what the buttons do with the one picked */
        int at = ui.song_pick, used = song_used(at);
        if (!store_on()) { text(PAD + 1, Y_FOOT + 2, "No store here", &FS, C_LABEL); return; }
        if (r) {
            text(PAD + 1, Y_FOOT + 2, "SAVE: keep it", &FS, C_LABEL);
            text_r(DMK_W - PAD - 1, Y_FOOT + 2, "OCT\xE2\x88\x92: init", &FS, C_LABEL);
            return;
        }
        text(PAD + 1, Y_FOOT + 2, !used ? "SAVE: save here" : at == ui.song ? "SAVE: save over" : "PLAY: open", &FS, C_LABEL);
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, used ? "OCT\xE2\x88\x92: delete" : "PLAY: new", &FS, C_LABEL);
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
    if (ui.rec_ready && seq_rec && !seq_on) {           /* ready: what starts it */
        text(PAD + 1, Y_FOOT + 2, "Ready", &FS, HOLO);
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, "A key starts", &FS, C_LABEL);
        return;
    }
    if (ui.page == PG_ROLL) {                           /* the zoom, or a held note's velocity */
        char v[16];
        int h = roll_held(), j = h >= 0 ? seq_find(ui.synth, ui.key_step[h], ui.key_note[h]) : -1;
        int arow = ui.row[PG_ROLL] == 2 && j < 0, x = text(PAD + 1, Y_FOOT + 2, j >= 0 ? "Velocity" : arow ? "Hold" : "Zoom", &FS, C_LABEL);
        if (arow) val_text(v, PG_ROLL, 2, 4);           /* the Arp's HOLD */
        else fmt_int(v, j >= 0 ? sq[ui.synth].n[j].vel : roll_z());
        text_fit(x + 6, Y_FOOT + 2, 100 - x, v, &FS, lit(4) ? HOLO : C_INK);
        {                                               /* the chord at the cursor (or where it plays), else how it follows */
            const seq_t *q = &sq[ui.synth];
            uint8_t nt[8];
            char cn[24];
            int t = seq_on ? seq_playing_step(ui.synth) : ui.roll_cur[ui.synth], c = 0, i;
            for (i = 0; i < q->count && c < 8; i++) if (q->n[i].step <= t && t < q->n[i].step + q->n[i].len) nt[c++] = q->n[i].note;
            cn[0] = 0;
            if (c >= 2 && chord_of(nt, c, cn)) text_r(DMK_W - PAD - 1, Y_FOOT + 2, cn, &FS, C_INK);
            else text_r(DMK_W - PAD - 1, Y_FOOT + 2, FOLLOWS[ch.follow[ui.synth]], &FS, C_LABEL);   /* (SEQ + ALGORITHM) */
        }
        return;
    }
    if (ui.page == PG_GLOBAL && r == 3) {               /* GLO's setup: how the machine is doing (the owner heard
                                                           it overload): the load of the sound and the second core, the
                                                           times the sound ran short, the mix past full */
        char t[28], *o = t;
        if (ui.now - ui.loud_ms >= 500u) { ui.loud_seen = sys.loud - ui.loud_mark; ui.loud_mark = sys.loud; ui.loud_ms = ui.now; }
        if (sys.on) { o = fmt_int(str_cat(o, "CPU "), sys.cpu); o = fmt_int(str_cat(o, "% \xC2\xB7 "), sys.core2); str_cat(o, "%"); }
        else str_cat(o, "CPU \xE2\x80\x94");
        text(PAD + 1, Y_FOOT + 2, t, &FS, sys.on && sys.cpu > 85 ? HOLO : C_LABEL);
        o = t; t[0] = 0;
        if (sys.shorts) o = fmt_int(str_cat(o, "Dropouts "), (int)(sys.shorts > 9999u ? 9999u : sys.shorts));
        if (ui.loud_seen) str_cat(o, t[0] ? ", loud" : "Loud: past full");
        if (!t[0]) str_cat(t, "Clean");
        text_r(DMK_W - PAD - 1, Y_FOOT + 2, t, &FS, sys.shorts || ui.loud_seen ? HOLO : C_LABEL);
        return;
    }
    if (pg->algo[r][0]) {                               /* the ALGORITHM encoder's value, under the cards */
        char v[16];
        int typed = ui.page == PG_FX || (ui.page == PG_GLOBAL && r < 3) || ui.page == PG_FILTER, nd = rows_built(ui.page), x;
        int end = nd > 1 ? (DMK_W - dots_w(nd)) / 2 - 4 : 130;   /* where its words end: short of the dots */
        val_text(v, ui.page, r, 4);
        if (PAD + 1 + text_w(pg->algo[r], &FS) + 6 + text_w(v, &FS) <= end)   /* its name, then the value; a long value */
            x = text(PAD + 1, Y_FOOT + 2, pg->algo[r], &FS, C_LABEL) + 6;   /* alone (the page above names it): never cut */
        else x = PAD + 1;
        text_fit(x, Y_FOOT + 2, end - x, v, &FS, lit(4) ? HOLO : C_INK);
        if (typed && !sel_scrolls(ui.page)) text_r(DMK_W - PAD - 1, Y_FOOT + 2, "PRESET too", &FS, C_LABEL);   /* (where SELECT
                                                           scrolls the rows, the dots say so: page_dots) */
        if (ui.page == PG_CHORD) text_r(DMK_W - PAD - 1, Y_FOOT + 2, !seq_on && seq_rec ? "Keys write" : "REC: write", &FS, C_LABEL);

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
    {3, {0, 64, 96}, {-1, -1, C_HOLO0}},                                   /* DESK: the wall, dark, a glow low by the screen */
    {5, {0, 22, 50, 74, 92}, {-1, -1, C_HOLO0, C_HOLO1, C_HOLO1}},       /* FOREST: after sunset, the fog's blue */
    {2, {0, 80}, {-1, -1}},                                                /* LIVING: indoors (its wall drawn) */
    {7, {0, 8, 18, 28, 38, 46, 52}, {-1, C_HOLO1, C_HOLO2, C_HOLO4, C_HOLO5, C_HOLO6, C_HOLO7}},   /* VILLA: the sun on the sea */
    {7, {0, 12, 24, 36, 46, 56, 66}, {-1, C_HOLO0, C_HOLO2, C_HOLO3, C_HOLO4, C_HOLO5, C_HOLO6}},  /* CALIFORNIA */
    {5, {0, 20, 38, 54, 68}, {-1, C_HOLO0, C_HOLO1, C_HOLO2, C_HOLO6}},    /* HARBOUR: blue fog, lit low by the town */
    {4, {0, 50, 74, 80}, {-1, -1, C_HOLO0, C_HOLO1}},                      /* LIGHTHOUSE: night, the sea's horizon light */
    {5, {0, 34, 56, 72, 88}, {-1, -1, C_HOLO0, C_HOLO1, C_HOLO2}}};        /* CITY: night, its glow low behind the towers */
static const uint8_t OUTDOOR[DMK_SCENES] = {1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1};   /* the sky spreads in full screen */
static const uint8_t STARS[DMK_SCENES] = {14, 14, 14, 34, 14, 0, 0, 0, 0, 6, 0, 40, 30};
static const uint8_t CLOUDS[DMK_SCENES] = {3, 3, 3, 1, 1, 0, 0, 0, 2, 0, 0, 0, 0};
static void land_band(int sc, int dk, int lt);          /* (the later six: dmk_lands.c) */
static void land_anim(int sc, uint32_t tick, int dk, int lt);
static void land_light(int sc, int i, int on, uint32_t tick);
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

/* building a scene is long (to ~0.9M instructions): the sound is let in every 2048 pixels, or with every effect on it
 * ran short while SELECT changed the landscape (the owner heard it) */
static unsigned fg_n;
#ifdef __GNUC__
__attribute__((noinline))                               /* (called everywhere a scene is built: inlined, 13 KB of flash) */
#endif
static void fg_put(int x, int y, int c)
{
    if (++fg_n >= 2048u) { fg_n = 0; ui_yield(); }
    if (x >= 0 && x < SC_W && y >= 0 && y < SC_H) sc_fg[y * SC_W + x] = (uint8_t)c;
}
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
        if (!(y & 7)) ui_yield();
        if (sy < 0) sy = 0;
        for (x = 0; x < SC_W; x++) {
            int sx = x + sh < SC_W ? x + sh : x, c = sc_fg[sy * SC_W + sx] != 0xFF ? sc_fg[sy * SC_W + sx] : sky_at(sc, sx, sy, dk);
            sc_fg[y * SC_W + x] = (uint8_t)(bay(x, y) < 9 ? dk : c);
        }
    }
}

DMK_SMALL static void scene_build(int sc, int dk, int lt)
{
    int x, y, k;
    for (k = 0; k < SC_W * SC_H; k++) sc_fg[k] = 0xFF;
    if (sc >= 6 || sc == 2) { land_band(sc, dk, lt); return; }   /* (the later ones, and the beach since 0.4.11) */
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
    if (sc_key != (uint8_t)(theme * 16 + sc)) {
        ui_yield();
        scene_build(sc, dk, lt);
        ui_yield();
        for (i = 0; i < SC_W / 2 * SC_H; i++) {
            int a = sc_fg[i * 2], b = sc_fg[i * 2 + 1];
            sc_fgp[i] = (uint8_t)((a != 0xFF ? a << 4 : 0) | (b != 0xFF ? b : 0));
            sc_fgm[i] = (uint8_t)((a != 0xFF ? 0xF0 : 0) | (b != 0xFF ? 0x0F : 0));
        }
        sc_key = (uint8_t)(theme * 16 + sc); sc_tick = 0xFFFFFFFFu;
    }
    if (tick != sc_tick) {                              /* a new frame of the scene: drawn, then held */
        sc_tick = tick;
        for (y = 0; y < SC_H; y++) {                    /* the sky: a row is two bytes over and over (its dither's period) */
            uint8_t *b = &fb[((SY + y) * DMK_W + SX) >> 1];
            uint8_t p0 = (uint8_t)(sky_at(sc, 0, y, dk) << 4 | sky_at(sc, 1, y, dk)), p1 = (uint8_t)(sky_at(sc, 2, y, dk) << 4 | sky_at(sc, 3, y, dk));
            for (x = 0; x < SC_W / 2; x += 2) { b[x] = p0; b[x + 1] = p1; }
        }
        for (i = 0; i < STARS[sc]; i++) {               /* the stars, twinkling (the night has many; the desk none) */
            uint32_t h = hsh((uint32_t)i * 7919u + (uint32_t)sc);
            if (hsh(h + tick / 3u) & 3u) px_put(SX + (int)(h % (uint32_t)SC_W), SY + 3 + (int)((h >> 12) % (sc == 3 || sc == 11 ? 54u : sc == 12 ? 30u : 22u)), lt);
        }
        if (sc == 0) { round_fill(SX + 22, SY + 18, 12, 12, 6, lt); }   /* the moon over the peaks, early */
        if (sc == 1) round_fill(SX + 140, SY + 50, 22, 22, 11, HOLO);    /* the sun in the valley */
        if (sc == 2) round_fill(SX + 128, SY + 56, 30, 30, 15, HOLO);    /* the sun, low on the sea */
        if (sc == 3) { round_fill(SX + 150, SY + 14, 30, 30, 15, HOLO); round_fill(SX + 156, SY + 20, 18, 18, 9, lt); }   /* the moon, haloed */
        if (sc == 4) round_fill(SX + 158, SY + 12, 10, 10, 5, lt);   /* the moon over the city, in the window's corner */
        if (sc == 8) round_fill(SX + 196, SY + 30, 20, 20, 10, HOLO);   /* the sun over the sea */
        if (sc == 9) round_fill(SX + 92, SY + 30, 44, 44, 22, HOLO);    /* the sun at the street's end */
        if (sc == 10) round_fill(SX + 40, SY + 30, 18, 18, 9, C_HOLO6); /* the sun, pale in the fog */
        if (sc == 11) { round_fill(SX + 70, SY + 18, 22, 22, 11, HOLO); round_fill(SX + 74, SY + 22, 14, 14, 7, lt); }   /* the moon */
        for (i = 0; i < CLOUDS[sc]; i++) {              /* clouds, drifting (in the room's window one) */
            int w = i == 0 ? 36 : i == 1 ? 26 : 20, cy = i == 0 ? 46 : i == 1 ? 34 : 40;
            int cx = (int)((i == 0 ? 30u : i == 1 ? 150u : 95u) + tick / (3u + (uint32_t)i)) % (SC_W + 80) - 40;
            if (sc == 3) cy = 60;
            if (sc == 4) cy = 30;
            if (sc == 8) cy = 30 + i * 8;               /* (under Home's name) */
            cloud(cx, cy, w, lt);
        }
        ui_yield();
        for (y = 0, i = 0; y < SC_H; y++) {             /* the still world over the sky */
            uint8_t *b = &fb[((SY + y) * DMK_W + SX) >> 1];
            for (x = 0; x < SC_W / 2; x++, i++) if (sc_fgm[i]) b[x] = (uint8_t)((b[x] & ~sc_fgm[i]) | sc_fgp[i]);
        }
        if (sc >= 6 || sc == 2) land_anim(sc, tick, dk, lt);
        if (sc == 1 || sc == 2) {                       /* the sun on the water: glints that move */
            int sea = sc == 1 ? 84 : 74, cx = sc == 1 ? 151 : 143;
            for (y = sea; y < (sc == 2 ? 90 : SC_H); y += 2) {   /* (the beach's: to its wave) */
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
            } else if (sc >= 6 || sc == 2) land_light(sc, i, on, tick);
            else if (sc == 3) {                         /* fireflies, wandering over the forest */
                lx = 40 + i * 64 + tri((int)tick + i * 11, 40, 14); ly = 80 + tri((int)tick * 2 + i * 7, 26, 8);
                if (on || (hsh(tick + (uint32_t)i) & 1u)) fill(SX + lx, SY + ly, 2, 2, on ? C_HOLO7 : C_HOLO4);
            } else {                                    /* lanterns along the shore, or the treeline */
                lx = sc == 0 ? 60 + i * 50 : 30 + i * 10; ly = sc == 0 ? 92 : 50 + i * 3 + 18 + (i * 7 % 5);
                fill(SX + lx, SY + ly, 2, 3, on ? C_HOLO7 : C_A_INK2);
            }
        }
        for (y = 0; y < SC_H; y++)                      /* held for the frames between (square: the full screen's) */
            for (x = 0; x < SC_W; x += 2) sc_out[y * (SC_W / 2) + x / 2] = fb[((SY + y) * DMK_W + SX + x) >> 1];
    } else
        for (y = 0; y < SC_H; y++)
            for (x = 0; x < SC_W; x += 2) fb[((SY + y) * DMK_W + SX + x) >> 1] = sc_out[y * (SC_W / 2) + x / 2];
    for (y = 0; y < R_CARD; y++)                        /* Home's card: the paper outside its corners */
        for (x = 0; x < R_CARD; x++) {
            int dx = R_CARD - x, dy = R_CARD - y;
            if (dx * dx + dy * dy > R_CARD * R_CARD) {
                px_put(SX + x, SY + y, C_PAPER); px_put(SX + SW - 1 - x, SY + y, C_PAPER);
                px_put(SX + x, SY + SH - 1 - y, C_PAPER); px_put(SX + SW - 1 - x, SY + SH - 1 - y, C_PAPER);
            }
        }
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
        int row = 6 - (degree_of(n->note) - view), s0 = n->step - x0, len = n->len, w, lx, up = snap(n->note) != n->note ? rh / 2 : 0;
        if (s0 < 0 || s0 >= end) continue;
        if (s0 + len > end) len = end - s0;
        lx = n->off * cw / 32;                          /* late in its step (a strum recorded): drawn late */
        w = len * cw - 2 - lx < 2 ? 2 : len * cw - 2 - lx;
        if (row < 0 || row > 6) {                       /* above or below the view: a mark at its edge, where it plays */
            round_fill(gx + s0 * cw + 1 + lx, row < 0 ? gy - 5 : gy + rh * 7 + 2, w, 2, 1, C_LABEL);
            continue;
        }
        round_fill(gx + s0 * cw + 1 + lx, gy + row * rh + 3 - up, w, rh - 6, (rh - 6) / 2, !seq_on && n->step == at ? HOLO : C_INK);   /* (off the
                                                           scale: between its neighbours) */
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

/* the lit shapes' bits move as the landscapes' do (a new arrangement every 140 ms): a few lights travel along the
 * edge, a few twinkle in the body. Drawn after the light is made, so a graph's cached depths stay */
static void graph_life(int x0, int w, const int16_t *ya, const int16_t *yb, int salt)
{
    uint32_t t = ui.now / 140u;
    int i;
    for (i = 0; i < 6; i++) {                           /* along the edge, each its own pace: a short light, two rows */
        uint32_t h = hsh((uint32_t)(i * 7 + salt));
        int x = (int)((h % (uint32_t)w + t * (1u + (h >> 8) % 3u)) % (uint32_t)w), k;
        for (k = 0; k < 3 && x + k < w; k++)
            if (yb[x + k] - ya[x + k] > 4) { px_put(x0 + x + k, ya[x + k] + 1, k == 1 ? C_HOLO7 : C_HOLO6); px_put(x0 + x + k, ya[x + k] + 2, C_HOLO6); }
    }
    for (i = 0; i < 8; i++) {                           /* inside, twinkling */
        uint32_t h = hsh((uint32_t)(i * 131 + salt) + t);
        int x = (int)(h % (uint32_t)w), span = yb[x] - ya[x] - 4;
        if (span > 2) { int y = ya[x] + 2 + (int)((h >> 12) % (uint32_t)span); px_put(x0 + x, y, C_HOLO7); if (x + 1 < w) px_put(x0 + x + 1, y, C_HOLO6); }
    }
}
static int voice_newest(int s)                          /* the newest sounding voice of synth s, or -1 */
{
    int v, best = -1;
    for (v = 0; v < DMK_VOICES; v++) if (vc[v].stage && vc[v].sy == s && (best < 0 || vc[v].age > vc[best].age)) best = v;
    return best;
}
static void spark(int x, int y)                         /* a point of light: a pale core in a pink ring */
{
    fill(x - 1, y - 2, 3, 5, C_HOLO5); fill(x - 2, y - 1, 5, 3, C_HOLO5);
    fill(x - 1, y - 1, 3, 3, C_HOLO7);
}

/* VOICE: two cards. Velocity: the level a note gets for how hard it is played, soft at the left, the last note's a
 * point on it; Glide: a note's pitch over its first second, from the last note's. Under GRIT both are drawn in steps,
 * the voice's coarser rate */
static void stage_voice(void)
{
    int16_t ys[DMK_W], yb[DMK_W];
    const synth_t *p = &syn[ui.synth];
    int i, k, st = p->grit ? 2 << p->grit : 1, nv = voice_newest(ui.synth);
    for (k = 0; k < 2; k++) {
        int cx = cell_x(SX, SW, 2, k), cw = cell_w(SX, SW, 2, k), x0 = cx + PAD + 1, w = cw - 2 * PAD - 2;
        int top = SY + 44, base = SY + SH - 12;
        char t[16];
        t[0] = 0;
        val_text(t, PG_SOUND, 2, k ? 0 : 1);
        card(cx, SY, cw, SH, C_LINE, 1);
        card_title(cx, SY, cw, k ? "Glide" : "Velocity", t, 0);
        if (!k) {
            for (i = 0; i < w; i++) {
                int x = i / st * st, vel = 1 + x * 126 / (w - 1);
                int32_t vg = 32768 - p->vel * (32768 - vel * vel * 2) / 127;
                ys[i] = (int16_t)(base - (base - top) * vg / 32768);
                yb[i] = (int16_t)(base + 1);
            }
        } else {
            int at = w / 5;                             /* the note begins here; before, the last note's pitch */
            uint32_t g = 65536, per = 65536;
            if (p->glide) {                             /* a pixel's share of the second: the factor per block, so many blocks */
                int n = (DMK_SR / DMK_SUB) / (w - at), j;
                for (j = 0; j < n; j++) per = (uint32_t)(((uint64_t)per * p->glide) >> 16);
            } else per = 0;
            for (i = 0; i < w; i++) {
                int x = i / st * st;
                if (x < at) g = 65536;
                else if (x == i) g = (uint32_t)(((uint64_t)g * per) >> 16);
                ys[i] = (int16_t)(top + (int)(((uint32_t)(base - top) * (g >> 4)) >> 12));
                yb[i] = (int16_t)(base + 1);
            }
        }
        lit_band(x0, w, ys, yb);
        graph_life(x0, w, ys, yb, 1 + k);
        ink_line(x0, w, ys);
        if (!k && nv >= 0) {                            /* the last note's velocity */
            int px = (vc[nv].vel - 1) * (w - 1) / 126;
            spark(x0 + px, ys[px]);
        }
    }
}

static void stage_sound(void)
{
    int16_t ya[DMK_W], yb[DMK_W];
    int row = ui.row[PG_SOUND], lay = row == 1;
    const dmk_sample_t *sm;
    int i, mid = SY + 76, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, n, loops, on;
    if (row == 2) { stage_voice(); return; }
    i = *vp(PG_SOUND, lay, lay ? 4 : 0);
    sm = &DMK_SAMPLES[i < DMK_SAMPLES_N ? i : 0];
    n = sm->blocks;
    loops = sm->loops && *vp(PG_SOUND, lay, 2) >= 64;
    on = !lay || *vp(PG_SOUND, 1, 0);                   /* the layer: off until it is blended in */
    card(SX, SY, SW, SH, C_LINE, 1);
    if (lay) {
        char t[16];
        t[0] = 0;
        if (on) str_cat(fmt_int(str_cat(t, "Layer, "), *vp(PG_SOUND, 1, 0) * 100 / 127), "%");
        else str_cat(t, "Layer off");
        card_title(SX, SY, SW, sm->name, t, !on);
    } else card_title(SX, SY, SW, sm->name, loops ? "Looped" : "One-shot", 0);
    for (i = 0; i < w; i++) {                           /* the sound's own shape: its body lit, its edge in ink */
        int h = sm->env[i * 48 / w] * 34 / 255;
        if (h < 1) h = 1;
        ya[i] = (int16_t)(mid - h); yb[i] = (int16_t)(mid + h + 1);
    }
    if (on) { lit_band(x0, w, ya, yb); graph_life(x0, w, ya, yb, 1); }
    for (i = 0; i < w; i++) { px_put(x0 + i, ya[i], on ? C_INK : C_LABEL); px_put(x0 + i, yb[i] - 1, on ? C_INK : C_LABEL); }
    if (on) {                                           /* each voice playing it: where it is in the sound */
        int v, y;
        for (v = 0; v < DMK_VOICES; v++) {
            const strm_t *t = lay ? &vc[v].l : &vc[v].m;
            if (vc[v].stage && vc[v].sy == ui.synth && t->s == sm && (!lay || t->on)) {
                int px = t->blk * (w - 1) / (n ? n : 1);
                for (y = ya[px] + 1; y < yb[px] - 1; y++) px_put(x0 + px, y, C_HOLO7);
                spark(x0 + px, ya[px]);
            }
        }
    }
    if (loops && on)                                    /* the loop's ends, as tall as the sample is there */
        for (i = 0; i < 2; i++) {
            int lx = (i ? sm->loop1 : sm->loop0) * (w - 1) / n, lh = mid - ya[lx] + 5;
            round_fill(x0 + lx - 2, mid - lh, 4, 2 * lh, 2, C_INK);
        }
    if (!lay && syn[ui.synth].start) {                  /* where it starts */
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
    graph_life(x0, w, ys, yb, 2);
    ink_line(x0, w, ys);
    {                                                   /* a note playing: where its envelope has the cutoff now */
        int v = voice_newest(ui.synth);
        if (v >= 0 && type != FT_OFF && type != FT_VOWEL && syn[ui.synth].fenv) {
            int idx = clampi(syn[ui.synth].cut + (int32_t)(((int64_t)syn[ui.synth].fenv * vc[v].env) >> 24), 0, 255), lx = x0 + idx * (w - 1) / 255;
            spark(lx, ys[lx - x0] + 1);
        }
    }
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
    graph_life(x0, w, ys, yb, 3);
    ink_line(x0, w, ys);
    for (i = 1; i < 4; i++) round_fill(pts[i][0] - 4, pts[i][1] - 4, 9, 9, 4, lit(i - 1) || (i == 3 && lit(3)) ? HOLO : C_INK);
    {                                                   /* a note playing: its envelope's point, travelling the shape */
        int v = voice_newest(ui.synth);
        if (v >= 0) {
            const voice_t *o = &vc[v];
            int32_t sus = syn[ui.synth].sus >> 8, e = o->env >> 8, full = FULL >> 8;   /* Q16: 32-bit products */
            int px;
            if (o->stage == 1) px = pts[0][0] + a * e / full;
            else if (o->stage == 2 && e > sus + full / 64) px = pts[1][0] + d * (full - e) / (full - sus > 0 ? full - sus : 1);
            else if (o->stage == 2) px = pts[2][0] + (int)(o->blocks / 8u % (unsigned)(hold > 0 ? hold : 1));   /* held: along the sustain */
            else px = pts[3][0] + (sus > 0 ? r * (sus - (e < sus ? e : sus)) / sus : r);
            if (px < x0) px = x0;
            if (px > x0 + w - 1) px = x0 + w - 1;
            spark(px, ys[px - x0]);
        }
    }
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
    graph_life(x0, w, ya, yb, 4);
    ink_line(x0, w, ys);
    if (fade) round_fill(x0 + fade - 4, mid - 4, 9, 9, 4, lit(3) ? HOLO : C_INK);
}

/* the row SELECT is on: its pill framed in the light, 3 px from a pixel outside it in, so the frame has its rim and its
 * body (a hairline would be the body's tone alone) */
static void sel_ring(int x, int y, int w, int h) { round_line(x - 1, y - 1, w + 2, h + 2, (h + 2) / 2, 3, HOLO); }

/* four rows, a pill each: its name, its switch (off), the open one framed in the light */
static void stage_rows(int page, const char *const *names, int n, int open)
{
    int i, h = (SH - (n - 1) * G) / n;
    for (i = 0; i < n; i++) {
        int y = SY + i * (h + G), tx = SX + SW - PAD - 22;
        if (i == open) sel_ring(SX, y, SW, h);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 20) / 2 - 1, names[i], &FM, row_soon(page, i) ? C_LABEL : C_INK);
        if (row_soon(page, i)) { text_r(SX + SW - PAD - 4, y + (h - 12) / 2, "Soon", &FS, C_LABEL); continue; }
        if (page == PG_GLOBAL && i == 3) {              /* the setup: what MIDI sends, at a glance */
            char v[24];
            str_cat(str_cat(v, "MIDI out: "), MIDI_OUTS[choice_index(choice_of(PG_GLOBAL, 3, 1), ui.val[0][PG_GLOBAL][3][1])]);
            text_r(SX + SW - PAD - 4, y + (h - 12) / 2, v, &FS, C_LABEL);
            continue;
        }
        if (page != PG_FX && i >= 3) continue;              /* the setup is not an effect: no switch */
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
    char sub[32], nm[24];
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
        {                                               /* the sound's initial, as the head's (its name was cut) */
            char ini[2] = {DMK_SAMPLES[VS(s, PG_SOUND, 0, 0)].name[0], 0};
            if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
            text(SX + PAD + 2, y + 4, ini, &FS, muted ? C_LINE : C_LABEL);
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

/* SAVE, Projects: the fourteen slots, four in view; the cursor ringed, the list moving only past its ends; a project
 * by its name, an empty slot "Empty" (or "Not saved" where the project open goes), the one open marked */
static void stage_projects(void)
{
    int i, h = (SH - 3 * G) / 4;
    for (i = 0; i < SAVE_ROWS; i++) {
        int n = ui.list_top + i, y = SY + i * (h + G), used = song_used(n), pick = n == ui.song_pick, open = n == ui.song;
        char t[3] = {(char)('0' + (n + 1) / 10), (char)('0' + (n + 1) % 10), 0}, nm[24];
        if (n >= ST_SONGS) break;
        if (pick) sel_ring(SX, y, SW, h);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 12) / 2, t, &FS, C_LABEL);
        nm[0] = 0;
        if (used && ui.ren_slot == n) nm_of(nm, ui.ren_v);   /* (its name as it is being turned) */
        text_fit(SX + 40, y + (h - 20) / 2 - 1, SW - 70, used ? (nm[0] ? nm : song_name(n)) : open ? "Not saved" : "Empty", &FM,
                 used || open ? C_INK : C_LABEL);
        if (open) round_fill(SX + SW - 22, y + h / 2 - 3, 6, 6, 3, C_INK);   /* the project open */
    }
}
/* SAVE, Sounds: the three tracks, each its sound (its recording's name; one from the library, its number) */
static void stage_sounds(void)
{
    int s, h = (SH - 2 * G) / 3;
    for (s = 0; s < DMK_SYNTHS; s++) {
        int y = SY + s * (h + G), user = ui.preset[s] >= DMK_SAMPLES_N ? lib_nth(ui.preset[s] - DMK_SAMPLES_N) : -1;
        char t[2] = {(char)('1' + s), 0};
        if (s == ui.synth) sel_ring(SX, y, SW, h);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 12) / 2, t, &FS, C_LABEL);
        text_fit(SX + 40, y + (h - 20) / 2 - 1, user >= 0 ? SW - 104 : SW - 54, DMK_SAMPLES[VS(s, PG_SOUND, 0, 0)].name, &FM, C_INK);
        if (user >= 0) {
            char u[10];
            fmt_int(str_cat(u, user < 9 ? "User 0" : "User "), user + 1);
            text_r(SX + SW - 14, y + (h - 12) / 2, u, &FS, C_LABEL);
        }
    }
}
static void stage_save(void) { if (ui.row[PG_DISCS]) stage_sounds(); else stage_projects(); }

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

/* a scene full screen (OCT- and OCT+ held; the start): the whole of it at its own pixels, nothing cut (the owner: not
 * its middle doubled, which cut the sides), and taller, each landscape going on above and below it (the owner: not a
 * fade to dark): outdoors its sky spread over the screen down to its horizon, stars, a high cloud, the gulls, the
 * Milky Way; under it the snow and the near pines, the lake's shore and its reeds and a dock, the waves on a beach, a
 * meadow and fireflies; indoors the wall above (a picture and fairy lights; a poster and a shelf) and the floor or the
 * desk's front below. Those parts are built once a scene (lf_build), the moving ones drawn each frame */
#define LF_Y0 ((DMK_H - SC_H) / 2)                      /* the scene's band */
#define LF_X0 ((DMK_W - SC_W) / 2)
#define LF_H LF_Y0                                      /* the rows above it, and below */
static uint8_t lf_ext[2][DMK_W / 2 * LF_H] POOL;        /* above, below: packed as the screen */
static uint8_t lf_key = 0xFF;
static uint8_t *lf_cv;                                  /* the part being built: a pixel a byte (the light's scratch) */
#ifdef __GNUC__
__attribute__((noinline))
#endif
static void lp(int x, int y, int c)
{
    if (++fg_n >= 2048u) { fg_n = 0; ui_yield(); }      /* (the sound let in, as in building a scene) */
    if (x >= 0 && x < DMK_W && y >= 0 && y < LF_H) lf_cv[y * DMK_W + x] = (uint8_t)c;
}
static void lrect(int x, int y, int w, int h, int c) { int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) lp(x + i, y + j, c); }
static void ldisc(int cx, int cy, int r, int c) { int i, j; for (j = -r; j <= r; j++) for (i = -r; i <= r; i++) if (i * i + j * j <= r * r + r / 2) lp(cx + i, cy + j, c); }
static void lpine(int x, int base, int h, int c, int snow)   /* a pine as the scenes draw them; snow on its boughs */
{
    int y, i;
    for (y = 0; y < h; y++) {
        int half = y / 3 + (y % 6 < 2 ? 0 : 1);
        for (i = -half; i <= half; i++) lp(x + i, base - h + y, snow && y % 6 == 2 && (i == -half || hsh((uint32_t)(x * 31 + y * 7 + i)) % 4u == 0) ? snow : c);
    }
}
static void lcloud(int x, int y, int w, int lt)         /* a high cloud, small: puffs on a flat base, lit pink under */
{
    int k, n = 2 + w / 14;
    for (k = 0; k <= n; k++) { int r = 3 + (int)(hsh((uint32_t)(w * 5 + k)) % 3u) + (k && k < n ? 2 : 0); ldisc(x + k * w / n, y - r + 1, r, C_HOLO6); }
    lrect(x - 2, y - 1, w + 4, 3, C_HOLO6);
    for (k = 0; k <= n; k++) { int r = 3 + (int)(hsh((uint32_t)(w * 5 + k)) % 3u) + (k && k < n ? 2 : 0); ldisc(x + k * w / n, y - r - 1, r, lt); }
    lrect(x - 2, y - 3, w + 4, 2, lt);
}
/* the sky over the whole screen: its colours spread from the top down to the horizon, dithered on the screen's rows */
static int sky_full(int sc, int x, int Y, int dk)
{
    int n = SKY[sc].n, last = SKY[sc].at[n - 1], q = Y * 16 * last / (LF_Y0 + last), i = 0, t, a, b;
    while (i < n - 1 && q >= SKY[sc].at[i + 1] * 16) i++;
    if (i == n - 1) return SKY[sc].c[i] < 0 ? dk : SKY[sc].c[i];
    t = (q - SKY[sc].at[i] * 16) / (SKY[sc].at[i + 1] - SKY[sc].at[i]);
    a = SKY[sc].c[i] < 0 ? dk : SKY[sc].c[i];
    b = SKY[sc].c[i + 1] < 0 ? dk : SKY[sc].c[i + 1];
    return bay(x, Y) < t ? b : a;
}
static int fgp_at(int bx, int by)                       /* the scene's still parts, a pixel (band coordinates) */
{
    uint8_t v = sc_fgp[by * (SC_W / 2) + (bx >> 1)];
    return bx & 1 ? v & 15 : v >> 4;
}
static int cat_y(int x) { return 16 + 18 * 4 * x * (DMK_W - 1 - x) / ((DMK_W - 1) * (DMK_W - 1)); }   /* the fairy lights' wire */
#include "dmk_lands.c"

DMK_SMALL static void lf_top(int sc, int dk, int lt)
{
    int x, y, k;
    if (sc >= 6) { land_top(sc, dk, lt); return; }
    if (sc < 4) {
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) lp(x, y, sky_full(sc, x, y, dk));
        if (sc == 0) lcloud(30, 44, 30, lt);
        if (sc == 1) lcloud(172, 40, 34, lt);
        if (sc == 2) {
            lcloud(70, 42, 28, lt);
            for (k = 0; k < 3; k++) {                   /* gulls, far: a stroke each side */
                int gx = 150 + k * 14 + (k == 1 ? 4 : 0), gy = 22 + (k & 1) * 5;
                lp(gx - 2, gy, dk); lp(gx - 1, gy + 1, dk); lp(gx, gy, dk); lp(gx + 1, gy + 1, dk); lp(gx + 2, gy, dk);
            }
        }
        if (sc == 3)                                    /* the Milky Way: a soft band across, thick with small stars */
            for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
                int d = y - (54 - x * 44 / DMK_W) - tri(x, 53, 4), ad = d < 0 ? -d : d;
                uint32_t h = hsh((uint32_t)(x * 977 + y * 131));
                if (ad < 12 && bay(x, y) < (12 - ad) / 2) lp(x, y, ad < 4 && bay(x, y) < 2 ? C_HOLO1 : C_HOLO0);
                if (ad < 10 && !(h % 23u)) lp(x, y, h & 32u ? lt : C_A_LABEL);
            }
        return;
    }
    if (sc == 4) {                                      /* ROOM: the wall going up, a picture, the fairy lights' wire */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) lp(x, y, bay(x, y) < 2 ? C_HOLO0 : dk);
        lrect(14, 16, 30, 26, C_LABEL);                 /* the picture: a frame, a little mountain at dusk */
        for (y = 18; y < 40; y++) for (x = 16; x < 42; x++) lp(x, y, y < 28 ? (bay(x, y) < (y - 18) ? C_HOLO2 : C_HOLO1) : bay(x, y) < 8 ? C_HOLO3 : C_HOLO2);
        for (x = 16; x < 42; x++) { int top = 36 - tri(x - 16, 26, 10); for (y = top; y < 40; y++) lp(x, y, y < top + 2 && x > 26 && x < 31 ? lt : dk); }
        for (x = 0; x < DMK_W; x++) lp(x, cat_y(x), C_A_INK2);
        return;
    }
    for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) lp(x, y, dk);   /* DESK: the wall, a poster, a shelf */
    lrect(14, 8, 52, 46, C_LABEL);                      /* the poster: a sun going down on the sea, in the light's colours */
    for (y = 10; y < 52; y++) for (x = 16; x < 64; x++) {
        int dx = x - 40, dy = y - 32, d = dx * dx + dy * dy, c;
        if (y > 34) c = (y - 35) % 4 == 0 ? C_HOLO5 : bay(x, y) < 8 ? C_HOLO1 : C_HOLO2;   /* the sea, its lines lit */
        else c = d < 64 ? (bay(x, y) * 4 < 64 - d ? C_HOLO7 : C_HOLO6) : d < 160 ? (bay(x, y) * 6 < 160 - d ? C_HOLO5 : C_HOLO4) : (bay(x, y) < (y - 10) / 2 ? C_HOLO3 : C_HOLO2);
        lp(x, y, c);
    }
    lp(40, 9, lt);                                      /* its pin */
    lrect(126, 46, 108, 2, C_LABEL); lrect(126, 48, 108, 1, C_A_INK2);   /* the shelf */
    lrect(132, 48, 2, 6, C_A_INK2); lrect(226, 48, 2, 6, C_A_INK2);
    for (k = 0; k < 10; k++) {                          /* CD cases, standing */
        static const uint8_t TC[5] = {C_LABEL, C_HOLO2, C_LINE, C_HOLO4, C_A_LABEL};
        int h = 16 + (int)(hsh((uint32_t)k + 17u) % 5u);
        lrect(132 + k * 4, 46 - h, 3, h, TC[(k * 3) % 5]);
        lp(133 + k * 4, 46 - h + 3, lt);
    }
    lrect(178, 26, 16, 20, C_LINE); lrect(192, 26, 2, 20, C_LABEL);   /* a small speaker */
    ldisc(185, 37, 5, dk); ldisc(185, 37, 2, C_LABEL); lp(185, 29, C_HOLO6);
    lrect(208, 38, 14, 8, C_LABEL);                     /* a plant, its pot */
    for (k = 0; k < 5; k++) ldisc(209 + (int)(hsh((uint32_t)k + 3u) % 12u), 30 + (int)(hsh((uint32_t)k + 9u) % 6u), 3 + (int)(hsh((uint32_t)k) % 2u), dk);
    lp(212, 28, C_HOLO1); lp(218, 31, C_HOLO1);
}

DMK_SMALL static void lf_bottom(int sc, int dk, int lt)
{
    int x, y, k;
    if (sc >= 6) { land_bottom(sc, dk, lt); return; }
    if (sc == 0) {                                      /* PEAKS: the snow from under the treeline, its drifts' blue
                                                           shade, near pines */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int edge = y < 7 ? 16 - y * 2 - (int)(hsh((uint32_t)(x * 13 + y)) % 4u) : 0, drift = (y + tri(x + y * 2, 61, 6)) % 13;
            int c = drift < 2 ? (bay(x, y) < 7 ? C_HOLO0 : C_A_LABEL) : (bay(x, y) < 5 ? lt : C_A_LABEL);
            lp(x, y, bay(x, y) < edge ? dk : c);
        }
        lpine(12, 60, 54, dk, lt); lpine(34, 60, 40, dk, lt); lpine(208, 60, 46, dk, lt); lpine(230, 60, 58, dk, lt);
        for (k = 0; k < 4; k++) lp(100 + k * 9, 52 + (k & 1) * 3, C_A_INK2);   /* steps in the snow */
        return;
    }
    if (sc == 1) {                                      /* LAKE: the water nearer, the shore, reeds, a dock */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int bx = clampi(x - LF_X0, 0, SC_W - 1), c = fgp_at(bx, SC_H - 8 + y % 8);
            lp(x, y, bay(x, y) < y * 6 / 34 ? dk : c);
        }
        for (x = 0; x < DMK_W; x++) {                   /* the shore, its reeds */
            int sh = 36 + tri(x, 70, 5);
            for (y = sh; y < LF_H; y++) lp(x, y, bay(x, y) < 2 ? C_HOLO0 : dk);
        }
        for (k = 0; k < 34; k++) {
            uint32_t h = hsh((uint32_t)k * 2654435761u + 5u);
            int rx = (int)(h % 150u), base = 38 + tri(rx, 70, 5), ht = 8 + (int)((h >> 8) % 20u);
            for (y = 0; y < ht; y++) lp(rx + (y > ht * 2 / 3 && (h & 1u) ? 1 : 0), base - y, y > ht - 3 ? C_HOLO1 : dk);
            if (!(h % 3u)) { lrect(rx - 1, base - ht + 2, 3, 5, dk); lp(rx - 1, base - ht + 2, C_HOLO2); }   /* a cattail */
        }
        for (y = 20; y < LF_H; y++) {                   /* the dock, its planks, from the near right out over the water */
            int l = 176 - (y - 20) * 26 / 40, r = 196 + (y - 20) * 34 / 40;
            for (x = l; x <= r; x++) lp(x, y, (y - 20) % 5 == 0 ? dk : x == l ? C_LINE : C_LABEL);
        }
        lrect(178, 14, 2, 8, dk); lrect(193, 14, 2, 8, dk);   /* its posts at the end, in the water */
        return;
    }
    if (sc == 2) {                                      /* the BEACH nearer: the dry sand on, its ripples, steps, a board */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int c = bay(x + y / 4, y) < 7 ? C_LINE : C_A_LABEL;
            if ((y * 2 + tri(x, 41, 3)) % 13 == 0 && bay(x, y) < 6) c = C_LABEL;   /* the wind's ripples */
            if (!(hsh((uint32_t)(x * 7 + y * 131)) % 71u)) c = dk;              /* pebbles */
            lp(x, y, c);
        }
        for (k = 0; k < 9; k++) {                       /* steps along it, going away */
            int fx = 150 - k * 9 + (k & 1) * 5, fy = LF_H - 4 - k * 5;
            lp(fx, fy, C_LABEL); lp(fx + 1, fy, C_LABEL); lp(fx, fy - 1, C_LABEL);
        }
        for (y = 22; y < 44; y++) for (x = -3; x <= 3; x++)   /* a surfboard stood in the sand */
            if (x * x * 22 < (y - 22) * (44 - y) * 4) lp(204 + x, y, x == -3 + (y > 33) ? C_HOLO5 : x == 0 ? C_HOLO2 : C_HOLO4);
        return;
    }
    if (sc == 3) {                                      /* NIGHT: a meadow, its grass */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) lp(x, y, dk);
        for (k = 0; k < 150; k++) {
            uint32_t h = hsh((uint32_t)k * 40503u + 11u);
            int gx = (int)(h % (uint32_t)DMK_W), ht = 4 + (int)((h >> 9) % 22u), lean = (int)((h >> 3) % 3u) - 1;
            for (y = 0; y < ht; y++) lp(gx + lean * y / 8, LF_H - 1 - y, y > ht - 3 && (h & 4u) ? C_HOLO1 : C_HOLO0);
        }
        return;
    }
    if (sc == 4) {                                      /* ROOM: the floor, the bed's foot, the nightstand's drawer, a rug */
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {
            int dx = x - (211 + LF_X0), dy = y + SC_H - 80 + 40, d = dx * dx + dy * dy, c;   /* the lamp's light on the boards */
            c = y % 7 == 0 ? C_HOLO0 : bay(x, y) < 1 ? C_HOLO0 : dk;
            if (d < 3000 && bay(x, y) * 300 < 3000 - d) c = d < 1200 ? C_HOLO3 : C_HOLO2;
            lp(x, y, c);
        }
        for (y = 0; y < 15; y++) for (x = 0; x < 150 + LF_X0; x++) {   /* the quilt, going on to its hem */
            int bx = x - LF_X0, by = SC_H + y;
            lp(x, y, ((bx / 8 + by / 6) & 1) ? (bay(x, y) < 6 ? C_HOLO0 : dk) : (bay(x, y) < 3 ? C_A_INK2 : dk));
        }
        lrect(0, 15, 150 + LF_X0, 2, C_LABEL);
        for (y = 17; y < 24; y++) for (x = 0; x < 150 + LF_X0; x++) lp(x, y, bay(x, y) < 4 ? C_A_INK2 : dk);   /* its frame */
        lrect(10, 24, 3, 9, C_A_INK2); lrect(150 + LF_X0 - 6, 24, 3, 9, C_A_INK2);
        lrect(196 + LF_X0, 0, 30, 30, dk);              /* the nightstand: its drawer, a knob, its legs */
        lrect(198 + LF_X0, 4, 26, 12, C_LABEL); lrect(199 + LF_X0, 5, 24, 10, dk); lp(211 + LF_X0, 10, lt);
        lrect(197 + LF_X0, 30, 2, 6, dk); lrect(223 + LF_X0, 30, 2, 6, dk);
        for (y = 38; y < LF_H; y++) for (x = 30; x < 200; x++) {   /* the rug, its rings */
            int dx = (x - 115) * 9 / 85, dy = y - 49, d = dx * dx + dy * dy;
            if (d < 81) lp(x, y, d < 25 ? (bay(x, y) < 6 ? C_HOLO2 : C_A_INK2) : d < 49 ? C_A_INK2 : (bay(x, y) < 8 ? C_HOLO1 : C_A_INK2));
        }
        return;
    }
    for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x++) {   /* DESK: its top going on, its front edge, under it */
        int by = SC_H + y;
        lp(x, y, y < 20 ? ((by - 90) % 7 == 0 ? C_HOLO0 : bay(x, y) < 2 ? C_HOLO0 : dk) : dk);
    }
    lrect(122, 2, 36, 13, C_A_INK2);                    /* a mouse on its pad, its cable */
    ldisc(140, 8, 4, C_LINE); lrect(136, 8, 9, 5, C_LINE); lrect(140, 4, 1, 4, C_LABEL);
    for (y = 0; y < 4; y++) lp(140 + (y & 1), y, C_LABEL);
    for (k = 0; k < 2; k++) {                           /* floppy disks: the shutter, the label */
        int fx = 168 + k * 15, fy = 3 + k * 2;
        lrect(fx, fy, 12, 12, k ? C_LABEL : C_HOLO4); lrect(fx + 3, fy, 6, 4, C_LINE); lrect(fx + 2, fy + 8, 8, 3, lt);
    }
    for (k = 0; k < 30; k++) lp(56 + k, 13 - k / 8, k < 3 ? dk : k < 26 ? C_HOLO6 : C_LINE);   /* a pencil */
    lrect(0, 20, DMK_W, 1, C_LINE); lrect(0, 21, DMK_W, 2, C_LABEL);   /* the desk's front edge */
    lrect(164, 26, 34, 35, C_LINE); lrect(194, 26, 4, 35, C_LABEL);    /* the tower under it */
    lrect(168, 30, 24, 2, dk); lrect(168, 36, 16, 1, dk); ldisc(180, 48, 2, lt); lp(188, 52, C_HOLO6);
    for (y = 23; y < 26; y++) lp(176 + y - 23, y, C_A_INK2);   /* its cable, up to the desk */
}

static void lf_build(int sc, int dk, int lt)
{
    int part, x, y;
    lf_cv = sc_fg;
    for (part = 0; part < 2; part++) {
        if (part) lf_bottom(sc, dk, lt); else lf_top(sc, dk, lt);
        for (y = 0; y < LF_H; y++) for (x = 0; x < DMK_W; x += 2)
            lf_ext[part][y * (DMK_W / 2) + x / 2] = (uint8_t)(lf_cv[y * DMK_W + x] << 4 | lf_cv[y * DMK_W + x + 1]);
        ui_yield();
    }
}

static uint32_t lf_tick = 0xFFFFFFFFu;                  /* the frame of the scene the screen holds (full screen) */
static void land_full(int sc, uint32_t t, int hold)
{
    int dark = THEME_DARK[theme], dk = dark ? C_PAPER : C_INK, lt = dark ? C_INK : C_PAPER, x, y, k, out;
    uint32_t tick = t / 140u;
    sc = sc < DMK_SCENES ? sc : 0;
    out = OUTDOOR[sc];
    /* held: nothing moves between the scene's frames (140 ms), so the screen is left as it is: no drawing, and nothing
     * to send (with every effect on, drawing each frame left the sound short: it clicked) */
    if (hold && tick == lf_tick && lf_key == (uint8_t)(theme * 16 + sc)) return;
    lf_tick = tick;
    t = tick * 140u;                                    /* what moves, moves on the scene's frames */
    scene_frame(sc, tick);
    if (lf_key != (uint8_t)(theme * 16 + sc)) { lf_build(sc, dk, lt); lf_key = (uint8_t)(theme * 16 + sc); }
    for (y = 0; y < LF_H; y++)                          /* above and below: as built */
        for (x = 0; x < DMK_W / 2; x++) {
            fb[y * (DMK_W / 2) + x] = lf_ext[0][y * (DMK_W / 2) + x];
            fb[(LF_Y0 + SC_H + y) * (DMK_W / 2) + x] = lf_ext[1][y * (DMK_W / 2) + x];
        }
    ui_yield();
    for (y = 0; y < SC_H; y++) {                        /* the scene, its sky (outdoors) spread as the sky above it */
        int Y = LF_Y0 + y, org[4], str[4], ph, j;
        uint8_t ob[2] = {0, 0}, sb[2] = {0, 0};
        uint8_t *b = &fb[Y * (DMK_W / 2)];
        const uint8_t *src = &sc_out[y * (SC_W / 2)], *msk = &sc_fgm[y * (SC_W / 2)];
        if ((y & 15) == 15) ui_yield();                 /* the sound, often (its ring is 17 ms) */
        if (out) {
            for (ph = 0; ph < 4; ph++) { org[ph] = sky_at(sc, ph, y, dk); str[ph] = sky_full(sc, ph, Y, dk); }
            ob[0] = (uint8_t)(org[0] << 4 | org[1]); ob[1] = (uint8_t)(org[2] << 4 | org[3]);   /* a band byte's sky, plain */
            sb[0] = (uint8_t)(str[0] << 4 | str[1]); sb[1] = (uint8_t)(str[2] << 4 | str[3]);   /* a screen byte's, spread */
        }
        for (j = 0; j < DMK_W / 2; j++) {
            int i = j - LF_X0 / 2, c2[2];
            if (i >= 0 && i < SC_W / 2) {               /* in the band: a byte at once when it is all plain sky, or ground */
                if (!out) { b[j] = src[i]; continue; }
                if (!msk[i] && src[i] == ob[i & 1]) { b[j] = sb[j & 1]; continue; }
                if (msk[i] == 0xFF) { b[j] = src[i]; continue; }
            }
            for (k = 0; k < 2; k++) {
                int px = j * 2 + k, sx = px < LF_X0 ? LF_X0 - 1 - px : px - LF_X0 >= SC_W ? 2 * SC_W - 1 - (px - LF_X0) : px - LF_X0;   /* (past
                                                           the band's sides: it mirrored, not its last pixel drawn out) */
                int c = sx & 1 ? src[sx >> 1] & 15 : src[sx >> 1] >> 4, m = sx & 1 ? msk[sx >> 1] & 15 : msk[sx >> 1] >> 4;
                if (out && !m && c == org[sx & 3]) c = str[px & 3];   /* plain sky (not a cloud, a star): the spread one */
                c2[k] = c;
            }
            b[j] = (uint8_t)(c2[0] << 4 | c2[1]);
        }
    }
    ui_yield();
    if (out && sc != 6)                                 /* what moves: stars twinkling where the sky above is dark */
        for (k = 0; k < (sc == 3 ? 90 : sc == 11 ? 70 : sc >= 8 && sc <= 10 ? 0 : 40); k++) {
            uint32_t h = hsh((uint32_t)k * 7919u + (uint32_t)sc * 31u);
            int sx = (int)(h % (uint32_t)DMK_W), sy = (int)((h >> 12) % (uint32_t)LF_H);
            if (sky_full(sc, sx, sy, dk) == dk && hsh(h + t / 420u) & 3u) px_put(sx, sy, k % 5 ? lt : C_A_LABEL);
        }
    /* (the BEACH's waves: on its band, land_anim; nearer, dry sand) */
    if (sc == 3)                                        /* fireflies over the meadow */
        for (k = 0; k < 14; k++) {
            uint32_t h = hsh((uint32_t)k * 2654435761u + 77u);
            int fx = (int)(h % (uint32_t)DMK_W) + sinp(t * 90000u + h) * 6 / 32768, fy = LF_Y0 + SC_H + 6 + (int)((h >> 10) % 48u) + sinp(t * 61000u + (h << 3)) * 3 / 32768;
            if (hsh(h + t / 300u) % 3u) { px_put(fx, fy, C_HOLO6); px_put(fx + 1, fy, C_HOLO5); }
        }
    if (sc >= 6) land_full_anim(sc, t, dk, lt);
    if (sc == 4)                                        /* the fairy lights, twinkling */
        for (k = 0; k < 20; k++) {
            int bx = 6 + k * 12, by = cat_y(bx) + 1, on = (int)(hsh((uint32_t)k + t / 500u) % 4u);
            int c = on == 0 ? C_HOLO3 : on == 1 ? C_HOLO5 : C_HOLO6;
            px_put(bx, by, c); px_put(bx + 1, by, c); px_put(bx, by + 1, c); px_put(bx + 1, by + 1, on > 1 ? C_HOLO7 : c);
        }
}
static void boot_land(uint32_t t) { land_full(boot_sc, t, 0); }   /* (the boot draws over it: never held) */

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
#ifdef DMK_VER
    if (t >= BOOT_ARRIVE) text_c(DMK_W / 2, 204, DMK_VER, &FS, C_LABEL);   /* the version (the FM-1's build sets it) */
#endif
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

#ifndef DMK_FM1_DIAG
#define DMK_FM1_DIAG 0
#endif
/* the test build's load case (docs/sound-design.md, stage 1): HOME and SAVE held, then REC. Every synth noise, a
 * ping-pong and a flanger, the haze, the grains and VHS on the mix, twelve notes struck again every two seconds; the
 * same keys put it all back */
static struct { uint8_t on, kept[DMK_SYNTHS][3], gkept[3]; uint32_t at; } stress;
static uint8_t stress_row(int r) { return r == 1 ? choice_val(2, 3) : choice_val(r == 0 ? FC_NOISE : FO_FLANGER, r == 0 ? FX_COLOURS : FX_MOTIONS); }   /* the rows' heaviest [M] */
static void stress_notes(int on)
{
    int i;
    for (i = 0; i < DMK_VOICES; i++) {
        if (on) note_on_s(i % DMK_SYNTHS, 48 + i * 3); else note_off_s(i % DMK_SYNTHS, 48 + i * 3);
    }
}
static void stress_set(int on)
{
    int s, r;
    if (on == stress.on) return;
    for (s = 0; s < DMK_SYNTHS; s++)
        for (r = 0; r < 3; r++) {
            if (on) { stress.kept[s][r] = ui.val[s][PG_FX][r][4]; ui.val[s][PG_FX][r][4] = stress_row(r); }
            else ui.val[s][PG_FX][r][4] = stress.kept[s][r];
        }
    for (r = 0; r < 3; r++) {
        const uint8_t HEAVY[3] = {choice_val(VB_HEAVY + 1, FX_VERBS), choice_val(2, FX_ECHOS), choice_val(FM_VHS, FX_MEDIAS)};   /* the heaviest verb, grains, VHS */
        if (on) { stress.gkept[r] = ui.val[0][PG_GLOBAL][r][4]; ui.val[0][PG_GLOBAL][r][4] = HEAVY[r]; }
        else ui.val[0][PG_GLOBAL][r][4] = stress.gkept[r];
    }
    sp_sync();
    stress.on = (uint8_t)on;
    stress_notes(on);
    stress.at = ui.now;
    notice(on ? "Load test on" : "Load test off");
}
static void stress_tick(void)
{
    if (stress.on && ui.now - stress.at >= 2000u) { stress_notes(0); stress_notes(1); stress.at = ui.now; }
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
    if (ui.ren_slot < ST_SONGS && ui.now - ui.ren_at > 900u && !seq_on) ren_flush();   /* a name turned: written as it rests */
    if (DMK_FM1_DIAG) stress_tick();
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
        if (!ui.both_on) { ui.both_on = 1; ui.both_at = ms; }   /* from when they both are, whatever took the second */
        if (!ui.full_latch && ms - ui.both_at > 500u) {
            ui.full = (uint8_t)!ui.full; ui.full_latch = 1;
            if (ui.full) notice("OCT\xE2\x88\x92 OCT+ held: back");
        }
    } else ui.full_latch = ui.both_on = 0;
    if (ui.full) {                                      /* the landscape full screen, the current synth's, moving */
        int shown = ui.note_ms && ms - ui.note_ms < 1700u;   /* a notice over it: drawn again each frame, and once after */
        land_full(scene_of(ui.synth), ms, !shown && !ui.full_note);
        ui.full_note = (uint8_t)shown;
        ta_cover(0, 0, DMK_W, DMK_H);
        overlays();
        return fb;
    }
    lf_tick = 0xFFFFFFFFu;                              /* back to a page: the next full screen drawn whole */
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
    default: stage_save(); break;
    }
    ui_yield();
    foot();
    if (!(ui.btn_held >> B_HOME & 1u && ui.now - ui.btn_down_at[B_HOME] > 350u)) page_dots();   /* (not over the scenes) */
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
/* how the synth picked follows the chords, a step: round; not in harmony with itself, nor with a synth that follows
 * another or plays the chords: it has no notes of its own to move (a chain was silent). Free: its roll as written
 * (the owner: a chord recorded kept, the progression not moving it) */
static void follow_turn(int dir)
{
    int f = ch.follow[ui.synth], i;
    for (i = 0; i < FW_COUNT; i++) {
        f = wrap(f + dir, FW_COUNT);
        if (!(f >= FW_HARM1 && f <= FW_HARM3 && (f - FW_HARM1 == ui.synth || harm_of(f - FW_HARM1) >= 0 || ch.follow[f - FW_HARM1] == FW_PLAYS))) break;
    }
    if (ch.follow[ui.synth] == FW_PLAYS) chord_off(ui.synth);
    ch.follow[ui.synth] = (uint8_t)f;
    chord_join(ui.synth);                               /* set to play them: the chord sounding now */
}
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
            int p = dir < 0 && snap(n->note) != n->note ? snap(n->note) : note_of(degree_of(n->note) + dir);   /* (off the
                                                           scale: to its neighbours on it) */
            if (p < 24 || p > 108 || seq_find(s, n->step, p) >= 0) return;
            note_off_s(s, n->note);
            n->note = (uint8_t)p; ui.key_note[h] = (uint8_t)p;
            note_on_s(s, p);
        }
        if (k == 2) {                                   /* its length, and the next written: as long (the pen) */
            int l = n->len + dir;
            n->len = (uint8_t)(l < 1 ? 1 : l > SEQ_LEN ? SEQ_LEN : l);
            ui.roll_pen[s] = n->len;
        }
        if (k == 3) {                                   /* NUDGE: a 32nd of a step later or earlier, on into the next */
            int t = (n->step * 32 + n->off + dir + st * 32) % (st * 32), ns = t / 32;
            if (ns != n->step && seq_find(s, ns, n->note) >= 0) return;
            n->step = (uint8_t)ns; n->off = (uint8_t)(t % 32); ui.key_step[h] = (uint8_t)ns; ui.roll_cur[s] = (uint8_t)ns;
        }
        if (k == 4) { int v = n->vel + dir * 4; n->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v); }
        return;
    }
    if (k == 0) ui.roll_cur[s] = (uint8_t)((ui.roll_cur[s] + dir + st) % st);
    if (k == 1) { int v = ui.roll_view[s] + dir; ui.roll_view[s] = (uint8_t)(v < 14 ? 14 : v > 43 ? 43 : v); }
    if (k == 2) follow_turn(dir);                       /* how it follows the chords: Free, its roll as written */
    if (k == 3) {                                       /* this synth's length: the three loop against each other; a
                                                           click a step to 16, a beat to 64, then a bar to 256 */
        int g = dir > 0 ? (st < 16 ? 1 : st < 64 ? 4 : 16) : (st <= 16 ? 1 : st <= 64 ? 4 : 16);
        int t = dir > 0 ? (st / g + 1) * g : (st - 1) / g * g;
        t = t < 1 ? 1 : t > SEQ_STEPS ? SEQ_STEPS : t;
        sq[s].steps = (uint16_t)t;
        if (ui.roll_cur[s] >= t) ui.roll_cur[s] = (uint8_t)(t - 1);
        if (sq[s].pos >= t) sq[s].pos = 0;
    }
    if (k == 4) ui.roll_zoom[s] = (uint8_t)wrap(ui.roll_zoom[s] + dir, 3);   /* (16, 32, 64: round) */
}

static void roll_oct(int dup)                           /* OCT- deletes the held notes; OCT+ copies each after itself */
{
    int s = ui.synth, k;
    for (k = 0; k < DMK_KEYS; k++) {
        int j;
        if (!ui.keys_down[k] || ui.key_roll[k] != 1 || ui.key_sy[k] != s) continue;
        j = seq_find(s, ui.key_step[k], ui.key_note[k]);
        if (j < 0) continue;
        if (!dup) { seq_del(s, j); ui.key_roll[k] = 3; continue; }
        {
            snote_t n = sq[s].n[j];
            int t = (n.step + n.len) % sq[s].steps;
            int c = seq_add(s, t, n.len, n.note, n.vel);
            if (c >= 0) { sq[s].n[c].off = n.off; ui.key_step[k] = (uint8_t)t; ui.roll_cur[s] = (uint8_t)t; }
        }
    }
}

/* ---- the controls */
static void play_toggle(void)                           /* PLAY: started or stopped */
{
    if (!seq_on && ui.page == PG_ARR) {                 /* Arrange: the song from the cursor; REC armed, recorded */
        if (seq_rec) {
            song_rec = 1; rec_from = (uint8_t)(ui.arr_cur < song_n ? ui.arr_cur : song_n); rec_bars = 0;
            notice("Recording the song");
        } else {
            if (!song_n) { notice("The song is empty"); return; }
            song_mode = 1;
            song_start(ui.arr_cur);
        }
    } else if (!seq_on && song_mode && song_n) song_start(0);   /* song mode: from the first step */
    if (!seq_on && clock_ext && !ext_running()) notice("No clock in: its own tempo");
    ui.rec_ready = 0;
    seq_play(!seq_on);
}

DMK_SMALL static void ui_button(int b, int down)
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
        if (DMK_FM1_DIAG && b == B_REC && (ui.btn_held & (1u << B_HOME | 1u << B_SAVE)) == (1u << B_HOME | 1u << B_SAVE)) {
            stress_set(!stress.on);                     /* the test build's load case */
            ui.btn_used |= (uint16_t)(bit | 1u << B_HOME | 1u << B_SAVE);
            return;
        }
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
            ui.btn_used |= (uint16_t)(1u << B_OCTD | 1u << B_OCTU);
            ui.both_at = ui.now;
            return;
        }
        if (b != B_HOME && ui.btn_held >> B_HOME & 1u && (b == B_SEQ || b == B_PLAY || b == B_OCTU)) {
            /* HOME held: SEQ opens Arrange; PLAY switches pattern mode and song mode; OCT+, then a pattern key, copies */
            ui.btn_used |= (uint16_t)(bit | 1u << B_HOME);
            if (b == B_SEQ) { ui.page = PG_ARR; return; }
            if (b == B_PLAY) {
                song_mode ^= 1u;
                if (!song_mode) { mix_g = 32768; mix_dg = mix_next = 0; }   /* left while it faded: whole again */
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
            if (ui.clear_arm == 4 && ui.now - ui.clear_at < 2000u) { notice(roll_clear(ui.synth) ? "Pattern cleared, chords off" : "Pattern cleared"); ui.clear_arm = 0; }
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
        if (b == B_OCTD && ui.page == PG_DISCS && ui.row[PG_DISCS]) {   /* Sounds, OCT- twice: the track's sound INIT */
            char t[24];
            ui.btn_used |= bit;                         /* (not the octave as well) */
            if (ui.clear_arm == 6 && ui.now - ui.clear_at < 2000u) {
                sound_init(ui.synth);
                ui.preset[ui.synth] = ui.val[ui.synth][PG_SOUND][0][0];
                fmt_int(str_cat(t, "Track "), ui.synth + 1); str_cat(t, ": init");
                ui.clear_arm = 0;
            } else { ui.clear_arm = 6; ui.clear_at = ui.now; str_cat(t, "OCT\xE2\x88\x92 again: init"); }
            notice(t);
            return;
        }
        if (b == B_OCTD && ui.page == PG_DISCS) {       /* Projects, OCT- twice: the project picked deleted */
            ui.btn_used |= bit;
            if (!song_used(ui.song_pick)) { notice("Empty already"); return; }
            if (ui.clear_arm == 3 && ui.now - ui.clear_at < 2000u) {
                if (ui.ren_slot == ui.song_pick) ui.ren_slot = 0xFF;   /* (its name being turned: gone with it) */
                notice(song_erase(ui.song_pick) ? "Not deleted" : "Deleted");
                ui.clear_arm = 0;
            } else { ui.clear_arm = 3; ui.clear_at = ui.now; notice("OCT\xE2\x88\x92 again: delete it"); }
            return;
        }
        if (b == B_OCTD && (ui.btn_held >> B_SEQ & 1u || ui.btn_held >> B_HOME & 1u)) {
            /* SEQ held, OCT- twice: this synth's roll cleared; HOME held (the patterns), all three. Twice, as
             * nothing is saved yet to bring them back */
            int all = (int)(ui.btn_held >> B_HOME & 1u), what = 1 + all, s;
            ui.btn_used |= (uint16_t)(bit | 1u << (all ? B_HOME : B_SEQ));
            if (ui.clear_arm == what && ui.now - ui.clear_at < 2000u) {
                int off = 0;
                for (s = 0; s < DMK_SYNTHS; s++) if (all || s == ui.synth) off |= roll_clear(s);
                notice(all ? (off ? "Rolls cleared, chords off" : "Rolls cleared") : (off ? "Roll cleared, chords off" : "Roll cleared"));
                ui.clear_arm = 0;
            } else {
                ui.clear_arm = (uint8_t)what;
                ui.clear_at = ui.now;
                notice(all ? "OCT\xE2\x88\x92 again: all three" : "OCT\xE2\x88\x92 again: clear");
            }
            return;
        }
        return;                                         /* OCT- and OCT+ move the octave as they are let go (below) */
    }
    ui.btn_held &= (uint16_t)~bit;
    if (b == B_HOME) ui.copy_arm = 0;                   /* the copy waits only while HOME is held */
    if (ui.btn_used & bit) return;                      /* a modifier that was used does nothing on release */
    if (b == B_OCTD || b == B_OCTU) {                   /* the octave, on release: the two pressed together never move it */
        char t[16];
        if (b == B_OCTD && ui.octave > -2) ui.octave--;
        if (b == B_OCTU && ui.octave < 2) ui.octave++;
        fmt_int(str_cat(t, "Octave "), ui.octave);
        notice(t);
        return;
    }
    if (ui.now - ui.btn_down_at[b] > 1000u && b == B_SAVE) { save_open(); return; }   /* (held: the same) */
    if (ui.now - ui.btn_down_at[b] > 350u) return;      /* a hold showed its layer: no tap */
    if (b == B_SAVE && ui.page != PG_DISCS) {           /* SAVE: its pages (the owner), Projects first */
        if (!store_on()) { notice("No store here"); return; }
        save_open();
        return;
    }
    if (b == B_SAVE && ui.row[PG_DISCS]) {              /* Sounds: the track's sound into the library */
        char t[24];
        int n;
        if (!store_on()) { notice("No store here"); return; }
        if (seq_on) { notice("Stop to save"); return; }
        n = sound_save(ui.synth);
        if (n <= 0) { notice(n ? "Not saved" : "The library is full"); return; }
        fmt_int(str_cat(t, n < 10 ? "Kept as User 0" : "Kept as User "), n);
        notice(t);
        ui.preset[ui.synth] = (uint8_t)(DMK_SAMPLES_N + lib_count() - 1);
        return;
    }
    if (b == B_SAVE) {                                  /* Projects: the work into an empty slot, a new name made up for
                                                           it, or over the project open; not over another (the owner) */
        int at = ui.song_pick;
        char m[32];
        if (!store_on()) { notice("No store here"); return; }
        if (seq_on) { notice("Stop to save"); return; }
        ren_flush();
        if (song_used(at) && at != ui.song) { notice("Pick an empty slot"); return; }
        if (!song_used(at)) nm_roll(ui.val[0][PG_DISCS][1], ++ui.nm_n * 2654435761u ^ ui.now ^ (uint32_t)at * 977u);
        if (song_save(at)) { notice("Not saved"); return; }
        ui.song = (uint8_t)at;
        str_cat(str_cat(m, "Saved: "), song_name(at));
        notice(m);
        return;
    }
    if (b == B_PLAY && ui.page == PG_DISCS && !ui.row[PG_DISCS] && !seq_on) {   /* Projects, stopped, PLAY twice: the
                                                           one picked opened (an empty slot: a new, empty project), the
                                                           work open left; said first when it has changes not saved.
                                                           (Playing, PLAY stops, as anywhere: the owner lost a song) */
        int at = ui.song_pick, s;
        ren_flush();
        if (!(ui.clear_arm == 5 && ui.copy_to == at && ui.now - ui.clear_at < 2000u)) {
            ui.clear_arm = 5; ui.copy_to = (uint8_t)at; ui.clear_at = ui.now;
            notice(store_on() && song_changed() ? "Unsaved: PLAY again to lose it" : song_used(at) ? "PLAY again: open it" : "PLAY again: new project");
            return;
        }
        ui.clear_arm = 0;
        if (song_used(at)) { if (song_load(at)) { notice("Not opened"); return; } notice("Opened"); }
        else { project_empty(); notice("New project"); }
        ui.song = (uint8_t)at;
        for (s = 0; s < DMK_SYNTHS; s++) ui.preset[s] = ui.val[s][PG_SOUND][0][0];
        ui.page = PG_HOME;
        return;
    }
    if (b == B_PLAY) {
        if (!seq_on && seq_rec && ui.page != PG_ARR && !clock_ext && !ui.rec_ready) {   /* REC lit: ready, the first
                                                           key starts it in time (the owner: as a pocket studio does);
                                                           PLAY again starts it at once */
            ui.rec_ready = 1;
            notice("Ready: a key starts");
            return;
        }
        play_toggle();
        return;
    }
    if (b == B_REC) {
        seq_rec ^= 1u; ui.rec_ready = 0;
        notice(seq_rec ? "Recording on" : "Recording off");
        return;
    }
    if (BTN_PAGE[b] >= 0 && ui.full) { ui.full = 0; return; }   /* the landscape: a page button, back to the page */
    if (BTN_PAGE[b] >= 0) {
        int p = BTN_PAGE[b];
        if (ui.page == p) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);   /* again: the next row built */
        else {
            ui.page = (uint8_t)p;
            if (row_soon(p, ui.row[p])) ui.row[p] = (uint8_t)row_next(p, ui.row[p]);
            if (rows_built(p) > 1 && !(ui.hinted >> p & 1u)) {   /* the first time: how to reach the next row */
                char t[32];
                if (sel_scrolls(p)) str_cat(t, "SELECT: the rows");
                else str_cat(str_cat(t, BTN_NAME[b]), " again: the next row");
                notice(t);
                ui.hinted |= (uint16_t)(1u << p);
            }
        }
        ui.touch_k = -1;
    }
}

DMK_SMALL static void ui_encoder(int e, int d)
{
    int r = ui.row[ui.page], k;
    uint8_t *v;
    ui.boot_done = 1;
    ui.full = 0;                                        /* the landscape: a knob turned, back to the page it turns */
    if (ui.btn_held) {                                  /* a held button's layer: it was used */
        int b;
        for (b = 0; b < B_COUNT; b++) if (ui.btn_held >> b & 1u) ui.btn_used |= (uint16_t)(1u << b);
    }
    if (e == E_SELECT) {                                /* a step a click: a second the same way within 120 ms is the
                                                           same click (the owner's FM-1 sent two: 1, then 3) */
        int dir = d > 0 ? 1 : -1, s = ui.synth + dir;
        if (dir == ui.sel_dir && ui.now - ui.sel_at < 120u) return;
        ui.sel_dir = (int8_t)dir; ui.sel_at = ui.now;
        if (ui.page == PG_DISCS && !ui.row[PG_DISCS] && !(ui.btn_held >> B_HOME & 1u)) { ui.touch_k = -1; save_pick(ui.song_pick + dir); return; }   /* the projects */
        if (!(ui.btn_held >> B_HOME & 1u) && sel_scrolls(ui.page)) {   /* it scrolls (the owner's feedback: the most
                                                           intuitive): a page's rows, the songs' slots */
            ui.touch_k = -1;
            {                                           /* round, past the rows not built */
                int n = r, i;
                for (i = 0; i < PAGE[ui.page].rows; i++) { n = wrap(n + dir, PAGE[ui.page].rows); if (!row_soon(ui.page, n)) break; }
                ui.row[ui.page] = (uint8_t)n;
            }
            return;
        }
        ui.synth = (uint8_t)wrap(s, DMK_SYNTHS);        /* the synth (HOME held: on any page) */
        sp_sync();
        return;
    }
    if (e == E_PRESET && (ui.page == PG_FX || (ui.page == PG_GLOBAL && ui.row[PG_GLOBAL] < 3) || ui.page == PG_FILTER)) {
        /* on a page of types, PRESET is the row's type: the effect, the delay, the verb, the echo, the media, the
         * filter (the owner: not the whole sound there) */
        int r0 = ui.row[ui.page], n;
        const choice_t *c = choice_of(ui.page, r0, 4);
        uint8_t *v = vp(ui.page, r0, 4);
        n = wrap(choice_index(c, *v) + (d > 0 ? 1 : -1), c->n);
        *v = choice_val(n, c->n);
        ui.touch_k = 4; ui.touch_ms = ui.now;
        sp_sync();
        return;
    }
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
    if (k == 4 && ui.btn_held >> B_SEQ & 1u) {          /* SEQ held, ALGORITHM: how this synth follows the chords (the
                                                           roll's K3 too) */
        char t[32];
        follow_turn(d > 0 ? 1 : -1);
        str_cat(t, FOLLOWS[ch.follow[ui.synth]]);
        notice(t);
        return;
    }
    if (ui.page == PG_ROLL && (!ui.row[PG_ROLL] || (k == 4 && ui.row[PG_ROLL] != 2) || roll_held() >= 0)) { roll_turn(k, d); return; }   /* (Vary, Arp: their values; the Arp's ALGO HOLD) */
    if (ui.page == PG_ARR) {
        /* ARRANGE: ALGORITHM the rows. The Scene row: K1-K3 each synth's pattern, K4 the progression. The Song row: K1
         * the step, K2 its scene, K3 its bars, K4 the end */
        int dir = d > 0 ? 1 : -1, c;
        sstep_t *t = ui.arr_cur < song_n ? &song[ui.arr_cur] : 0;
        if (k == 4) { ui.row[PG_ARR] = (uint8_t)!ui.row[PG_ARR]; return; }
        if (!ui.row[PG_ARR]) {
            if (k < DMK_SYNTHS) {
                c = wrap((spat_next[k] < SYN_PATS ? spat_next[k] : syn_pat(k)) + dir, SYN_PATS);
                pat_pick_syn(k, c);
            } else prog_step(dir);
            return;
        }
        if (k == 0) ui.arr_cur = (uint8_t)wrap(ui.arr_cur + dir, song_n + 1);   /* (its last place: the new step) */
        else if (k == 3) song_end = (uint8_t)((song_end + SE_COUNT + dir) % SE_COUNT);
        else if (k == 2) {                              /* a step's bars; past the last, the next one's */
            c = (t ? t->bars : ui.arr_bars) + dir;
            c = c < 1 ? 1 : c > 8 ? 8 : c;
            if (t) t->bars = (uint8_t)c;
            ui.arr_bars = (uint8_t)c;
        } else if (t && k == 1) t->scene = (uint8_t)((t->scene + SCENES + dir) % SCENES);
        return;
    }
    if (ui.page == PG_DISCS) {                          /* SAVE: ALGO the page; Projects K1 the project, K2 K3 its
                                                           name; Sounds K1 the track, K2 a recording, K3 a kept sound */
        int dir = d > 0 ? 1 : -1, s = ui.synth;
        if (k == 4) { ren_flush(); ui.row[PG_DISCS] = (uint8_t)(!ui.row[PG_DISCS]); return; }
        if (!ui.row[PG_DISCS]) {
            int at = ui.song_pick;
            if (k == 0) save_pick(at + dir);
            if (k == 1 || k == 2) {                     /* a project's name, a word a knob */
                if (!song_used(at)) { notice("Saved, it has a name"); return; }
                if (seq_on) { notice("Stop to rename"); return; }
                if (ui.ren_slot != at) {
                    ren_flush();
                    ui.ren_v[0] = song_nmv[at][0]; ui.ren_v[1] = song_nmv[at][1];
                    if (song_nmv[at][2] != NM_MARK) ui.ren_v[0] = ui.ren_v[1] = 0;   /* (one of before: a name now) */
                    ui.ren_slot = (uint8_t)at;
                }
                ui.ren_v[k - 1] = (uint8_t)wrap(ui.ren_v[k - 1] + dir, 24);
                ui.ren_v[2] = NM_MARK;
                ui.ren_at = ui.now;
            }
            return;
        }
        if (k == 0) { ui.synth = (uint8_t)wrap(s + dir, DMK_SYNTHS); sp_sync(); }
        if (k == 1) {                                   /* a recording for the track, as PRESETS does */
            uint8_t *v = &ui.val[s][PG_SOUND][0][0];
            *v = (uint8_t)wrap(*v + dir, DMK_SAMPLES_N);
            ui.preset[s] = *v;
            sp_sync();
        }
        if (k == 2) {                                   /* a sound kept in the library, whole */
            int n = lib_count(), at = ui.preset[s] >= DMK_SAMPLES_N ? ui.preset[s] - DMK_SAMPLES_N : dir > 0 ? -1 : n;
            if (!n) { notice("None kept yet"); return; }
            at = wrap(at + dir, n);
            lib_load(s, lib_nth(at));
            ui.preset[s] = (uint8_t)(DMK_SAMPLES_N + at);
        }
        return;
    }
    if (ui.page == PG_CHORD) {
        int h, i;
        for (h = 0; h < DMK_KEYS && !(ui.keys_down[h] && ui.key_slot[h]); h++) {}
        if (h < DMK_KEYS && k < 4) {                    /* a written chord held: K1 its slot, K2 quality, K3 length, K4 bass */
            prog_t *p = edit_prog();
            int at = ui.key_slot[h] - 1, dir = d > 0 ? 1 : -1;
            cslot_t *c;
            if (!p || at >= p->n) return;                /* (four full, or not this progression's) */
            c = &p->s[at];
            if (k == 0 && at + dir >= 0 && at + dir < p->n) { cslot_t t = p->s[at + dir]; p->s[at + dir] = *c; *c = t; ui.key_slot[h] = (uint8_t)(at + dir + 1); }
            if (k == 1) c->q = (uint8_t)((c->q + Q_COUNT + dir) % Q_COUNT);
            if (k == 2) { int l = c->half + dir; c->half = (uint8_t)(l < 1 ? 1 : l > 16 ? 16 : l); ch_len = c->half; }
            if (k == 3) c->pedal = (uint8_t)(dir > 0);
            for (i = 0; i < ui.key_nch[h]; i++) later_off(ui.key_sy[h], ui.key_ch[h][i], SRC_KEY, h + 1);   /* heard as it is now */
            ch_pn = 0;
            ui.key_nch[h] = (uint8_t)voice(48 + (seq_key + c->root) % 12, QI[c->q], c->pedal, ui.key_ch[h]);
            ch_name[0] = 0; chord_name(ch_name, seq_key + c->root, c->q);
            strike(ui.key_sy[h], ui.key_ch[h], ui.key_nch[h], h);
            ch_shown = ui.key_slot[h] - 1;
            return;
        }
        if (k == 0 && ui.btn_held >> B_ARP & 1u) {      /* ARP held, K1: the slot cursor */
            ch_cur = (uint8_t)wrap(ch_cur + (d > 0 ? 1 : -1), cur_prog()->n + 1);
            return;
        }
    }
    if (ui.page == PG_CHORD && k == 4) { prog_step(d); return; }   /* a progression: its own mode and voicing with it */
    if (k == 4 && ui.page == PG_GLOBAL && r == 3) return;   /* (a row with no type; FX's Space: the verb all share) */
    if (ui.page == PG_GLOBAL && r == 3 && k == 3) {    /* theme */
        theme = (uint8_t)wrap(theme + (d > 0 ? 1 : -1), DMK_THEMES);
        ui.val[0][PG_GLOBAL][3][3] = choice_val(theme, DMK_THEMES);   /* (its dots) */
        return;
    }
    v = vp(ui.page, r, k);
    if (ui.page == PG_SOUND && ((r == 0 && k == 0) || (r == 1 && k == 4))) {   /* a recording: a step a detent */
        *v = (uint8_t)wrap(*v + (d > 0 ? 1 : -1), DMK_SAMPLES_N);
        sp_sync();
        return;
    }
    {
        const choice_t *c = choice_of(ui.page, r, k);
        int n = *v + d;
        if (c && c->n > 1) *v = choice_val(wrap(choice_index(c, *v) + (d > 0 ? 1 : -1), c->n), c->n);   /* round */
        else *v = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    }
    sp_sync();
}

/* a key: on the roll, stopped, it writes at the cursor (or holds the note of its pitch there); playing with REC, it
 * is recorded as played; else it plays. Its note as it is, on the scale or off it */
DMK_SMALL static void ui_key(int k, int down)
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
            if (song_playing()) { song_mode = 0; mix_g = 32768; mix_dg = mix_next = 0; notice("Scene mode"); }   /* a scene picked: the song
                                                           left, its fade too */
            scn_pick(t);
        } else if (k == 1 || k == 3 || k == 5) pat_mute_flip(k / 2);
        return;
    }
    if (!down && ui.key_home[k]) { ui.key_home[k] = 0; return; }   /* its release: nothing sounded */
    if (ui.page == PG_ARR && (!ui.row[PG_ARR] || !seq_on) && (down || !ui.keys_down[k])) {
        /* Arrange: on the Scene row a white key 1-8 is the selected synth's pattern (at the bar, playing), a black key
         * 1-3 mutes; on the Song row, stopped, a white key writes a step of that scene and moves on (a key held from
         * another page: its release below, as it began) */
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
    if (down && ui.rec_ready && !seq_on && seq_rec) play_toggle();   /* ready: this key starts it, recorded from the
                                                           first step */
    ui.keys_down[k] = (uint8_t)down;
    /* a key, a chord: on CHORD, or everywhere but the roll when the Keys row says so; released as it began */
    if (down ? chords_here() : ui.key_nch[k] > 0) {
        int j;
        if (down) {
            s = ui.synth;
            ui.key_sy[k] = (uint8_t)s;
            ui.key_slot[k] = 0;
            if (!seq_on && seq_rec && ui.page != PG_ROLL) {   /* stopped, REC armed: the chord into the slot at the cursor */
                ui.key_slot[k] = (uint8_t)(slot_write(n) + 1);
                if (!ui.key_slot[k]) notice(ch.prog < 16 ? "User 1-4 are full" : "Eight chords at most");
                ui.val[0][PG_CHORD][0][4] = choice_val(ch.prog, PROG_N);
            }
            ui.key_nch[k] = (uint8_t)chord_key(n, ui.octave, ui.key_ch[k]);
            if (ui.key_slot[k]) ch_shown = ui.key_slot[k] - 1;
            strike(s, ui.key_ch[k], ui.key_nch[k], k);
            ui.key_roll[k] = seq_on && seq_rec && !song_rec ? 2 : 0;   /* (a song's take records the switches only) */
            ui.key_t0[k] = seq_now();
            if (ui.key_roll[k] == 2) ui.key_q8[k] = seq_rec_q8(s);   /* where on its own roll, as played */
            if (ui.page == PG_ROLL && !seq_on && seq_rec) {   /* the roll, stopped, REC lit: the chord written at the cursor */
                int c = ui.roll_cur[s];
                roll_heard(s);
                for (j = 0; j < ui.key_nch[k]; j++)     /* as it strums, all ending at the pen's length */
                    if (roll_strum(s, (uint32_t)c << 8, (uint32_t)(c + ui.roll_pen[s]) << 8, ui.key_ch[k][j], VEL_KEY, j, ui.key_nch[k], 1) < 0) roll_full();
                roll_show(s, ui.key_ch[k][0]);
                ui.key_roll[k] = 4;
                ui.key_step[k] = (uint8_t)c;
            } else if (ui.page == PG_ROLL && !seq_on) roll_unarmed();
            return;
        }
        s = ui.key_sy[k];
        if (ui.key_slot[k]) { ch_cur = ui.key_slot[k]; ui.key_slot[k] = 0; }   /* the cursor to the next slot */
        if (ui.key_roll[k] == 4) ui.roll_cur[s] = (uint8_t)((ui.key_step[k] + ui.roll_pen[s]) % sq[s].steps);   /* after the chord */
        if (ui.key_roll[k] == 2 && seq_on) roll_heard(s);
        for (j = 0; j < ui.key_nch[k]; j++) {
            later_off(s, ui.key_ch[k][j], SRC_KEY, k + 1);
            if (ui.key_roll[k] == 2 && seq_on) {        /* recorded: each of its notes into the synth's roll, where the
                                                           strum started it (one let go before it came never sounded) */
                uint32_t held = seq_now() - ui.key_t0[k], d = strum_q8(j, ui.key_nch[k]);
                if (d && d >= held) continue;
                if (roll_strum(s, ui.key_q8[k], ui.key_q8[k] + held, ui.key_ch[k][j], 100, j, ui.key_nch[k], 0) < 0) roll_full();
                else if (!j) roll_show(s, ui.key_ch[k][j]);
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
        if (ui.page == PG_ROLL && !seq_on) {            /* the roll, stopped: a note there held, to edit or delete; a new
                                                           one written only while REC is lit (the owner: else it records
                                                           without REC), else it only plays */
            int c = ui.roll_cur[s];                     /* (the key as it is, on every page: the owner heard the roll
                                                           move a black key onto the scale) */
            if (seq_find(s, c, n) >= 0) { ui.key_roll[k] = 1; ui.key_step[k] = (uint8_t)c; }
            else if (!seq_rec) roll_unarmed();
            else if (seq_add(s, c, ui.roll_pen[s], n, 100) >= 0) { ui.key_roll[k] = 1; ui.key_step[k] = (uint8_t)c; roll_heard(s); roll_show(s, n); }
            else roll_full();
        } else if (seq_on && seq_rec && !song_rec) {   /* a song's take records the switches, not the keys */
            ui.key_roll[k] = 2;
            ui.key_t0[k] = seq_now();
            ui.key_q8[k] = seq_rec_q8(s);               /* where on its own roll, as played (a switched pattern starts
                                                           it again) */
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
    } else if (ui.key_roll[k] == 2 && seq_on) {         /* recorded: where it began, as long as it was held */
        uint32_t held = seq_now() - ui.key_t0[k];
        roll_heard(s);
        if (roll_strum(s, ui.key_q8[k], ui.key_q8[k] + held, ui.key_note[k], 100, 0, 1, 0) < 0) roll_full();
        else roll_show(s, ui.key_note[k]);
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
        if (!ui.keys_down[i] && chords_here() && ui.page != PG_ROLL && ch_cmask >> (n % 12) & 1u) leds[i] = 255;   /* the chord now */
    }
    if (ui.btn_held >> B_HOME & 1u) {                   /* HOME held: the scenes in use lit (the next blinking), the synths playing */
        for (i = 0; i < DMK_KEYS; i++) leds[i] = 0;
        for (i = 0; i < SCENES; i++) leds[WKEY[i]] = i == scn_cur || (scn[i].used && i != scn_next) || (i == scn_next && (ui.now / 250u) & 1u) ? 255 : 0;
        for (i = 0; i < DMK_SYNTHS; i++) leds[1 + 2 * i] = pat_mute >> i & 1u ? 0 : 255;
    }
    for (i = 0; i < B_COUNT; i++) leds[DMK_KEYS + i] = BTN_PAGE[i] == ui.page ? 255 : 0;
    leds[DMK_KEYS + B_PLAY] = seq_on || (ui.rec_ready && seq_rec && (ui.now / 250u) & 1u) ? 255 : 0;   /* ready: blinking */
    leds[DMK_KEYS + B_REC] = seq_rec ? 255 : 0;         /* REC armed: lit (it never was) */
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

/* ---- MIDI in (dmk_midi): notes on the synth selected, or synth n on channel n (GLO's MIDI row, In: Ch 1-3), the
 * synths in harmony with it along; with the clock outside (Clock: Ext), its start and stop play and stop, and each six
 * of its 24 a beat step the sequences, its pace taken as the tempo */
static uint8_t mi_hn[DMK_SYNTHS][128][DMK_SYNTHS];      /* a note's harmony on each follower, + 1 */
static uint8_t mi_sy[16][128];                          /* each channel's notes on: the synth that plays it, + 1 */
static uint32_t mi_clk, mi_last, mi_per;                /* clocks since start; when the last came; samples between, Q4 */
static void midi_in(int st, int d1, int d2)
{
    int ty = st & 0xF0, s, f;
    if (st == 0xF8) {                                   /* a clock */
        uint32_t now = seq_frames, dt = now - mi_last;
        mi_last = now;
        ext_last = now; ext_seen = 1;
        if (dt > 200u && dt < 20000u) mi_per = mi_per ? mi_per + (int32_t)(dt * 16u - mi_per) / 8 : dt * 16u;
        if (clock_ext && mi_per) { int b = (int)(DMK_SR * 60u * 16u / (mi_per * 24u)); seq_bpm = b < 20 ? 20 : b > 300 ? 300 : b; }
        if (clock_ext && seq_on && ++mi_clk % 6u == 0u) {
            seq_step();
            if (seq_stop_req) { seq_stop_req = 0; seq_play(0); }   /* the song's end, on the outside clock too */
        }
        return;
    }
    if (st == 0xFA || st == 0xFB) {                     /* start, continue */
        if (clock_ext && !seq_on) { mi_clk = 0; if (song_mode && song_n) song_start(0); seq_play(1); }
        return;
    }
    if (st == 0xFC) { if (clock_ext) seq_play(0); return; }   /* stop */
    if ((ty != 0x80 && ty != 0x90) || d1 < 0 || d1 > 127) return;
    s = midi_chans && (st & 15) < DMK_SYNTHS ? st & 15 : ui.synth;
    if (mi_sy[st & 15][d1]) {                           /* on already: the synth it began on; ended first if again */
        int s0 = mi_sy[st & 15][d1] - 1;
        mi_sy[st & 15][d1] = 0;
        note_off_s(s0, d1);
        for (f = 0; f < DMK_SYNTHS; f++) if (mi_hn[s0][d1][f]) { note_off_s(f, mi_hn[s0][d1][f] - 1); mi_hn[s0][d1][f] = 0; }
        if (!(ty == 0x90 && d2)) return;
    } else if (!(ty == 0x90 && d2)) return;             /* an off for nothing on */
    {
        mi_sy[st & 15][d1] = (uint8_t)(s + 1);
        note_on_vel(s, d1, SRC_KEY, d2);                /* its velocity heard (VOICE's Velocity) */
        for (f = 0; f < DMK_SYNTHS; f++) {
            mi_hn[s][d1][f] = 0;
            if (f != s && harm_of(f) == s) { int h = harm_note(f, d1); if (h >= 0 && h < 128) { note_on_vel(f, h, SRC_KEY, d2); mi_hn[s][d1][f] = (uint8_t)(h + 1); } }
        }
    }
}

