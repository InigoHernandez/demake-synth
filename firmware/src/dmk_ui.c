/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's screen and controls, phase 0: the shell, under the design rules (docs/plan.md, 5): one typeface (Geist),
 * all lowercase, three sizes (FS 11, FM 16, FL 32); eight colours, each a state (PAPER the ground, LINE the structure,
 * LABEL what a thing is, INK what it holds, FOCUS what is being edited, PLAY what sounds, REC what records, HARM the
 * harmony); a rigid grid of hairline cells; sliders and segment bars for every value. The stages are placeholders
 * until their pages are built. Included by dmk_core.c.
 *
 * The grid: 240 x 240 on an 8 px rhythm, four 60 px columns under K1-K4.
 *   status  0-19    the page and the synth, the synths, the transport and the tempo
 *   fields  20-67   four cells: label, value, slider
 *   stage   68-219  the page's picture
 *   footer  220-239 the ALGORITHM cell, then what OCT− / OCT+ do now, or the chord and the key */

enum { PG_HOME, PG_ROLL, PG_SOUND, PG_FILTER, PG_AMP, PG_FX, PG_CHORD, PG_GLOBAL, PG_LFO, PG_DISCS, PG_COUNT };

#define Y_FIELDS 20
#define Y_STAGE  68
#define Y_FOOT   220
#define COL      60
#define PAD      6                                      /* text's inset in a cell */

typedef struct {
    const char *name;
    uint8_t rows;
    const char *row[4];
    const char *algo[4];
    const char *k[4][4];
} page_t;

static const page_t PAGE[PG_COUNT] = {
    {"home", 4, {"level", "verb", "echo", "pan"}, {"tempo", "tempo", "tempo", "tempo"},
     {{"level 1", "level 2", "level 3", "verb"}, {"verb 1", "verb 2", "verb 3", "time"},
      {"echo 1", "echo 2", "echo 3", "feedback"}, {"pan 1", "pan 2", "pan 3", "width"}}},
    {"roll", 1, {"roll"}, {"zoom"}, {{"cursor", "view", "length", "fine"}}},
    {"sound", 2, {"engine", "layer"}, {"engine", "sample"},
     {{"source", "shape", "move", "grit"}, {"blend", "tune", "loop", "tone"}}},
    {"filter", 1, {"filter"}, {"type"}, {{"cutoff", "reso", "env amt", "drift"}}},
    {"amp", 1, {"amp"}, {"level"}, {{"attack", "decay", "sustain", "release"}}},
    {"fx", 4, {"color", "media", "motion", "space"}, {"effect", "effect", "effect", ""},
     {{"amount", "tone", "time", "spice"}, {"amount", "tone", "time", "spice"},   /* an effect names its own */
      {"amount", "tone", "time", "spice"}, {"verb", "echo", "pan", "width"}}},
    {"chord", 2, {"voice", "key"}, {"prog", "prog"},
     {{"voicing", "invert", "spread", "strum"}, {"key", "mode", "change", "rhythm"}}},
    {"global", 4, {"verb", "echo", "song", "setup"}, {"type", "type", "", "velocity"},
     {{"return", "tone", "size", "mod"}, {"return", "tone", "time", "feedback"},
      {"swing", "switch", "click", "rec"}, {"theme", "keys", "midi ch", "clock"}}},
    {"lfo", 1, {"lfo"}, {"target"}, {{"rate", "depth", "shape", "fade"}}},
    {"songs", 1, {"songs"}, {""}, {{"song", "card", "", ""}}},
};

/* the page each button opens (-1: none) */
static const int8_t BTN_PAGE[B_COUNT] = {PG_FX, PG_FILTER, PG_AMP, PG_LFO, PG_SOUND, PG_GLOBAL, PG_HOME, -1, PG_CHORD,
                                         PG_ROLL, -1, -1, -1, -1};
static const char *const BTN_NAME[B_COUNT] = {"fx", "sel", "env", "lfo", "edit", "glo", "home", "save", "arp", "seq",
                                              "play", "rec", "oct−", "oct+"};
static const char *const LAYER_NAME[B_COUNT] = {"the four amounts", "help", "help", "help", "the sound", "the history",
                                                "patterns and mutes", "songs", "the progression", "the sequence",
                                                "", "", "", ""};
static const char *const SYNTH_SOUND[DMK_SYNTHS] = {"snow pad", "felt piano", "glass pluck"};

