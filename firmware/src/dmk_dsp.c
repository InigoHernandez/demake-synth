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
enum { FC_OFF, FC_SAT, FC_CRUSH, FC_LOSSY, FC_NOISE };          /* COLOUR */
enum { FM_OFF, FM_TAPE, FM_CASS, FM_VHS };                       /* MEDIA */
enum { FO_OFF, FO_CHORUS, FO_VIB, FO_DRIFT };                    /* MOTION */
enum { VB_ROOM, VB_HALL, VB_HAZE, VB_BLOOM, VB_LOFI };           /* the VERB's types */
typedef struct {
    uint8_t smp, loops, ftype, tape;
    int16_t tune, cut, fenv;       /* semitones; the cutoff's index 0..255 (255: open); the envelope's reach in indexes */
    int8_t fine;                   /* cents, -50..50 */
    uint16_t start, resq, dec, rel;   /* blocks; damping Q14; decay and release factors Q16 a block */
    uint32_t atk;                  /* the attack's step a block, Q24 */
    int32_t sus, level, gl, gr, vsend, esend;   /* Q24; Q15 ... */
    int32_t tdepth, tdrive;        /* the tape's wow depth (Q8 samples a unit of the sine), drive Q8 */
    uint16_t thl;                  /* its head loss, a one-pole Q15 */
    uint32_t twow, tflut;          /* its wow's and flutter's phase steps */
    uint8_t colour, media, motion; /* the rows' effects (FC_, FM_, FO_), 0 off */
    uint8_t ck[4], mk[4], ok[4];   /* their knobs, 0..127 as turned */
} synth_t;
static synth_t syn[DMK_SYNTHS];

/* the shared buses' controls */
static struct { int32_t vret, vfb, vdamp, eret, efb, elp, etime, vfbq, vmod; uint32_t vrate; uint8_t vtype; } gp;

/* ---- the voices */
typedef struct {
    const dmk_sample_t *s;
    uint32_t ph, inc, inc0, age, rng;
    int32_t a, b, s1, s2, env, lp, bp, lp2, bp2;
    int16_t drift, dgoal, det;     /* MOTION's drift, its goal, the note's own detune (cents Q4) */
    uint16_t blocks;               /* blocks since the note began: vibrato's fade-in */
    uint16_t blk, end;
    uint8_t bi, stage, note, sy, on, loop;   /* stage: 0 silent, 1 attack, 2 decay and sustain, 3 release */
    int16_t buf[28];
} voice_t;
static voice_t vc[DMK_VOICES];
static uint32_t vc_clock;

