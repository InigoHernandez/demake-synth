/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the firmware's core, unchanged, as the DSP of a Schwung overtake module (plugin API v2).
 * move/build.sh compiles it to dsp.so for the Move (aarch64 Linux, glibc 2.35).
 *
 * Schwung runs every entry point but create and destroy on its audio thread (core 3, SCHED_FIFO 70, a block of 128
 * frames every 2.9 ms), so nothing on that path allocates, locks, logs, formats with libc or touches a file. create and
 * destroy run on Schwung's loader thread: the store is read there, and written there on closing. Two threads of our
 * own, made in create and joined in destroy, demoted to SCHED_OTHER on cores 0-2 (Schwung's rule):
 *   the drawer   the 128 x 64 screen from the page state the audio thread publishes, into a triple buffer
 *                get_param("frame") reads;
 *   the saver    the store's file from a snapshot the audio thread copies a piece a block (a temp file, then a rename).
 * In: the pads and the steps to on_midi; Move's clock and transport to on_midi too (once a block); the knobs, the jog,
 * the buttons and the knobs' touch from ui.js, through set_param("in"). All of it goes into one queue the next render
 * drains, in order, into the controller (dmk_move_ui.c). Out: get_param("frame"), the screen and the LEDs, once a frame
 * (ui.js reads one every two of the panel's frames, ~28.7 a second).
 * The queue, the clock, the block, the controller and the screen are dmk_move_glue.c's; this file is Schwung's side of
 * it. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "../firmware/src/dmk_core.c"
#include "vendor/schwung/plugin_api_v1.h"

#define EXPORT __attribute__((visibility("default")))

/* what the Move sends: the 16 steps and the 32 pads (bottom left to top right, rows of 8), notes on cable 0 */
#define NOTE_STEP1 16
#define NOTE_PAD1  68
#define PADS       32

static const host_api_v1_t *host;
static float mv_host_bpm(void) { return host && host->get_bpm ? host->get_bpm() : 0.0f; }

#include "dmk_move_glue.c"   /* the queue, the clock, the block, the controller, the screen */

/* ------------------------------------------------------------------------------------------------ the store */

/* the store (dmk_store.c) as the FM-1's, its song slots grown so that every pattern can be full, here in RAM, loaded at
 * create. A record written goes to RAM (the core's save takes well under a block here, so it may play on) and asks for
 * a save: the audio thread copies the RAM into a snapshot a piece a block, the saver's thread writes that as a temp
 * file and renames it over the last */
#define ST_SIZE (ST_SONG0 + ST_SONGS * ST_SLOT)
#define SV_FILE "projects.dmks"
#define SV_OLD "demake.dmks"           /* the file before the slots grew: read until a save, never written */
#define SV_OLD_SIZE 65536u
#define SV_PIECE 65536u
enum { SV_IDLE, SV_COPY, SV_READY };
static uint8_t st_ram[ST_SIZE], sv_snap[ST_SIZE];
static struct {
    _Atomic int state;
    uint32_t at;
    uint8_t want;
    _Atomic uint32_t done, failed;     /* the saver's counts */
    uint32_t seen_failed;              /* the audio thread's */
} sv;
static int th_save_on;

static void mv_save_request(void) { sv.want = 1; }

static int rs_read(uint32_t at, void *p, uint32_t n)
{
    if (at + n > ST_SIZE) return -1;
    memcpy(p, st_ram + at, n);
    return 0;
}
static int rs_write(uint32_t at, const void *p, uint32_t n)
{
    const uint32_t room = at >= ST_SONG0 ? ST_SLOT : ST_SECTOR;
    if (at & 4095u || n > room || at + room > ST_SIZE) return -1;
    memset(st_ram + at, 0xFF, room);
    memcpy(st_ram + at, p, n);
    sv.want = 1;
    return 0;
}

/* each block, at the end of the render: a piece of the snapshot, or a new one begun */
static void sv_step(void)
{
    const int st = atomic_load_explicit(&sv.state, memory_order_acquire);
    if (st == SV_IDLE) {
        if (!sv.want || !th_save_on) return;
        sv.want = 0;
        sv.at = 0;
        atomic_store_explicit(&sv.state, SV_COPY, memory_order_relaxed);
    } else if (st != SV_COPY) return;
    const uint32_t n = ST_SIZE - sv.at < SV_PIECE ? ST_SIZE - sv.at : SV_PIECE;
    memcpy(sv_snap + sv.at, st_ram + sv.at, n);
    sv.at += n;
    if (sv.at >= ST_SIZE) atomic_store_explicit(&sv.state, SV_READY, memory_order_release);
}

static char *p_cat(char *d, const char *s, const char *end) { while (*s && d < end) *d++ = *s++; *d = 0; return d; }
static void sv_path(char *o, int n, const char *name)
{
    char *e = p_cat(o, mv.dir, o + n - 1);
    e = p_cat(e, "/", o + n - 1);
    p_cat(e, name, o + n - 1);
}

/* the bytes as the store's file: a temp file, then a rename */
static int sv_write(const uint8_t *b)
{
    char tmp[256], path[256];
    sv_path(tmp, sizeof tmp, SV_FILE ".tmp");
    sv_path(path, sizeof path, SV_FILE);
    mkdir(mv.dir, 0755);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fwrite(b, ST_SIZE, 1, f) == 1 && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    else unlink(tmp);
    return ok;
}

/* the file before the slots grew: what it holds, each song in its slot (the records as they were, read by their version) */
static void store_old(void)
{
    static uint8_t old[SV_OLD_SIZE];
    char path[256];
    sv_path(path, sizeof path, SV_OLD);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    const size_t n = fread(old, 1, SV_OLD_SIZE, f);
    fclose(f);
    if (n != SV_OLD_SIZE) return;
    memcpy(st_ram, old, ST_SONG0);
    for (uint32_t i = 0; i < ST_SONGS; i++) memcpy(st_ram + slot_at((int)i), old + ST_SONG0 + i * ST_SECTOR, ST_SECTOR);
}

/* the store from its file, at create (before any render). One of another size is kept aside as .bad */
static void store_load(void)
{
    char path[256], bad[256];
    memset(st_ram, 0xFF, ST_SIZE);
    sv_path(path, sizeof path, SV_FILE);
    FILE *f = fopen(path, "rb");
    if (!f) { store_old(); return; }
    const size_t n = fread(st_ram, 1, ST_SIZE, f);
    fclose(f);
    if (n == ST_SIZE) return;
    memset(st_ram, 0xFF, ST_SIZE);
    sv_path(bad, sizeof bad, SV_FILE ".bad");
    rename(path, bad);
}

/* ------------------------------------------------------------------------------------- the screen, the thread */

/* the page state: built on the audio thread once a panel frame (6 blocks, 17.4 ms: the OLED takes a slice a block),
 * and 3 blocks after a press at the soonest, published by a sequence lock the drawer reads without ever making the
 * audio thread wait */
#define VW_EVERY 6                     /* blocks between publishes, nothing pressed */
#define VW_SOON 3                      /* after a press, at the soonest */
static mv_view_t vw_pub, vw_now;
static _Atomic uint32_t vw_lock;
static uint32_t vw_count, vw_last;     /* publishes; the block of the last */

/* what the screen costs, for the bench (get_param "cost"): the publish on the audio thread, the drawing on the
 * drawer's, the reads; ns */
static struct { uint64_t pub_ns, pub_max, draw_ns, draw_max; uint32_t pub_n, draw_n, reads; } cost;
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   /* (the vDSO's: no system call) */
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void view_publish(void)
{
    const uint64_t t0 = now_ns();
    mv_view_build(&vw_now);
    vw_now.seq = ++vw_count;
    atomic_fetch_add_explicit(&vw_lock, 1u, memory_order_acq_rel);     /* (odd: being written) */
    atomic_thread_fence(memory_order_release);
    memcpy(&vw_pub, &vw_now, sizeof vw_pub);
    atomic_fetch_add_explicit(&vw_lock, 1u, memory_order_release);
    const uint64_t d = now_ns() - t0;
    cost.pub_ns += d;
    if (d > cost.pub_max) cost.pub_max = d;
    cost.pub_n++;
}

static int view_read(mv_view_t *v)
{
    for (int tries = 0; tries < 16; tries++) {
        const uint32_t s0 = atomic_load_explicit(&vw_lock, memory_order_acquire);
        if (s0 & 1u) continue;
        memcpy(v, &vw_pub, sizeof *v);
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&vw_lock, memory_order_relaxed) == s0) return 1;
    }
    return 0;
}

