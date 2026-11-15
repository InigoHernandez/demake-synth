/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move from inside dsp.so's own code, for what the plugin API would take hours to reach: every pattern
 * full, saved and opened; the file of before the slots grew; every scale, chord and progression. Built as dsp.so is
 * (move/build.sh, static, so the Move can run it too: its times are the ones to read).
 *   inside DIR [OLD_STORE]     DIR is the tests' own: its store files are removed
 *   inside show FILE DIR       what a store of before holds, as the Move would open it */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "../dmk_move.c"

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    printf("  %-5s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fails++;
}
static double t_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static const char *in(const char *dir, const char *name)
{
    static char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return p;
}
static void *open_at(const char *dir)
{
    char js[700];
    snprintf(js, sizeof js, "{\"seed\": 7, \"data_dir\": \"%s\", \"save_on_close\": 0, \"draw_thread\": 0}", dir);
    return v2_create("", js);
}
static void copy(const char *from, const char *to)
{
    static uint8_t b[1 << 20];
    FILE *a = fopen(from, "rb"), *o = fopen(to, "wb");
    if (!a || !o) { printf("  FAIL  cannot copy %s\n", from); fails++; return; }
    const size_t n = fread(b, 1, sizeof b, a);
    fwrite(b, 1, n, o);
    fclose(a); fclose(o);
}

/* ------------------------------------------------------------------------------------- the lists */

static int same_q(const char *a, const char *b) { return !strcmp(a, b); }

static void scales(void)
{
    static const uint8_t FM1[6][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
                                      {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}};
    const int key = seq_key, mode = seq_mode;
    int first = 1, shape = 1, set = 1, round = 1, steps = 1;
    check(sizeof MODES / sizeof MODES[0] == MO_COUNT && sizeof MODE_LEN == MO_COUNT && sizeof MODENAMES / sizeof MODENAMES[0] == MO_COUNT, "the scales: every list as long");
    for (int m = 0; m < 6; m++) first &= !memcmp(MODES[m], FM1[m], 7) && MODE_LEN[m] == 7;
    check(first, "the FM-1's six first, as they are");
    for (int m = 0; m < MO_COUNT; m++) {
        int notes = 1;
        for (int i = 1; i < 8; i++) notes += MODES[m][i] != 0;
        shape &= MODES[m][0] == 0 && MODE_LEN[m] >= 5 && MODE_LEN[m] <= 8 && notes == MODE_LEN[m];
        for (int i = 1; i < MODE_LEN[m]; i++) shape &= MODES[m][i] > MODES[m][i - 1] && MODES[m][i] < 12;
        for (int k = 0; k < 12; k++) {
            seq_mode = (uint8_t)m; seq_key = (uint8_t)k;
            set &= __builtin_popcount(pc_scale()) == SCALE_N;
            for (int n = 12; n < 116; n++) {
                const int s = snap(n);
                set &= s <= n && (pc_scale() >> (s % 12) & 1u);
                round &= note_of(degree_of(s)) == s;
                steps &= degree_of(n + 1) - degree_of(n) >= 0 && degree_of(n + 1) - degree_of(n) <= 1;
            }
        }
    }
    check(shape, "each scale of five to eight notes, rising inside an octave");
    check(set, "every note snaps into its scale, at each key");
    check(round, "a note to its degree and back, in every scale and key");
    check(steps, "the degrees climb one at a time");
    seq_key = (uint8_t)key; seq_mode = (uint8_t)mode;
}

