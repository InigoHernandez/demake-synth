/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake's cases for the Move's proof, after the core's unity build: the first song, every effect's type on a heavy
 * base, the worst voices found and the realistic jams of the 0.4.22 audit. Each sets the core from dmk_init: render_hash.c
 * prints their bits, cpu.c times them on the device. */
#define V(s, pg, r, k) ui.val[s][pg][r][k]

static void fx_synth(int s, int rv, int dt, int mo)
{
    V(s, PG_FX, 0, 4) = choice_val(rv, FX_REVERBS); V(s, PG_FX, 0, 0) = 100; V(s, PG_FX, 0, 2) = 127;
    V(s, PG_FX, 1, 4) = choice_val(dt, DT_N); V(s, PG_FX, 1, 0) = 100; V(s, PG_FX, 1, 3) = 100;
    V(s, PG_FX, 2, 4) = choice_val(mo, FX_MOTIONS); V(s, PG_FX, 2, 0) = 100; V(s, PG_FX, 2, 3) = 100;
}
static void fx_all(int rv, int dt, int mo) { for (int s = 0; s < DMK_SYNTHS; s++) fx_synth(s, rv, dt, mo); }
static void glob(int col, int sp, int med)
{
    V(0, PG_GLOBAL, 0, 4) = choice_val(col, FX_COLOURS); V(0, PG_GLOBAL, 0, 0) = 100; V(0, PG_GLOBAL, 0, 3) = 100;
    V(0, PG_GLOBAL, 1, 4) = choice_val(sp, SP_N); V(0, PG_GLOBAL, 1, 0) = 127; V(0, PG_GLOBAL, 1, 2) = 127;
    V(0, PG_GLOBAL, 1, 3) = 127;
    V(0, PG_GLOBAL, 2, 4) = choice_val(med, FX_MEDIAS); V(0, PG_GLOBAL, 2, 0) = 100; V(0, PG_GLOBAL, 2, 3) = 100;
}
static void sends(int v) { for (int s = 0; s < DMK_SYNTHS; s++) V(s, PG_FX, 3, 0) = (uint8_t)v; }
static void layer_all(int blend, int ltune)             /* ltune: 0 two octaves down .. 6 two up */
{
    for (int s = 0; s < DMK_SYNTHS; s++) {
        V(s, PG_SOUND, 1, 0) = (uint8_t)blend; V(s, PG_SOUND, 1, 1) = choice_val(ltune, 7); V(s, PG_SOUND, 1, 2) = choice_val(1, 2);
    }
}
static void lfo_all(int target, int depth)
{
    for (int s = 0; s < DMK_SYNTHS; s++) { V(s, PG_LFO, 0, 1) = (uint8_t)depth; V(s, PG_LFO, 0, 4) = choice_val(target, 5); V(s, PG_LFO, 0, 0) = 90; }
}
static void filt_all(int ft, int drive, int vel)
{
    for (int s = 0; s < DMK_SYNTHS; s++) {
        V(s, PG_FILTER, 0, 4) = choice_val(ft, 8); V(s, PG_FILTER, 0, 0) = 90; V(s, PG_FILTER, 0, 1) = 60;
        V(s, PG_FILTER, 0, 3) = (uint8_t)drive; V(s, PG_SOUND, 2, 1) = (uint8_t)vel;
    }
}
static void sustain_all(void) { for (int s = 0; s < DMK_SYNTHS; s++) { V(s, PG_AMP, 0, 0) = 0; V(s, PG_AMP, 0, 2) = 127; V(s, PG_SOUND, 0, 2) = 127; } }
static void notes12(int base, int step) { for (int i = 0; i < DMK_VOICES; i++) note_on_s(i % DMK_SYNTHS, base + (i * step) % 60); }

/* twelve held voices, each synth's heaviest reverb, delay and motion, the heaviest Space at full send, the heaviest
 * colour, VHS */
static void heavy_base(void)
{
    dmk_init(7);
    sustain_all();
    fx_all(RV_PLATE, DT_GRAINS, FO_FLANGER);
    sends(127);
    glob(FC_LOSSY, SP_REVERSE, FM_VHS);
}
static void heavy_go(void) { sp_sync(); notes12(48, 3); }

/* the worst voices: heavy, a layer two octaves up, LP24, drive, velocity, the LFO on the pitch, high notes */
static void worst(int sample)
{
    heavy_base();
    if (sample >= 0) for (int s = 0; s < DMK_SYNTHS; s++) V(s, PG_SOUND, 0, 0) = (uint8_t)sample;
    layer_all(90, 6);
    filt_all(FT_LP24, 100, 100);
    lfo_all(LT_PITCH, 60);
    sp_sync();
    notes12(72, 2);
}

