/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's sound (docs/plan.md, 2.1, 1.2, 1.3): the owner's recordings are the core. The sampler reads them as 4-bit
 * block ADPCM straight from flash, pitched by a Q16 phase with linear interpolation, looped where tools/gen_samples.py
 * found the seam; each voice has a state-variable filter of eight types and an envelope. Each synth has its MOTION
 * (chorus and the like; vibrato and drift on the voices' pitch), its DELAY (a delay, a ping-pong, the tape echo and the
 * grains: an 8-bit mu-law line, its loop filtered and clipped) and its own REVERB (the owner, 2026-10-25: ROOM, PLATE,
 * SPRING and a small HALL, at half rate); one shared SPACE, the huge reverbs (the owner, 2026-10-26: an eight-line
 * network at half rate that every synth sends to); then on the whole mix its COLOUR (saturate, crush, lossy, noise,
 * warm, distort) and its MEDIA (tape, cassette, VHS, vinyl, radio, dictaphone), and a soft limiter. 12 voices shared
 * by the three synths, half of them on the second core when there is one. Integer only. Included by dmk_core.c. */

#define FULL (1 << 24)
#define XG_STEP (32768 / 16)                            /* a type change's fade, a block's step of the share: 16 blocks */

#ifdef DMK_METER                                        /* the host's meters: where a sound runs out of room */
static struct { int32_t pre_max, syn_max, voice_max; uint32_t knee, sat_line, sat_rv, sat_sp, clamp_v; } dmeter;
#define METER(x) (x)
#else
#define METER(x) ((void)0)
#endif
static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a one-pole's step towards x, (x - s) c / 2^15 (c Q15), or (x - s) / 2^sh: at least one either way, so it reaches x
 * and stays there (a floored step stalls short of it, below zero: a tail held on that offset for ever, made louder by
 * whatever gain comes after) */
static inline int32_t op_step(int32_t s, int32_t x, int32_t c) { int32_t t = (x - s) * c; return s + ((t + ((~t >> 31) & 32767)) >> 15); }
static inline int32_t op_sh(int32_t s, int32_t x, int sh) { int32_t t = x - s; return s + ((t + ((~t >> 31) & ((1 << sh) - 1))) >> sh); }
/* (rounded away from zero: up a step going up, down going down, never past x) */

static inline int32_t sinp(uint32_t ph)                 /* Q15, linear between the table's points */
{
    uint32_t i = ph >> 22, f = (ph >> 6) & 0xFFFFu;
    int32_t a = T_SIN[i], b = T_SIN[i + 1];
    return a + (((b - a) * (int32_t)f) >> 16);          /* |b - a| <= 202: no overflow */
}

/* -131072..131072 in, through a 257-point curve (the limiter's, the tape's) */
static inline int32_t curve(const int16_t *t, int32_t x)
{
    int32_t u, i, f;
    if (x >= 131071) return t[256];
    if (x <= -131071) return t[0];
    u = x + 131072; i = u >> 10; f = u & 1023;
    return t[i] + (((t[i + 1] - t[i]) * f) >> 10);
}

/* ---- a synth's sound, in the units the voices use; sp_sync (dmk_ui.c) sets it from the controls */
enum { FT_LP24, FT_LP12, FT_HP, FT_BP, FT_NOTCH, FT_PEAK, FT_VOWEL, FT_OFF };
/* each row's types; the later ones after, so a stored type keeps its place (dmk_store's version 6) */
enum { FC_OFF, FC_SAT, FC_CRUSH, FC_LOSSY, FC_NOISE, FC_WARM, FC_DIST };                  /* COLOUR */
#define FC_HEAVY FC_LOSSY                               /* the costliest colour (the load test's, --cpu's): measured */
enum { FM_OFF, FM_TAPE, FM_CASS, FM_VHS, FM_VINYL, FM_RADIO, FM_DICTA };                  /* MEDIA */
enum { FO_OFF, FO_CHORUS, FO_VIB, FO_DRIFT, FO_ENS, FO_PHASER, FO_FLANGER, FO_TREM };      /* MOTION */
enum { RV_OFF, RV_ROOM, RV_PLATE, RV_SPRING, RV_HALL, RV_N };   /* each synth's REVERB (FX's first row, since 0.4.17) */
#define RV_HEAVY RV_PLATE                               /* the costliest (the load test's, --cpu's): measured */
enum { DT_OFF, DT_DELAY, DT_PING, DT_TAPE, DT_GRAINS, DT_N };   /* each synth's DELAY (the shared echo's two since 0.4.18) */
#define DT_HEAVY DT_GRAINS                              /* (measured: 86 a synth, the plain delay 53) */
enum { SP_OFF, SP_HALL, SP_SPACE, SP_HAZE, SP_BLOOM, SP_LOFI, SP_SHIMMER, SP_REVERSE, SP_FREEZE, SP_N };   /* the SPACE's */
#define SP_HEAVY SP_REVERSE                             /* (measured: 287 a sample, the hall 244) */
typedef struct {
    uint8_t smp, loops, ftype, tape;
    int16_t tune, cut, fenv;       /* semitones; the cutoff's index 0..255 (255: open); the envelope's reach in indexes */
    int8_t fine;                   /* cents, -50..50 */
    uint16_t start, resq, dec, rel;   /* blocks; damping Q14; decay and release factors Q16 a block */
    uint32_t atk;                  /* the attack's step a block, Q24 */
    int32_t sus, level, gl, gr, ssend;   /* Q24; Q15 ... (ssend: to the SPACE) */
    int32_t pan;                   /* 0..127 as turned (the LFO swings it) */
    int32_t width;                 /* its stereo's width, Q8 (256 as it is, 0 one, 508 twice) */
    int32_t tdepth, tdrive, tcomp; /* the tape's wow depth (Q8 samples a unit of the sine), drive Q8, its makeup Q15 */
    uint16_t thl;                  /* its head loss, a one-pole Q15 */
    uint32_t twow, tflut;          /* its wow's and flutter's phase steps */
    uint8_t colour, media, motion; /* the rows' effects (FC_, FM_, FO_), 0 off (colour and media: the whole mix's, gcol's
                                      and gmed's) */
    uint8_t dtype, ddiv;           /* the DELAY (FX row 2, the owner's: one a synth): its type (DT_); its division */
    int32_t dmix, dtone, dfb;      /*   its wet share Q15, its tone (a one-pole), its regen Q15 */
    uint32_t dtime;                /*   (dmk_seq: its time, quarter-rate samples, of the tempo)  */
    uint32_t etime;                /*   the TAPE ECHO's, free: quarter-rate samples */
    int32_t glen, gdens, gtex;     /*   the GRAINS': length (quarter-rate samples), a grain's step a sample (Q16), texture */
    uint8_t rtype, rsize, rx;      /* the REVERB (FX's first row): its type (RV_), SIZE and its type's own knob, 0..127 */
    int32_t ramt, rdamp;           /*   how much goes in, Q15 (its AMOUNT); its tone, a one-pole Q15 */
    uint8_t ck[4], mk[4], ok[4];   /* their knobs, 0..127 as turned */
    /* the LAYER: its recording, loop and tune (semitones), its share of the voice (Q15, 0: off) and tone (a one-pole) */
    uint8_t lsmp, lloops;
    int16_t ltune;
    int32_t lblend, ltone;
    /* VOICE: glide's factor a block (Q16, 0: none), velocity's reach (0..127), the cutoff's tracking of the key
     * (indexes a semitone, Q8), the voices' rate (GRIT: 0 full, 1 half, 2 a quarter); FILTER's drive (Q8, 256 none)
     * and the level it gives back (Q8) */
    uint16_t glide;
    uint8_t vel, grit;
    int16_t track;
    int32_t drive, dcomp;
    /* the LFO: its shape, target, sync (sixteenths a cycle, 0 free) and retrigger; its free step a block, depth (Q15
     * on a straight law; the pitch squares it) and fade (blocks to full) */
    uint8_t lshape, ltarget, lsync, lretrig;
    uint32_t lrate;
    int32_t ldepth;
    uint16_t lfade;
} synth_t;
enum { LT_PITCH, LT_CUTOFF, LT_LEVEL, LT_PAN, LT_START };         /* the LFO's targets */
static synth_t syn[DMK_SYNTHS];

/* the shared SPACE's controls (sp_sync sets them from GLO's Space row) and the mix's width: its return (Q15), its
 * damping (a one-pole, Q15), its tank's two halves' decays (Q15), its sweep's depth (Q8 steps) and rate, FREEZE's
 * share of new sound in (Q15), SHIMMER's octave fed back (Q15), REVERSE's segment (half-rate steps) and its reciprocal
 * (Q24 over it), the type's level (Q8) */
static struct { int32_t sret, sdamp, sga, sgb, smod, sgin, sshine, srlen, srinv, sgain, width; uint32_t srate; uint8_t stype; } gp;

/* ---- the voices */
/* a recording as a voice reads it: its ADPCM blocks decoded 28 at a time, the predictor carried on; on until a
 * one-shot ends */
typedef struct {
    const dmk_sample_t *s;
    uint32_t ph, inc, inc0;
    int32_t a, b, s1, s2;
    uint16_t blk, end;
    uint8_t bi, loop, on;
    int16_t buf[28];
} strm_t;
typedef struct {
    strm_t m, l;                   /* the sound's recording; its layer's (l.on: blended in) */
    uint32_t age, rng;
    int32_t env, lp, bp, lp2, bp2, lastv;   /* lastv: its last sample out, for a steal's fade */
    int32_t ltp, gl, vg;           /* the layer's tone; glide's way still to go (cents Q4); velocity's level (Q15) */
    int16_t drift, dgoal, det;     /* MOTION's drift, its goal, the note's own detune (cents Q4) */
    int16_t kcut;                  /* the cutoff's offset for this note: velocity and the key's tracking, indexes */
    uint8_t lay;                   /* the note has a layer: blended to its end and past it (to silence, not cut) */
    uint16_t blocks;               /* blocks since the note began: vibrato's fade-in */
    uint8_t stage, note, sy, on, src, vel;   /* stage: 0 silent, 1 attack, 2 decay and sustain, 3 release */
} voice_t;
static voice_t vc[DMK_VOICES];
/* the render's passes: up to VB_MAX frames, the voices first, then each synth's chain in place, vbuf[0] its left after,
 * vbuf[1] its right */
#define VB_MAX 256
static int32_t vbuf[2][DMK_SYNTHS][VB_MAX] POOL;

static uint32_t vc_clock;
/* who started a voice: the keys, a roll, the chord machine; a note off ends only its own source's (the progression's
 * chord change never cuts a note the hands hold, nor the reverse) */
enum { SRC_KEY, SRC_ROLL, SRC_CHORD, SRC_ARP, SRC_MIDI };   /* (MIDI in's: never sent back out, or a loop sang on) */
static int arp_take(int sy, int n, int vel);            /* (dmk_seq.c: a synth's arp takes its notes, plays them its way) */
static int arp_drop(int sy, int n);
static int32_t vc_tail[DMK_SYNTHS];                     /* a stolen voice's last level, fading out (~1.5 ms) */
/* MIDI out (dmk_midi_take; GLO's MIDI row): the notes each synth starts and ends on its channel (1-3), and the clock
 * while playing, queued for the platform to send from its loop */
#define MQ_LEN 128u
static uint8_t mq[MQ_LEN][3];
static uint32_t mq_w, mq_r;
static uint8_t midi_out, midi_chans, clock_ext;         /* out: bit 0 notes, bit 1 the clock; in: by channel; the clock in */
static void mq_put(int st, int d1, int d2)
{
    if (mq_w - mq_r >= MQ_LEN) return;                  /* nobody reading: dropped, not blocking */
    mq[mq_w % MQ_LEN][0] = (uint8_t)st; mq[mq_w % MQ_LEN][1] = (uint8_t)(d1 & 127); mq[mq_w % MQ_LEN][2] = (uint8_t)(d2 & 127);
    mq_w++;
}
/* the song's fades (dmk_seq.c sets them): the synths' share of the mix, Q15, and its step a block; the spaces' tails
 * are left to ring */
static int32_t mix_g = 32768, mix_dg, mix_next;          /* mix_next: the fade after this one ends (0: none) */
/* the machine's state for GLO: the platform's load (dmk_status) and the times the mix went past full (the limiter
 * bending it: what sounds like overload) */
static struct { uint8_t cpu, core2, screen, on; uint32_t shorts, loud; } sys;

static const int8_t ADPCM_F[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* a block of 28 samples from its 16 bytes, the predictor's state carried on */
static void blk_decode(strm_t *o)
{
    const uint8_t *d = o->s->data + (uint32_t)o->blk * 16u;
    int f = d[0] >> 4, sh = d[0] & 15, i;
    int32_t f0 = ADPCM_F[f > 4 ? 0 : f][0], f1 = ADPCM_F[f > 4 ? 0 : f][1], s1 = o->s1, s2 = o->s2;
    for (i = 0; i < 28; i++) {
        int32_t q = (d[2 + (i >> 1)] >> ((i & 1) * 4)) & 15, v;
        if (q & 8) q -= 16;
        v = sat16(((q * 4096) >> sh) + ((s1 * f0 + s2 * f1 + 32) >> 6));
        o->buf[i] = (int16_t)v;
        s2 = s1; s1 = v;
    }
    o->s1 = s1; o->s2 = s2;
}

static int32_t smp_next(strm_t *o)
{
    if (o->bi >= 28) {
        if (++o->blk >= o->end) {
            if (!o->loop) { o->on = 0; return 0; }
            o->blk = o->s->loop0; o->s1 = o->s->ls1; o->s2 = o->s->ls2;
        }
        blk_decode(o);
        o->bi = 0;
    }
    return o->buf[o->bi++];
}
/* the sound's own recording: as smp_next, and its end (a one-shot's) ends the voice */
static int32_t smp_next_m(voice_t *v)
{
    strm_t *o = &v->m;
    if (o->bi >= 28) {
        if (o->blk + 1u >= o->end) {
            if (!o->loop) {                             /* a one-shot's end: the voice's, unless its layer still sounds */
                o->on = 0; o->blk = o->end;
                if (!(v->lay && v->l.on)) { v->stage = 0; v->env = 0; }
                return 0;
            }
            o->blk = o->s->loop0; o->s1 = o->s->ls1; o->s2 = o->s->ls2;
        } else o->blk++;
        blk_decode(o);
        o->bi = 0;
    }
    return o->buf[o->bi++];
}

/* a voice's step: at most four of the recording's samples an output sample. Two octaves up from where it was recorded
 * is as high as a note reads (past it the recording aliases into noise, and each step decodes more: CPU grew with the
 * transposition, 16 x at the top) */
#define INC_MAX (4u << 16)
/* a stream at the start of recording s, for note n moved by tune semitones and fine cents, from block start */
static void strm_start(strm_t *t, const dmk_sample_t *s, int n, int tune, int fine, int loops, int start)
{
    int c = fine - s->cents, k;                         /* the cents to move it, -100..100: past half a semitone, the
                                                           semitone carried (clamped, FINE stopped at the recording's
                                                           own cents) */
    uint32_t inc;
    k = n - s->root + tune + (c > 50 ? 1 : c < -50 ? -1 : 0);
    c -= c > 50 ? 100 : c < -50 ? -100 : 0;
    k = clampi(k, -48, 48);
    inc = (uint32_t)(((uint64_t)s->rate_q16 * T_SEMI[k + 48]) >> 16);
    inc = (uint32_t)(((uint64_t)inc * T_CENT[50 + c]) >> 16);
    t->inc = t->inc0 = inc > INC_MAX ? INC_MAX : inc;
    t->s = s;
    t->loop = (uint8_t)(s->loops && loops);             /* LOOP OFF: a looped sample plays through once */
    t->end = t->loop ? s->loop1 : s->blocks;
    t->blk = (uint16_t)(start < t->end - 1 ? start : 0);
    t->s1 = t->s2 = 0;
    t->on = 1;
    blk_decode(t);
    t->bi = 0;
    t->a = smp_next(t);
    t->b = smp_next(t);
    t->ph = 0;
}

/* the LFO, a synth's: its phase, its random value, its smoothed output, blocks since the phrase began; and each
 * block's value with its depth and fade (lfo_c), and the depth alone (lfo_g), Q15, which both cores read */
static struct { uint32_t ph, rng, sstep, poff; int32_t hold, sm, gs, fade, lv, pl, pr, cut, pit; uint8_t rst; } lfo[DMK_SYNTHS];
static int16_t lfo_c[DMK_SYNTHS][16], lfo_g[DMK_SYNTHS][16];   /* VB_MAX / DMK_SUB blocks a run */
/* and what the voices take from it a block, slewed so a target changed glides: the cutoff's offset (indexes), the
 * pitch's (cents Q4) */
static int16_t lfo_cut[DMK_SYNTHS][16];
static int32_t lfo_pit[DMK_SYNTHS][16];
static uint8_t syn_last[DMK_SYNTHS];                    /* each synth's last note + 1, for the glide (0: none yet) */

static void note_on_vel(int sy, int n, int src, int vel)
{
    int i, best = -1, first = 1;
    uint32_t oldest = 0xFFFFFFFFu;
    int32_t quiet = 0x7FFFFFFF, st;
    const synth_t *p = &syn[sy];
    voice_t *o;
    if (n < 0 || n > 127 || sy < 0 || sy >= DMK_SYNTHS) return;
    vel = clampi(vel, 1, 127);
    if (src != SRC_ARP && arp_take(sy, n, vel)) return;   /* the ARP's: the note into its pool, it plays them */
    for (i = 0; i < DMK_VOICES; i++) if (vc[i].stage && vc[i].sy == sy) first = 0;
    if (first) {                                        /* a phrase begins (from silence): the LFO's fade from nothing,
                                                           its phase too (synced: from here, lfo_sync) */
        lfo[sy].fade = 0;
        if (p->lretrig) { lfo[sy].ph = 0; lfo[sy].rst = 1; }
    }
    for (i = 0; i < DMK_VOICES; i++) if (vc[i].stage == 0) { best = i; break; }   /* a silent voice */
    if (best < 0) {                                     /* else the quietest released one, else the oldest held */
        for (i = 0; i < DMK_VOICES; i++) if (!vc[i].on && vc[i].env < quiet) { quiet = vc[i].env; best = i; }
        if (best < 0) for (i = 0; i < DMK_VOICES; i++) if (vc[i].age < oldest) { oldest = vc[i].age; best = i; }
        vc_tail[vc[best].sy] += vc[best].lastv;         /* what it was sounding fades out, not cut */
    }
    o = &vc[best];
    st = p->start;
    if (p->ltarget == LT_START && p->ldepth) {          /* the LFO moves where each note starts, around START, faded */
        const dmk_sample_t *sm = &DMK_SAMPLES[p->smp < DMK_SAMPLES_N ? p->smp : 0];
        int32_t v = (lfo[sy].sm * lfo[sy].gs) >> 15, end = sm->loops && p->loops ? sm->loop1 : sm->blocks;
        st = clampi(st + ((v * ((sm->loops ? sm->loop0 : sm->blocks) / 2)) >> 15), 0, end - 2);
    }
    strm_start(&o->m, &DMK_SAMPLES[p->smp < DMK_SAMPLES_N ? p->smp : 0], n, p->tune, p->fine, p->loops, st);
    o->l.on = 0;
    o->lay = (uint8_t)(p->lblend != 0);
    if (o->lay) strm_start(&o->l, &DMK_SAMPLES[p->lsmp < DMK_SAMPLES_N ? p->lsmp : 0], n, p->tune + p->ltune, p->fine, p->lloops, 0);
    o->ltp = 0;
    o->rng = o->rng * 1664525u + 1013904223u + (uint32_t)n;
    o->det = p->motion == FO_DRIFT ? (int16_t)((int32_t)((o->rng >> 16) % 801u) - 400) * p->ok[1] / 127 : 0;
    o->drift = o->dgoal = 0;
    o->blocks = 0;
    /* VOICE: glide from the synth's last note; velocity to the level (a square law) and the cutoff; the key's tracking */
    o->gl = p->glide && syn_last[sy] ? clampi((syn_last[sy] - 1 - n) * 1600, -48 * 1600, 48 * 1600) : 0;
    syn_last[sy] = (uint8_t)(n + 1);
    o->vel = (uint8_t)vel;
    o->vg = 32768 - p->vel * (32768 - vel * vel * 2) / 127;
    o->kcut = (int16_t)(-(p->vel * (127 - vel) * 48 / 16129) + (((n - 60) * p->track) >> 8));
    o->env = 0; o->stage = 1;
    o->lp = o->bp = o->lp2 = o->bp2 = 0;
    o->note = (uint8_t)n; o->sy = (uint8_t)sy; o->on = 1; o->age = vc_clock++; o->src = (uint8_t)src; o->lastv = 0;
    if (midi_out & 1u && src != SRC_MIDI) mq_put(0x90 | sy, n, vel);
}
#define VEL_KEY 100                                     /* the keys' velocity, as the roll writes it */
static void note_on_src(int sy, int n, int src) { note_on_vel(sy, n, src, VEL_KEY); }

/* a note off: one voice, the oldest of that note from that source (a second key, an octave apart, or a chord sharing
 * a note, keeps its own: letting one go cut the other) */
static void note_off_src(int sy, int n, int src)
{
    int i, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = 0; i < DMK_VOICES; i++)
        if (vc[i].on && vc[i].note == n && vc[i].sy == sy && vc[i].src == src && vc[i].age < oldest) { oldest = vc[i].age; best = i; }
    /* its own voice first; none, one the arp took (the arp asked first ate another's off: a key held as the arp came on
     * stuck, its pitch in the pool from the roll) */
    if (best < 0 && src != SRC_ARP && sy >= 0 && sy < DMK_SYNTHS && arp_drop(sy, n)) return;
    if (midi_out & 1u && src != SRC_MIDI && n >= 0 && n < 128) mq_put(0x80 | sy, n, 0);
    if (best >= 0) { vc[best].on = 0; if (vc[best].stage) vc[best].stage = 3; }
}
static void note_on_s(int sy, int n) { note_on_src(sy, n, SRC_KEY); }   /* the keys' */
static void note_off_s(int sy, int n) { note_off_src(sy, n, SRC_KEY); }