static void chords(void)
{
    int tones = 1, twin = 0, room = 1;
    check(sizeof QN / sizeof QN[0] == Q_COUNT && sizeof QS / sizeof QS[0] == Q_COUNT, "the qualities: every list as long");
    for (int q = 0; q < Q_COUNT; q++) {
        int n = 0;
        uint32_t m = 0;
        while (n < 7 && QI[q][n] >= 0) { if (n) tones &= QI[q][n] > QI[q][n - 1]; m |= 1u << QI[q][n]; n++; }
        tones &= n >= 2 && n <= 6 && QI[q][0] == 0;
        for (int t = 0; t < q; t++) {
            uint32_t w = 0;
            for (int i = 0; i < 7 && QI[t][i] >= 0; i++) w |= 1u << QI[t][i];
            twin += w == m;
        }
    }
    check(tones, "each quality two to six rising tones from the root");
    check(!twin, "no two qualities the same tones");
    check(sizeof KTYPES / sizeof KTYPES[0] == KT_COUNT, "the keys' types: the names as long as the list");
    const int key = seq_key, mode = seq_mode, kt = ch.ktype;
    int named = 1, exact = 1;
    for (int m = 0; m < MO_COUNT; m++)
        for (int t = 0; t < KT_COUNT; t++)
            for (int n = 48; n < 72; n++) {
                int8_t iv[IV_N];
                char nm[48];
                seq_mode = (uint8_t)m; seq_key = 9; ch.ktype = (uint8_t)t;
                const int q = diatonic(n, iv);
                int k = 0;
                while (k < IV_N && iv[k] >= 0) k++;
                named &= q >= 0 && q < Q_COUNT && k >= 2 && k <= 6;
                nm[0] = 0;
                chord_name(nm, n, q);
                room &= strlen(nm) < sizeof ch_name;
                int same = 1;
                for (int i = 0; i < k; i++) same &= QI[q][i] == iv[i];
                same &= k == 7 || QI[q][k] < 0;
                if (m < 6 && t < 8) exact &= same;
                if (!same) for (int o = 0; o < Q_COUNT; o++) {    /* a quality with exactly these tones: it, not a near one */
                    int eq = 1;
                    for (int i = 0; i < k; i++) eq &= QI[o][i] == iv[i];
                    named &= !(eq && (k == 7 || QI[o][k] < 0));
                }
            }
    check(named, "a chord on every key, in each scale and type, named");
    check(room, "every name fits");
    check(exact, "the six first scales' chords are the qualities they were");
    {
        int8_t iv[IV_N];
        char nm[48] = "";
        seq_mode = MO_AEOL; seq_key = 9; ch.ktype = KT_SEVENTH;
        chord_name(nm, 57, diatonic(57, iv));
        check(!strcmp(nm, "Am7"), "A Aeolian, sevenths, on A: Am7");
        nm[0] = 0; seq_mode = MO_MPENT; ch.ktype = KT_TRIAD;
        chord_name(nm, 57, diatonic(57, iv));
        check(!strcmp(nm, "Am"), "A minor pentatonic, triads, on A: Am, its thirds found by ear");
        nm[0] = 0; ch.ktype = KT_ALLMAJ9;
        chord_name(nm, 60, diatonic(60, iv));
        check(!strcmp(nm, "Cmaj9"), "All maj9, on C: Cmaj9");
    }
    seq_key = (uint8_t)key; seq_mode = (uint8_t)mode; ch.ktype = (uint8_t)kt;
}

