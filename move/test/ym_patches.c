/* SPDX-License-Identifier: GPL-3.0-only */
/* Each YM2612 factory patch (move/dmk_ym.c's YM_PATCH) as a WAV, for the owner to hear before any is kept: through
 * Demake on the Move's engine, synth 4 alone, dry (no effects), its filter open, RAW (the chip's samples held, its
 * aliasing kept), the clean chip; one phrase a kind: a melody for the plucks, held chords for the pads, a line for the
 * basses, a high tune for the bells.
 *   ym_patches DIR       DIR/NN-name.wav, one a patch
 * Built with move/dmk_ymfm.cpp (move/build.sh's ymfm.o, or on a Mac: c++ -std=c++14 -fno-exceptions -fno-rtti
 * -DNDEBUG -c move/dmk_ymfm.cpp, then this with -DDMK_MOVE=1 and both linked with c++). */
#include <stdio.h>
#include <stdlib.h>
#include "dmk_core.c"

#define V(s, pg, r, k) ui.val[s][pg][r][k]
enum { PT_SY = DMK_SYNTHS - 1, PT_RUN = 128, PT_TAIL_MS = 2500, PT_MAX_EV = 64, PT_KINDS = 4, PT_EACH = YM_PATCHES / PT_KINDS };

typedef struct { int on_ms, len_ms, note, vel; } ev_t;

static void wav(const char *path, const int16_t *x, int frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t data = (uint32_t)frames * 4u, sr = DMK_SR, br = DMK_SR * 4u, fmt = 16, riff = data + 36u;
    const uint16_t pcm = 1, chn = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(x, 4, (size_t)frames, f);
    fclose(f);
}

static void render(const char *path, int pt, const ev_t *ev, int nev)
{
    int end = 0;
    for (int i = 0; i < nev; i++) if (ev[i].on_ms + ev[i].len_ms > end) end = ev[i].on_ms + ev[i].len_ms;
    const int frames = (end + PT_TAIL_MS) * (DMK_SR / 1000) / PT_RUN * PT_RUN;
    int16_t *out = calloc((size_t)frames * 2u, sizeof *out);
    uint8_t played[PT_MAX_EV] = {0}, ended[PT_MAX_EV] = {0};
    dmk_init(7);
    V(PT_SY, PG_SOUND, 0, 0) = (uint8_t)(DMK_SAMPLES_N + pt);
    V(PT_SY, PG_FX, 0, 4) = choice_val(0, FX_REVERBS);  /* dry */
    V(PT_SY, PG_FX, 1, 4) = choice_val(0, DT_N);
    V(PT_SY, PG_FX, 2, 4) = choice_val(0, FX_MOTIONS);
    V(PT_SY, PG_FX, 3, 0) = 0;
    V(0, PG_GLOBAL, 0, 4) = choice_val(0, FX_COLOURS);
    V(0, PG_GLOBAL, 1, 4) = choice_val(0, SP_N);
    V(0, PG_GLOBAL, 2, 4) = choice_val(0, FX_MEDIAS);
    V(PT_SY, PG_FILTER, 0, 4) = choice_val(FT_LP24, 8); /* open */
    V(PT_SY, PG_FILTER, 0, 0) = 127; V(PT_SY, PG_FILTER, 0, 1) = 0; V(PT_SY, PG_FILTER, 0, 3) = 0;
    V(PT_SY, PG_AMP, 0, 0) = 0; V(PT_SY, PG_AMP, 0, 2) = 127; V(PT_SY, PG_AMP, 0, 3) = 70;   /* the patch's own envelopes */
    V(PT_SY, PG_SOUND, 2, 1) = 0;
    V(PT_SY, PG_LFO, 0, 1) = 0;
    sp_sync();
    ym.raw = 1;
    for (int at = 0; at < frames; at += PT_RUN) {
        const int ms = (int)((int64_t)at * 1000 / DMK_SR);
        for (int i = 0; i < nev; i++) {
            if (!played[i] && ev[i].on_ms <= ms) { note_on_vel(PT_SY, ev[i].note, SRC_KEY, ev[i].vel); played[i] = 1; }
            if (played[i] && !ended[i] && ev[i].on_ms + ev[i].len_ms <= ms) { note_off_src(PT_SY, ev[i].note, SRC_KEY); ended[i] = 1; }
        }
        dmk_render(&out[at * 2], PT_RUN);
    }
    wav(path, out, frames);
    free(out);
}

int main(int argc, char **argv)
{
    static const uint8_t MELODY[10] = {69, 72, 76, 79, 77, 76, 72, 74, 71, 69};             /* A minor, falling home */
    static const uint8_t PADS[3][4] = {{57, 60, 64, 71}, {53, 57, 60, 64}, {55, 59, 62, 69}};   /* Am(add9), Fmaj7, G6 */
    static const uint8_t LINE[16] = {33, 33, 36, 33, 31, 31, 28, 31, 33, 33, 40, 38, 36, 33, 31, 28};
    static const uint8_t TUNE[10] = {81, 84, 88, 86, 84, 81, 79, 81, 76, 81};
    enum { MEL_MS = 400, MEL_LEN = 300, PAD_MS = 3000, PAD_LEN = 2600, BASS_MS = 300, BASS_LEN = 240, BELL_MS = 600, BELL_LEN = 450 };
    if (argc < 2) { fprintf(stderr, "ym_patches DIR\n"); return 2; }
    for (int pt = 0; pt < YM_PATCHES; pt++) {
        ev_t ev[PT_MAX_EV];
        int n = 0;
        switch (pt / PT_EACH) {
        case 0: for (int k = 0; k < 10; k++) ev[n++] = (ev_t){k * MEL_MS, MEL_LEN, MELODY[k], 100}; break;
        case 1: for (int c = 0; c < 3; c++) for (int k = 0; k < 4; k++) ev[n++] = (ev_t){c * PAD_MS, PAD_LEN, PADS[c][k], 100}; break;
        case 2: for (int k = 0; k < 16; k++) ev[n++] = (ev_t){k * BASS_MS, BASS_LEN, LINE[k], 110}; break;
        default: for (int k = 0; k < 10; k++) ev[n++] = (ev_t){k * BELL_MS, BELL_LEN, TUNE[k], 100}; break;
        }
        char path[512], name[64];
        int j = 0;
        for (const char *c = YM_PATCH[pt].name; *c && j < (int)sizeof name - 1; c++) name[j++] = *c == ' ' ? '-' : (char)(*c | 0x20);
        name[j] = 0;
        snprintf(path, sizeof path, "%s/%02d-%s.wav", argv[1], pt + 1, name);
        render(path, pt, ev, n);
        printf("%s\n", path);
    }
    return 0;
}
