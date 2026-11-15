/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's dsp.so driven as Schwung drives it: dlopen, move_plugin_init_v2, create_instance, then on_midi, set_param,
 * get_param and render_block in 128-frame blocks (Schwung's audio thread: here the main thread, marked so around each
 * of those calls). Built and run in the Move's container by move/proof.sh:
 *   bench DSP.so hash         the first song through the plugin, 10 s, FNV-1a as render_hash.c prints it
 *   bench DSP.so rt           presses, Move's clock and reads with the render: every allocation, file, thread and sleep
 *                             call made on the audio thread counted (none allowed)
 *   bench DSP.so ctl          every control: the pages and each knob on them, the keyboard, the chord row, the steps,
 *                             the scenes, Mute, Copy, Delete, Undo, Capture, Sample, the velocity
 *   bench DSP.so clock        Play and Move's transport, the tempo asked of Move, Move's clock with jitter at tenths of
 *                             a BPM: Demake's phase against Move's, a take's micro timing kept
 *   bench DSP.so save DIR [OLD]  saving, reopening, closing, a broken file, a store of before
 *   bench DSP.so draw DIR     the screen in a few states, as PBM files
 *   bench DSP.so time [SEC]   each block's render timed, the first song playing, and the screen's cost */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../vendor/schwung/plugin_api_v1.h"
#include "../dmk_move_view.h"

/* ------------------------------------------------------------------- the audio thread's rules, counted */

/* the calls dsp.so makes, through this program (-rdynamic: a dlopened library binds to its symbols first); counted
 * while the calling thread is inside an audio-thread entry point. The allocator over glibc's; the rest forwarded */
static __thread int in_audio;
static volatile unsigned long n_alloc, n_file, n_thread, n_sleep, n_log;
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);
extern void __libc_free(void *);
void *malloc(size_t n) { if (in_audio) n_alloc++; return __libc_malloc(n); }
void *calloc(size_t a, size_t b) { if (in_audio) n_alloc++; return __libc_calloc(a, b); }
void *realloc(void *p, size_t n) { if (in_audio) n_alloc++; return __libc_realloc(p, n); }
void free(void *p) { if (in_audio && p) n_alloc++; __libc_free(p); }
void *memalign(size_t a, size_t n) { if (in_audio) n_alloc++; return __libc_memalign(a, n); }
void *aligned_alloc(size_t a, size_t n) { if (in_audio) n_alloc++; return __libc_memalign(a, n); }
int posix_memalign(void **p, size_t a, size_t n) { if (in_audio) n_alloc++; *p = __libc_memalign(a, n); return *p ? 0 : 12; }

#define REAL(name) static __typeof__(name) *real_##name
REAL(fopen); REAL(fopen64); REAL(fread); REAL(fwrite); REAL(fclose); REAL(fflush); REAL(fsync); REAL(rename);
REAL(unlink); REAL(mkdir); REAL(nanosleep); REAL(pthread_create); REAL(pthread_join); REAL(sched_setaffinity);
REAL(sched_setscheduler);
static void real_init(void)
{
#define R(name) real_##name = (__typeof__(name) *)dlsym(RTLD_NEXT, #name)
    R(fopen); R(fopen64); R(fread); R(fwrite); R(fclose); R(fflush); R(fsync); R(rename); R(unlink); R(mkdir);
    R(nanosleep); R(pthread_create); R(pthread_join); R(sched_setaffinity); R(sched_setscheduler);
#undef R
}
#define FILECALL do { if (in_audio) n_file++; } while (0)
FILE *fopen(const char *p, const char *m) { FILECALL; return real_fopen(p, m); }
FILE *fopen64(const char *p, const char *m) { FILECALL; return real_fopen64(p, m); }
size_t fread(void *b, size_t s, size_t n, FILE *f) { FILECALL; return real_fread(b, s, n, f); }
size_t fwrite(const void *b, size_t s, size_t n, FILE *f) { FILECALL; return real_fwrite(b, s, n, f); }
int fclose(FILE *f) { FILECALL; return real_fclose(f); }
int fflush(FILE *f) { FILECALL; return real_fflush(f); }
int fsync(int fd) { FILECALL; return real_fsync(fd); }
int rename(const char *a, const char *b) { FILECALL; return real_rename(a, b); }
int unlink(const char *p) { FILECALL; return real_unlink(p); }
int mkdir(const char *p, mode_t m) { FILECALL; return real_mkdir(p, m); }
int nanosleep(const struct timespec *a, struct timespec *b) { if (in_audio) n_sleep++; return real_nanosleep(a, b); }
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg)
{
    if (in_audio) n_thread++;
    return real_pthread_create(t, a, f, arg);
}
int pthread_join(pthread_t t, void **r) { if (in_audio) n_thread++; return real_pthread_join(t, r); }
int sched_setaffinity(pid_t p, size_t n, const cpu_set_t *s) { if (in_audio) n_thread++; return real_sched_setaffinity(p, n, s); }
int sched_setscheduler(pid_t p, int pol, const struct sched_param *sp)
{
    if (in_audio) n_thread++;
    return real_sched_setscheduler(p, pol, sp);
}
static unsigned long rules(void) { return n_alloc + n_file + n_thread + n_sleep + n_log; }

/* ------------------------------------------------------------------------------------------ the host */

static void host_log(const char *m) { if (in_audio) n_log++; else fprintf(stderr, "  [log] %s\n", m); }
static float host_bpm = 0.0f;
static float get_bpm(void) { return host_bpm; }
static host_api_v1_t host;              /* as the shim fills it (zeroed, the reserved tail NULL) */
static plugin_api_v2_t *api;
static char dir[512];
static void *inst;
static int16_t blk[256];
#define NOSTORE "{\"seed\": 7, \"data_dir\": \"\"}"

static void *open_js(const char *js)
{
    void *in = api->create_instance(dir, js);   /* (pass "data_dir": "" unless a test wants the store) */
    if (!in) { fprintf(stderr, "create_instance failed\n"); exit(1); }
    return in;
}

/* the audio thread's calls */
static void A_render(void) { in_audio = 1; api->render_block(inst, blk, 128); in_audio = 0; }
static void A_midi(const uint8_t *m, int n, int src) { in_audio = 1; api->on_midi(inst, m, n, src); in_audio = 0; }
static void A_note(int st, int n, int v) { uint8_t m[3] = {(uint8_t)st, (uint8_t)n, (uint8_t)v}; A_midi(m, 3, MOVE_MIDI_SOURCE_INTERNAL); }
static void A_rt(int st) { uint8_t b = (uint8_t)st; A_midi(&b, 1, MOVE_MIDI_SOURCE_EXTERNAL); }
static void A_set(const char *k, const char *v) { in_audio = 1; api->set_param(inst, k, v); in_audio = 0; }
static int A_get(const char *k, char *b, int n) { in_audio = 1; int r = api->get_param(inst, k, b, n); in_audio = 0; return r; }
static void blocks(int n) { while (n-- > 0) A_render(); }
static void btn(int cc, int v) { char s[32]; snprintf(s, sizeof s, "B %d %d", cc, v); A_set("in", s); }
static void tap(int cc) { btn(cc, 127); A_render(); btn(cc, 0); A_render(); }
static void pad(int i, int down) { A_note(down ? 0x90 : 0x80, 68 + i, down ? 100 : 0); }
static void padv(int i, int vel) { A_note(0x90, 68 + i, vel); }
static void pad_tap(int i) { pad(i, 1); A_render(); pad(i, 0); A_render(); }
static void step(int k, int down) { A_note(down ? 0x90 : 0x80, 16 + k, down ? 100 : 0); }
static void step_tap(int k) { step(k, 1); A_render(); step(k, 0); A_render(); }
static void step_hold(int k) { step(k, 1); blocks(120); step(k, 0); A_render(); }
static void knob(int k, int d) { char s[32]; snprintf(s, sizeof s, "K %d %d", k, d); A_set("in", s); A_render(); }
static void jog(int d) { char s[32]; snprintf(s, sizeof s, "J %d", d); A_set("in", s); A_render(); }
static void page(int i) { pad_tap(24 + i); }
static void held(int cc) { btn(cc, 127); A_render(); }
static void lift(int cc) { btn(cc, 0); A_render(); }