static void progressions(void)
{
    int form = 1, names = 1, voiced = 1, used[MO_COUNT] = {0}, all = 1;
    const int key = seq_key, mode = seq_mode, vo = ch.voicing;
    for (int p = 0; p < PROG_PRE; p++) {
        const prog_t *q = &PROG[p];
        form &= q->n >= 1 && q->n <= PROG_SLOTS && q->mode < MO_COUNT && q->voicing < VO_COUNT;
        names &= same_q(q->name, PROGS[p]) && strlen(q->name) <= 13;
        used[q->mode] = 1;
        for (int i = 0; i < q->n; i++) {
            form &= q->s[i].root < 12 && q->s[i].q < Q_COUNT && q->s[i].half >= 1;
            for (int v = 0; v < VO_COUNT; v++)
                for (int k = 0; k < 12; k++) {
                    uint8_t out[8];
                    seq_mode = q->mode; seq_key = (uint8_t)k; ch.voicing = (uint8_t)v; ch_pn = 0;
                    const int n = voice(48 + (k + q->s[i].root) % 12, QI[q->s[i].q], q->s[i].pedal, out);
                    voiced &= n >= 1 && n <= 7;
                    for (int j = 0; j < n; j++) voiced &= out[j] >= 12 && out[j] <= 96;
                }
        }
    }
    for (int m = 0; m < MO_COUNT; m++) all &= used[m];
    check(form, "each preset's chords in range");
    check(names, "each preset's name the list's, and short");
    check(voiced, "every chord voiced in every voicing and key");
    check(all, "every scale has a preset of its own");
    check(!strcmp(PROGS[PROG_PRE], "User 1") && !strcmp(PROGS[PROG_N - 1], "User 4"), "the four User ones last");
    {
        const int p = ch.prog;
        ch.prog = PROG_PRE - 1;
        for (int k = 0; k < 4; k++) uprog[k].n = 0;
        const prog_t *w = edit_prog();
        check(w == &uprog[0] && ch.prog == PROG_PRE && w->n == PROG[PROG_PRE - 1].n, "the last preset, written to: its copy, User 1");
        ch.prog = PROG_PRE + 2;
        check(edit_prog() == &uprog[2] && cur_prog() == &uprog[2], "a User one, its own");
        uprog[0].n = 0;
        ch.prog = (uint8_t)p;
    }
    {
        seq_mode = MO_AEOL; seq_key = 9;
        ui.val[0][PG_CHORD][1][1] = choice_val(MO_AEOL, MO_COUNT);
        sp_sync();
        mc.scale_n = SCALE_N;
        ui.roll_view[1] = (uint8_t)degree_of(60);
        ui.val[0][PG_CHORD][1][1] = choice_val(MO_MPENT, MO_COUNT);
        sp_sync();
        scale_view();
        const int at = note_of(ui.roll_view[1]);
        check(at >= 54 && at <= 66, "a pentatonic: the roll's view stays on its notes");
        ui.val[0][PG_CHORD][1][1] = choice_val(MO_AEOL, MO_COUNT);
        sp_sync();
        scale_view();
    }
    {
        int moved = 1;
        for (int p = 0; p < 20; p++) moved &= !strcmp(PROGS[prog_moved(choice_val(p, 20)) * PROG_N / 128], p < 16 ? PROG[p].name : uprog[p - 16].name);
        check(moved, "the twenty of before, each to its own place in the longer list");
    }
    int back = 1;
    const int ns[3] = {MO_COUNT, PROG_N, KT_COUNT};
    for (int l = 0; l < 3; l++) for (int i = 0; i < ns[l]; i++) back &= choice_val(i, ns[l]) * ns[l] / 128 == i;
    check(back, "each long list's places round to themselves");
    seq_key = (uint8_t)key; seq_mode = (uint8_t)mode; ch.voicing = (uint8_t)vo;
}

/* ---------------------------------------------------------------------------- the file of before */

static void old_file(const char *dir, const char *file)
{
    unlink(in(dir, SV_FILE));
    copy(file, in(dir, SV_OLD));
    void *i = open_at(dir);
    check(i != NULL, "a store of before opens");
    check(!strcmp(PROGS[ch.prog], "Night drive"), "its progression the same one");
    check(!strcmp(MODENAMES[seq_mode], "Mixolydian"), "its scale");
    check(!strcmp(KTYPES[ch.ktype], "Sus4"), "its keys' type");
    check(sq[0].count == 23, "its notes");
    check(!strcmp(PROGS[scn[1].prog * PROG_N / 128], "User 2"), "a scene's progression, a User one");
    check(!strcmp(song_name(0), song_nm[0]) && song_nm[0][0], "its name");
    v2_destroy(i);
    check(access(in(dir, SV_FILE), F_OK) != 0, "nothing written until a save");
}

static void show(const char *file, const char *dir)
{
    unlink(in(dir, SV_FILE));
    copy(file, in(dir, SV_OLD));
    void *i = open_at(dir);
    for (int k = 0; k < ST_SONGS; k++) {
        if (!song_used(k)) continue;
        printf("  slot %d: \"%s\"\n", k + 1, song_nm[k]);
    }
    printf("  open: %s, %s %s, keys %s, %d BPM\n", PROGS[ch.prog], KEYS[seq_key], MODENAMES[seq_mode], KTYPES[ch.ktype], seq_bpm);
    for (int s = 0; s < DMK_SYNTHS; s++) {
        printf("  synth %d, %d notes in its open pattern, %d steps; patterns:", s + 1, sq[s].count, sq[s].steps);
        for (int p = 0; p < SYN_PATS; p++) if (spat[s][p].used) printf(" %d:%d", p + 1, spat[s][p].count);
        puts("");
    }
    v2_destroy(i);
}

