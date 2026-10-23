/* SPDX-License-Identifier: GPL-3.0-only */
/* Phase 0's sound: a sine a key from a pool of 12 voices with an attack and a release, enough to hear the FM-1's keys
 * and to load the render for the meter. The engines (docs/plan.md, 2.1) replace it in phase 1. Included by
 * dmk_core.c. */

typedef struct { uint32_t ph, inc, age; int32_t env; uint8_t note, on; } voice_t;
static voice_t vc[DMK_VOICES];
static uint32_t vc_clock;

static inline int32_t sinp(uint32_t ph)                 /* Q15, linear between the table's points */
{
    uint32_t i = ph >> 22, f = (ph >> 6) & 0xFFFFu;
    int32_t a = T_SIN[i], b = T_SIN[i + 1];
    return a + (((b - a) * (int32_t)f) >> 16);          /* |b - a| <= 202: no overflow */
}

static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

static void note_on(int n)
{
    int i, best = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    if (n < 0 || n > 127) return;
    for (i = 0; i < DMK_VOICES; i++)                    /* a silent voice, else the oldest released, else the oldest */
        if (!vc[i].on && vc[i].env == 0) { best = i; oldest = 0; break; }
    if (oldest)
        for (i = 0; i < DMK_VOICES; i++) {
            uint32_t a = vc[i].age + (vc[i].on ? 0x80000000u : 0u);
            if (a < oldest) { oldest = a; best = i; }
        }
    vc[best].note = (uint8_t)n; vc[best].inc = T_INC[n]; vc[best].on = 1; vc[best].age = vc_clock++;
}

static void note_off(int n)
{
    int i;
    for (i = 0; i < DMK_VOICES; i++) if (vc[i].on && vc[i].note == n) vc[i].on = 0;
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

static void dsp_render(int16_t *lr, int frames)
{
    int b, i, v;
    for (b = 0; b < frames; b += DMK_SUB) {
        int32_t mix[DMK_SUB];
        for (i = 0; i < DMK_SUB; i++) mix[i] = 0;
        for (v = 0; v < DMK_VOICES; v++) {
            voice_t *o = &vc[v];
            int32_t e0 = o->env, e1, de;
            if (!o->on && e0 == 0) continue;
            e1 = o->on ? e0 + (((1 << 24) - e0) >> 4) : e0 - (e0 >> 7) - 1;   /* ~6 ms up, ~60 ms down */
            if (e1 < 0) e1 = 0;
            de = (e1 - e0) / DMK_SUB;
            for (i = 0; i < DMK_SUB; i++) {
                mix[i] += (sinp(o->ph) * ((e0 + de * i) >> 9)) >> 15;         /* Q15 x Q15 */
                o->ph += o->inc;
            }
            o->env = e1;
        }
        for (i = 0; i < DMK_SUB; i++) {
            int32_t s = sat16(mix[i] / 4);              /* four voices at full scale before the clip */
            lr[(b + i) * 2] = (int16_t)s;
            lr[(b + i) * 2 + 1] = (int16_t)s;
        }
#ifdef DMK_BENCH
        bench_block(DMK_SUB);
#endif
    }
}