/* the filter: a Chamberlin state-variable, one stage or two; 64-bit products (resonance runs past 16 bits) */
static inline int32_t svf_step(int32_t *lp, int32_t *bp, int32_t x, int32_t f, int32_t q, int32_t *hp_out)
{
    int32_t hp;
    *lp += (int32_t)(((int64_t)f * *bp) >> 15);
    hp = x - *lp - (int32_t)(((int64_t)q * *bp) >> 14);
    *bp += (int32_t)(((int64_t)f * hp) >> 15);
    *lp = clampi(*lp, -(1 << 20), 1 << 20);
    *bp = clampi(*bp, -(1 << 20), 1 << 20);
    *hp_out = hp;
    return *lp;
}

/* ---- each synth's effects, after its voices: COLOUR, MEDIA, MOTION (docs/plan.md, 1.2); knobs 0..127 */
static inline uint32_t xs32(uint32_t *r) { uint32_t x = *r; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *r = x; }

#define TP_LEN 2048u                                    /* MEDIA's line: 46 ms, read back by wow and flutter */
#define CH_LEN 1024u                                    /* MOTION's line: the chorus's 23 ms */
static int16_t tp_buf[2][TP_LEN] POOL;                  /* (since 0.5 one MEDIA, on the mix: its left, its right) */
static int16_t ch_buf[DMK_SYNTHS][CH_LEN] POOL;
static struct {
    uint32_t tw, wow, flut, rng, wdrift, warb, tick, cw, clfo, hold_ph, vib;
    int32_t hl, lp1, lp2, hp, fol, drop, drop_g, track, track_g, wd, dd, toff, tenv, jit, nb;   /* MEDIA */
    int32_t clp, hold, sl, band[2], peak[3], step[3], frame, crk, nfol, nlp, sdc, lg[3];   /* COLOUR */
    uint32_t hum;                                       /* (its phase: it wraps) */
    int32_t chlp[2];                                    /* MOTION */
    int32_t wlo, wpre, wtl, wdc, dpre, dg, dfi, dfo;    /* WARM; DISTORT, its level's gain (Q8) and what it hears */
    uint32_t mrng;                                      /* MEDIA's dice (its own: the mix's two sides throw the same) */
    int32_t ng, nfl, mcrk, md;                          /* a medium's own noise: its gain, the follower it rides (Q8), a crackle;
                                                           the later media's delay (Q8), as the block began */
    int32_t vlp[2], vrm[2];                             /* VINYL: the top rolled off, the rumble */
    uint32_t vph, mph, fph;                             /* the platter; the dictaphone's motor, its capstan */
    int32_t rhp[2], rlp[4], rdr, rdt, rcnt;             /* RADIO: the band; the drift, where it goes, when it turns */
    int32_t agc, agc_g;                                 /* DICTAPHONE: the level it hears, its gain (Q8) */
    int32_t px[2][4], py[2][4], pfb[2];                 /* PHASER: each side's stages, the fed back */
    int32_t fdc[2], ftl[2];                             /* FLANGER: the DC of each side's copy, its tone */
    uint32_t eph[2];                                    /* ENSEMBLE: the slow sweep, the fast */
    int32_t ed[3];                                      /*   each tap's delay (Q8), as the block began */
    uint32_t tph, tstep;                                /* TREMOLO: its place, its step a block (dmk_seq: the tempo's) */
    int32_t tg[2];                                      /*   each side's gain, as the block began */
    int32_t xg[3];                                      /* each row's share while its type changes, Q15 */
    uint8_t gate, cur[3];                               /* the holes; the type each row runs now */
} fx[DMK_SYNTHS];

/* COLOUR (since 0.4.17 the whole mix's, the owner's: a side in each slot, gcol's settings): SATURATE (drive into the
 * tape's curve, a bias for even harmonics, a tone after), CRUSH (bits, a rate held by a jittered clock, a low-pass
 * before), LOSSY (three bands, each quantized by its last frame's peak, the weak and random ones dropped: a codec's
 * smear and holes), NOISE (hiss, crackle and hum riding the sound) */
static void colour_block(int sy, const synth_t *p, int32_t *x, int type)   /* (sy: the state's slot; p: its settings) */
{
    int i, k0 = p->ck[0], k1 = p->ck[1], k2 = p->ck[2], k3 = p->ck[3];
    int32_t tone = T_OP[64 + k1 / 2];
    if (type == FC_SAT) {
        int32_t drive = 256 + k0 * 14, bias = k2 * 60, base = curve(T_SAT, bias), mix = k3 * 258;   /* (the bias after
                                                           the drive: driven, it took the curve to its flat top, 14 dB
                                                           down at both full) */
        for (i = 0; i < DMK_SUB; i++) {
            int32_t y = curve(T_SAT, ((x[i] * drive) >> 8) + bias) - base;
            y = y * 256 / (160 + drive / 3 + (drive - 256) * 2 / 7);   /* about the level it went in at (0.4.11: driven
                                                           hard it was 4 dB over) */
            fx[sy].sdc = op_sh(fx[sy].sdc, y * 256, 10); /* the bias's DC, out (~7 Hz, Q8) */
            y -= fx[sy].sdc >> 8;
            fx[sy].clp = op_step(fx[sy].clp, y, tone);
            x[i] += ((fx[sy].clp - x[i]) * mix) >> 15;
        }
    } else if (type == FC_CRUSH) {
        int sh = k0 * 10 / 127, k = 127 - k2;           /* (to 6 bits: past them a mix's quiet parts were all gone, 16
                                                           dB down) */
        uint32_t step = 1800u + (uint32_t)(k * k) * 4u; /* (held at 1.2 kHz at the most: lower, 12 dB down) */
        int32_t post = step * 2u > 32767u ? 32767 : (int32_t)(step * 2u);   /* ~0.6 of the held rate */
        for (i = 0; i < DMK_SUB; i++) {
            fx[sy].clp = op_step(fx[sy].clp, x[i], tone);
            fx[sy].hold_ph += step + (k3 ? (xs32(&fx[sy].rng) >> 20) * (uint32_t)k3 / 8u : 0u);
            if (fx[sy].hold_ph >= 65536u) {
                fx[sy].hold_ph &= 65535u;
                fx[sy].hold = sh ? ((fx[sy].clp + (1 << (sh - 1))) >> sh) * (1 << sh) : fx[sy].clp;   /* (a negative
                                                           value shifted left is undefined: times) */
            }
            fx[sy].sl = op_step(fx[sy].sl, fx[sy].hold, post);   /* the stairs' edges: no clicks of their own */
            x[i] = fx[sy].sl;
        }
    } else if (type == FC_LOSSY) {
        int frame = 64 << (k2 * 3 / 128), bits[3];
        bits[0] = 9 - k0 * 5 / 127; bits[1] = 8 - k0 * 5 / 127; bits[2] = 7 - k0 * 5 / 127;   /* the top never a sign alone */
        for (i = 0; i < DMK_SUB; i++) {
            int32_t b[3], y = 0;
            int j;
            fx[sy].band[0] = op_step(fx[sy].band[0], x[i], 2300);    /* ~500 Hz */
            fx[sy].band[1] = op_step(fx[sy].band[1], x[i], 13000);   /* ~3 kHz */
            b[0] = fx[sy].band[0]; b[1] = fx[sy].band[1] - fx[sy].band[0]; b[2] = x[i] - fx[sy].band[1];
            for (j = 0; j < 3; j++) {
                int32_t a = b[j] < 0 ? -b[j] : b[j], st = fx[sy].step[j];
                if (a > fx[sy].peak[j]) fx[sy].peak[j] = a;
                /* a hole: this band dropped this frame, faded out and back in 1.5 ms (a cut would click) */
                a = st > 1 ? (b[j] + (b[j] < 0 ? -st / 2 : st / 2)) / st * st : b[j];
                if (fx[sy].lg[j] == 32768 && !(fx[sy].gate >> j & 1u)) { y += a; continue; }
                if (fx[sy].gate >> j & 1u) { if (fx[sy].lg[j] > 0) fx[sy].lg[j] -= 512; }
                else fx[sy].lg[j] += 512;
                y += (a * (fx[sy].lg[j] >> 1)) >> 14;
            }
            fx[sy].clp = op_step(fx[sy].clp, y, tone);
            x[i] = fx[sy].clp;
            if (++fx[sy].frame >= frame) {              /* a frame: the steps from its peaks; the holes drawn */
                int32_t top = 1;
                fx[sy].frame = 0;
                for (j = 0; j < 3; j++) if (fx[sy].peak[j] > top) top = fx[sy].peak[j];
                fx[sy].gate = 0;
                for (j = 0; j < 3; j++) {
                    fx[sy].step[j] = fx[sy].peak[j] >> bits[j];
                    if (j && k3 && (fx[sy].peak[j] < top / 12 || (int32_t)(xs32(&fx[sy].rng) >> 25) < k3 / (4 - j)))
                        fx[sy].gate |= (uint8_t)(1u << j);
                    fx[sy].peak[j] = 0;
                }
            }
        }
    } else if (type == FC_NOISE) {
        for (i = 0; i < DMK_SUB; i++) {
            int32_t a = x[i] < 0 ? -x[i] : x[i], g, n;
            if (a > fx[sy].nfol) fx[sy].nfol += (a - fx[sy].nfol) >> 4;   /* rides the sound, its tail too */
            else fx[sy].nfol -= (fx[sy].nfol >> 14) + (fx[sy].nfol > 0);   /* at least a step: it reaches zero */
            g = fx[sy].nfol > 4096 ? 32768 : fx[sy].nfol * 8;
            fx[sy].nlp += ((((int32_t)xs32(&fx[sy].rng) >> 16) - fx[sy].nlp) * tone) >> 15;
            n = fx[sy].nlp * k0 / 5200;                 /* the hiss: at most ~-26 dB of the sound's full scale */
            if ((xs32(&fx[sy].rng) >> 12) < (uint32_t)(k2 * k2 * k2) >> 10) {   /* a crackle: a pop of either sign */
                uint32_t r = xs32(&fx[sy].rng);
                fx[sy].crk = (int32_t)(300u + (r & 0x3FFu)) * (r >> 31 ? -1 : 1);
            }
            n += fx[sy].crk; fx[sy].crk = fx[sy].crk * 5 / 8;
            fx[sy].hum += 4870390;                      /* 50 Hz */
            n += ((sinp((uint32_t)fx[sy].hum) + sinp((uint32_t)fx[sy].hum * 3u) / 3) * k3) >> 13;
            x[i] += (n * g) >> 15;
        }
    } else if (type == FC_WARM) {                       /* a triode's lean: the lows and mids bent (an even harmonic, a
                                                           soft top), the highs passed by clean so it thickens without
                                                           fizz; BODY lifts the lows into it, TILT turns the result */
        int32_t drive = 256 + k0 * 6, even = k0 * 64, body = k2 * 2, tilt = (k1 - 64) * 256, mix = k3 * 258;
        int32_t comp = 65536 / drive, lin = (65536 + k0 * 100) / (256 + body / 2);   /* as loud as it came in (+1.5 dB at
                                                           full), small; BODY paid back */
        for (i = 0; i < DMK_SUB; i++) {
            int32_t v, u, q, y;
            fx[sy].wlo = op_step(fx[sy].wlo, x[i], 750);          /* ~170 Hz: the body */
            v = x[i] + ((fx[sy].wlo * body) >> 8);
            fx[sy].wpre = op_step(fx[sy].wpre, v, 14000);         /* ~4 kHz: below it bends, above it is clean */
            u = clampi((fx[sy].wpre * drive) >> 8, -65535, 65535);
            q = ((u >> 1) * (u >> 1)) >> 15;                          /* u^2 / 2^17 */
            y = curve(T_SAT, u - ((q * even) >> 13));                 /* up to 0.25 u^2: the bottom bent harder */
            fx[sy].wdc = op_sh(fx[sy].wdc, y * 256, 10);               /* its DC out (~7 Hz, Q8) */
            y -= fx[sy].wdc >> 8;
            y = ((((y * comp) >> 8) + v - fx[sy].wpre) * lin) >> 8;
            fx[sy].wtl = op_step(fx[sy].wtl, y, 3500);            /* ~800 Hz: the tilt's hinge */
            y += (int32_t)(((int64_t)(2 * fx[sy].wtl - y) * tilt) >> 15);
            x[i] += (int32_t)(((int64_t)(y - x[i]) * mix) >> 15);
        }
    } else if (type == FC_DIST) {                       /* drive into a clip whose knee is soft; SHAPE turns it into a
                                                           fold (a sine of the drive: past full it folds back) */
        int32_t drive = 256 + k0 * k0 / 2, fold = k2 * 258, mix = k3 * 258, g0 = fx[sy].dg, g1, sin_ = 0, sout = 0, gst;
        /* the level matched, slowly (~0.2 s): what went in against what the clip made of it, +3.5 dB (its tone and mix take some back), at most as it
         * made it (a drive loudens the quiet sounds most, and a fixed law can't know the sound) */
        g1 = fx[sy].dfo > 64 ? fx[sy].dfi * 384 / fx[sy].dfo : g0;   /* (a mean level: 32 bits hold it x 384) */
        g1 = clampi(g1, 8, 256);
        g1 = g0 + ((g1 - g0) >> 3);
        fx[sy].dg = g1;
        gst = (g1 - g0) / DMK_SUB;
        for (i = 0; i < DMK_SUB; i++) {
            int32_t u, c, y;
            fx[sy].dpre = op_step(fx[sy].dpre, x[i], 18000);      /* ~5 kHz in: fewer of the fold's aliases */
            u = (int32_t)(((int64_t)fx[sy].dpre * drive) >> 8);
            c = curve(T_LIM, u);
            y = c + (((sinp((uint32_t)u << 15) - c) * fold) >> 15);
            sin_ += (x[i] < 0 ? -x[i] : x[i]) >> 4; sout += (y < 0 ? -y : y) >> 4;
            y = (y * (g0 + gst * (i + 1))) >> 8;
            fx[sy].clp = op_step(fx[sy].clp, y, tone);
            x[i] += (int32_t)(((int64_t)(fx[sy].clp - x[i]) * mix) >> 15);
        }
        fx[sy].dfi += (sin_ - fx[sy].dfi) >> 9;         /* a block's mean, in and out (~0.2 s) */
        fx[sy].dfo += (sout - fx[sy].dfo) >> 9;
    }
}

/* MEDIA: TAPE (wow drifting in rate, flutter, head loss, saturation), CASSETTE (a faster warble, the top closing as
 * it gets loud, dropouts), VHS (band-limited, a slow deep wobble with a wandering time base, and now and then the
 * tracking slipping: in over 150 ms and out over 400, the pitch bending down and back, the sound dimming and ducking
 * under a soft hiss; nothing sudden) */