static char vbuf[8192];
static const char *view(void) { A_get("view", vbuf, sizeof vbuf); return vbuf; }
static int view_row(void) { const char *p = strstr(view(), " row="); return p ? atoi(p + 5) : -1; }
static int vint(const char *key)
{
    char k[32];
    const char *s = view();
    snprintf(k, sizeof k, "%s=", key);
    size_t n = strlen(k);
    for (const char *p = s; (p = strstr(p, k)); p++)
        if (p == s || p[-1] == ' ') return atoi(p + n);
    return -9999;
}
static void vlist(const char *key, int *o, int n)
{
    char k[32];
    snprintf(k, sizeof k, " %s=", key);
    const char *p = strstr(view(), k);
    if (!p) return;
    p += strlen(k);
    for (int i = 0; i < n; i++) { o[i] = atoi(p); p = strchr(p, ','); if (!p) break; p++; }
}

static const char *vstr(const char *key, char *o, int n)
{
    char k[32];
    snprintf(k, sizeof k, "|%s=", key);
    const char *p = strstr(view(), k);
    o[0] = 0;
    if (!p) return o;
    p += strlen(k);
    int i = 0;
    while (*p && *p != '|' && i < n - 1) o[i++] = *p++;
    o[i] = 0;
    return o;
}
static char rollb[4096];
static const char *roll(void) { A_get("dump", rollb, sizeof rollb); return rollb; }
static int nnotes(int s) { int o[3] = {0, 0, 0}; vlist("notes", o, 3); return o[s]; }
static int vals(int k) { char b[128]; int o[8]; A_get("vals", b, sizeof b); const char *p = b; for (int i = 0; i < 8; i++) { o[i] = atoi(p); p = strchr(p, ','); if (!p) break; p++; } return o[k]; }

static int has_note(const char *r, int st, int note)
{
    for (const char *p = r; p && *p; p = strchr(p, ' '), p = p ? p + 1 : p) {
        int a, b, c;
        if (sscanf(p, "%d.%d.%d", &a, &b, &c) == 3 && a == st && c == note) return 1;
    }
    return 0;
}

static int fails, checks;
static void check_(int ok, const char *what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; checks++; }
#define check(ok, what) check_((ok) != 0, what)
static int done(void) { printf("  %d checks, %d failed\n", checks, fails); return fails != 0; }

/* ---------------------------------------------------------------------------------------------- hash */

/* dsp.so's first song as render_hash.c's first_song plays it: seed 7, no tempo from the host, from the start */
static int do_hash(void)
{
    host.get_bpm = 0;
    inst = open_js(NOSTORE);
    A_set("play", "1");
    uint32_t h = 2166136261u;
    for (int k = 0; k < 10 * 44100 / 128; k++) {
        A_render();
        for (int j = 0; j < 512; j++) h = (h ^ ((uint8_t *)blk)[j]) * 16777619u;
    }
    printf("%-32s %08x\n", "first song", h);
    api->destroy_instance(inst);
    return 0;
}

/* ---------------------------------------------------------------------------------------------- rt */

static int do_rt(void)
{
    char raw[256];
    inst = open_js("{\"seed\": 7, \"data_dir\": \"/tmp/dmk_rt\"}");
    A_get("ui", raw, sizeof raw);
    printf("  get_param(\"ui\") at power-on: \"%s\"\n", raw);
    check(api->create_instance(dir, NULL) == NULL, "a second instance is refused (the core is one)");
    unsigned long r0 = rules();
    unsigned long nb = 0, calls = 0;
    host.get_bpm = get_bpm;
    host_bpm = 96;
    for (int i = 0; i < 60 * 44100 / 128; i++) {
        if (i % 7 == 0) { pad((i / 7) % 32, 1); calls++; }
        if (i % 7 == 3) { pad((i / 7) % 32, 0); calls++; }
        if (i % 53 == 0) { A_note(0x90, 16 + (i / 53) % 16, 100); calls++; }
        if (i % 53 == 5) { A_note(0x80, 16 + (i / 53) % 16, 0); calls++; }
        if (i % 11 == 0) { char s[32]; snprintf(s, sizeof s, "K %d %d", i % 8, (i % 5) - 2); A_set("in", s); calls++; }
        if (i % 97 == 0) { A_set("in", "J 1"); calls++; }
        if (i % 401 == 0) { A_set("in", (i / 401) % 2 ? "B 42 127;B 42 0" : "B 43 127;B 43 0"); calls++; }
        if (i % 613 == 0 || i % 613 == 9) {
            static const int cc[] = {88, 60, 119, 56, 62, 63, 55, 54, 58, 86, 3, 50, 118, 52, 85, 40};
            btn(cc[(i / 613) % (int)(sizeof cc / sizeof cc[0])], i % 613 ? 0 : 127);
            calls++;
        }
        if (i % 12 == 0) { A_get("frame", vbuf, sizeof vbuf); calls++; }
        if (i % 1777 == 0) { btn(49, 127); btn(86, 127); btn(86, 0); btn(49, 0); calls++; }   /* (a save) */
        if (i % 211 == 0) {            /* the tempo: Shift + knob 1, ui.js's writes, a re-sync asked */
            char x[48];
            snprintf(x, sizeof x, "B 49 127;P 0 %d %d;B 49 0", (i / 211) % 2 ? 7 : -7, (i / 211) % 3 == 0);
            A_set("in", x);
            snprintf(x, sizeof x, "W %d %d", (i / 211) % 256, (i / 211) % 5 != 0);
            A_set("in", x);
            calls += 2;
        }
        if (i % 3001 == 0) { A_set("in", "T 1 1"); A_set("in", "T 1 0"); calls++; }
        if (i % 1000 == 0) A_rt(0xFA);
        if (i % 1000 == 700) A_rt(0xFC);
        if (i % 1000 < 700 && i % 7 == 0) A_rt(0xF8);
        if (i % 977 == 0) host_bpm = (float)(60 + (i / 977) % 100);
        A_render();
        nb++;
    }
    for (int i = 0; i < 1000; i++) pad(i % 32, 1);
    for (int i = 0; i < 32; i++) pad(i, 0);
    A_render();
    unsigned long r1 = rules();
    char small[8];
    int sn = A_get("ui", small, sizeof small);
    int unknown = A_get("rui_poll", raw, sizeof raw) == -1 && A_get("state", raw, sizeof raw) == -1;
    check(sn == 7 && small[7] == 0, "get_param truncates to the buffer it is given");
    check(unknown, "unknown keys (rui_poll, state) answer -1");
    printf("  %lu blocks and %lu presses, turns, buttons, reads and Move's clock, then 1000 presses at once:\n"
           "    %lu allocations, %lu file calls, %lu thread calls, %lu sleeps, %lu logs on the audio thread\n",
           nb, calls, n_alloc, n_file, n_thread, n_sleep, n_log);
    check(r1 == r0, "nothing allocated, written, read, slept, threaded or logged on the audio thread");
    api->destroy_instance(inst);
    void *again = open_js("{\"data_dir\": \"\"}");
    check(again != NULL, "after destroy_instance a new instance opens");
    inst = again;
    api->destroy_instance(again);
    return done();
}

/* ---------------------------------------------------------------------------------------------- ctl */

static int loud(int n) { int m = 0; while (n-- > 0) { A_render(); for (int i = 0; i < 256; i++) { int v = blk[i] < 0 ? -blk[i] : blk[i]; if (v > m) m = v; } } return m; }

