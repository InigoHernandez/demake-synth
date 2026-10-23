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
    {"Roll", 1, {"Roll"}, {"Zoom"}, {{"Cursor", "View", "Length", "Fine"}}},
    {"Sound", 2, {"Engine", "Layer"}, {"Engine", "Sample"},
     {{"Source", "Shape", "Move", "Grit"}, {"Blend", "Tune", "Loop", "Tone"}}},
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
static const char *const SYNTH_SOUND[DMK_SYNTHS] = {"Snow pad", "Felt piano", "Glass pluck"};

/* a choice field: its names (none: a number 0..127) */
typedef struct { uint8_t page, row, k, n; const char *const *name; } choice_t;
static const char *const ENGINES[] = {"Sample", "Pad", "Drone", "Pluck"};
static const char *const FILTERS[] = {"LP24", "LP12", "BP", "HP", "Off"};
static const char *const ZOOMS[] = {"16", "32", "64"};
static const char *const OFF[] = {"Off"};
static const choice_t CHOICE[] = {
    {PG_SOUND, 0, 4, 4, ENGINES}, {PG_FILTER, 0, 4, 5, FILTERS}, {PG_ROLL, 0, 4, 3, ZOOMS},
    {PG_FX, 0, 4, 1, OFF}, {PG_FX, 1, 4, 1, OFF}, {PG_FX, 2, 4, 1, OFF}, {PG_GLOBAL, 3, 0, DMK_THEMES, THEME_NAME},
};

static struct {
    uint8_t page, row[PG_COUNT], synth, boot_done;
    int8_t octave;
    uint8_t val[PG_COUNT][4][5];                        /* [page][row][K1..K4, ALGORITHM] 0..127 */
    uint32_t now, btn_down_at[B_COUNT], note_ms, touch_ms;
    int8_t touch_k;                                     /* the field turned last (lit for a second) */
    uint16_t btn_held, btn_used;                        /* bit per button */
    char note[32];                                      /* a notice, shown for 1.5 s from note_ms */
    uint8_t keys_down[DMK_KEYS];
} ui;

static uint8_t leds[DMK_KEYS + B_COUNT];

static void notice(const char *s)
{
    int i;
    for (i = 0; i < 31 && s[i]; i++) ui.note[i] = s[i];
    ui.note[i] = 0;
    ui.note_ms = ui.now ? ui.now : 1;
}

static void ui_init(void)
{
    int p, r, k;
    for (p = 0; p < PG_COUNT; p++)
        for (r = 0; r < 4; r++)
            for (k = 0; k < 5; k++) ui.val[p][r][k] = 64;
    ui.val[PG_HOME][0][4] = 72;                         /* tempo 72 */
    ui.val[PG_SOUND][0][4] = 0;                         /* sample */
    ui.val[PG_FILTER][0][4] = 0;                        /* lp24 */
    ui.val[PG_ROLL][0][4] = 0;                          /* 16 */
    ui.val[PG_ROLL][0][0] = 24;
    ui.page = PG_HOME;
    ui.touch_k = -1;
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
    int v = ui.val[page][row][k];
    if (page == PG_GLOBAL && row == 3 && k == 0) { str_cat(o, THEME_NAME[theme]); return; }
    if (c) { str_cat(o, c->name[choice_index(c, v)]); return; }
    fmt_int(o, v);
}

static int lit(int k) { return ui.touch_k == k && ui.now - ui.touch_ms < 1000u; }
static int row_off(int page, int row) { return page == PG_FX && row < 3; }   /* (phase 0: every effect is off) */

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
    meter(x, Y_FIELDS + 44, w, ui.val[page][row][i], choice_of(page, row, i), on, 0);
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

static void fields_band(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], k;
    for (k = 0; k < 4; k++) field(k, pg->k[r][k], ui.page, r);
}

static void foot(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page];
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