static void media_block(int sy, const synth_t *p, int32_t *x, int t)   /* (sy: the state's slot; p: its settings) */
{
    int i, k0 = p->mk[0], k1 = p->mk[1], k2 = p->mk[2], k3 = p->mk[3];
    int32_t tone = T_OP[64 + k1 / 2];
    fx[sy].wd += ((int32_t)(xs32(&fx[sy].mrng) >> 22) - 512 - fx[sy].wd) >> 6;   /* the wow's rate wanders */
    if (t == FM_CASS && !fx[sy].drop && (xs32(&fx[sy].mrng) >> 20) < (uint32_t)k3 / 10u)  /* a dropout begins */
        { fx[sy].drop = 450 + (int32_t)(fx[sy].mrng % 5000u); fx[sy].drop_g = 16000 + (int32_t)(fx[sy].mrng % 12000u); }
    if (t == FM_VHS && !fx[sy].track && !fx[sy].tenv && (xs32(&fx[sy].mrng) >> 16) < (uint32_t)(k3 * k3) >> 11)
        fx[sy].track = 13000 + (int32_t)(fx[sy].mrng % 60000u);   /* the tracking slips (~3 s apart at full): 0.3-1.7 s */
    for (i = 0; i < DMK_SUB; i++) {
        int32_t m, d, pos, f, y, a, c;
        uint32_t j, rate = t == FM_TAPE ? p->twow : t == FM_CASS ? p->twow + (uint32_t)k2 * 1200u : p->twow / 2u + (uint32_t)k2 * 1000u;
        METER(dmeter.sat_line += x[i] > 32767 || x[i] < -32768);
        tp_buf[sy][fx[sy].tw & (TP_LEN - 1)] = (int16_t)sat16(x[i]);
        fx[sy].wow += rate + (uint32_t)(fx[sy].wd * 30);
        fx[sy].flut += p->tflut;
        m = (sinp(fx[sy].wow) * 48 + sinp(fx[sy].flut)) >> 6;
        if (t == FM_VHS) {                              /* the jitter: the time base wandering, not a new place each sample */
            fx[sy].jit += ((int32_t)(xs32(&fx[sy].mrng) >> 20) - 2048 - fx[sy].jit) >> 6;
            m += fx[sy].jit / 8;
        }
        d = (t == FM_CASS ? 300 : 400) * 256 + (int32_t)(((int64_t)m * (t == FM_TAPE ? p->tdepth : 100 + k0 * (t == FM_VHS ? 300 : 220))) >> 15);
        /* tracking: the pitch dips, glided (a jump in the line's read would click) */
        if (fx[sy].track || fx[sy].tenv) {              /* tracking: its envelope, in over 150 ms, out over 400 */
            if (fx[sy].track) { fx[sy].track--; fx[sy].tenv += fx[sy].tenv < 32763 ? 5 : 32768 - fx[sy].tenv; }
            else fx[sy].tenv -= fx[sy].tenv > 2 ? 2 : fx[sy].tenv;
            d += (fx[sy].tenv * (k3 + 30)) >> 8;        /* read later by up to 80 samples: the pitch bends, gently */
        }
        pos = (int32_t)(fx[sy].tw << 8) - d;
        j = (uint32_t)(pos >> 8); f = pos & 255;
        y = (tp_buf[sy][j & (TP_LEN - 1)] * (256 - f) + tp_buf[sy][(j + 1) & (TP_LEN - 1)] * f) >> 8;
        fx[sy].tw++;
        if (t == FM_TAPE) {
            fx[sy].hl = op_step(fx[sy].hl, y, p->thl);
            y = curve(T_LIM, (fx[sy].hl * p->tdrive) >> 8);   /* clean until AMOUNT drives it */
            y = (y * p->tcomp) >> 15;                   /* its gain taken back: AMOUNT bends, not louder (it was 5 dB) */
        } else if (t == FM_CASS) {
            a = y < 0 ? -y : y;
            fx[sy].fol += (a - fx[sy].fol) >> 8;
            c = tone - (fx[sy].fol >> 3);               /* loud: the top closes */
            if (fx[sy].drop) fx[sy].drop--;             /* a dropout: in and out over 3 ms, as the tape lifts */
            fx[sy].dd += ((fx[sy].drop ? 32768 : 0) - fx[sy].dd) >> 7;
            c -= (3000 * fx[sy].dd) >> 15;
            y = (y * (32768 - (((32768 - fx[sy].drop_g) * fx[sy].dd) >> 15))) >> 15;
            if (c < 1500) c = 1500;
            fx[sy].lp1 = op_step(fx[sy].lp1, y, c);
            fx[sy].lp2 = op_step(fx[sy].lp2, fx[sy].lp1, c);
            y = curve(T_LIM, fx[sy].lp2 * 5 / 4);
        } else {
            fx[sy].hp = op_sh(fx[sy].hp, y * 256, 9);    /* ~80 Hz out */
            y -= fx[sy].hp >> 8;
            c = fx[sy].tenv ? tone - ((tone * (fx[sy].tenv >> 1)) >> 15) : tone;   /* tracking dims the top */
            fx[sy].lp1 = op_step(fx[sy].lp1, y, c);
            fx[sy].lp2 = op_step(fx[sy].lp2, fx[sy].lp1, c);
            y = fx[sy].lp2;
            if (fx[sy].tenv) {                          /* and ducks it a little under a soft hiss that rides the sound */
                a = y < 0 ? -y : y;
                if (a > fx[sy].fol) fx[sy].fol += (a - fx[sy].fol) >> 4; else fx[sy].fol -= (fx[sy].fol >> 13) + (fx[sy].fol > 0);
                c = fx[sy].fol > 2048 ? 32768 : fx[sy].fol * 16;
                fx[sy].nb += ((((int32_t)xs32(&fx[sy].mrng) >> 19) * k3 >> 7) - fx[sy].nb) >> 3;
                y = ((y * (32768 - fx[sy].tenv / 4)) >> 15) + ((((fx[sy].nb * c) >> 15) * (fx[sy].tenv >> 1)) >> 15);
            }
            y = curve(T_LIM, y);
        }
        x[i] = y;
    }
}

/* MEDIA's later three: VINYL (a record at 33 1/3: the platter's slow wow, a little warp, the top rolled off; its
 * surface's crackle, most of it small, and a rumble under it), RADIO (AM through a small speaker: a 150 Hz - 5 kHz band
 * BAND narrows to 600 Hz - 1.2 kHz and TONE moves, its top four poles; static in the band; the station drifting off and back, the static
 * rising as it fades), DICTAPHONE (a microcassette: narrow, a fast capstan warble on a slower wow, the automatic level
 * squeezing it, its hiss and motor raised in the gaps; SPEED: the slower, the darker and the more it warbles). Their
 * noise rides the sound and its tail (~3 s, the needle still on the record), so a quiet synth is quiet */
static void media_new(int sy, const synth_t *p, int32_t *x, int t)   /* (sy: the state's slot; p: its settings) */
{
    int i, k0 = p->mk[0], k1 = p->mk[1], k2 = p->mk[2], k3 = p->mk[3];
    int32_t g0 = fx[sy].ng, g1, pk = 0, gs;
    for (i = 0; i < DMK_SUB; i++) { int32_t a = x[i] < 0 ? -x[i] : x[i]; if (a > pk) pk = a; }
    pk <<= 8;
    if (pk > fx[sy].nfl) fx[sy].nfl += (pk - fx[sy].nfl) >> 2;
    else fx[sy].nfl -= (fx[sy].nfl >> 12) + (fx[sy].nfl > 0);   /* ~1.5 s a block at a time */
    g1 = fx[sy].nfl > (2048 << 8) ? 32768 : fx[sy].nfl >> 4;
    fx[sy].ng = g1;
    gs = (g1 - g0) / DMK_SUB;
    if (t == FM_VINYL) {
        int32_t depth = k0 * 280, tone = T_OP[64 + k1 / 2], rum = k3;
        uint32_t crk = (uint32_t)(k2 * k2) / 17u;       /* crackles a sample, in 2^-20: ~40 a second at full */
        int32_t m, d = fx[sy].md, ds;
        fx[sy].vph += 54045u * DMK_SUB;                 /* a turn each 1.8 s */
        m = sinp(fx[sy].vph) + sinp(fx[sy].vph * 2u) / 5;   /* off its centre, a little warped: up to ~1.1 % */
        fx[sy].md = 200 * 256 + ((m * depth) >> 15);
        ds = (fx[sy].md - d) / DMK_SUB;
        for (i = 0; i < DMK_SUB; i++) {
            int32_t pos, f, y, n, g = g0 + gs * i;
            uint32_t j, r;
            tp_buf[sy][fx[sy].tw & (TP_LEN - 1)] = (int16_t)sat16(x[i]);
            pos = (int32_t)(fx[sy].tw << 8) - (d += ds);
            j = (uint32_t)(pos >> 8); f = pos & 255;
            y = (tp_buf[sy][j & (TP_LEN - 1)] * (256 - f) + tp_buf[sy][(j + 1) & (TP_LEN - 1)] * f) >> 8;
            fx[sy].tw++;
            fx[sy].vlp[0] = op_step(fx[sy].vlp[0], y, tone);
            fx[sy].vlp[1] = op_step(fx[sy].vlp[1], fx[sy].vlp[0], tone);
            r = xs32(&fx[sy].mrng);
            n = ((int32_t)r >> 20) * k2 >> 10;          /* the surface's hiss, faint */
            if ((r >> 12) < crk) {                      /* a crackle: its size cubed, so most are small */
                uint32_t q = xs32(&fx[sy].mrng), a = q >> 17;
                a = (((a * a) >> 15) * a) >> 15;
                fx[sy].mcrk = (int32_t)((a * (uint32_t)(24 + k2)) >> 9) * (q & 1u ? -1 : 1);
            }
            n += fx[sy].mcrk; fx[sy].mcrk = fx[sy].mcrk * 5 / 8;
            fx[sy].vrm[0] += (int32_t)(((int64_t)((((int32_t)r >> 16) * 256) - fx[sy].vrm[0]) * 187) >> 15);   /* ~40 Hz */
            fx[sy].vrm[1] += (int32_t)(((int64_t)(fx[sy].vrm[0] - fx[sy].vrm[1]) * 187) >> 15);
            n += ((fx[sy].vrm[1] >> 8) * rum) >> 9;     /* the rumble, ~-43 dB at full */
            x[i] = curve(T_LIM, fx[sy].vlp[1] + ((n * g) >> 15));
        }
    } else if (t == FM_RADIO) {
        int sh = (k1 - 64) * 13 / 64, lo = clampi(37 + k0 * 25 / 127 + sh, 0, 127), hi;
        int32_t fade, lvl, sa, chp, c, mk = 280 + k0 * 4;   /* (1.1x, to 3.1x as BAND narrows) */
        if (--fx[sy].rcnt <= 0) {                       /* the drift: somewhere new each 0.1 - 0.85 s, most often near */
            uint32_t q = xs32(&fx[sy].mrng);
            fx[sy].rcnt = 300 + (int32_t)(q >> 21);
            q = xs32(&fx[sy].mrng) >> 17;
            fx[sy].rdt = (int32_t)((q * q) >> 15);
        }
        fx[sy].rdr += (fx[sy].rdt - fx[sy].rdr) >> 9;  /* glided, ~0.9 Hz */
        fade = (fx[sy].rdr * k3) >> 7;
        lvl = 32768 - ((fade * 5) >> 3);                /* down to ~-8.5 dB as it drifts (to -12: 5 dB under at full) */
        sa = k2 * (64 + (fade >> 8));                   /* the static, up as the station goes */
        hi = clampi(101 - k0 * 22 / 127 + sh - (fade * 10 >> 15), 0, 127);
        chp = T_OP[lo]; c = T_OP[hi];
        for (i = 0; i < DMK_SUB; i++) {
            int32_t y, n, g = g0 + gs * i;
            uint32_t r = xs32(&fx[sy].mrng);
            tp_buf[sy][fx[sy].tw++ & (TP_LEN - 1)] = (int16_t)sat16(x[i]);   /* (the line kept, for a type after) */
            n = ((int32_t)r >> 18) * sa >> 15;          /* hiss and, now and then, a crackle in it */
            if ((r >> 12) < (uint32_t)k2 * 3u) fx[sy].mcrk = ((int32_t)xs32(&fx[sy].mrng) >> 19) * k2 >> 7;
            n += fx[sy].mcrk; fx[sy].mcrk = fx[sy].mcrk * 3 / 4;
            y = clampi((int32_t)(((int64_t)x[i] * lvl) >> 15) + ((n * g) >> 15), -49151, 49151);
            fx[sy].rhp[0] = op_step(fx[sy].rhp[0], y, chp); y -= fx[sy].rhp[0];
            fx[sy].rhp[1] = op_step(fx[sy].rhp[1], y, chp); y -= fx[sy].rhp[1];
            fx[sy].rlp[0] = op_step(fx[sy].rlp[0], y, c);   /* the top: four poles, as a receiver's is steep */
            fx[sy].rlp[1] = op_step(fx[sy].rlp[1], fx[sy].rlp[0], c);
            fx[sy].rlp[2] = op_step(fx[sy].rlp[2], fx[sy].rlp[1], c);
            fx[sy].rlp[3] = op_step(fx[sy].rlp[3], fx[sy].rlp[2], c);
            x[i] = curve(T_SAT, (fx[sy].rlp[3] * mk) >> 8);   /* the small speaker: made up, and soft at the top */
        }
    } else {                                            /* DICTAPHONE */
        int32_t mul = 256 + (127 - k2) * 2, af = (k0 * 9 * mul) >> 8, aw = (k0 * 46 * mul) >> 8;
        int32_t c = T_OP[clampi(84 + k2 * 14 / 127 + (k1 - 64) * 13 / 64, 0, 127)], chp = T_OP[50];
        int32_t hiss = 64 + (127 - k2), gmax = 256 + k3 * 14, a0 = fx[sy].agc_g, a1, as, pk2 = 0, fl, d = fx[sy].md, ds;
        uint32_t fr = 680000u + (uint32_t)k2 * 3800u, mi;   /* the capstan: 7 .. 12 Hz */
        /* the level control: from what it heard (in fast, out over ~0.2 s), the gain toward 6000, at most SQUASH's
         * (8x), glided; a block's step across it */
        a1 = fx[sy].agc * gmax > (5400 << 8) ? (5400 << 8) / (fx[sy].agc + 1) : gmax;   /* (3800: 4 dB under at full) */
        a1 = 256 + (((a1 - 256) * k3) >> 7);
        fx[sy].agc_g += (a1 - fx[sy].agc_g) >> 3;
        as = (fx[sy].agc_g - a0) / DMK_SUB;
        fx[sy].fph += fr * DMK_SUB;
        fx[sy].wow += (146000u + (uint32_t)(fx[sy].wd * 20)) * DMK_SUB;   /* ~1.5 Hz, its rate wandering */
        fl = sinp(fx[sy].fph);
        fx[sy].md = 150 * 256 + ((fl * af + sinp(fx[sy].wow) * aw) >> 15);
        ds = (fx[sy].md - d) / DMK_SUB;
        mi = 97000000u + (uint32_t)k2 * 200000u + (uint32_t)(fl * 40);   /* the motor's whine, ~1 kHz, warbling */
        for (i = 0; i < DMK_SUB; i++) {
            int32_t pos, f, y, n, g = g0 + gs * i, a;
            uint32_t j, r = xs32(&fx[sy].mrng);
            tp_buf[sy][fx[sy].tw & (TP_LEN - 1)] = (int16_t)sat16(x[i]);
            pos = (int32_t)(fx[sy].tw << 8) - (d += ds);
            j = (uint32_t)(pos >> 8); f = pos & 255;
            y = (tp_buf[sy][j & (TP_LEN - 1)] * (256 - f) + tp_buf[sy][(j + 1) & (TP_LEN - 1)] * f) >> 8;
            fx[sy].tw++;
            fx[sy].mph += mi;
            n = ((((int32_t)r >> 22) * hiss) >> 9) + (sinp(fx[sy].mph) >> 10);
            y += (n * g) >> 15;                         /* under the level control: it raises them in the gaps */
            fx[sy].rhp[0] = op_step(fx[sy].rhp[0], y, chp); y -= fx[sy].rhp[0];   /* ~300 Hz up */
            fx[sy].rlp[0] = op_step(fx[sy].rlp[0], y, c);
            fx[sy].rlp[1] = op_step(fx[sy].rlp[1], fx[sy].rlp[0], c);
            y = fx[sy].rlp[1] * 5 / 4;                  /* (the band made up) */
            a = y < 0 ? -y : y;
            if (a > pk2) pk2 = a;
            x[i] = curve(T_LIM, (int32_t)(((int64_t)y * (a0 + as * (i + 1))) >> 8));
        }
        if (pk2 > fx[sy].agc) fx[sy].agc += (pk2 - fx[sy].agc) >> 1;
        else fx[sy].agc -= (fx[sy].agc >> 9) + (fx[sy].agc > 0);
        fx[sy].wd += ((int32_t)(xs32(&fx[sy].mrng) >> 22) - 512 - fx[sy].wd) >> 6;
    }
}

/* MOTION's chorus: two taps swept in quadrature at 7-17 ms, a tone on the wet, mixed into a stereo pair */
static void chorus_block(int sy, const int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[sy];
    int i, k;
    int32_t depth = 8 + p->ok[0] * 200 / 127, tone = T_OP[64 + p->ok[1] / 2], mix = p->ok[3] * 200;
    uint32_t rate = 9740u + (uint32_t)p->ok[2] * 2300u;
    for (i = 0; i < DMK_SUB; i++) {
        int32_t tap[2];
        ch_buf[sy][fx[sy].cw & (CH_LEN - 1)] = (int16_t)sat16(x[i]);
        fx[sy].clfo += rate;
        for (k = 0; k < 2; k++) {
            int32_t d = 310 * 256 + depth * ((sinp(fx[sy].clfo + (uint32_t)k * 0x40000000u) + 32768) >> 8);
            int32_t pos = (int32_t)(fx[sy].cw << 8) - d, f = pos & 255;
            uint32_t j = (uint32_t)(pos >> 8);
            int32_t y = (ch_buf[sy][j & (CH_LEN - 1)] * (256 - f) + ch_buf[sy][(j + 1) & (CH_LEN - 1)] * f) >> 8;
            fx[sy].chlp[k] = op_step(fx[sy].chlp[k], y, tone);
            tap[k] = fx[sy].chlp[k];
        }
        fx[sy].cw++;
        l[i] = x[i] - ((x[i] * mix) >> 16) + ((tap[0] * mix) >> 15);
        r[i] = x[i] - ((x[i] * mix) >> 16) + ((tap[1] * mix) >> 15);
    }
}

/* ENSEMBLE: the string machine's: three taps of one line at 120 degrees, each swept by a slow sweep (0.3 - 1.5 Hz)
 * and a fast one (4 - 8 Hz) at once, so the three never beat the same way; the sweeps a block at a time, each tap's
 * delay stepped across it. Left and right: the three's middle and their difference, WIDTH apart */
static void ens_block(int sy, const int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[sy];
    int i, k;
    int32_t as = p->ok[0] * 160, af = p->ok[0] * 20, tone = T_OP[64 + p->ok[1] / 2], w = p->ok[3] * 258, d[3], ds[3];
    fx[sy].eph[0] += (29218u + (uint32_t)p->ok[2] * 920u) * DMK_SUB;
    fx[sy].eph[1] += (389564u + (uint32_t)p->ok[2] * 3067u) * DMK_SUB;
    for (k = 0; k < 3; k++) {
        uint32_t o = (uint32_t)k * 0x55555555u;
        int32_t e = 200 * 256 + ((sinp(fx[sy].eph[0] + o) * as + sinp(fx[sy].eph[1] + o) * af) >> 15);
        d[k] = fx[sy].ed[k]; ds[k] = (e - d[k]) / DMK_SUB; fx[sy].ed[k] = e;
    }
    for (i = 0; i < DMK_SUB; i++) {
        int32_t t[3], c, sd;
        ch_buf[sy][fx[sy].cw & (CH_LEN - 1)] = (int16_t)sat16(x[i]);
        for (k = 0; k < 3; k++) {
            int32_t pos = (int32_t)(fx[sy].cw << 8) - (d[k] += ds[k]), f = pos & 255;
            uint32_t j = (uint32_t)(pos >> 8);
            t[k] = (ch_buf[sy][j & (CH_LEN - 1)] * (256 - f) + ch_buf[sy][(j + 1) & (CH_LEN - 1)] * f) >> 8;
        }
        fx[sy].cw++;
        c = (t[0] + t[1] + t[2]) * 11 >> 5;             /* (about a third of the three, a little over) */
        sd = (int32_t)(((int64_t)(t[0] - t[1]) * w) >> 16);
        fx[sy].chlp[0] = op_step(fx[sy].chlp[0], c + sd, tone);
        fx[sy].chlp[1] = op_step(fx[sy].chlp[1], c - sd, tone);
        l[i] = (((x[i] >> 1) + ((fx[sy].chlp[0] * 7) >> 3)) * 11) >> 3;   /* (x 1.4: as loud as it came in) */
        r[i] = (((x[i] >> 1) + ((fx[sy].chlp[1] * 7) >> 3)) * 11) >> 3;
    }
}