/* ------------------------------------------------------------------------------------- every pattern full */

static pseq_t ref[DMK_SYNTHS][SYN_PATS];
static void fill_all(void)
{
    for (int s = 0; s < DMK_SYNTHS; s++)
        for (int p = 0; p < SYN_PATS; p++) {
            pseq_t *q = &spat[s][p];
            q->used = 1; q->steps = 256; q->count = 0;
            for (int t = 0; t < 256; t++)
                for (int j = 0; j < 8; j++) {
                    snote_t *n = &q->n[q->count++];
                    n->step = (uint8_t)t; n->len = (uint8_t)(1 + (t + j) % 8); n->note = (uint8_t)(36 + (t + j * 7) % 60);
                    n->vel = (uint8_t)(40 + (t * 3 + j * 11 + s + p) % 80); n->off = (uint8_t)(t % 32);
                }
        }
    for (int s = 0; s < DMK_SYNTHS; s++) spat_load(s, syn_pat(s));
    memcpy(ref, spat, sizeof ref);
}
static int as_filled(void)
{
    for (int s = 0; s < DMK_SYNTHS; s++)
        for (int p = 0; p < SYN_PATS; p++)
            if (spat[s][p].count != SEQ_NOTES || spat[s][p].steps != 256 || memcmp(spat[s][p].n, ref[s][p].n, sizeof ref[s][p].n)) return 0;
    return 1;
}
#define TIMED(label, most, ...) do { double top_ = 0; for (int r_ = 0; r_ < 5; r_++) { const double a_ = t_us(); __VA_ARGS__; const double b_ = t_us() - a_; if (b_ > top_) top_ = b_; } printf("  %-44s %8.0f us\n", label, top_); most = top_; } while (0)

