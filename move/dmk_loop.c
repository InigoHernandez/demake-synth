/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: loops (the owner's, 2026-11-21: "a looper ... like a Torso S-4 lite": record Demake's mix or the
 * audio in, then mangle and overdub). Included by the core's unity build (firmware/src/dmk_core.c) under DMK_MOVE only.
 *
 * Four loops, a kind of sound after the YM2612's patches: a synth set to one plays it with the transport, locked to the
 * bar, through its own filter (the LFO on its cutoff), and then its chain, level, pan and sends like any of its sounds.
 * REC on its Sound page waits for the bar and records the loop's length from Demake's mix (every synth but the ones
 * playing it, with their own effects, before Global's: the loop goes through Global's on its way out) or from the
 * audio in; REC again overdubs one pass on the next bar, the old layer kept by Overdub; Delete + REC clears it.
 *
 * Time is the transport's: steps since PLAY in Q32, so a loop recorded at one tempo plays at another bar for bar
 * (varispeed), and recording and playing find a sample by the same rule, its place in the loop. The buffers are big
 * (64 s of stereo each): they are touched once when Demake opens, never faulted in during a block.
 *
 * Step 2 (the owner's mangling: pitch apart from speed, a granular cloud). Pitch is Haizea's HEGAN (the owner's, MIT)
 * reading the loop instead of a line: two taps sweeping a short window round the playhead, crossfaded, so the loop climbs
 * without moving faster. The cloud is Clouds' granular mode (Emilie Gillet, MIT; its algorithm, none of its code):
 * grains of the loop from about the playhead (or where Freeze held it), Size long, Density a second, Spray scattering
 * them back through the loop, Shape their rise, Shimmer some an octave up, Flip some backwards, each at Speed and Pitch.
 * Integer, nothing allocated; at Pitch 0 and Cloud 0 the loop plays exactly as step 1 played it.
 *
 * Step 3: the loops kept with their project, as WAV files beside the store (move/dmk_loopfile.c reads and writes them
 * on the saver's thread). Here only what the audio thread owns: each loop's version, the version its project's file
 * holds, the jobs it hands the saver and the results it takes back. A loop being read or written takes no REC. */
#include <stdatomic.h>

#define LP_SEC       64                 /* a loop's longest: 16 bars at 60 BPM */
#define LP_FRAMES    (LP_SEC * DMK_SR)
#define LP_PEAKS     48                 /* its picture's columns (mv_pic_t's env) */
#define LP_STEPS_BAR 16                 /* sixteenths a bar */
#define LP_BARS_N    5                  /* Length: 1, 2, 4, 8, 16 bars */
#define LP_SPEED_N   5                  /* Speed: a quarter, a half, as recorded, twice, four times */
#define LP_SPEED_ONE 2
#define LP_GAIN_ONE  64                 /* Gain at its middle: the source as it is */
#define LP_KEEP_ALL  127                /* Overdub at the top: the old layer whole */
#define LP_PEAK_MAX  255
#define LP_BACK_Q8   724                /* a loop into its synth, Q8 (2.83): the chain's quarter, full level's twice and the
                                           middle pan's 0.71 undone, so at full level, panned middle, it plays as loud as it
                                           was recorded */
#define LP_PITCH_MID 24                 /* Pitch at its middle: as Speed plays it (-24..+24 semitones) */
#define LP_SHIFT_W   1536               /* Pitch's taps' window, frames (~35 ms: HEGAN's, short so it waits little) */
#define LP_GRAINS    16                 /* a synth's cloud at once (more asked: the ones over wait) */
#define LP_GR_MIN    441                /* Size at its least, frames (10 ms) */
#define LP_SIZE_N    57                 /* Size: 10 ms x 2^(u/8), to 1.28 s */
#define LP_DENS_MIN  128                /* Density at its least, grains a second Q8 (0.5) */
#define LP_DENS_N    64                 /* Density: 0.5 x 2^(u/8) a second, to ~117 */
#define LP_SHAPE_N   5                  /* Shape: Smooth, Soft, Even, Sharp, Click */
#define LP_AMT_ALL   127                /* Cloud, Spray, Shimmer, Flip at the top */
#define LP_OVER_FREE 512                /* grains overlapping (Q8) heard as they are; past two, the sum held by its power */
#define LP_Q24       (1u << 24)         /* a grain's half window */

enum { LS_EMPTY, LS_ARMED, LS_REC, LS_PLAY, LS_ODUB_ARMED, LS_ODUB };   /* a loop's state */
enum { LV_SET, LV_SRC, LV_BARS, LV_KEEP, LV_SPEED, LV_REV, LV_GAIN, LV_PITCH,   /* the sound's loop values (lpv) */
       LV_CLOUD, LV_SIZE, LV_DENS, LV_SPRAY, LV_SHAPE, LV_SHIM, LV_FLIP, LV_FREEZE, LV_COUNT };
_Static_assert(LV_COUNT == LV_N, "dmk_dsp.c's LV_N is the loop values' count");
enum { LSRC_MIX, LSRC_JACK };

static const uint8_t LP_SPEED_NUM[LP_SPEED_N] = {1, 1, 1, 2, 4}, LP_SPEED_DEN[LP_SPEED_N] = {4, 2, 1, 1, 1};
static const uint8_t LP_SHAPE_RISE[LP_SHAPE_N] = {128, 77, 38, 15, 5};   /* each Shape's rise, of the grain, Q8 */

static int16_t lp_buf[LP_N][LP_FRAMES][2];
static struct {
    uint32_t len;                       /* its frames (0: empty) */
    uint16_t steps;                     /* its length in steps */
    uint8_t state, src, keep, gain;     /* LS_*; how it records: the recording synth's, taken at REC */
    uint64_t start;                     /* the bar (Q32 steps since PLAY) the recording or the overdub starts on */
    uint8_t peak[LP_PEAKS];
    uint32_t ver;                       /* its version: up with each pass written, each clear, each load */
} lps[LP_N];
static voice_t lp_vf[DMK_SYNTHS];       /* each synth's filter state for its loop (vfilt's) */
typedef struct {
    int64_t pos;                        /* where it reads in its loop, frames Q16 */
    int32_t inc;                        /* a sample's step, Q16 (below 0: backwards) */
    uint32_t ph, up, down;              /* its window: the phase in its half (Q24), the rise's and the fall's steps */
    uint8_t on, fall;
} lp_grain_t;
typedef struct {                        /* each synth's pitch and cloud */
    lp_grain_t g[LP_GRAINS];
    int32_t wait;                       /* samples to the next grain */
    uint32_t rnd;                       /* its dice (xorshift: the same cloud every render) */
    uint32_t phi;                       /* Pitch's taps: where in their window, Q32 */
    uint32_t frz;                       /* Freeze's place (frame Q8), held while it is on */
    uint8_t frz_on;
} lp_cloud_t;
static lp_cloud_t lpc[DMK_SYNTHS];
static uint64_t lp_t0, lp_span;         /* the transport at the block's first sample, and how far it goes: Q32 steps */
static uint32_t lp_frames;              /* the block's samples */
static uint8_t lp_run_ok;               /* the transport has a place (playing, past PLAY) */
static uint8_t lp_ready;                /* the buffers touched */
static const int16_t *lp_jack;          /* the audio in this block (interleaved, the Move's; 0: none) */

/* the files: the jobs the audio thread hands the saver (a ring, one writer each side), the result of each it takes back
 * (one a loop, taken at the next block), and what each loop's file in its project holds */
#define LJ_RING      32
#define LP_SLOT_NONE 0xFF               /* a loop's file not known: an empty loop saved there deletes it */
enum { LJ_LOAD, LJ_WRITE, LJ_DELETE };
enum { LR_NONE, LR_LOADED, LR_EMPTY, LR_DONE, LR_FAILED };
typedef struct { uint8_t op, k, slot; uint16_t steps; uint32_t len; } lp_job_t;
static lp_job_t lj_ring[LJ_RING];
static _Atomic uint32_t lj_head, lj_tail;   /* the audio thread's next, the saver's next */
static struct {
    _Atomic int kind;                   /* LR_*: set by the saver, taken (back to LR_NONE) by the audio thread */
    uint32_t len;
    uint16_t steps;
    uint8_t peak[LP_PEAKS];
} lr[LP_N];
static struct { uint8_t slot; uint32_t ver; } lpd[LP_N];   /* the project whose file has each loop, at which version */
static uint8_t lp_wait[LP_N], lp_wait_op[LP_N];            /* its jobs not yet back; the last one's LJ_* */

static void lp_touch(void)              /* at create, off the audio thread: every page of the buffers in memory */
{
    memset(lp_buf, 0, sizeof lp_buf);
    memset(lps, 0, sizeof lps);
    memset(lpc, 0, sizeof lpc);
    memset(lp_wait, 0, sizeof lp_wait);
    for (int k = 0; k < LP_N; k++) { atomic_store(&lr[k].kind, LR_NONE); lpd[k].slot = LP_SLOT_NONE; lpd[k].ver = 0; }
    atomic_store(&lj_head, 0u);
    atomic_store(&lj_tail, 0u);
    lp_ready = 1;
}

static int lj_push(int op, int k, int slot)
{
    const uint32_t h = atomic_load_explicit(&lj_head, memory_order_relaxed);
    if (h - atomic_load_explicit(&lj_tail, memory_order_acquire) >= LJ_RING) return 0;
    lj_ring[h % LJ_RING] = (lp_job_t){(uint8_t)op, (uint8_t)k, (uint8_t)slot, lps[k].steps, lps[k].len};
    atomic_store_explicit(&lj_head, h + 1u, memory_order_release);
    lp_wait[k]++;
    lp_wait_op[k] = (uint8_t)op;
    return 1;
}
static int lj_pop(lp_job_t *j)          /* (the saver's) */
{
    const uint32_t t = atomic_load_explicit(&lj_tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&lj_head, memory_order_acquire)) return 0;
    *j = lj_ring[t % LJ_RING];
    atomic_store_explicit(&lj_tail, t + 1u, memory_order_release);
    return 1;
}

/* each block (lp_begin's, playing or not): what the saver finished. A load becomes the loop, as its file has it */
static void lp_adopt(void)
{
    for (int k = 0; k < LP_N; k++) {
        const int r = atomic_load_explicit(&lr[k].kind, memory_order_acquire);
        if (r == LR_NONE) continue;
        if (r == LR_LOADED) {
            lps[k].len = lr[k].len; lps[k].steps = lr[k].steps;
            memcpy(lps[k].peak, lr[k].peak, sizeof lps[k].peak);
            lps[k].state = LS_PLAY;
            lpd[k].ver = ++lps[k].ver;
        }
        if (r == LR_FAILED) { lpd[k].slot = LP_SLOT_NONE; notice("Loop not saved: file not written"); }
        if (lp_wait[k]) lp_wait[k]--;
        atomic_store_explicit(&lr[k].kind, LR_NONE, memory_order_release);
    }
}

static int lp_writing_state(int k)
{
    const int st = lps[k].state;
    return st == LS_ARMED || st == LS_REC || st == LS_ODUB_ARMED || st == LS_ODUB;
}
/* the loops changed since their project's files (song_changed's, on the Move): one being written, one whose file there
 * holds another version (a pass since, a clear), one with sound and no file there. An empty loop whose file is not
 * known is no change */
static int lp_dirty(void)
{
    for (int k = 0; k < LP_N; k++) {
        if (lp_wait[k]) continue;
        if (lp_writing_state(k)) return 1;
        if (lpd[k].slot == ui.song ? lpd[k].ver != lps[k].ver : lps[k].len != 0) return 1;
    }
    return 0;
}
/* a project opened or new (the Move's mv_opened): every loop let go, its project's files asked for */
static void lp_open(int slot, int used)
{
    for (int k = 0; k < LP_N; k++) {
        lps[k].state = LS_EMPTY; lps[k].len = 0; lps[k].steps = 0;
        memset(lps[k].peak, 0, sizeof lps[k].peak);
        lpd[k].ver = ++lps[k].ver;
        lpd[k].slot = used ? (uint8_t)slot : LP_SLOT_NONE;   /* (an unused slot's files, if any are left: unknown) */
        if (used) lj_push(LJ_LOAD, k, slot);
    }
}
/* the project saved into slot (the Move's mv_save_project): each loop changed since its file written, an empty one's
 * file deleted. One being written is left for after its pass (still a change) */
static void lp_save(int slot)
{
    for (int k = 0; k < LP_N; k++) {
        if (lp_wait[k] || lp_writing_state(k)) continue;
        if (lpd[k].slot == slot && lpd[k].ver == lps[k].ver) continue;
        if (!lj_push(lps[k].len ? LJ_WRITE : LJ_DELETE, k, slot)) continue;
        lpd[k].slot = (uint8_t)slot;
        lpd[k].ver = lps[k].ver;
    }
}
static void lp_erase(int slot)          /* a project deleted (song_erase's, on the Move): its loops' files with it */
{
    for (int k = 0; k < LP_N; k++) {
        lj_push(LJ_DELETE, k, slot);
        if (lpd[k].slot == slot) lpd[k].slot = LP_SLOT_NONE;
    }
}

static void lp_vals_new(int s)          /* a sound's loop values as first picked */
{
    uint8_t *v = lpv[s];
    v[LV_SET] = 1; v[LV_SRC] = LSRC_MIX; v[LV_BARS] = 2; v[LV_KEEP] = LP_KEEP_ALL * 3 / 4; v[LV_SPEED] = LP_SPEED_ONE;
    v[LV_REV] = 0; v[LV_GAIN] = LP_GAIN_ONE; v[LV_PITCH] = LP_PITCH_MID;
    v[LV_CLOUD] = 0; v[LV_SIZE] = 27; v[LV_DENS] = 40; v[LV_SPRAY] = 16;   /* (~100 ms grains, 16 a second: no gaps) */
    v[LV_SHAPE] = 0; v[LV_SHIM] = 0; v[LV_FLIP] = 0; v[LV_FREEZE] = 0;
}

/* the Sound list: the recordings, the YM2612's patches (when no other synth plays the chip), the loops; a step on */
static int snd_kind(int v) { return v < DMK_SAMPLES_N ? 0 : v < DMK_SAMPLES_N + YM_PATCHES ? 1 : 2; }
static int snd_step(int s, int v, int dir)
{
    const int o = ym_owner(), fm = o < 0 || o == s, top = DMK_SAMPLES_N + YM_PATCHES + LP_N;
    if (v < 0 || v >= top) v = 0;
    do v = wrap(v + dir, top); while (!fm && snd_kind(v) == 1);
    return v;
}
/* Shift + jog on the Sound page: to the next kind (the recordings, the patches, the loops), the last of it the synth had */
static uint8_t snd_last[DMK_SYNTHS][3];
static int snd_jump(int s, int v, int dir)
{
    const int o = ym_owner(), fm = o < 0 || o == s;
    static const int FIRST[3] = {0, DMK_SAMPLES_N, DMK_SAMPLES_N + YM_PATCHES}, N[3] = {DMK_SAMPLES_N, YM_PATCHES, LP_N};
    int k = snd_kind(v);
    snd_last[s][k] = (uint8_t)(v - FIRST[k] + 1);
    do k = wrap(k + dir, 3); while (!fm && k == 1);
    return FIRST[k] + (snd_last[s][k] ? snd_last[s][k] - 1 : 0) % N[k];
}

/* where the transport is for this block (dsp_render's, before its passes): its first sample and its span, each sample
 * placed by the exact span (a rounded step a sample, added up, fell short at the block's end: an overdub missed the
 * block's last frame) */
static void lp_begin(int frames)
{
    const uint32_t per = seq_per();
    lp_adopt();
    lp_run_ok = 0;
    if (!seq_on || !seq_ticks || !per || frames <= 0) return;
    const uint64_t end = ((uint64_t)(seq_ticks - 1u) << 32) + ((uint64_t)seq_acc << 32) / per;
    const uint64_t back = ((uint64_t)frames * 256u << 32) / per;
    if (end < back) return;                             /* (PLAY inside this block: from the next) */
    lp_t0 = end - back;
    lp_span = back;
    lp_frames = (uint32_t)frames;
    lp_run_ok = 1;
}
static uint64_t lp_t(int i) { return lp_t0 + lp_span * (uint64_t)i / lp_frames; }   /* the transport at sample i */

/* a loop's frame (Q8) at transport t, at speed num/den, reversed or not: one exact division, so consecutive samples
 * step a frame each (a rounded step between, a pass's share first, made them step 0 then 2 near a frame's edge: an
 * overdub missed one frame in fourteen). pos * len stays under 2^62: 256 steps of Q32 times LP_FRAMES */
static uint32_t lp_frame_q8(int k, uint64_t t, int num, int den, int rev)
{
    const uint64_t L = (uint64_t)lps[k].steps << 32, pos = (t * (uint64_t)num / (uint64_t)den) % L;
    uint32_t f = (uint32_t)(pos * lps[k].len / ((uint64_t)lps[k].steps << 24));
    if (rev) f = lps[k].len * 256u - 1u - f;
    return f;
}

static uint32_t lp_rand(uint32_t *r)    /* xorshift32 */
{
    uint32_t x = *r ? *r : 2463534242u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *r = x;
}
static uint32_t lp_exp2(int x64) { return T_EXP2[64 - (x64 & 63)] << ((x64 >> 6) + 1); }   /* 2^(x64/64), Q16, x64 >= 0 */
static uint32_t lp_size(int u) { return (uint32_t)(((uint64_t)LP_GR_MIN * lp_exp2(u * 8)) >> 16); }   /* Size's frames */
static uint32_t lp_rate(int u) { return (uint32_t)(((uint64_t)LP_DENS_MIN * lp_exp2(u * 8)) >> 16); }  /* Density, Q8 */

/* a loop's frame a0 and the next, fr (Q8) between them, in mono */
static int32_t lp_mono(int k, uint32_t a0, int32_t fr)
{
    const uint32_t a1 = a0 + 1 < lps[k].len ? a0 + 1 : 0;
    const int32_t m0 = (lp_buf[k][a0][0] + lp_buf[k][a0][1]) / 2, m1 = (lp_buf[k][a1][0] + lp_buf[k][a1][1]) / 2;
    return m0 + (((m1 - m0) * fr) >> 8);
}
static int32_t lp_at_q8(int k, int64_t q8)   /* at any place (Q8) round the loop */
{
    const int64_t L = (int64_t)lps[k].len << 8;
    while (q8 >= L) q8 -= L;
    while (q8 < 0) q8 += L;
    return lp_mono(k, (uint32_t)(q8 >> 8), (int32_t)(q8 & 255));
}

/* Pitch: two taps half a window apart round the playhead fq, each fading in as it passes the middle, so neither's jump
 * back is heard. At phi 0 the second is the playhead itself, the first silent: a pitch picked starts where step 1 played */
static int32_t lp_shifted(int k, uint32_t phi, uint32_t fq)
{
    const int32_t dA = (int32_t)(phi - 0x80000000u), dB = (int32_t)phi;
    const int64_t oA = ((int64_t)dA * (LP_SHIFT_W * 256)) >> 32, oB = ((int64_t)dB * (LP_SHIFT_W * 256)) >> 32;
    const int32_t gA = 32768 - (int32_t)((dA < 0 ? -(int64_t)dA : (int64_t)dA) >> 16);
    const int32_t a = lp_at_q8(k, (int64_t)fq + oA), b = lp_at_q8(k, (int64_t)fq + oB);
    return b + (((a - b) * (gA >> 1)) >> 14);
}

/* a grain from about centre (frame Q8): Spray back through the loop, Shimmer an octave up, Flip backwards */
static void lp_grain_start(int s, int k, uint32_t centre, int32_t inc)
{
    const uint8_t *v = lpv[s];
    lp_grain_t *g = 0;
    for (int j = 0; j < LP_GRAINS && !g; j++) if (!lpc[s].g[j].on) g = &lpc[s].g[j];
    if (!g) return;
    uint32_t *r = &lpc[s].rnd;
    const uint32_t len = lps[k].len, spray = (uint32_t)((uint64_t)len * v[LV_SPRAY] / (2u * LP_AMT_ALL));
    uint32_t size = lp_size(v[LV_SIZE] < LP_SIZE_N ? v[LV_SIZE] : 0);
    if (size > len) size = len;
    if (size < 2) size = 2;
    const uint32_t back = spray ? lp_rand(r) % (spray + 1u) : 0;
    const int64_t L = (int64_t)len << 16;
    int64_t p = ((int64_t)centre << 8) - ((int64_t)back << 16);
    while (p < 0) p += L;
    while (p >= L) p -= L;
    if (lp_rand(r) % LP_AMT_ALL < v[LV_SHIM]) inc *= 2;
    if (lp_rand(r) % LP_AMT_ALL < v[LV_FLIP]) inc = -inc;
    uint32_t rise = (uint32_t)((uint64_t)size * LP_SHAPE_RISE[v[LV_SHAPE] < LP_SHAPE_N ? v[LV_SHAPE] : 0] >> 8);
    if (rise < 1) rise = 1;
    if (rise >= size) rise = size - 1;
    g->pos = p; g->inc = inc; g->ph = 0; g->fall = 0;
    g->up = LP_Q24 / rise; g->down = LP_Q24 / (size - rise);
    g->on = 1;
}
/* a grain's next n samples into wet: its window a raised cosine (T_SIN's first quarter, squared) up, then down */
static void lp_grain_run(int k, lp_grain_t *g, int32_t *wet, int n)
{
    const int64_t L = (int64_t)lps[k].len << 16;
    for (int i = 0; i < n && g->on; i++) {
        if (g->pos >= L || g->pos < 0) { g->pos %= L; if (g->pos < 0) g->pos += L; }   /* (its loop shorter since) */
        const uint32_t x = g->fall ? LP_Q24 - g->ph : g->ph, ix = x >> 16;
        const int32_t fx = (int32_t)((x >> 8) & 255u), sv = T_SIN[ix] + (((T_SIN[ix + 1] - T_SIN[ix]) * fx) >> 8);
        const int32_t w = (sv * sv) >> 15;
        wet[i] += (lp_mono(k, (uint32_t)(g->pos >> 16), (int32_t)((g->pos >> 8) & 255)) * w) >> 15;
        g->pos += g->inc;
        g->ph += g->fall ? g->down : g->up;
        if (g->ph >= LP_Q24) { if (g->fall) g->on = 0; else { g->fall = 1; g->ph -= LP_Q24; } }
    }
}

/* each synth on a loop: its loop at this pass's samples (Pitch's taps or the playhead, the cloud mixed in), through its
 * filter, into its voices' sum (vbuf[0]) */
static void lp_play(int at, int n)
{
    if (!lp_run_ok) return;
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const synth_t *p = &syn[s];
        const int k = p->lp - 1;
        if (k < 0 || !lps[k].len || (lps[k].state != LS_PLAY && lps[k].state != LS_ODUB_ARMED && lps[k].state != LS_ODUB))
            continue;                                                 /* (armed for a new one: the old frames not heard) */
        const uint8_t *v = lpv[s];
        const int sp = v[LV_SPEED] < LP_SPEED_N ? v[LV_SPEED] : LP_SPEED_ONE, rev = v[LV_REV];
        const int pitch = clampi(v[LV_PITCH], 0, 2 * LP_PITCH_MID) - LP_PITCH_MID, cloud = v[LV_CLOUD];
        const int64_t ratio = T_SEMI[48 + pitch], num = LP_SPEED_NUM[sp] * (rev ? -1 : 1), den = LP_SPEED_DEN[sp];
        const int32_t dphi = (int32_t)(((ratio - 65536) * num * 65536) / (den * LP_SHIFT_W));
        const int32_t inc = (int32_t)(ratio * num / den);             /* a grain's step, Q16: Speed and Pitch */
        voice_t *o = &lp_vf[s];
        lp_cloud_t *c = &lpc[s];
        int32_t gnorm = 32768, gdry = 0, gwet = 0, every = 0;
        if (!pitch) c->phi = 0;
        if (!cloud) for (int j = 0; j < LP_GRAINS; j++) c->g[j].on = 0;
        else {                                                        /* grains overlapping: their sum held by power */
            const uint32_t rate = lp_rate(v[LV_DENS] < LP_DENS_N ? v[LV_DENS] : 0);
            uint32_t size = lp_size(v[LV_SIZE] < LP_SIZE_N ? v[LV_SIZE] : 0);
            if (size > lps[k].len) size = lps[k].len;
            uint64_t over = (uint64_t)rate * size / DMK_SR;
            if (over > (uint64_t)LP_GRAINS << 8) over = (uint64_t)LP_GRAINS << 8;
            if (over > LP_OVER_FREE) gnorm = (int32_t)isqrt32((uint32_t)((1ull << 39) / over));
            gwet = T_SIN[cloud * 256 / LP_AMT_ALL]; gdry = T_SIN[256 - cloud * 256 / LP_AMT_ALL];   /* (by power, as
                                                           Clouds blends: grains and loop apart, no dip at the middle) */
            every = (int32_t)((uint32_t)DMK_SR * 256u / rate);
        }
        if (!v[LV_FREEZE]) c->frz_on = 0;
        else if (!c->frz_on) { c->frz = lp_frame_q8(k, lp_t(at), LP_SPEED_NUM[sp], LP_SPEED_DEN[sp], rev); c->frz_on = 1; }
        for (int b = 0; b < n / DMK_SUB; b++) {
            int idx = clampi(p->cut + lfo_cut[s][at / DMK_SUB + b], 0, 255);
            int32_t f = T_SVF[idx], f2 = 0, q = p->resq, q1 = q < 11469 ? 11469 : q;
            const int open = (p->ftype == FT_LP24 || p->ftype == FT_LP12) && idx >= 240 ? idx - 239 : 0;
            if (p->ftype == FT_VOWEL) {
                const int pos = idx * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
                f = T_SVF[(VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8];
                f2 = T_SVF[(VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8];
            }
            int32_t xs[DMK_SUB], wet[DMK_SUB];
            uint32_t fq0 = 0;
            for (int i = 0; i < DMK_SUB; i++) {
                const uint64_t t = lp_t(at + b * DMK_SUB + i);
                const uint32_t fq = lp_frame_q8(k, t, LP_SPEED_NUM[sp], LP_SPEED_DEN[sp], rev);
                if (!i) fq0 = fq;
                if (pitch) { xs[i] = lp_shifted(k, c->phi, fq); c->phi += (uint32_t)dphi; }
                else xs[i] = lp_mono(k, fq >> 8, (int32_t)(fq & 255u));
            }
            if (cloud) {
                c->wait -= DMK_SUB;
                if (c->wait <= 0) {                                   /* (every 0.5x to 1.5x its interval) */
                    lp_grain_start(s, k, c->frz_on ? c->frz : fq0, inc);
                    c->wait += (int32_t)(((int64_t)every * (128 + (lp_rand(&c->rnd) & 255))) >> 8);
                    if (c->wait <= 0) c->wait = every;
                }
                memset(wet, 0, sizeof wet);
                for (int j = 0; j < LP_GRAINS; j++) if (c->g[j].on) lp_grain_run(k, &c->g[j], wet, DMK_SUB);
                for (int i = 0; i < DMK_SUB; i++)
                    xs[i] = (int32_t)(((int64_t)xs[i] * gdry + (((int64_t)wet[i] * gnorm) >> 15) * gwet) >> 15);
            }
            int32_t *out = &vbuf[0][s][b * DMK_SUB];
            for (int i = 0; i < DMK_SUB; i++) {
                int32_t x = (xs[i] * LP_BACK_Q8) >> 8, y;
                if (p->drive > 256) x = (curve(T_SAT, (x * p->drive) >> 8) * p->dcomp) >> 8;
                y = vfilt(o, p->ftype, x, f, f2, q, q1);
                if (open) y += ((x - y) * open) >> 4;
                if (y > 49152 || y < -49152) y = curve(T_LIM, y / 2) * 2;
                out[i] += y;
            }
        }
    }
}

/* the mix each pass (lp_mix, dsp_render's mix gathers it): every synth not playing a loop being written, as it goes
 * into the mix */
static void lp_pass_begin(int n)
{
    lp_writing = 0;
    for (int s = 0; s < DMK_SYNTHS; s++) lp_skip[s] = 0;
    for (int k = 0; k < LP_N; k++) {
        if (lps[k].state != LS_REC && lps[k].state != LS_ODUB && lps[k].state != LS_ARMED && lps[k].state != LS_ODUB_ARMED) continue;
        lp_writing = 1;
        for (int s = 0; s < DMK_SYNTHS; s++) if (syn[s].lp == k + 1) lp_skip[s] = 1;
    }
    (void)n;
    if (lp_writing) memset(lp_mix, 0, sizeof lp_mix);
}

static int16_t lp_sat(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

/* the loops being written, this pass: armed ones start on their bar, a recording ends after one pass of its length */
static void lp_write(int at, int n)
{
    if (!lp_run_ok || !lp_writing) return;
    for (int k = 0; k < LP_N; k++) {
        if (lps[k].state == LS_EMPTY || lps[k].state == LS_PLAY) continue;
        const uint64_t L = (uint64_t)lps[k].steps << 32, end = lps[k].start + L;
        const int32_t g = lps[k].gain * 256 / LP_GAIN_ONE, keep = lps[k].keep * 32767 / LP_KEEP_ALL;
        for (int i = 0; i < n; i++) {
            const uint64_t t = lp_t(at + i);
            if (t < lps[k].start) continue;
            if (t >= end) {                             /* one pass: played from now on */
                lps[k].state = LS_PLAY;
                lps[k].ver++;
                break;
            }
            if (lps[k].state == LS_ARMED) lps[k].state = LS_REC;
            if (lps[k].state == LS_ODUB_ARMED) lps[k].state = LS_ODUB;
            const uint32_t f = ((lp_frame_q8(k, t, 1, 1, 0) + 128u) >> 8) % lps[k].len;   /* (the nearest frame: a pass
                                                           starts on a frame exactly, its samples a hair under each
                                                           one after; rounded down, a block's edge skipped one) */
            int32_t il, ir;
            if (lps[k].src == LSRC_JACK) { il = lp_jack ? lp_jack[(at + i) * 2] : 0; ir = lp_jack ? lp_jack[(at + i) * 2 + 1] : 0; }
            else { il = lp_mix[0][i]; ir = lp_mix[1][i]; }
            il = (il * g) >> 8; ir = (ir * g) >> 8;
            int16_t *w = lp_buf[k][f];
            if (lps[k].state == LS_ODUB) { il += (w[0] * keep) >> 15; ir += (w[1] * keep) >> 15; }
            w[0] = lp_sat(il); w[1] = lp_sat(ir);
            const int c = (int)((uint64_t)f * LP_PEAKS / lps[k].len), a = (w[0] < 0 ? -w[0] : w[0]) >> 7;
            if (a > lps[k].peak[c]) lps[k].peak[c] = (uint8_t)(a > LP_PEAK_MAX ? LP_PEAK_MAX : a);
        }
    }
}

/* REC on a loop's Sound page (synth s): empty, it records on the next bar; playing, it overdubs a pass on the next bar;
 * armed, it lets go. Stopped, the next bar is the first: PLAY starts it */
static void lp_rec_press(int s)
{
    const int k = syn[s].lp - 1;
    if (k < 0 || !lp_ready) return;
    if (lp_wait[k]) { notice(lp_wait_op[k] == LJ_LOAD ? "Loop loading" : "Loop saving"); return; }
    if (lps[k].state == LS_ODUB_ARMED) { lps[k].state = LS_PLAY; return; }
    if (lps[k].state == LS_ARMED) { lps[k].state = LS_EMPTY; lps[k].len = 0; lps[k].steps = 0; return; }
    if (lps[k].state == LS_REC || lps[k].state == LS_ODUB) return;   /* (a pass under way finishes) */
    const uint8_t *v = lpv[s];
    const uint32_t per = seq_per();
    uint64_t next = 0;
    if (seq_on && seq_ticks) {
        const uint64_t now = ((uint64_t)(seq_ticks - 1u) << 32) + ((uint64_t)seq_acc << 32) / per, bar = (uint64_t)LP_STEPS_BAR << 32;
        next = (now + bar - 1) / bar * bar;
    }
    lps[k].src = v[LV_SRC]; lps[k].keep = v[LV_KEEP]; lps[k].gain = v[LV_GAIN];
    lps[k].start = next;
    if (lps[k].state == LS_EMPTY || !lps[k].len) {        /* a new recording: its length from the tempo, within LP_SEC */
        uint32_t steps = (uint32_t)LP_STEPS_BAR << (v[LV_BARS] < LP_BARS_N ? v[LV_BARS] : 0);
        while (steps > LP_STEPS_BAR && (uint64_t)steps * per / 256u > LP_FRAMES) steps /= 2;
        lps[k].steps = (uint16_t)steps;
        lps[k].len = (uint32_t)((uint64_t)steps * per / 256u);
        if (lps[k].len > LP_FRAMES) lps[k].len = LP_FRAMES;
        memset(lps[k].peak, 0, sizeof lps[k].peak);
        lps[k].state = LS_ARMED;
    } else lps[k].state = LS_ODUB_ARMED;
}
static void lp_clear(int s)             /* Delete + REC: silent, empty (its frames zeroed as a new one overwrites them) */
{
    const int k = syn[s].lp - 1;
    if (k < 0) return;
    if (lp_wait[k]) { notice(lp_wait_op[k] == LJ_LOAD ? "Loop loading" : "Loop saving"); return; }
    lps[k].state = LS_EMPTY; lps[k].len = 0; lps[k].steps = 0;
    lps[k].ver++;
    memset(lps[k].peak, 0, sizeof lps[k].peak);
}

/* sp_sync's: a synth newly on a loop takes its values */
static void lp_sync(void) { for (int s = 0; s < DMK_SYNTHS; s++) if (syn[s].lp && !lpv[s][LV_SET]) lp_vals_new(s); }