/* a choice field: its names (none: a number 0..127) */
typedef struct { uint8_t page, row, k, n; const char *const *name; } choice_t;
static const char *const ENGINES[] = {"sample", "pad", "drone", "pluck"};
static const char *const FILTERS[] = {"lp24", "lp12", "bp", "hp", "off"};
static const char *const ZOOMS[] = {"16", "32", "64"};
static const char *const OFF[] = {"off"};
static const choice_t CHOICE[] = {
    {PG_SOUND, 0, 4, 4, ENGINES}, {PG_FILTER, 0, 4, 5, FILTERS}, {PG_ROLL, 0, 4, 3, ZOOMS},
    {PG_FX, 0, 4, 1, OFF}, {PG_FX, 1, 4, 1, OFF}, {PG_FX, 2, 4, 1, OFF}, {PG_GLOBAL, 3, 0, DMK_THEMES, THEME_NAME},
};

static struct {
    uint8_t page, row[PG_COUNT], synth, boot_done;
    int8_t octave;
    uint8_t val[PG_COUNT][4][5];                        /* [page][row][K1..K4, ALGORITHM] 0..127 */
    uint32_t now, btn_down_at[B_COUNT], note_ms, touch_ms;
    int8_t touch_k;                                     /* the field turned last (FOCUS for a second) */
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

/* ---- the parts */
static void hair_h(int y) { hline(0, y, DMK_W, C_LINE); }

/* a slider: the track in LINE, the value's share in INK (FOCUS while it turns); a choice is a segment bar */
static void slider(int x, int y, int w, int v, const choice_t *c, int focus)
{
    int on = focus ? C_FOCUS : C_INK;
    if (c && c->n > 1) {
        int i, gap = 2, sw = (w - gap * (c->n - 1)) / c->n, sel = choice_index(c, v);
        for (i = 0; i < c->n; i++) fill(x + i * (sw + gap), y, sw, 3, i == sel ? on : C_LINE);
        return;
    }
    fill(x, y, w, 3, C_LINE);
    fill(x, y, 1 + (v * (w - 1)) / 127, 3, on);
}

static int row_off(int page, int row) { return page == PG_FX && row < 3; }   /* (phase 0: every effect is off) */

static void field(int i, const char *label, int page, int row)
{
    int x = i * COL, focus = ui.touch_k == i && ui.now - ui.touch_ms < 1000u;
    char v[16];
    if (i) vline(x, Y_FIELDS, Y_STAGE - Y_FIELDS, C_LINE);
    if (!label[0]) return;
    text_fit(x + PAD, Y_FIELDS + 4, COL - 2 * PAD, label, &FS, C_LABEL);
    if (row_off(page, row)) {
        text(x + PAD, Y_FIELDS + 20, "—", &FM, C_LABEL);
        fill(x + PAD, Y_FIELDS + 40, COL - 2 * PAD, 3, C_LINE);
        return;
    }
    val_text(v, page, row, i);
    text_fit(x + PAD, Y_FIELDS + 20, COL - 2 * PAD, v, &FM, focus ? C_FOCUS : C_INK);
    slider(x + PAD, Y_FIELDS + 40, COL - 2 * PAD, ui.val[page][row][i], choice_of(page, row, i), focus);
}

static void status_bar(void)
{
    char t[40];
    int i;
    fill(0, 0, DMK_W, Y_FIELDS, C_PAPER);
    {
        char *o = str_cat(t, PAGE[ui.page].name);
        if (PAGE[ui.page].rows > 1) { o = str_cat(o, " · "); o = str_cat(o, PAGE[ui.page].row[ui.row[ui.page]]); }
        if (ui.page != PG_GLOBAL && ui.page != PG_DISCS) { o = str_cat(o, " · "); str_cat(o, SYNTH_SOUND[ui.synth]); }
    }
    text_fit(PAD, 4, 132, t, &FS, C_INK);
    for (i = 0; i < DMK_SYNTHS; i++) {                  /* the synths: the current one inverted */
        char s[2] = {(char)('1' + i), 0};
        int x = 150 + i * 16;
        if (i == ui.synth) { fill(x, 4, 13, 12, C_INK); text_c(x + 7, 4, s, &FS, C_PAPER); }
        else text_c(x + 7, 4, s, &FS, C_LABEL);
    }
    fill(202, 7, 6, 6, C_LABEL);                        /* stopped: a square; PLAY a triangle, REC a dot (later) */
    {
        char b[8];
        fmt_int(b, ui.val[PG_HOME][0][4]);
        text_r(DMK_W - PAD, 4, b, &FS, C_INK);
    }
    hair_h(Y_FIELDS - 1);
}

static void fields_band(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], k;
    fill(0, Y_FIELDS, DMK_W, Y_STAGE - Y_FIELDS, C_PAPER);
    for (k = 0; k < 4; k++) field(k, pg->k[r][k], ui.page, r);
    hair_h(Y_STAGE - 1);
}

