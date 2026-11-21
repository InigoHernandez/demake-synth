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
 * (64 s of stereo each): they are touched once when Demake opens, never faulted in during a block. */

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

enum { LS_EMPTY, LS_ARMED, LS_REC, LS_PLAY, LS_ODUB_ARMED, LS_ODUB };   /* a loop's state */
enum { LV_SET, LV_SRC, LV_BARS, LV_KEEP, LV_SPEED, LV_REV, LV_GAIN, LV_COUNT };   /* the sound's loop values (lpv) */
_Static_assert(LV_COUNT == LV_N, "dmk_dsp.c's LV_N is the loop values' count");
enum { LSRC_MIX, LSRC_JACK };

static const uint8_t LP_SPEED_NUM[LP_SPEED_N] = {1, 1, 1, 2, 4}, LP_SPEED_DEN[LP_SPEED_N] = {4, 2, 1, 1, 1};

static int16_t lp_buf[LP_N][LP_FRAMES][2];
static struct {
    uint32_t len;                       /* its frames (0: empty) */
    uint16_t steps;                     /* its length in steps */
    uint8_t state, src, keep, gain;     /* LS_*; how it records: the recording synth's, taken at REC */
    uint64_t start;                     /* the bar (Q32 steps since PLAY) the recording or the overdub starts on */
    uint8_t peak[LP_PEAKS];
} lps[LP_N];
static voice_t lp_vf[DMK_SYNTHS];       /* each synth's filter state for its loop (vfilt's) */
static uint64_t lp_t0, lp_span;         /* the transport at the block's first sample, and how far it goes: Q32 steps */
static uint32_t lp_frames;              /* the block's samples */
static uint8_t lp_run_ok;               /* the transport has a place (playing, past PLAY) */
static uint8_t lp_ready;                /* the buffers touched */
static const int16_t *lp_jack;          /* the audio in this block (interleaved, the Move's; 0: none) */

static void lp_touch(void)              /* at create, off the audio thread: every page of the buffers in memory */
{
    memset(lp_buf, 0, sizeof lp_buf);
    memset(lps, 0, sizeof lps);
    lp_ready = 1;
}

static void lp_vals_new(int s)          /* a sound's loop values as first picked */
{
    uint8_t *v = lpv[s];
    v[LV_SET] = 1; v[LV_SRC] = LSRC_MIX; v[LV_BARS] = 2; v[LV_KEEP] = LP_KEEP_ALL * 3 / 4; v[LV_SPEED] = LP_SPEED_ONE;
    v[LV_REV] = 0; v[LV_GAIN] = LP_GAIN_ONE;
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

/* each synth on a loop: its loop at this pass's samples, through its filter, into its voices' sum (vbuf[0]) */
static void lp_play(int at, int n)
{
    if (!lp_run_ok) return;
    for (int s = 0; s < DMK_SYNTHS; s++) {
        const synth_t *p = &syn[s];
        const int k = p->lp - 1;
        if (k < 0 || !lps[k].len || lps[k].state == LS_REC) continue;
        const uint8_t *v = lpv[s];
        const int sp = v[LV_SPEED] < LP_SPEED_N ? v[LV_SPEED] : LP_SPEED_ONE, rev = v[LV_REV];
        voice_t *o = &lp_vf[s];
        for (int b = 0; b < n / DMK_SUB; b++) {
            int idx = clampi(p->cut + lfo_cut[s][at / DMK_SUB + b], 0, 255);
            int32_t f = T_SVF[idx], f2 = 0, q = p->resq, q1 = q < 11469 ? 11469 : q;
            const int open = (p->ftype == FT_LP24 || p->ftype == FT_LP12) && idx >= 240 ? idx - 239 : 0;
            if (p->ftype == FT_VOWEL) {
                const int pos = idx * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
                f = T_SVF[(VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8];
                f2 = T_SVF[(VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8];
            }
            int32_t *out = &vbuf[0][s][b * DMK_SUB];
            for (int i = 0; i < DMK_SUB; i++) {
                const uint64_t t = lp_t(at + b * DMK_SUB + i);
                const uint32_t fq = lp_frame_q8(k, t, LP_SPEED_NUM[sp], LP_SPEED_DEN[sp], rev);
                const uint32_t a0 = fq >> 8, a1 = a0 + 1 < lps[k].len ? a0 + 1 : 0;
                const int32_t fr = (int32_t)(fq & 255u);
                const int32_t m0 = (lp_buf[k][a0][0] + lp_buf[k][a0][1]) / 2, m1 = (lp_buf[k][a1][0] + lp_buf[k][a1][1]) / 2;
                int32_t x = ((m0 + (((m1 - m0) * fr) >> 8)) * LP_BACK_Q8) >> 8, y;
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
    if (lps[k].state == LS_ARMED || lps[k].state == LS_ODUB_ARMED) { lps[k].state = lps[k].len && lps[k].state == LS_ODUB_ARMED ? LS_PLAY : LS_EMPTY; return; }
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
    lps[k].state = LS_EMPTY; lps[k].len = 0; lps[k].steps = 0;
    memset(lps[k].peak, 0, sizeof lps[k].peak);
}

/* sp_sync's: a synth newly on a loop takes its values */
static void lp_sync(void) { for (int s = 0; s < DMK_SYNTHS; s++) if (syn[s].lp && !lpv[s][LV_SET]) lp_vals_new(s); }
