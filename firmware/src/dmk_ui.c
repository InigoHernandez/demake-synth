/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's screen and controls, phase 0: the shell (docs/plan.md, 5). Every page is a window: a title bar (0-15),
 * four fields under K1-K4 (16-55), the stage (56-211), the taskbar (212-239). The stages are placeholders in the
 * planned style until their pages are built; the fields hold values the knobs turn. Included by dmk_core.c. */

enum { PG_HOME, PG_ROLL, PG_SOUND, PG_FILTER, PG_AMP, PG_FX, PG_CHORD, PG_GLOBAL, PG_LFO, PG_DISCS, PG_COUNT };

#define Y_FIELDS 16
#define Y_STAGE  56
#define Y_TASK   212
#define M        4                                      /* the grid: margin and gutter */

typedef struct {
    const char *title;
    uint8_t rows;
    const char *row[4];
    const char *algo[4];
    const char *k[4][4];
} page_t;

static const page_t PAGE[PG_COUNT] = {
    {"HOME", 4, {"LEVEL", "VERB", "ECHO", "PAN"}, {"TEMPO", "TEMPO", "TEMPO", "TEMPO"},
     {{"LEVEL 1", "LEVEL 2", "LEVEL 3", "VERB"}, {"VERB 1", "VERB 2", "VERB 3", "TIME"},
      {"ECHO 1", "ECHO 2", "ECHO 3", "FEEDBACK"}, {"PAN 1", "PAN 2", "PAN 3", "WIDTH"}}},
    {"ROLL", 1, {"ROLL"}, {"ZOOM"}, {{"CURSOR", "VIEW", "LENGTH", "FINE"}}},
    {"SOUND", 2, {"ENGINE", "LAYER"}, {"ENGINE", "SAMPLE"},
     {{"SOURCE", "SHAPE", "MOVE", "GRIT"}, {"BLEND", "TUNE", "LOOP", "TONE"}}},
    {"FILTER", 1, {"FILTER"}, {"TYPE"}, {{"CUTOFF", "RESO", "ENV AMT", "DRIFT"}}},
    {"AMP", 1, {"AMP"}, {"LEVEL"}, {{"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}}},
    {"FX", 4, {"COLOR", "MEDIA", "MOTION", "SPACE"}, {"EFFECT", "EFFECT", "EFFECT", "—"},
     {{"—", "—", "—", "—"}, {"—", "—", "—", "—"},     /* OFF: each effect names its own four (TAPE: DRIVE BUMP WOW FLUTTER) */
      {"—", "—", "—", "—"}, {"VERB", "ECHO", "PAN", "WIDTH"}}},
    {"CHORD", 2, {"VOICE", "KEY"}, {"PROG", "PROG"},
     {{"VOICING", "INVERT", "SPREAD", "STRUM"}, {"KEY", "MODE", "CHANGE", "RHYTHM"}}},
    {"GLOBAL", 4, {"VERB", "ECHO", "SONG", "SETUP"}, {"TYPE", "TYPE", "—", "VELOCITY"},
     {{"RETURN", "TONE", "SIZE", "MOD"}, {"RETURN", "TONE", "TIME", "FEEDBACK"},
      {"SWING", "SWITCH", "CLICK", "REC"}, {"THEME", "KEYS", "MIDI CH", "CLOCK"}}},
    {"LFO", 1, {"LFO"}, {"TARGET"}, {{"RATE", "DEPTH", "SHAPE", "FADE"}}},
    {"DISCS", 1, {"DISCS"}, {"—"}, {{"DISC", "CARD", "—", "—"}}},
};

/* the page each button opens (-1: none) */
static const int8_t BTN_PAGE[B_COUNT] = {PG_FX, PG_FILTER, PG_AMP, PG_LFO, PG_SOUND, PG_GLOBAL, PG_HOME, -1, PG_CHORD,
                                         PG_ROLL, -1, -1, -1, -1};
static const char *const BTN_NAME[B_COUNT] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                              "PLAY", "REC", "OCT−", "OCT+"};
static const char *const SYNTH_SOUND[DMK_SYNTHS] = {"SNOW PAD", "FELT PIANO", "GLASS PLUCK"};
static const char *const ENGINE_NAME[4] = {"SAMPLE", "PAD", "DRONE", "PLUCK"};
static const char *const FILTER_NAME[5] = {"LP24", "LP12", "BP", "HP", "OFF"};

static struct {
    uint8_t page, row[PG_COUNT], synth, boot_done;
    int8_t octave;
    uint8_t val[PG_COUNT][4][5];                        /* [page][row][K1..K4, ALGORITHM] 0..127 */
    uint32_t now, btn_down_at[B_COUNT], note_ms;
    uint16_t btn_held, btn_used;                        /* bit per button */
    char note[32];                                      /* a notice, shown for 1.5 s from note_ms */
    uint8_t keys_down[DMK_KEYS];
    uint32_t snow_seed;
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
    ui.val[PG_HOME][0][4] = 72;                         /* TEMPO 72 */
    ui.val[PG_GLOBAL][3][0] = 0;                        /* THEME SNOW */
    ui.page = PG_HOME;
    ui.snow_seed = 12345u;
}

/* ---- values as text */
static void val_text(char *o, int page, int row, int k)
{
    int v = ui.val[page][row][k];
    if (page == PG_HOME && k == 4) { fmt_int(o, v); return; }
    if (page == PG_SOUND && row == 0 && k == 4) { str_cat(o, ENGINE_NAME[v * 4 / 128]); return; }
    if (page == PG_FILTER && k == 4) { str_cat(o, FILTER_NAME[v * 5 / 128]); return; }
    if (page == PG_GLOBAL && row == 3 && k == 0) { str_cat(o, THEME_NAME[theme]); return; }
    if (page == PG_ROLL && k == 4) { str_cat(o, v < 43 ? "16" : v < 86 ? "32" : "64"); return; }
    fmt_int(o, v);
}

/* ---- the chrome */
static void cassette_icon(int x, int y, int c)          /* 12 x 8 */
{
    tile(x, y, 12, 8, 1, C_FACE, c);
    hline(x + 3, y + 2, 6, c);
    px_put(x + 4, y + 4, c); px_put(x + 7, y + 4, c);
    hline(x + 3, y + 6, 6, C_SHADOW);
}

/* text on the title colour: highlight on a light theme, ink on a dark one */
static int c_on_title(void) { return THEME_DARK[theme] ? C_INK : C_HILITE; }

static void title_bar(const char *title)
{
    fill(0, 0, 150, 16, C_TITLE1);                     /* flat under the title (never text on dither), */
    grad(150, 0, DMK_W - 150, 16, C_TITLE1, C_TITLE2, 1);   /* then the gradient to the right end */
    cassette_icon(M, 4, C_INK);
    text_fit(20, 1, 172, title, &F15, c_on_title());
    {                                                   /* three small window boxes, decorative */
        int i;
        for (i = 0; i < 3; i++) { fill(198 + i * 13, 3, 10, 10, C_FACE); bevel(198 + i * 13, 3, 10, 10, 0); }
    }
}

static void field(int i, const char *label, const char *value, int v)
{
    int w = (DMK_W - 2 * M - 3 * M) / 4, x = M + i * (w + M), t;
    text_fit(x + 2, Y_FIELDS + 1, w - 2, label, &F11, C_INK);
    tile(x, Y_FIELDS + 13, w, 20, 6, C_FOG1, C_SHADOW);
    text_fit(x + 5, Y_FIELDS + 15, w - 9, value, &F15, C_INK);
    hline(x + 3, Y_FIELDS + 36, w - 6, C_SHADOW);       /* the slider's groove and its thumb */
    t = x + 3 + (v * (w - 10)) / 127;
    tile(t, Y_FIELDS + 34, 5, 5, 1, C_FACE, C_DARK);
}

static void chip(int x, int y, int w, int h, const char *label, int pressed, const dmk_font_t *f)
{
    tile(x, y, w, h, h / 2, pressed ? C_TITLE1 : C_FACE, pressed ? C_TITLE1 : C_SHADOW);
    track = 0;
    if (text_w(label, f) > w - 6) text_fit(x + 3, y + (h - (f->ascent + 1)) / 2 - 1, w - 6, label, f, pressed ? c_on_title() : C_INK);
    else text_c(x + w / 2, y + (h - (f->ascent + 1)) / 2 - 1, label, f, pressed ? c_on_title() : C_INK);
}

static void pill_bar(int x, int y, int w, const char *label)
{
    tile(x, y, w, 13, 6, C_FACE, -1);
    text_c(x + w / 2, y + 1, label, &F11, C_INK);
}

static void taskbar(void)
{
    char b[24];
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], i, x;
    fill(0, Y_TASK, DMK_W, DMK_H - Y_TASK, C_FACE);
    hline(0, Y_TASK, DMK_W, C_HILITE);
    b[0] = 0;
    {
        char v[16];
        char *o = str_cat(b, pg->algo[r]);
        if (pg->algo[r][0] != (char)0xE2) { *o++ = ' '; *o = 0; val_text(v, ui.page, r, 4); str_cat(o, v); }
    }
    chip(M, Y_TASK + 5, 76, 18, b, 0, &F11);
    for (i = 0, x = 84; i < DMK_SYNTHS; i++, x += 29) {
        char s[3] = {'S', (char)('1' + i), 0};
        chip(x, Y_TASK + 5, 26, 18, s, i == ui.synth, &F11);
    }
    tile(171, Y_TASK + 5, 65, 18, 4, C_FOG1, C_SHADOW);   /* the tray: the clock is the tempo */
    {
        char t[8];
        fmt_int(t, ui.val[PG_HOME][0][4]);
        fill(176, Y_TASK + 11, 6, 6, C_INK);            /* stopped: a square */
        text(186, Y_TASK + 8, t, &F11, C_INK);
        text_r(231, Y_TASK + 8, "Am9", &F11, C_LIGHT);
    }
}

static void fields_band(void)
{
    const page_t *pg = &PAGE[ui.page];
    int r = ui.row[ui.page], k;
    char v[16];
    fill(0, Y_FIELDS, DMK_W, Y_STAGE - Y_FIELDS, C_PAPER);
    for (k = 0; k < 4; k++) {
        val_text(v, ui.page, r, k);
        field(k, pg->k[r][k], pg->k[r][k][0] == (char)0xE2 ? "" : v, ui.val[ui.page][r][k]);
    }
    hline(0, Y_STAGE - 1, DMK_W, C_FOG2);
}

/* ---- the stages (placeholders in the planned style) */
static void stage_home(void)
{
    int i, y = Y_STAGE + M;
    static const char *const LINE[3] = {"plays the chords", "a melody, on the key", "a pluck on the root"};
    for (i = 0; i < DMK_SYNTHS; i++, y += 31) {
        tile(M, y, DMK_W - 2 * M, 28, 6, i == ui.synth ? C_FOG1 : C_PAPER, C_FOG2);
        tile(M + 5, y + 10, 7, 7, 3, i == 2 ? C_PAPER : C_MINT, C_DARK);   /* the status dot */
        cassette_icon(M + 16, y + 9, C_INK);
        text_fit(M + 33, y + 2, 120, SYNTH_SOUND[i], &F15, C_INK);
        text_fit(M + 33, y + 16, 120, i == 2 ? "muted" : LINE[i], &F11, C_FOG3);
        {
            int k;
            for (k = 0; k < 16; k++)                    /* the pattern in miniature */
                if ((0x9A6Bu >> k ^ (unsigned)i * 3u) & 1u) fill(160 + k * 4, y + 12, 3, 3, i == 2 ? C_FOG2 : C_INK);
        }
    }
    pill_bar(M, y + 1, DMK_W - 2 * M, "PATTERNS");
    for (i = 0; i < 8; i++) chip(M + i * 29, y + 17, 26, 15, (const char[]){(char)('1' + i), 0}, i == 0, &F11);
    {
        static const char *const CH[4] = {"Am(add9)", "Fmaj7", "Cmaj7", "Gsus2"};
        int w = (DMK_W - 2 * M - 3 * M) / 4;
        for (i = 0; i < 4; i++) {
            tile(M + i * (w + M), y + 36, w, 16, 5, i == 2 ? C_LIGHT : C_FOG1, -1);
            text_fit(M + i * (w + M) + 4, y + 38, w - 6, CH[i], &F11, C_INK);
        }
    }
}

static void stage_roll(void)
{
    static const char *const ROW[10] = {"C5", "B4", "A4", "G4", "F4", "E4", "D4", "C4", "B3", "A3"};
    static const uint8_t CHORD_TONE[10] = {1, 1, 1, 1, 0, 1, 0, 1, 1, 1};
    int gx = 28, gy = Y_STAGE + 14, gw = 192, rh = 14, i, k;
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    for (k = 0; k <= 16; k++)                           /* the steps, beats darker */
        vline(gx + k * 12, gy, rh * 10, k % 4 ? C_FOG1 : C_FOG2);
    for (k = 0; k < 4; k++) {
        char n[2] = {(char)('1' + k), 0};
        text(gx + k * 48 + 3, Y_STAGE + 2, n, &F11, C_FOG3);
    }
    for (i = 0; i < 10; i++) {
        int y = gy + i * rh, root = ROW[i][0] == 'A';
        hline(gx, y, gw, C_FOG1);
        if (root) fill(M, y + 1, 22, rh - 1, C_FOG1);
        text(M + 2, y + 1, ROW[i], &F11, root ? C_INK : C_FOG3);
        if (CHORD_TONE[i]) fill(M + 19, y + 6, 2, 2, C_LIGHT);
    }
    hline(gx, gy + rh * 10, gw, C_FOG1);
    {                                                   /* the cursor, the light */
        int c = gx + 12 * (ui.val[PG_ROLL][0][0] * 16 / 128);
        fill(c, gy, 2, rh * 10, C_LIGHT);
    }
    tile(224, gy, 12, rh * 10, 6, C_FOG1, C_SHADOW);    /* the scroll bar: VIEW */
    tile(226, gy + 4 + (ui.val[PG_ROLL][0][1] * (rh * 10 - 44)) / 127, 8, 36, 4, C_FACE, C_DARK);
}

static void cassette_big(int x, int y, const char *name)
{
    tile(x, y, 150, 86, 8, C_DARK, C_INK);
    tile(x + 10, y + 8, 130, 32, 5, C_TIP, C_SHADOW);
    text_fit(x + 16, y + 11, 118, name, &F15, C_INK);
    text(x + 16, y + 26, "KEYS · C4", &F11, C_FOG3);
    tile(x + 28, y + 50, 22, 22, 11, C_FACE, C_SHADOW);
    tile(x + 100, y + 50, 22, 22, 11, C_FACE, C_SHADOW);
    tile(x + 35, y + 57, 8, 8, 4, C_DARK, -1);
    tile(x + 107, y + 57, 8, 8, 4, C_DARK, -1);
    fill(x + 56, y + 58, 38, 6, C_FOG3);
}

static void stage_sound(void)
{
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    pill_bar(M, Y_STAGE + M, DMK_W - 2 * M, ui.row[PG_SOUND] ? "LAYER" : "THE CASSETTE DECK");
    cassette_big(45, Y_STAGE + 24, SYNTH_SOUND[ui.synth]);
    {
        int i;
        for (i = 0; i < 160; i++) {                     /* the sample's envelope, in fog */
            int h = i < 8 ? i * 3 : 24 - i / 8;
            if (h < 2) h = 2;
            vline(40 + i, Y_STAGE + 140 - h / 2, h, i > 100 && i < 140 ? C_TITLE2 : C_FOG3);
        }
    }
}

static void stage_curve(int amp)
{
    int i, x0 = M + 6, w = DMK_W - 2 * M - 12, base = Y_STAGE + 132, top = Y_STAGE + 24;
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    pill_bar(M, Y_STAGE + M, DMK_W - 2 * M, amp ? "THE ENVELOPE" : "THE RESPONSE");
    tile(M, Y_STAGE + 20, DMK_W - 2 * M, 128, 6, C_FOG1, C_FOG2);
    if (!amp) {
        int fc = x0 + ui.val[PG_FILTER][0][0] * w / 127, q = ui.val[PG_FILTER][0][1] / 4;
        for (i = 0; i < w; i++) {
            int x = x0 + i, y;
            if (x < fc - 12) y = top + 40;
            else if (x < fc) y = top + 40 - q * (x - fc + 12) / 12;
            else y = top + 40 - q + (x - fc) * 3;
            if (y > base) y = base;
            if (y < top) y = top;
            dither(x, y, 1, base - y, C_FOG2, C_FOG3, 6);
            px_put(x, y, C_INK); px_put(x, y + 1, C_INK);
        }
    } else {
        int a = ui.val[PG_AMP][0][0] / 3 + 4, d = ui.val[PG_AMP][0][1] / 3 + 4, s = ui.val[PG_AMP][0][2];
        int r = ui.val[PG_AMP][0][3] / 3 + 4, hold = w - a - d - r, sy = base - (s * (base - top)) / 127;
        int pts[5][2] = {{x0, base}, {x0 + a, top}, {x0 + a + d, sy}, {x0 + a + d + hold, sy}, {x0 + w - 1, base}};
        for (i = 0; i < 4; i++) {
            int k, n = pts[i + 1][0] - pts[i][0];
            for (k = 0; k <= n; k++) {
                int y = pts[i][1] + (n ? (pts[i + 1][1] - pts[i][1]) * k / n : 0);
                dither(pts[i][0] + k, y, 1, base - y, C_FOG2, C_FOG3, 6);
                px_put(pts[i][0] + k, y, C_INK); px_put(pts[i][0] + k, y + 1, C_INK);
            }
        }
    }
}

static void stage_rows(const char *const *names, int n, int open)
{
    int i, y = Y_STAGE + M, h = (Y_TASK - Y_STAGE - M) / n - M;
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    for (i = 0; i < n; i++, y += h + M) {
        tile(M, y, DMK_W - 2 * M, h, 6, i == open ? C_FOG1 : C_PAPER, i == open ? C_LIGHT : C_FOG2);
        tile(M + 6, y + h / 2 - 5, 10, 10, 2, C_PAPER, C_DARK);   /* the check box: off */
        text(M + 22, y + h / 2 - 8, names[i], &F15, C_INK);
        text_r(DMK_W - M - 8, y + h / 2 - 5, "OFF", &F11, C_FOG3);
    }
}

static void stage_chord(void)
{
    static const char *const CH[4] = {"i add9", "VI maj7", "III maj7", "VII sus2"};
    int i, w = (DMK_W - 2 * M - 3 * M) / 4, y = Y_STAGE + 52;
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    text_c(DMK_W / 2, Y_STAGE + 10, "Am(add9)", &F28, C_INK);
    for (i = 0; i < 4; i++) chip(M + i * (w + M), y, w, 18, CH[i], i == 0, &F11);
    pill_bar(M, y + 26, DMK_W - 2 * M, "THE VOICING");
    for (i = 0; i < 14; i++) {                          /* two octaves of white keys, the voicing's in the light */
        int x = M + 2 + i * 16;
        tile(x, y + 46, 14, 44, 3, (i == 0 || i == 2 || i == 4 || i == 8) ? C_LIGHT : C_HILITE, C_SHADOW);
    }
}

static void stage_discs(void)
{
    int i;
    fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER);
    for (i = 0; i < 15; i++) {
        int x = M + 4 + (i % 5) * 46, y = Y_STAGE + 6 + (i / 5) * 44;
        tile_grad(x, y, 34, 34, 17, i < 4 ? C_PINK : C_FOG1, i < 4 ? C_MINT : C_FOG2, C_SHADOW);
        tile(x + 13, y + 13, 8, 8, 4, C_PAPER, C_SHADOW);
    }
}