/* the sweep's triangle at q, 0 .. 65534 */
static inline uint32_t tri16(uint32_t q) { uint32_t t = q >> 16; return t < 32768u ? t << 1 : (65535u - t) << 1; }

/* PHASER: the pedal's four all-pass stages, mono as the pedal is (after Felucca's, perform.c, GPL-3.0: its stages, its
 * coefficients, T_PHA); their breaks swept by a triangle around TONE's place (350 Hz .. 2 kHz) by DEPTH (to 4 octaves
 * each way, within 150 Hz .. 4.8 kHz), fed back by RESO (0 .. 0.7); the coefficient a block at a time, at its
 * middle. Out: (dry + all-passed) / 2, its two notches. Each stage clamped (an all-pass overshoots up to 3x on a
 * sharp edge) */
static void phaser_block(int sy, const int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[sy];
    int i, st;
    int32_t a, fbk = p->ok[3] * 180, cq = (8 * 2048) + p->ok[1] * 16 * 2048 / 127, dsp = p->ok[0] * 16 * 2048 / 127, q;
    int32_t *px = fx[sy].px[0], *py = fx[sy].py[0];
    uint32_t inc = T_LFO[p->ok[2] * 80 / 127];           /* 0.05 .. 2.2 Hz */
    q = cq + (int32_t)(((int64_t)((int32_t)tri16(fx[sy].clfo + inc / 2u) - 32767) * dsp) >> 15);
    q = clampi(q, 0, 32 * 2048 - 1);
    a = T_PHA[q >> 11] + (((T_PHA[(q >> 11) + 1] - T_PHA[q >> 11]) * (q & 2047)) >> 11);
    fx[sy].clfo += inc;
    for (i = 0; i < DMK_SUB; i++) {
        int32_t t = fx[sy].pfb[0] * fbk, xs = clampi((x[i] + 2) >> 2, -16383, 16383), u, y;
        u = y = xs + ((t + ((t >> 31) & 32767)) >> 15);   /* (towards zero, here and in the stages: rounded, a stage held
                                                           an offset of a sample, a tail on it for ever) */
        ch_buf[sy][fx[sy].cw++ & (CH_LEN - 1)] = (int16_t)sat16(x[i]);   /* (the line kept, for a type after) */
        for (st = 0; st < 4; st++) {                    /* y = a (u - y1) + u1 */
            t = a * (u - py[st]);
            y = clampi(px[st] + ((t + ((t >> 31) & 16383)) >> 14), -65535, 65535);
            px[st] = u;
            py[st] = y;
            u = y;
        }
        fx[sy].pfb[0] = clampi(y, -32767, 32767);
        l[i] = r[i] = (xs + y) * 13 >> 2;               /* (x + y) / 2 * 1.6: x at a quarter above, as loud as it came in */
    }
}

/* FLANGER: a short tape flange (after Felucca's): one line, its delay swept from 0.25 ms to DEPTH's (up to 8 ms) on
 * a squared triangle (longer near the short end, where the comb is widest: the sweep sounds even), the right a quarter
 * behind, fed back by RESO (0 .. 0.85, the sum soft), TONE on the swept copy, a DC blocker on it (one: both read the same
 * line). The delays a block at a time, stepped across it. Out: (dry + swept) / 2 */
static void flanger_block(int sy, const int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[sy];
    int i, c;
    int32_t fbk = p->ok[3] * 219, tone = T_OP[64 + p->ok[1] / 2], d[2], ds[2];
    uint32_t inc = T_LFO[p->ok[2] * 80 / 127], span = (uint32_t)p->ok[0] * 342u / 127u;
    fx[sy].clfo += inc;
    for (c = 0; c < 2; c++) {
        uint32_t t = tri16(fx[sy].clfo - (uint32_t)c * 0x40000000u);
        int32_t e = (int32_t)((11u << 8) + ((span * ((t * t) >> 16)) >> 8));
        d[c] = fx[sy].ed[c]; ds[c] = (e - d[c]) / DMK_SUB; fx[sy].ed[c] = e;
    }
    for (i = 0; i < DMK_SUB; i++) {
        int32_t v[2], dc;
        for (c = 0; c < 2; c++) {
            int32_t pos = (int32_t)(fx[sy].cw << 8) - (d[c] += ds[c]), f = pos & 255, y;
            uint32_t j = (uint32_t)(pos >> 8);
            y = (ch_buf[sy][j & (CH_LEN - 1)] * (256 - f) + ch_buf[sy][(j + 1) & (CH_LEN - 1)] * f) >> 8;
            fx[sy].ftl[c] = op_step(fx[sy].ftl[c], y, tone);
            v[c] = fx[sy].ftl[c];
        }
        /* the line at the sound's level, as every type writes it (a type after reads it as it is), the sum soft */
        {
            int32_t t = v[0] * fbk;
            ch_buf[sy][fx[sy].cw++ & (CH_LEN - 1)] = (int16_t)curve(T_LIM, x[i] + ((t + ((t >> 31) & 32767)) >> 15));
        }
        fx[sy].fdc[0] = op_sh(fx[sy].fdc[0], v[0] * 256, 9);
        dc = fx[sy].fdc[0] >> 8;
        l[i] = (x[i] + v[0] - dc) * 5 >> 3;             /* (dry + swept) / 2 * 1.25 */
        r[i] = (x[i] + v[1] - dc) * 5 >> 3;
    }
}

/* TREMOLO's divisions, in thirds of a sixteenth: 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T (the screen's TREMS) */
#define TREM_N 7
static const uint8_t TREM3[TREM_N] = {24, 12, 8, 6, 4, 3, 2};
/* TREMOLO: the level, or with PAN the two sides against each other, swung by a sine that SHAPE squares (driven into
 * its top, so its edges stay ~1 % of the cycle: no click), at RATE's division of the tempo (dmk_seq keeps it in time
 * with the bars); its gains a block at a time, stepped across it */
static void trem_block(int sy, const int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[sy];
    int i;
    int32_t d = p->ok[0] * 258, sh = p->ok[1], pan = p->ok[3] * 258, s, m, g, gr, sl, sr;
    fx[sy].tph += fx[sy].tstep;
    s = sinp(fx[sy].tph);
    if (sh) s = clampi((int32_t)(((int64_t)s * (256 + sh * sh / 4)) >> 8), -32767, 32767);
    m = (s + 32767) >> 1;                               /* 0 .. 32767: 0 the trough */
    g = 32768 - (((32768 - m) * d) >> 15);
    gr = g + (int32_t)(((int64_t)((32768 - ((m * d) >> 15)) - g) * pan) >> 15);   /* PAN: the right against the left */
    sl = (g - fx[sy].tg[0]) / DMK_SUB; sr = (gr - fx[sy].tg[1]) / DMK_SUB;
    for (i = 0; i < DMK_SUB; i++) {
        int32_t a = fx[sy].tg[0] + sl * (i + 1), b = fx[sy].tg[1] + sr * (i + 1);
        ch_buf[sy][fx[sy].cw++ & (CH_LEN - 1)] = (int16_t)sat16(x[i]);
        l[i] = (int32_t)(((int64_t)x[i] * a) >> 15);
        r[i] = (int32_t)(((int64_t)x[i] * b) >> 15);
    }
    fx[sy].tg[0] = g; fx[sy].tg[1] = gr;
}

static void motion_block(int sy, const int32_t *x, int32_t *l, int32_t *r, int t)
{
    if (t == FO_CHORUS) chorus_block(sy, x, l, r);
    else if (t == FO_ENS) ens_block(sy, x, l, r);
    else if (t == FO_PHASER) phaser_block(sy, x, l, r);
    else if (t == FO_FLANGER) flanger_block(sy, x, l, r);
    else trem_block(sy, x, l, r);
}

/* a later type coming in: its own state from rest (a stale one rings as it fades in); the line is the sound's */
static void fx_enter(int sy, int row, int t)
{
    int c, k;
    if (row == 1 && t >= FM_VINYL) {
        fx[sy].vlp[0] = fx[sy].vlp[1] = fx[sy].vrm[0] = fx[sy].vrm[1] = 0;
        fx[sy].rhp[0] = fx[sy].rhp[1] = fx[sy].rlp[0] = fx[sy].rlp[1] = fx[sy].rlp[2] = fx[sy].rlp[3] = 0;
        fx[sy].agc = 0; fx[sy].agc_g = 256; fx[sy].mcrk = 0; fx[sy].md = (t == FM_DICTA ? 150 : 200) << 8;
        fx[sy].nfl = fx[sy].ng = 0;
    } else if (row == 0 && t >= FC_WARM) {
        fx[sy].wlo = fx[sy].wpre = fx[sy].wtl = fx[sy].wdc = fx[sy].dpre = fx[sy].clp = fx[sy].dfi = fx[sy].dfo = 0;
        fx[sy].dg = 64;
    } else if (row == 2 && t >= FO_ENS) {
        for (c = 0; c < 2; c++) {
            for (k = 0; k < 4; k++) fx[sy].px[c][k] = fx[sy].py[c][k] = 0;
            fx[sy].pfb[c] = fx[sy].fdc[c] = fx[sy].ftl[c] = fx[sy].chlp[c] = 0;
            fx[sy].tg[c] = 32768;
        }
        for (k = 0; k < 3; k++) fx[sy].ed[k] = t == FO_FLANGER ? 11 << 8 : 200 * 256;
    }
}

/* 8-bit mu-law, the lines' (the delays' and REVERSE's) */
static uint8_t mulaw_enc(int32_t x)
{
    int sign = 0, exp, mask, man;
    if (x < 0) { sign = 0x80; x = -x; }
    if (x > 32635) x = 32635;
    x += 0x84;
    mask = x >> 7;                                      /* its segment, the top bit's place over 7, by halves */
    exp = mask >= 16 ? (mask >= 64 ? (mask >= 128 ? 7 : 6) : (mask >= 32 ? 5 : 4)) : (mask >= 4 ? (mask >= 8 ? 3 : 2) : (mask >= 2 ? 1 : 0));
    man = (x >> (exp + 3)) & 0x0F;
    return (uint8_t)~(sign | (exp << 4) | man);
}

/* ---- each synth's REVERB (the owner's, 2026-10-25: a reverb a track, the shared ones gone; light enough for three
 * at once). Its own line memory at half rate, 4096 samples (186 ms), that each type lays its lines and all-passes in;
 * one place runs down through it a step at a time and every line moves with it (a line is a stretch of the memory,
 * written at its start, read back at its length: after the effects engines' single delay memory). ROOM and HALL: four
 * lines mixed by a 4 x 4 Hadamard over 2 (lossless, adds only) behind input all-passes, each line damped by TONE and
 * given the gain that loses 60 dB in SIZE's time; HALL's lines longer, two of them swept slowly by MOD. PLATE:
 * Dattorro's figure of eight (two halves, each a swept all-pass, a line, TONE's damping, an all-pass, a line, into the
 * other half through the decay) behind an input all-pass, at a sixth of his lengths, read out at his taps. SPRING:
 * a spring, a loop of a line, TONE's low-pass, the decay, a low cut and eight stretched all-passes ((a + z^-2) /
 * (1 + a z^-2), after Valimaki, Parker and Abel: the chirp, more of it each time round; DRIP is a), read by two
 * pickups, wobbling. Every product that goes round a loop keeps its remainder (or, in an all-pass, rounds towards
 * zero), so a tail decays as it should down to exact silence (floors would hold an offset and a hum for ever); a tail
 * at a few samples with nothing coming in sleeps, none of its work run. A block (8 steps) at a time, its state in
 * hand */
#define RV_MEM 4096u
#define RV_MASK (RV_MEM - 1u)
#define RV_SW 8                                         /* a swept line's reach, samples either way */
#define RV_SLEEP 8192u                                  /* steps with no input and the tail at a few samples: asleep */
#define RV_ST (DMK_SUB / 2)                             /* steps a block */
static int16_t rv_mem[DMK_SYNTHS][RV_MEM] POOL;
typedef struct {
    uint32_t w;                                         /* the place, down a step at a time */
    int32_t hp, he;                                     /* the input's low cut, its step's remainder */
    int32_t lp[4], le[4], ge[4];                        /* the damping one-poles and their remainders; the decays' */
    int32_t sh, se;                                     /* SPRING: its low cut */
    int32_t ap[2][9];                                   /* SPRING: its chain, two steps of it (z^-2) */
    int32_t x4;                                         /* PLATE: the second half's end, into the first */
    int32_t gin, g;                                     /* the way in where the last block ended; the type's share, Q15 */
    int32_t sw[2];                                      /* the sweeps' places, Q8 samples */
    uint32_t ph[2];                                     /* their phases */
    int16_t lg[4];                                      /* each line's decay gain, Q15 (PLATE: a half's; SPRING: its loop's) */
    uint16_t quiet;
    uint8_t cur, gty, gsz;                              /* the type running; the type and SIZE (+ 1) lg is for */
} rvs_t;
static rvs_t rvs[DMK_SYNTHS];

/* the layouts, in half-rate steps: each stretch's start in the memory, its length (a line's stretch is a step longer,
 * a swept one 2 RV_SW + 2) */
static const uint16_t RM_AP[2] = {47, 113}, RM_APB[2] = {0, 48};              /* ROOM: lines of 13 .. 23 ms */
static const uint16_t RM_L[4] = {293, 359, 433, 503}, RM_B[4] = {162, 456, 816, 1250};
static const uint16_t HL_AP[2] = {131, 199}, HL_APB[2] = {0, 132};            /* HALL: 32 .. 47 ms, 2 and 4 swept */
static const uint16_t HL_L[4] = {709, 811, 919, 1031}, HL_B[4] = {332, 1059, 1888, 2825};
#define PL_AP 140u                                      /* PLATE: an all-pass in (0.75), then the halves: a swept */
static const uint16_t PL_L[8] = {112, 742, 300, 620, 151, 703, 443, 527};   /* all-pass, a line, an all-pass, a line */
static const uint16_t PL_B[8] = {141, 271, 1014, 1315, 1936, 2105, 2809, 3253};
#define SP_L 873u                                       /* SPRING: 40 ms round */
/* SIZE's time to lose 60 dB: 0.25 .. 3 s (ROOM), 0.6 .. 9 (PLATE), 0.5 .. 5 (SPRING), 1 .. 16 (HALL): 3 log2(10) /
 * (22050 the shortest), Q16 a step; log2 of the longest over it, Q16. A loop of L steps: g = 2^-(L k 2^-(SIZE span)) */
static const uint8_t RV_K[RV_N] = {0, 118, 49, 59, 30};
static const uint32_t RV_SPAN[RV_N] = {0, 234947, 256036, 217706, 262144};
/* each type's input (a shift: the lines' headroom) and output (Q8), so each sits about as loud as the others */
static const uint8_t RV_IN[RV_N] = {0, 2, 2, 2, 3};
static const uint16_t RV_OUT[RV_N] = {0, 155, 155, 338, 184};

/* a * g / 2^15, towards zero (g Q15; |a| < 65536): the all-passes' (lossless: no growth from them) */
static inline int32_t mz15(int32_t a, int32_t g) { int32_t t = a * g; return (t + ((t >> 31) & 32767)) >> 15; }
/* a * g / 2^15 with the step's remainder kept in *e: a loop's decay (rounded either way it has a dead band, a tail
 * stuck at a few samples; towards zero it dies early, the last 30 dB in a second; with the remainder carried it decays
 * as it should all the way down, and 0 stays 0). |a * g| < 2^31 - 2^15 */
static inline int32_t mr15(int32_t a, int32_t g, int32_t *e) { int32_t t = a * g + *e; *e = t & 32767; return t >> 15; }
/* a one-pole low-pass of x (|x| < 32768), its step's remainder kept (no dead band: it reaches x, and 0) */
static inline int32_t rv_lp(int32_t *lp, int32_t *le, int32_t x, int32_t c)
{
    int32_t e = (x - *lp) * c + *le;
    *lp += e >> 15;
    *le = e & 32767;
    return *lp;
}
/* an all-pass of d steps whose stretch starts at at, coefficient a (Q15): v = x + a v(d back), out v(d back) - a v */
static inline int32_t rv_ap(int16_t *m, uint32_t at, uint32_t d, int32_t x, int32_t a)
{
    int32_t z = m[(at + d) & RV_MASK], v = sat16(x + mz15(z, a));
    m[at & RV_MASK] = (int16_t)v;
    return z - mz15(v, a);
}
/* the stretch at at read d + RV_SW + sw (Q8, |sw| <= RV_SW steps) back, between two samples (rounded: a floor here
 * goes round the loop, an offset of a few samples' worth) */
static inline int32_t rv_rd(const int16_t *m, uint32_t at, uint32_t d, int32_t sw)
{
    uint32_t q = (uint32_t)((int32_t)((d + RV_SW) * 256u) + sw), i = at + (q >> 8);
    int32_t a = m[i & RV_MASK], b = m[(i + 1u) & RV_MASK];
    return a + (((b - a) * (int32_t)(q & 255u) + 128) >> 8);
}
/* the same all-pass with its delay swept */
static inline int32_t rv_ap_sw(int16_t *m, uint32_t at, uint32_t d, int32_t sw, int32_t x, int32_t a)
{
    int32_t z = rv_rd(m, at, d, sw), v = sat16(x + mz15(z, a));
    m[at & RV_MASK] = (int16_t)v;
    return z - mz15(v, a);
}
/* 2^-x, x Q16: Q16 */
static uint32_t exp2n(uint32_t x)
{
    uint32_t i = x >> 16, j = (x >> 10) & 63u, r = x & 1023u, v;
    if (i > 16u) return 0;
    v = T_EXP2[j] - (((T_EXP2[j] - T_EXP2[j + 1]) * r) >> 10);
    return v >> i;
}
/* the decay gains for type t at SIZE sz: each line's own, so all lose 60 dB in the same time */
DMK_SMALL static void rv_gains(rvs_t *r, int t, int sz)
{
    static const uint16_t PLH = 112 + 742 + 300 + 620;  /* (PLATE: a half's length, round which the decay is taken) */
    uint32_t q = exp2n((uint32_t)sz * RV_SPAN[t] / 127u), g;
    int k;
    for (k = 0; k < 4; k++) {
        uint32_t len = t == RV_ROOM ? RM_L[k] : t == RV_HALL ? HL_L[k] : t == RV_PLATE ? PLH : SP_L;
        g = exp2n((uint32_t)(((uint64_t)len * RV_K[t] * q) >> 16)) >> 1;
        r->lg[k] = (int16_t)(g > 32600u ? 32600u : g);
    }
    r->gty = (uint8_t)t; r->gsz = (uint8_t)(sz + 1);
}
/* the sweeps, a block at a time (slow: held between): two sines, depth d (Q8 samples), at two rates (a step's) */
static void rv_sweep(rvs_t *r, int32_t d, uint32_t f0, uint32_t f1)
{
    r->ph[0] += f0 * RV_ST; r->ph[1] += f1 * RV_ST;
    r->sw[0] = (sinp(r->ph[0]) * d) >> 15;
    r->sw[1] = (sinp(r->ph[1]) * d) >> 15;
}

/* ROOM and HALL, a block: the input through two all-passes (ag0, ag1); the four lines, damped, mixed, each through
 * its decay (the Hadamard's half in it), the input in with alternate signs; out the lines in two pairs */