/* a heavy jam as a player sets it: three synths on the chord machine's m9s, open, spread two octaves, pulsing with a
 * strum, two arps, layers an octave up, heavy FX, Shimmer at full send, 110 BPM */
static void jam(int variant)
{
    dmk_init(7);
    V(0, PG_HOME, 0, 4) = 110;
    V(0, PG_CHORD, 0, 0) = choice_val(VO_OPEN, 5); V(0, PG_CHORD, 0, 2) = choice_val(2, 4);
    V(0, PG_CHORD, 0, 4) = choice_val(1, 20);
    V(0, PG_CHORD, 1, 3) = choice_val(RH_PULSE8, 5); V(0, PG_CHORD, 0, 3) = 80;
    for (int s = 0; s < DMK_SYNTHS; s++) {
        ch.follow[s] = FW_PLAYS;
        V(s, PG_AMP, 0, 4) = 127;
        V(s, PG_SOUND, 1, 0) = 90; V(s, PG_SOUND, 1, 1) = choice_val(5, 7); V(s, PG_SOUND, 1, 2) = choice_val(1, 2);
        V(s, PG_FX, 3, 0) = 127;
    }
    fx_synth(0, RV_HALL, DT_TAPE, FO_ENS); fx_synth(1, RV_PLATE, DT_GRAINS, FO_FLANGER); fx_synth(2, RV_PLATE, DT_PING, FO_PHASER);
    glob(FC_WARM, SP_SHIMMER, FM_TAPE);
    V(1, PG_ROLL, 2, 0) = choice_val(AR_UP, AR_N); V(1, PG_ROLL, 2, 1) = choice_val(2, 9); V(1, PG_ROLL, 2, 2) = choice_val(3, 4);
    V(2, PG_ROLL, 2, 0) = choice_val(AR_CHORD, AR_N); V(2, PG_ROLL, 2, 1) = choice_val(5, 9); V(2, PG_ROLL, 2, 2) = choice_val(1, 4);
    if (variant == 1) {
        for (int s = 0; s < DMK_SYNTHS; s++) { V(s, PG_FILTER, 0, 3) = 80; V(s, PG_SOUND, 2, 1) = 100; V(s, PG_FILTER, 0, 4) = choice_val(FT_LP24, 8); }
        lfo_all(LT_PITCH, 40);
    }
    if (variant == 2) { V(2, PG_SOUND, 0, 0) = 23; V(1, PG_SOUND, 0, 0) = 24; }   /* low-root basses arpeggiated up */
    sp_sync();
    seq_play(1);
}

static void first_song(void) { dmk_init(7); seq_play(1); }

/* case i into the core; its name (0: no such case). The heavy ones first, the sweep of every type after */
#define HC_HEAVY 6                      /* the ones the device's CPU is read on */
static const char *heavy_case(int i)
{
    static char nm[48];
    int k = i;
#define NM(p, n) (snprintf(nm, sizeof nm, "%s %d", p, n), nm)
    switch (k) {
    case 0: first_song(); return "first song";
    case 1: jam(0); return "jam";
    case 2: jam(1); return "jam + LP24 drive LFO";
    case 3: jam(2); return "jam on low samples";
    case 4: worst(-1); return "worst voices";
    case 5: worst(31); return "worst voices, Ailanthus";
    }
    k -= HC_HEAVY;
    if (k < SP_N) { heavy_base(); glob(FC_LOSSY, k, FM_VHS); heavy_go(); return NM("Space", k); }
    k -= SP_N;
    if (k < FX_COLOURS) { heavy_base(); glob(k, SP_REVERSE, FM_VHS); heavy_go(); return NM("colour", k); }
    k -= FX_COLOURS;
    if (k < FX_MEDIAS) { heavy_base(); glob(FC_LOSSY, SP_REVERSE, k); heavy_go(); return NM("media", k); }
    k -= FX_MEDIAS;
    if (k < RV_N) { heavy_base(); fx_all(k, DT_GRAINS, FO_FLANGER); heavy_go(); return NM("reverb", k); }
    k -= RV_N;
    if (k < DT_N) { heavy_base(); fx_all(RV_PLATE, k, FO_FLANGER); heavy_go(); return NM("delay", k); }
    k -= DT_N;
    if (k < FX_MOTIONS) { heavy_base(); fx_all(RV_PLATE, DT_GRAINS, k); heavy_go(); return NM("motion", k); }
    k -= FX_MOTIONS;
    if (k < 8) { heavy_base(); layer_all(90, 6); filt_all(k, 100, 100); heavy_go(); return NM("filter", k); }
#undef NM
    return 0;
}