static void boot(void)
{
    int i, t = (int)ui.now;
    grad(0, 0, DMK_W, DMK_H, C_FOG3, C_FOG1, 0);
    fill(40, 92, 160, 40, C_FOG1);
    text_c(DMK_W / 2, 92, "Demake", &F36, C_INK);
    tile(40, 150, 160, 12, 6, C_PAPER, C_SHADOW);
    for (i = 0; i < 12 && i * 125 < t; i++) fill(44 + i * 13, 153, 10, 6, C_TITLE2);
    for (i = 0; i < 40; i++) {                          /* snow */
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = (int)(h >> 24) % DMK_W, y = (int)((h >> 8 & 255u) + (uint32_t)t / (20u + (h & 7u))) % DMK_H;
        px_put(x, y, C_HILITE);
    }
}

/* ---- the overlays: a notice from the tray's corner, a tooltip for a held button */
static void overlays(void)
{
    if (ui.note[0] && ui.now - ui.note_ms < 1500u) {
        int w = 118, h = 34, x = DMK_W - M - w, y = Y_TASK - M - h, dy = 0;
        uint32_t age = ui.now - ui.note_ms;
        if (age < 120u) dy = (int)(120u - age) * h / 120;   /* it slides up */
        ta_cover(x, y + dy, w + 2, h + 2);
        fill(x + 2, y + dy + 2, w, h, C_FOG3);              /* the drop shadow */
        fill(x, y + dy, w, h, C_PAPER);
        rect(x, y + dy, w, h, C_DARK);
        grad(x + 1, y + dy + 1, w - 2, 11, C_TITLE1, C_TITLE2, 1);
        fill(x + 2, y + dy + 1, 40, 11, C_TITLE1);
        text(x + 4, y + dy, "notice", &F11, c_on_title());
        text_fit(x + 5, y + dy + 16, w - 9, ui.note, &F11, C_INK);
    }
    {
        int b;
        for (b = 0; b < B_COUNT; b++)
            if (ui.btn_held >> b & 1u && !(ui.btn_used >> b & 1u) && ui.now - ui.btn_down_at[b] > 350u) {
                char t[32];
                int w, x;
                str_cat(str_cat(t, BTN_NAME[b]), " held: its layer");
                w = text_w(t, &F11) + 12;
                x = (DMK_W - w) / 2;
                ta_cover(x, Y_TASK - 22, w, 18);
                tile(x, Y_TASK - 22, w, 18, 6, C_TIP, C_SHADOW);
                text(x + 6, Y_TASK - 21, t, &F11, C_INK);
                break;
            }
    }
}