enum { MP_HOME, MP_ROLL, MP_CHORD, MP_SOUND, MP_FILTAMP, MP_LFO, MP_FX, MP_GLOBAL, MP_SAVE };
static const int ROWS[9] = {3, 2, 2, 2, 1, 1, 3, 3, 2};


/* the jog's patterns' list, and what a step takes: each from the first song */
static int pen_n(void) { const char *p = strstr(view(), " pen="); int n = 1; for (p += 5; *p && *p != ' '; p++) n += *p == ','; return n; }
static void ctl_pens(void)
{
    char t[64], t2[64];
    inst = open_js(NOSTORE);
    blocks(8);
    /* the jog on Sound the sound (above); anywhere else the list */
    vstr("sound", t, sizeof t);
    jog(1);
    vstr("sound", t2, sizeof t2);
    check(!strcmp(t, t2), "the jog on Home leaves the sound");
    check(vint("pic") == PIC_PATS && vint("patsel") == 1 && vint("patnow") == 0, "a turn: the patterns' list, the cursor moved, nothing loaded");
    tap(50);
    check(vint("pic") != PIC_PATS && vint("page") == MP_HOME, "Menu closes the list, and goes nowhere");
    tap(3);
    check(vint("pic") == PIC_PATS && vint("patsel") == 0, "a click opens it at the pattern playing");
    jog(2);
    blocks(1100);
    check(vint("pic") != PIC_PATS && vint("patnow") == 0, "left alone 3 s it closes, nothing loaded");
    const int n0 = nnotes(0);
    int st0[3];
    vlist("steps", st0, 3);
    jog(1); tap(3);
    { int st[3], no[3]; vlist("steps", st, 3); vlist("notes", no, 3);
      check(vint("pic") != PIC_PATS && vint("patnow") == 1 && no[0] + no[1] + no[2] == 0, "stopped, a click: that pattern now, all three synths, empty");
      check(st[0] == 16 && st[1] == 16 && st[2] == 16, "a new pattern: a bar"); }
    jog(-1); tap(3);
    check(vint("patnow") == 0 && nnotes(0) == n0, "and back: its notes as they were");
    { int st[3]; vlist("steps", st, 3); check(st[0] == st0[0] && st[1] == st0[1] && st[2] == st0[2], "its length too"); printf("  steps %d, %d, %d\n", st[0], st[1], st[2]); }
    A_set("play", "1");
    for (int i = 0; i < 2000 && !vint("play"); i++) A_render();
    check(vint("play") == 1, "playing");
    jog(1); tap(3);
    check(vint("pic") == PIC_PATS && vint("patnow") == 0 && vint("patnext") == 1, "playing, a click: at the bar, the list open on it");
    tap(3);
    check(vint("patnow") == 1 && vint("patnext") == 255 && vint("pic") != PIC_PATS, "a second click: now");
    blocks(8);
    check(vint("follows") == 0, "an empty pattern: synth 1 leaves the chords");
    { int on = 0; blocks(3000); for (int i = 0; i < 4000; i++) { A_render(); if (!(i & 7)) on |= vint("lights"); }
      check(on == 0, "an empty pattern: nothing sounds, on any synth"); }
    jog(-1); tap(3);
    { int ok = 0; for (int i = 0; i < 4000 && !ok; i++) { A_render(); ok = vint("patnow") == 0; } check(ok, "and at the bar it plays"); }
    blocks(8);
    check(vint("follows") == 4, "back on pattern 1: synth 1 on the chords again");
    jog(1); tap(3); tap(3);
    blocks(3000);
    check(!(vint("lights") & 1), "(an empty pattern again: silent)");
    jog(-1); tap(3); tap(3);           /* now, mid-chord: the machine strikes nothing of its own here */
    blocks(8);
    check(vint("lights") & 1, "back now: the chord sounding, not silence until the next");
    A_set("play", "0");
    blocks(8);
    check(nnotes(0) == n0, "its notes kept through all of that");
    api->destroy_instance(inst);

    inst = open_js(NOSTORE);
    blocks(8);
    /* an empty pattern, so a step's notes are all it holds */
    jog(1); tap(3);
    int led[32];
    tap(40);
    pad_tap(4);
    check(vint("keys") == 1 && pen_n() >= 3, "track button 4, Keys on: the piano's pad a chord, a step takes it as heard");
    tap(40);
    pad_tap(4);
    vlist("pads", led, 32);
    { int pn[1] = {0}; vlist("pen", pn, 1);
      check(pn[0] >= 60 && pen_n() == 1 && vint("penpad") == 4 && led[4] == 17, "a pad pressed: the pen, its pad blue");
      step_hold(5);
      char at[32]; snprintf(at, sizeof at, "5.1.%d.", pn[0]);
      check(nnotes(0) == 1 && strstr(roll(), at), "a step takes that note"); }
    step_tap(5);
    check(nnotes(0) == 0, "a quick tap takes it away");
    pad_tap(16);
    const int cn = pen_n();
    vlist("pads", led, 32);
    check(cn >= 3 && vint("penpad") == 16 && led[16] == 17, "a chord pad: the pen holds its notes");
    step_tap(5);
    check(nnotes(0) == cn, "a step takes the whole chord");
    check(vint("follows") == 0, "and the synth plays its roll");
    step_tap(5);
    check(nnotes(0) == 0, "a quick tap: all of it goes");
    pad(16, 1); A_render();
    step_tap(5); step_tap(7);
    check(nnotes(0) == 2 * cn, "a chord pad held: each step pressed takes it");
    step_tap(5);
    check(nnotes(0) == cn, "the same step again comes off");
    pad(16, 0); A_render();
    pad(0, 1); A_render(); step_tap(7); pad(0, 0); A_render();
    check(nnotes(0) == 1, "another pad held: it replaces what the step held");
    tap(56);
    check(nnotes(0) == cn, "Undo: the chord back");
    api->destroy_instance(inst);

    inst = open_js(NOSTORE);
    blocks(8);
    tap(42);
    const int n1 = nnotes(1), f1 = vint("follows");
    check(f1 != 0, "synth 2 follows the key");
    int free = 0, stled[16];           /* a step of its figure with no note */
    vlist("led", stled, 16);
    while (free < 15 && stled[free] == 17) free++;
    pad_tap(2);
    step_tap(free);
    check(vint("follows") == 0 && nnotes(1) == n1 + 1, "a step written: what was picked plays");
    tap(56);
    check(vint("follows") == f1 && nnotes(1) == n1, "Undo: its Follow back too");
    page(MP_ROLL);
    knob(2, 1); knob(2, -1);
    step_tap(free);
    check(vint("follows") == f1, "a Follow set by hand stays");
    api->destroy_instance(inst);
}