static inline __attribute__((always_inline)) void rv_fdn(rvs_t *r, int16_t *m, const uint16_t *L, const uint16_t *B, const uint16_t *AD, const uint16_t *AB,
                   int32_t ag0, int32_t ag1, int swept, const int32_t *x, int32_t c, int32_t *yl, int32_t *yr)
{
    uint32_t w = r->w;
    int32_t lp[4], le[4], ge[4], g[4], sw0 = r->sw[0], sw1 = r->sw[1];
    int i, k;
    for (k = 0; k < 4; k++) { lp[k] = r->lp[k]; le[k] = r->le[k]; ge[k] = r->ge[k]; g[k] = r->lg[k] >> 1; }
    for (i = 0; i < RV_ST; i++, w--) {
        int32_t u[4], s[4], xi = x[i];
        xi = rv_ap(m, w + AB[0], AD[0], xi, ag0);
        xi = rv_ap(m, w + AB[1], AD[1], xi, ag1);
        u[0] = rv_lp(&lp[0], &le[0], m[(w + B[0] + L[0]) & RV_MASK], c);
        u[1] = rv_lp(&lp[1], &le[1], swept ? rv_rd(m, w + B[1], L[1], sw0) : m[(w + B[1] + L[1]) & RV_MASK], c);
        u[2] = rv_lp(&lp[2], &le[2], m[(w + B[2] + L[2]) & RV_MASK], c);
        u[3] = rv_lp(&lp[3], &le[3], swept ? rv_rd(m, w + B[3], L[3], sw1) : m[(w + B[3] + L[3]) & RV_MASK], c);
        s[0] = u[0] + u[1] + u[2] + u[3]; s[1] = u[0] - u[1] + u[2] - u[3];
        s[2] = u[0] + u[1] - u[2] - u[3]; s[3] = u[0] - u[1] - u[2] + u[3];
        for (k = 0; k < 4; k++) {                       /* (the half in the gain: Q14 over 2^15) */
            int32_t v = mr15(s[k], g[k], &ge[k]) + (k & 1 ? -xi : xi);
            METER(dmeter.sat_rv += v > 32767 || v < -32768);
            m[(w + B[k]) & RV_MASK] = (int16_t)sat16(v);
        }
        yl[i] = u[0] + u[2]; yr[i] = u[1] + u[3];
    }
    for (k = 0; k < 4; k++) { r->lp[k] = lp[k]; r->le[k] = le[k]; r->ge[k] = ge[k]; }
    r->w = w;
}
#define RVM(b, d) m[(w + (uint32_t)(b) + (uint32_t)(d)) & RV_MASK]
static void rv_plate(rvs_t *r, int16_t *m, const synth_t *p, const int32_t *x, int32_t *yl, int32_t *yr)
{
    uint32_t w = r->w;
    int32_t c = p->rdamp, g = r->lg[0], x4 = r->x4, lp0 = r->lp[0], le0 = r->le[0], lp1 = r->lp[1], le1 = r->le[1];
    int32_t ge0 = r->ge[0], ge1 = r->ge[1], sw0, sw1;
    int i;
    rv_sweep(r, p->rx * (RV_SW * 256) / 127, 155830u, 116872u);  /* MOD: the halves' first all-passes, 0.8 and 0.6 Hz */
    sw0 = r->sw[0]; sw1 = r->sw[1];
    for (i = 0; i < RV_ST; i++, w--) {
        int32_t xi = x[i], a, b;
        xi = rv_ap(m, w, PL_AP, xi, 24576);
        a = xi + mr15(x4, g, &ge0);                     /* the first half, from the second's end */
        a = rv_ap_sw(m, w + PL_B[0], PL_L[0], sw0, a, 22938);   /* 0.7 */
        RVM(PL_B[1], 0) = (int16_t)sat16(a);
        a = rv_lp(&lp0, &le0, RVM(PL_B[1], PL_L[1]), c);
        a = rv_ap(m, w + PL_B[2], PL_L[2], a, 16384);   /* 0.5 */
        RVM(PL_B[3], 0) = (int16_t)sat16(a);
        b = xi + mr15(RVM(PL_B[3], PL_L[3]), g, &ge1); /* the second, from the first's */
        b = rv_ap_sw(m, w + PL_B[4], PL_L[4], sw1, b, 22938);
        RVM(PL_B[5], 0) = (int16_t)sat16(b);
        b = rv_lp(&lp1, &le1, RVM(PL_B[5], PL_L[5]), c);
        b = rv_ap(m, w + PL_B[6], PL_L[6], b, 16384);
        RVM(PL_B[7], 0) = (int16_t)sat16(b);
        x4 = RVM(PL_B[7], PL_L[7]);
        /* his taps, at a sixth: the left from the second half and the first's line, the right the other way */
        yl[i] = RVM(PL_B[5], 44) + RVM(PL_B[5], 497) - RVM(PL_B[6], 319);
        yr[i] = RVM(PL_B[1], 59) + RVM(PL_B[1], 606) - RVM(PL_B[2], 205);
    }
    r->x4 = x4; r->lp[0] = lp0; r->le[0] = le0; r->lp[1] = lp1; r->le[1] = le1; r->ge[0] = ge0; r->ge[1] = ge1;
    r->w = w;
}
static void rv_spring(rvs_t *r, int16_t *m, const synth_t *p, const int32_t *x, int32_t *yl, int32_t *yr)
{
    uint32_t w = r->w;
    int32_t a = 1843 + p->rx * 9, c = p->rdamp, g = r->lg[0];   /* DRIP: the all-passes' a, 0.45 .. 0.73 (Q12; past, it rings) */
    int32_t lp = r->lp[0], le = r->le[0], ge = r->ge[0], sh = r->sh, se = r->se;
    int i, k;
    rv_sweep(r, 384, 63371u, 87720u);                   /* the pickups' wobble, 1.5 samples, 0.33 and 0.45 Hz */
    for (i = 0; i < RV_ST; i++, w--) {
        int32_t v = RVM(0, SP_L), e, pz, *st = r->ap[w & 1u];
        v = mr15(rv_lp(&lp, &le, v, c), g, &ge) + x[i];
        e = (v - sh) * 1400 + se;                       /* a low cut, ~150 Hz: a spring carries little bass */
        sh += e >> 15; se = e & 32767;
        v -= sh;
        pz = st[0]; st[0] = v;                          /* each stage: a (x - y 2 back) + x 2 back, towards zero */
        for (k = 1; k <= 8; k++) {
            int32_t t = (v - st[k]) * a, o = st[k];
            v = ((t + ((t >> 31) & 4095)) >> 12) + pz;
            pz = o; st[k] = v;
        }
        RVM(0, 0) = (int16_t)sat16(v);
        yl[i] = rv_rd(m, w, SP_L / 2u - RV_SW, r->sw[0]);   /* the pickups: half way, three quarters */
        yr[i] = rv_rd(m, w, SP_L * 3u / 4u - RV_SW, r->sw[1]);
    }
    r->lp[0] = lp; r->le[0] = le; r->ge[0] = ge; r->sh = sh; r->se = se;
    r->w = w;
}
#undef RVM

/* a synth's reverb silent: its lines and its state (its type, its share and its way in kept) */
DMK_SMALL static void rv_clear(int s)
{
    rvs_t *r = &rvs[s];
    uint8_t cur = r->cur, *z = (uint8_t *)r;
    int32_t g = r->g, gin = r->gin;
    uint32_t i;
    for (i = 0; i < RV_MEM; i++) rv_mem[s][i] = 0;
    for (i = 0; i < sizeof *r; i++) z[i] = 0;
    r->cur = cur; r->g = g; r->gin = gin;
}

/* the reverbs, a run at a time: each synth's way in from its chain (vbuf, as the mix takes it: its level, the song's
 * fade, the LFO's, its AMOUNT: rv_gb, set before), two samples into each step; out, added at half rate to out_l and
 * out_r (rv_out: the three's sum; the mix takes it to the full rate, the sample between two steps half of each). A
 * synth's own state only, so they run on either core */
static int32_t rv_out[2][VB_MAX / 2], rv_prev[2];
static int32_t rv_gb[DMK_SYNTHS][VB_MAX / DMK_SUB];
static int rv_nb;
static void rv_run(int s, int32_t *out_l, int32_t *out_r)
{
    rvs_t *r = &rvs[s];
    const synth_t *p = &syn[s];
    int16_t *m = rv_mem[s];
    int b, i, want = p->rtype < RV_N ? p->rtype : RV_OFF;
    for (b = 0; b < rv_nb; b++) {
        const int32_t *sl = &vbuf[0][s][b * DMK_SUB], *sr = &vbuf[1][s][b * DMK_SUB];
        int32_t g0, g1, gi0 = r->gin, gi1 = rv_gb[s][b], x[RV_ST], yl[RV_ST], yr[RV_ST];
        int t;
        if (want != r->cur && !r->cur) { r->cur = (uint8_t)want; rv_clear(s); r->g = 0; }   /* from off: in, from silence */
        g0 = r->g; g1 = clampi(g0 + (want != r->cur ? -XG_STEP : XG_STEP), 0, 32768);
        r->g = g1; r->gin = gi1;
        t = r->cur;
        if (t && r->quiet >= RV_SLEEP) {                /* asleep: woken by any sound coming in */
            int wake = 0;
            if (gi0 | gi1) for (i = 0; i < DMK_SUB && !wake; i++) wake = sl[i] || sr[i];
            if (wake) r->quiet = 0; else t = RV_OFF;
        }
        if (t) {
            int32_t hp = r->hp, he = r->he, any = 0, loud = 0, gs, gd;
            int sh = RV_IN[t];
            int32_t *ol = &out_l[b * RV_ST], *orr = &out_r[b * RV_ST];
            if (r->gty != t || r->gsz != p->rsize + 1) rv_gains(r, t, p->rsize);
            for (i = 0; i < RV_ST; i++) {               /* in: the two samples' mid, through the way in (glided) */
                int32_t mm = clampi((sl[2 * i] + sr[2 * i] + sl[2 * i + 1] + sr[2 * i + 1]) >> 2, -65535, 65535);
                int32_t gi = gi0 + (((gi1 - gi0) * (i + 1)) >> 3), v, e;
                v = ((mm * gi) >> 14) >> sh;
                e = (v - hp) * 940 + he;                /* a low cut at ~100 Hz: no boom in a tail */
                hp += e >> 15; he = e & 32767;
                x[i] = v - hp; any |= x[i];
            }
            r->hp = hp; r->he = he;
            if (t == RV_ROOM) {                         /* DIFFUSE: 0.3 (a few echoes: a small box) .. 0.75 (smooth) */
                int32_t a = 9830 + p->rx * 115;
                rv_fdn(r, m, RM_L, RM_B, RM_AP, RM_APB, a, a, 0, x, p->rdamp, yl, yr);
            } else if (t == RV_HALL) {                  /* MOD: to RV_SW either way, 0.37 and 0.53 Hz */
                rv_sweep(r, p->rx * (RV_SW * 256) / 127, 72073u, 103236u);
                rv_fdn(r, m, HL_L, HL_B, HL_AP, HL_APB, 20316, 18022, 1, x, p->rdamp, yl, yr);   /* (0.62, 0.55) */
            } else if (t == RV_PLATE) rv_plate(r, m, p, x, yl, yr);
            else rv_spring(r, m, p, x, yl, yr);
            /* out: the type's level and its share, glided across the block */
            gs = (RV_OUT[t] * g0) >> 12; gd = (((RV_OUT[t] * g1) >> 12) - gs) / RV_ST;
            for (i = 0; i < RV_ST; i++) {
                int32_t a = yl[i], c = yr[i];
                loud |= (uint32_t)(a + 3) > 6u || (uint32_t)(c + 3) > 6u;
                gs += gd;
                ol[i] += (a * gs) >> 11; orr[i] += (c * gs) >> 11;
            }
            if (!any && !loud) { r->quiet = (uint16_t)(r->quiet + RV_ST > RV_SLEEP ? RV_SLEEP : r->quiet + RV_ST); }
            else r->quiet = 0;
        }
        if (want != r->cur && !g1) { r->cur = (uint8_t)want; if (want) rv_clear(s); }   /* out: the new type, from silence */
    }
}
static int rv_busy(int s) { return rvs[s].cur || syn[s].rtype; }

/* ---- the SPACE (the owner's, 2026-10-26: "Space will contain the huge different reverbs in global", on GLO only, as
 * cheap as it can be): one tank that all three synths send to (each FX Sends row's first knob, Home's Space row), at
 * half rate, the second core's job. The shared verbs of before 0.4.17 back as its types, on one engine and made safe:
 * the input through four all-passes into Dattorro's figure of eight at full size (two halves, each a swept all-pass, a
 * line, TONE's damping, an all-pass, a line, into the other half through the decay; the reverbs a track's PLATE is it
 * at a sixth), its SIZE the decay of the type's range, its MOD the sweep. HALL to 21 s, swept a little; SPACE to two
 * minutes, wider; HAZE deep and slow, darker; BLOOM the hall fed through a swell (a slow follower over a fast one: a
 * new sound rises into the space); LOFI held at 11 kHz into the tank, kept at fewer bits in it and out of it, the
 * hall's decay, wowing deep;
 * SHIMMER its sound an octave up fed back in (a pitch shifter: two taps sliding through a short window, crossfaded);
 * REVERSE the sound read backwards in segments into a mid tail; FREEZE nearly lossless, only so much new sound let in
 * (HOLD: all of it held at the top, five minutes). The eight-line network of before cost twice this for the same
 * types, which with three reverbs a track no longer fit. As the reverbs a track are: one memory a place runs down
 * through, every line a stretch of it, every product round the loop with its remainder kept (or, in an all-pass,
 * towards zero), so a tail decays to exact silence and holds no DC (the old ones' bugs gone: FREEZE's damping past
 * one, SHIMMER's DC latched round its octaves, sums past 32 bits) */
#define SP_MEM 16384u
#define SP_MASK (SP_MEM - 1u)
#define SR_LEN 16384u                                   /* REVERSE: 0.74 s of mu-law, read backwards */
#define SH_LEN 1024u                                    /* SHIMMER: the shifter's window, 46 ms (in REVERSE's memory) */
static int16_t sp_mem[SP_MEM];                          /* (in RAM: the pool holds the rest) */
static union { uint8_t b[SR_LEN]; int16_t h[SR_LEN / 2]; } sr POOL;
/* the stretches, in half-rate steps: four all-passes in (0.75, 0.75, 0.625, 0.625); the tank's halves, each a swept
 * all-pass (0.7, its stretch 2 SP_SW + 2 longer), a line, an all-pass (0.5), a line: Dattorro's lengths at 0.7 */
static const uint16_t FD_AL[4] = {142, 107, 379, 277}, FD_AB[4] = {0, 143, 251, 631};
static const int16_t FD_AG[4] = {24576, 24576, 20480, 20480};
#define SP_SW 32                                        /* a swept all-pass's reach, steps either way */
static const uint16_t TK_L[8] = {470, 3117, 1260, 2604, 636, 2952, 1859, 2214};
static const uint16_t TK_B[8] = {909, 1445, 4563, 5824, 8429, 9131, 12084, 13944};
static struct {
    uint32_t w, ph[2], rvw, rva, shf;                   /* the place; the sweeps; REVERSE's write (SHIMMER's) and its heads'
                                                           age; the shift */
    int32_t hp, he, lp[2], le[2], ge[2], xb, sw[2];     /* the low cut in; the damping and the decays' remainders; the
                                                           second half's end, into the first; the sweeps (Q8) */
    int32_t fast, slow, hold, sfb, shs, she, shp;       /* BLOOM's followers; LOFI's held; SHIMMER's way in, its low cut */
    int32_t g;                                          /* the type's share, Q15 */
    uint16_t quiet;
    uint8_t cur, half;
} sps;
static int32_t sp_gb[DMK_SYNTHS][VB_MAX / DMK_SUB];     /* each synth's way in a block: its level, the fade, the LFO's
                                                           tremolo, its send (Q15), set before the job */
/* its queues, a block (eight steps) a row: the sends in, a run late; out, a run's reserve ahead (so any run's length
 * finds its blocks there), the latency a run's at most (5.8 ms, as the shared verb's was) */
#define SP_Q 32u
static int32_t spq_in[SP_Q][RV_ST] POOL, spq_out[SP_Q][2][RV_ST] POOL;
static uint32_t spq_i0, spq_i1, spq_o0, spq_o1;          /* in: read, written; out: read, written (counts, mod SP_Q) */
static uint8_t sp_live;                                 /* the queues running: the Space on, or fading out */
static int sp_nb;

/* a tank half's decay, a * g / 2^15: the remainder kept (in *e), so a tail decays as it should; at the last few
 * samples' worth towards zero, so it ends (the all-passes pass a sample of one on as it is: the remainders' own noise
 * went round for ever in the near-lossless ones) */
static inline int32_t sp_dec(int32_t a, int32_t g, int32_t *e)
{
    if (a < 16 && a > -16) { *e = 0; return mz15(a, g); }
    return mr15(a, g, e);
}
static inline int32_t sp_ap(int16_t *m, uint32_t at, uint32_t d, int32_t x, int32_t a)
{
    int32_t z = m[(at + d) & SP_MASK], v = sat16(x + mz15(z, a));
    m[at & SP_MASK] = (int16_t)v;
    return z - mz15(v, a);
}
static inline int32_t sp_ap_sw(int16_t *m, uint32_t at, uint32_t d, int32_t sw, int32_t x, int32_t a)
{
    uint32_t q = (uint32_t)((int32_t)((d + SP_SW) * 256u) + sw), i = at + (q >> 8);
    int32_t z = m[i & SP_MASK], v;
    z += ((m[(i + 1u) & SP_MASK] - z) * (int32_t)(q & 255u) + 128) >> 8;
    v = sat16(x + mz15(z, a));
    m[at & SP_MASK] = (int16_t)v;
    return z - mz15(v, a);
}
/* REVERSE: the sound in, read by two heads from each segment's end back to its start, half a segment apart, each
 * faded in and out (triangles summing to one) */
static inline int32_t sp_reverse(int32_t x)
{
    int32_t s = 0, len = gp.srlen, k;
    if (sps.rva >= (uint32_t)len) sps.rva = 0;          /* (LENGTH turned shorter) */
    sr.b[sps.rvw] = mulaw_enc(curve(T_SAT, x));
    for (k = 0; k < 2; k++) {
        int32_t a = (int32_t)sps.rva + (k ? len / 2 : 0), g;
        if (a >= len) a -= len;
        g = ((a < len / 2 ? a : len - a) * gp.srinv) >> 8;   /* to 32768 half way */
        s += (T_MULAW[sr.b[(sps.rvw - 2u * (uint32_t)a) & (SR_LEN - 1u)]] * g) >> 15;
    }
    if (++sps.rva >= (uint32_t)len) sps.rva = 0;
    sps.rvw = (sps.rvw + 1u) & (SR_LEN - 1u);
    return s;
}
/* SHIMMER: the tank's last output an octave up (two taps reading its window twice as fast), its lows cut (~150 Hz,
 * the remainder kept: no DC goes round the octaves), its top softened, added in by SHINE */