/* the frames: a triple buffer. The drawer draws into its own, then trades it for the middle one (marked new); a read
 * takes the middle one when it is new. Neither waits */
static uint8_t fbuf[3][MS_BYTES];
static uint32_t fseq[3];
static _Atomic uint32_t fb_mid = 1;
static int fb_w, fb_r = 2;             /* the drawer's, the reader's */
static uint32_t fb_count;

static void frame_draw(const mv_view_t *v)
{
    const uint64_t t0 = now_ns();
    ms_draw(v, fbuf[fb_w]);
    const uint64_t d = now_ns() - t0;
    cost.draw_ns += d;                 /* (the drawer's own, or the render's without one: one writer either way) */
    if (d > cost.draw_max) cost.draw_max = d;
    cost.draw_n++;
    fseq[fb_w] = ++fb_count;
    fb_w = (int)(atomic_exchange_explicit(&fb_mid, (uint32_t)fb_w | 4u, memory_order_acq_rel) & 3u);
}

static pthread_t th_draw, th_save;
static _Atomic int th_stop;
static int th_draw_on;

static void nap_ms(int ms)
{
    struct timespec ts = {0, ms * 1000000L};
    nanosleep(&ts, 0);
}

/* a thread of ours off the audio's: SCHED_OTHER first (a thread inherits its maker's FIFO), cores 0-2 */
static void thread_demote(void)
{
#ifdef __linux__
    struct sched_param sp;
    memset(&sp, 0, sizeof sp);
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t s;
    CPU_ZERO(&s);
    for (int c = 0; c < 3; c++) CPU_SET(c, &s);
    sched_setaffinity(0, sizeof s, &s);
#endif
}

