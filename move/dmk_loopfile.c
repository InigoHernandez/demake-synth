/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the loops' files (loops step 3). Included by dmk_move.c after the store's helpers; runs on the
 * saver's thread, or on Schwung's loader thread at create and destroy, never on the audio thread.
 *
 * Each project's loops are WAV files beside the store: loops/projectNN-loopK.wav, 44.1 kHz 16-bit stereo, the loop's
 * frames exactly, with an "acid" chunk giving its beats and tempo, so a DAW takes it in as a loop of its bars. A file
 * written elsewhere opens too (mono or stereo, 16-bit, 44.1 kHz; without beats, its length in bars at the tempo playing).
 * Writing goes to a temp file, then a rename, as the store does. The audio thread hands jobs over a ring and takes the
 * results back a block later (move/dmk_loop.c); a loop's frames are only read here while it is marked busy there, and
 * only written while it is empty there. */

#define LF_DIR       "loops"
#define LF_HEAD      76                 /* RIFF, fmt (16), acid (24), data's header */
#define LF_CHUNK     4096               /* frames read or converted at a time */
#define LF_BEATS_BAR 4
#define LF_ACID_STRETCH 0x04u           /* the acid flags: a loop to stretch, not a one-shot */
#define LF_ROOT      60                 /* (C4: no key of its own) */

static int16_t lf_tmp[LF_CHUNK * 2];

static void lf_path(char *o, int n, int slot, int k, const char *ext)
{
    char name[40], *e = name;
    e = p_cat(e, LF_DIR "/project", name + sizeof name - 1);
    *e++ = (char)('0' + (slot + 1) / 10); *e++ = (char)('0' + (slot + 1) % 10); *e = 0;
    e = p_cat(e, "-loop", name + sizeof name - 1);
    *e++ = (char)('1' + k); *e = 0;
    p_cat(e, ext, name + sizeof name - 1);
    sv_path(o, n, name);
}

static void lf_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void lf_put32(uint8_t *p, uint32_t v) { lf_put16(p, v & 0xFFFFu); lf_put16(p + 2, v >> 16); }
static uint32_t lf_get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t lf_get32(const uint8_t *p) { return lf_get16(p) | lf_get16(p + 2) << 16; }

/* the loop's peaks, as recording draws them (lp_write's) */
static void lf_peaks(int k, uint32_t len, uint8_t *peak)
{
    memset(peak, 0, LP_PEAKS);
    for (uint32_t f = 0; f < len; f++) {
        const int c = (int)((uint64_t)f * LP_PEAKS / len), x = lp_buf[k][f][0], a = (x < 0 ? -x : x) >> 7;
        if (a > peak[c]) peak[c] = (uint8_t)(a > LP_PEAK_MAX ? LP_PEAK_MAX : a);
    }
}

/* loop k's len frames as slot's file. The frames go out as they lie in memory: the Move (aarch64) and the Macs the
 * tests run on are little-endian, as WAV is */
static int lf_write(int k, int slot, uint32_t len, uint32_t steps)
{
    char dir[256], tmp[256], path[256];
    uint8_t h[LF_HEAD];
    const uint32_t data = len * 4u, beats = steps / (LP_STEPS_BAR / LF_BEATS_BAR);
    const float tempo = len ? (float)beats * 60.0f * (float)DMK_SR / (float)len : 0.0f;
    uint32_t tbits;
    memcpy(&tbits, &tempo, sizeof tbits);
    memcpy(h, "RIFF", 4); lf_put32(h + 4, LF_HEAD - 8u + data); memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); lf_put32(h + 16, 16u);
    lf_put16(h + 20, 1u); lf_put16(h + 22, 2u); lf_put32(h + 24, DMK_SR); lf_put32(h + 28, DMK_SR * 4u);
    lf_put16(h + 32, 4u); lf_put16(h + 34, 16u);
    memcpy(h + 36, "acid", 4); lf_put32(h + 40, 24u);
    lf_put32(h + 44, LF_ACID_STRETCH); lf_put16(h + 48, LF_ROOT); lf_put16(h + 50, 0x8000u); lf_put32(h + 52, 0u);
    lf_put32(h + 56, beats); lf_put16(h + 60, LF_BEATS_BAR); lf_put16(h + 62, LF_BEATS_BAR); lf_put32(h + 64, tbits);
    memcpy(h + 68, "data", 4); lf_put32(h + 72, data);
    sv_path(dir, sizeof dir, LF_DIR);
    mkdir(mv.dir, 0755);
    mkdir(dir, 0755);
    lf_path(tmp, sizeof tmp, slot, k, ".wav.tmp");
    lf_path(path, sizeof path, slot, k, ".wav");
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fwrite(h, LF_HEAD, 1, f) == 1 && fwrite(lp_buf[k], 4, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    else unlink(tmp);
    return ok;
}