static inline int32_t sp_shimmer(int32_t x)
{
    int32_t s = 0, e, v, k;
    sr.h[sps.rvw & (SH_LEN - 1u)] = (int16_t)sat16(sps.sfb);
    for (k = 0; k < 2; k++) {
        int32_t d = (int32_t)SH_LEN - 1 - (int32_t)((sps.shf + (k ? SH_LEN / 2u : 0u)) & (SH_LEN - 1u));
        int32_t g = (d < (int32_t)SH_LEN / 2 ? d : (int32_t)SH_LEN - d) * (65536 / (int32_t)SH_LEN);
        s += (sr.h[(sps.rvw - (uint32_t)d) & (SH_LEN - 1u)] * g) >> 15;
    }
    sps.rvw++; sps.shf++;
    e = (s - sps.shs) * 1400 + sps.she; sps.shs += e >> 15; sps.she = e & 32767;
    s -= sps.shs;
    v = (s + sps.shp) / 2; sps.shp = s;
    return x + ((v * gp.sshine) >> 15);
}
#define TKM(k, d) m[(w + TK_B[k] + (uint32_t)(d)) & SP_MASK]
#define TKQ(v) (lofi ? ((v) < 0 ? -(-(v) & ~7) : (v) & ~7) : (v))   /* (LOFI: fewer bits kept, towards zero) */
/* a block (eight steps) of type t: its way in (in: the sends' sum a step, at the mix's level), the type's own, the
 * all-passes, the tank; out to ol, orr at the type's level, its return and its share glided from g0 to g1 */
#define SP_IN 2                                         /* the way in down, the way out up (bits): the tank's headroom */
static void sp_block(int t, const int32_t *in, int32_t *ol, int32_t *orr, int32_t g0, int32_t g1)
{
    int16_t *m = sp_mem;
    uint32_t w = sps.w;
    int32_t yl[RV_ST], yr[RV_ST], damp = gp.sdamp, ga = gp.sga, gb = gp.sgb, hp = sps.hp, he = sps.he, bg = 32768;
    int32_t lpa = sps.lp[0], lea = sps.le[0], lpb = sps.lp[1], leb = sps.le[1], gea = sps.ge[0], geb = sps.ge[1], xb = sps.xb;
    int32_t any = 0, loud = 0, gq = (gp.sgain * gp.sret) >> 8, gs, gd, sw0, sw1;
    int i, k, lofi = t == SP_LOFI;
    sps.ph[0] += gp.srate * RV_ST; sps.ph[1] += (gp.srate + (gp.srate >> 2)) * RV_ST;   /* the sweeps, a block at a time */
    sw0 = (sinp(sps.ph[0]) * gp.smod) >> 15; sw1 = (sinp(sps.ph[1]) * gp.smod) >> 15;
    if (t == SP_BLOOM) {                                /* the swell's gain this block: the slow over the fast */
        int32_t sn = sps.slow >> 8 > 32767 ? 32767 : sps.slow >> 8, f = sps.fast >> 8;
        bg = f > 0 ? (sn << 15) / (f + 1) : 32768;
        if (bg > 32768) bg = 32768;
    }
    for (i = 0; i < RV_ST; i++, w--) {
        int32_t x = clampi(in[i] >> (1 + SP_IN), -32767, 32767), e, a, b;   /* (halved: the lines' headroom at every knob's top) */
        e = (x - hp) * 470 + he; hp += e >> 15; he = e & 32767;   /* a low cut, ~50 Hz: no boom building up */
        x -= hp;
        any |= x;
        if (t == SP_REVERSE) x = sp_reverse(x);
        else if (t == SP_SHIMMER) x = sp_shimmer(x);
        else if (t == SP_FREEZE) x = (x * gp.sgin) >> 15;
        else if (lofi) { if ((sps.half ^= 1)) sps.hold = x; x = sps.hold; }
        else if (t == SP_BLOOM) {
            int32_t ax = (x < 0 ? -x : x) << 8;
            sps.fast += ax > sps.fast ? (ax - sps.fast) >> 4 : -(sps.fast >> 12) - (sps.fast > 0);
            sps.slow += ax > sps.slow ? (ax - sps.slow) >> 14 : -(sps.slow >> 16) - (sps.slow > 0);
            x = (x * bg) >> 15;
        }
        for (k = 0; k < 4; k++) x = sp_ap(m, w + FD_AB[k], FD_AL[k], x, FD_AG[k]);
        a = x + sp_dec(xb, ga, &gea);                   /* the first half, from the second's end */
        a = sp_ap_sw(m, w + TK_B[0], TK_L[0], sw0, a, 22938);
        TKM(1, 0) = (int16_t)sat16(TKQ(a));
        a = rv_lp(&lpa, &lea, TKM(1, TK_L[1]), damp);
        a = sp_ap(m, w + TK_B[2], TK_L[2], a, 16384);
        TKM(3, 0) = (int16_t)sat16(TKQ(a));
        b = x + sp_dec(TKM(3, TK_L[3]), gb, &geb);      /* the second, from the first's */
        b = sp_ap_sw(m, w + TK_B[4], TK_L[4], sw1, b, 22938);
        TKM(5, 0) = (int16_t)sat16(TKQ(b));
        b = rv_lp(&lpb, &leb, TKM(5, TK_L[5]), damp);
        b = sp_ap(m, w + TK_B[6], TK_L[6], b, 16384);
        TKM(7, 0) = (int16_t)sat16(TKQ(b));
        xb = TKM(7, TK_L[7]);
        METER(dmeter.sat_sp += a > 32767 || a < -32768 || b > 32767 || b < -32768);
        /* his taps, at 0.7: the left from the second half's lines and the first's, the right the other way */
        yl[i] = TKM(5, 186) + TKM(5, 2082) - TKM(6, 1339) + TKM(7, 1397) - TKM(1, 1393) - TKM(2, 131) - TKM(3, 746);
        yr[i] = TKM(1, 247) + TKM(1, 2539) - TKM(2, 860) + TKM(3, 1871) - TKM(5, 1478) - TKM(6, 235) - TKM(7, 85);
        sps.sfb = (yl[i] + yr[i]) / 8;                  /* (SHIMMER's: the sound to shift) */
    }
    sps.lp[0] = lpa; sps.le[0] = lea; sps.lp[1] = lpb; sps.le[1] = leb; sps.ge[0] = gea; sps.ge[1] = geb; sps.xb = xb;
    sps.hp = hp; sps.he = he; sps.w = w;
    gs = (int32_t)(((int64_t)gq * g0) >> 15); gd = ((int32_t)(((int64_t)gq * g1) >> 15) - gs) / RV_ST;
    for (i = 0; i < RV_ST; i++) {                       /* (in 64 bits: seven taps at full, the return up) */
        int32_t p, c;
        loud |= (uint32_t)(yl[i] + 3) > 6u || (uint32_t)(yr[i] + 3) > 6u;
        gs += gd;
        if (lofi) { yl[i] = TKQ(yl[i]); yr[i] = TKQ(yr[i]); }   /* out at fewer bits too: the tank's, before the return
                                                           (at the mix's 8 bits a quiet return was cut to nothing) */
        p = (int32_t)(((int64_t)yl[i] * gs) >> (16 - SP_IN)); c = (int32_t)(((int64_t)yr[i] * gs) >> (16 - SP_IN));
        ol[i] = clampi(p, -98304, 98304); orr[i] = clampi(c, -98304, 98304);   /* (three times full at most: the mix's
                                                           media takes it unheld) */
    }
    if (!any && !loud) sps.quiet = (uint16_t)(sps.quiet + RV_ST > RV_SLEEP ? RV_SLEEP : sps.quiet + RV_ST);
    else sps.quiet = 0;
}
#undef TKM
#undef TKQ
/* the Space silent: its lines, its state (its type and share kept); REVERSE's line (mu-law's silence) or SHIMMER's
 * window too when one comes in */
DMK_SMALL static void sp_clear(void)
{
    uint8_t cur = sps.cur, *z = (uint8_t *)&sps;
    int32_t g = sps.g;
    uint32_t i;
    for (i = 0; i < SP_MEM; i++) sp_mem[i] = 0;
    for (i = 0; i < sizeof sps; i++) z[i] = 0;
    sps.cur = cur; sps.g = g;
    if (cur == SP_REVERSE) for (i = 0; i < SR_LEN; i++) sr.b[i] = 0xFF;
    if (cur == SP_SHIMMER) for (i = 0; i < SH_LEN; i++) sr.h[i] = 0;
}
static int sp_busy(void) { return sps.cur || gp.stype; }
/* this run's sends into the queue, a block a row (the second core's, after the chains): each synth's way in from its
 * chain (vbuf, as the mix takes it, the chain's level back up; sp_gb set before), two samples into each step, the
 * send glided across the block */
static int32_t sp_gin[DMK_SYNTHS];                      /* each send where the last block ended */
static void sp_feed(int nb)
{
    int b, i, s;
    for (b = 0; b < nb && spq_i1 - spq_i0 < SP_Q; b++) {
        int32_t *x = spq_in[spq_i1++ & (SP_Q - 1u)];
        for (i = 0; i < RV_ST; i++) x[i] = 0;
        for (s = 0; s < DMK_SYNTHS; s++) {
            const int32_t *sl = &vbuf[0][s][b * DMK_SUB], *sr = &vbuf[1][s][b * DMK_SUB];
            int32_t gi0 = sp_gin[s], gi1 = sp_gb[s][b];
            sp_gin[s] = gi1;
            if (!(gi0 | gi1)) continue;
            for (i = 0; i < RV_ST; i++) {
                int32_t mm = clampi((sl[2 * i] + sr[2 * i] + sl[2 * i + 1] + sr[2 * i + 1]) >> 1, -65535, 65535);
                x[i] += (mm * (gi0 + (((gi1 - gi0) * (i + 1)) >> 3))) >> 15;
            }
        }
    }
}
/* up to n queued blocks through the Space, out queued: a type changed fades out (5.8 ms), the new one comes in from
 * silence; asleep (its tail at a few samples, nothing sent) until something is sent */
static void sp_work(uint32_t n)
{
    int i, want = gp.stype < SP_N ? gp.stype : SP_OFF;
    while (n-- && spq_i0 != spq_i1) {
        const int32_t *x = spq_in[spq_i0++ & (SP_Q - 1u)];
        int32_t g0, g1, *ol = spq_out[spq_o1 & (SP_Q - 1u)][0], *orr = spq_out[spq_o1 & (SP_Q - 1u)][1], any = 0;
        int t;
        spq_o1++;
        if (want != sps.cur && !sps.cur) { sps.cur = (uint8_t)want; sps.g = 0; sp_clear(); }
        g0 = sps.g; g1 = clampi(g0 + (want != sps.cur ? -XG_STEP : XG_STEP), 0, 32768);
        sps.g = g1;
        t = sps.cur;
        for (i = 0; i < RV_ST; i++) { ol[i] = orr[i] = 0; any |= x[i]; }
        if (t && sps.quiet >= RV_SLEEP) { if (any) sps.quiet = 0; else t = SP_OFF; }   /* asleep: woken by a send */
        if (t) sp_block(t, x, ol, orr, g0, g1);
        if (want != sps.cur && !g1) { sps.cur = (uint8_t)want; if (want) sp_clear(); }
    }
}
/* the queues from rest: nothing in, a run's reserve of silence out */
DMK_SMALL static void sp_rest(void)
{
    uint32_t i;
    for (i = 0; i < SP_Q * 2u * RV_ST; i++) (&spq_out[0][0][0])[i] = 0;
    spq_i0 = spq_i1 = spq_o0 = 0; spq_o1 = VB_MAX / DMK_SUB;
    for (i = 0; i < DMK_SYNTHS; i++) sp_gin[i] = 0;
}
/* the mix phase's job on the second core: the Space's blocks of a run ago still queued, this run's sends in; the first
 * synth's reverb (rv_out1: the first core runs the second's into rv_out meanwhile) */
static int32_t rv_out1[2][VB_MAX / 2] POOL;
static void sp_job(void)
{
    int i;
    if (sp_live) { sp_work(SP_Q); sp_feed(sp_nb); }
    if (rv_busy(0)) {
        for (i = 0; i < rv_nb * RV_ST; i++) rv_out1[0][i] = rv_out1[1][i] = 0;
        rv_run(0, rv_out1[0], rv_out1[1]);
    }
}

#ifdef DMK_BENCH
/* a known load for the device's calibration (docs/plan.md, 1.1): bench_level times a fixed integer kernel per
 * sample, K1 on the bench build; the host counts its instructions a level, the FM-1's meter shows its share */
static int bench_level;
static volatile int32_t bench_sink;
static void bench_block(int n)
{
    uint32_t x = (uint32_t)bench_sink | 1u;
    int32_t acc = 0;
    int i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < bench_level; k++) { x = x * 1664525u + 1013904223u; acc += (int32_t)(x >> 16) * 3; }
    bench_sink = acc ^ (int32_t)x;
}
#endif

/* a synth's three rows. A row whose type changes fades its effect out (5.8 ms), takes the new type and fades it in:
 * a type swapped in place clicks. MEDIA's line and the chorus's are written while their row is off, so a type
 * coming in reads the sound as it is, not what was there when it last ran */
static int xg_next(int sy, int row, int want)           /* the row's share at the block's end; its type taken when out */
{
    int32_t g;
    if (want != fx[sy].cur[row] && !fx[sy].cur[row]) { fx[sy].cur[row] = (uint8_t)want; fx[sy].xg[row] = 0; fx_enter(sy, row, want); }   /* from off: in */
    g = fx[sy].xg[row];
    g += want != fx[sy].cur[row] ? -XG_STEP : XG_STEP;
    return g < 0 ? 0 : g > 32768 ? 32768 : g;
}
static void xg_mix(int32_t *y, const int32_t *dry, int32_t g0, int32_t g1)
{
    int i;
    for (i = 0; i < DMK_SUB; i++) y[i] = dry[i] + (int32_t)(((int64_t)(y[i] - dry[i]) * (g0 + (g1 - g0) * i / DMK_SUB)) >> 15);
}
static void rows_run(int s, int32_t *x, int32_t *l, int32_t *r)
{
    const synth_t *p = &syn[s];
    int i;
    {                                                   /* MOTION: here (vibrato and drift are the voices'; COLOUR and
                                                           MEDIA the mix's, gm_side) */
        int want = p->motion == FO_VIB || p->motion == FO_DRIFT ? FO_OFF : p->motion;
        int32_t g1 = xg_next(s, 2, want), g0 = fx[s].xg[2];
        if (fx[s].cur[2]) {
            motion_block(s, x, l, r, fx[s].cur[2]);
            if (g0 < 32768 || g1 < 32768) { xg_mix(l, x, g0, g1); xg_mix(r, x, g0, g1); }
        } else
            for (i = 0; i < DMK_SUB; i++) { l[i] = r[i] = x[i]; ch_buf[s][fx[s].cw++ & (CH_LEN - 1)] = (int16_t)sat16(x[i]); }
        fx[s].xg[2] = g1;
        if (want != fx[s].cur[2] && !g1) { fx[s].cur[2] = (uint8_t)want; fx_enter(s, 2, want); }
    }
}

/* a synth's DELAY (the owner's, 2026-10-25: one a synth, the media one for the mix; 2026-10-26: the shared echo's two
 * kinds too): its line at a quarter of the rate (lofi, and the memory: 0.84 s), the input the synth after its rows.
 * DELAY and PING-PONG read at the tempo's division (dmk_seq sets dtime), glided to a new time; DELAY's two sides a hair
 * apart, PING-PONG's right twice as late and fed back from it, so the repeats go left, right. TAPE ECHO the shared
 * echo's of before: a free time (to 0.69 s) bent as it is turned and by a slow wow, its right a sixteenth later, the
 * loop darkened and driven into the tape's curve. GRAINS: the line read by six grains (below). Its wet glided in and
 * out (a type changed: out, then the new one in) */
#define DL_LEN 9216u                                    /* 0.84 s of 8-bit mu-law at 11.025 kHz: a quarter at 72 */
static uint8_t dl_buf[DMK_SYNTHS][DL_LEN] POOL;
static struct { uint32_t w, wph[2]; int32_t nl, nr, lp, hp, tq, g; uint8_t cur; } dls[DMK_SYNTHS];
/* GRAINS, the shared echo's other kind, a synth's own since 0.4.18: its line read by six grains at once, each from a
 * random moment of the last 0.8 s, as long as SIZE, as often as DENSITY, some an octave or a fifth up as TEXTURE
 * rises, each its own place across the stereo, shaped by a window; a cloud of what was played */
