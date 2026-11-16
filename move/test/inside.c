/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move from inside dsp.so's own code, for what the plugin API would take hours to reach: every pattern
 * full, saved and opened; the file of before the slots grew; every scale, chord and progression. Built as dsp.so is
 * (move/build.sh, static, so the Move can run it too: its times are the ones to read).
 *   inside DIR [OLD_STORE [V16_STORE [V17_STORE]]]   DIR is the tests' own: its store files are removed
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
static void clear_store(const char *dir)               /* the store's file and every one before it */
{
    unlink(in(dir, SV_FILE));
    for (unsigned k = 0; k < sizeof SV_BEFORE / sizeof SV_BEFORE[0]; k++) unlink(in(dir, SV_BEFORE[k].name));
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
    for (size_t n; (n = fread(b, 1, sizeof b, a)) > 0;) fwrite(b, 1, n, o);   /* (a store is MBs: all of it) */
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
    clear_store(dir);
    copy(file, in(dir, "demake.dmks"));
    void *i = open_at(dir);
    check(i != NULL, "a store of before opens");
    check(!strcmp(PROGS[ch.prog], "Night drive"), "its progression the same one");
    check(!strcmp(MODENAMES[seq_mode], "Mixolydian"), "its scale");
    check(!strcmp(KTYPES[ch.ktype], "Sus4"), "its keys' type");
    check(sq[0].count == 23, "its notes");
    check(!strcmp(PROGS[scn[1].prog * PROG_N / 128], "User 2"), "a scene's progression, a User one");
    check(!strcmp(song_name(0), song_nm[0]) && song_nm[0][0], "its name");
    check(ui.val[3][PG_SOUND][0][0] == smp_named("Scirpus") && ch.follow[3] == FW_KEY && sq[3].count == 0 && !(pat_mute >> 3 & 1),
          "its fourth synth new: Scirpus, following the key, no notes");
    v2_destroy(i);
    check(access(in(dir, SV_FILE), F_OK) != 0, "nothing written until a save");
}

static void show(const char *file, const char *dir)
{
    clear_store(dir);
    copy(file, in(dir, "demake.dmks"));
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
    clear_store(dir);
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
    for (int k = 0; k < 5000 && (sv.want || atomic_load(&sv.state) != SV_IDLE); k++) { v2_render(i, out, 128); usleep(2000); }
    if (sv.want || atomic_load(&sv.state) != SV_IDLE) puts("  (the file still being written: the times below share the core with it)");
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
#ifndef __SANITIZE_ADDRESS__                            /* (under ASan a block's time says nothing: the plain run holds it) */
    check(most < 2902, "no block over its time");
#endif
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
    /* the first song itself (no store: with one, dmk_init opens the newest song saved): what the voices it can keep do */
    dmk_store_read = 0; dmk_store_write = 0;
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
    check(low == 32768 && most > 12, "the first song keeps tails twelve voices would take, the sum never held down");
    dmk_store_read = rs_read; dmk_store_write = rs_write;
    v2_destroy(i);
}

/* ---------------------------------------------------------------------------------- four synths (Phase 2) */