static int do_ctl(void)
{
    int led[32], trk[4];
    char t[256], t2[256];
    host.get_bpm = 0;
    inst = open_js(NOSTORE);
    blocks(8);
    check(vint("play") == 0 && vint("voices") == 0, "at power-on: stopped, no voice");
    check(loud(40) == 0, "at power-on: silence");
    vlist("pads", led, 32);
    check(led[0] == 123 && led[8] == 119 && led[10] == 0 && led[16] == 109 && led[23] == 109, "the keyboard lit: white keys grey, black near black, gaps dark, the chord row violet");
    check(led[9 + 0] == 119 && led[5] == 26, "the song's key, A, its root magenta");
    check(led[24] == 17 && led[25] != 17 && led[27] != 26 && led[30] != 52, "the top row the pages: Home bright, the others dim");

    /* ---- the keyboard */
    pad(0, 1);
    blocks(2);
    check(vint("voices") >= 1, "the bottom left pad plays a note");
    int m = loud(40);
    printf("  its peak: %d\n", m);
    check(m > 300, "and it sounds");
    vlist("pads", led, 32);
    check(led[0] == 120, "lit white while held");
    pad(0, 0);
    blocks(2);
    vlist("pads", led, 32);
    check(led[0] == 17, "blue when let go: the pad a step takes");
    blocks(4000);
    pad(10, 1); blocks(2);
    check(vint("voices") == 0, "a gap in the black keys plays nothing");
    pad(10, 0);
    A_note(0x90, 68, 100); A_note(0x90, 68, 100); blocks(2);
    A_note(0x80, 68, 0); blocks(2);
    vlist("pads", led, 32);
    check(led[0] == 17, "a note on twice, one note off: no key stays down");
    uint8_t ext[3] = {0x90, 68, 100};
    blocks(4000);
    A_midi(ext, 3, MOVE_MIDI_SOURCE_EXTERNAL); blocks(2);
    check(vint("voices") == 0, "an external note on a pad's number plays nothing");
    tap(55);
    check(vint("oct") == 1, "Up: an octave up");
    tap(54); tap(54);
    check(vint("oct") == -1, "Down twice: an octave down");
    tap(55);
    pad(16, 1); blocks(3);
    check(vint("voices") >= 3, "a chord pad: a chord");
    pad(16, 0); blocks(4000);
    tap(118);
    check(vint("inkey") == 1, "Sample: the keyboard in the key");
    vlist("pads", led, 32);
    check(led[0] == 26 && led[7] == 26 && led[1] == 123 && led[10] != 0, "the root on the first and last, every pad a scale note");
    tap(118);
    check(vint("inkey") == 0, "Sample again: the piano");

    /* ---- the synths */
    tap(42);
    check(vint("syn") == 1, "the second track button: synth 2");
    vlist("trkled", trk, 4);
    check(trk[1] == 120 && trk[0] == 119, "its light on, the others dim");
    tap(41);
    check(vint("syn") == 2, "the third: synth 3");
    tap(43);
    check(vint("syn") == 0, "the first: synth 1");
    int k0 = vint("keys");
    tap(40);
    check(vint("keys") != k0, "the fourth: the Keys of the synth in view");
    tap(40);
    check(vint("keys") == k0, "and off");

    /* ---- the pages: every knob on every row has a name, a value, and moves something */
    page(MP_ROLL);
    for (int pg = 0; pg < 8; pg++) {
        page(pg);
        if (vint("page") != pg) { printf("  FAIL  page %d did not open\n", pg); fails++; continue; }
        for (int r = 0; r < ROWS[pg]; r++) {
            if (vint("row") != r) { printf("  FAIL  page %d row %d: row %d\n", pg, r, vint("row")); fails++; }
            vstr("knobs", t, sizeof t);
            vstr("pname", t2, sizeof t2);
            printf("  %-18s %s\n", t2, t);
            char kk[9] = "";
            { const char *p = strstr(view(), " kk="); if (p) memcpy(kk, p + 4, 8); }
            for (int k = 0; k < 8; k++) {
                if (kk[k] == '3') continue;
                char a[256], b[256];
                vstr("knobs", a, sizeof a);
                const int v0 = vals(k);
                if (kk[k] == '2') { char x[32]; snprintf(x, sizeof x, "P %d 5 0", k); A_set("in", x); A_render(); }
                else knob(k, kk[k] == '1' ? 1 : 6);
                vstr("knobs", b, sizeof b);
                if (!strcmp(a, b) && vals(k) == v0) { knob(k, -6); vstr("knobs", b, sizeof b); }   /* (at its top) */
                if (!strcmp(a, b) && vals(k) == v0 && !(pg == MP_ROLL && r == 0 && k == 5) && !(pg == MP_HOME && r == 2 && (k == 4 || k == 5))) { printf("  FAIL  page %d row %d knob %d turned: nothing moved\n", pg, r, k + 1); fails++; }
            }
            page(pg);
        }
        check(vint("row") == 0, "a page's pad again goes round its rows");
    }
    page(MP_HOME);
    check(vint("page") == 0 && vint("core") == 0, "Home back");

    /* Filter·Amp's two halves reach their own core pages */
    page(MP_FILTAMP);
    const int c0 = vals(0), a0 = vals(4);
    knob(0, 5);
    check(vals(0) == c0 + 5 && vals(4) == a0, "Filter·Amp: knob 1 the cutoff");
    knob(4, 3);
    check(vals(4) == a0 + 3 && vals(0) == c0 + 5, "knob 5 the attack");
    held(49);                          /* Shift + jog: the filter's type */
    vstr("knobs", t, sizeof t);
    jog(1);
    lift(49);
    tap(56); tap(56); tap(56);
    check(vals(0) == c0 && vals(4) == a0, "Undo three times: the type, the attack, the cutoff back");
    held(49); tap(56); tap(56); tap(56); lift(49);
    check(vals(0) == c0 + 5 && vals(4) == a0 + 3, "Shift + Undo three times: redone");
    tap(56); tap(56); tap(56);
    /* Delete + a knob: the value back to its start */
    knob(1, 9);
    held(119);
    { char x[16]; snprintf(x, sizeof x, "T 1 1"); A_set("in", x); A_render(); A_set("in", "T 1 0"); A_render(); }
    lift(119);
    int r0 = vals(1);
    knob(1, 9);
    held(119); A_set("in", "T 1 1"); A_render(); A_set("in", "T 1 0"); A_render(); lift(119);
    check(vals(1) == r0, "Delete + a knob: its value back to its start");
    page(MP_HOME);

    /* a touched knob shows large */
    A_set("in", "T 2 1"); A_render();
    check(vint("focus") == 2, "a knob touched shows large");
    A_set("in", "T 2 0"); blocks(600);
    check(vint("focus") == -1, "and goes when let go");

    /* ---- the jog: the sound, on Sound */
    page(MP_SOUND);
    vstr("sound", t, sizeof t);
    jog(1);
    vstr("sound", t2, sizeof t2);
    check(strcmp(t, t2) != 0, "the jog on Sound: the synth's next sound");
    printf("  %s to %s\n", t, t2);
    jog(-1);
    page(MP_HOME);

    api->destroy_instance(inst);

    /* ---- the steps, from the first song again */
    inst = open_js(NOSTORE);
    blocks(8);
    const int n0 = nnotes(0);
    vlist("led", led, 16);
    check(led[0] == 17 && led[1] == 0 && led[4] == 119, "the steps: a note blue, a beat faint, the rest dark (the pad's chord on step 1)");
    step_hold(5);
    check(nnotes(0) == n0 + 1, "an empty step pressed: a note there");
    step_tap(5);
    step_hold(5);
    step_tap(5);
    check(nnotes(0) == n0, "a quick tap on a lit step: its notes go");
    step_tap(5);
    check(nnotes(0) == n0 + 1, "a quick tap on an empty step places a note, and keeps it");
    step_hold(5);
    check(nnotes(0) == n0 + 1, "a lit step held long keeps its notes");
    held(119); step_tap(5); lift(119);
    check(nnotes(0) == n0, "Delete + a step: its notes go");

    step(6, 1); A_render();
    pad(2, 1); A_render(); pad(2, 0); pad(4, 1); A_render(); pad(4, 0); A_render();
    { const char *r = roll(); check(strstr(r, "6.1.64.") && strstr(r, "6.1.67.") && !strstr(r, "6.1.60."), "a step held, pads: those pitches on it, the placed note gone"); }
    knob(2, 3);
    { const char *r = roll(); check(strstr(r, "6.4.64.") && strstr(r, "6.4.67."), "a step held, knob 3: the length of all its notes"); }
    knob(3, 2);
    { const char *r = roll(); check(strstr(r, "6.4.64.100.2"), "knob 4: its place in the step (nudge)"); }
    knob(0, 1);
    { const char *r = roll(); check(strstr(r, "7.4.64.") && !strstr(r, "6.4.64."), "knob 1: moved a step on"); }
    vstr("sound", t, sizeof t);
    check(!strncmp(t, "Step 8", 6), "the screen says the step and its notes");
    printf("  %s\n", t);
    jog(1);
    { const char *r = roll(); check(!strstr(r, "7.4.64."), "the jog: its pitch, up the scale"); }
    held(49); jog(4); lift(49);
    { const char *r = roll(); check(strstr(r, ".108.2"), "Shift + jog: its velocity"); }
    step(6, 0); A_render();
    for (int i = 0; i < 8; i++) tap(56);
    check(nnotes(0) == n0, "Undo: back through every edit of that step");

    /* Copy */
    step(9, 1); A_render(); pad(0, 1); A_render(); pad(0, 0); pad(2, 1); A_render(); pad(2, 0); step(9, 0); A_render();
    held(60);
    step_tap(9); step_tap(11); step_tap(13);
    lift(60);
    { const char *r = roll(); check(has_note(r, 11, 64) && has_note(r, 13, 60), "Copy held: a step, then others: its notes there"); }
    /* the bars */
    check(vint("bar") == 0 && vint("bars") == 2, "the roll's two bars");
    tap(63);
    check(vint("bar") == 1 && vint("follow") == 0, "Right: the next bar, the playhead no longer followed");
    tap(63);
    check(vint("bar") == 1, "and stops at its last");
    step_tap(2);
    { const char *r = roll(); check(has_note(r, 18, 60) || strstr(r, " 18.") != NULL, "a step on the second bar lands there"); }
    tap(62);
    check(vint("bar") == 0, "Left: back");
    held(62); held(63); lift(62); lift(63);
    check(vint("follow") == 1, "both: the steps follow the playhead");

    /* ---- Mute */
    tap(88);
    check(vint("mutes") == 1, "Mute tapped: the synth in view");
    tap(88);
    held(88); tap(42); step_tap(2);
    vlist("led", led, 16);
    lift(88);
    check(vint("mutes") == 6, "Mute held: track buttons and steps mute a synth each, and no tap");
    check(led[0] == 120 && led[1] == 119, "held, the steps show the mutes");
    held(88); tap(42); step_tap(2); lift(88);
    check(vint("mutes") == 0, "and back");

    /* ---- the scenes */
    held(49);
    vlist("led", led, 16);
    check(led[0] == 120 && led[1] == 0, "Shift: the steps the scenes, the open one white");
    step_tap(3);
    lift(49);
    check(vint("scene") == 3, "Shift + a step: that scene, stopped at once");
    check(nnotes(0) > 0, "an empty scene starts as a copy");
    held(60); held(49); step_tap(3); step_tap(5); lift(49); lift(60);
    held(49); vlist("led", led, 16); lift(49);
    check(led[5] == 17, "Copy, Shift + a step, then another: the scene");
    held(119); held(49); step_tap(5); lift(49); lift(119);
    held(49); vlist("led", led, 16); lift(49);
    check(led[5] == 0, "Delete, Shift + a step: that scene");
    held(49); tap(63); lift(49);
    check(vint("pat") == 1, "Shift + Right: the synth's next pattern");
    held(49); tap(62); lift(49);
    check(vint("pat") == 0, "Shift + Left: back");
    held(60); tap(43); lift(60);
    check(vint("pat") == 2, "Copy + a track button: its pattern into a new one, this scene's");
    held(119); tap(43); lift(119);
    check(nnotes(0) == 0, "Delete + a track button: that synth's roll");
    held(49); step_tap(0); lift(49);
    check(vint("scene") == 0 && nnotes(0) > 0, "back to the first scene, its roll as it was");

    /* the Song row */
    page(MP_HOME); page(MP_HOME);
    check(vint("row") == 2, "Home's third row: Song");
    vlist("led", led, 16);
    check(led[0] == 120, "the steps stay the scenes");
    step_tap(0);
    step_tap(3);
    check(vint("song") == 2, "stopped, a step writes the song's next step of that scene");
    tap(119);
    check(vint("song") == 1, "Delete tapped: the step at its cursor");
    page(MP_HOME);

    /* ---- recording: the pad's velocity */
    A_set("play", "1");
    blocks(2);
    tap(86);
    check(vint("rec") == 1, "Rec");
    padv(1, 33); blocks(80); pad(1, 0); blocks(2);
    { const char *r = roll(); check(strstr(r, ".62.33.") != NULL, "a pad recorded at its velocity"); if (!strstr(r, ".62.33.")) printf("  %s\n", r); }
    A_set("play", "0"); blocks(2);
    A_set("play", "1"); blocks(2);
    padv(1, 44); blocks(80); pad(1, 0); blocks(2);
    { const char *r = roll(); check(strstr(r, ".62.44.") != NULL && strstr(r, ".62.33.") == NULL, "a take over a note: its velocity too"); }
    tap(86);
    A_set("play", "0");
    blocks(2);

    /* ---- Capture */
    tap(42);
    const int n1 = nnotes(1);
    tap(52);
    vstr("note", t, sizeof t);
    printf("  on nothing played on it: \"%s\"\n", t);
    held(119); tap(42); lift(119);
    padv(0, 77); blocks(40); pad(0, 0); blocks(40); padv(4, 90); blocks(60); pad(4, 0); blocks(4);
    check(nnotes(1) == 0, "stopped, no REC: the pads write nothing");
    tap(52);
    check(nnotes(1) == 2, "Capture: what was just played, into the roll");
    { const char *r = roll(); check(strstr(r, ".60.77.") && strstr(r, ".67.90."), "each note at its pitch and velocity"); printf("  %s\n", r); }
    tap(56);
    check(nnotes(1) == 0, "one Undo takes it back");
    tap(56);
    check(nnotes(1) == n1, "and the Delete before it");
    tap(43);
    page(MP_CHORD);
    { const int n2 = nnotes(0); tap(52); check(nnotes(0) != n2, "on Chord, Capture prints the chords into the roll"); }

    /* an event flood */
    for (int i = 0; i < 1000; i++) pad(i % 24, 1);
    for (int i = 0; i < 24; i++) pad(i, 0);
    blocks(2);
    check(vint("dropped") >= 0, "a flood of presses: the queue holds");
    api->destroy_instance(inst);
    ctl_pens();
    return done();
}