#define GR_N 6
#define GR_MAXLEN ((DL_LEN - 256u) / 2u)                /* (an octave up reads twice its length: both inside the line) */
typedef struct { uint32_t pos, inc, len, age, wph, wst; int16_t pl, pr; } grain_t;   /* (pos Q8; the window's place, Q16) */
static struct { grain_t g[GR_N]; uint32_t clock, rng; } grs[DMK_SYNTHS];
DMK_SMALL static void dl_clear(int s)
{
    uint32_t i;
    for (i = 0; i < DL_LEN; i++) dl_buf[s][i] = 0xFF;   /* (mu-law's silence) */
    dls[s].nl = dls[s].nr = dls[s].lp = dls[s].hp = 0;
    dls[s].tq = (int32_t)((dls[s].cur == DT_TAPE ? syn[s].etime : syn[s].dtime) << 8);
    for (i = 0; i < GR_N; i++) grs[s].g[i].len = 0;
    grs[s].clock = 0;
}
static int32_t dl_at(int s, uint32_t q8)               /* the line q8 (Q8) quarter samples back */
{
    int32_t i, j;
    if (q8 > (DL_LEN - 2u) << 8) q8 = (DL_LEN - 2u) << 8;   /* (a ping-pong fading out reads twice a delay's time) */
    i = (int32_t)(dls[s].w) - (int32_t)(q8 >> 8);
    if (i < 0) i += DL_LEN;
    j = i ? i - 1 : (int32_t)DL_LEN - 1;
    return T_MULAW[dl_buf[s][i]] + (((T_MULAW[dl_buf[s][j]] - T_MULAW[dl_buf[s][i]]) * (int32_t)(q8 & 255u)) >> 8);
}
/* a quarter-rate step of a synth's grains: a new one begun as DENSITY says (if one is free), each read on */
static void grains_step(int s, const synth_t *p, int32_t *ol, int32_t *orr)
{
    grain_t *g = grs[s].g;
    const uint8_t *m = dl_buf[s];
    int k;
    int32_t l = 0, r = 0;
    grs[s].clock += (uint32_t)p->gdens;
    if (grs[s].clock >= 65536u) {
        grs[s].clock -= 65536u;
        for (k = 0; k < GR_N && g[k].len; k++) {}
        if (k < GR_N) {
            uint32_t rn = xs32(&grs[s].rng), len = (uint32_t)p->glen, inc, back;
            if (len > GR_MAXLEN) len = GR_MAXLEN;
            if (len < 64u) len = 64u;
            if ((int32_t)((rn >> 8) & 127u) < p->gtex) inc = rn & 1u ? 512u : 384u;   /* an octave, or a fifth, up */
            else inc = 254u + (rn >> 30);               /* or as played, a hair off */
            back = len * inc / 256u + 64u + xs32(&grs[s].rng) % (DL_LEN - 2u * len - 128u);
            g[k].pos = (dls[s].w + (back <= dls[s].w ? 0u : DL_LEN) - back) << 8;
            g[k].inc = inc; g[k].len = len; g[k].age = 0; g[k].wph = 0; g[k].wst = (256u << 16) / len;
            g[k].pl = (int16_t)T_PAN[127 - (rn >> 25)]; g[k].pr = (int16_t)T_PAN[rn >> 25];
        }
    }
    for (k = 0; k < GR_N; k++) {
        uint32_t i, j, f;
        int32_t a, b, x, t;
        if (!g[k].len) continue;
        i = g[k].pos >> 8; f = g[k].pos & 255u; j = i + 1u >= DL_LEN ? 0u : i + 1u;
        a = T_MULAW[m[i]]; b = T_MULAW[m[j]];
        x = a + (((b - a) * (int32_t)f) >> 8);
        t = (int32_t)(g[k].wph >> 16);                  /* the window: a parabola over the grain */
        x = (x * ((t * (255 - t)) >> 6)) >> 8;
        l += (x * g[k].pl) >> 15;
        r += (x * g[k].pr) >> 15;
        g[k].pos += g[k].inc;
        if (g[k].pos >= DL_LEN << 8) g[k].pos -= DL_LEN << 8;
        g[k].wph += g[k].wst;
        if (++g[k].age >= g[k].len) g[k].len = 0;
    }
    *ol = l / 2; *orr = r / 2;                          /* (as the shared one came back: half the tape's) */
}
static void syn_delay(int s, int32_t *pl, int32_t *pr)
{
    const synth_t *p = &syn[s];
    int want = p->dtype < DT_N ? p->dtype : DT_OFF, q, k, t;
    int32_t g0 = dls[s].g, g1, to, el, er;
    if (want != dls[s].cur && !dls[s].cur) { dls[s].cur = (uint8_t)want; dl_clear(s); dls[s].g = g0 = 0; }   /* from off: in */
    to = want == dls[s].cur ? p->dmix : 0;
    g1 = g0 + clampi(to - g0, -1024, 1024);
    el = (dls[s].nl * g0) >> 15; er = (dls[s].nr * g0) >> 15;   /* where the last block left it, at its share then */
    t = dls[s].cur;
    if (t)
        for (q = 0; q < 4; q++) {                       /* four steps at a quarter of the rate */
            const int32_t *a = pl + q * 4, *c = pr + q * 4;
            int32_t in = (a[0] + a[1] + a[2] + a[3] + c[0] + c[1] + c[2] + c[3]) >> 3, l, r, gq, dl, dr, al, ar;
            if (t == DT_GRAINS) {                       /* the line as it is played (no regen), the grains reading it */
                dl_buf[s][dls[s].w] = mulaw_enc(curve(T_SAT, in * 2) / 2);
                if (++dls[s].w >= DL_LEN) dls[s].w = 0;
                grains_step(s, p, &l, &r);
            } else {
                int32_t back, v;
                uint32_t tq;
                if (t == DT_TAPE) {                     /* its free time glided (bent as it turns), a slow wow on it */
                    int32_t wow;
                    dls[s].tq += ((int32_t)(p->etime << 8) - dls[s].tq) >> 8;
                    dls[s].wph[0] += 194783u; dls[s].wph[1] += 50663u;   /* 0.5 and 0.13 Hz: ~9 cents, to 2.7 ms */
                    wow = (sinp(dls[s].wph[0]) * 9 + sinp(dls[s].wph[1]) * 6) >> 6;
                    tq = (uint32_t)(dls[s].tq + wow);
                    l = dl_at(s, tq);
                    r = dl_at(s, tq + (tq >> 4));
                } else {
                    dls[s].tq += ((int32_t)(p->dtime << 8) - dls[s].tq) >> 8;
                    tq = (uint32_t)dls[s].tq;
                    l = dl_at(s, tq);
                    r = t == DT_PING ? dl_at(s, tq * 2u) : dl_at(s, tq + (tq >> 6));
                }
                back = t == DT_PING ? r : (l + r) >> 1;
                dls[s].lp += ((back - dls[s].lp) * p->dtone + 16384) >> 15;   /* the loop: its tone, its lows out, soft */
                dls[s].hp += (dls[s].lp * 256 - dls[s].hp) >> 6;
                v = in + (((dls[s].lp - ((dls[s].hp + 128) >> 8)) * p->dfb + 16384) >> 15);
                dl_buf[s][dls[s].w] = mulaw_enc(t == DT_TAPE ? curve(T_SAT, v * 2) / 2 : curve(T_SAT, v));   /* (the tape
                                                           driven as the shared one was, at the mix's level) */
                if (++dls[s].w >= DL_LEN) dls[s].w = 0;
            }
            dls[s].nl = l; dls[s].nr = r;
            gq = g0 + (((g1 - g0) * (q + 1)) >> 2);     /* its share glided across the block, at each step */
            l = (l * gq) >> 15; r = (r * gq) >> 15;
            {                                           /* the dry under it as it rises: to -3.5 dB (both at full were 5
                                                           to 11 dB over the sound) */
                int32_t dg = 32768 - ((gq * 11) >> 5);
                for (k = 0; k < 4; k++) { pl[q * 4 + k] = (pl[q * 4 + k] * (dg >> 3)) >> 12; pr[q * 4 + k] = (pr[q * 4 + k] * (dg >> 3)) >> 12; }
            }
            dl = l - el; dr = r - er; al = el * 4; ar = er * 4;
            for (k = 0; k < 4; k++) {                   /* up to the full rate: a line from the last step to this */
                al += dl; ar += dr;
                pl[q * 4 + k] += al >> 2; pr[q * 4 + k] += ar >> 2;
            }
            el = l; er = r;
        }
    dls[s].g = g1;
    if (want != dls[s].cur && !g1) { dls[s].cur = (uint8_t)want; if (want) dl_clear(s); }
}

/* the whole mix's COLOUR and MEDIA (the owner's: one each, for the mix; the colour since 0.4.17): each side through
 * both, the state of the left in slot 0 and the right in slot 1, the same settings (gcol, gmed) and the same dice, so
 * the two are one; a fade when a type changes. The colour and the tapes at half the level, so they bend as a synth did
 * (the mix is up to twice one); the later media as it is: their crackle, their hiss and the dictaphone's level are
 * their own, not the sound's. Planned a run at a time (a type and a share a block, the dice a type takes on its way
 * in), so the two sides, one on each core, run the same */
static synth_t gcol, gmed;
#define GM_NB (VB_MAX / DMK_SUB)
static int32_t gm_x[2][VB_MAX];                         /* the run's mix, its two sides (in RAM) */
typedef struct {
    int32_t g, g0[GM_NB], g1[GM_NB];
    uint32_t seed;
    uint8_t cur, pend, ty[GM_NB], in[GM_NB];            /* (pend: a type to take in on the next run's first block) */
} mrow_t;
static mrow_t mrow[2];                                  /* the colour, the media */
static int gm_nb;
static void mrow_plan(mrow_t *m, int want, int nb)
{
    int b, enter = m->pend;
    for (b = 0; b < nb; b++) {
        if (want != m->cur && !m->cur) { m->cur = (uint8_t)want; m->g = 0; enter = 1; }   /* from off: in */
        m->ty[b] = m->cur; m->in[b] = (uint8_t)enter; enter = 0;
        m->g0[b] = m->g;
        m->g = clampi(m->g + (want != m->cur ? -XG_STEP : XG_STEP), 0, 32768);
        m->g1[b] = m->g;
        if (want != m->cur && !m->g) { m->cur = (uint8_t)want; enter = 1; }   /* out: the new type from the next block */
        if (m->in[b]) m->seed = m->seed * 1664525u + 1013904223u;
    }
    m->pend = (uint8_t)enter;
}
static int mrow_busy(const mrow_t *m) { return m->cur || m->ty[0] || m->pend; }
static void gm_side(int c)
{
    int b, i;
    int32_t dry[DMK_SUB];
    const mrow_t *mc = &mrow[0], *mm = &mrow[1];
    for (b = 0; b < gm_nb; b++) {
        int32_t *x = &gm_x[c][b * DMK_SUB], g0 = mc->g0[b], g1 = mc->g1[b];
        int t = mc->ty[b], half;
        if (mc->in[b]) { fx_enter(c, 0, t); fx[c].rng = (mc->seed ^ (c && t == FC_NOISE ? 0x5BD1E995u : 0u)) | 1u; }   /* (the
                                                           same dice both sides: one clock, the same holes; NOISE's hiss
                                                           each its own) */
        if (t) {
            int fading = g0 < 32768 || g1 < 32768;
            for (i = 0; i < DMK_SUB; i++) x[i] = clampi(x[i] / 2, -32767, 32767);   /* (past twice full: held, as the
                                                           colours' one-poles and curves take a synth's range) */
            if (fading) for (i = 0; i < DMK_SUB; i++) dry[i] = x[i];
            colour_block(c, &gcol, x, t);
            if (fading) xg_mix(x, dry, g0, g1);
            for (i = 0; i < DMK_SUB; i++) x[i] *= 2;
        }
        t = mm->ty[b]; half = t && t < FM_VINYL; g0 = mm->g0[b]; g1 = mm->g1[b];
        if (mm->in[b]) { fx_enter(c, 1, t); fx[c].mrng = mm->seed; }
        if (half) for (i = 0; i < DMK_SUB; i++) x[i] /= 2;
        if (t) {
            int fading = g0 < 32768 || g1 < 32768;
            if (fading) for (i = 0; i < DMK_SUB; i++) dry[i] = x[i];
            if (t >= FM_VINYL) media_new(c, &gmed, x, t); else media_block(c, &gmed, x, t);
            if (fading) xg_mix(x, dry, g0, g1);
        } else
            for (i = 0; i < DMK_SUB; i++) tp_buf[c][fx[c].tw++ & (TP_LEN - 1)] = (int16_t)sat16(x[i] / 2);   /* (kept fresh) */
        if (half) for (i = 0; i < DMK_SUB; i++) x[i] *= 2;
    }
}
/* a side out through the limiter, with the colour and media's own core, when the mix's width is as it is (else the
 * first core does both, the width first) */
static int16_t *gm_lr;                                  /* the run's out, its two sides interleaved (0: not here) */
static uint32_t gm_loud[2];                             /* each side's samples past full */
static void gm_out(int c)
{
    int i;
    for (i = 0; i < gm_nb * DMK_SUB; i++) {
        int32_t v = gm_x[c][i];
        gm_loud[c] += v > 32767 || v < -32767;
        gm_lr[i * 2 + c] = (int16_t)curve(T_LIM, v);
    }
}
static void gm_job(void) { gm_side(1); if (gm_lr) gm_out(1); }

/* the screen reads these: what each synth sounds, now */
static int32_t syn_peak[DMK_SYNTHS];
static int32_t syn_dc[DMK_SYNTHS];                      /* each synth's DC blocker (a sample's loop can sit off zero) */

/* the lines and the state, silent */
DMK_SMALL static void dsp_reset(void)
{
    uint32_t i;
    for (i = 0; i < 2 * TP_LEN; i++) tp_buf[i / TP_LEN][i % TP_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) {                  /* the delays: off (as set, they come in from silence), their
                                                           lines, their grains' dice (renders repeat) */
        dls[i].cur = DT_OFF; dls[i].g = 0;
        dl_clear((int)i);
        dls[i].wph[0] = dls[i].wph[1] = 0;
        grs[i].rng = 0x2545F491u * (i + 1u);
    }
    for (i = 0; i < 2; i++) {                           /* the mix's colour and media: none running */
        uint8_t *z = (uint8_t *)&mrow[i];
        uint32_t k;
        for (k = 0; k < sizeof mrow[i]; k++) z[k] = 0;
        mrow[i].seed = 0x6C8E9CF5u;                     /* (the same dice every start: renders repeat) */
    }
    for (i = 0; i < DMK_SYNTHS * CH_LEN; i++) ch_buf[i / CH_LEN][i % CH_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) {
        uint8_t *z = (uint8_t *)&fx[i];
        uint32_t k;
        for (k = 0; k < sizeof fx[i]; k++) z[k] = 0;
        fx[i].rng = 0x9E3779B9u * (i + 1u);
        fx[i].mrng = 0x6C8E9CF5u;                       /* (the same for the mix's two sides: one tape, its wow both sides) */
        fx[i].lg[0] = fx[i].lg[1] = fx[i].lg[2] = 32768;
        syn_peak[i] = 0; syn_dc[i] = 0;
        rvs[i].cur = syn[i].rtype < RV_N ? syn[i].rtype : RV_OFF; rvs[i].g = 32768; rvs[i].gin = 0;
        rv_clear((int)i);
    }
    for (i = 0; i < DMK_SYNTHS; i++) {
        vc_tail[i] = 0; syn_last[i] = 0;
        lfo[i].ph = lfo[i].poff = 0; lfo[i].sm = lfo[i].gs = lfo[i].hold = lfo[i].fade = lfo[i].cut = lfo[i].pit = 0; lfo[i].rst = 0;
        lfo[i].rng = 0x2545F491u * (i + 3u); lfo[i].lv = 32768; lfo[i].pl = lfo[i].pr = -1;
    }
    sps.cur = SP_OFF; sps.g = 0;                        /* the Space off: as set, it comes in from silence */
    sp_clear(); sp_rest(); sp_live = 0;
    rv_prev[0] = rv_prev[1] = 0;
}

/* ---- the render: the voices first, a whole call at once, half of them on the second core when there is one; then
 * sample by sample the synths' effects, level, pan, sends, the buses and the limiter */
static int16_t vib_c[DMK_SYNTHS][VB_MAX / DMK_SUB];     /* each synth's vibrato a block, cents Q4: both cores read it */
static int v_frames;

/* the formants for VOWEL, as cutoff indexes (T_SVF): u, o, a, e, i; the cutoff glides through them */
static const uint8_t VOW[5][2] = {{112, 164}, {132, 160}, {156, 175}, {140, 201}, {107, 211}};

/* a step's rate moved by c cents Q4: within half a semitone by the cents table alone, past it (the LFO, the glide)
 * by the semitones' too; at most INC_MAX */
static uint32_t inc_by(uint32_t inc0, int32_t c)
{
    int32_t ct = c / 16, st, r;
    uint32_t inc;
    if (ct >= -50 && ct <= 50) inc = (uint32_t)(((uint64_t)inc0 * T_CENT[50 + ct]) >> 16);
    else {
        st = clampi((ct + 4850) / 100 - 48, -48, 48);   /* the nearest semitone (ct >= -4850 after the clamp) */
        r = clampi(ct - st * 100, -50, 50);
        inc = (uint32_t)(((((uint64_t)inc0 * T_SEMI[st + 48]) >> 16) * T_CENT[50 + r]) >> 16);
    }
    return inc > INC_MAX ? INC_MAX : inc;
}

/* a voice's filter, one sample: its type's response to x */
static inline __attribute__((always_inline)) int32_t vfilt(voice_t *o, int ftype, int32_t x, int32_t f, int32_t f2, int32_t q, int32_t q1)
{
    int32_t y, hp;
    switch (ftype) {
    case FT_LP24: y = svf_step(&o->lp2, &o->bp2, svf_step(&o->lp, &o->bp, x, f, q1, &hp), f, q, &hp); break;
    case FT_LP12: y = svf_step(&o->lp, &o->bp, x, f, q, &hp); break;
    case FT_HP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = hp; break;
    case FT_BP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = o->bp; break;
    case FT_NOTCH: y = svf_step(&o->lp, &o->bp, x, f, q, &hp); y += hp; break;
    case FT_PEAK: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = x + o->bp; break;
    case FT_VOWEL: {
        int32_t h2;
        int32_t qv = 1500 + q / 4;                      /* the resonance narrows the formants */
        svf_step(&o->lp, &o->bp, x, f, qv, &hp);
        svf_step(&o->lp2, &o->bp2, x, f2, qv, &h2);
        y = (o->bp + (o->bp2 * 3) / 4) * 3 / 2;
        break;
    }
    default: y = x; break;
    }
    return y;
}