static void four(const char *dir, const char *v16)
{
    /* a store of format 16 (three synths, 256 KiB a song: move/test/store_v16.c built on the commit before) */
    if (v16) {
        clear_store(dir);
        copy(v16, in(dir, "projects.dmks"));
        void *i = open_at(dir);
        int notes = 1;
        for (int s = 0; s < 3; s++) {
            notes &= sq[s].count == 40;
            for (int t = 0; t < 40; t++) notes &= seq_find(s, t, 40 + s * 12 + t % 12) >= 0;
        }
        check(notes, "a store of format 16 (three synths, 256 KiB a song): every note of its three back");
        check(ui.val[3][PG_SOUND][0][0] == smp_named("Scirpus") && sq[3].count == 0 && ch.follow[3] == FW_KEY,
              "and its fourth synth new");
        v2_destroy(i);
        check(access(in(dir, SV_FILE), F_OK) != 0, "nothing written until a save");
    }
    void *i = open_at(dir);
    /* Home: a synth a knob; the width and the Space's return on the Pan row; the tempo Shift + knob 1's alone */
    check(vp(PG_HOME, 0, 3) == &ui.val[3][PG_AMP][0][4] && vp(PG_HOME, 1, 3) == &ui.val[3][PG_FX][3][0]
          && vp(PG_HOME, 2, 3) == &ui.val[3][PG_FX][3][2] && vp(PG_HOME, 3, 3) == &ui.val[0][PG_HOME][3][3],
          "Home: knob 4 synth 4's level, send and pan; row 4 the mix's width");
    {
        const kref_t w = MP[MP_HOME].k[1][4], r = MP[MP_HOME].k[1][5];
        mc.page = MP_HOME; mc.row[MP_HOME] = 0; mc.shift = 0;
        const int notempo = !is_tempo(3);
        mc.shift = 1;
        const int shifted = is_tempo(0);
        mc.shift = 0;
        check(w.pg == PG_HOME && w.row == 3 && w.k == 3 && r.pg == PG_GLOBAL && r.row == 1 && r.k == 0 && notempo && shifted,
              "the Pan row's knobs 5 and 6 the width and the return; the tempo only on Shift + knob 1");
    }
    /* Harmony of 4: synth 1 in harmony with synth 4's notes */
    {
        const int f = ch.follow[0];
        ch.follow[0] = FW_HARM4;
        check(harm_of(0) == 3 && !strcmp(FOLLOWS[FW_HARM4], "Harmony of 4"), "Harmony of 4: synth 4 the lead");
        ch.follow[0] = (uint8_t)f;
    }
    /* four synths saved and opened (format 17), mutes and a scene's patterns included */
    {
        clear_store(dir);
        v2_destroy(i);
        i = open_at(dir);
        sq[3].count = 0;
        seq_add(3, 2, 4, 36, 100);
        ui.val[3][PG_AMP][0][4] = 77;
        pat_mute = 8;
        check(song_save(0) == 0, "four synths saved");
        sq[3].count = 0; ui.val[3][PG_AMP][0][4] = 0; pat_mute = 0;
        check(song_load(0) == 0 && seq_find(3, 2, 36) >= 0 && ui.val[3][PG_AMP][0][4] == 77 && (pat_mute & 8) && st_ver >= 17u,
              "and opened: synth 4's notes, level and mute (format 17 and on)");
    }
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
        clear_store(dir);
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

/* ------------------------------------------------------------------------- Phase 3: each pattern its sound */

static int psi(int s, int page, int row, int k)        /* where a value is in a pattern's sound */
{
    for (int i = 0; i < PSND_VALS; i++) if (psnd_val(s, i) == &ui.val[s][page][row][k]) return i;
    return -1;
}

static void psounds(const char *dir, const char *v17)
{
    static int16_t out[256];
    enum { S = 1 };
    const int cut = psi(S, PG_FILTER, 0, 0), lvl = psi(S, PG_AMP, 0, 4), pan = psi(S, PG_FX, 3, 2);
    const int A = smp_named("Scirpus"), B = (A + 5) % DMK_SAMPLES_N;
    check(cut >= 0 && lvl >= 0 && pan >= 0 && psi(S, PG_CHORD, 0, 3) >= 0 && psi(S, PG_ROLL, 2, 4) >= 0
          && psi(S, PG_GLOBAL, 1, 0) < 0 && psi(S, PG_CHORD, 0, 4) < 0 && psi(S, PG_ROLL, 1, 4) < 0,
          "a pattern's sound: its pages, the mixer's three, Strum and the Arp; never Global, the progression or the song's Vary");
    check(psnd_glides(cut) && psnd_glides(lvl) && !psnd_glides(psi(S, PG_SOUND, 0, 0)) && !psnd_glides(psi(S, PG_SOUND, 0, 3))
          && !psnd_glides(psi(S, PG_FILTER, 0, 4)) && !psnd_glides(psi(S, PG_AMP, 0, 0)),
          "what glides: the cutoff and the level; never the recording, tune, the filter's type or the attack");
    /* a store of format 17 (four synths, the sound the song's: move/test/store_v17.c on the commit before) */
    if (v17) {
        clear_store(dir);
        copy(v17, in(dir, SV_FILE));
        void *j = open_at(dir);
        check(st_ver == 17u && psnd[S][0].set && psnd[S][2].set && psnd[S][0].v[cut] == 33 && psnd[S][2].v[cut] == 33
              && psnd[S][2].v[lvl] == 99, "a song of format 17: each pattern kept takes the song's sound");
        check(psnd[S][3].set && psnd[S][3].follow == FW_FREE && !psnd[S][1].set,
              "an empty one kept follows nothing; one never kept takes the sound when it is picked");
        check(ch.follow[0] == FW_PLAYS && sq[0].count == 0, "the open one follows as it was saved (synth 1 on the chords, its roll empty)");
        pat_pick_syn(S, 2);
        check(ui.val[S][PG_FILTER][0][0] == 33 && ui.val[S][PG_AMP][0][4] == 99 && seq_find(S, 5, 55) >= 0,
              "its pattern 3 picked: its notes, the same sound");
        check(ch.prog == PROG_PRE + 1 && scn[1].used && scn[1].prog * PROG_N / 128 == 44,
              "and its progressions where they were: User 2, Bell tower (the list eleven longer, format 19)");
        v2_destroy(j);
    }
    clear_store(dir);
    void *i = open_at(dir);
    seq_play(0);
    syn_pat_set(S, 0);
    sq[S].count = 0; seq_add(S, 0, 4, 48, 100);
    ui.val[S][PG_FILTER][0][0] = 20; ui.val[S][PG_AMP][0][4] = 40; ui.val[S][PG_FX][3][2] = 10;
    ui.val[S][PG_SOUND][0][0] = (uint8_t)A; sp_sync();
    const uint8_t key = ui.val[0][PG_CHORD][1][0], tempo = ui.val[0][PG_HOME][0][4], glo = ui.val[0][PG_GLOBAL][1][0];
    const int prog = ch.prog, f0 = ch.follow[S];
    /* stopped: a switch is at once. An empty pattern never played: the sound playing, following nothing */
    pat_pick_syn(S, 1);
    check(syn_pat(S) == 1 && ui.val[S][PG_FILTER][0][0] == 20 && ui.val[S][PG_SOUND][0][0] == A && ch.follow[S] == FW_FREE,
          "an empty pattern picked: the sound playing, following nothing (silent)");
    seq_add(S, 0, 4, 55, 100);
    ui.val[S][PG_FILTER][0][0] = 120; ui.val[S][PG_AMP][0][4] = 120; ui.val[S][PG_FX][3][2] = 120;
    ui.val[S][PG_SOUND][0][0] = (uint8_t)B; sp_sync();
    psnd_follow(S, FW_ROOT);
    pat_pick_syn(S, 0);
    check(ui.val[S][PG_FILTER][0][0] == 20 && ui.val[S][PG_AMP][0][4] == 40 && ui.val[S][PG_FX][3][2] == 10
          && ui.val[S][PG_SOUND][0][0] == A && syn[S].smp == A && syn[S].cut == 40 && ch.follow[S] == f0,
          "pattern 1 again: its own cutoff, level, pan and recording, and how it follows");
    pat_pick_syn(S, 1);
    check(ui.val[S][PG_FILTER][0][0] == 120 && ui.val[S][PG_AMP][0][4] == 120 && ui.val[S][PG_SOUND][0][0] == B
          && ch.follow[S] == FW_ROOT, "pattern 2 again: its own");
    check(ui.val[0][PG_CHORD][1][0] == key && ui.val[0][PG_HOME][0][4] == tempo && ch.prog == prog
          && ui.val[0][PG_GLOBAL][1][0] == glo, "the key, the tempo, the progression and Global stay the song's");
    /* playing: at the bar, the kinds at once and the rest on its way */
    pat_pick_syn(S, 0);
    seq_play(1);
    pat_pick_syn(S, 1);
    check(spat_next[S] == 1 && ui.val[S][PG_FILTER][0][0] == 20, "playing: the switch waits for the bar");
    for (int k = 0; k < 4000 && syn_pat(S) != 1; k++) v2_render(i, out, 128);
    const int now_b = ui.val[S][PG_SOUND][0][0] == B && syn[S].smp == B;
    v2_render(i, out, 128); v2_render(i, out, 128);
    const int c1 = ui.val[S][PG_FILTER][0][0], l1 = ui.val[S][PG_AMP][0][4];
    check(syn_pat(S) == 1 && now_b, "at the bar: the recording at once, before the bar's notes start");
    check(c1 > 20 && c1 < 120 && l1 > 40 && l1 < 120, "the cutoff and the level on their way, no jump");
    psnd_keep(S);
    check(psnd[S][1].v[cut] == 120 && psnd[S][1].v[lvl] == 120, "kept on the way: where it goes, not where it is");
    ui.val[S][PG_FILTER][0][0] = 64;
    for (int k = 0; k < 20; k++) v2_render(i, out, 128);
    check(ui.val[S][PG_FILTER][0][0] == 64 && ui.val[S][PG_AMP][0][4] == 120 && ui.val[S][PG_FX][3][2] == 120,
          "a value turned on the way stays the player's; the rest there within 58 ms");
    seq_play(0);
    ui.val[S][PG_FILTER][0][0] = 120;
    /* a pattern copied: its sound with it */
    {
        const int q = spat_dup(S);
        check(q >= 0 && syn_pat(S) == q && psnd[S][q].set && psnd[S][q].v[cut] == 120 && psnd[S][q].follow == FW_ROOT,
              "a pattern copied: its sound and how it follows with it");
        syn_pat_set(S, 1);
    }
    /* format 18: every pattern's sound saved and opened */
    check(song_save(0) == 0, "patterns with sounds of their own saved");
    psnd[S][0].v[cut] = 1; psnd[S][1].v[cut] = 2; psnd[S][0].follow = FW_FREE; ui.val[S][PG_FILTER][0][0] = 3;
    check(song_load(0) == 0 && st_ver >= 18u && psnd[S][0].v[cut] == 20 && psnd[S][1].v[cut] == 120 && psnd[S][0].follow == f0
          && psnd[S][1].follow == FW_ROOT && ui.val[S][PG_FILTER][0][0] == 120 && syn_pat(S) == 1,
          "and opened (format 18 and on): each pattern's sound, how each follows, the open one's playing");
    pat_pick_syn(S, 0);
    check(ui.val[S][PG_FILTER][0][0] == 20 && ui.val[S][PG_AMP][0][4] == 40, "and pattern 1's back on it");
    /* Undo in the block of a switch, before the ring's own check (audit I4) */
    {
        undo_mark(0);
        seq_add(S, 3, 1, 60, 100);
        syn_pat_set(S, 1);
        const int n = sq[S].count;
        undo_do(0);
        check(syn_pat(S) == 1 && sq[S].count == n && seq_find(S, 3, 60) < 0 && seq_find(S, 0, 55) >= 0 && seq_find(S, 0, 48) < 0,
              "Undo in the block of a switch: nothing of the old pattern into the new (audit I4)");
    }
    v2_destroy(i);
}

/* ------------------------------------------------------------------- Phase 5: chord mode */

static int pcs(const uint8_t *t, int n) { int m = 0; for (int i = 0; i < n; i++) m |= 1 << (t[i] % 12); return m; }
static int pcs_of(int root, int q) { int m = 0; for (int i = 0; QI[q][i] >= 0; i++) m |= 1 << ((root + QI[q][i]) % 12); return m; }
static int find_prog(const char *name) { for (int i = 0; i < PROG_N; i++) if (!strcmp(PROGS[i], name)) return i; return -1; }

static void to_page(int i) { if (mc.page != i) mv_page(i); }   /* the Move's page, and the core's with it */

static void chordmode(const char *dir)
{
    clear_store(dir);
    void *i = open_at(dir);
    /* the list: at least 24, every preset a style, each style four or more; the brief's own progressions there */
    {
        uint8_t list[PROG_N];
        int every = 1, each = 1, total = 0;
        for (int p = 0; p < PROG_PRE; p++) every &= PROG_STYLE[p] < PS_USER && PROGS[p] && !strcmp(PROGS[p], PROG[p].name);
        for (int st = 0; st < PS_USER; st++) { const int n = style_progs(st, list); each &= n >= 4; total += n; }
        check(PROG_PRE == 56 && sizeof PROGS / sizeof PROGS[0] == PROG_N && every && each && total == PROG_PRE && total >= 24
              && style_progs(PS_USER, list) == 4 && list[0] == PROG_PRE,
              "56 progressions in six styles, four or more each, the four User ones their own");
        const prog_t *v = &PROG[find_prog("Velvet chair")], *a = &PROG[find_prog("Andalusia")], *h = &PROG[find_prog("Home again")];
        const prog_t *t = &PROG[find_prog("Old tale")], *d = &PROG[find_prog("Hill road")], *b = &PROG[find_prog("Basement")];
        check(v->s[0].root == 2 && v->s[0].q == Q_M9 && v->s[1].root == 7 && v->s[1].q == Q_13 && v->s[2].q == Q_MAJ9
              && a->s[1].root == 10 && a->s[2].root == 8 && a->s[3].root == 7 && a->s[3].q == Q_MAJ
              && h->s[0].root == 2 && h->s[1].root == 7 && h->s[2].root == 0 && t->s[1].root == 8 && t->s[3].root == 10
              && d->mode == MO_DOR && d->s[1].q == Q_7 && b->s[0].root == 0 && b->s[1].root == 0
              && PROG_STYLE[find_prog("Tower beacon")] == PS_MODAL && PROG[find_prog("Tower beacon")].mode == MO_PHRY,
              "the brief's: ii9 V13 Imaj9, the Andalusian fall, ii V I, i VI III VII, Dorian i7 IV7, a dub vamp, Phrygian");
    }
    /* outside chord mode: row 2 the chords in the key, rows 3 and 4 the piano, as before */
    to_page(MP_HOME); mc.shift = 0;
    for (int s = 0; s < DMK_SYNTHS; s++) ch.keys &= (uint8_t)~(1u << s);
    check(!chord_mode(), "Home, Keys off: not chord mode");
    /* the Chord page: row 2 the style's eight, Shift + row 2 the styles */
    to_page(MP_CHORD);
    check(chord_mode() && ui.page == PG_CHORD, "the Chord page: chord mode");
    {
        uint8_t list[PROG_N];
        mc.shift = 1; mv_pad(CM_ROW2 + PS_CLASSIC, 100, 1); mv_pad(CM_ROW2 + PS_CLASSIC, 0, 0); mc.shift = 0;
        const int n = style_progs(PS_CLASSIC, list);
        mv_pad(CM_ROW2 + 3, 100, 1); mv_pad(CM_ROW2 + 3, 0, 0);
        check(mc.style == PS_CLASSIC && n > 3 && ch.prog == list[3] && !mc.pkind[CM_ROW2 + 3],
              "Shift + row 2's fifth: Classic; then its fourth pad: that progression, nothing sounding");
        mc.shift = 1; mv_pad(CM_ROW2 + PS_LOFI, 100, 1); mv_pad(CM_ROW2 + PS_LOFI, 0, 0);
        const int nl = style_progs(PS_LOFI, list);
        mv_pad(CM_ROW2 + CM_MORE, 100, 1); mv_pad(CM_ROW2 + CM_MORE, 0, 0); mc.shift = 0;
        mv_pad(CM_ROW2 + 1, 100, 1); mv_pad(CM_ROW2 + 1, 0, 0);
        check(nl > 9 && mc.spage == 1 && ch.prog == list[9], "Lo-fi's next eight (Shift + row 2's last): its tenth on pad 2");
    }
    /* rows 3 and 4: Four winds (I V vi IV) in C major */
    prog_to(find_prog("Four winds"));
    ui.val[0][PG_CHORD][1][0] = choice_val(0, 12); sp_sync();
    {
        cpad_t pads[CM_PADS];
        uint8_t t[8], u[8];
        const int n = cm_pads(pads);
        int order = 1, nodup = 1;
        for (int c = 0; c < 4; c++) order &= pads[c].root == PROG[ch.prog].s[c].root && pads[c].q == PROG[ch.prog].s[c].q && !pads[c].next;
        for (int c = 4; c < n; c++) for (int k = 0; k < c; k++) nodup &= !(pads[c].root == pads[k].root && pads[c].q == pads[k].q);
        check(n == CM_PADS && order && nodup && pads[4].next, "row 3: the progression in order, then four that can come next, none twice");
        mv_pad(CM_PADS + 1, 100, 1);
        const int m = ui.key_nch[CM_PADS + 1];
        memcpy(t, ui.key_ch[CM_PADS + 1], (size_t)m);
        mv_pad(CM_PADS + 1, 0, 0);
        check(m >= 3 && pcs(t, m) == pcs_of(7, Q_MAJ), "row 3's second pad: the V, G B D");
        mv_pad(1, 100, 1);
        const int m2 = ui.key_nch[1];
        memcpy(u, ui.key_ch[1], (size_t)m2);
        mv_pad(1, 0, 0);
        check(m2 >= 3 && (pcs(u, m2) >> 7 & 1) && (pcs(u, m2) != pcs(t, m) || u[0] % 12 != t[0] % 12),
              "row 4's second pad: the V another way (richer, or inverted), its root still in it");
        static mv_view_t vw;
        mv_leds(&vw);
        const uint8_t *lp = vw.led.pad;
        check(lp[CM_PADS + 0] == L_BLUE && lp[CM_PADS + 1] == L_PINK && lp[CM_PADS + 2] == L_BLUE && lp[CM_PADS + 3] == L_ROSE
              && lp[0] == L_DBLUE && lp[1] == L_DPINK,
              "by function: I and vi blue, V pink, IV rose; row 4 the same dimmer");
        int borrowed = 0;
        for (int c = 4; c < n; c++) if (!(pc_scale() >> ((seq_key + pads[c].root) % 12) & 1u)) borrowed |= lp[CM_PADS + c] == L_VIOLET;
        check(borrowed, "a borrowed chord (off the scale) violet");
        /* the key moved: every chord with it */
        ui.val[0][PG_CHORD][1][0] = choice_val(2, 12); sp_sync();
        mv_pad(CM_PADS + 1, 100, 1);
        const int m3 = ui.key_nch[CM_PADS + 1];
        const int moved = pcs(ui.key_ch[CM_PADS + 1], m3) == pcs_of(9, Q_MAJ);
        mv_pad(CM_PADS + 1, 0, 0);
        check(moved, "the key to D: row 3's V is A major");
        ui.val[0][PG_CHORD][1][0] = choice_val(0, 12); sp_sync();
    }
    /* Home again (ii7 V7 Imaj7) holds V7, the first that can come next in a major key: not shown twice */
    {
        cpad_t pads[CM_PADS];
        const int was = ch.prog;
        prog_to(find_prog("Home again"));
        const int n = cm_pads(pads);
        int twice = 0;
        for (int c = 0; c < n; c++) for (int k = 0; k < c; k++) twice += pads[c].root == pads[k].root && pads[c].q == pads[k].q;
        check(n == CM_PADS && !twice && pads[1].root == 7 && pads[1].q == Q_7, "Home again: its V7 once, what comes next after it");
        prog_to(was);
    }
    /* stopped, REC lit, on the Chord page: a pad's chord written into a User progression */
    {
        const int was = ch.prog;
        seq_play(0); seq_rec = 1; ch_cur = 0;
        mv_pad(CM_PADS + 1, 100, 1); mv_pad(CM_PADS + 1, 0, 0);
        seq_rec = 0;
        const prog_t *u = cur_prog();
        check(ch.prog >= PROG_PRE && u->n >= 1 && u->s[0].root == 7 && u->s[0].q == Q_MAJ, "stopped, REC: the pad's chord written (V) into a User one");
        prog_to(was);
    }
    /* a step held + a chord-mode pad: that chord on it */
    {
        const int s = vsyn();
        sq[s].count = 0;
        mc.sdown[2] = 1; mc.sstep[2] = 2;
        mv_pad(CM_PADS + 3, 100, 1); mv_pad(CM_PADS + 3, 0, 0);
        mc.sdown[2] = 0;
        int m = 0;
        for (int k = 0; k < sq[s].count; k++) if (sq[s].n[k].step == 2) m |= 1 << (sq[s].n[k].note % 12);
        check(m == pcs_of(5, Q_MAJ), "a step held, row 3's fourth pad: the IV on it");
    }
    /* Keys on, Home: chord mode too; off again: the piano */
    to_page(MP_HOME);
    ch.keys |= (uint8_t)(1u << vsyn());
    check(chord_mode(), "Keys on (Home): chord mode");
    ch.keys &= (uint8_t)~(1u << vsyn());
    v2_destroy(i);
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
    four(argv[1], argc > 3 ? argv[3] : NULL);
    psounds(argv[1], argc > 4 ? argv[4] : NULL);
    chordmode(argv[1]);
    printf("  %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
