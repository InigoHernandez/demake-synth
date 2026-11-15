/* SPDX-License-Identifier: GPL-3.0-only */
/* Demake on the Move: the page state. The controller (dmk_move_ui.c, mv_view_build) fills one on the audio thread and
 * publishes it; the drawer reads a copy on its own thread and draws the 128 x 64 screen from it alone. Plain data:
 * nothing in it points into the core. The LEDs ride along: ui.js sets them as they are here. */
#ifndef DMK_MOVE_VIEW_H
#define DMK_MOVE_VIEW_H

#include <stdint.h>

#define MS_W 128
#define MS_H 64
#define MS_BYTES (16 * MS_H)           /* MSB first, the top row first */
#define MV_SYNTHS 4                    /* the synths (the core's DMK_SYNTHS on the Move) */

/* the tempo's source: Move's clock (followed, or waited for), or Demake's own (Move sent none when PLAY was pressed) */
enum { MV_SYNC_INT, MV_SYNC_MOVE };

/* how ui.js turns each knob's detents into steps (its curve, ui.js): a range, a short list's options (slower), the
 * tempo (tenths of BPM, whole ones fast), nothing on it. Sent in the frame as '0'.. '3' */
enum { MV_KK_RANGE, MV_KK_CHOICE, MV_KK_TEMPO, MV_KK_NONE };

/* the tempo asked of Move (dmk_move_glue.c tq): none, waiting for Move to follow (Demake at it already), Demake alone
 * at it (Move did not follow: no Link, or Move->Schwung off) */
enum { MV_TQ_NONE, MV_TQ_WAIT, MV_TQ_ALONE };

/* the LEDs as palette indices (pads, steps, track buttons, Rec, Sample: RGB) or brightness 0..127 (the white buttons,
 * MV_BTN_CC) */
#define MV_NBTN 13
static const uint8_t MV_BTN_CC[MV_NBTN] = {86, 88, 56, 60, 119, 62, 63, 55, 54, 50, 58, 118, 52};   /* Rec Mute Undo Copy
                                                                         * Delete Left Right Up Down Menu Loop Sample Capture */
enum { MB_REC, MB_MUTE, MB_UNDO, MB_COPY, MB_DEL, MB_LEFT, MB_RIGHT, MB_UP, MB_DOWN, MB_MENU, MB_LOOP, MB_SAMPLE, MB_CAPTURE };
typedef struct {
    uint8_t step[16], pad[32], trk[4], btn[MV_NBTN];
} mv_leds_t;

typedef struct {
    char name[12], val[16];            /* plain ASCII, as the screen's type has it */
    uint8_t raw;                       /* 0..127: its meter */
    uint8_t on;                        /* something on this knob */
    uint8_t n, idx;                    /* a choice: how many, which (n 0: a number) */
} mv_knob_t;

/* the roll, as it is drawn: a note's cells in pixels, its row, whether it sounds */
#define MV_NOTES 512                   /* the most the roll can show: seven rows of 64 steps, and the patterns' list */
typedef struct { uint8_t x, w; int8_t row; uint8_t lit; } mv_note_t;
typedef struct {
    mv_note_t n[MV_NOTES];
    uint16_t count;
    int16_t head, end;                 /* the playhead or the cursor; where the roll ends (-1 past the view) */
    uint8_t cw8;                       /* a step's width, eighths of a pixel */
} mv_roll_t;

/* the page's one picture: which, and what it is drawn from */
enum { PIC_NONE, PIC_ROLL, PIC_ARP, PIC_PAN, PIC_SONG, PIC_CHORD, PIC_SOUND, PIC_SHAPES, PIC_LFO, PIC_TAIL, PIC_ECHO,
       PIC_MOTION, PIC_COLOUR, PIC_MEDIA, PIC_PROJECTS, PIC_SOUNDS, PIC_PATS };
enum { TL_PLAIN, TL_BLOOM, TL_SHIMMER, TL_REVERSE, TL_FREEZE };   /* a tail's shape: the reverbs' and the Space's types */
typedef struct {
    char name[8];                      /* a chord, a scene's number; a sound's library number */
    uint8_t bars, mutes;               /* a song's step */
} mv_cell_t;
#define MV_CELLS 5                     /* a song's steps in view; a progression's chords, one fewer: their names are wider */
typedef struct {
    uint8_t kind;                      /* PIC_* */
    uint8_t type, on;                  /* its type in its list; off draws it quiet */
    uint8_t p[8];                      /* its numbers, 0..127 */
    int16_t live[2];                   /* what moves in it, -1 nothing */
    char name[20], tag[18];            /* what it is, top left; what it says, top right */
    char big[14];                      /* the chord sounding */
    mv_cell_t cell[MV_CELLS];
    uint8_t cells, now, cur;           /* how many; the one sounding (0xFF none); the one under the cursor */
    uint8_t open;                      /* the row of the project open; the pattern waiting for the bar (0xFF none) */
    char line[4][18];                  /* a list's rows */
    uint8_t num[4];                    /* and their numbers */
    uint8_t env[48];                   /* a sound's shape */
    uint8_t head[4];                   /* where its voices are, 1 + x (0 none) */
} mv_pic_t;

typedef struct {
    uint32_t seq;                      /* bumps with every publish */
    char page[12], row[10];
    char sound[18];                    /* the synth's sound, or the held step's notes */
    char note[34];                     /* a notice, "" none */
    uint8_t mp, mrow, rows;            /* the Move's page, its row, how many */
    uint8_t land, lights;              /* the landscape; a bit a synth sounding */
    char letters[MV_SYNTHS];           /* each synth's sound's first letter */
    uint16_t t;                        /* the slow clock of what moves on its own */
    uint8_t held;                      /* a step is held: sound says which */
    uint8_t ready;                     /* REC and PLAY pressed: the first pad starts it */
    mv_roll_t roll;
    mv_pic_t pic;
    mv_knob_t knob[8];
    int8_t focus;                      /* the knob shown large (touched, or turned a moment ago), -1 none */
    int8_t step;                       /* the playhead among the 16 steps, -1 stopped or another bar */
    uint8_t playing, rec, sync, synth, mutes, scene, pat, bar, bars, in_key;
    int8_t oct;
    uint16_t bpm10;                    /* the tempo heard, tenths */
    uint8_t kkind[8];                  /* MV_KK_*: how ui.js turns each knob */
    uint8_t tq_seq;                    /* the tempo asked of Move: its number (0 never) */
    uint16_t tq_b10;                   /* and its value, tenths: ui.js writes it for Move (desired-tempo) */
    mv_leds_t led;
} mv_view_t;

#endif