/* ---------------------------------------------------------------------------------------------- clock */

/* Move's clock at BPM with its jitter: a pulse seen on the block after it happened, up to JIT blocks later now and then.
 * Demake's place against Move's true one, in pulses, over the last two thirds of the run: its spread (a constant
 * offset is the lag, a block or so) */
static void phase_run(double bpm, double sec, int jit, double *spread, double *mean)
{
    static const int J[8] = {0, 1, 0, 0, 2, 0, 1, 0};
    const double spp = 60.0 * 44100.0 / (24.0 * bpm);
    double next = 0, lo = 1e9, hi = -1e9, s = 0;
    int i = 0, n = 0;
    const int blocks_n = (int)(sec * 44100 / 128);
    A_rt(0xFA);
    for (int b = 0; b < blocks_n; b++) {
        const double t = b * 128.0;
        while (next <= t - (jit ? J[i & 7] : 0) * 128.0) { A_rt(0xF8); next += spp; i++; }
        A_render();
        if (b > blocks_n / 3) {
            const double e = vint("pos") / 1000.0 - (t + 128.0) / spp;
            if (e < lo) lo = e;
            if (e > hi) hi = e;
            s += e;
            n++;
        }
    }
    *spread = hi - lo;
    *mean = s / n;
}

static int do_clock(void)
{
    int led[16];
    /* Move answers Play */
    host.get_bpm = get_bpm;
    host_bpm = 120;
    inst = open_js(NOSTORE);
    blocks(2);
    check(vint("bpm") == 120, "Move's tempo followed before anything plays");
    btn(85, 127); A_render(); btn(85, 0);
    blocks(4);
    A_rt(0xFA);
    A_render();
    check(vint("play") == 1, "Play, then Move's start: Demake plays");
    int on = 0;
    for (int i = 0; i < 400; i++) { if (i % 7 == 0) A_rt(0xF8); A_render(); on |= vint("step") != 0; }
    check(on, "the playhead moves");
    check(vint("sync") == 1 && vint("run") == 1, "on Move's transport");
    vlist("led", led, 16);
    int s = vint("step");
    check(s >= 0 && s < 16 && led[s] == 120, "the step LED shows the playhead");
    A_rt(0xFC);
    A_render();
    check(vint("play") == 0 && vint("step") == -1, "Move's stop: Demake stops");
    tap(85);
    blocks(30);
    check(vint("play") == 0, "a Play seen just after Move's stop is that stop's own press");
    A_rt(0xFA); A_render(); A_rt(0xF8);
    host_bpm = 90.4f;
    blocks(2);
    check(vint("b10") == 904 && vint("bpm") == 90, "Move's tempo changed: followed, tenths and all");
    /* Move gone */
    blocks(44100 / 2 / 128 + 4);
    check(vint("play") == 0 && vint("run") == 0, "no pulse for half a second: Move has gone, Demake stops");
    api->destroy_instance(inst);

    /* Rec, then Play: it waits for the first pad, Move running on */
    host_bpm = 120;
    inst = open_js(NOSTORE);
    blocks(2);
    const int rn0 = nnotes(0);
    tap(86);
    btn(85, 127); A_render(); btn(85, 0);
    blocks(4);
    A_rt(0xFA);
    A_render();
    check(vint("rec") == 1 && vint("ready") == 1 && vint("play") == 0 && vint("run") == 1, "Rec, then Play: ready, nothing plays, Move runs on");
    { /* 918.75 samples a pulse at 120: Move's pulses against the blocks, a pulse = 7.18 blocks */
      const double spp = 44100.0 * 60 / (120 * 24.0) / 128.0;
      double next = 0; int b = 0, n = 0;
      #define RUN_PULSES(to) while (n < (to)) { while (next <= b) { A_rt(0xF8); next += spp; n++; } A_render(); b++; }
      RUN_PULSES(100);
      check(vint("play") == 0 && vint("ready") == 1 && vint("step") == -1, "still waiting, as long as it takes");
      padv(4, 90);
      A_render(); b++;
      check(vint("play") == 1 && vint("ready") == 0 && vint("rec") == 1, "the first pad starts it");
      const int p0 = (n + 3) / 6 * 6;   /* Move's step nearest the pad: Demake's first */
      RUN_PULSES(p0 + 14);
      pad(4, 0);
      A_render(); b++;
      check(nnotes(0) == rn0 + 1 && has_note(roll(), 0, 67), "the pad is on step 1");
      RUN_PULSES(p0 + 6 * 16 * 8 + 9);
      int pos = vint("step");
      check(pos == 0 || pos == 1, "eight bars on: Demake's steps on Move's, from the pad");
      printf("  step %d at Move's pulse %d, its first %d\n", pos, n, p0); }
    A_rt(0xFC);
    A_render();
    check(vint("play") == 0 && vint("ready") == 0, "Move's stop stops it");
    /* Play again: not ready */
    tap(85); blocks(4);
    A_rt(0xFA); A_render();
    check(vint("ready") == 1, "ready again");
    A_rt(0xFC); A_render();
    check(vint("ready") == 0 && vint("play") == 0, "Play again, Move stops: no longer ready");
    tap(86);
    tap(85); blocks(4);
    A_rt(0xFA); A_render();
    check(vint("rec") == 0 && vint("ready") == 0 && vint("play") == 1, "without Rec, Play plays at once");
    api->destroy_instance(inst);

    /* no clock from Move: the same on Demake's own */
    host.get_bpm = 0;
    inst = open_js(NOSTORE);
    blocks(2);
    tap(86);
    tap(85);
    blocks(60);
    check(vint("ready") == 1 && vint("play") == 0, "no clock: ready all the same");
    padv(16, 90);
    A_render();
    check(vint("play") == 1 && vint("ready") == 0, "a chord pad starts it too");
    pad(16, 0); A_render();
    api->destroy_instance(inst);
    host.get_bpm = get_bpm;

    /* the phase */
    double w, m;
    static const double T[4] = {120.0, 92.5, 71.3, 133.37};
    for (int i = 0; i < 4; i++) {
        host_bpm = (float)T[i];
        inst = open_js(NOSTORE);
        blocks(4);
        phase_run(T[i], 60, 1, &w, &m);
        char x[80];
        const double ms = w * 60000.0 / (24.0 * T[i]), off = m * 60000.0 / (24.0 * T[i]);
        printf("  %.2f BPM, 60 s, a block of jitter: against Move's true time, %.2f ms off on average, wandering %.2f ms\n", T[i], off, ms);
        snprintf(x, sizeof x, "held to Move's phase at %.2f BPM", T[i]);
        check(ms < 3.0 && off < 3.0 && off > -3.0, x);
        api->destroy_instance(inst);
    }

    /* a take's micro timing under Move's clock: the note lands between steps, as played */
    host_bpm = 100;
    inst = open_js(NOSTORE);
    blocks(4);
    held(119); tap(43); lift(119);
    {                                  /* (Rec once it plays: before Play it would wait for the pad) */
        const double spp = 60.0 * 44100.0 / (24.0 * 100.0);
        double next = 0;
        A_rt(0xFA);
        for (int b = 0; b < 3000; b++) {
            if (b == 100) { btn(86, 127); btn(86, 0); }
            if (b == 1000) pad(0, 1);
            if (b == 1040) pad(0, 0);
            while (next <= b * 128.0) { A_rt(0xF8); next += spp; }
            A_render();
        }
    }
    {
        const char *r = roll();
        printf("  the take: %s\n", r);
        int st, len, note, vel, off;
        check(sscanf(r, "%d.%d.%d.%d.%d", &st, &len, &note, &vel, &off) == 5 && off > 0, "a take's place inside its step is kept under Move's clock");
    }
    api->destroy_instance(inst);

    /* Move's tempo asked: Shift + knob 1 */
    host_bpm = 120;
    inst = open_js(NOSTORE);
    blocks(4);
    held(49);
    A_set("in", "P 0 15 0"); A_render();
    lift(49);
    {
        int q[3] = {0, 0, 0};
        vlist("tq", q, 3);
        check(q[0] == 1 && q[2] == 1215, "Shift + knob 1: the tempo asked of Move");
        check(vint("b10") == 1215, "Demake at it at once");
        char x[32];
        snprintf(x, sizeof x, "W %d 1", q[1]);
        A_set("in", x);
        host_bpm = 121.5f;
        blocks(4);
        vlist("tq", q, 3);
        check(q[0] == 0 && vint("b10") == 1215, "Move has followed: Move's tempo again");
    }
    held(49);
    A_set("in", "P 0 5 0"); A_render();
    lift(49);
    {
        int q[3] = {0, 0, 0};
        vlist("tq", q, 3);
        char x[32];
        snprintf(x, sizeof x, "W %d 0", q[1]);
        A_set("in", x);
        blocks(4);
        vlist("tq", q, 3);
        check(q[0] == 0 && vint("b10") == 1215, "no file written, stopped: Move keeps its tempo");
    }
    api->destroy_instance(inst);

    /* Move silent */
    host_bpm = 133;
    inst = open_js(NOSTORE);
    blocks(2);
    btn(85, 127); A_render(); btn(85, 0);
    blocks(10);
    check(vint("play") == 0, "Play: Demake waits for Move's answer");
    blocks(20);
    check(vint("play") == 1 && vint("sync") == 0, "no answer in 70 ms: Demake plays alone (INT)");
    check(vint("bpm") == 72 && vint("b10") == 720, "at the song's tempo");
    held(49);
    A_set("in", "P 0 20 0"); A_render();
    lift(49);
    check(vint("bpm") == 74, "alone, the tempo knob the song's own");
    tap(85);
    check(vint("play") == 0, "alone, Play stops it at once");
    tap(85);
    check(vint("play") == 1, "and starts it at once");
    A_rt(0xFA);
    A_render();
    check(vint("sync") == 1 && vint("play") == 1, "Move starts after all: back on Move's transport");
    api->destroy_instance(inst);
    return done();
}

