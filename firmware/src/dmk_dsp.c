/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's sound (docs/plan.md, 2.1, 1.2, 1.3): the owner's recordings are the core. The sampler reads them as 4-bit
 * block ADPCM straight from flash, pitched by a Q16 phase with linear interpolation, looped where tools/gen_samples.py
 * found the seam; each voice has a state-variable filter of eight types and an envelope. Each synth has three effects
 * in a chain, one a row: COLOUR (saturate, crush, lossy, noise), MEDIA (tape, cassette, VHS, on one line read back by
 * wow and flutter), MOTION (chorus; vibrato and drift on the voices' pitch). Two shared buses at half rate: the VERB
 * (ROOM, MONO2K's SPU-flavoured combs and all-passes; or an eight-line feedback delay network, swept, for HALL, HAZE,
 * BLOOM and LOFI) and the ECHO (an 8-bit mu-law line, its loop filtered and clipped). Then a soft limiter. 12 voices
 * shared by the three synths, half of them on the second core when there is one. Integer only. Included by
 * dmk_core.c. */

#define FULL (1 << 24)

#ifdef DMK_METER                                        /* the host's meters: where a sound runs out of room */
static struct { int32_t pre_max, syn_max, voice_max; uint32_t knee, sat_line, sat_fd, sat_echo, clamp_v; } dmeter;
#define METER(x) (x)
#else
#define METER(x) ((void)0)
#endif
static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

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
enum { FM_OFF, FM_TAPE, FM_CASS, FM_VHS, FM_VINYL, FM_RADIO, FM_DICTA };                  /* MEDIA */
enum { FO_OFF, FO_CHORUS, FO_VIB, FO_DRIFT, FO_ENS, FO_PHASER, FO_FLANGER, FO_TREM };      /* MOTION */
enum { VB_ROOM, VB_HALL, VB_SPACE, VB_HAZE, VB_BLOOM, VB_LOFI, VB_OFF };   /* the VERB's types (off: nothing run) */
enum { ET_TAPE, ET_GRAINS, ET_OFF };                                     /* the ECHO's (its delays: each synth's since 0.5) */
typedef struct {
    uint8_t smp, loops, ftype, tape;
    int16_t tune, cut, fenv;       /* semitones; the cutoff's index 0..255 (255: open); the envelope's reach in indexes */
    int8_t fine;                   /* cents, -50..50 */
    uint16_t start, resq, dec, rel;   /* blocks; damping Q14; decay and release factors Q16 a block */
    uint32_t atk;                  /* the attack's step a block, Q24 */
    int32_t sus, level, gl, gr, vsend, esend;   /* Q24; Q15 ... */
    int32_t pan;                   /* 0..127 as turned (the LFO swings it) */
    int32_t width;                 /* its stereo's width, Q8 (256 as it is, 0 one, 508 twice) */
    int32_t tdepth, tdrive;        /* the tape's wow depth (Q8 samples a unit of the sine), drive Q8 */
    uint16_t thl;                  /* its head loss, a one-pole Q15 */
    uint32_t twow, tflut;          /* its wow's and flutter's phase steps */
    uint8_t colour, media, motion; /* the rows' effects (FC_, FM_, FO_), 0 off (media: the whole mix's, gmed's) */
    uint8_t dtype, ddiv;           /* the DELAY (FX row 2, the owner's: one a synth): 0 off, 1 delay, 2 ping-pong; its time */
    int32_t dmix, dtone, dfb;      /*   its wet share Q15, its tone (a one-pole), its regen Q15 */
    uint32_t dtime;                /*   (dmk_seq: its time, quarter-rate samples, of the tempo)  */
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

/* the shared buses' controls */
static struct { int32_t vret, vfb, vdamp, eret, efb, elp, etime, vfbq, vmod, glen, gdens, gtex, width; uint32_t vrate; uint8_t vtype, etype; } gp;
/* the VERB's and ECHO's types as they run: a new one is taken once the return has faded out (5.8 ms), then faded in */
static uint8_t v_cur, e_cur;
static int32_t v_g = 32768, e_g = 32768;

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

static uint32_t vc_clock;
/* who started a voice: the keys, a roll, the chord machine; a note off ends only its own source's (the progression's
 * chord change never cuts a note the hands hold, nor the reverse) */
enum { SRC_KEY, SRC_ROLL, SRC_CHORD };
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

/* a stream at the start of recording s, for note n moved by tune semitones and fine cents, from block start */
static void strm_start(strm_t *t, const dmk_sample_t *s, int n, int tune, int fine, int loops, int start)
{
    int k = clampi(n - s->root + tune, -48, 48);
    uint32_t inc = (uint32_t)(((uint64_t)s->rate_q16 * T_SEMI[k + 48]) >> 16);
    t->inc = t->inc0 = (uint32_t)(((uint64_t)inc * T_CENT[clampi(50 - s->cents + fine, 0, 100)]) >> 16);
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
    if (midi_out & 1u) mq_put(0x90 | sy, n, vel);
}
#define VEL_KEY 100                                     /* the keys' velocity, as the roll writes it */
static void note_on_src(int sy, int n, int src) { note_on_vel(sy, n, src, VEL_KEY); }

/* a note off: one voice, the oldest of that note from that source (a second key, an octave apart, or a chord sharing
 * a note, keeps its own: letting one go cut the other) */
static void note_off_src(int sy, int n, int src)
{
    int i, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    if (midi_out & 1u && n >= 0 && n < 128) mq_put(0x80 | sy, n, 0);
    for (i = 0; i < DMK_VOICES; i++)
        if (vc[i].on && vc[i].note == n && vc[i].sy == sy && vc[i].src == src && vc[i].age < oldest) { oldest = vc[i].age; best = i; }
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

/* COLOUR: SATURATE (drive into the tape's curve, a bias for even harmonics, a tone after), CRUSH (bits, a rate held
 * by a jittered clock, a low-pass before), LOSSY (three bands, each quantized by its last frame's peak, the weak and
 * random ones dropped: a codec's smear and holes), NOISE (hiss, crackle and hum riding the sound) */
static void colour_block(int sy, int32_t *x, int type)
{
    const synth_t *p = &syn[sy];
    int i, k0 = p->ck[0], k1 = p->ck[1], k2 = p->ck[2], k3 = p->ck[3];
    int32_t tone = T_OP[64 + k1 / 2];
    if (type == FC_SAT) {
        int32_t drive = 256 + k0 * 14, bias = k2 * 60, base = curve(T_SAT, (bias * drive) >> 8), mix = k3 * 258;
        for (i = 0; i < DMK_SUB; i++) {
            int32_t y = curve(T_SAT, ((x[i] + bias) * drive) >> 8) - base;
            y = y * 256 / (160 + drive / 3);            /* about the level it went in at */
            fx[sy].sdc += (y * 256 - fx[sy].sdc) >> 10; /* the bias's DC, out (~7 Hz, Q8) */
            y -= fx[sy].sdc >> 8;
            fx[sy].clp += ((y - fx[sy].clp) * tone) >> 15;
            x[i] += ((fx[sy].clp - x[i]) * mix) >> 15;
        }
    } else if (type == FC_CRUSH) {
        int sh = k0 * 13 / 127, k = 127 - k2;
        uint32_t step = 600u + (uint32_t)(k * k) * 4u;
        int32_t post = step * 5u / 4u > 32767u ? 32767 : (int32_t)(step * 5u / 4u);   /* ~0.4 of the held rate */
        for (i = 0; i < DMK_SUB; i++) {
            fx[sy].clp += ((x[i] - fx[sy].clp) * tone) >> 15;
            fx[sy].hold_ph += step + (k3 ? (xs32(&fx[sy].rng) >> 20) * (uint32_t)k3 / 8u : 0u);
            if (fx[sy].hold_ph >= 65536u) {
                fx[sy].hold_ph &= 65535u;
                fx[sy].hold = sh ? ((fx[sy].clp + (1 << (sh - 1))) >> sh) << sh : fx[sy].clp;
            }
            fx[sy].sl += ((fx[sy].hold - fx[sy].sl) * post) >> 15;   /* the stairs' edges: no clicks of their own */
            x[i] = fx[sy].sl;
        }
    } else if (type == FC_LOSSY) {
        int frame = 64 << (k2 * 3 / 128), bits[3];
        bits[0] = 9 - k0 * 5 / 127; bits[1] = 8 - k0 * 5 / 127; bits[2] = 7 - k0 * 5 / 127;   /* the top never a sign alone */
        for (i = 0; i < DMK_SUB; i++) {
            int32_t b[3], y = 0;
            int j;
            fx[sy].band[0] += ((x[i] - fx[sy].band[0]) * 2300) >> 15;    /* ~500 Hz */
            fx[sy].band[1] += ((x[i] - fx[sy].band[1]) * 13000) >> 15;   /* ~3 kHz */
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
            fx[sy].clp += ((y - fx[sy].clp) * tone) >> 15;
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
            fx[sy].wlo += ((x[i] - fx[sy].wlo) * 750) >> 15;          /* ~170 Hz: the body */
            v = x[i] + ((fx[sy].wlo * body) >> 8);
            fx[sy].wpre += ((v - fx[sy].wpre) * 14000) >> 15;         /* ~4 kHz: below it bends, above it is clean */
            u = clampi((fx[sy].wpre * drive) >> 8, -65535, 65535);
            q = ((u >> 1) * (u >> 1)) >> 15;                          /* u^2 / 2^17 */
            y = curve(T_SAT, u - ((q * even) >> 13));                 /* up to 0.25 u^2: the bottom bent harder */
            fx[sy].wdc += (y * 256 - fx[sy].wdc) >> 10;               /* its DC out (~7 Hz, Q8) */
            y -= fx[sy].wdc >> 8;
            y = ((((y * comp) >> 8) + v - fx[sy].wpre) * lin) >> 8;
            fx[sy].wtl += ((y - fx[sy].wtl) * 3500) >> 15;            /* ~800 Hz: the tilt's hinge */
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
            fx[sy].dpre += ((x[i] - fx[sy].dpre) * 18000) >> 15;      /* ~5 kHz in: fewer of the fold's aliases */
            u = (int32_t)(((int64_t)fx[sy].dpre * drive) >> 8);
            c = curve(T_LIM, u);
            y = c + (((sinp((uint32_t)u << 15) - c) * fold) >> 15);
            sin_ += (x[i] < 0 ? -x[i] : x[i]) >> 4; sout += (y < 0 ? -y : y) >> 4;
            y = (y * (g0 + gst * (i + 1))) >> 8;
            fx[sy].clp += ((y - fx[sy].clp) * tone) >> 15;
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
            fx[sy].hl += ((y - fx[sy].hl) * p->thl) >> 15;
            y = curve(T_LIM, (fx[sy].hl * p->tdrive) >> 8);   /* clean until AMOUNT drives it */
        } else if (t == FM_CASS) {
            a = y < 0 ? -y : y;
            fx[sy].fol += (a - fx[sy].fol) >> 8;
            c = tone - (fx[sy].fol >> 3);               /* loud: the top closes */
            if (fx[sy].drop) fx[sy].drop--;             /* a dropout: in and out over 3 ms, as the tape lifts */
            fx[sy].dd += ((fx[sy].drop ? 32768 : 0) - fx[sy].dd) >> 7;
            c -= (3000 * fx[sy].dd) >> 15;
            y = (y * (32768 - (((32768 - fx[sy].drop_g) * fx[sy].dd) >> 15))) >> 15;
            if (c < 1500) c = 1500;
            fx[sy].lp1 += ((y - fx[sy].lp1) * c) >> 15;
            fx[sy].lp2 += ((fx[sy].lp1 - fx[sy].lp2) * c) >> 15;
            y = curve(T_LIM, fx[sy].lp2 * 5 / 4);
        } else {
            fx[sy].hp += (y * 256 - fx[sy].hp) >> 9;    /* ~80 Hz out */
            y -= fx[sy].hp >> 8;
            c = fx[sy].tenv ? tone - ((tone * (fx[sy].tenv >> 1)) >> 15) : tone;   /* tracking dims the top */
            fx[sy].lp1 += ((y - fx[sy].lp1) * c) >> 15;
            fx[sy].lp2 += ((fx[sy].lp1 - fx[sy].lp2) * c) >> 15;
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
            fx[sy].vlp[0] += ((y - fx[sy].vlp[0]) * tone) >> 15;
            fx[sy].vlp[1] += ((fx[sy].vlp[0] - fx[sy].vlp[1]) * tone) >> 15;
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
        int32_t fade, lvl, sa, chp, c, mk = 384 + k0 * 3;   /* (1.5x, to 3x as BAND narrows) */
        if (--fx[sy].rcnt <= 0) {                       /* the drift: somewhere new each 0.1 - 0.85 s, most often near */
            uint32_t q = xs32(&fx[sy].mrng);
            fx[sy].rcnt = 300 + (int32_t)(q >> 21);
            q = xs32(&fx[sy].mrng) >> 17;
            fx[sy].rdt = (int32_t)((q * q) >> 15);
        }
        fx[sy].rdr += (fx[sy].rdt - fx[sy].rdr) >> 9;  /* glided, ~0.9 Hz */
        fade = (fx[sy].rdr * k3) >> 7;
        lvl = 32768 - ((fade * 3) >> 2);                /* down to ~-12 dB as it drifts */
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
            y = (int32_t)(((int64_t)x[i] * lvl) >> 15) + ((n * g) >> 15);
            fx[sy].rhp[0] += ((y - fx[sy].rhp[0]) * chp) >> 15; y -= fx[sy].rhp[0];
            fx[sy].rhp[1] += ((y - fx[sy].rhp[1]) * chp) >> 15; y -= fx[sy].rhp[1];
            fx[sy].rlp[0] += ((y - fx[sy].rlp[0]) * c) >> 15;   /* the top: four poles, as a receiver's is steep */
            fx[sy].rlp[1] += ((fx[sy].rlp[0] - fx[sy].rlp[1]) * c) >> 15;
            fx[sy].rlp[2] += ((fx[sy].rlp[1] - fx[sy].rlp[2]) * c) >> 15;
            fx[sy].rlp[3] += ((fx[sy].rlp[2] - fx[sy].rlp[3]) * c) >> 15;
            x[i] = curve(T_SAT, (fx[sy].rlp[3] * mk) >> 8);   /* the small speaker: made up, and soft at the top */
        }
    } else {                                            /* DICTAPHONE */
        int32_t mul = 256 + (127 - k2) * 2, af = (k0 * 9 * mul) >> 8, aw = (k0 * 46 * mul) >> 8;
        int32_t c = T_OP[clampi(84 + k2 * 14 / 127 + (k1 - 64) * 13 / 64, 0, 127)], chp = T_OP[50];
        int32_t hiss = 64 + (127 - k2), gmax = 256 + k3 * 14, a0 = fx[sy].agc_g, a1, as, pk2 = 0, fl, d = fx[sy].md, ds;
        uint32_t fr = 680000u + (uint32_t)k2 * 3800u, mi;   /* the capstan: 7 .. 12 Hz */
        /* the level control: from what it heard (in fast, out over ~0.2 s), the gain toward 6000, at most SQUASH's
         * (8x), glided; a block's step across it */
        a1 = fx[sy].agc * gmax > (6000 << 8) ? (6000 << 8) / (fx[sy].agc + 1) : gmax;
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
            fx[sy].rhp[0] += ((y - fx[sy].rhp[0]) * chp) >> 15; y -= fx[sy].rhp[0];   /* ~300 Hz up */
            fx[sy].rlp[0] += ((y - fx[sy].rlp[0]) * c) >> 15;
            fx[sy].rlp[1] += ((fx[sy].rlp[0] - fx[sy].rlp[1]) * c) >> 15;
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
            fx[sy].chlp[k] += ((y - fx[sy].chlp[k]) * tone) >> 15;
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
        fx[sy].chlp[0] += ((c + sd - fx[sy].chlp[0]) * tone) >> 15;
        fx[sy].chlp[1] += ((c - sd - fx[sy].chlp[1]) * tone) >> 15;
        l[i] = (x[i] >> 1) + ((fx[sy].chlp[0] * 7) >> 3);
        r[i] = (x[i] >> 1) + ((fx[sy].chlp[1] * 7) >> 3);
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
        int32_t xs = clampi((x[i] + 2) >> 2, -16383, 16383), u = xs + ((fx[sy].pfb[0] * fbk + 16384) >> 15), y = u;
        ch_buf[sy][fx[sy].cw++ & (CH_LEN - 1)] = (int16_t)sat16(x[i]);   /* (the line kept, for a type after) */
        for (st = 0; st < 4; st++) {                    /* y = a (u - y1) + u1 */
            y = clampi(px[st] + ((a * (u - py[st]) + 8192) >> 14), -65535, 65535);
            px[st] = u;
            py[st] = y;
            u = y;
        }
        fx[sy].pfb[0] = clampi(y, -32767, 32767);
        l[i] = r[i] = (xs + y) * 5 >> 1;                /* (x + y) / 2 * 1.25: x at a quarter above */
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
            fx[sy].ftl[c] += ((y - fx[sy].ftl[c]) * tone) >> 15;
            v[c] = fx[sy].ftl[c];
        }
        /* the line at the sound's level, as every type writes it (a type after reads it as it is), the sum soft */
        ch_buf[sy][fx[sy].cw++ & (CH_LEN - 1)] = (int16_t)curve(T_LIM, x[i] + ((v[0] * fbk) >> 15));
        fx[sy].fdc[0] += ((v[0] << 8) - fx[sy].fdc[0]) >> 9;
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

/* ---- the buses, at half rate */
static const uint16_t CB_LEN[4] = {557, 593, 641, 677};   /* ROOM: four combs, four all-passes (MONO2K's lengths) */
static const uint16_t AP_LEN[4] = {225, 341, 248, 367};
static int16_t cb_buf[557 + 593 + 641 + 677] POOL;
static int16_t ap_buf[225 + 341 + 248 + 367] POOL;
/* HALL, HAZE, BLOOM, LOFI: eight lines of 52..112 ms at 22.05 kHz, mixed by a Hadamard matrix (adds only), damped,
 * the first ones swept slowly so the tail shimmers rather than rings; four all-passes diffuse the input first */
#define FD_N 8
static const uint16_t FD_LEN[FD_N] = {1153, 1327, 1559, 1699, 1877, 2069, 2297, 2477};
static const uint16_t DF_LEN[4] = {142, 107, 379, 277};
static int16_t fd_buf[1153 + 1327 + 1559 + 1699 + 1877 + 2069 + 2297 + 2477] POOL;
static int16_t df_buf[142 + 107 + 379 + 277] POOL;
static struct { uint16_t pos[FD_N], dpos[4]; int32_t lp[FD_N], m[FD_N], fast, slow, hold, acc, ol, orr, dci, dcl, dcr; uint32_t lfo[2]; uint8_t half, step, vhalf; } fd;
static const uint16_t FD_OFF[FD_N] = {0, 1153, 2480, 4039, 5738, 7615, 9684, 11981};
static const uint16_t DF_OFF[4] = {0, 142, 249, 628};
#define EL_LEN 16384u                                   /* ECHO: 0.74 s of 8-bit mu-law at 22.05 kHz (its other half: the
                                                           synths' delays since 0.5) */
static uint8_t el_buf[EL_LEN] POOL;
static struct {
    uint16_t cb_pos[4], ap_pos[4];
    int32_t cb_lp[4], vl, vr, el, er, elp, ehp;
    uint32_t ew;
    int32_t vacc, eacc;
    uint8_t half;
} bus;

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

static void room_step(int32_t vin)
{
    int c, a;
    int32_t sum = 0, off = 0, ch[2], x, y;
    x = vin / 4;                                        /* a quarter in: the combs' headroom */
    for (c = 0; c < 4; c++) {
        int16_t *b = &cb_buf[off];
        y = b[bus.cb_pos[c]];
        bus.cb_lp[c] += ((y - bus.cb_lp[c]) * gp.vdamp + 16384) >> 15;   /* rounded: a loop amplifies a floor's bias */
        b[bus.cb_pos[c]] = (int16_t)sat16(x + ((bus.cb_lp[c] * gp.vfb + 16384) >> 15));
        if (++bus.cb_pos[c] >= CB_LEN[c]) bus.cb_pos[c] = 0;
        sum += y;
        off += CB_LEN[c];
    }
    ch[0] = ch[1] = sum;
    off = 0;
    for (a = 0; a < 4; a++) {                           /* two chains of two: left and right drift apart */
        int16_t *b = &ap_buf[off];
        int k = a >> 1;
        int32_t d = b[bus.ap_pos[a]], v = ch[k] + d / 2;
        b[bus.ap_pos[a]] = (int16_t)sat16(v);
        ch[k] = d - v / 2;
        if (++bus.ap_pos[a] >= AP_LEN[a]) bus.ap_pos[a] = 0;
        off += AP_LEN[a];
    }
    bus.vl = (ch[0] * gp.vret) >> 15;
    bus.vr = (ch[1] * gp.vret) >> 15;
}

/* the network: HALL four lines swept a little; HAZE all eight swept deep and slow, darker; BLOOM HALL's tank fed
 * through a swell (a slow follower over a fast one: a new sound rises into the space); LOFI held at 11 kHz into the
 * lines, stored at 8 bits, two lines wowing deep */
static void fdn_step(int32_t vin)
{
    int k, h, j, nmod = v_cur == VB_HAZE || v_cur == VB_SPACE ? 8 : v_cur == VB_LOFI ? 2 : 4;
    int32_t x = vin / 2, y[FD_N], ol, orr, damp = v_cur == VB_HAZE ? gp.vdamp * 3 / 4 : v_cur == VB_LOFI ? gp.vdamp / 2 : gp.vdamp;
    fd.dci += (x * 256 - fd.dci) >> 9;                  /* no DC in: a loop this long would hold it a hundredfold */
    x -= fd.dci >> 8;
    if (!(fd.step++ & 3)) {                             /* the sweep, every fourth step: four sines, the lines take turns */
        int32_t sv[4];
        sv[0] = sinp(fd.lfo[0]); sv[1] = sinp(fd.lfo[0] + 0x40000000u); sv[2] = sinp(fd.lfo[1]); sv[3] = sinp(fd.lfo[1] + 0x40000000u);
        for (k = 0; k < nmod; k++) fd.m[k] = (gp.vmod * ((k & 4 ? -sv[k & 3] : sv[k & 3]) + 32768)) >> 16;
        fd.lfo[0] += gp.vrate * 4u;
        fd.lfo[1] += (gp.vrate + (gp.vrate >> 2)) * 4u;
    }
    if (v_cur == VB_BLOOM) {                         /* the swell: slow over fast, 0..1 */
        int32_t a = (x < 0 ? -x : x) << 8, g, sn;
        fd.fast += a > fd.fast ? (a - fd.fast) >> 4 : -(fd.fast >> 12) - (fd.fast > 0);
        fd.slow += a > fd.slow ? (a - fd.slow) >> 14 : -(fd.slow >> 16) - (fd.slow > 0);
        sn = fd.slow >> 8 > 32767 ? 32767 : fd.slow >> 8;
        g = (fd.fast >> 8) > 0 ? (sn << 15) / ((fd.fast >> 8) + 1) : 32768;
        x = (x * (g > 32768 ? 32768 : g)) >> 15;
    }
    if (v_cur == VB_LOFI) { if ((fd.half ^= 1)) fd.hold = x; x = fd.hold; }
    for (k = 0; k < 4; k++) {                           /* diffusion: four all-passes, 0.62 */
        int16_t *b = &df_buf[DF_OFF[k]];
        int32_t d = b[fd.dpos[k]], v = x + ((d * 20316) >> 15);
        b[fd.dpos[k]] = (int16_t)sat16(v);
        x = d - ((v * 20316) >> 15);
        if (++fd.dpos[k] >= DF_LEN[k]) fd.dpos[k] = 0;
    }
    for (k = 0; k < FD_N; k++) {                        /* each line's end, the first ones swept, then damped */
        const int16_t *b = &fd_buf[FD_OFF[k]];
        uint32_t p = fd.pos[k];
        int32_t v;
        if (k < nmod) {
            int32_t m = fd.m[k], f = m & 255;
            uint32_t i0 = p + (uint32_t)(m >> 8), i1;
            if (i0 >= FD_LEN[k]) i0 -= FD_LEN[k];
            i1 = i0 + 1 >= FD_LEN[k] ? 0 : i0 + 1;
            v = (b[i0] * (256 - f) + b[i1] * f + 128) >> 8;   /* rounded: a floor's bias would hum in the loop */
        } else v = b[p];
        {                                               /* damped: a step at least one either way, so a dying tail
                                                           reaches zero instead of sitting a bit off it */
            int32_t e = v - fd.lp[k], st = (e * damp) >> 15;
            fd.lp[k] += st ? st : e > 0 ? 1 : e < 0 ? -1 : 0;
        }
        y[k] = fd.lp[k];
    }
    ol = y[0] - y[2] + y[4] - y[6] + (y[1] - y[7]) / 2;
    orr = y[1] - y[3] + y[5] - y[7] + (y[0] - y[6]) / 2;
    for (h = 1; h < FD_N; h <<= 1)                      /* Hadamard: lossless, every line into every line */
        for (k = 0; k < FD_N; k += h << 1)
            for (j = k; j < k + h; j++) { int32_t a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
    for (k = 0; k < FD_N; k++) {
        int32_t w = (k & 1 ? -x : x) + ((y[k] * gp.vfbq + 16384) >> 15);
        if (v_cur == VB_LOFI) w = (w + 128) & ~255;  /* eight bits */
        METER(dmeter.sat_fd += w > 32767 || w < -32768);
        fd_buf[FD_OFF[k] + fd.pos[k]] = (int16_t)sat16(w);
        if (++fd.pos[k] >= FD_LEN[k]) fd.pos[k] = 0;
    }
    fd.dcl += (ol * 256 - fd.dcl) >> 10;                /* nor out: what the loop's rounding leaves */
    fd.dcr += (orr * 256 - fd.dcr) >> 10;
    fd.ol = ((ol - (fd.dcl >> 8)) * gp.vret) >> 16;
    fd.orr = ((orr - (fd.dcr >> 8)) * gp.vret) >> 16;
}

/* the network's own pass, a run of the verb's input at a time, half rate, its output held for the sample between:
 * on the second core while the first mixes the next run (a run's latency, a few ms before the tail: unheard) */
#define VQ_LEN 1024u
static int32_t vq[VQ_LEN], wq[2][VQ_LEN];
static uint32_t vq_w, vq_r, wq_w, wq_r;
static void verb_job(void)
{
    uint32_t n = vq_w - vq_r;
    while (n--) {
        int32_t x = vq[vq_r++ & (VQ_LEN - 1)];
        if (!(fd.vhalf ^= 1)) fdn_step((fd.acc + x) >> 1);
        else fd.acc = x;
        wq[0][wq_w & (VQ_LEN - 1)] = fd.ol;
        wq[1][wq_w & (VQ_LEN - 1)] = fd.orr;
        wq_w++;
    }
}

/* GRAINS, the ECHO's other type: the same line, read by six grains at once, each from a random moment of the last
 * 1.4 s, as long as SIZE, as often as DENSITY, some an octave or a fifth up as TEXTURE rises, each its own place
 * across the stereo, shaped by a window; a cloud of what was played, into the verb too */
#define GR_N 6
static struct { uint32_t pos, inc, len, age; int16_t pl, pr; } gr[GR_N];
static uint32_t gr_clock, gr_rng = 0x2545F491u;
static void grains_step(int32_t ein)
{
    int k;
    int32_t l = 0, r = 0;
    el_buf[bus.ew & (EL_LEN - 1)] = mulaw_enc(curve(T_SAT, ein));
    bus.ew++;
    gr_clock += (uint32_t)gp.gdens;
    if (gr_clock >= 65536u) {                           /* a grain begins, if one is free */
        gr_clock -= 65536u;
        for (k = 0; k < GR_N && gr[k].len; k++) {}
        if (k < GR_N) {
            uint32_t rn = xs32(&gr_rng), len = (uint32_t)gp.glen, inc = 256u, back;
            if ((int32_t)((rn >> 8) & 127u) < gp.gtex) inc = rn & 1u ? 512u : 384u;   /* an octave, or a fifth, up */
            else inc = 254u + (rn >> 30);               /* or as played, a hair off */
            back = len * inc / 256u + 64u + (xs32(&gr_rng) % (EL_LEN - 2u * len - 128u));
            gr[k].pos = (bus.ew - back) << 8; gr[k].inc = inc; gr[k].len = len; gr[k].age = 0;
            gr[k].pl = (int16_t)T_PAN[127 - (rn >> 25)]; gr[k].pr = (int16_t)T_PAN[rn >> 25];
        }
    }
    for (k = 0; k < GR_N; k++) {
        uint32_t i, f;
        int32_t a, b, x, t, w;
        if (!gr[k].len) continue;
        i = gr[k].pos >> 8; f = gr[k].pos & 255u;
        a = T_MULAW[el_buf[i & (EL_LEN - 1)]]; b = T_MULAW[el_buf[(i + 1) & (EL_LEN - 1)]];
        x = a + (((b - a) * (int32_t)f) >> 8);
        t = (int32_t)(gr[k].age * 256u / gr[k].len);    /* the window: a parabola over the grain */
        w = t * (255 - t) / 64;
        x = (x * w) >> 8;
        l += (x * gr[k].pl) >> 15;
        r += (x * gr[k].pr) >> 15;
        gr[k].pos += gr[k].inc;
        if (++gr[k].age >= gr[k].len) gr[k].len = 0;
    }
    bus.el = (l * gp.eret) >> 16;
    bus.er = (r * gp.eret) >> 16;
}

/* one half-rate step: the verb's and the echo's inputs in, their stereo outputs kept in bus.vl/vr, el/er */
static void bus_step(int32_t vin, int32_t ein)
{
    int32_t l, r;
    if (v_cur == VB_ROOM) room_step(vin);
    if (e_cur == ET_OFF) { bus.el = bus.er = 0; return; }
    if (e_cur == ET_GRAINS) { grains_step(ein); return; }
    /* ECHO: the left read at the time, the right a little later; the loop through a low-pass and a soft clip */
    l = T_MULAW[el_buf[(bus.ew - (uint32_t)gp.etime) & (EL_LEN - 1)]];
    r = T_MULAW[el_buf[(bus.ew - (uint32_t)gp.etime - (uint32_t)(gp.etime >> 4)) & (EL_LEN - 1)]];
    bus.elp += ((((l + r) / 2) - bus.elp) * gp.elp + 16384) >> 15;
    bus.ehp += (bus.elp * 256 - bus.ehp) / 128;         /* ~30 Hz out of the loop, Q8 so it settles: no low build-up */
    el_buf[bus.ew & (EL_LEN - 1)] = mulaw_enc(curve(T_SAT, ein + (((bus.elp - (bus.ehp + 128) / 256) * gp.efb + 16384) >> 15)));
    bus.ew++;
    bus.el = (l * gp.eret) >> 15;
    bus.er = (r * gp.eret) >> 15;
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
#define XG_STEP (32768 / 16)                            /* a block's step of the share: 16 blocks, 256 samples */
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
    int row, i;
    int32_t dry[DMK_SUB];
    for (row = 0; row < 1; row++) {                     /* COLOUR (MEDIA: the mix's, gm_side) */
        int want = p->colour;
        int32_t g1 = xg_next(s, row, want), g0 = fx[s].xg[row];
        if (fx[s].cur[row]) {
            int fading = g0 < 32768 || g1 < 32768;
            if (fading) for (i = 0; i < DMK_SUB; i++) dry[i] = x[i];
            colour_block(s, x, fx[s].cur[row]);
            if (fading) xg_mix(x, dry, g0, g1);
        }
        fx[s].xg[row] = g1;
        if (want != fx[s].cur[row] && !g1) { fx[s].cur[row] = (uint8_t)want; fx_enter(s, row, want); }
    }
    {                                                   /* MOTION: here (vibrato and drift are the voices') */
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

/* a synth's DELAY (the owner's, 2026-10-25: one a synth, the media one for the mix): its line at a quarter of the rate
 * (lofi, and the memory: 0.84 s), the input the synth after its rows, the read at the tempo's division (dmk_seq sets
 * dtime), glided to a new time; DELAY's two sides a hair apart, PING-PONG's right twice as late and fed back from it,
 * so the repeats go left, right. Its wet glided in and out (a type changed: out, then the new one in) */
#define DL_LEN 9216u                                    /* 0.84 s of 8-bit mu-law at 11.025 kHz: a quarter at 72 */
static uint8_t dl_buf[DMK_SYNTHS][DL_LEN] POOL;
static struct { uint32_t w; int32_t nl, nr, lp, hp, tq, g; uint8_t cur; } dls[DMK_SYNTHS];
static void dl_clear(int s)
{
    uint32_t i;
    for (i = 0; i < DL_LEN; i++) dl_buf[s][i] = 0xFF;   /* (mu-law's silence) */
    dls[s].nl = dls[s].nr = dls[s].lp = dls[s].hp = 0;
    dls[s].tq = (int32_t)(syn[s].dtime << 8);
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
static void syn_delay(int s, int32_t *pl, int32_t *pr)
{
    const synth_t *p = &syn[s];
    int want = p->dtype, q, k;
    int32_t g0 = dls[s].g, g1, to, el, er;
    if (want != dls[s].cur && !dls[s].cur) { dls[s].cur = (uint8_t)want; dl_clear(s); dls[s].g = g0 = 0; }   /* from off: in */
    to = want == dls[s].cur ? p->dmix : 0;
    g1 = g0 + clampi(to - g0, -1024, 1024);
    el = (dls[s].nl * g0) >> 15; er = (dls[s].nr * g0) >> 15;   /* where the last block left it, at its share then */
    if (dls[s].cur)
        for (q = 0; q < 4; q++) {                       /* four steps at a quarter of the rate */
            const int32_t *a = pl + q * 4, *c = pr + q * 4;
            int32_t in = (a[0] + a[1] + a[2] + a[3] + c[0] + c[1] + c[2] + c[3]) >> 3, l, r, back, gq, dl, dr, al, ar;
            uint32_t tq;
            dls[s].tq += ((int32_t)(p->dtime << 8) - dls[s].tq) >> 8;
            tq = (uint32_t)dls[s].tq;
            l = dl_at(s, tq);
            r = dls[s].cur == 2 ? dl_at(s, tq * 2u) : dl_at(s, tq + (tq >> 6));
            back = dls[s].cur == 2 ? r : (l + r) >> 1;
            dls[s].lp += ((back - dls[s].lp) * p->dtone + 16384) >> 15;   /* the loop: its tone, its lows out, soft */
            dls[s].hp += (dls[s].lp * 256 - dls[s].hp) >> 6;
            dl_buf[s][dls[s].w] = mulaw_enc(curve(T_SAT, in + (((dls[s].lp - ((dls[s].hp + 128) >> 8)) * p->dfb + 16384) >> 15)));
            if (++dls[s].w >= DL_LEN) dls[s].w = 0;
            dls[s].nl = l; dls[s].nr = r;
            gq = g0 + (((g1 - g0) * (q + 1)) >> 2);     /* its share glided across the block, at each step */
            l = (l * gq) >> 15; r = (r * gq) >> 15;
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

/* the whole mix's MEDIA (since 0.5, the owner's: one, for the mix): the media of before on each side, the state of
 * the left in slot 0 and the right in slot 1, the same dice and the same settings (gmed), so the wow is one; a fade
 * when its type changes. The tapes at half the level, so they bend as a synth did (the mix is up to twice one); the
 * later three as it is: their crackle, their hiss and the dictaphone's level are their own, not the sound's */
static synth_t gmed;
static int32_t gm_g;
static uint8_t gm_cur;
/* the run's mix, its two sides, for the media; its share and type a block (planned first, so the two sides, one on
 * each core, run the same: a side in slot 0, the other in 1), the dice a side takes on a type's way in */
#define VB_MAX 256                                      /* frames a pass */
#define GM_NB (VB_MAX / DMK_SUB)
static int32_t gm_x[2][VB_MAX];                         /* (in RAM: the pool is full) */
static int32_t gm_g0[GM_NB], gm_g1[GM_NB];
static uint8_t gm_ty[GM_NB], gm_in[GM_NB], gm_pend;   /* (gm_pend: a type to take in on the next run's first block) */
static uint32_t gm_seed = 0x6C8E9CF5u;
static int gm_nb;
static void gm_plan(int nb)
{
    int b, enter = gm_pend, want = gmed.media;
    gm_nb = nb;
    for (b = 0; b < nb; b++) {
        if (want != gm_cur && !gm_cur) { gm_cur = (uint8_t)want; gm_g = 0; enter = 1; }   /* from off: in */
        gm_ty[b] = gm_cur; gm_in[b] = (uint8_t)enter; enter = 0;
        gm_g0[b] = gm_g;
        gm_g = clampi(gm_g + (want != gm_cur ? -XG_STEP : XG_STEP), 0, 32768);
        gm_g1[b] = gm_g;
        if (want != gm_cur && !gm_g) { gm_cur = (uint8_t)want; enter = 1; }   /* out: the new type from the next block */
        if (gm_in[b]) gm_seed = gm_seed * 1664525u + 1013904223u;
    }
    gm_pend = (uint8_t)enter;
}
static void gm_side(int c)
{
    int b, i;
    int32_t dry[DMK_SUB];
    for (b = 0; b < gm_nb; b++) {
        int32_t *x = &gm_x[c][b * DMK_SUB], g0 = gm_g0[b], g1 = gm_g1[b];
        int t = gm_ty[b], half = t && t < FM_VINYL;
        if (gm_in[b]) { fx_enter(c, 1, t); fx[c].mrng = gm_seed; }
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
static void gm_job(void) { gm_side(1); }

/* the screen reads these: what each synth sounds, now */
static int32_t syn_peak[DMK_SYNTHS];
static int32_t syn_dc[DMK_SYNTHS];                      /* each synth's DC blocker (a sample's loop can sit off zero) */

/* the verb's lines and state silent (dsp_reset; a verb back from off) */
static void verb_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof cb_buf / sizeof cb_buf[0]; i++) cb_buf[i] = 0;
    for (i = 0; i < sizeof ap_buf / sizeof ap_buf[0]; i++) ap_buf[i] = 0;
    for (i = 0; i < sizeof fd_buf / sizeof fd_buf[0]; i++) fd_buf[i] = 0;
    for (i = 0; i < sizeof df_buf / sizeof df_buf[0]; i++) df_buf[i] = 0;
    { uint8_t *z = (uint8_t *)&fd; uint32_t k; for (k = 0; k < sizeof fd; k++) z[k] = 0; }
    for (i = 0; i < 4; i++) bus.cb_lp[i] = 0;
    bus.vl = bus.vr = 0;
}
/* the echo's line silent (mu-law's silence is 0xFF, not 0: 0 is its loudest negative), its grains gone */
static void echo_clear(void)
{
    uint32_t i;
    for (i = 0; i < EL_LEN; i++) el_buf[i] = 0xFF;
    for (i = 0; i < GR_N; i++) gr[i].len = 0;
    gr_clock = 0;
    bus.el = bus.er = bus.elp = bus.ehp = 0;
}

/* the lines and the state, silent */
static void dsp_reset(void)
{
    uint32_t i;
    for (i = 0; i < EL_LEN; i++) el_buf[i] = 0xFF;
    for (i = 0; i < sizeof cb_buf / sizeof cb_buf[0]; i++) cb_buf[i] = 0;
    for (i = 0; i < sizeof ap_buf / sizeof ap_buf[0]; i++) ap_buf[i] = 0;
    for (i = 0; i < sizeof fd_buf / sizeof fd_buf[0]; i++) fd_buf[i] = 0;
    for (i = 0; i < sizeof df_buf / sizeof df_buf[0]; i++) df_buf[i] = 0;
    for (i = 0; i < 2 * TP_LEN; i++) tp_buf[i / TP_LEN][i % TP_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) dl_clear((int)i);
    gm_cur = 0; gm_g = 0; gm_pend = 0;
    for (i = 0; i < DMK_SYNTHS * CH_LEN; i++) ch_buf[i / CH_LEN][i % CH_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) {
        uint8_t *z = (uint8_t *)&fx[i];
        uint32_t k;
        for (k = 0; k < sizeof fx[i]; k++) z[k] = 0;
        fx[i].rng = 0x9E3779B9u * (i + 1u);
        fx[i].mrng = 0x6C8E9CF5u;                       /* (the same for the mix's two sides: one tape, its wow both sides) */
        fx[i].lg[0] = fx[i].lg[1] = fx[i].lg[2] = 32768;
        syn_peak[i] = 0; syn_dc[i] = 0;
    }
    { uint8_t *z = (uint8_t *)&fd; uint32_t k; for (k = 0; k < sizeof fd; k++) z[k] = 0; }
    vq_w = vq_r = wq_w = wq_r = 0;
    for (i = 0; i < DMK_SYNTHS; i++) {
        vc_tail[i] = 0; syn_last[i] = 0;
        lfo[i].ph = lfo[i].poff = 0; lfo[i].sm = lfo[i].gs = lfo[i].hold = lfo[i].fade = lfo[i].cut = lfo[i].pit = 0; lfo[i].rst = 0;
        lfo[i].rng = 0x2545F491u * (i + 3u); lfo[i].lv = 32768; lfo[i].pl = lfo[i].pr = -1;
    }
    v_cur = gp.vtype; e_cur = gp.etype; v_g = e_g = 32768;
    for (i = 0; i < GR_N; i++) gr[i].len = 0;
    gr_clock = 0;
    bus.vl = bus.vr = bus.el = bus.er = bus.elp = bus.ehp = bus.vacc = bus.eacc = 0;
    for (i = 0; i < 4; i++) bus.cb_lp[i] = 0;
}

/* ---- the render: the voices first, a whole call at once, half of them on the second core when there is one; then
 * sample by sample the synths' effects, level, pan, sends, the buses and the limiter */
static int32_t vbuf[2][DMK_SYNTHS][VB_MAX] POOL;
static int16_t vib_c[DMK_SYNTHS][VB_MAX / DMK_SUB];     /* each synth's vibrato a block, cents Q4: both cores read it */
static int v_frames;

/* the formants for VOWEL, as cutoff indexes (T_SVF): u, o, a, e, i; the cutoff glides through them */
static const uint8_t VOW[5][2] = {{112, 164}, {132, 160}, {156, 175}, {140, 201}, {107, 211}};

/* a step's rate moved by c cents Q4: within half a semitone by the cents table alone, past it (the LFO, the glide)
 * by the semitones' too */
static uint32_t inc_by(uint32_t inc0, int32_t c)
{
    int32_t ct = c / 16, st, r;
    if (ct >= -50 && ct <= 50) return (uint32_t)(((uint64_t)inc0 * T_CENT[50 + ct]) >> 16);
    st = clampi((ct + 4850) / 100 - 48, -48, 48);     /* the nearest semitone (ct >= -4850 after the clamp) */
    r = clampi(ct - st * 100, -50, 50);
    return (uint32_t)(((((uint64_t)inc0 * T_SEMI[st + 48]) >> 16) * T_CENT[50 + r]) >> 16);
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
    for (v = half * (DMK_VOICES / 2); v < (half + 1) * (DMK_VOICES / 2); v++) {
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
/* the second core's share of the mix, in two parts: the last synth's chain while this core runs the other two; then
 * the verb's network, on the last run's input, while this core mixes (as 0.3 ran it, which the owner heard clean: the
 * network over the chains alone kept the first core waiting) */
static void chain_job(void) { chain_run(DMK_SYNTHS - 1); }

static void dsp_render(int16_t *lr, int frames)
{
    int b, i, s, n, at;
    for (at = 0; at < frames; at += n) {
        int nb, par;
        uint32_t vfill;
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
        /* the chains, the last on the second core with the network (the last run's input: the mix takes what it
         * made), the rest here */
        if (v_cur != gp.vtype && !v_g) {
            if (v_cur == VB_OFF || (v_cur == VB_ROOM) != (gp.vtype == VB_ROOM)) verb_clear();
            v_cur = gp.vtype;
        }   /* faded out: the new
                                                           type, from the next run; out of off, from silence (not
                                                           what was held when it went off) */
        if (e_cur != gp.etype && !e_g) { if (e_cur == ET_OFF) echo_clear(); e_cur = gp.etype; }
        if (v_cur == VB_ROOM || v_cur == VB_OFF) { vq_r = vq_w; wq_r = wq_w; }   /* the room runs in the mix, off nothing: the queue stays empty */
        vfill = wq_w - wq_r;
        ch_nb = nb;
        par = dmk_par_begin && dmk_par_begin(chain_job);
        for (s = 0; s < DMK_SYNTHS - 1; s++) chain_run(s);
        if (par) dmk_par_end(); else chain_job();
        par = v_cur != VB_ROOM && v_cur != VB_OFF && dmk_par_begin && dmk_par_begin(verb_job);
        if (v_cur != VB_ROOM && v_cur != VB_OFF && !par) verb_job();
        for (b = 0; b < nb; b++) {
            int32_t L[DMK_SUB], R[DMK_SUB], vin[DMK_SUB], ein[DMK_SUB];
            for (i = 0; i < DMK_SUB; i++) L[i] = R[i] = vin[i] = ein[i] = 0;
            if (mix_dg) {                               /* a song's fade, a step a block */
                mix_g += mix_dg;
                if (mix_g <= 0) { mix_g = 0; mix_dg = mix_next; mix_next = 0; }
                if (mix_g >= 32768) { mix_g = 32768; mix_dg = mix_next; mix_next = 0; }
            }
            for (s = 0; s < DMK_SYNTHS; s++) {
                const synth_t *p = &syn[s];
                const int32_t *sl = &vbuf[0][s][b * DMK_SUB], *sr = &vbuf[1][s][b * DMK_SUB];
                int32_t pk = 0, lvl = mix_g < 32768 ? (p->level * mix_g) >> 15 : p->level;
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
                /* each side held under twice full scale (the limiter bends it anyway), so its products with a gain
                 * (to 32768) stay in 32 bits: a chain past full at full level wrapped into a click */
                if (mod) for (i = 0; i < DMK_SUB; i++) {   /* the level and the pan, glided across the block */
                    int32_t xl = clampi((sl[i] * lvl) >> 14, -65535, 65535), xr = clampi((sr[i] * lvl) >> 14, -65535, 65535), x;
                    int32_t lv = lv0 + (lv1 - lv0) * i / DMK_SUB;
                    xl = (xl * lv) >> 15; xr = (xr * lv) >> 15;
                    x = (xl + xr) / 2;
                    L[i] += (xl * (gl0 + (gl1 - gl0) * i / DMK_SUB)) >> 15;
                    R[i] += (xr * (gr0 + (gr1 - gr0) * i / DMK_SUB)) >> 15;
                    vin[i] += (x * p->vsend) >> 15;
                    ein[i] += (x * p->esend) >> 15;
                    if (x > pk) pk = x; else if (-x > pk) pk = -x;
                } else for (i = 0; i < DMK_SUB; i++) {
                    int32_t xl = clampi((sl[i] * lvl) >> 14, -65535, 65535), xr = clampi((sr[i] * lvl) >> 14, -65535, 65535), x = (xl + xr) / 2;   /* back up */
                    L[i] += (xl * p->gl) >> 15;
                    R[i] += (xr * p->gr) >> 15;
                    vin[i] += (x * p->vsend) >> 15;
                    ein[i] += (x * p->esend) >> 15;
                    if (x > pk) pk = x; else if (-x > pk) pk = -x;
                }
                syn_peak[s] = pk > syn_peak[s] ? pk : syn_peak[s] - (syn_peak[s] >> 6);
            }
            /* the returns' fades, a step a block (16 blocks); the buses at half rate, held for the sample between; then
             * the limiter */
            v_g += gp.vtype != v_cur ? (v_g > 0 ? -2048 : 0) : (v_g < 32768 ? 2048 : 0);
            e_g += gp.etype != e_cur ? (e_g > 0 ? -2048 : 0) : (e_g < 32768 ? 2048 : 0);
            for (i = 0; i < DMK_SUB; i++) {
                int32_t wl = bus.vl, wr = bus.vr;
                if (!(bus.half ^= 1)) bus_step((bus.vacc + vin[i]) >> 1, (bus.eacc + ein[i]) >> 1);
                else { bus.vacc = vin[i]; bus.eacc = ein[i]; }
                int32_t el, er;
                el = e_g < 32768 ? (bus.el * e_g) >> 15 : bus.el;
                er = e_g < 32768 ? (bus.er * e_g) >> 15 : bus.er;
                if (v_cur != VB_ROOM && v_cur != VB_OFF) {   /* the network's: what it made of the last run, in order */
                    vq[vq_w++ & (VQ_LEN - 1)] = vin[i] + (e_cur == ET_GRAINS ? (el + er) / 2 : 0);   /* the grains, into the space */
                    wl = wr = 0;
                    if (vfill) { wl = wq[0][wq_r & (VQ_LEN - 1)]; wr = wq[1][wq_r & (VQ_LEN - 1)]; wq_r++; vfill--; }
                } else if (v_cur == VB_ROOM) { wl = bus.vl; wr = bus.vr; }
                else wl = wr = 0;
                if (v_g < 32768) { wl = (wl * v_g) >> 15; wr = (wr * v_g) >> 15; }
#ifdef DMK_METER
                { int32_t a = L[i] + wl + el, c = R[i] + wr + er; a = a < 0 ? -a : a; c = c < 0 ? -c : c; if (c > a) a = c;
                  if (a > dmeter.pre_max) dmeter.pre_max = a; dmeter.knee += a > 24575; }
#endif
                gm_x[0][b * DMK_SUB + i] = L[i] + wl + el; gm_x[1][b * DMK_SUB + i] = R[i] + wr + er;   /* (for the media) */
            }
#ifdef DMK_BENCH
            bench_block(DMK_SUB);
#endif
        }
        if (par) dmk_par_end();
        gm_plan(nb);                                    /* the mix's MEDIA: a side on each core */
        par = (gm_cur || gm_ty[0] || gm_pend) && dmk_par_begin && dmk_par_begin(gm_job);
        gm_side(0);
        if (par) dmk_par_end(); else gm_job();
        for (i = 0; i < n; i++) {
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
