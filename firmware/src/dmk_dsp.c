/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's sound, phase 1 (docs/plan.md, 2.1, 1.2, 1.3): the owner's recordings are the core. The SAMPLE engine reads
 * them as 4-bit block ADPCM straight from flash, pitched by a Q16 phase with linear interpolation, looped where
 * tools/gen_samples.py found the seam; each voice has a state-variable filter and an envelope. Each synth has its TAPE
 * (wow, flutter, head loss, saturation; after MONO2K's bus tape). Two shared buses: ROOM (MONO2K's SPU-flavoured
 * combs and all-passes, at half rate) and ECHO (an 8-bit mu-law line at half rate, its loop filtered and clipped).
 * Then a soft limiter. 12 voices shared by the three synths. Integer only. Included by dmk_core.c. */

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
enum { FT_LP24, FT_LP12, FT_BP, FT_HP, FT_OFF };
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
} synth_t;
static synth_t syn[DMK_SYNTHS];

/* the shared buses' controls */
static struct { int32_t vret, vfb, vdamp, eret, efb, elp, etime; } gp;

/* ---- the voices */
typedef struct {
    const dmk_sample_t *s;
    uint32_t ph, inc, age;
    int32_t a, b, s1, s2, env, lp, bp, lp2, bp2;
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
    o->inc = (uint32_t)(((uint64_t)inc * T_CENT[clampi(50 - s->cents + p->fine, 0, 100)]) >> 16);
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

/* ---- each synth's tape: a short line read back by wow and flutter, head loss, saturation */
#define TP_LEN 2048u
static int16_t tp_buf[DMK_SYNTHS][TP_LEN] POOL;
static struct { uint32_t tw, wow, flut; int32_t hl; } tp[DMK_SYNTHS];

static void tape_block(int sy, int32_t *x)
{
    const synth_t *p = &syn[sy];
    int i;
    for (i = 0; i < DMK_SUB; i++) {
        int32_t m, d, pos, f, t;
        uint32_t j;
        tp_buf[sy][tp[sy].tw & (TP_LEN - 1)] = (int16_t)sat16(x[i]);
        tp[sy].wow += p->twow;
        tp[sy].flut += p->tflut;
        m = (sinp(tp[sy].wow) * 48 + sinp(tp[sy].flut)) >> 6;          /* the flutter a quarter of the wow in pitch */
        d = (400 << 8) + (int32_t)(((int64_t)m * p->tdepth) >> 15);   /* Q8 samples behind */
        pos = (int32_t)(tp[sy].tw << 8) - d;
        j = (uint32_t)(pos >> 8); f = pos & 255;
        t = (tp_buf[sy][j & (TP_LEN - 1)] * (256 - f) + tp_buf[sy][(j + 1) & (TP_LEN - 1)] * f) >> 8;
        tp[sy].tw++;
        tp[sy].hl += ((t - tp[sy].hl) * p->thl) >> 15;
        x[i] = curve(T_SAT, (tp[sy].hl * p->tdrive) >> 8);
    }
}

/* ---- the buses, at half rate */
static const uint16_t CB_LEN[4] = {557, 593, 641, 677};   /* ROOM: four combs, four all-passes (MONO2K's lengths) */
static const uint16_t AP_LEN[4] = {225, 341, 248, 367};
static int16_t cb_buf[557 + 593 + 641 + 677] POOL;
static int16_t ap_buf[225 + 341 + 248 + 367] POOL;
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

/* one half-rate step: the verb's and the echo's inputs in, their stereo outputs kept in bus.vl/vr, el/er */
static void bus_step(int32_t vin, int32_t ein)
{
    int c, a;
    int32_t sum = 0, off = 0, ch[2], x, y, l, r;
    /* ROOM */
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
    for (i = 0; i < DMK_SYNTHS * TP_LEN; i++) tp_buf[i / TP_LEN][i % TP_LEN] = 0;
    for (i = 0; i < DMK_SYNTHS; i++) { syn_peak[i] = 0; syn_dc[i] = 0; tp[i].hl = 0; }
    bus.vl = bus.vr = bus.el = bus.er = bus.elp = bus.ehp = bus.vacc = bus.eacc = 0;
    for (i = 0; i < 4; i++) bus.cb_lp[i] = 0;
}

static void dsp_render(int16_t *lr, int frames)
{
    int b, i, v, s;
    for (b = 0; b < frames; b += DMK_SUB) {
        int32_t sm[DMK_SYNTHS][DMK_SUB], L[DMK_SUB], R[DMK_SUB], vin[DMK_SUB], ein[DMK_SUB];
        for (s = 0; s < DMK_SYNTHS; s++) for (i = 0; i < DMK_SUB; i++) sm[s][i] = 0;
        /* the voices: the sampler, the filter, the envelope */
        for (v = 0; v < DMK_VOICES; v++) {
            voice_t *o = &vc[v];
            const synth_t *p = &syn[o->sy];
            int32_t e0 = o->env, e1, de, f, q, hp, idx;
            if (!o->stage) continue;
            if (o->stage == 1) { e1 = e0 + (int32_t)p->atk; if (e1 >= FULL) { e1 = FULL; o->stage = 2; } }
            else if (o->stage == 2) e1 = p->sus + (int32_t)(((int64_t)(e0 - p->sus) * p->dec) >> 16);
            else { e1 = (int32_t)(((int64_t)e0 * p->rel) >> 16); if (e1 < 256) { e1 = 0; o->stage = 0; } }
            de = (e1 - e0) / DMK_SUB;
            idx = clampi(p->cut + (int32_t)(((int64_t)p->fenv * e0) >> 24), 0, 255);
            f = T_SVF[idx]; q = p->resq;
            for (i = 0; i < DMK_SUB; i++) {
                int32_t x, y;
                o->ph += o->inc;
                while (o->ph >= 65536u) { o->ph -= 65536u; o->a = o->b; o->b = smp_next(o); }
                x = o->a + (((o->b - o->a) * (int32_t)(o->ph >> 1)) >> 15);
                switch (p->ftype) {
                case FT_LP24: y = svf_step(&o->lp2, &o->bp2, svf_step(&o->lp, &o->bp, x, f, q, &hp), f, q, &hp); break;
                case FT_LP12: y = svf_step(&o->lp, &o->bp, x, f, q, &hp); break;
                case FT_BP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = o->bp; break;
                case FT_HP: svf_step(&o->lp, &o->bp, x, f, q, &hp); y = hp; break;
                default: y = x; break;
                }
                if ((p->ftype == FT_LP24 || p->ftype == FT_LP12) && idx >= 255) y = x;   /* open: the sample as it is */
                sm[o->sy][i] += (int32_t)(((int64_t)clampi(y, -65536, 65536) * ((e0 + de * i) >> 9)) >> 15);
                if (!o->stage) break;                   /* a one-shot ended */
            }
            if (o->stage) o->env = e1;
        }
        /* the synths: tape, level, pan, the sends */
        for (i = 0; i < DMK_SUB; i++) L[i] = R[i] = vin[i] = ein[i] = 0;
        for (s = 0; s < DMK_SYNTHS; s++) {
            const synth_t *p = &syn[s];
            int32_t pk = 0;
            for (i = 0; i < DMK_SUB; i++) {             /* two voices at full scale before the limiter's knee; no DC */
                int32_t x = sm[s][i] >> 1;
                syn_dc[s] += (x * 4096 - syn_dc[s]) / 512;   /* ~14 Hz, Q12: its stall under a 512th is under */
                sm[s][i] = x - ((syn_dc[s] + 2048) >> 12);       /* an eighth of a step, rounded away */
            }
            if (p->tape) tape_block(s, sm[s]);
            for (i = 0; i < DMK_SUB; i++) {
                int32_t x = (sm[s][i] * p->level) >> 15;
                L[i] += (x * p->gl) >> 15;
                R[i] += (x * p->gr) >> 15;
                vin[i] += (x * p->vsend) >> 15;
                ein[i] += (x * p->esend) >> 15;
                if (x > pk) pk = x; else if (-x > pk) pk = -x;
            }
            syn_peak[s] = pk > syn_peak[s] ? pk : syn_peak[s] - (syn_peak[s] >> 6);
        }
        /* the buses at half rate, held for the sample between; then the limiter */
        for (i = 0; i < DMK_SUB; i++) {
            if (!(bus.half ^= 1)) bus_step((bus.vacc + vin[i]) >> 1, (bus.eacc + ein[i]) >> 1);
            else { bus.vacc = vin[i]; bus.eacc = ein[i]; }
            lr[(b + i) * 2] = (int16_t)curve(T_LIM, L[i] + bus.vl + bus.el);
            lr[(b + i) * 2 + 1] = (int16_t)curve(T_LIM, R[i] + bus.vr + bus.er);
        }
#ifdef DMK_BENCH
        bench_block(DMK_SUB);
#endif
    }
}

/* the keys play the current synth (dmk_ui.c sets it) */
static int cur_synth;
static void note_on(int n) { note_on_s(cur_synth, n); }
static void note_off(int n) { note_off_s(cur_synth, n); }