/* slot's file for loop k into its buffer: 1 read (len, steps, peaks), 0 none or not one Demake can play */
static int lf_read(int k, int slot, uint32_t *len, uint16_t *steps, uint8_t *peak)
{
    char path[256];
    uint8_t c[24];
    uint32_t ch = 0, rate = 0, bits = 0, fmt = 0, beats = 0, n = 0;
    lf_path(path, sizeof path, slot, k, ".wav");
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(c, 12, 1, f) == 1 && !memcmp(c, "RIFF", 4) && !memcmp(c + 8, "WAVE", 4), found = 0;
    while (ok && !found && fread(c, 8, 1, f) == 1) {
        const uint32_t size = lf_get32(c + 4);
        if (!memcmp(c, "fmt ", 4) && size >= 16) {
            ok = fread(c, 16, 1, f) == 1 && fseek(f, (long)(size - 16u + (size & 1u)), SEEK_CUR) == 0;
            fmt = lf_get16(c); ch = lf_get16(c + 2); rate = lf_get32(c + 4); bits = lf_get16(c + 14);
        } else if (!memcmp(c, "acid", 4) && size >= 24) {
            ok = fread(c, 24, 1, f) == 1 && fseek(f, (long)(size - 24u + (size & 1u)), SEEK_CUR) == 0;
            beats = lf_get32(c + 12);
        } else if (!memcmp(c, "data", 4)) {
            found = 1;
            ok = fmt == 1u && bits == 16u && rate == DMK_SR && (ch == 1u || ch == 2u);
            n = ok ? size / (2u * ch) : 0;
        } else ok = fseek(f, (long)(size + (size & 1u)), SEEK_CUR) == 0;
    }
    if (n > LP_FRAMES) n = LP_FRAMES;
    for (uint32_t at = 0; ok && found && at < n;) {     /* (mono: each sample both sides) */
        const uint32_t m = n - at < LF_CHUNK ? n - at : LF_CHUNK;
        if (fread(lf_tmp, 2u * ch, m, f) != m) { ok = 0; break; }
        for (uint32_t i = 0; i < m; i++) {
            lp_buf[k][at + i][0] = lf_tmp[i * ch];
            lp_buf[k][at + i][1] = lf_tmp[i * ch + ch - 1u];
        }
        at += m;
    }
    fclose(f);
    if (!ok || !found || !n) return 0;
    const uint32_t most = (uint32_t)LP_STEPS_BAR << (LP_BARS_N - 1);
    uint32_t st = beats * (LP_STEPS_BAR / LF_BEATS_BAR);
    if (!beats || st % LP_STEPS_BAR || st > most) {     /* (no beats of its own: its length in bars at the tempo playing) */
        const uint32_t per = seq_per();
        const uint64_t bars = per ? ((uint64_t)n * 256u / per + LP_STEPS_BAR / 2) / LP_STEPS_BAR : 1u;
        st = LP_STEPS_BAR;
        while (st < most && st * 2u <= bars * LP_STEPS_BAR) st *= 2u;
    }
    *len = n;
    *steps = (uint16_t)st;
    lf_peaks(k, n, peak);
    return 1;
}

/* a job's result for the audio thread, once it took the last (th_stop: nothing takes it now, the closing one runs) */
static void lf_post(int k, int kind, uint32_t len, uint16_t steps, const uint8_t *peak)
{
    while (atomic_load_explicit(&lr[k].kind, memory_order_acquire) != LR_NONE && !atomic_load(&th_stop)) nap_ms(2);
    lr[k].len = len;
    lr[k].steps = steps;
    if (peak) memcpy(lr[k].peak, peak, sizeof lr[k].peak);
    atomic_store_explicit(&lr[k].kind, kind, memory_order_release);
}

/* every job waiting, in order. Alone (create, destroy: no audio thread), each result is taken at once. 1 if any ran */
static int lf_run(int alone)
{
    static uint8_t peak[LP_PEAKS];
    lp_job_t j;
    int any = 0;
    while (lj_pop(&j)) {
        char path[256];
        uint32_t len = 0;
        uint16_t steps = 0;
        int kind = LR_DONE;
        any = 1;
        if (j.op == LJ_LOAD) kind = lf_read(j.k, j.slot, &len, &steps, peak) ? LR_LOADED : LR_EMPTY;
        else if (j.op == LJ_WRITE) kind = lf_write(j.k, j.slot, j.len, j.steps) ? LR_DONE : LR_FAILED;
        else { lf_path(path, sizeof path, j.slot, j.k, ".wav"); unlink(path); }
        lf_post(j.k, kind, len, steps, peak);
        if (alone) lp_adopt();
    }
    return any;
}