static void stage_home(void)                            /* the synths: a card each, lit while it sounds */
{
    static const uint8_t ENGINE[DMK_SYNTHS] = {0, 1, 3};
    int i;
    for (i = 0; i < DMK_SYNTHS; i++) {
        int x = cell_x(SX, SW, DMK_SYNTHS, i), w = cell_w(SX, SW, DMK_SYNTHS, i), sounding = i != 2, cx = x + w / 2;
        char a[16], *b;
        str_cat(a, SYNTH_SOUND[i]);
        for (b = a; *b && *b != ' '; b++) {}
        if (*b) *b++ = 0;                               /* "Snow pad": what, which */
        if (i == ui.synth) card(x, SY, w, SH, C_INK, 2);
        else card(x, SY, w, SH, C_LINE, 1);
        card_title(x, SY, w, a, b, !sounding);
        if (sounding) round_fill(cx - 16, SY + 46, 32, 32, 16, HOLO);
        else round_line(cx - 16, SY + 46, 32, 32, 16, 1, C_LINE);
        text_fit(x + PAD + 1, SY + SH - 20, w - 2 * PAD, sounding ? ENGINES[ENGINE[i]] : "Muted", &FS, C_LABEL);
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
    static const char *const ROW[7] = {"A4", "G4", "E4", "D4", "C4", "B3", "A3"};
    static const struct { uint8_t row, step, len; } NOTE[4] = {{0, 0, 4}, {2, 4, 2}, {4, 8, 4}, {6, 0, 8}};
    int gx = SX + 30, gy = SY + 7, cw = 12, rh = 15, i, k, cur;
    card(SX, SY, SW, SH, C_LINE, 1);
    for (k = 0; k <= 16; k += 4) vline(gx + k * cw, gy, rh * 7, C_LINE);   /* the beats: rules; the steps: dots */
    for (i = 0; i <= 7; i++) for (k = 0; k <= 16; k++) if (k % 4) px_put(gx + k * cw, gy + i * rh, C_LINE);
    for (i = 0; i < 7; i++) text(SX + PAD + 1, gy + i * rh + 2, ROW[i], &FS, ROW[i][0] == 'A' ? C_INK : C_LABEL);
    for (i = 0; i < 4; i++)
        round_fill(gx + NOTE[i].step * cw + 2, gy + NOTE[i].row * rh + 3, NOTE[i].len * cw - 3, rh - 6, (rh - 6) / 2, C_INK);
    cur = gx + cw * (ui.val[PG_ROLL][0][0] * 16 / 128);
    round_fill(cur - 2, gy - 3, 4, rh * 7 + 6, 2, HOLO);
}

static void stage_sound(void)
{
    int i, mid = SY + 74, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2;
    card(SX, SY, SW, SH, C_LINE, 1);
    if (ui.row[PG_SOUND]) card_title(SX, SY, SW, "No layer", "Layer", 0);
    else card_title(SX, SY, SW, SYNTH_SOUND[ui.synth], "Sample", 0);
    for (i = 0; i < w; i++) {                           /* the sample's envelope as a line */
        int h = i < 8 ? i * 4 : 32 - i / 7;
        if (h < 1) h = 1;
        px_put(x0 + i, mid - h, C_INK);
        px_put(x0 + i, mid + h, C_INK);
    }
    round_fill(x0 + w * 3 / 5, mid - 36, 4, 72, 2, HOLO);   /* the loop's ends, lit */
    round_fill(x0 + w * 4 / 5, mid - 36, 4, 72, 2, HOLO);
}

static void stage_curve(int amp)
{
    int i, x0 = SX + PAD + 1, w = SW - 2 * PAD - 2, base = SY + SH - 14, top = SY + 14;
    card(SX, SY, SW, SH, C_LINE, 1);
    if (!amp) {
        int fc = x0 + ui.val[PG_FILTER][0][0] * w / 127, q = ui.val[PG_FILTER][0][1] / 4, ly = top + 40, yp = ly;
        for (i = 0; i < w; i++) {
            int x = x0 + i, y;
            if (x < fc - 12) y = ly;
            else if (x < fc) y = ly - q * (x - fc + 12) / 12;
            else y = ly - q + (x - fc) * 3;
            if (y > base) y = base;
            if (y < top) y = top;
            vline(x, y < yp ? y : yp, (y < yp ? yp - y : y - yp) + 2, C_INK);
            yp = y;
        }
        round_fill(fc - 4, base - 4, 9, 9, 4, lit(0) ? HOLO : C_INK);
    } else {
        int a = ui.val[PG_AMP][0][0] / 3 + 4, d = ui.val[PG_AMP][0][1] / 3 + 4, s = ui.val[PG_AMP][0][2];
        int r = ui.val[PG_AMP][0][3] / 3 + 4, hold = w - a - d - r, sy = base - (s * (base - top)) / 127;
        int pts[5][2] = {{x0, base}, {x0 + a, top}, {x0 + a + d, sy}, {x0 + a + d + hold, sy}, {x0 + w - 1, base}};
        for (i = 0; i < 4; i++) {
            int k, n = pts[i + 1][0] - pts[i][0], yp = pts[i][1];
            for (k = 0; k <= n; k++) {
                int y = pts[i][1] + (n ? (pts[i + 1][1] - pts[i][1]) * k / n : 0);
                vline(pts[i][0] + k, y < yp ? y : yp, (y < yp ? yp - y : y - yp) + 2, C_INK);
                yp = y;
            }
        }
        for (i = 1; i < 4; i++) round_fill(pts[i][0] - 4, pts[i][1] - 4, 9, 9, 4, lit(i - 1) ? HOLO : C_INK);
    }
}

/* four rows, a pill each: its name, its switch (off), the open one framed in ink */
static void stage_rows(const char *const *names, int n, int open)
{
    int i, h = (SH - (n - 1) * G) / n;
    for (i = 0; i < n; i++) {
        int y = SY + i * (h + G), tx = SX + SW - PAD - 22;
        if (i == open) round_line(SX, y, SW, h, h / 2, 2, C_INK);
        else round_line(SX, y, SW, h, h / 2, 1, C_LINE);
        text(SX + 14, y + (h - 20) / 2 - 1, names[i], &FM, C_INK);
        round_line(tx, y + h / 2 - 6, 22, 12, 6, 1, C_LABEL);   /* the switch: off */
        round_fill(tx + 3, y + h / 2 - 3, 6, 6, 3, C_LABEL);
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

static void boot(void)
{
    int t = (int)ui.now;
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    text_c(DMK_W / 2, 92, "demake", &FL, HOLO);
    round_fill(70, 149, 100, 4, 2, C_LINE);
    round_fill(70, 149, 4 + 96 * (t > 1500 ? 1500 : t) / 1500, 4, 2, C_INK);
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
    if (!ui.boot_done && ms < 1500u) { boot(); return fb; }
    ui.boot_done = 1;
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
    case PG_FX: stage_rows(PAGE[PG_FX].row, 4, ui.row[PG_FX]); break;
    case PG_CHORD: stage_chord(); break;
    case PG_GLOBAL: stage_rows(PAGE[PG_GLOBAL].row, 4, ui.row[PG_GLOBAL]); break;
    case PG_LFO: stage_curve(1); break;
    default: stage_songs(); break;
    }
    foot();
    overlays();
    return fb;
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
    if (BTN_PAGE[b] >= 0) {
        int p = BTN_PAGE[b];
        if (ui.page == p) ui.row[p] = (uint8_t)((ui.row[p] + 1) % PAGE[p].rows);   /* again: the next row */
        else ui.page = (uint8_t)p;
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
    if (e == E_SELECT) { int s = ui.synth + (d > 0 ? 1 : -1); ui.synth = (uint8_t)(s < 0 ? 0 : s >= DMK_SYNTHS ? DMK_SYNTHS - 1 : s); return; }
    if (e == E_PRESET) { notice("Sounds come in phase 1"); return; }
    k = e == E_ALGO ? 4 : e >= E_K1 && e <= E_K4 ? e - E_K1 : -1;
    if (k < 0) return;
#ifdef DMK_BENCH
    if (k == 0) { bench_level += d * 10; if (bench_level < 0) bench_level = 0; if (bench_level > 1000) bench_level = 1000; }
#endif
    ui.touch_k = (int8_t)k;
    ui.touch_ms = ui.now;
    if (ui.page == PG_GLOBAL && r == 3 && k == 0) {    /* theme */
        int t = theme + (d > 0 ? 1 : -1);
        theme = (uint8_t)(t < 0 ? DMK_THEMES - 1 : t >= DMK_THEMES ? 0 : t);
        return;
    }
    v = &ui.val[ui.page][r][k];
    {
        const choice_t *c = choice_of(ui.page, r, k);
        int n = c && c->n > 1 ? (choice_index(c, *v) + (d > 0 ? 1 : -1)) * 128 / c->n + 128 / c->n / 2 : *v + d;
        if (c && c->n > 1 && (n < 0 || n > 127)) return;   /* a choice stops at its ends */
        *v = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    }
}

static void ui_key(int k, int down)
{
    int n;
    if (k < 0 || k >= DMK_KEYS) return;
    ui.boot_done = 1;
    n = 53 + k + ui.octave * 12;                        /* F3 = 53, chromatic */
    ui.keys_down[k] = (uint8_t)down;
    if (down) note_on(n); else note_off(n);
}

static const uint8_t *ui_leds(void)
{
    int i;
    for (i = 0; i < DMK_KEYS; i++) leds[i] = ui.keys_down[i] ? 255 : 0;
    for (i = 0; i < B_COUNT; i++) leds[DMK_KEYS + i] = BTN_PAGE[i] == ui.page ? 255 : 0;
    leds[DMK_KEYS + B_OCTD] = ui.octave < 0 ? 255 : 0;
    leds[DMK_KEYS + B_OCTU] = ui.octave > 0 ? 255 : 0;
    return leds;
}