static void footer(const char *hint, int hint_c)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page];
    fill(0, Y_FOOT, DMK_W, DMK_H - Y_FOOT, C_PAPER);
    hair_h(Y_FOOT);
    if (pg->algo[r][0]) {                               /* the ALGORITHM cell, by its encoder's side */
        char v[16];
        int x = text(PAD, Y_FOOT + 5, pg->algo[r], &FS, C_LABEL);
        val_text(v, ui.page, r, 4);
        text_fit(x + 5, Y_FOOT + 5, 118 - x, v, &FS, ui.touch_k == 4 && ui.now - ui.touch_ms < 1000u ? C_FOCUS : C_INK);
    }
    vline(COL * 2, Y_FOOT, DMK_H - Y_FOOT, C_LINE);
    text_fit(COL * 2 + PAD, Y_FOOT + 5, COL * 2 - 2 * PAD, hint, &FS, hint_c);
}

/* ---- the stages (placeholders under the rules) */
static void stage_home(void)
{
    static const char *const LINE[3] = {"plays the chords", "a melody, on the key", "muted"};
    static const char *const CH[4] = {"am(add9)", "fmaj7", "cmaj7", "gsus2"};
    int i, k, y;
    for (i = 0; i < DMK_SYNTHS; i++) {
        y = Y_STAGE + i * 36;
        if (i == ui.synth) fill(0, y, 3, 35, C_FOCUS);
        fill(PAD + 2, y + 8, 6, 6, i == 2 ? C_LINE : C_PLAY);   /* sounding, or muted */
        text_fit(PAD + 14, y + 4, 110, SYNTH_SOUND[i], &FM, i == 2 ? C_LABEL : C_INK);
        text_fit(PAD + 14, y + 21, 110, LINE[i], &FS, C_LABEL);
        for (k = 0; k < 16; k++)                        /* the sequence in miniature */
            fill(132 + k * 6, y + 14, 4, 4, ((0x9A6Bu >> k ^ (unsigned)i * 3u) & 1u) ? (i == 2 ? C_LABEL : C_INK) : C_LINE);
        hair_h(y + 35);
    }
    y = Y_STAGE + 108;
    text(PAD, y + 4, "patterns", &FS, C_LABEL);
    for (i = 0; i < 8; i++) {
        char n[2] = {(char)('1' + i), 0};
        int x = 72 + i * 21;
        if (i == 0) { fill(x, y + 3, 18, 14, C_PLAY); text_c(x + 9, y + 4, n, &FS, C_PAPER); }
        else if (i == 4) { rect(x, y + 3, 18, 14, C_FOCUS); text_c(x + 9, y + 4, n, &FS, C_FOCUS); }
        else { rect(x, y + 3, 18, 14, C_LINE); text_c(x + 9, y + 4, n, &FS, C_LABEL); }
    }
    hair_h(y + 21);
    for (i = 0; i < 4; i++) {
        int x = i * COL;
        if (i) vline(x, y + 22, 22, C_LINE);
        text_fit(x + PAD, y + 26, COL - 2 * PAD, CH[i], &FS, i == 2 ? C_HARM : C_LABEL);
    }
}

