/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the YM2612 as a synth's sound (the owner's Phase 4, 2026-11-18: one FM synth at a time, six
 * notes; ymfm, the owner's blind pick against Nuked-OPN2 in all three pairs, and a sixth of its cost: Nuked's chip
 * beside a jam went past the render's 2 ms). Included by the core's unity build (firmware/src/dmk_core.c) under
 * DMK_MOVE only; the FM-1's firmware has none of it.
 *
 * One chip (move/dmk_ymfm.cpp over move/vendor/ymfm, Aaron Giles's, BSD-3-Clause, unchanged), its six channels a note
 * each, read apart, so each note goes through its voice's filter, envelope and velocity as a recording would
 * (dmk_dsp.c's voices_run), then through the synth's own chain.
 * The chip runs once a render, before the voices, at its own 53,267 Hz, read at 44,100 between two of its samples
 * (or the latest held: RAW, its aliasing kept). With no note of it sounding for a few runs it sleeps: a song without
 * it pays nothing.
 *
 * Its registers are written as a real chip takes them: an address, four clocks, its data, forty-eight (its busy time
 * and more), counted in its clocks, in order; a note's pitch only when it has moved, the newest value (a glide or a vibrato
 * never queues up behind itself). A note: its key off, its pitch, its key on, so a channel taken again strikes again. */
#include <string.h>
void ym_chip_reset(void);                              /* move/dmk_ymfm.cpp */
void ym_chip_write(int port, int data);
void ym_chip_channels(int32_t *o);                      /* each channel's sample, at the chip's rate */
void ym_chip_grit(int on);

#define YM_CLOCK     7670453u           /* the chip's clock (an NTSC console's): a chip sample is 144 of it, 53,267 Hz */
#define YM_CH        6
#define YM_CYC       24                 /* the chip's clocks a sample: the writes' timing is counted in them */
#define YM_WR_ADDR   4                  /* clocks from a register's address to its data */
#define YM_WR_DATA   48                 /* clocks from its data to the next address: the chip's 32 busy, and more */
#define YM_STEP      79159u             /* chip samples an output sample, Q16: 53,267.03 / 44,100 */
#define YM_GAIN      40                 /* a channel's window (three clocks of nine bits) to a recording's scale */
#define YM_QN        512u               /* register writes waiting at most (a whole patch is 183, a chord 18) */
#define YM_KEYS_ROOM 64u                /* of them, kept for the notes: a sound's registers never crowd a key on or off out */
#define YM_IDLE      8                  /* runs the chip goes on after its last note has ended, then sleeps */
#define YM_KEY       0x28               /* the key on / off register (part 0) */
#define YM_KEY_ALL   0xF0               /* its four operators on */
#define YM_FNUM_MAX  2047               /* an F-number's 11 bits */
#define YM_BLOCKS    8                  /* its octaves (3 bits) */
#define YM_BLOCK_OF(n) ((n) / 12 - 1)   /* a MIDI note's block, with YM_FN's F-numbers (C4, note 60, in block 4) */

/* each semitone's F-number from C, in a note's block (YM_BLOCK_OF): 440 Hz, A, is 1083 in block 4 */
static const uint16_t YM_FN[12] = {644, 682, 723, 766, 811, 859, 910, 965, 1022, 1083, 1147, 1215};
static const uint8_t YM_OPR[4] = {0, 8, 4, 12};         /* operators 1 to 4 at their registers' offsets */

/* an operator: multiple, detune, level (attenuation, 0 loudest), the four rates and the sustain level, key scaling */
typedef struct { uint8_t mul, dt, tl, ar, d1r, d2r, sl, rr, ks; } ym_op_t;
typedef struct { const char *name; uint8_t alg, fbk; ym_op_t op[4]; } ym_patch_t;   /* (fbk: the feedback; fb is the screen's) */

/* the factory patches, the Sound list's after the recordings (the owner's brief: melancholic plucks, lo-fi pads,
 * basses, bells; four each, heard as WAVs before any is kept: move/test/ym_patches.c). Operators 1 to 4: multiple
 * (0 a half), detune (4-7 down), level (attenuation: 0 loudest, 0.75 dB a step), attack, first decay, second decay,
 * sustain level, release, key scaling. Algorithms: 0 four in a line; 1 two into a line; 2 one and a pair into 4;
 * 3 a pair and one into 4; 4 two pairs; 5 one into three; 6 a pair and two alone; 7 four alone */
static const ym_patch_t YM_PATCH[YM_PATCHES] = {
    /* plucks */
    {"Tine", 4, 5, {{1, 0, 30, 31, 10, 4, 2, 7, 0}, {2, 0, 0, 31, 6, 3, 4, 6, 0}, {1, 3, 36, 31, 12, 4, 2, 7, 0},
                    {1, 0, 4, 31, 7, 3, 4, 6, 0}}},
    {"Harp", 4, 2, {{1, 0, 30, 31, 14, 0, 15, 7, 1}, {1, 0, 2, 31, 9, 0, 15, 7, 1}, {3, 3, 40, 31, 16, 0, 15, 7, 1},
                    {1, 7, 8, 31, 9, 0, 15, 7, 1}}},
    {"Koto", 4, 4, {{3, 0, 32, 31, 18, 0, 15, 8, 2}, {1, 0, 3, 31, 11, 2, 12, 8, 1}, {1, 0, 35, 31, 20, 0, 15, 8, 2},
                    {2, 3, 12, 31, 13, 2, 12, 8, 1}}},
    {"Glass pluck", 4, 3, {{5, 0, 40, 31, 15, 0, 15, 8, 1}, {1, 0, 0, 31, 10, 3, 10, 6, 1}, {7, 3, 50, 31, 18, 0, 15, 8, 1},
                           {2, 7, 10, 31, 12, 3, 11, 6, 1}}},
    /* pads */
    {"Haze", 4, 2, {{1, 0, 40, 14, 2, 0, 2, 4, 0}, {1, 3, 9, 12, 1, 0, 1, 4, 0}, {2, 0, 45, 14, 2, 0, 2, 4, 0},
                    {1, 7, 11, 12, 1, 0, 1, 4, 0}}},
    {"Tape choir", 5, 3, {{1, 0, 38, 12, 3, 0, 3, 4, 0}, {1, 2, 9, 11, 1, 0, 2, 4, 0}, {2, 6, 15, 11, 1, 0, 2, 4, 0},
                          {1, 5, 9, 11, 1, 0, 2, 4, 0}}},
    {"Drift organ", 7, 4, {{1, 0, 11, 13, 1, 0, 1, 4, 0}, {2, 3, 15, 13, 1, 0, 1, 4, 0}, {4, 0, 23, 13, 1, 0, 1, 4, 0},
                           {1, 7, 11, 13, 1, 0, 1, 4, 0}}},
    {"Dust strings", 1, 5, {{1, 3, 35, 12, 2, 0, 2, 5, 0}, {1, 7, 38, 11, 2, 0, 2, 5, 0}, {1, 0, 30, 12, 2, 0, 2, 5, 0},
                            {1, 0, 4, 11, 1, 0, 1, 5, 0}}},
    /* basses */
    {"Grit bass", 0, 6, {{0, 0, 28, 31, 14, 5, 5, 8, 1}, {1, 0, 30, 31, 12, 4, 5, 8, 1}, {1, 3, 25, 31, 9, 3, 4, 8, 1},
                         {1, 0, 0, 31, 8, 2, 3, 8, 1}}},
    {"Sub", 4, 0, {{1, 0, 40, 31, 14, 0, 15, 8, 1}, {1, 0, 5, 31, 4, 1, 3, 8, 1}, {1, 0, 127, 31, 0, 0, 15, 15, 0},
                   {0, 0, 9, 31, 4, 1, 3, 8, 1}}},
    {"Slap", 3, 7, {{1, 0, 25, 31, 18, 0, 15, 10, 2}, {3, 0, 35, 31, 20, 0, 15, 10, 2}, {1, 0, 30, 31, 15, 0, 15, 10, 2},
                    {1, 0, 2, 31, 8, 3, 6, 9, 1}}},
    {"Dub", 4, 3, {{1, 0, 34, 31, 8, 2, 6, 7, 1}, {1, 0, 3, 31, 5, 2, 4, 7, 1}, {2, 0, 50, 31, 10, 0, 15, 7, 1},
                   {0, 0, 8, 31, 5, 2, 4, 7, 1}}},
    /* bells */
    {"Bell", 4, 3, {{7, 0, 35, 31, 9, 4, 8, 4, 0}, {2, 0, 3, 31, 5, 2, 10, 4, 0}, {14, 3, 40, 31, 10, 5, 9, 4, 0},
                    {1, 0, 5, 31, 4, 2, 10, 3, 0}}},
    {"Music box", 4, 2, {{7, 0, 42, 31, 12, 0, 15, 5, 1}, {4, 0, 0, 31, 8, 4, 15, 5, 1}, {11, 3, 48, 31, 14, 0, 15, 5, 1},
                         {2, 0, 4, 31, 7, 4, 15, 5, 1}}},
    {"Glass", 6, 0, {{14, 0, 45, 31, 9, 0, 15, 4, 0}, {1, 0, 2, 31, 6, 3, 12, 4, 0}, {3, 3, 14, 31, 7, 3, 13, 4, 0},
                     {5, 7, 18, 31, 8, 3, 14, 4, 0}}},
    {"Chime", 4, 1, {{9, 0, 38, 31, 10, 2, 12, 4, 0}, {1, 0, 1, 31, 4, 2, 9, 3, 0}, {6, 3, 44, 31, 11, 2, 12, 4, 0},
                     {3, 0, 9, 31, 5, 2, 10, 3, 0}}},
};

/* the FM values a sound keeps (ymv[s], dmk_dsp.c's: in each pattern's sound and in the song, format 20): the factory
 * patch it came from; that patch as edited (algorithm, feedback, each operator's own values, the chip's); the Main
 * row's macros over it, each at its middle doing nothing; the Chip row */
enum { YO_MUL, YO_DT, YO_TL, YO_AR, YO_D1R, YO_D2R, YO_SL, YO_RR, YO_KS, YO_N };
enum {
    YV_FROM,                            /* the factory patch the rest came from + 1 (0 none: picked, it loads) */
    YV_ALG, YV_FB,
    YV_OP,                              /* operators 1 to 4, YO_N each */
    YV_BRIGHT = YV_OP + 4 * YO_N,       /* the modulators' levels, louder (brighter) or softer: YM_BRIGHT_MID none */
    YV_RATIO,                           /* the modulators' multiples, up or down: YM_RATIO_MID none */
    YV_DETUNE,                          /* the carriers spread apart, alternately up and down: 0 the patch's own */
    YV_DECAY,                           /* every operator's decays, longer (up) or shorter: YM_DECAY_MID none */
    YV_RELEASE,                         /* every operator's release, longer or shorter: YM_REL_MID none */
    YV_GRIT, YV_RAW,                    /* the Chip row: the YM2612's own output; the chip's samples held (RAW) */
    YV_LFO, YV_VIB, YV_TREM,            /*   the chip's LFO (0 off, its eight rates), how much pitch and level it moves */
    YV_COUNT
};
_Static_assert(YV_COUNT == YV_N, "dmk_dsp.c's YV_N is the FM values' count");
#define YM_BRIGHT_MID 32                /* Bright 0..63 */
#define YM_RATIO_MID  7                 /* Ratio 0..14: -7..+7 */
#define YM_DETUNE_MAX 3
#define YM_DECAY_MID  16                /* Decay 0..31 */
#define YM_REL_MID    8                 /* Release 0..15 */
#define YM_LFO_RATES  8
#define YM_VIB_MAX    7                 /* the chip's PMS */
#define YM_TREM_MAX   3                 /* its AMS */
#define YM_TL_MAX 127
#define YM_MUL_MAX 15
#define YM_RATE_MAX 31
#define YM_RR_MAX 15
#define YM_DT_DOWN 4                    /* a detune's code down by d: YM_DT_DOWN + d */
#define YM_AM_ON 0x80                   /* an operator's first decay register: the LFO's level reaches it */
#define YM_REG_NONE 0xFFFFu             /* a register not yet written */

/* which operators sound, by algorithm, a bit each (operator 1 the lowest): the rest modulate */
static const uint8_t YM_CARRIERS[8] = {0x8, 0x8, 0x8, 0x8, 0xA, 0xE, 0xE, 0xF};

static struct {
    uint8_t q[YM_QN][3];                /* register writes waiting: part, register, value */
    uint32_t qr, qw;
    uint8_t sent, wait;                 /* the write under way: its address sent (1); clocks still to wait */
    uint16_t fn[YM_CH];                 /* each channel's pitch: its block << 11 | its F-number */
    uint8_t dirty[YM_CH];               /*   moved since it was last written (a byte each: two cores' voices write them) */
    int8_t hold[YM_CH];                 /* the voice that took each channel (-1 none) */
    uint32_t age[YM_CH];                /* when each channel last took a note: the free one longest free goes first */
    uint32_t clock;
    uint32_t ph;                        /* where between its two last samples the output is, Q16 */
    int32_t a[YM_CH], b[YM_CH];         /* each channel's two last samples */
    uint16_t reg[2][256];               /* what each register was last given (YM_REG_NONE: not yet), part 0 and 1 */
    uint8_t raw, idle;                  /* RAW: the latest sample held; runs since a note of it last sounded */
    uint8_t retry;                      /* a register the full queue refused: ym_sync again at the next run */
    uint8_t grit;                       /* the YM2612's own output (its DAC's step at zero), else the YM3438's, clean */
    uint32_t made;                      /* chip samples made (a test sees it asleep) */
} ym;
static int32_t ym_buf[YM_CH][VB_MAX];   /* each channel this run, at the voices' rate */

static int ym_put_in(int part, int reg, int val, uint32_t room)
{
    if (ym.qw - ym.qr >= room) return 0;                /* (full: a patch and a chord's worth already wait) */
    uint8_t *w = ym.q[ym.qw % YM_QN];
    w[0] = (uint8_t)part; w[1] = (uint8_t)reg; w[2] = (uint8_t)val;
    ym.qw++;
    return 1;
}
static int ym_put(int part, int reg, int val) { return ym_put_in(part, reg, val, YM_QN); }
/* a register to a value, queued only if it is not that already (a refused one tried again at the next sync) */
static void ym_set(int part, int reg, int val)
{
    if (ym.reg[part][reg] == val) return;
    if (ym_put_in(part, reg, val, YM_QN - YM_KEYS_ROOM)) ym.reg[part][reg] = (uint16_t)val;
    else { ym.reg[part][reg] = YM_REG_NONE; ym.retry = 1; }
}

static void ym_reset(void)
{
    memset(&ym, 0, sizeof ym);
    memset(ym_buf, 0, sizeof ym_buf);
    for (int k = 0; k < YM_CH; k++) ym.hold[k] = -1;
    for (int p = 0; p < 2; p++) for (int r = 0; r < 256; r++) ym.reg[p][r] = YM_REG_NONE;
    ym_chip_reset();
    ym_chip_grit(0);
    ym_set(0, 0x27, 0x00);                              /* channel 3 as the others, the DAC off (the LFO: ym_sync's) */
    ym_set(0, 0x2B, 0x00);
}

/* factory patch pt as FM values (v: a sound's YV_N), the macros and the Chip row at rest (RAW on: the chip's aliasing
 * kept, as the owner heard the patches) */
static void ym_vals_of(uint8_t *v, int pt)
{
    const ym_patch_t *P = &YM_PATCH[pt];
    v[YV_FROM] = (uint8_t)(pt + 1);
    v[YV_ALG] = P->alg; v[YV_FB] = P->fbk;
    for (int o = 0; o < 4; o++) {
        const ym_op_t *O = &P->op[o];
        uint8_t *w = &v[YV_OP + o * YO_N];
        w[YO_MUL] = O->mul; w[YO_DT] = O->dt; w[YO_TL] = O->tl; w[YO_AR] = O->ar; w[YO_D1R] = O->d1r;
        w[YO_D2R] = O->d2r; w[YO_SL] = O->sl; w[YO_RR] = O->rr; w[YO_KS] = O->ks;
    }
    v[YV_BRIGHT] = YM_BRIGHT_MID; v[YV_RATIO] = YM_RATIO_MID; v[YV_DETUNE] = 0;
    v[YV_DECAY] = YM_DECAY_MID; v[YV_RELEASE] = YM_REL_MID;
    v[YV_GRIT] = 0; v[YV_RAW] = 1; v[YV_LFO] = 0; v[YV_VIB] = 0; v[YV_TREM] = 0;
}
static void ym_val_load(int s, int pt) { ym_vals_of(ymv[s], pt); }

/* the synth that plays it: the first set to one of its patches (the Move lets one at a time; -1 none) */
static int ym_owner(void)
{
    for (int s = 0; s < DMK_SYNTHS; s++) if (syn[s].ym) return s;
    return -1;
}

/* a note's pitch with c cents Q4 more (the voice's tune, glide, vibrato, the LFO): its block << 11 | its F-number */
static uint16_t ym_pitch(int n, int32_t c)
{
    int32_t ct = c / 16, st = clampi((ct + 4850) / 100 - 48, -48, 48), r = clampi(ct - st * 100, -50, 50);
    int nn = clampi(n + st, 12, 119), blk = YM_BLOCK_OF(nn);
    uint32_t fn = mulu16(YM_FN[nn % 12], T_CENT[50 + r]);
    while (fn > YM_FNUM_MAX && blk < YM_BLOCKS - 1) { fn >>= 1; blk++; }
    if (fn > YM_FNUM_MAX) fn = YM_FNUM_MAX;
    return (uint16_t)(blk << 11 | fn);
}
static void ym_put_pitch(int ch, uint16_t p)
{
    const int part = ch / 3, k = ch % 3;
    ym_put(part, 0xA4 + k, (p >> 11) << 3 | (p >> 8 & 7));   /* (the high byte first: the low one writes both) */
    ym_put(part, 0xA0 + k, p & 255);
}

static int32_t ym_tune(const synth_t *p) { return (p->tune * 100 + p->fine) * 16; }   /* the sound's TUNE and FINE, cents Q4 */

/* the chip as the FM sound now is (sp_sync calls it: after any value turned, a pattern's sound loaded): a sound newly
 * set to a factory patch loads it; the playing synth's values, the macros over them, written to all six channels, only
 * the registers that changed */
static void ym_sync(void)
{
    for (int s = 0; s < DMK_SYNTHS; s++) if (syn[s].ym && ymv[s][YV_FROM] != syn[s].ym) ym_val_load(s, syn[s].ym - 1);
    const int s = ym_owner();
    if (s < 0) return;
    const uint8_t *v = ymv[s];
    const int alg = v[YV_ALG] & 7, car = YM_CARRIERS[alg];
    const int bright = v[YV_BRIGHT] - YM_BRIGHT_MID, ratio = v[YV_RATIO] - YM_RATIO_MID;
    const int decay = v[YV_DECAY] - YM_DECAY_MID, rel = v[YV_RELEASE] - YM_REL_MID, det = v[YV_DETUNE];
    if (ym.grit != v[YV_GRIT]) { ym.grit = v[YV_GRIT]; ym_chip_grit(ym.grit); }
    ym.raw = v[YV_RAW];
    ym_set(0, 0x22, v[YV_LFO] ? 0x08 | (v[YV_LFO] - 1) : 0x00);
    for (int ch = 0; ch < YM_CH; ch++) {
        const int part = ch / 3, k = ch % 3;
        ym_set(part, 0xB0 + k, (v[YV_FB] & 7) << 3 | alg);
        ym_set(part, 0xB4 + k, 0xC0 | (v[YV_TREM] & 3) << 4 | (v[YV_VIB] & 7));   /* both sides; the LFO's reach */
        for (int o = 0, nc = 0; o < 4; o++) {
            const uint8_t *w = &v[YV_OP + o * YO_N];
            const int r = YM_OPR[o] + k, is_car = car >> o & 1;
            int mul = w[YO_MUL], dt = w[YO_DT], tl = w[YO_TL];
            if (is_car) { if (det) dt = nc++ & 1 ? YM_DT_DOWN + det : det; }
            else { mul = clampi(mul + ratio, 0, YM_MUL_MAX); tl = clampi(tl - bright, 0, YM_TL_MAX); }
            ym_set(part, 0x30 + r, (dt & 7) << 4 | mul);
            ym_set(part, 0x40 + r, tl);
            ym_set(part, 0x50 + r, (w[YO_KS] & 3) << 6 | w[YO_AR]);
            ym_set(part, 0x60 + r, (is_car && v[YV_TREM] ? YM_AM_ON : 0) | clampi(w[YO_D1R] - decay, 0, YM_RATE_MAX));
            ym_set(part, 0x70 + r, clampi(w[YO_D2R] - decay, 0, YM_RATE_MAX));
            ym_set(part, 0x80 + r, (w[YO_SL] & 15) << 4 | clampi(w[YO_RR] - rel, 0, YM_RR_MAX));
            ym_set(part, 0x90 + r, 0);
        }
    }
}

static int ym_note_on(int sy, voice_t *o, int n)
{
    const synth_t *p = &syn[sy];
    int ch = -1;
    if (ym_owner() != sy) return 0;
    for (int k = 0; k < YM_CH; k++) {                   /* a free channel, the one free longest */
        const int h = ym.hold[k];
        if ((h < 0 || !vc[h].stage || vc[h].ych != k + 1) && (ch < 0 || ym.age[k] < ym.age[ch])) ch = k;
    }
    if (ch < 0) {                                       /* else the quietest released one, else the oldest */
        int32_t quiet = 0x7FFFFFFF;
        for (int k = 0; k < YM_CH; k++) if (!vc[ym.hold[k]].on && vc[ym.hold[k]].env < quiet) { quiet = vc[ym.hold[k]].env; ch = k; }
        if (ch < 0) for (int k = 0; k < YM_CH; k++) if (ch < 0 || ym.age[k] < ym.age[ch]) ch = k;
        voice_t *v = &vc[ym.hold[ch]];
        vc_tail[v->sy] += v->lastv;                     /* (what it sounded fades out, not cut) */
        v->stage = 0; v->ych = 0; v->lastv = 0;
    }
    ym.hold[ch] = (int8_t)(o - vc);
    ym.age[ch] = ++ym.clock;
    o->ych = (uint8_t)(ch + 1);
    ym.fn[ch] = ym_pitch(n, ym_tune(p));
    ym.dirty[ch] = 0;
    ym_put(0, YM_KEY, (ch / 3) << 2 | ch % 3);          /* off, the pitch, on: a channel taken again strikes again */
    ym_put_pitch(ch, ym.fn[ch]);
    ym_put(0, YM_KEY, YM_KEY_ALL | (ch / 3) << 2 | ch % 3);
    ym.idle = 0;
    return 1;
}

static void ym_note_off(voice_t *o)
{
    const int ch = o->ych - 1;
    if (ch >= 0 && ym.hold[ch] == o - vc) ym_put(0, YM_KEY, (ch / 3) << 2 | ch % 3);
}

/* one write's step, between two clocks: the address, then (after YM_WR_ADDR) its data; a moved pitch when nothing
 * else waits */
static inline void ym_drain(void)
{
    if (ym.wait) { ym.wait--; return; }
    if (ym.qr == ym.qw) {
        for (int k = 0; k < YM_CH; k++)
            if (ym.dirty[k]) { ym.dirty[k] = 0; ym_put_pitch(k, ym.fn[k]); break; }
        if (ym.qr == ym.qw) return;
    }
    const uint8_t *w = ym.q[ym.qr % YM_QN];
    if (!ym.sent) { ym_chip_write(w[0] * 2, w[1]); ym.sent = 1; ym.wait = YM_WR_ADDR; }
    else { ym_chip_write(w[0] * 2 + 1, w[2]); ym.sent = 0; ym.wait = YM_WR_DATA; ym.qr++; }
}


/* one chip sample: the writes due over its 24 clocks, then each channel's output */
static void ym_sample(int32_t *o)
{
    int32_t ch[YM_CH];
    for (int c = 0; c < YM_CYC; c++) ym_drain();
    ym_chip_channels(ch);
    for (int k = 0; k < YM_CH; k++) o[k] = ch[k] * YM_GAIN;
    ym.made++;
}

static void ym_run(int frames)
{
    int on = 0;
    if (ym.retry && ym.qw - ym.qr < YM_QN / 2) { ym.retry = 0; ym_sync(); }   /* (what the full queue refused) */
    for (int k = 0; k < YM_CH; k++) { const int h = ym.hold[k]; on |= h >= 0 && vc[h].stage && vc[h].ych == k + 1; }
    if (!on && ym.qr == ym.qw) {                        /* nothing sounds, nothing to write: on a little, then asleep */
        if (ym.idle >= YM_IDLE) return;
        ym.idle++;
    } else ym.idle = 0;
    for (int i = 0; i < frames; i++) {
        ym.ph += YM_STEP;
        while (ym.ph >= 65536u) {
            ym.ph -= 65536u;
            for (int k = 0; k < YM_CH; k++) ym.a[k] = ym.b[k];
            ym_sample(ym.b);
        }
        if (ym.raw) for (int k = 0; k < YM_CH; k++) ym_buf[k][i] = ym.b[k];
        else for (int k = 0; k < YM_CH; k++) ym_buf[k][i] = ym.a[k] + (((ym.b[k] - ym.a[k]) * (int32_t)(ym.ph >> 2)) >> 14);
    }
}

/* an FM voice's block (voices_run's, the envelope's step and the filter's coefficients worked out there): its pitch to
 * the chip if it has moved, then its channel through the filter, DRIVE, the envelope and velocity */
static void ym_voice(voice_t *o, const synth_t *p, int32_t *out, int b, int32_t e0, int32_t de, int32_t f, int32_t f2,
                     int32_t q, int32_t q1, int open, int32_t c)
{
    const int ch = o->ych - 1;
    const uint16_t pt = ym_pitch(o->note, ym_tune(p) + c);
    if (pt != ym.fn[ch]) { ym.fn[ch] = pt; ym.dirty[ch] = 1; }
    const int32_t *in = &ym_buf[ch][b * DMK_SUB];
    for (int i = 0; i < DMK_SUB; i++) {
        int32_t x = in[i], y;
        if (p->drive > 256) x = (curve(T_SAT, (x * p->drive) >> 8) * p->dcomp) >> 8;
        y = vfilt(o, p->ftype, x, f, f2, q, q1);
        if (p->drive > 256) { o->bp = curve(T_LIM, o->bp); o->bp2 = curve(T_LIM, o->bp2); }
        if (open) y += ((x - y) * open) >> 4;
        if (y > 49152 || y < -49152) y = curve(T_LIM, y / 2) * 2;
        o->lastv = (y * ((e0 + de * i) >> 9)) >> 15;
        if (o->vg < 32768) o->lastv = (o->lastv * (o->vg >> 1)) >> 14;
        out[i] += o->lastv;
        if (!o->stage) { o->lastv = 0; break; }
    }
}