/* ---------------------------------------------------------------------------------------------- save */

static long fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }

#define ST_BYTES 3678208           /* the store's file: settings, the library, 14 songs of 256 KiB */
static int same_file(const char *a, const char *b)
{
    static char x[70000], y[70000];
    FILE *f = fopen(a, "rb"), *g = fopen(b, "rb");
    if (!f || !g) return 0;
    const size_t n = fread(x, 1, sizeof x, f), m = fread(y, 1, sizeof y, g);
    fclose(f); fclose(g);
    return n == m && !memcmp(x, y, n);
}
static int do_save(const char *d, const char *old)
{
    char js[700], file[640], bad[660], was[640];
    char t[256];
    host.get_bpm = 0;
    snprintf(js, sizeof js, "{\"seed\": 7, \"data_dir\": \"%s\", \"save_on_close\": 0}", d);
    snprintf(file, sizeof file, "%s/projects.dmks", d);
    snprintf(bad, sizeof bad, "%s/projects.dmks.bad", d);
    snprintf(was, sizeof was, "%s/demake.dmks", d);
    remove(file); remove(bad); remove(was);
    inst = open_js(js);
    blocks(4);
    step_tap(3);
    held(49); tap(86); lift(49);       /* Shift + Rec */
    vstr("note", t, sizeof t);
    printf("  \"%s\"\n", t);
    check(!strncmp(t, "Saved: ", 7), "Shift + Rec saves the project");
    for (int i = 0; i < 400 && fsize(file) != ST_BYTES; i++) { A_render(); struct timespec ts = {0, 20000000}; nanosleep(&ts, 0); }
    check(fsize(file) == ST_BYTES, "the store's file written, off the audio thread");
    A_set("play", "1"); blocks(10);
    step_tap(7);
    held(49); tap(86); lift(49);
    vstr("note", t, sizeof t);
    check(!strncmp(t, "Saved: ", 7), "and while it plays");
    A_set("play", "0"); blocks(4);
    const int n2 = nnotes(0);
    api->destroy_instance(inst);
    inst = open_js(js);
    blocks(4);
    check(nnotes(0) == n2, "reopened: the project as saved");
    tap(50);
    check(vint("page") == MP_SAVE, "Menu: Save");
    vstr("knobs", t, sizeof t);
    printf("  %s\n", t);
    check(strstr(t, "Project:1") != NULL, "Projects: the one open");
    jog(1);
    vstr("knobs", t, sizeof t);
    check(strstr(t, "Project:2") != NULL, "the jog scrolls");
    jog(-1);
    held(49); jog(1); lift(49);
    vstr("pname", t, sizeof t);
    check(strstr(t, "Sounds") != NULL, "Shift + jog: Sounds");
    held(49); jog(1); lift(49);
    jog(1);
    tap(3);
    vstr("note", t, sizeof t);
    printf("  a click on an empty slot: \"%s\"\n", t);
    tap(3);
    check(vint("page") == MP_HOME && nnotes(0) == 0, "a click twice: a new, empty project; Home");
    tap(50);
    tap(50);
    check(vint("page") == MP_HOME, "Menu again closes");
    tap(50); jog(-1); tap(3); tap(3);
    check(nnotes(0) == n2, "a project opened from the list");
    api->destroy_instance(inst);

    /* closing keeps what was played */
    snprintf(js, sizeof js, "{\"seed\": 7, \"data_dir\": \"%s\"}", d);
    inst = open_js(js);
    blocks(4);
    step_tap(11);
    const int n3 = nnotes(0);
    check(n3 != n2, "an edit");
    api->destroy_instance(inst);
    inst = open_js(js);
    blocks(4);
    check(nnotes(0) == n3, "closed and opened: kept");
    api->destroy_instance(inst);

    /* a broken file is kept aside */
    FILE *f = fopen(file, "wb");
    if (f) { fwrite("broken", 6, 1, f); fclose(f); }
    inst = open_js(js);
    blocks(4);
    check(fsize(bad) == 6, "a broken file kept aside as .bad");
    check(nnotes(0) > 0, "and the first song plays");
    api->destroy_instance(inst);

    /* the file of before the slots grew: opened, and left as it is when the new one is written */
    if (old) {
        static char b[65536];
        FILE *a = fopen(old, "rb"), *o = fopen(was, "wb");
        const size_t n = a ? fread(b, 1, sizeof b, a) : 0;
        if (o) fwrite(b, 1, n, o);
        if (a) fclose(a);
        if (o) fclose(o);
        remove(file);
        inst = open_js(js);
        blocks(4);
        check(nnotes(0) == 23, "a store of before: its project opens");
        step_tap(14);
        const int n4 = nnotes(0);
        check(n4 != 23, "an edit");
        api->destroy_instance(inst);
        check(fsize(file) == ST_BYTES, "closed: saved in the new file");
        check(same_file(old, was), "the old file left as it was");
        inst = open_js(js);
        blocks(4);
        check(nnotes(0) == n4, "and the new one read from then on");
        api->destroy_instance(inst);
    }
    return done();
}