static void stage_roll(void)
{
    static const char *const ROW[9] = {"c5", "b4", "a4", "g4", "e4", "d4", "c4", "b3", "a3"};
    static const uint8_t TONE[9] = {1, 1, 1, 1, 1, 0, 1, 1, 1};
    static const struct { uint8_t row, step, len; } NOTE[5] = {{2, 0, 4}, {4, 4, 2}, {6, 8, 4}, {8, 0, 8}, {2, 10, 2}};
    int gx = 28, gy = Y_STAGE + 16, cw = 12, rh = 15, i, k, cur;
    for (k = 0; k <= 16; k++) vline(gx + k * cw, gy, rh * 9, k % 4 ? C_LINE : C_LABEL);
    for (k = 0; k < 4; k++) {
        char n[2] = {(char)('1' + k), 0};
        text(gx + k * 48 + 3, Y_STAGE + 2, n, &FS, C_LABEL);
    }
    for (i = 0; i <= 9; i++) hline(gx, gy + i * rh, 16 * cw + 1, C_LINE);
    for (i = 0; i < 9; i++) {
        int root = ROW[i][0] == 'a';
        text(PAD, gy + i * rh + 2, ROW[i], &FS, root ? C_INK : C_LABEL);
        if (TONE[i]) fill(gx - 5, gy + i * rh + 7, 2, 2, C_HARM);
    }
    for (i = 0; i < 5; i++)
        fill(gx + NOTE[i].step * cw + 1, gy + NOTE[i].row * rh + 4, NOTE[i].len * cw - 1, rh - 7, C_INK);
    cur = gx + cw * (ui.val[PG_ROLL][0][0] * 16 / 128);
    fill(cur - 1, gy - 2, 2, rh * 9 + 4, C_FOCUS);
    fill(DMK_W - 8, gy, 2, rh * 9, C_LINE);             /* VIEW: where the window sits in the range */
    fill(DMK_W - 8, gy + (ui.val[PG_ROLL][0][1] * (rh * 9 - 40)) / 127, 2, 40, C_INK);
}

static void stage_sound(void)
{
    int i;
    const char *name = SYNTH_SOUND[ui.synth];
    text(PAD, Y_STAGE + 8, ui.row[PG_SOUND] ? "layer: off" : name, &FL, C_INK);
    text(PAD, Y_STAGE + 46, ui.row[PG_SOUND] ? "one of your samples, under the engine" : "keys · c4 · 22 khz · 1.2 s",
         &FS, C_LABEL);
    for (i = 0; i < DMK_W - 2 * PAD; i++) {             /* the sample's envelope as a line */
        int h = i < 10 ? i * 4 : 40 - i / 6;
        if (h < 1) h = 1;
        px_put(PAD + i, Y_STAGE + 110 - h, C_INK);
        px_put(PAD + i, Y_STAGE + 110 + h, C_INK);
    }
    hline(PAD, Y_STAGE + 110, DMK_W - 2 * PAD, C_LINE);
    fill(150, Y_STAGE + 64, 1, 92, C_FOCUS);            /* the loop, in focus */
    fill(200, Y_STAGE + 64, 1, 92, C_FOCUS);
    text(154, Y_STAGE + 66, "loop", &FS, C_FOCUS);
}

static void curve_grid(void)
{
    int k, x0 = PAD, w = DMK_W - 2 * PAD, top = Y_STAGE + 10, h = 130;
    for (k = 0; k <= 4; k++) vline(x0 + k * w / 4, top, h, C_LINE);
    for (k = 0; k <= 2; k++) hline(x0, top + k * h / 2, w + 1, C_LINE);
}

static void stage_curve(int amp)
{
    int i, x0 = PAD, w = DMK_W - 2 * PAD, base = Y_STAGE + 140, top = Y_STAGE + 18;
    curve_grid();
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
        vline(fc, base - 6, 8, C_FOCUS);
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
        for (i = 1; i < 4; i++)
            fill(pts[i][0] - 2, pts[i][1] - 2, 5, 5, ui.touch_k == i - 1 && ui.now - ui.touch_ms < 1000u ? C_FOCUS : C_INK);
    }
}

static void stage_rows(const char *const *names, int n, int open)
{
    int i, h = (Y_FOOT - Y_STAGE) / n;
    for (i = 0; i < n; i++) {
        int y = Y_STAGE + i * h;
        if (i == open) fill(0, y, 3, h - 1, C_FOCUS);
        rect(PAD + 2, y + h / 2 - 5, 9, 9, C_INK);        /* the check box: empty, off */
        text(PAD + 18, y + h / 2 - 9, names[i], &FM, C_INK);
        text_r(DMK_W - PAD, y + h / 2 - 6, "off", &FS, C_LABEL);
        hair_h(y + h - 1);
    }
}