static const uint8_t *ui_frame(uint32_t ms)
{
    char t[40];
    ui.now = ms;
    clip_all();
#ifdef DMK_TEXT_AUDIT
    ta_reset();
#endif
    if (!ui.boot_done && ms < 1500u) { boot(); return fb; }
    ui.boot_done = 1;
    {
        char *o = str_cat(t, PAGE[ui.page].title);
        if (ui.page != PG_DISCS && ui.page != PG_GLOBAL) { o = str_cat(o, " — "); str_cat(o, SYNTH_SOUND[ui.synth]); }
        else if (ui.page == PG_DISCS) str_cat(o, " — 4 of 15");
        else { o = str_cat(o, " — "); str_cat(o, PAGE[PG_GLOBAL].row[ui.row[PG_GLOBAL]]); }
    }
    title_bar(t);
    fields_band();
    switch (ui.page) {
    case PG_HOME: fill(0, Y_STAGE, DMK_W, Y_TASK - Y_STAGE, C_PAPER); stage_home(); break;
    case PG_ROLL: stage_roll(); break;
    case PG_SOUND: stage_sound(); break;
    case PG_FILTER: stage_curve(0); break;
    case PG_AMP: stage_curve(1); break;
    case PG_FX: stage_rows(PAGE[PG_FX].row, 4, ui.row[PG_FX]); break;
    case PG_CHORD: stage_chord(); break;
    case PG_GLOBAL: stage_rows(PAGE[PG_GLOBAL].row, 4, ui.row[PG_GLOBAL]); break;
    case PG_LFO: stage_curve(1); break;
    default: stage_discs(); break;
    }
    taskbar();
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
        ui.btn_held |= bit;
        ui.btn_down_at[b] = ui.now;
        ui.btn_used &= (uint16_t)~bit;
        {
            int o;
            for (o = 0; o < B_COUNT; o++)               /* another button held: it was used as a modifier */
                if (o != b && ui.btn_held >> o & 1u) ui.btn_used |= (uint16_t)(1u << o);
        }
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
    if (b == B_SAVE) { notice("nothing to burn yet"); return; }
    if (BTN_PAGE[b] >= 0) {
        int p = BTN_PAGE[b];
        if (ui.page == p) ui.row[p] = (uint8_t)((ui.row[p] + 1) % PAGE[p].rows);   /* again: the next row */
        else ui.page = (uint8_t)p;
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
    if (e == E_PRESET) { notice("the cassettes come in phase 1"); return; }
    k = e == E_ALGO ? 4 : e >= E_K1 && e <= E_K4 ? e - E_K1 : -1;
    if (k < 0) return;
#ifdef DMK_BENCH
    if (k == 0) { bench_level += d * 10; if (bench_level < 0) bench_level = 0; if (bench_level > 1000) bench_level = 1000; }
#endif
    if (ui.page == PG_GLOBAL && r == 3 && k == 0) {    /* THEME */
        int t = theme + (d > 0 ? 1 : -1);
        theme = (uint8_t)(t < 0 ? DMK_THEMES - 1 : t >= DMK_THEMES ? 0 : t);
        return;
    }
    v = &ui.val[ui.page][r][k];
    {
        int n = *v + d;
        *v = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    }
}

static void ui_key(int k, int down)
{
    static const uint8_t SEMI[DMK_KEYS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                           22, 23, 24, 25, 26};   /* F3 = 53, chromatic */
    int n;
    if (k < 0 || k >= DMK_KEYS) return;
    ui.boot_done = 1;
    n = 53 + SEMI[k] + ui.octave * 12;
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
