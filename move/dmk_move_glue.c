/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the glue between Schwung's side (dmk_move.c) and the core, after the core's unity build:
 *   the queue      every press, in the order it came, drained into the controller before each block;
 *   the clock      Move's transport and tempo followed, Demake's own clock steered to Move's phase, and the tempo asked
 *                  of Move;
 *   the block      the queue drained, the core rendered, as the FM-1 renders it: 128 frames at a time;
 *   then the controller (dmk_move_ui.c) and the 128 x 64 screen (screen/dmk_screen.c).
 * The core's frame is never called (it draws for a 240 x 240 colour screen): the controller keeps what else it keeps.
 * The including build gives mv_host_bpm (Move's tempo, 0 none) and mv_save_request. One instance: the core is a set of
 * statics. */
#include <string.h>
#include "dmk_move_view.h"

#define SR         44100.0
#define CLAMP(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))

static float mv_host_bpm(void);         /* Move's own tempo now (Schwung's get_bpm), 0 none */

/* ------------------------------------------------------------------------------------------------ the queue */

/* from on_midi and set_param to the next render, in the order they came. All three run on the audio thread, one after
 * another, so the queue needs no atomics; 256 is more than hands make in a block, and a press past it is dropped and
 * counted */
enum { EV_STEP, EV_PAD, EV_KNOB, EV_JOG, EV_BTN, EV_TOUCH, EV_RT, EV_TEMPO, EV_WROTE };
#define EV_N 256u
static struct {
    uint8_t kind[EV_N], a[EV_N];
    int16_t b[EV_N];
    uint32_t w, r, dropped;
} evq;

static void ev_push(int kind, int a, int b)
{
    if (evq.w - evq.r >= EV_N) { evq.dropped++; return; }
    evq.kind[evq.w % EV_N] = (uint8_t)kind;
    evq.a[evq.w % EV_N] = (uint8_t)a;
    evq.b[evq.w % EV_N] = (int16_t)CLAMP(b, -32767, 32767);
    evq.w++;
}

/* --------------------------------------------------------------------------------------------- the instance */

static struct {
    int live;
    uint64_t samples;                  /* frames rendered since create */
    uint32_t blocks;
    int want_play;                     /* set_param's "play" (-1 nothing): the bench's, the tools' */
    uint8_t touched, pending;          /* the queue fed the controller this block; a publish owed since */
    uint8_t draw_thread, save_on_close;
    uint32_t nodraw;                   /* publishes, when the render draws (no drawer thread) */
    uint32_t cpu_sum, cpu_max, cpu_n, cpu_mean, cpu_most;
    char dir[200];                     /* where the store lives ("" none: nothing read or written) */
} mv;

/* ------------------------------------------------------------------------------------------------ the clock */

/* Demake runs its own clock at Move's tempo, tenths and all, and steers it to Move's phase. Not its MIDI clock in: a
 * clock in steps it on whole pulses, which loses a take's micro timing and the delay's and tremolo's bar sync. Move's
 * pulses come once a block, each up to a block (2.9 ms) after it happened, so a phase follows Move's count of them,
 * gently (a correction of 1/32 of the error a pulse, and an integrator for a tempo slightly off), and each block the
 * sequencer's clock is moved a little towards it (ck_steer). The rest is MONO2K's (its m2k_move_glue.c): start,
 * continue and stop are Move's, passed on at once; Move has PLAY (module.json's button_passthrough): a press Demake sees
 * with no answer from Move within ~70 ms (no clock: no set, or a setting) plays on Demake's own clock, INT */
#define CK_KP (1.0 / 32)
#define CK_KI (1.0 / 2048)
#define CK_SNAP 6.0                    /* pulses off: a jump, not a drift (taken at once) */
#define CK_STALE (SR / 2)              /* no pulse for half a second while running: Move has gone */
#define CK_PLAY_WAIT 24                /* blocks (70 ms) for Move to answer PLAY */
#define CK_ANSWERED (SR * 0.15)        /* a transport message this recent answered the PLAY just seen */
#define CK_WIN 97                      /* pulses measured, when the host gives no tempo (4 beats) */
#define CK_SLEW (1.0 / 16)             /* a re-lock's correction a pulse at most */
#define CK_BAR 96                      /* a bar's pulses (4/4): a re-lock after a free run lands on Move's bar */
#define CK_LOCKED 0.05                 /* pulses off: the re-lock is done, the gentle lock again */
#define CK_STEER 2.0                   /* samples the sequencer's clock moves a block at most (1.6 %) */
static struct {
    uint8_t sync, run, anchored;       /* MV_SYNC_*; Move's transport running; its first pulse seen */
    uint8_t free;                      /* the phase runs free at Demake's own tempo (the tempo asked of Move, or alone) */
    uint8_t relock;                    /* back to Move's phase after a free run: 1 to begin (the bar picked), 2 under way */
    int32_t off;                       /* Demake's pulses ahead of Move's: base, and whole bars (a re-lock's) */
    int32_t base;                      /* the same, set by a take that waited for its pad: whole steps */
    int32_t nrx;                       /* Move's pulses since its start */
    double ph, fc, inc;                /* Move's pulses since its start now; the integrator; pulses a sample */
    double lag;                        /* samples a pulse is seen after it happened, on average (half a block) */
    double bpm, err;                   /* the tempo played (Move's, or the one asked of it); the last phase error, pulses */
    double steer;                      /* the sequencer against the phase now, pulses (ahead negative): the bench's */
    double move_bpm;                   /* Move's own tempo now (the host's, else measured), 0 none */
    uint64_t last_rx, last_tr;         /* the last pulse, the last start / continue / stop (tr_any: one came) */
    uint8_t tr_any;
    int play_wait;
    /* the tempo measured, when the host gives none */
    uint64_t at[CK_WIN];
    int nat, hat;
    uint8_t settled;
    double held;
} ck;

/* the tempo asked of Move (the tempo knob under SYNC: tq_ask). Demake plays it at once, its phase free, and ui.js
 * writes it to /data/UserData/schwung/desired-tempo, which Schwung's Link sidecar gives the Link session (Move alone in
 * it), so Move follows. When Move's tempo reaches it Demake follows Move again, its phase brought back gently
 * (ck.relock). If Move has not followed TQ_WAIT_S after the write (no Link on Move, Move->Schwung off, another peer in
 * the session) Demake stays at it alone (INT), started and stopped by Move's PLAY still, until Move's tempo next changes
 * or Delete + the tempo knob: then it follows Move again */
#define TQ_TOL 0.05                    /* BPM: Move has followed */
#define TQ_WAIT_S 2.5                  /* seconds after ui.js wrote it */
#define TQ_NOWRITE_S 4.0               /* seconds after the last turn, if ui.js said nothing of a write */
#define TQ_MIN 200                     /* tenths: the tempo asked between 20.0 */
#define TQ_MAX 3000                    /* and 300.0 BPM */
static struct {
    uint8_t state, seq, wseq;          /* MV_TQ_*; the request's number (1..255, 0 never); the one ui.js wrote */
    uint8_t nofile;                    /* ui.js could not write it */
    uint16_t b10;                      /* the tempo asked, tenths */
    uint64_t at, wrote;                /* samples: the last change; ui.js's write of it (0 not yet) */
    double move0;                      /* Move's tempo when Demake was left alone (a change re-syncs) */
    int32_t acc10;                     /* tenths turned under INT, towards the song's whole BPM */
} tq;

static int mv_sync(void) { return ck.sync == MV_SYNC_INT || tq.state == MV_TQ_ALONE ? MV_SYNC_INT : MV_SYNC_MOVE; }
static uint32_t mv_bpm10(void)
{
    if (ck.sync == MV_SYNC_MOVE && tq.state != MV_TQ_NONE) return tq.b10;
    if (ck.sync == MV_SYNC_MOVE && ck.bpm > 0) return (uint32_t)(ck.bpm * 10 + 0.5);
    return (uint32_t)seq_bpm * 10u;
}

static int song_row(void);
/* Demake's PLAY: on Home's Song row the song from its cursor (with a song to play, or REC to take one), as Arrange */
static void mv_transport(int on)
{
    if (on == seq_on) return;
    const int pg = ui.page;
    if (on && song_row() && (song_n || seq_rec)) ui.page = PG_ARR;
    play_toggle();
    ui.page = (uint8_t)pg;
}

static void ck_measure_reset(void) { ck.nat = ck.hat = 0; ck.settled = 0; }

/* the tempo from the pulses when the host gives none (Schwung's own way, shadow_transport.c): a least-squares line
 * through the window's pulses (one seen a block late moves it 1/n as much as at an end), held: taken when the window
 * first fills, then only when it disagrees by more than its ends can be wrong (three blocks over its span); before,
 * from half a beat, only a tempo 2 % off (a stop at another tempo) */
static double ck_fit(int n)
{
    int i = (ck.hat + CK_WIN - n) % CK_WIN;
    const uint64_t t0 = ck.at[i];
    double st = 0, sit = 0;
    for (int k = 0; k < n; k++, i = (i + 1) % CK_WIN) {
        const double t = (double)(ck.at[i] - t0);
        st += t;
        sit += k * t;
    }
    const double si = n * (n - 1) / 2.0, sii = (double)(n - 1) * n * (2 * n - 1) / 6.0;
    return (n * sit - si * st) / (n * sii - si * si);
}

static void ck_measure(uint64_t now)
{
    ck.at[ck.hat] = now;
    ck.hat = (ck.hat + 1) % CK_WIN;
    if (ck.nat < CK_WIN) ck.nat++;
    if (ck.nat < 13) return;
    const double spt = ck_fit(ck.nat);
    if (spt <= 0) return;
    const double bpm = 60.0 * SR / (24.0 * spt), off = ck.held > 0 ? bpm / ck.held - 1 : 1;
    if (bpm < 20 || bpm > 999) return;
    const int full = ck.nat == CK_WIN;
    const double band = ck.nat < 25 ? 0.02 : 3.0 * 128.0 / (spt * (ck.nat - 1));
    if (off > band || off < -band || (full && !ck.settled)) ck.held = bpm;
    if (full) ck.settled = 1;
}

/* REC lit, stopped, PLAY: ready, the FM-1's: the first pad starts it (Move has PLAY too, so it runs on meanwhile). 0: no */
static int mv_arm(void)
{
    if (seq_on || !seq_rec || song_row()) return 0;
    ui.rec_ready = 1;
    return 1;
}
/* the pad that ends the wait: Demake's pulse 0 is the step of Move's nearest it, so their steps stay together; the rest
 * of the way is ck_steer's */
static void ck_take_start(void)
{
    if (!ck.run || !ck.anchored) return;
    const int32_t p = (int32_t)(ck.ph / 6.0 + 0.5) * 6;
    ck.ph -= p;
    ck.off -= p;
    ck.base = ck.off;
}

static void ck_start(uint64_t now)
{
    ck.sync = MV_SYNC_MOVE;
    ck.run = 1;
    ck.anchored = 0;
    ck.nrx = 0;
    ck.fc = 0;
    ck.off = ck.base = 0;
    ck.relock = 0;
    ck.free = tq.state != MV_TQ_NONE;  /* (waiting for Move, or alone: Demake at the tempo asked, from the start) */
    ck.play_wait = 0;
    ck.last_rx = ck.last_tr = now;
    ck.tr_any = 1;
    ck_measure_reset();
    const int was = seq_on;
    mv_transport(0);                   /* (playing alone already: back to the top with Move) */
    if (was || !mv_arm()) mv_transport(1);
}

static void ck_stop(void)
{
    ck.run = 0;
    ck.anchored = 0;
    ck.relock = 0;
    ck.play_wait = 0;
    ui.rec_ready = 0;
    ck_measure_reset();
    mv_transport(0);
}

static void ck_rt(int st, uint64_t now)
{
    if (st == 0xFA || st == 0xFB) { ck_start(now); return; }   /* (a continue too: Demake has no place to continue from) */
    if (st == 0xFC) { ck.sync = MV_SYNC_MOVE; ck.last_tr = now; ck.tr_any = 1; ck_stop(); return; }
    if (st != 0xF8) return;
    ck.sync = MV_SYNC_MOVE;
    ck_measure(now);
    ck.last_rx = now;
    if (!ck.run) return;               /* (a clock without a start: its tempo, but nothing starts) */
    ck.nrx++;
    const double obs = (ck.nrx - 1) + ck.lag * ck.inc * (1 + ck.fc);   /* pulse nrx - 1, seen about lag ago */
    if (!ck.anchored) { ck.anchored = 1; ck.ph = obs + ck.off; ck.err = 0; return; }
    if (ck.relock == 1) {              /* after a free run: Move's bar nearest Demake's phase (at most half a bar away) */
        const double bars = (ck.ph - obs - ck.base) / CK_BAR;
        ck.off = ck.base + CK_BAR * (int32_t)(bars < 0 ? bars - 0.5 : bars + 0.5);
        ck.relock = 2;
    }
    const double e = obs + ck.off - ck.ph;
    ck.err = e;
    if (ck.free) return;               /* (Demake's own tempo: Move's pulses only counted) */
    if (ck.relock) {                   /* back to Move's phase without a jump: a capped nudge a pulse, no integrator */
        ck.ph += CLAMP(CK_KP * e, -CK_SLEW, CK_SLEW);
        if (e < CK_LOCKED && e > -CK_LOCKED) ck.relock = 0;
        return;
    }
    if (e > CK_SNAP || e < -CK_SNAP) { ck.ph = obs + ck.off; return; }
    ck.ph += CK_KP * e;
    ck.fc += CK_KI * e;
    ck.fc = ck.fc > 0.02 ? 0.02 : ck.fc < -0.02 ? -0.02 : ck.fc;
}

/* the tempo played, each block: the one asked of Move while it is (Demake's at once), else Move's: the host's (steady
 * by its contract), else the pulses measured */
static void ck_tempo(void)
{
    double b = 0;
    { const float f = mv_host_bpm(); if (f >= 20.0f && f <= 999.0f) b = f; }
    if (b <= 0) b = ck.held;
    ck.move_bpm = b;
    if (ck.sync == MV_SYNC_MOVE && tq.state != MV_TQ_NONE) b = tq.b10 / 10.0;
    if (b <= 0) { ck.inc = 0; return; }
    ck.bpm = b;
    ck.inc = b * 24.0 / (60.0 * SR);
}

/* the core's tempo, each block after the queue (sp_sync, on any control, sets the song's whole number): Move's, in
 * tenths, while following; the song's on Demake's own clock */
static void ck_apply(void)
{
    if (ck.sync == MV_SYNC_MOVE && ck.bpm >= 20.0) {
        const double b = ck.bpm > 300.0 ? 300.0 : ck.bpm;
        seq_b10 = (int)(b * 10 + 0.5);
        seq_bpm = (int)(b + 0.5);
    } else seq_b10 = seq_bpm * 10;
}

/* the sequencer's clock towards Move's phase, a little a block: Demake's steps since PLAY as pulses, six a step, and
 * seq_acc moved by a sixteenth of the difference, at most CK_STEER samples, so a note's place in its step, the swing of
 * a take, is never touched. Behind, its next step comes sooner; ahead, later (never back over a step already played) */
static void ck_steer(void)
{
    ck.steer = 0;
    if (!ck.run || !ck.anchored || !seq_on || ck.inc <= 0 || !seq_ticks) return;
    const double per = seq_per(), at = (seq_ticks - 1) * 6.0 + 6.0 * seq_acc / per;
    const double e = ck.ph - at;       /* pulses: + Demake is behind */
    ck.steer = e;
    if (ck.free) return;
    double d = e / ck.inc / 16.0;      /* samples */
    d = CLAMP(d, -CK_STEER, CK_STEER);
    const int32_t q8 = (int32_t)(d * 256.0);
    if (q8 >= 0) seq_acc += (uint32_t)q8;
    else seq_acc -= (uint32_t)-q8 > seq_acc ? seq_acc : (uint32_t)-q8;
}

/* a PLAY press seen (ui.js): Move answers it with a start or a stop when it sends its clock; else Demake plays alone */
static void mv_play_press(void)
{
    if (ck.sync == MV_SYNC_INT) {      /* (ready, PLAY again: not any more, as under Move) */
        if (ui.rec_ready) ui.rec_ready = 0;
        else if (!mv_arm()) mv_transport(!seq_on);
        return;
    }
    if (ck.tr_any && mv.samples - ck.last_tr < (uint64_t)CK_ANSWERED) return;   /* (Move's start or stop came first) */
    ck.play_wait = CK_PLAY_WAIT;
}

/* ------------------------------------------------------------------------------------- the tempo asked of Move */

/* Demake's phase back to Move's: at the next pulse its bar is picked, then nudged there (ck_rt) */
static void tq_relock(void)
{
    if (!ck.free) return;
    ck.free = 0;
    if (ck.run && ck.anchored) ck.relock = 1;
}

/* the tempo knob under SYNC: d10 tenths (coarse: whole BPM, landing on one). Demake at it now; ui.js asks Move */
static void tq_ask(int d10, int coarse)
{
    const int base = (int)mv_bpm10();
    int b = base + d10;
    if (coarse) b = (d10 > 0 ? base / 10 * 10 : (base + 9) / 10 * 10) + d10;
    b = CLAMP(b, TQ_MIN, TQ_MAX);
    if (b == base && tq.state == MV_TQ_NONE) return;
    tq.b10 = (uint16_t)b;
    tq.seq = (uint8_t)(tq.seq == 255 ? 1 : tq.seq + 1);
    tq.at = mv.samples;
    tq.wrote = 0;
    tq.nofile = 0;
    tq.state = MV_TQ_WAIT;
    ck.free = 1;                       /* (Demake's phase its own until Move has followed) */
    ck.relock = 0;
    ui.val[0][PG_HOME][0][4] = (uint8_t)CLAMP((b + 5) / 10, 20, 255);   /* (the song keeps it, whole) */
    ck_tempo();
}

/* a project opened: Move asked for its tempo */
static void tq_song(void)
{
    if (ck.sync == MV_SYNC_MOVE) tq_ask(ui.val[0][PG_HOME][0][4] * 10 - (int)mv_bpm10(), 0);
}

/* Delete + the tempo knob: Move's tempo again (the user asks) */
static void tq_cancel(void)
{
    if (tq.state == MV_TQ_NONE) return;
    tq.state = MV_TQ_NONE;
    tq_relock();
    ck_tempo();
}

/* ui.js wrote request SEQ (ok: the write worked) */
static void tq_written(int seq, int ok)
{
    if (tq.state != MV_TQ_WAIT || seq != tq.seq) return;
    tq.wseq = (uint8_t)seq;
    tq.wrote = mv.samples + 1;         /* (0: not yet) */
    tq.nofile = (uint8_t)!ok;          /* (no file written: Move cannot follow, no wait) */
}

/* each block: Move has followed (Demake follows it again), or not in time (alone, if playing), or, alone, Move's tempo
 * moved (follow it again) */
static void tq_tick(void)
{
    if (ck.sync != MV_SYNC_MOVE || tq.state == MV_TQ_NONE) return;
    const double mb = ck.move_bpm, want = tq.b10 / 10.0;
    if (tq.state == MV_TQ_WAIT) {
        if (mb > 0 && mb - want <= TQ_TOL && want - mb <= TQ_TOL) {
            tq.state = MV_TQ_NONE;
            tq_relock();
            ck_tempo();
            return;
        }
        const int late = tq.nofile || (tq.wrote ? mv.samples + 1 - tq.wrote > (uint64_t)(TQ_WAIT_S * SR)
                                                : mv.samples - tq.at > (uint64_t)(TQ_NOWRITE_S * SR));
        if (!late) return;
        mv.touched = 1;
        if (seq_on && ck.run) {        /* playing: on alone at it, INT */
            tq.state = MV_TQ_ALONE;
            tq.move0 = mb;
            notice("Own tempo: Move has no Link");
        } else {                       /* stopped: nothing to keep; Move's tempo (its start brings it) */
            tq.state = MV_TQ_NONE;
            ck.free = 0;
            ck_tempo();
            notice("Move keeps its tempo");
        }
        return;
    }
    if (mb > 0 && (mb - tq.move0 > TQ_TOL || tq.move0 - mb > TQ_TOL)) {   /* ALONE: Move's tempo changed */
        tq.state = MV_TQ_NONE;
        tq_relock();
        ck_tempo();
    }
}

/* ---------------------------------------------------------------------------------- the controller, the screen */

static void mv_save_request(void);
#include "dmk_move_ui.c"
#include "screen/dmk_screen.c"

/* the queue into the controller, and Move's transport into the clock, in the order they came */
static void ev_drain(void)
{
    for (; evq.r != evq.w; evq.r++) {
        const int k = evq.kind[evq.r % EV_N], a = evq.a[evq.r % EV_N], b = evq.b[evq.r % EV_N];
        if (k == EV_RT) { ck_rt(a, mv.samples); continue; }
        if (k == EV_WROTE) { tq_written(a, b != 0); continue; }
        mv.touched = 1;
        switch (k) {
        case EV_STEP: mv_step(a, b); break;
        case EV_PAD: mv_pad(a, b < 0 ? 0 : b, b >= 0); break;
        case EV_KNOB: mv_knob(a, b); break;
        case EV_TEMPO: mv_tempo_knob(a & 15, b, a >> 4); break;
        case EV_JOG: mv_jog(b); break;
        case EV_BTN: mv_button(a, b); break;
        case EV_TOUCH: mv_touch(a, b); break;
        }
    }
}

/* ------------------------------------------------------------------------------------------- the instance */

/* the instance cleared, Move's clock the rule, one core: before dmk_init, which loads the store if the build set it */
static void mv_begin(void)
{
    memset(&evq, 0, sizeof evq);
    memset(&mv, 0, sizeof mv);
    memset(&ck, 0, sizeof ck);
    memset(&tq, 0, sizeof tq);
    mv.want_play = -1;
    ck.lag = 64;
    ck.sync = MV_SYNC_MOVE;
    dmk_par_begin = 0;
    dmk_par_end = 0;
    dmk_yield = 0;
}

/* after dmk_init: no boot picture, the sounds' places, the latency a take has off (a block in, a block out), the
 * controller */
static void mv_ready(void)
{
    ui.boot_done = 1;
    dmk_latency(256);
    mv_ctl_init();
}

/* a block of FRAMES (Schwung's are 128) */
static void mv_block(int16_t *out, int frames)
{
    ui.now = (uint32_t)(mv.samples * 10u / 441u);       /* the UI's milliseconds (its notices, its taps) */
    if (mv.want_play >= 0) { mv_transport(mv.want_play); mv.want_play = -1; }
    ck_tempo();
    ev_drain();
    tq_tick();
    mv_tick();
    if (ck.run && mv.samples - ck.last_rx > (uint64_t)CK_STALE) { ck_stop(); mv.touched = 1; }
    if (ck.play_wait && --ck.play_wait == 0) {          /* no answer from Move: Demake's own clock */
        ck.sync = MV_SYNC_INT;
        tq.state = MV_TQ_NONE;                          /* (a tempo asked of Move meanwhile: moot, no clock) */
        ck.free = 0;
        sp_sync();                                      /* (the song's tempo back) */
        if (!mv_arm()) mv_transport(!seq_on);
        mv.touched = 1;
    }
    ck_apply();
    ck_steer();
    const int n = frames / DMK_SUB * DMK_SUB;
    dmk_render(out, n);
    if (n < frames) memset(out + 2 * n, 0, (size_t)(frames - n) * 4u);
    if (ck.run && ck.anchored) ck.ph += frames * ck.inc * (ck.free ? 1 : 1 + ck.fc);
    mv.samples += (uint64_t)frames;
    mv.blocks++;
}