/* ---------------------------------------------------------------------------------------------- draw */

static void pbm(const char *d, const char *name, const char *frame)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s.pbm", d, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P4\n128 64\n");
    for (int y = 0; y < 64; y++) {
        const char *r = frame + 12 + y * 22;
        uint8_t o[16] = {0};
        for (int x = 0; x < 128; x++)
            if ((r[x / 6] - 48) >> (5 - x % 6) & 1) o[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
        fwrite(o, 16, 1, f);
    }
    fclose(f);
}

static char fbf[8192];
/* the pixels lit from row y0 to y1 of the frame last read */
static int lit_rows(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int c = 0; c < 22; c++) { int b = fbf[12 + y * 22 + c] - 48; while (b) { n += b & 1; b >>= 1; } }
    return n;
}
static void snap_scr(const char *d, const char *name) { blocks(30); A_get("frame", fbf, sizeof fbf); pbm(d, name, fbf); }

static int do_draw(const char *d)
{
    host.get_bpm = 0;
    inst = open_js("{\"seed\": 7, \"data_dir\": \"\", \"draw_thread\": 0}");
    blocks(12);
    int n = A_get("frame", fbf, sizeof fbf);
    printf("  a frame: %d characters\n", n);
    check(n == 4 + 8 + 64 * 22 + 2 * 65 + 8 + 2 + 4, "the frame's length");
    check(!strncmp(fbf, "DMK1", 4), "its tag");
    /* every page, as the sheets show them. The roll on synth 3's figure and synth 1's chords, stopped and playing */
    char nm[64];
    page(MP_ROLL);
    tap(41);
    snap_scr(d, "roll_stop");
    A_set("play", "1");
    blocks(470);
    snap_scr(d, "roll_play");
    A_set("play", "0");
    blocks(4);
    tap(43);
    A_set("play", "1");
    blocks(180);
    snap_scr(d, "roll_chords");
    A_set("in", "T 3 1");
    snap_scr(d, "roll_touch");
    A_set("in", "T 3 0");
    blocks(500);
    A_get("frame", fbf, sizeof fbf);
    check(lit_rows(10, 63) > 200, "the Roll: its grid, its notes");
    /* the Chord page with the longest of its names, the knob held, a full list's marks */
    page(MP_CHORD);
    held(49); for (int i = 0; i < 16; i++) jog(1); lift(49);
    knob(4, 1);
    for (int i = 0; i < 5; i++) knob(5, 1);
    A_set("in", "T 5 1"); snap_scr(d, "chord_long_knob");
    {
        char t[256];
        vstr("knobs", t, sizeof t);
        printf("  %s\n", t);
        check(strstr(t, "Scale:Hungarian minor") != NULL, "the Scale knob: a step a turn, the long list");
    }
    A_set("in", "T 5 0");
    blocks(900);
    snap_scr(d, "chord_long");
    page(MP_CHORD);
    knob(1, 12);
    A_set("in", "T 1 1"); snap_scr(d, "chord_keys_knob"); A_set("in", "T 1 0");
    blocks(900);
    snap_scr(d, "chord_keys");
    tap(3);
    /* each page's rows, playing, as they stand */
    static const struct { int pg, rows; const char *name; uint8_t pic[3]; } TOUR[] = {
        {MP_HOME, 3, "home", {PIC_NONE, PIC_PAN, PIC_SONG}}, {MP_ROLL, 2, "roll_row", {PIC_ROLL, PIC_ARP, 0}},
        {MP_CHORD, 2, "chord", {PIC_CHORD, PIC_CHORD, 0}}, {MP_SOUND, 2, "sound", {PIC_SOUND, PIC_SOUND, 0}},
        {MP_FILTAMP, 1, "filter", {PIC_SHAPES, 0, 0}}, {MP_LFO, 1, "lfo", {PIC_LFO, 0, 0}},
        {MP_FX, 3, "fx", {PIC_TAIL, PIC_ECHO, PIC_MOTION}}, {MP_GLOBAL, 3, "global", {PIC_TAIL, PIC_COLOUR, PIC_MEDIA}}};
    for (unsigned i = 0; i < sizeof TOUR / sizeof TOUR[0]; i++) {
        page(TOUR[i].pg);
        for (int r = 0; r < TOUR[i].rows; r++) {
            if (r) page(TOUR[i].pg);
            snprintf(nm, sizeof nm, "%s%d", TOUR[i].name, r);
            snap_scr(d, nm);
            char say[80];
            snprintf(say, sizeof say, "%s, row %d: its picture, on the stage", TOUR[i].name, r + 1);
            check(vint("pic") == TOUR[i].pic[r] && lit_rows(10, 52) > 40, say);
        }
    }
    A_set("play", "0");
    blocks(600);
    page(MP_ROLL);
    while (view_row() != 0) page(MP_ROLL);
    step(2, 1); blocks(120); snap_scr(d, "held"); step(2, 0); A_render();
    tap(50);
    blocks(600);
    snap_scr(d, "save0");
    check(vint("pic") == PIC_PROJECTS && lit_rows(10, 52) > 40, "Save: the projects");
    tap(50);
    page(MP_HOME);
    /* each effect's types: knob 1 turns them */
    static const struct { int pg, row, n; const char *name; } FXT[] = {
        {MP_ROLL, 1, 14, "arp"}, {MP_FX, 0, 5, "tail"}, {MP_FX, 1, 5, "echo"}, {MP_FX, 2, 8, "mot"}, {MP_GLOBAL, 0, 9, "space"}, {MP_GLOBAL, 1, 7, "col"}, {MP_GLOBAL, 2, 7, "med"}};
    for (unsigned i = 0; i < sizeof FXT / sizeof FXT[0]; i++) {
        page(FXT[i].pg);
        while (view_row() != FXT[i].row) page(FXT[i].pg);
        A_set("in", "K 0 -40"); blocks(8);
        for (int t = 0; t < FXT[i].n; t++) {
            if (t) { A_set("in", "K 0 1"); blocks(8); }
            A_set("in", "T 0 0");
            blocks(600);
            snprintf(nm, sizeof nm, "%s%d", FXT[i].name, t);
            snap_scr(d, nm);
        }
    }
    page(MP_HOME); while (view_row() != 0) page(MP_HOME);
    blocks(400);
    A_set("play", "1");
    int moved = 0;
    for (int s = 0; s < 13; s++) {
        blocks(120);
        snprintf(nm, sizeof nm, "land%02d", s); snap_scr(d, nm);
        char was[8192];
        memcpy(was, fbf, sizeof was);
        blocks(190);
        snprintf(nm, sizeof nm, "land%02db", s); snap_scr(d, nm);
        moved += memcmp(was + 12, fbf + 12, 64 * 22) != 0;
        page(MP_SOUND); jog(1); page(MP_HOME);
    }
    A_set("play", "0");
    check(moved >= 10, "Home moves on its own");
    jog(1);
    snap_scr(d, "pats");
    check(vint("pic") == PIC_PATS && lit_rows(10, 63) > 40, "the patterns' list");
    tap(50);
    A_set("in", "T 0 1"); snap_scr(d, "home_touch"); A_set("in", "T 0 0");
    api->destroy_instance(inst);
    /* the drawer's thread: frames arrive */
    inst = open_js(NOSTORE);
    struct timespec ts = {0, 2902000};
    A_set("play", "1");
    for (int i = 0; i < 200; i++) { A_render(); nanosleep(&ts, 0); }
    A_get("frame", fbf, sizeof fbf);
    char c[128];
    A_get("cost", c, sizeof c);
    printf("  publishes, mean ns, most, draws, mean ns, most, reads: %s\n", c);
    check(strtoul(fbf + 4, 0, 16) > 1, "the drawer's frames arrive");
    api->destroy_instance(inst);
    return done();
}