/* the drawer: a frame for each page state published whose screen changed, drawn from its copy alone */
static void *draw_main(void *arg)
{
    (void)arg;
    thread_demote();
    static mv_view_t v, last;
    uint32_t seen = 0;
    int have = 0;
    while (!atomic_load_explicit(&th_stop, memory_order_acquire)) {
        if (view_read(&v) && v.seq != seen) {
            seen = v.seq;
            v.seq = 0;                 /* (the number apart, the same page state draws the same) */
            if (!have || memcmp(&v, &last, sizeof last)) { frame_draw(&v); last = v; have = 1; }
        }
        nap_ms(4);
    }
    return 0;
}

static void *save_main(void *arg)
{
    (void)arg;
    thread_demote();
    for (;;) {
        if (atomic_load_explicit(&sv.state, memory_order_acquire) == SV_READY) {
            if (sv_write(sv_snap)) atomic_fetch_add(&sv.done, 1u);
            else atomic_fetch_add(&sv.failed, 1u);
            atomic_store_explicit(&sv.state, SV_IDLE, memory_order_release);
            continue;
        }
        if (atomic_load_explicit(&th_stop, memory_order_acquire)) break;
        nap_ms(20);
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------ what ui.js reads */

static char *ap(char *e, const char *s) { while (*s) *e++ = *s++; *e = 0; return e; }
static char *ap_int(char *e, int v)
{
    char t[12];
    int n = 0;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    if (v < 0) *e++ = '-';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *e++ = t[--n];
    *e = 0;
    return e;
}
static const char HEX[] = "0123456789ABCDEF";
static char *ap_hex8(char *e, uint32_t v) { for (int sh = 28; sh >= 0; sh -= 4) *e++ = HEX[v >> sh & 15]; *e = 0; return e; }

/* "DMK1", the frame's number (8 hex), its 64 rows of 22 characters (6 pixels each from '0', MSB first, the last
 * character's low two bits past the edge), then the LEDs as two hex digits each: 16 steps, 32 pads, 4 track buttons
 * and MV_NBTN buttons (MV_BTN_CC's order); then how each knob turns ('0' + MV_KK_*, 8), the tempo asked of Move: its
 * number (2 hex, 00 never) and the tempo (4 hex, tenths of BPM) */
#define FRAME_LEN (4 + 8 + 64 * 22 + 2 * (16 + 32 + 4 + MV_NBTN) + 8 + 2 + 4)
static int frame_string(char *o)
{
    if (atomic_load_explicit(&fb_mid, memory_order_acquire) & 4u)
        fb_r = (int)(atomic_exchange_explicit(&fb_mid, (uint32_t)fb_r, memory_order_acq_rel) & 3u);
    cost.reads++;
    const uint8_t *px = fbuf[fb_r];
    char *e = ap(o, "DMK1");
    e = ap_hex8(e, fseq[fb_r]);
    for (int y = 0; y < 64; y++) {
        const uint8_t *r = px + 16 * y;
        uint32_t acc = 0;
        int nb = 0, i = 0;
        for (int c = 0; c < 22; c++) {
            while (nb < 6) { acc = acc << 8 | (i < 16 ? r[i] : 0); i++; nb += 8; }
            *e++ = (char)('0' + (acc >> (nb - 6) & 63));
            nb -= 6;
        }
    }
    const mv_leds_t *l = &vw_now.led;
    const uint8_t *seq[4] = {l->step, l->pad, l->trk, l->btn};
    const int len[4] = {16, 32, 4, MV_NBTN};
    for (int g = 0; g < 4; g++)
        for (int k = 0; k < len[g]; k++) { *e++ = HEX[seq[g][k] >> 4 & 15]; *e++ = HEX[seq[g][k] & 15]; }
    for (int k = 0; k < 8; k++) *e++ = (char)('0' + (vw_now.kkind[k] & 3));
    *e++ = HEX[vw_now.tq_seq >> 4 & 15]; *e++ = HEX[vw_now.tq_seq & 15];
    for (int sh = 12; sh >= 0; sh -= 4) *e++ = HEX[vw_now.tq_b10 >> sh & 15];
    *e = 0;
    return (int)(e - o);
}

/* "PLAY STEP BPM SYNTH CPU_MEAN CPU_MOST", for the tools */
static int ui_string(char *o)
{
    char *e = o;
    e = ap_int(e, seq_on); *e++ = ' ';
    e = ap_int(e, seq_on ? seq_playing_step(ui.synth) : -1); *e++ = ' ';
    e = ap_int(e, (int)mv_bpm10() / 10); *e++ = ' ';
    e = ap_int(e, ui.synth); *e++ = ' ';
    e = ap_int(e, (int)mv.cpu_mean); *e++ = ' ';
    e = ap_int(e, (int)mv.cpu_most);
    return (int)(e - o);
}

static int voices_on(void) { int n = 0; for (int i = 0; i < DMK_VOICES; i++) n += vc[i].on != 0; return n; }

/* the page state as text, for the tests: "KEY=VALUE" pairs, the last strings of each kind joined with '|' */
static int view_string(char *o)
{
    static mv_view_t v;
    mv_view_build(&v);
    char *e = o;
    e = ap(e, "page="); e = ap_int(e, mc.page);
    e = ap(e, " row="); e = ap_int(e, mp_row());
    e = ap(e, " pic="); e = ap_int(e, v.pic.kind);
    e = ap(e, " patsel="); e = ap_int(e, mc.pat_sel);
    e = ap(e, " patnow="); e = ap_int(e, syn_pat(ui.synth));
    e = ap(e, " patnext="); e = ap_int(e, spat_next[ui.synth]);
    e = ap(e, " follows="); e = ap_int(e, ch.follow[ui.synth]);
    e = ap(e, " lights="); e = ap_int(e, v.lights);
    e = ap(e, " ready="); e = ap_int(e, v.ready);
    e = ap(e, " pen="); for (int i = 0; i < mc.pen[ui.synth].n; i++) { if (i) *e++ = ','; e = ap_int(e, mc.pen[ui.synth].note[i]); }
    e = ap(e, " penpad="); e = ap_int(e, mc.pen[ui.synth].pad);
    e = ap(e, " core="); e = ap_int(e, ui.page);
    e = ap(e, " play="); e = ap_int(e, v.playing);
    e = ap(e, " rec="); e = ap_int(e, v.rec);
    e = ap(e, " sync="); e = ap_int(e, v.sync);
    e = ap(e, " run="); e = ap_int(e, ck.run);
    e = ap(e, " syn="); e = ap_int(e, v.synth);
    e = ap(e, " step="); e = ap_int(e, v.step);
    e = ap(e, " bpm="); e = ap_int(e, seq_bpm);
    e = ap(e, " b10="); e = ap_int(e, seq_b10);
    e = ap(e, " oct="); e = ap_int(e, ui.octave);
    e = ap(e, " voices="); e = ap_int(e, voices_on());
    e = ap(e, " dropped="); e = ap_int(e, (int)evq.dropped);
    e = ap(e, " mutes="); e = ap_int(e, v.mutes);
    e = ap(e, " scene="); e = ap_int(e, v.scene);
    e = ap(e, " next="); e = ap_int(e, scn_next);
    e = ap(e, " pat="); e = ap_int(e, v.pat);
    e = ap(e, " bar="); e = ap_int(e, v.bar);
    e = ap(e, " bars="); e = ap_int(e, v.bars);
    e = ap(e, " follow="); e = ap_int(e, mc.follow);
    e = ap(e, " inkey="); e = ap_int(e, v.in_key);
    e = ap(e, " focus="); e = ap_int(e, v.focus);
    e = ap(e, " keys="); e = ap_int(e, ch.keys);
    e = ap(e, " song="); e = ap_int(e, song_n);
    e = ap(e, " undo="); e = ap_int(e, un.at); *e++ = ','; e = ap_int(e, un.n);
    e = ap(e, " notes="); for (int s = 0; s < DMK_SYNTHS; s++) { if (s) *e++ = ','; e = ap_int(e, sq[s].count); }
    e = ap(e, " steps="); for (int s = 0; s < DMK_SYNTHS; s++) { if (s) *e++ = ','; e = ap_int(e, sq[s].steps); }
    e = ap(e, " kk="); for (int k = 0; k < 8; k++) *e++ = (char)('0' + v.kkind[k]);
    *e = 0;
    e = ap(e, " led=");
    for (int k = 0; k < 16; k++) { if (k) *e++ = ','; e = ap_int(e, v.led.step[k]); }
    e = ap(e, " pads=");
    for (int k = 0; k < 32; k++) { if (k) *e++ = ','; e = ap_int(e, v.led.pad[k]); }
    e = ap(e, " trkled=");
    for (int k = 0; k < 4; k++) { if (k) *e++ = ','; e = ap_int(e, v.led.trk[k]); }
    e = ap(e, " btnled=");
    for (int k = 0; k < MV_NBTN; k++) { if (k) *e++ = ','; e = ap_int(e, v.led.btn[k]); }
    e = ap(e, " tq="); e = ap_int(e, tq.state); *e++ = ','; e = ap_int(e, tq.seq); *e++ = ','; e = ap_int(e, tq.b10);
    e = ap(e, " phase="); e = ap_int(e, (int)(ck.steer * 1000));
    e = ap(e, " pos="); e = ap_int(e, seq_ticks ? (int)(((seq_ticks - 1) * 6.0 + 6.0 * seq_acc / seq_per()) * 1000) : 0);
    /* strings last, spaces kept: the bench reads them from their tag to the next '|' */
    e = ap(e, " |pname="); e = ap(e, v.page); *e++ = ' '; e = ap(e, v.row);
    e = ap(e, "|sound="); e = ap(e, v.sound);
    e = ap(e, "|note="); e = ap(e, v.note);
    e = ap(e, "|knobs=");
    for (int k = 0; k < 8; k++) { if (v.knob[k].on) { e = ap(e, v.knob[k].name); *e++ = ':'; e = ap(e, v.knob[k].val); } *e++ = ','; }
    e = ap(e, "|");
    return (int)(e - o);
}

/* the synth in view's roll, for the tests: "step.len.note.vel.off" a note */
static int dump_string(char *o)
{
    const seq_t *q = &sq[ui.synth];
    char *e = o;
    for (int i = 0; i < q->count; i++) {
        if (i) *e++ = ' ';
        e = ap_int(e, q->n[i].step); *e++ = '.';
        e = ap_int(e, q->n[i].len); *e++ = '.';
        e = ap_int(e, q->n[i].note); *e++ = '.';
        e = ap_int(e, q->n[i].vel); *e++ = '.';
        e = ap_int(e, q->n[i].off);
    }
    *e = 0;
    return (int)(e - o);
}

/* the value on knob i of the page in view, for the tests */
static int val_string(char *o)
{
    char *e = o;
    for (int k = 0; k < 8; k++) {
        const kref_t r = mp_knob(k);
        if (k) *e++ = ',';
        e = ap_int(e, r.pg < 0 || r.pg == PG_ARR || r.pg == PG_DISCS || (r.pg == PG_ROLL && !r.row) ? -1 : *vp(r.pg, r.row, r.k));
    }
    return (int)(e - o);
}

/* the screen's cost, for the bench: "PUBLISHES PUB_NS_MEAN PUB_NS_MOST DRAWS DRAW_NS_MEAN DRAW_NS_MOST READS" */
static int cost_string(char *o)
{
    char *e = o;
    e = ap_int(e, (int)cost.pub_n); *e++ = ' ';
    e = ap_int(e, (int)(cost.pub_n ? cost.pub_ns / cost.pub_n : 0)); *e++ = ' ';
    e = ap_int(e, (int)cost.pub_max); *e++ = ' ';
    e = ap_int(e, (int)cost.draw_n); *e++ = ' ';
    e = ap_int(e, (int)(cost.draw_n ? cost.draw_ns / cost.draw_n : 0)); *e++ = ' ';
    e = ap_int(e, (int)cost.draw_max); *e++ = ' ';
    e = ap_int(e, (int)cost.reads);
    return (int)(e - o);
}

/* ------------------------------------------------------------------------------------------- the plugin API */

static int s_is(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* a whole number at the start of s (a param's value), signed; *ok whether there was one */
static int s_snum(const char **ps, int *ok)
{
    const char *s = *ps;
    int v = 0, neg = 0, any = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9' && v < 100000) { v = v * 10 + (*s++ - '0'); any = 1; }
    *ps = s;
    if (ok) *ok = any;
    return neg ? -v : v;
}

/* module.json's "defaults" (Schwung hands the file to create_instance): "seed" fixes the dice (the tests'; else the
 * clock's), "data_dir" where the store lives ("" none), "save_on_close" 0 or 1, "draw_thread" 0 to draw in the render
 * (the bench's), "clock_lag" in samples */
static int json_int(const char *js, const char *key, int dflt)
{
    if (!js) return dflt;
    const char *p = strstr(js, key);
    if (!p) return dflt;
    p += strlen(key);
    while (*p == ' ' || *p == '"' || *p == ':' || *p == '\t') p++;
    int ok;
    int v = s_snum(&p, &ok);
    return ok ? v : dflt;
}

static int json_str(const char *js, const char *key, char *o, int n)
{
    if (!js) return 0;
    const char *p = strstr(js, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p++ != '"') return 0;
    int i = 0;
    while (*p && *p != '"' && i < n - 1) o[i++] = *p++;
    o[i] = 0;
    return 1;
}

/* a page's first write is a fault, tens of microseconds each as measured on the Move, and a first save reaches some
 * 900 of them: every big store the audio thread writes, written once here, off it */
static void pages_touch(void *p, size_t n)
{
    volatile uint8_t *b = p;
    for (size_t i = 0; i < n; i += 4096u) b[i] = b[i];
    b[n - 1] = b[n - 1];
}

static void *v2_create(const char *module_dir, const char *json_defaults)
{
    (void)module_dir;
    if (mv.live) return NULL;           /* (the core is one: a second would share the first's state and break it) */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint32_t clock_seed = (uint32_t)ts.tv_nsec ^ (uint32_t)ts.tv_sec * 2654435761u;
    const int seed = json_int(json_defaults, "\"seed\"", -1);
    mv_begin();
    if (!json_str(json_defaults, "\"data_dir\"", mv.dir, sizeof mv.dir)) p_cat(mv.dir, "/data/UserData/schwung/demake", mv.dir + sizeof mv.dir - 1);
    mv.save_on_close = (uint8_t)(json_int(json_defaults, "\"save_on_close\"", 1) != 0);
    mv.draw_thread = (uint8_t)(json_int(json_defaults, "\"draw_thread\"", 1) != 0);
    ck.lag = json_int(json_defaults, "\"clock_lag\"", 64);
    dmk_store_read = 0;
    dmk_store_write = 0;
    if (mv.dir[0]) { store_load(); dmk_store_read = rs_read; dmk_store_write = rs_write; }
    dmk_init(seed >= 0 ? (uint32_t)seed : clock_seed);
    mv_ready();
    pages_touch(sv_snap, sizeof sv_snap);
    pages_touch(st_buf, sizeof st_buf);
    pages_touch(spat, sizeof spat);
    pages_touch(sq, sizeof sq);
    pages_touch(&un, sizeof un);
    memset(&cost, 0, sizeof cost);
    memset(fbuf, 0, sizeof fbuf);
    memset(fseq, 0, sizeof fseq);
    atomic_store(&fb_mid, 1u);
    fb_w = 0; fb_r = 2; fb_count = 0;
    vw_count = 0;
    vw_last = 0;
    atomic_store(&vw_lock, 0u);
    atomic_store(&sv.state, SV_IDLE);
    sv.want = 0;
    atomic_store(&sv.done, 0u);
    atomic_store(&sv.failed, 0u);
    sv.seen_failed = 0;
    view_publish();
    atomic_store(&th_stop, 0);
    th_draw_on = mv.draw_thread && pthread_create(&th_draw, 0, draw_main, 0) == 0;
    th_save_on = mv.dir[0] && pthread_create(&th_save, 0, save_main, 0) == 0;
    if (!th_draw_on) frame_draw(&vw_now);
    mv.live = 1;
    return &mv;
}

static void v2_destroy(void *inst)
{
    if (inst != &mv || !mv.live) return;
    mv.live = 0;
    atomic_store(&th_stop, 1);
    if (th_draw_on) pthread_join(th_draw, 0);
    if (th_save_on) pthread_join(th_save, 0);   /* (a save under way is finished first) */
    th_draw_on = th_save_on = 0;
    /* closing keeps what was played (as MONO2K, the owner's choice): the project open into its slot, the store's file
     * with it. Nothing else runs now */
    if (mv.dir[0] && mv.save_on_close && store_on() && song_changed()) mv_save();
    if (mv.dir[0] && (sv.want || atomic_load(&sv.state) != SV_IDLE)) sv_write(st_ram);   /* (and a save not yet written) */
}

static void v2_on_midi(void *inst, const uint8_t *msg, int len, int source)
{
    if (inst != &mv || !mv.live || len < 1) return;
    const int st = msg[0];
    if (st >= 0xF8) {                  /* Move's transport, a byte (cable 0's, which the shim marks external) */
        if (st == 0xF8 || st == 0xFA || st == 0xFB || st == 0xFC) ev_push(EV_RT, st, 0);
        return;
    }
    if (len < 3 || source != MOVE_MIDI_SOURCE_INTERNAL) return;
    const int cmd = st & 0xF0, n = msg[1], v = msg[2];
    if (cmd != 0x90 && cmd != 0x80) return;
    const int on = cmd == 0x90 && v > 0;
    if (n >= NOTE_STEP1 && n < NOTE_STEP1 + 16) ev_push(EV_STEP, n - NOTE_STEP1, on);
    else if (n >= NOTE_PAD1 && n < NOTE_PAD1 + PADS) ev_push(EV_PAD, n - NOTE_PAD1, on ? v : -1);
}

/* "in": ui.js's controls, ';'-separated: "K i d" knob i turned d steps (ui.js's curve), "P i d c" knob i turned as the
 * tempo, d tenths of BPM (c 1: whole BPM, landing on one), "J d" the jog, "B cc v" a button's CC, "T i v" knob i
 * touched (1) or let go (0), "W n ok" ui.js wrote the tempo asked number n for Move (ok 0: it could not) */
static void in_parse(const char *s)
{
    while (*s) {
        while (*s == ' ' || *s == ';') s++;
        const char c = *s;
        if (!c) break;
        s++;
        int ok1 = 0, ok2 = 0, ok3 = 0, a = s_snum(&s, &ok1), b = 0, e = 0;
        if (c != 'J') b = s_snum(&s, &ok2);
        if (c == 'P') e = s_snum(&s, &ok3);
        if (c == 'K' && ok1 && ok2) ev_push(EV_KNOB, a, b);
        else if (c == 'P' && ok1 && ok2 && a >= 0 && a < 8) ev_push(EV_TEMPO, a | (ok3 && e ? 16 : 0), b);
        else if (c == 'W' && ok1 && ok2) ev_push(EV_WROTE, a & 255, b);
        else if (c == 'J' && ok1) ev_push(EV_JOG, 0, a);
        else if (c == 'B' && ok1 && ok2 && a >= 0 && a < 128) ev_push(EV_BTN, a, b);
        else if (c == 'T' && ok1 && ok2) ev_push(EV_TOUCH, a, b);
        while (*s && *s != ';') s++;
    }
}

static void v2_set_param(void *inst, const char *key, const char *val)
{
    if (inst != &mv || !mv.live || !key || !val) return;
    if (s_is(key, "in")) { in_parse(val); return; }
    if (s_is(key, "play") && (val[0] == '0' || val[0] == '1')) mv.want_play = val[0] - '0';
}

static int put_out(char *buf, int buf_len, const char *o, int n)
{
    if (n > buf_len - 1) n = buf_len - 1;
    memcpy(buf, o, (size_t)n);
    buf[n] = 0;
    return n;
}

static int v2_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    if (inst != &mv || !mv.live || !key || !buf || buf_len <= 0) return -1;
    static char o[FRAME_LEN + 64 > 8192 ? FRAME_LEN + 64 : 8192];
    if (s_is(key, "frame")) return put_out(buf, buf_len, o, frame_string(o));
    if (s_is(key, "ui")) return put_out(buf, buf_len, o, ui_string(o));
    if (s_is(key, "view")) return put_out(buf, buf_len, o, view_string(o));
    if (s_is(key, "dump")) return put_out(buf, buf_len, o, dump_string(o));
    if (s_is(key, "vals")) return put_out(buf, buf_len, o, val_string(o));
    if (s_is(key, "cost")) return put_out(buf, buf_len, o, cost_string(o));
    return -1;                          /* (rui_poll, state, remote_snapshot_rt_safe...: not served) */
}

static int v2_get_error(void *inst, char *buf, int buf_len)
{
    (void)inst; (void)buf; (void)buf_len;
    return 0;                           /* (a refused second instance is NULL from create: nothing else fails) */
}

static uint32_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   /* (the vDSO's: no system call) */
    return (uint32_t)ts.tv_sec * 1000000u + (uint32_t)(ts.tv_nsec / 1000);
}

/* a block: the glue's, then the snapshot's next piece, the page state, the cost */
static void v2_render(void *inst, int16_t *out, int frames)
{
    if (inst != &mv || !mv.live || frames <= 0) {
        if (out && frames > 0) memset(out, 0, (size_t)frames * 4u);
        return;
    }
    const uint32_t t0 = now_us();
    mv_block(out, frames);
    sv_step();
    const uint32_t x = atomic_load(&sv.failed);
    if (x != sv.seen_failed) { sv.seen_failed = x; notice("Not saved: file not written"); mv.touched = 1; }
    mv.pending |= mv.touched;
    const uint32_t since = mv.blocks - vw_last;
    if (since >= VW_EVERY || (mv.pending && since >= VW_SOON)) {
        view_publish();
        vw_last = mv.blocks;
        mv.pending = 0;
        if (!th_draw_on && !(++mv.nodraw & 1u)) frame_draw(&vw_now);   /* (no drawer: here, every other publish) */
    }
    mv.touched = 0;
    const uint32_t dt = now_us() - t0;
    mv.cpu_sum += dt;
    if (dt > mv.cpu_max) mv.cpu_max = dt;
    if (++mv.cpu_n == 128u) {
        mv.cpu_mean = mv.cpu_sum / 128u;
        mv.cpu_most = mv.cpu_max;
        mv.cpu_sum = mv.cpu_max = mv.cpu_n = 0;
    }
}

static plugin_api_v2_t api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = v2_create,
    .destroy_instance = v2_destroy,
    .on_midi = v2_on_midi,
    .set_param = v2_set_param,
    .get_param = v2_get_param,
    .get_error = v2_get_error,
    .render_block = v2_render,
};

EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *h)
{
    host = h;
    return &api;
}