static void voices_run(int half)
{
    int v, i, j, s, b, nb = v_frames / DMK_SUB;
    for (s = 0; s < DMK_SYNTHS; s++) for (i = 0; i < v_frames; i++) vbuf[half][s][i] = 0;
    for (v = half; v < DMK_VOICES; v += 2) {            /* (every other voice: a chord taken in order, low to high, was
                                                           all its high notes, the costly ones, on one core) */
        voice_t *o = &vc[v];
        const synth_t *p = &syn[o->sy];
        /* GRIT: the voice runs every gs-th sample, held for the ones between (the console's rate); its filter's
         * coefficients an octave up for each halving */
        int gs = 1 << p->grit, gi = p->grit * 34;
        for (b = 0; b < nb && o->stage; b++) {
            int32_t e0 = o->env, e1, de, f, q, q1, open, idx, f2 = 0, c = 0, *out = &vbuf[half][o->sy][b * DMK_SUB];
            uint32_t inc, linc = 0;
            if (o->stage == 1) { e1 = e0 + (int32_t)p->atk; if (e1 >= FULL) { e1 = FULL; o->stage = 2; } }
            else if (o->stage == 2) e1 = p->sus + (int32_t)(((int64_t)(e0 - p->sus) * p->dec) >> 16);
            else { e1 = (int32_t)(((int64_t)e0 * p->rel) >> 16); if (e1 < 256) { e1 = 0; o->stage = 0; } }
            de = (e1 - e0) / DMK_SUB;
            /* the pitch: MOTION's vibrato (faded in) or drift (a wandering goal, and the note's own detune) */
            if (p->motion == FO_VIB) {
                int fade = 1 + p->ok[3] * 4;
                c = vib_c[o->sy][b] * (o->blocks < fade ? o->blocks : fade) / fade;
            } else if (p->motion == FO_DRIFT) {
                int fast = p->ok[2] > 64;                /* a new goal every 0.37 s (or 0.09 s), glided to */
                if (!(o->blocks & (fast ? 255 : 1023))) { o->rng = o->rng * 1664525u + 1013904223u; o->dgoal = (int16_t)((int32_t)(o->rng >> 23) - 256); }
                o->drift += (int16_t)((o->dgoal - o->drift) / (fast ? 64 : 256));
                c = o->drift * p->ok[0] / 127 + o->det;
            }
            if (o->gl) {                                /* the glide: the way still to go, shrinking */
                c += o->gl;
                {                                       /* toward zero (a negative one floored would stall) */
                    int32_t a = o->gl < 0 ? -o->gl : o->gl;
                    a = (int32_t)(((int64_t)a * p->glide) >> 16);
                    o->gl = a < 16 ? 0 : o->gl < 0 ? -a : a;
                }
            }
            c += lfo_pit[o->sy][b];                     /* the LFO's pitch, when it is its target */
            if (o->blocks < 65535u) o->blocks++;
            o->m.inc = c ? inc_by(o->m.inc0, c) : o->m.inc0;
            if (o->l.on) o->l.inc = c ? inc_by(o->l.inc0, c) : o->l.inc0;
            inc = o->m.inc << p->grit;
            if (o->l.on) linc = o->l.inc << p->grit;
            idx = p->cut + (int32_t)(((int64_t)p->fenv * e0) >> 24) + o->kcut;
            idx += lfo_cut[o->sy][b];                   /* and its cutoff */
            idx = clampi(idx, 0, 255);
            f = T_SVF[gi ? clampi(idx + gi, 0, 255) : idx]; q = p->resq;
            q1 = q < 11469 ? 11469 : q;                 /* LP24: one stage resonates; two would square the peak */
            open = (p->ftype == FT_LP24 || p->ftype == FT_LP12) && idx >= 240 ? idx - 239 : 0;   /* 1..16: to the sample */
            if (p->ftype == FT_VOWEL) {                 /* the cutoff picks the vowel: two band-passes at its formants */
                int pos = idx * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
                f = T_SVF[clampi(((VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8) + gi, 0, 255)];
                f2 = T_SVF[clampi(((VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8) + gi, 0, 255)];
            }
            if (gs == 1 && !o->lay && p->drive <= 256 && o->vg >= 32768) {   /* the plain voice: as it was, no cost */
                for (i = 0; i < DMK_SUB; i++) {
                    int32_t x, y;
                    o->m.ph += inc;
                    while (o->m.ph >= 65536u) {
                        o->m.ph -= 65536u; o->m.a = o->m.b; o->m.b = smp_next_m(o);   /* a one-shot's end ends it */
                    }
                    x = o->m.a + (((o->m.b - o->m.a) * (int32_t)(o->m.ph >> 1)) >> 15);
                    y = vfilt(o, p->ftype, x, f, f2, q, q1);
                    if (open) y += ((x - y) * open) >> 4;   /* the top: the sample as it is, reached gradually (a switch clicks) */
                    METER(dmeter.clamp_v += y > 65536 || y < -65536);
                    METER(dmeter.voice_max = (y < 0 ? -y : y) > dmeter.voice_max ? (y < 0 ? -y : y) : dmeter.voice_max);
                    if (y > 49152 || y < -49152) y = curve(T_LIM, y / 2) * 2;   /* a resonance past full: bent, not cut */
                    o->lastv = (int32_t)(((int64_t)y * ((e0 + de * i) >> 9)) >> 15);
                    out[i] += o->lastv;
                    if (!o->stage) { o->lastv = 0; break; }
                }
            } else for (i = 0; i < DMK_SUB; i += gs) {  /* with its layer, drive, velocity or grit */
                int32_t x, y;
                o->m.ph += inc;
                while (o->m.ph >= 65536u) {
                    o->m.ph -= 65536u; o->m.a = o->m.b; o->m.b = smp_next_m(o);
                }
                x = o->m.a + (((o->m.b - o->m.a) * (int32_t)(o->m.ph >> 1)) >> 15);
                if (o->lay) {                           /* the LAYER: its own recording, its tone, blended in; ended, silence
                                                           glided in by its tone (a cut would click) */
                    int32_t xl = 0;
                    if (o->l.on) {
                        o->l.ph += linc;
                        while (o->l.ph >= 65536u) { o->l.ph -= 65536u; o->l.a = o->l.b; o->l.b = smp_next(&o->l); }
                        xl = o->l.a + (((o->l.b - o->l.a) * (int32_t)(o->l.ph >> 1)) >> 15);
                    }
                    o->ltp += ((xl - o->ltp) * p->ltone) >> 15;
                    x += ((o->ltp - x) * p->lblend) >> 15;
                    if (!o->m.on && !o->l.on && o->ltp > -64 && o->ltp < 64) { o->stage = 0; o->env = 0; }   /* both through */
                }
                if (p->drive > 256) x = (curve(T_SAT, (x * p->drive) >> 8) * p->dcomp) >> 8;   /* DRIVE: into the filter */
                y = vfilt(o, p->ftype, x, f, f2, q, q1);
                if (p->drive > 256) { o->bp = curve(T_LIM, o->bp); o->bp2 = curve(T_LIM, o->bp2); }   /* the resonance sings, held */
                if (open) y += ((x - y) * open) >> 4;
                if (y > 49152 || y < -49152) y = curve(T_LIM, y / 2) * 2;
                for (j = 0; j < gs; j++) {              /* (held, under GRIT) */
                    o->lastv = (int32_t)(((int64_t)y * ((e0 + de * (i + j)) >> 9)) >> 15);
                    if (o->vg < 32768) o->lastv = (o->lastv * (o->vg >> 1)) >> 14;
                    out[i + j] += o->lastv;
                }
                if (!o->stage) { o->lastv = 0; break; }
            }
            if (o->stage) o->env = e1;
        }
    }
}
static void voices_job(void) { voices_run(1); }

/* a synth's LFO over the run's blocks: its shape from the phase (a random one held a cycle), smoothed over ~1.5 ms
 * (a square's edge or a new random value would click), times its depth, faded in from the phrase's first note */
static void lfo_run(int s, int nb)
{
    const synth_t *p = &syn[s];
    int b;
    if (!p->ldepth && !lfo[s].gs && !lfo[s].cut && !lfo[s].pit) {   /* off, and all of it glided away: nothing */
        for (b = 0; b < nb; b++) { lfo_c[s][b] = lfo_g[s][b] = lfo_cut[s][b] = 0; lfo_pit[s][b] = 0; }
        lfo[s].sm = 0;
        return;
    }
    for (b = 0; b < nb; b++) {
        int32_t g = 0, c, tc, tp;
        if (p->ldepth) {
            uint32_t was = lfo[s].ph, t;
            int32_t v;
            lfo[s].ph += p->lsync ? lfo[s].sstep : p->lrate;
            if (lfo[s].ph < was) lfo[s].hold = (int32_t)(xs32(&lfo[s].rng) >> 16) - 32768;   /* a new cycle */
            t = lfo[s].ph >> 16;
            switch (p->lshape) {
            case 0: v = sinp(lfo[s].ph); break;
            case 1: v = t < 32768u ? (int32_t)t * 2 - 32767 : 98303 - (int32_t)t * 2; break;   /* triangle */
            case 2: v = 32767 - (int32_t)t; break;      /* ramp, falling */
            case 3: v = t < 32768u ? 32767 : -32767; break;   /* square */
            default: v = lfo[s].hold; break;            /* random, held a cycle */
            }
            lfo[s].sm += (v - lfo[s].sm) >> 2;
            if (lfo[s].fade < 65535) lfo[s].fade++;
            g = p->ldepth;
            if (p->lfade && lfo[s].fade < p->lfade) g = g * lfo[s].fade / p->lfade;
        } else lfo[s].sm = 0;
        lfo[s].gs += (g - lfo[s].gs) >> 4;              /* the depth glided (~6 ms): a fade begun again, DEPTH turned */
        g = lfo[s].gs;
        c = clampi((lfo[s].sm * g) >> 15, -32767, 32767);
        lfo_c[s][b] = (int16_t)c;
        lfo_g[s][b] = (int16_t)g;
        /* the cutoff: to 3.8 octaves either way (the owner: it should be felt); the pitch: an octave, finer low (its
         * depth squared); each slewed toward it, so leaving the target glides back */
        tc = p->ltarget == LT_CUTOFF ? (c * 128) >> 15 : 0;
        tp = p->ltarget == LT_PITCH ? (((c * (g >> 1)) >> 14) * 19200) >> 15 : 0;
        lfo[s].cut += clampi(tc - lfo[s].cut, -8, 8);
        lfo[s].pit += clampi(tp - lfo[s].pit, -1200, 1200);
        lfo_cut[s][b] = (int16_t)lfo[s].cut;
        lfo_pit[s][b] = lfo[s].pit;
    }
}

/* a synth's chain over the run, in place: its voices' halves summed with a stolen voice's tail, its DC out, the
 * limiter's bend, then its rows; vbuf[0] holds its left after, vbuf[1] its right. Each synth's state is its own, so
 * the chains can run on either core */
static int ch_nb;                                       /* blocks this run */
static void chain_run(int s)
{
    int b, i;
    for (b = 0; b < ch_nb; b++) {
        int32_t *pl = &vbuf[0][s][b * DMK_SUB], *pr = &vbuf[1][s][b * DMK_SUB], x[DMK_SUB];
        for (i = 0; i < DMK_SUB; i++) {                 /* half the level through the effects, room for a chord; no DC */
            int32_t v = (pl[i] + pr[i] + vc_tail[s]) >> 2;
            vc_tail[s] -= (vc_tail[s] >> 6) + (vc_tail[s] > 0) - (vc_tail[s] < 0);   /* to zero, ~1.5 ms */
            syn_dc[s] += (v * 4096 - syn_dc[s]) / 512;  /* ~14 Hz, Q12: its stall under a 512th is under */
            x[i] = curve(T_LIM, v - ((syn_dc[s] + 2048) >> 12));   /* a chord past full: bent, never cut */
        }
#ifdef DMK_METER
        for (i = 0; i < DMK_SUB; i++) { int32_t a = x[i] < 0 ? -x[i] : x[i]; if (a > dmeter.syn_max) dmeter.syn_max = a; }
#endif
        rows_run(s, x, pl, pr);
        syn_delay(s, pl, pr);                           /* its DELAY, into its sides and its sends */
        if (syn[s].width != 256)                        /* WIDTH: its sides, narrowed or widened */
            for (i = 0; i < DMK_SUB; i++) {
                int32_t m = (pl[i] + pr[i]) / 2, d = ((pl[i] - pr[i]) * syn[s].width) >> 9;
                pl[i] = m + d; pr[i] = m - d;
            }
    }
}
/* the second core's share of the mix, in two parts: the last synth's chain and its reverb, then a third of the
 * Space's blocks queued a run ago, while this core runs the other two's chains; then the rest of the Space and the
 * first synth's reverb while this core mixes and runs the second's (sp_job). The reverbs' sum in rv_out, which the
 * first part starts */
static void chain_job(void)
{
    int i;
    chain_run(DMK_SYNTHS - 1);
    for (i = 0; i < rv_nb * RV_ST; i++) rv_out[0][i] = rv_out[1][i] = 0;
    if (rv_busy(DMK_SYNTHS - 1)) rv_run(DMK_SYNTHS - 1, rv_out[0], rv_out[1]);
    if (sp_live) sp_work((spq_i1 - spq_i0 + 2u) / 3u);
}

static void dsp_render(int16_t *lr, int frames)
{
    int b, i, s, n, at;
    for (at = 0; at < frames; at += n) {
        int nb, par, rv_on = 0, sp_on, r0;
        int32_t mg[VB_MAX / DMK_SUB];
        n = frames - at > VB_MAX ? VB_MAX : frames - at;
        v_frames = n;
        nb = n / DMK_SUB;
        for (s = 0; s < DMK_SYNTHS; s++) lfo_run(s, nb);   /* the LFOs: both cores read them */
        for (s = 0; s < DMK_SYNTHS; s++)                /* the vibrato's LFO, a block at a time, before the split */
            for (b = 0; b < nb; b++) {
                fx[s].vib += 3100000u + (uint32_t)syn[s].ok[2] * 74000u;   /* 2..8 Hz, a block at a time */
                vib_c[s][b] = (int16_t)(syn[s].motion == FO_VIB ? sinp(fx[s].vib) * syn[s].ok[0] * 5 / 32768 : 0);   /* to ~40 cents */
            }
        par = dmk_par_begin && dmk_par_begin(voices_job);
        voices_run(0);
        if (par) dmk_par_end(); else voices_run(1);
        /* the song's fades, a step a block; and each synth's way into its reverb a block (its level, the fade, the LFO's
         * tremolo, its AMOUNT): both before the jobs that run the reverbs */
        for (b = 0; b < nb; b++) {
            if (mix_dg) {
                mix_g += mix_dg;
                if (mix_g <= 0) { mix_g = 0; mix_dg = mix_next; mix_next = 0; }
                if (mix_g >= 32768) { mix_g = 32768; mix_dg = mix_next; mix_next = 0; }
            }
            mg[b] = mix_g;
        }
        for (s = 0; s < DMK_SYNTHS; s++) {
            const synth_t *p = &syn[s];
            rv_on |= rv_busy(s);
            for (b = 0; b < nb; b++) {
                int32_t lvl = mg[b] < 32768 ? (p->level * mg[b]) >> 15 : p->level, lv = 32768;
                if (p->ltarget == LT_LEVEL) lv = 32768 - ((lfo_g[s][b] - lfo_c[s][b]) >> 1);
                lv = (lvl * lv) >> 15;
                rv_gb[s][b] = (lv * p->ramt) >> 15;
                sp_gb[s][b] = (lv * p->ssend) >> 15;     /* (and to the Space) */
            }
        }
        rv_nb = nb;
        sp_on = sp_busy();                              /* the Space's queues: from rest as it comes on */
        if (sp_on && !sp_live) sp_rest();
        sp_live = (uint8_t)sp_on;
        /* the chains (and the last synth's reverb, a third of the Space) there, the rest here */
        ch_nb = nb;
        par = dmk_par_begin && dmk_par_begin(chain_job);
        for (s = 0; s < DMK_SYNTHS - 1; s++) chain_run(s);
        if (par) dmk_par_end(); else chain_job();
        /* the SPACE's rest and the first synth's reverb there (the owner: the second core's job); the mix and the second
         * synth's reverb here */
        sp_nb = nb; r0 = rv_busy(0);
        par = (sp_on || r0) && dmk_par_begin && dmk_par_begin(sp_job);
        for (b = 0; b < nb; b++) {
            int32_t L[DMK_SUB], R[DMK_SUB];
            for (i = 0; i < DMK_SUB; i++) L[i] = R[i] = 0;
            for (s = 0; s < DMK_SYNTHS; s++) {
                const synth_t *p = &syn[s];
                const int32_t *sl = &vbuf[0][s][b * DMK_SUB], *sr = &vbuf[1][s][b * DMK_SUB];
                int32_t pk = 0, lvl = mg[b] < 32768 ? (p->level * mg[b]) >> 15 : p->level;
                int32_t gl1 = p->gl, gr1 = p->gr, lv1 = 32768, gl0, gr0, lv0, mod;
                if (lfo[s].pl < 0) { lfo[s].pl = p->gl; lfo[s].pr = p->gr; }   /* the first block */
                if (p->ltarget == LT_LEVEL) lv1 = 32768 - ((lfo_g[s][b] - lfo_c[s][b]) >> 1);   /* the tremolo: down by its depth */
                if (p->ltarget == LT_PAN) {             /* or the pan, swung either way around PAN */
                    int pn = clampi(p->pan + ((lfo_c[s][b] * 64) >> 15), 0, 127);
                    gl1 = T_PAN[127 - pn]; gr1 = T_PAN[pn];
                }
                lv0 = lfo[s].lv; gl0 = lfo[s].pl; gr0 = lfo[s].pr;   /* from where the last block left them: glided */
                mod = lv0 != 32768 || lv1 != 32768 || gl0 != gl1 || gr0 != gr1;
                lfo[s].lv = lv1; lfo[s].pl = gl1; lfo[s].pr = gr1;
                /* each side held under twice full scale before its gain (to 32768), and after it (the limiter bends it
                 * anyway), so the products stay in 32 bits: a chain past full at full level wrapped into a click */
                if (mod) for (i = 0; i < DMK_SUB; i++) {   /* the level and the pan, glided across the block */
                    int32_t xl = clampi((clampi(sl[i], -65535, 65535) * lvl) >> 14, -65535, 65535);
                    int32_t xr = clampi((clampi(sr[i], -65535, 65535) * lvl) >> 14, -65535, 65535), x;
                    int32_t lv = lv0 + (lv1 - lv0) * i / DMK_SUB;
                    xl = (xl * lv) >> 15; xr = (xr * lv) >> 15;
                    x = (xl + xr) / 2;
                    L[i] += (xl * (gl0 + (gl1 - gl0) * i / DMK_SUB)) >> 15;
                    R[i] += (xr * (gr0 + (gr1 - gr0) * i / DMK_SUB)) >> 15;
                    if (x > pk) pk = x; else if (-x > pk) pk = -x;
                } else for (i = 0; i < DMK_SUB; i++) {
                    int32_t xl = clampi((clampi(sl[i], -65535, 65535) * lvl) >> 14, -65535, 65535);
                    int32_t xr = clampi((clampi(sr[i], -65535, 65535) * lvl) >> 14, -65535, 65535), x = (xl + xr) / 2;   /* back up */
                    L[i] += (xl * p->gl) >> 15;
                    R[i] += (xr * p->gr) >> 15;
                    if (x > pk) pk = x; else if (-x > pk) pk = -x;
                }
                syn_peak[s] = pk > syn_peak[s] ? pk : syn_peak[s] - (syn_peak[s] >> 6);
            }
            for (i = 0; i < DMK_SUB; i++) { gm_x[0][b * DMK_SUB + i] = L[i]; gm_x[1][b * DMK_SUB + i] = R[i]; }
#ifdef DMK_BENCH
            bench_block(DMK_SUB);
#endif
        }
        if (rv_busy(1)) rv_run(1, rv_out[0], rv_out[1]);
        if (par) dmk_par_end(); else if (sp_on || r0) sp_job();
        if (r0) for (i = 0; i < n / 2; i++) { rv_out[0][i] += rv_out1[0][i]; rv_out[1][i] += rv_out1[1][i]; }
        if (sp_on)                                      /* the Space's blocks out, a run's reserve behind */
            for (b = 0; b < nb && spq_o0 != spq_o1; b++, spq_o0++) {
                const int32_t (*q)[RV_ST] = spq_out[spq_o0 & (SP_Q - 1u)];
                for (i = 0; i < RV_ST; i++) { rv_out[0][b * RV_ST + i] += q[0][i]; rv_out[1][b * RV_ST + i] += q[1][i]; }
            }
        if (rv_on || sp_on)                             /* the reverbs and the Space, to the full rate: between two steps,
                                                           half of each */
            for (i = 0; i < n / 2; i++) {
                int32_t a = rv_out[0][i], c = rv_out[1][i];
                gm_x[0][2 * i] += (rv_prev[0] + a) >> 1; gm_x[0][2 * i + 1] += a;
                gm_x[1][2 * i] += (rv_prev[1] + c) >> 1; gm_x[1][2 * i + 1] += c;
                rv_prev[0] = a; rv_prev[1] = c;
            }
        else rv_prev[0] = rv_prev[1] = 0;
#ifdef DMK_METER
        for (i = 0; i < n; i++) { int32_t a = gm_x[0][i], c = gm_x[1][i]; a = a < 0 ? -a : a; c = c < 0 ? -c : c; if (c > a) a = c;
                                  if (a > dmeter.pre_max) dmeter.pre_max = a; dmeter.knee += a > 24575; }
#endif
        gm_nb = nb;                                     /* the mix's COLOUR and MEDIA: a side on each core */
        mrow_plan(&mrow[0], gcol.colour, nb);
        mrow_plan(&mrow[1], gmed.media, nb);
        gm_lr = gp.width == 256 ? lr + at * 2 : 0;
        par = (mrow_busy(&mrow[0]) || mrow_busy(&mrow[1]) || gm_lr) && dmk_par_begin && dmk_par_begin(gm_job);
        gm_side(0);
        if (gm_lr) gm_out(0);
        if (par) dmk_par_end(); else gm_job();
        if (gm_lr) { sys.loud += gm_loud[0] + gm_loud[1]; gm_loud[0] = gm_loud[1] = 0; }   /* (past full: bent) */
        else for (i = 0; i < n; i++) {
            int32_t ol = gm_x[0][i], orr = gm_x[1][i];
            if (gp.width != 256) {                      /* the whole mix's width (Home's Pan row) */
                int32_t m = (ol + orr) / 2, d = (int32_t)(((int64_t)(ol - orr) * gp.width) >> 9);
                ol = m + d; orr = m - d;
            }
            if (ol > 32767 || ol < -32767 || orr > 32767 || orr < -32767) sys.loud++;   /* past full: bent */
            lr[(at + i) * 2] = (int16_t)curve(T_LIM, ol);
            lr[(at + i) * 2 + 1] = (int16_t)curve(T_LIM, orr);
        }
    }
}

/* the keys play the current synth (dmk_ui.c sets it) */
static int cur_synth;
static void note_on(int n) { note_on_s(cur_synth, n); }
static void note_off(int n) { note_off_s(cur_synth, n); }