/* ---------------------------------------------------------------------------------------------- time */

static int cmpu(const void *a, const void *b) { const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static int do_time(double sec)
{
    const int n = (int)(sec * 44100 / 128);
    uint32_t *t = malloc((size_t)n * sizeof *t);
    host.get_bpm = 0;
    inst = open_js(NOSTORE);
    A_set("play", "1");
    double sum = 0;
    for (int k = 0; k < n; k++) {
        struct timespec a, c;
        clock_gettime(CLOCK_MONOTONIC, &a);
        A_render();
        clock_gettime(CLOCK_MONOTONIC, &c);
        t[k] = (uint32_t)((c.tv_sec - a.tv_sec) * 1000000000LL + (c.tv_nsec - a.tv_nsec));
        sum += t[k];
    }
    qsort(t, (size_t)n, sizeof *t, cmpu);
    printf("  the first song: mean %.0f us, p99 %.0f us, most %.0f us a block\n", sum / n / 1000, t[n * 99 / 100] / 1000.0, t[n - 1] / 1000.0);
    api->destroy_instance(inst);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "bench DSP.so hash|rt|ctl|clock|save DIR|draw DIR|time [SEC]\n"); return 2; }
    real_init();
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    if (!init) { fprintf(stderr, "no %s\n", MOVE_PLUGIN_INIT_V2_SYMBOL); return 1; }
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    host.log = host_log;
    host.get_bpm = get_bpm;
    api = init(&host);
    if (!api || api->api_version != MOVE_PLUGIN_API_VERSION_2) { fprintf(stderr, "not a v2 plugin\n"); return 1; }
    snprintf(dir, sizeof dir, "%s", argv[1]);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    if (!strcmp(argv[2], "hash")) return do_hash();
    if (!strcmp(argv[2], "rt")) return do_rt();
    if (!strcmp(argv[2], "ctl")) return do_ctl();
    if (!strcmp(argv[2], "clock")) return do_clock();
    if (!strcmp(argv[2], "save") && argc > 3) return do_save(argv[3], argc > 4 ? argv[4] : NULL);
    if (!strcmp(argv[2], "draw") && argc > 3) return do_draw(argv[3]);
    if (!strcmp(argv[2], "time")) return do_time(argc > 3 ? atof(argv[3]) : 20.0);
    fprintf(stderr, "bench DSP.so hash|rt|ctl|clock|save DIR|draw DIR|time [SEC]\n");
    return 2;
}