static void full(const char *dir)
{
    static int16_t out[256];
    static mv_view_t vw;
    double t;
    unlink(in(dir, SV_FILE)); unlink(in(dir, SV_OLD));
    void *i = open_at(dir);
    {   /* a first save and Undo mark, as the audio thread makes them: no new memory touched there (faults counted, this thread's) */
        struct rusage a, b;
        for (int k = 0; k < 200; k++) v2_render(i, out, 128);
        getrusage(RUSAGE_THREAD, &a);
        un_take(&un.s[UN_N]);
        const int ok = song_save(0) == 0;
        for (int k = 0; k < 400 && !atomic_load(&sv.done); k++) { v2_render(i, out, 128); usleep(2000); }
        getrusage(RUSAGE_THREAD, &b);
        printf("  page faults in a first save: %ld\n", b.ru_minflt - a.ru_minflt);
        check(ok && atomic_load(&sv.done) >= 1, "a first save");
        check(b.ru_minflt - a.ru_minflt < 16, "it touches no new memory on the audio thread");
    }
    fill_all();
    check(sq[0].count == SEQ_NOTES, "a pattern takes 2048 notes");
    check(seq_add(0, 3, 1, 127, 100) < 0, "the next is refused");
    {
        const char *was = "";
        roll_full();
        was = ui.note;
        check(strstr(was, "2048") != NULL, "and said, with the number");
    }
    TIMED("a whole project saved (song_save)", t, check(song_save(0) == 0 && 1, "saved"));
    check(as_filled(), "saving leaves the patterns as they were");
    TIMED("changes looked for (song_changed)", t, song_changed());
    check(!song_changed(), "saved: nothing changed");
    for (int s = 0; s < DMK_SYNTHS; s++) for (int p = 0; p < SYN_PATS; p++) spat[s][p].count = 0;
    check(song_load(0) == 0, "opened again");
    check(as_filled(), "every note of every pattern back");
    TIMED("a project opened (song_load)", t, song_load(0));
    TIMED("a pattern switched, all three", t, for (int s = 0; s < DMK_SYNTHS; s++) { spat_keep(s); spat_load(s, syn_pat(s)); });
    TIMED("an Undo mark", t, un_take(&un.s[0]));
    TIMED("an Undo", t, un_put(&un.s[0]));
    for (int s = 0; s < DMK_SYNTHS; s++) memset(sq[s].n, 0, sizeof sq[s].n);
    un_put(&un.s[0]);
    {
        int same = 1;
        for (int s = 0; s < DMK_SYNTHS; s++) same &= sq[s].count == SEQ_NOTES && !memcmp(sq[s].n, ref[s][syn_pat(s)].n, sizeof sq[s].n);
        check(same, "Undo puts every note back");
    }
    TIMED("the page published", t, view_publish());
    for (int k = 0; k < 400 && !atomic_load(&sv.done); k++) { v2_render(i, out, 128); usleep(2000); }
    check(atomic_load(&sv.done) >= 1, "the file written, off the audio thread");
    {
        FILE *f = fopen(in(dir, SV_FILE), "rb");
        long n = -1;
        if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
        check(n == ST_SIZE, "at its size");
    }
    /* a block, as Schwung runs it, the patterns all full and sounding (a save's file written first: pinned to one core, as
     * on the Move's test, the saver would share this thread's, which the audio thread's never does) */
    for (int k = 0; k < 1000 && (sv.want || atomic_load(&sv.state) != SV_IDLE); k++) { v2_render(i, out, 128); usleep(2000); }
    seq_play(1);
    double sum = 0, most = 0;
    const int blocks = 3000;
    for (int k = 0; k < blocks; k++) {
        const double a = t_us();
        v2_render(i, out, 128);
        const double b = t_us() - a;
        sum += b;
        if (b > most) most = b;
    }
    seq_play(0);
    printf("  %-44s %8.0f us, most %.0f\n", "a block, every step all eight notes", sum / blocks, most);
    check(most < 2902, "no block over its time");
    v2_destroy(i);
    i = open_at(dir);
    check(as_filled(), "closed and reopened from the file: every note");

    /* the roll's view: as full as seven rows of 64 steps, whatever else the pattern holds */
    const int view = ui.roll_view[0];
    sq[0].count = 0;
    for (int t2 = 0; t2 < 64; t2++) for (int r = -6; r < 0; r++) seq_add(0, t2, 1, note_of(view + r), 100);
    for (int t2 = 0; t2 < 64; t2++) for (int r = 0; r < 7; r++) seq_add(0, t2, 1, note_of(view + r), 100);
    ui.synth = 0; ui.roll_zoom[0] = 2; mc.bar[0] = 0;
    memset(&vw, 0, sizeof vw);
    roll_view(&vw);
    check(vw.roll.count == 448, "the roll shows every note in view, none crowded out by those above or below");
    v2_destroy(i);
}

/* ---------------------------------------------------------------------------------------- forty voices */

static double held(int n, int one)
{
    static int16_t b[256];
    dmk_init(7);
    for (int s = 0; s < DMK_SYNTHS; s++) { ui.val[s][PG_AMP][0][0] = 0; ui.val[s][PG_AMP][0][2] = 127; ui.val[s][PG_SOUND][0][2] = 127; ui.val[s][PG_SOUND][0][0] = 21; }
    sp_sync();
    for (int i = 0; i < n; i++) note_on_s(one ? 0 : i % DMK_SYNTHS, 36 + i * 7 % 60);
    for (int k = 0; k < 200; k++) dmk_render(b, 128);
    double sum = 0;
    const int blocks = 700;
    for (int k = 0; k < blocks; k++) { dmk_render(b, 128); for (int i = 0; i < 256; i++) sum += b[i] * (double)b[i]; }
    return 10 * log10(sum / (blocks * 256.0) / (32768.0 * 32768.0) + 1e-12);
}