static void stage_chord(void)
{
    static const char *const CH[4] = {"i add9", "vi maj7", "iii maj7", "vii sus2"};
    static const uint8_t VOICED[14] = {1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    int i, y = Y_STAGE + 50;
    text(PAD, Y_STAGE + 8, "am(add9)", &FL, C_HARM);
    hair_h(y);
    for (i = 0; i < 4; i++) {
        int x = i * COL;
        if (i == 0) fill(x + 1, y + 1, COL - 1, 22, C_INK);
        if (i) vline(x, y, 24, C_LINE);
        text_fit(x + PAD, y + 6, COL - 2 * PAD, CH[i], &FS, i == 0 ? C_PAPER : C_LABEL);
    }
    hair_h(y + 24);
    for (i = 0; i < 14; i++) {                          /* two octaves of white keys: the voicing's in HARM */
        int x = PAD + i * 16;
        if (VOICED[i]) fill(x + 1, y + 37, 13, 49, C_HARM);
        rect(x, y + 36, 15, 51, C_LINE);
    }
}

static void stage_songs(void)
{
    static const char *const SONG[4] = {"first snow", "embers", "night drive", "static"};
    int i;
    for (i = 0; i < 8; i++) {
        int y = Y_STAGE + i * 19;
        char n[4] = {'0', (char)('1' + i), 0, 0};
        text(PAD, y + 3, n, &FS, C_LABEL);
        text(PAD + 24, y + 3, i < 4 ? SONG[i] : "—", &FS, i < 4 ? C_INK : C_LABEL);
        if (i < 4) text_r(DMK_W - PAD, y + 3, "8 patterns", &FS, C_LABEL);
        hair_h(y + 18);
    }
}

static void boot(void)
{
    int t = (int)ui.now;
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    text(PAD, 92, "demake", &FL, C_INK);
    text(PAD, 128, "an ambient lofi machine", &FS, C_LABEL);
    fill(PAD, 150, DMK_W - 2 * PAD, 1, C_LINE);
    fill(PAD, 150, (DMK_W - 2 * PAD) * (t > 1500 ? 1500 : t) / 1500, 1, C_INK);
}

/* ---- the overlays: a box, as an old desktop's (paper, an ink frame, a hard shadow) */
static void box(int x, int y, int w, int h)
{
    ta_cover(x, y, w + 2, h + 2);
    fill(x + 2, y + 2, w, h, C_INK);
    fill(x, y, w, h, C_PAPER);
    rect(x, y, w, h, C_INK);
}

static void overlays(void)
{
    int b;
    if (ui.note[0] && ui.now - ui.note_ms < 1500u) {
        int w = 132, h = 22, x = DMK_W - PAD - w - 2, y = Y_FOOT - h - 6;
        box(x, y, w, h);
        text_fit(x + PAD, y + 5, w - 2 * PAD, ui.note, &FS, C_INK);
    }
    for (b = 0; b < B_COUNT; b++)
        if (ui.btn_held >> b & 1u && !(ui.btn_used >> b & 1u) && ui.now - ui.btn_down_at[b] > 350u && LAYER_NAME[b][0]) {
            char t[40];
            int w, x;
            str_cat(str_cat(str_cat(t, BTN_NAME[b]), " held: "), LAYER_NAME[b]);
            w = text_w(t, &FS) + 2 * PAD;
            x = (DMK_W - w) / 2;
            box(x, Y_FOOT - 28, w, 22);
            text(x + PAD, Y_FOOT - 23, t, &FS, C_INK);
            break;
        }
}

static const uint8_t *ui_frame(uint32_t ms)
{
    ui.now = ms;
    clip_all();
#ifdef DMK_TEXT_AUDIT
    ta_reset();
#endif
    if (!ui.boot_done && ms < 1500u) { boot(); return fb; }
    ui.boot_done = 1;
    fill(0, 0, DMK_W, DMK_H, C_PAPER);
    status_bar();
    fields_band();
    switch (ui.page) {
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
    if (ui.page == PG_ROLL) footer("oct− del · oct+ copy", C_LABEL);
    else footer("am(add9) · a aeolian", C_HARM);
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
            fmt_int(str_cat(t, "octave "), ui.octave);
            notice(t);
        }
        return;
    }
    ui.btn_held &= (uint16_t)~bit;
    if (ui.btn_used & bit) return;                      /* a modifier that was used does nothing on release */
    if (ui.now - ui.btn_down_at[b] > 1000u && b == B_SAVE) { ui.page = PG_DISCS; return; }
    if (ui.now - ui.btn_down_at[b] > 350u) return;      /* a hold showed its layer: no tap */
    if (b == B_SAVE) { notice("nothing to save yet"); return; }
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
    if (e == E_PRESET) { notice("sounds come in phase 1"); return; }
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