static const int8_t ADPCM_F[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* a block of 28 samples from its 16 bytes, the predictor's state carried on */
static void blk_decode(voice_t *o)
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

static int32_t smp_next(voice_t *o)
{
    if (o->bi >= 28) {
        if (++o->blk >= o->end) {
            if (!o->loop) { o->stage = 0; o->env = 0; return 0; }
            o->blk = o->s->loop0; o->s1 = o->s->ls1; o->s2 = o->s->ls2;
        }
        blk_decode(o);
        o->bi = 0;
    }
    return o->buf[o->bi++];
}

static void note_on_s(int sy, int n)
{
    int i, best = 0, k;
    uint32_t oldest = 0xFFFFFFFFu, inc;
    const synth_t *p = &syn[sy];
    const dmk_sample_t *s;
    voice_t *o;
    if (n < 0 || n > 127 || sy < 0 || sy >= DMK_SYNTHS) return;
    for (i = 0; i < DMK_VOICES; i++)                    /* a silent voice, else the oldest released, else the oldest */
        if (vc[i].stage == 0) { best = i; oldest = 0; break; }
    if (oldest)
        for (i = 0; i < DMK_VOICES; i++) {
            uint32_t a = vc[i].age + (vc[i].on ? 0x80000000u : 0u);
            if (a < oldest) { oldest = a; best = i; }
        }
    o = &vc[best];
    s = &DMK_SAMPLES[p->smp < DMK_SAMPLES_N ? p->smp : 0];
    k = clampi(n - s->root + p->tune, -48, 48);
    inc = (uint32_t)(((uint64_t)s->rate_q16 * T_SEMI[k + 48]) >> 16);
    o->inc = o->inc0 = (uint32_t)(((uint64_t)inc * T_CENT[clampi(50 - s->cents + p->fine, 0, 100)]) >> 16);
    o->rng = o->rng * 1664525u + 1013904223u + (uint32_t)n;
    o->det = p->motion == FO_DRIFT ? (int16_t)((int32_t)((o->rng >> 16) % 801u) - 400) * p->ok[1] / 127 : 0;
    o->drift = o->dgoal = 0;
    o->blocks = 0;
    o->s = s;
    o->loop = (uint8_t)(s->loops && p->loops);          /* LOOP OFF: a looped sample plays through once */
    o->end = o->loop ? s->loop1 : s->blocks;
    o->blk = (uint16_t)(p->start < o->end - 1 ? p->start : 0);
    o->s1 = o->s2 = 0;
    blk_decode(o);
    o->bi = 0;
    o->a = smp_next(o);
    o->b = smp_next(o);
    o->ph = 0;
    o->env = 0; o->stage = 1;
    o->lp = o->bp = o->lp2 = o->bp2 = 0;
    o->note = (uint8_t)n; o->sy = (uint8_t)sy; o->on = 1; o->age = vc_clock++;
}

static void note_off_s(int sy, int n)
{
    int i;
    for (i = 0; i < DMK_VOICES; i++)
        if (vc[i].on && vc[i].note == n && vc[i].sy == sy) { vc[i].on = 0; if (vc[i].stage) vc[i].stage = 3; }
}

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
static int16_t tp_buf[DMK_SYNTHS][TP_LEN] POOL;
static int16_t ch_buf[DMK_SYNTHS][CH_LEN] POOL;
static struct {
    uint32_t tw, wow, flut, rng, wdrift, warb, tick, cw, clfo, hold_ph, vib;
    int32_t hl, lp1, lp2, hp, fol, drop, drop_g, track, track_g, wd;     /* MEDIA */
    int32_t clp, hold, sl, band[2], peak[3], step[3], frame, crk, nfol, nlp, hum, sdc;   /* COLOUR */
    int32_t chlp[2];                                    /* MOTION */
    uint8_t gate;
} fx[DMK_SYNTHS];

/* COLOUR: SATURATE (drive into the tape's curve, a bias for even harmonics, a tone after), CRUSH (bits, a rate held
 * by a jittered clock, a low-pass before), LOSSY (three bands, each quantized by its last frame's peak, the weak and
 * random ones dropped: a codec's smear and holes), NOISE (hiss, crackle and hum riding the sound) */
static void colour_block(int sy, int32_t *x)
{
    const synth_t *p = &syn[sy];
    int i, k0 = p->ck[0], k1 = p->ck[1], k2 = p->ck[2], k3 = p->ck[3];
    int32_t tone = T_OP[64 + k1 / 2];
    if (p->colour == FC_SAT) {
        int32_t drive = 256 + k0 * 14, bias = k2 * 60, base = curve(T_SAT, (bias * drive) >> 8), mix = k3 * 258;
        for (i = 0; i < DMK_SUB; i++) {
            int32_t y = curve(T_SAT, ((x[i] + bias) * drive) >> 8) - base;
            y = y * 256 / (160 + drive / 3);            /* about the level it went in at */
            fx[sy].sdc += (y * 256 - fx[sy].sdc) >> 10; /* the bias's DC, out (~7 Hz, Q8) */
            y -= fx[sy].sdc >> 8;
            fx[sy].clp += ((y - fx[sy].clp) * tone) >> 15;
            x[i] += ((fx[sy].clp - x[i]) * mix) >> 15;
        }
    } else if (p->colour == FC_CRUSH) {
        int sh = k0 * 13 / 127, k = 127 - k2;
        uint32_t step = 600u + (uint32_t)(k * k) * 4u;
        for (i = 0; i < DMK_SUB; i++) {
            fx[sy].clp += ((x[i] - fx[sy].clp) * tone) >> 15;
            fx[sy].hold_ph += step + (k3 ? (xs32(&fx[sy].rng) >> 20) * (uint32_t)k3 / 8u : 0u);
            if (fx[sy].hold_ph >= 65536u) {
                fx[sy].hold_ph &= 65535u;
                fx[sy].hold = sh ? ((fx[sy].clp + (1 << (sh - 1))) >> sh) << sh : fx[sy].clp;
            }
            x[i] = fx[sy].hold;
        }
    } else if (p->colour == FC_LOSSY) {
        int frame = 64 << (k2 * 3 / 128), bits[3];
        bits[0] = 9 - k0 * 6 / 127; bits[1] = 8 - k0 * 6 / 127; bits[2] = 7 - k0 * 6 / 127;
        for (i = 0; i < DMK_SUB; i++) {
            int32_t b[3], y = 0;
            int j;
            fx[sy].band[0] += ((x[i] - fx[sy].band[0]) * 2300) >> 15;    /* ~500 Hz */
            fx[sy].band[1] += ((x[i] - fx[sy].band[1]) * 13000) >> 15;   /* ~3 kHz */
            b[0] = fx[sy].band[0]; b[1] = fx[sy].band[1] - fx[sy].band[0]; b[2] = x[i] - fx[sy].band[1];
            for (j = 0; j < 3; j++) {
                int32_t a = b[j] < 0 ? -b[j] : b[j], st = fx[sy].step[j];
                if (a > fx[sy].peak[j]) fx[sy].peak[j] = a;
                if (fx[sy].gate >> j & 1u) continue;    /* a hole: this band dropped this frame */
                y += st > 1 ? (b[j] + (b[j] < 0 ? -st / 2 : st / 2)) / st * st : b[j];
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
    } else if (p->colour == FC_NOISE) {
        for (i = 0; i < DMK_SUB; i++) {
            int32_t a = x[i] < 0 ? -x[i] : x[i], g, n;
            if (a > fx[sy].nfol) fx[sy].nfol += (a - fx[sy].nfol) >> 4;   /* rides the sound, its tail too */
            else fx[sy].nfol -= (fx[sy].nfol >> 14) + (fx[sy].nfol > 0);   /* at least a step: it reaches zero */
            g = fx[sy].nfol > 8192 ? 32768 : fx[sy].nfol * 4;
            fx[sy].nlp += ((((int32_t)xs32(&fx[sy].rng) >> 16) - fx[sy].nlp) * tone) >> 15;
            n = fx[sy].nlp * k0 / 2600;                 /* the hiss: at most ~-26 dB */
            if ((xs32(&fx[sy].rng) >> 20) < (uint32_t)(k2 * k2) / 64u) {   /* a crackle: a click of either sign */
                uint32_t r = xs32(&fx[sy].rng);
                fx[sy].crk = (int32_t)(2000u + (r & 0x1FFFu)) * (r >> 31 ? -1 : 1);
            }
            n += fx[sy].crk; fx[sy].crk = fx[sy].crk * 5 / 8;
            fx[sy].hum += 4870390;                      /* 50 Hz */
            n += ((sinp((uint32_t)fx[sy].hum) + sinp((uint32_t)fx[sy].hum * 3u) / 3) * k3) >> 12;
            x[i] += (n * g) >> 15;
        }
    }
}

/* MEDIA: TAPE (wow drifting in rate, flutter, head loss, saturation), CASSETTE (a faster warble, the top closing as
 * it gets loud, dropouts), VHS (band-limited, a slow deep wobble with jitter, the head-switch tick, tracking events:
 * a dip in pitch, a duck and a noise burst) */
static void media_block(int sy, int32_t *x)
{
    const synth_t *p = &syn[sy];
    int i, k0 = p->mk[0], k1 = p->mk[1], k2 = p->mk[2], k3 = p->mk[3], t = p->media;
    int32_t tone = T_OP[64 + k1 / 2];
    fx[sy].wd += ((int32_t)(xs32(&fx[sy].rng) >> 22) - 512 - fx[sy].wd) >> 6;   /* the wow's rate wanders */
    if (t == FM_CASS && !fx[sy].drop && (xs32(&fx[sy].rng) >> 20) < (uint32_t)k3 / 3u)   /* a dropout begins */
        { fx[sy].drop = 450 + (int32_t)(fx[sy].rng % 6000u); fx[sy].drop_g = 6000 + (int32_t)(fx[sy].rng % 14000u); }
    if (t == FM_VHS && !fx[sy].track && (xs32(&fx[sy].rng) >> 20) < (uint32_t)k3 / 4u)  /* a tracking event */
        fx[sy].track = 900 + (int32_t)(fx[sy].rng % 8000u);
    for (i = 0; i < DMK_SUB; i++) {
        int32_t m, d, pos, f, y, a, c;
        uint32_t j, rate = t == FM_TAPE ? p->twow : t == FM_CASS ? p->twow * 3u + (uint32_t)k2 * 5000u : p->twow / 2u + (uint32_t)k2 * 1500u;
        tp_buf[sy][fx[sy].tw & (TP_LEN - 1)] = (int16_t)sat16(x[i]);
        fx[sy].wow += rate + (uint32_t)(fx[sy].wd * 30);
        fx[sy].flut += p->tflut;
        m = (sinp(fx[sy].wow) * 48 + sinp(fx[sy].flut)) >> 6;
        if (t == FM_VHS) m += (int32_t)(xs32(&fx[sy].rng) >> 21) - 1024;   /* the jitter */
        d = (t == FM_CASS ? 300 : 400) * 256 + (int32_t)(((int64_t)m * (t == FM_TAPE ? p->tdepth : 200 + k0 * (t == FM_VHS ? 780 : 500))) >> 15);
        if (fx[sy].track) d += 30000 - (fx[sy].track & 4095) * 4;   /* tracking: the pitch dips */
        pos = (int32_t)(fx[sy].tw << 8) - d;
        j = (uint32_t)(pos >> 8); f = pos & 255;
        y = (tp_buf[sy][j & (TP_LEN - 1)] * (256 - f) + tp_buf[sy][(j + 1) & (TP_LEN - 1)] * f) >> 8;
        fx[sy].tw++;
        if (t == FM_TAPE) {
            fx[sy].hl += ((y - fx[sy].hl) * p->thl) >> 15;
            y = curve(T_SAT, (fx[sy].hl * p->tdrive) >> 8);
        } else if (t == FM_CASS) {
            a = y < 0 ? -y : y;
            fx[sy].fol += (a - fx[sy].fol) >> 8;
            c = tone - (fx[sy].fol >> 2);               /* loud: the top closes */
            if (fx[sy].drop) { fx[sy].drop--; c -= 6000; y = (y * fx[sy].drop_g) >> 15; }
            if (c < 1500) c = 1500;
            fx[sy].lp1 += ((y - fx[sy].lp1) * c) >> 15;
            fx[sy].lp2 += ((fx[sy].lp1 - fx[sy].lp2) * c) >> 15;
            y = curve(T_SAT, fx[sy].lp2 * 3 / 2);
        } else {
            fx[sy].hp += (y * 256 - fx[sy].hp) >> 9;    /* ~80 Hz out */
            y -= fx[sy].hp >> 8;
            fx[sy].lp1 += ((y - fx[sy].lp1) * tone) >> 15;
            fx[sy].lp2 += ((fx[sy].lp1 - fx[sy].lp2) * tone) >> 15;
            y = fx[sy].lp2;
            a = y < 0 ? -y : y;                         /* the machine's noises ride the sound, as the tape's hiss */
            if (a > fx[sy].fol) fx[sy].fol += (a - fx[sy].fol) >> 4; else fx[sy].fol -= (fx[sy].fol >> 13) + (fx[sy].fol > 0);
            c = fx[sy].fol > 4096 ? 32768 : fx[sy].fol * 8;
            if (++fx[sy].tick >= 1471u) { fx[sy].tick = 0; y += (k3 * 12 * c) >> 15; }   /* the head switch, 29.97 Hz */
            if (fx[sy].track) {
                fx[sy].track--;
                y = (y * 12000 >> 15) + (((((int32_t)xs32(&fx[sy].rng) >> 19) * k3 >> 6) * c) >> 15);
            }
            y = curve(T_SAT, y * 5 / 4);
        }
        x[i] = y;
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
static struct { uint16_t pos[FD_N], dpos[4]; int32_t lp[FD_N], m[FD_N], fast, slow, hold, acc, ol, orr; uint32_t lfo[2]; uint8_t half, step, vhalf; } fd;
static const uint16_t FD_OFF[FD_N] = {0, 1153, 2480, 4039, 5738, 7615, 9684, 11981};
static const uint16_t DF_OFF[4] = {0, 142, 249, 628};
#define EL_LEN 32768u                                   /* ECHO: 1.49 s of 8-bit mu-law at 22.05 kHz */
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
    int sign = 0, exp = 7, mask, man;
    if (x < 0) { sign = 0x80; x = -x; }
    if (x > 32635) x = 32635;
    x += 0x84;
    for (mask = 0x4000; (x & mask) == 0 && exp > 0; exp--, mask >>= 1) {}
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
    int k, h, j, nmod = gp.vtype == VB_HAZE ? 8 : gp.vtype == VB_LOFI ? 2 : 4;
    int32_t x = vin / 2, y[FD_N], ol, orr, damp = gp.vtype == VB_HAZE ? gp.vdamp * 3 / 4 : gp.vtype == VB_LOFI ? gp.vdamp / 2 : gp.vdamp;
    if (!(fd.step++ & 3)) {                             /* the sweep, every fourth step: four sines, the lines take turns */
        int32_t sv[4];
        sv[0] = sinp(fd.lfo[0]); sv[1] = sinp(fd.lfo[0] + 0x40000000u); sv[2] = sinp(fd.lfo[1]); sv[3] = sinp(fd.lfo[1] + 0x40000000u);
        for (k = 0; k < nmod; k++) fd.m[k] = (gp.vmod * ((k & 4 ? -sv[k & 3] : sv[k & 3]) + 32768)) >> 16;
        fd.lfo[0] += gp.vrate * 4u;
        fd.lfo[1] += (gp.vrate + (gp.vrate >> 2)) * 4u;
    }
    if (gp.vtype == VB_BLOOM) {                         /* the swell: slow over fast, 0..1 */
        int32_t a = (x < 0 ? -x : x) << 8, g, sn;
        fd.fast += a > fd.fast ? (a - fd.fast) >> 4 : -(fd.fast >> 12) - (fd.fast > 0);
        fd.slow += a > fd.slow ? (a - fd.slow) >> 14 : -(fd.slow >> 16) - (fd.slow > 0);
        sn = fd.slow >> 8 > 32767 ? 32767 : fd.slow >> 8;
        g = (fd.fast >> 8) > 0 ? (sn << 15) / ((fd.fast >> 8) + 1) : 32768;
        x = (x * (g > 32768 ? 32768 : g)) >> 15;
    }
    if (gp.vtype == VB_LOFI) { if ((fd.half ^= 1)) fd.hold = x; x = fd.hold; }
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
        if (gp.vtype == VB_LOFI) w = (w + 128) & ~255;  /* eight bits */
        fd_buf[FD_OFF[k] + fd.pos[k]] = (int16_t)sat16(w);
        if (++fd.pos[k] >= FD_LEN[k]) fd.pos[k] = 0;
    }
    fd.ol = (ol * gp.vret) >> 16;
    fd.orr = (orr * gp.vret) >> 16;
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

/* one half-rate step: the verb's and the echo's inputs in, their stereo outputs kept in bus.vl/vr, el/er */
static void bus_step(int32_t vin, int32_t ein)
{
    int32_t l, r;
    if (gp.vtype == VB_ROOM) room_step(vin);
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

/* the screen reads these: what each synth sounds, now */
static int32_t syn_peak[DMK_SYNTHS];
static int32_t syn_dc[DMK_SYNTHS];                      /* each synth's DC blocker (a sample's loop can sit off zero) */

/* the lines and the state, silent: mu-law's silence is 0xFF, not 0 (0 is its loudest negative) */
static void dsp_reset(void)
{
    uint32_t i;
    for (i = 0; i < EL_LEN; i++) el_buf[i] = 0xFF;
    for (i = 0; i < sizeof cb_buf / sizeof cb_buf[0]; i++) cb_buf[i] = 0;
    for (i = 0; i < sizeof ap_buf / sizeof ap_buf[0]; i++) ap_buf[i] = 0;
    for (i = 0; i < sizeof fd_buf / sizeof fd_buf[0]; i++) fd_buf[i] = 0;
    for (i = 0; i < sizeof df_buf / sizeof df_buf[0]; i++) df_buf[i] = 0;
    for (i = 0; i < DMK_SYNTHS * TP_LEN; i++) tp_buf[i / TP_LEN][i % TP_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS * CH_LEN; i++) ch_buf[i / CH_LEN][i % CH_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) {
        uint8_t *z = (uint8_t *)&fx[i];
        uint32_t k;
        for (k = 0; k < sizeof fx[i]; k++) z[k] = 0;
        fx[i].rng = 0x9E3779B9u * (i + 1u);
        syn_peak[i] = 0; syn_dc[i] = 0;
    }
    { uint8_t *z = (uint8_t *)&fd; uint32_t k; for (k = 0; k < sizeof fd; k++) z[k] = 0; }
    vq_w = vq_r = wq_w = wq_r = 0;
    bus.vl = bus.vr = bus.el = bus.er = bus.elp = bus.ehp = bus.vacc = bus.eacc = 0;
    for (i = 0; i < 4; i++) bus.cb_lp[i] = 0;
}

/* ---- the render: the voices first, a whole call at once, half of them on the second core when there is one; then
 * sample by sample the synths' effects, level, pan, sends, the buses and the limiter */
#define VB_MAX 256                                      /* frames a pass */
static int32_t vbuf[2][DMK_SYNTHS][VB_MAX] POOL;
static int16_t vib_c[DMK_SYNTHS][VB_MAX / DMK_SUB];     /* each synth's vibrato a block, cents Q4: both cores read it */
static int v_frames;

/* the formants for VOWEL, as cutoff indexes (T_SVF): u, o, a, e, i; the cutoff glides through them */
static const uint8_t VOW[5][2] = {{112, 164}, {132, 160}, {156, 175}, {140, 201}, {107, 211}};

static void voices_run(int half)
{
    int v, i, s, b, nb = v_frames / DMK_SUB;
    for (s = 0; s < DMK_SYNTHS; s++) for (i = 0; i < v_frames; i++) vbuf[half][s][i] = 0;
    for (v = half * (DMK_VOICES / 2); v < (half + 1) * (DMK_VOICES / 2); v++) {
        voice_t *o = &vc[v];
        const synth_t *p = &syn[o->sy];
        for (b = 0; b < nb && o->stage; b++) {
            int32_t e0 = o->env, e1, de, f, q, hp, idx, f2 = 0, c = 0, *out = &vbuf[half][o->sy][b * DMK_SUB];
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
            if (o->blocks < 65535u) o->blocks++;
            o->inc = c ? (uint32_t)(((uint64_t)o->inc0 * T_CENT[clampi(50 + c / 16, 0, 100)]) >> 16) : o->inc0;
            idx = clampi(p->cut + (int32_t)(((int64_t)p->fenv * e0) >> 24), 0, 255);
            f = T_SVF[idx]; q = p->resq;
            if (p->ftype == FT_VOWEL) {                 /* the cutoff picks the vowel: two band-passes at its formants */
                int pos = idx * 4, a = pos >> 8, t = pos & 255, z = a < 4 ? a + 1 : 4;
                f = T_SVF[(VOW[a][0] * (256 - t) + VOW[z][0] * t) >> 8];
                f2 = T_SVF[(VOW[a][1] * (256 - t) + VOW[z][1] * t) >> 8];
            }
            for (i = 0; i < DMK_SUB; i++) {
                int32_t x, y;
                o->ph += o->inc;
                while (o->ph >= 65536u) { o->ph -= 65536u; o->a = o->b; o->b = smp_next(o); }
                x = o->a + (((o->b - o->a) * (int32_t)(o->ph >> 1)) >> 15);
                switch (p->ftype) {
                case FT_LP24: y = svf_step(&o->lp2, &o->bp2, svf_step(&o->lp, &o->bp, x, f, q, &hp), f, q, &hp); break;
                case FT_LP12: y = svf_step(&o->lp, &o->bp, x, f, q, &hp); break;
                case FT_HP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = hp; break;
                case FT_BP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = o->bp; break;
                case FT_NOTCH: y = svf_step(&o->lp, &o->bp, x, f, q, &hp); y += hp; break;
                case FT_PEAK: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = x + o->bp; break;
                case FT_VOWEL: {
                    int32_t h2;
                    int32_t qv = 1500 + q / 4;          /* the resonance narrows the formants */
                    svf_step(&o->lp, &o->bp, x, f, qv, &hp);
                    svf_step(&o->lp2, &o->bp2, x, f2, qv, &h2);
                    y = (o->bp + (o->bp2 * 3) / 4) * 3 / 2;
                    break;
                }
                default: y = x; break;
                }
                if ((p->ftype == FT_LP24 || p->ftype == FT_LP12) && idx >= 255) y = x;   /* open: the sample as it is */
                out[i] += (int32_t)(((int64_t)clampi(y, -65536, 65536) * ((e0 + de * i) >> 9)) >> 15);
                if (!o->stage) break;                   /* a one-shot ended */
            }
            if (o->stage) o->env = e1;
        }
    }
}
static void voices_job(void) { voices_run(1); }

static void dsp_render(int16_t *lr, int frames)
{
    int b, i, s, n, at;
    for (at = 0; at < frames; at += n) {
        int nb, par;
        uint32_t vfill;
        n = frames - at > VB_MAX ? VB_MAX : frames - at;
        v_frames = n;
        nb = n / DMK_SUB;
        for (s = 0; s < DMK_SYNTHS; s++)                /* the vibrato's LFO, a block at a time, before the split */
            for (b = 0; b < nb; b++) {
                fx[s].vib += 3100000u + (uint32_t)syn[s].ok[2] * 74000u;   /* 2..8 Hz, a block at a time */
                vib_c[s][b] = (int16_t)(syn[s].motion == FO_VIB ? sinp(fx[s].vib) * syn[s].ok[0] * 5 / 32768 : 0);   /* to ~40 cents */
            }
        par = dmk_par_begin && dmk_par_begin(voices_job);
        voices_run(0);
        if (par) dmk_par_end(); else voices_run(1);
        /* the network: the last run's input, on the second core while this run mixes; the mix takes what it made */
        if (gp.vtype == VB_ROOM) { vq_r = vq_w; wq_r = wq_w; }   /* the room runs in the mix: the queue stays empty */
        vfill = wq_w - wq_r;
        par = gp.vtype != VB_ROOM && dmk_par_begin && dmk_par_begin(verb_job);
        if (gp.vtype != VB_ROOM && !par) verb_job();
        for (b = 0; b < nb; b++) {
            int32_t sm[DMK_SYNTHS][DMK_SUB], L[DMK_SUB], R[DMK_SUB], vin[DMK_SUB], ein[DMK_SUB];
            for (s = 0; s < DMK_SYNTHS; s++) for (i = 0; i < DMK_SUB; i++) sm[s][i] = vbuf[0][s][b * DMK_SUB + i] + vbuf[1][s][b * DMK_SUB + i];
            for (i = 0; i < DMK_SUB; i++) L[i] = R[i] = vin[i] = ein[i] = 0;
            for (s = 0; s < DMK_SYNTHS; s++) {
                const synth_t *p = &syn[s];
                int32_t pk = 0, sl[DMK_SUB], sr[DMK_SUB];
                for (i = 0; i < DMK_SUB; i++) {         /* two voices at full scale before the limiter's knee; no DC */
                    int32_t x = sm[s][i] >> 1;
                    syn_dc[s] += (x * 4096 - syn_dc[s]) / 512;   /* ~14 Hz, Q12: its stall under a 512th is under */
                    sm[s][i] = x - ((syn_dc[s] + 2048) >> 12);   /* an eighth of a step, rounded away */
                }
                if (p->colour) colour_block(s, sm[s]);
                if (p->media) media_block(s, sm[s]);
                if (p->motion == FO_CHORUS) chorus_block(s, sm[s], sl, sr);
                else for (i = 0; i < DMK_SUB; i++) sl[i] = sr[i] = sm[s][i];
                for (i = 0; i < DMK_SUB; i++) {
                    int32_t xl = (sl[i] * p->level) >> 15, xr = (sr[i] * p->level) >> 15, x = (xl + xr) / 2;
                    L[i] += (xl * p->gl) >> 15;
                    R[i] += (xr * p->gr) >> 15;
                    vin[i] += (x * p->vsend) >> 15;
                    ein[i] += (x * p->esend) >> 15;
                    if (x > pk) pk = x; else if (-x > pk) pk = -x;
                }
                syn_peak[s] = pk > syn_peak[s] ? pk : syn_peak[s] - (syn_peak[s] >> 6);
            }
            /* the buses at half rate, held for the sample between; then the limiter */
            for (i = 0; i < DMK_SUB; i++) {
                int k = (at + b * DMK_SUB + i) * 2;
                int32_t wl = bus.vl, wr = bus.vr;
                if (!(bus.half ^= 1)) bus_step((bus.vacc + vin[i]) >> 1, (bus.eacc + ein[i]) >> 1);
                else { bus.vacc = vin[i]; bus.eacc = ein[i]; }
                if (gp.vtype != VB_ROOM) {              /* the network's: what it made of the last run, in order */
                    vq[vq_w++ & (VQ_LEN - 1)] = vin[i];
                    wl = wr = 0;
                    if (vfill) { wl = wq[0][wq_r & (VQ_LEN - 1)]; wr = wq[1][wq_r & (VQ_LEN - 1)]; wq_r++; vfill--; }
                } else { wl = bus.vl; wr = bus.vr; }
                lr[k] = (int16_t)curve(T_LIM, L[i] + wl + bus.el);
                lr[k + 1] = (int16_t)curve(T_LIM, R[i] + wr + bus.er);
            }
#ifdef DMK_BENCH
            bench_block(DMK_SUB);
#endif
        }
        if (par) dmk_par_end();
    }
}

/* the keys play the current synth (dmk_ui.c sets it) */
static int cur_synth;
static void note_on(int n) { note_on_s(cur_synth, n); }
static void note_off(int n) { note_off_s(cur_synth, n); }