static void voices(const char *dir)
{
    static int16_t b[256];
    void *i = open_at(dir);
    const double twelve = held(12, 1);
    int on = 0;
    for (int v = 0; v < DMK_VOICES; v++) on += vc[v].stage != 0;
    check(on == 12 && poly_g == 32768, "twelve voices: the sum as it was, to the bit");
    const double forty = held(40, 1);
    on = 0;
    for (int v = 0; v < DMK_VOICES; v++) on += vc[v].stage != 0;
    printf("  the loudest recording on a synth, RMS: 12 notes %.1f dB, 40 notes %.1f dB, the sum at %.0f%%\n", twelve, forty, poly_g * 100.0 / 32768);
    check(on == 40, "forty sound at once");
    check(poly_g < 20000 && poly_g > 16000, "forty: the sum held by their power, to twelve's");
    check(forty - twelve < 1.0, "forty no louder than twelve");
    held(40, 0);
    for (int v = 0; v < DMK_VOICES; v++) if (vc[v].stage) note_off_src(vc[v].sy, vc[v].note, SRC_KEY);
    for (int k = 0; k < 3000 && poly_g != 32768; k++) dmk_render(b, 128);
    check(poly_g == 32768, "and back to exactly as it was when they end");
    dmk_init(7);
    sq[0].count = 0;
    for (int j = 0; j < 16; j++) seq_add(0, 0, 8, 40 + j, 100);
    ch.follow[0] = FW_FREE;
    pat_mute = 0;
    seq_play(1);
    for (int k = 0; k < 20; k++) dmk_render(b, 128);
    on = 0;
    for (int v = 0; v < DMK_VOICES; v++) on += vc[v].on && vc[v].sy == 0 && vc[v].src == SRC_ROLL;
    seq_play(0);
    check(on == 16, "a roll sounds sixteen at once");
    /* the first song: what the voices it can keep now do to it */
    dmk_init(7);
    seq_play(1);
    int most = 0;
    uint32_t low = 32768;
    for (int k = 0; k < 30 * 44100 / 128; k++) {
        dmk_render(b, 128);
        on = 0;
        for (int v = 0; v < DMK_VOICES; v++) on += vc[v].stage != 0;
        if (on > most) most = on;
        if (poly_g < low) low = poly_g;
    }
    seq_play(0);
    printf("  the first song, 30 s: %d voices at most, the sum at %.0f%% at its lowest\n", most, low * 100.0 / 32768);
    check(low > 26000, "the first song stays as it was, the sum within 2 dB");
    v2_destroy(i);
}

/* ------------------------------------------------------------------------- Phase 1's critical findings */

static void *open_saving(const char *dir)
{
    char js[700];
    snprintf(js, sizeof js, "{\"seed\": 7, \"data_dir\": \"%s\", \"save_on_close\": 1, \"draw_thread\": 0}", dir);
    return v2_create("", js);
}

static void criticals(const char *dir)
{
    static mv_view_t vw;
    void *i = open_at(dir);

    /* 11ths and 13ths: six tones and the end into each chord's buffer (ASan's build reads it too) */
    {
        int six = 1;
        const int kt = ch.ktype, mode = seq_mode, key = seq_key;
        for (int m = 0; m < MO_COUNT; m++)
            for (int t = KT_11; t <= KT_13; t++) {
                seq_mode = (uint8_t)m; seq_key = 9; ch.ktype = (uint8_t)t;
                for (int n = 53; n < 80; n++) {
                    uint8_t notes[8];
                    const int c = chord_key(n, 0, notes);
                    six &= c >= 3 && c <= 7;
                    for (int j = 1; j < c; j++) six &= notes[j] > notes[j - 1];
                }
            }
        seq_mode = MO_AEOL; ch.ktype = KT_11;
        uint8_t notes[8];
        uint16_t pcs = 0;
        const int c = chord_key(60, 0, notes);
        for (int j = 0; j < c; j++) pcs |= (uint16_t)(1u << notes[j] % 12);
        check(six && pcs == (1u << 0 | 1u << 2 | 1u << 4 | 1u << 5 | 1u << 7 | 1u << 11),
              "11ths and 13ths on every key, in every scale: whole chords (A Aeolian, 11ths, C: C D E F G B)");
        ch.ktype = (uint8_t)kt; seq_mode = (uint8_t)mode; seq_key = (uint8_t)key;
    }

    /* Follows the root, in a scale of other than seven notes: the roll moves to the chord's root's note, not another */
    {
        const int mode = seq_mode, key = seq_key, prog = ch.prog, f = ch.follow[2];
        int same = 1;
        seq_mode = MO_MPENT; seq_key = 9; ch.prog = 0; ch.follow[2] = FW_ROOT; ch_cmask = 0;
        for (int idx = 0; idx < SCALE_N; idx++) {
            ch_root = (uint16_t)(48 + 9 + MODES[seq_mode][idx]);
            const int got = follow_note(2, 57), want = note_of(degree_of(57) + idx);
            same &= (got % 12) == (want % 12);
        }
        check(same, "Follows the root in a pentatonic: each chord's root moves the roll to that root's note");
        seq_mode = (uint8_t)mode; seq_key = (uint8_t)key; ch.prog = (uint8_t)prog; ch.follow[2] = (uint8_t)f;
    }

    /* a held step's notes: as many as fit in the view's line, the notice beside it untouched */
    {
        ui.synth = 0;
        sq[0].count = 0;
        seq_add(0, 120, 1, 61, 100); seq_add(0, 120, 1, 64, 100); seq_add(0, 120, 1, 68, 100);
        memset(&vw, 0, sizeof vw);
        memcpy(vw.note, "Hello", 6);
        mc.sdown[0] = 1; mc.sstep[0] = 120;
        held_view(&vw);
        mc.sdown[0] = 0;
        check(!strcmp(vw.note, "Hello") && memchr(vw.sound, 0, sizeof vw.sound) && !strncmp(vw.sound, "Step 121 C#4 E4", 15),
              "a held step's notes fit its line (\"Step 121 C#4 E4\"), the notice beside it as it was");
    }

    /* a scene copied: never onto the open one, twice over one in use, as the FM-1 */
    {
        scn_cur = 0; scn[0].used = 1;
        for (int s = 0; s < DMK_SYNTHS; s++) scn[0].pat[s] = 0;
        scn[1] = scn[0]; scn[1].pat[0] = 1;
        scn[2] = scn[0]; scn[2].pat[0] = 2;
        mc.copy = 1; mc.csrc = CS_SCENE; mc.cstep = 1;
        scene_key(0);
        const int kept = scn[0].pat[0] == 0 && !strcmp(ui.note, "Another scene");
        scene_key(2);
        const int asked = scn[2].pat[0] == 2;
        scene_key(2);
        const int over = scn[2].pat[0] == 1;
        mc.copy = 0; mc.csrc = CS_NONE;
        check(kept && asked && over, "Copy, scene onto scene: not onto the open one; over one in use, the second press");
    }

    /* the tests' dump of a full roll stays in its buffer */
    {
        static char buf[8192 + 64];
        sq[ui.synth].count = 0;
        for (int t = 0; t < 256; t++) for (int j = 0; j < 8; j++) seq_add(ui.synth, t, 8, 30 + j * 11, 127);
        memset(buf + 8192, 0x5A, 64);
        const int n = dump_string(buf, 8192);
        int canary = 1;
        for (int k = 8192; k < 8192 + 64; k++) canary &= buf[k] == 0x5A;
        check(n < 8192 && canary, "a full roll's dump stays in its 8 KiB");
    }
    v2_destroy(i);

    /* closed on Save's Sounds row: the project kept, no sound added */
    {
        unlink(in(dir, SV_FILE)); unlink(in(dir, SV_OLD));
        i = open_saving(dir);
        const int libs = lib_count();
        seq_add(0, 9, 1, 71, 100);
        const int notes = sq[0].count;
        mc.page = MP_SAVE; ui.row[PG_DISCS] = 1;
        v2_destroy(i);
        i = open_at(dir);
        const int libs2 = lib_count();
        check(sq[0].count == notes && seq_find(0, 9, 71) >= 0 && libs2 == libs,
              "closed on Save's Sounds row: the project saved, no sound added");
        v2_destroy(i);
    }
}

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[1], "show")) { show(argv[2], argv[3]); return 0; }
    if (argc < 2) return 2;
    void *i = open_at(argv[1]);
    scales();
    chords();
    progressions();
    v2_destroy(i);
    if (argc > 2) old_file(argv[1], argv[2]);
    full(argv[1]);
    voices(argv[1]);
    criticals(argv[1]);
    printf("  %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
