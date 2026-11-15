#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Each proof shown to fail on a deliberately broken copy (after move/proof.sh, whose first song and bench it reuses): a
# copy of move/ in the container's /tmp, one change, built to a dsp.so of its own and put to the bench. Nothing in the
# repo is touched.   sh move/test/mutants.sh
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
if [ -z "${DOCKER_CONFIG:-}" ] && [ -d "$HOME/.jieli/docker" ]; then
    DOCKER_CONFIG="$HOME/.jieli/docker"
    export DOCKER_CONFIG
fi
docker run --rm --platform linux/arm64 -v "$ROOT":/w -w /w demake-move-build:bookworm sh -eu -c '
F="-std=gnu11 -O2 -mtune=cortex-a72 -DDMK_MOVE=1 -fsigned-char -fwrapv -fPIC -shared -fvisibility=hidden -w -Ifirmware/src -Ifirmware/gen"
W=/tmp/mut
mkdir -p $W/move/test
ln -sf /w/firmware $W/firmware
gcc -O2 -w -rdynamic -o $W/bench move/test/bench.c -ldl -lpthread
want=$(sed -n 1p build/move/proof/mac/hash_move.txt | awk "{print \$NF}")
bad=0
one() {   # what it breaks, the file, the change, the bench that must catch it
    cp -r move/. $W/move/
    sed -i "$3" "$W/move/$2"
    if cmp -s "$W/move/$2" "move/$2"; then echo "  $1: THE CHANGE DID NOT APPLY"; bad=1; return; fi
    gcc $F -o $W/x.so $W/move/dmk_move.c -pthread
    set +e
    mkdir -p /tmp/mut_save
    if [ "$4" = hash ]; then
        got=$($W/bench $W/x.so hash | awk "{print \$NF}")
        [ "$got" != "$want" ]; ok=$?
        r="the first song $got"
    else
        out=$(timeout 300 $W/bench $W/x.so $4 2>&1)
        printf "%s\n" "$out" | grep -q FAIL; ok=$?
        r=$(printf "%s\n" "$out" | grep -m1 FAIL | sed "s/^ *FAIL *//")
    fi
    set -e
    if [ $ok = 0 ]; then echo "  ok    $1: $r"; else echo "  MISSED  $1"; bad=1; fi
}
# the same of the core too, whatever the file (from the root): a whole copy. move/test/inside.c must catch it, or the bench
core() {   # what it breaks, the file, the change, the bench (none: inside)
    rm -rf /tmp/mut2
    mkdir -p /tmp/mut2/move /tmp/mut2/firmware /tmp/mut_in
    cp -r move/. /tmp/mut2/move/
    cp -r firmware/src firmware/gen /tmp/mut2/firmware/
    sed -i "$3" "/tmp/mut2/$2"
    if cmp -s "/tmp/mut2/$2" "$2"; then echo "  $1: THE CHANGE DID NOT APPLY"; bad=1; return; fi
    G="-std=gnu11 -O2 -mtune=cortex-a72 -DDMK_MOVE=1 -fsigned-char -fwrapv -w -I/tmp/mut2/firmware/src -I/tmp/mut2/firmware/gen"
    set +e
    if [ "${4:-}" = "" ]; then
        gcc $G -static -pthread -o /tmp/mut2/inside /tmp/mut2/move/test/inside.c -lm || { echo "  $1: DOES NOT BUILD"; bad=1; set -e; return; }
        out=$(timeout 120 /tmp/mut2/inside /tmp/mut_in build/move/proof/old/demake.dmks 2>&1); code=$?
    else
        gcc $G -fPIC -shared -fvisibility=hidden -pthread -o /tmp/mut2/x.so /tmp/mut2/move/dmk_move.c || { echo "  $1: DOES NOT BUILD"; bad=1; set -e; return; }
        if [ "$4" = hash ]; then
            got=$($W/bench /tmp/mut2/x.so hash | awk "{print \$NF}")
            out=""; code=0
            [ "$got" != "$want" ] && code=1
        else
            out=$(timeout 300 $W/bench /tmp/mut2/x.so $4 2>&1); code=$?
        fi
    fi
    set -e
    r=$(printf "%s\n" "$out" | grep -m1 FAIL | sed "s/^ *FAIL *//")
    if [ -n "$r" ] || [ $code != 0 ]; then echo "  ok    $1: ${r:-it stopped, $code}"; else echo "  MISSED  $1"; bad=1; fi
}
GLUE=dmk_move_glue.c
one "a sample changed in a block" $GLUE "s#    mv.samples += (uint64_t)frames;#    if (mv.blocks == 1000) out[100] ^= 1; mv.samples += (uint64_t)frames;#" hash
one "blocks of 64 frames" $GLUE "s#dmk_render(out, n);#dmk_render(out, 64); dmk_render(out + 128, 64);#" hash
# (a pointer kept in a volatile: gcc drops a plain free(malloc(8)) altogether)
one "an allocation on the audio path" $GLUE "s|    ev_drain();|    { extern void *malloc(unsigned long); extern void free(void *); void *volatile p = malloc(8); free(p); } ev_drain();|" rt
one "a sleep on the audio path" $GLUE "s|    ev_drain();|    { struct timespec t = {0, 1}; nanosleep(\&t, 0); } ev_drain();|" rt
one "a file opened on the audio path" $GLUE "s|    ev_drain();|    { FILE *volatile f = fopen(\"/dev/null\", \"r\"); if (f) fclose(f); } ev_drain();|" rt
one "a key gone from the piano" dmk_move_ui.c "s|{0, 2, 4, 5, 7, 9, 11, 12, 1, 3|{-1, 2, 4, 5, 7, 9, 11, 12, 1, 3|" ctl
one "no fallback when Move is silent" $GLUE "s|#define CK_PLAY_WAIT 24 |#define CK_PLAY_WAIT 2400 |" clock
one "Move gone never noticed" $GLUE "s|#define CK_STALE (SR / 2) |#define CK_STALE (SR * 600) |" clock
one "the tempo left at the song'"'"'s" $GLUE "s|^    ck_tempo();|    ;|" clock
one "Move'"'"'s stop ignored" $GLUE "s#ck.tr_any = 1; ck_stop(); return; }#ck.tr_any = 1; ck.run = 0; return; }#" clock
one "the clock not steered to Move" $GLUE "s#^    ck_steer();#    ;#" clock
one "the tempo whole, no tenths" $GLUE "s#seq_b10 = (int)(b \* 10 + 0.5);#seq_b10 = (int)(b + 0.5) * 10;#" clock
UI=dmk_move_ui.c
one "Filter Amp: knob 5 on the filter" $UI "s#KR(FILTER, 0, 3), KR(AMP, 0, 0)#KR(FILTER, 0, 3), KR(FILTER, 0, 0)#" ctl
one "a page pad does not change rows" $UI "s#mc.row\[i\] = (uint8_t)((mp_row() + 1) % MP\[i\].rows);#;#" ctl
one "Undo puts nothing back" $UI "s#^    memcpy(ui.val, u->val, sizeof u->val);#    ;#" ctl
one "a quick tap keeps its notes" $UI "s#ui.now - mc.st0\[k\] < MV_TAP_MS#0#" ctl
one "a held step takes no pads" $UI "s#if (p < 16) held_pad(p, note + ui.octave \* 12); else#if (0) ; else#" ctl
one "Shift + steps not the scenes" $UI "s#if (mc.shift || song_row()) { if (mc.shift) mc.used = 1; scene_key(k); return; }#;#" ctl
one "Capture writes nothing" $UI "s#if (roll_strum(s, a, a + held, c->note, c->vel, 0, 1, 0) < 0) { roll_full(); break; }#;#" ctl
one "a pad velocity lost" $UI "s#mv_kvel\[p\] = mv_vel;#mv_kvel[p] = 100;#" ctl
one "the chord row plays single notes" $UI "s#mv_kchord\[p\] = p >= 16;#mv_kchord[p] = 0;#" ctl
one "a chord pad forgotten" $UI "s#pen_take(s, ui.key_ch\[p\], ui.key_nch\[p\], p);#;#" ctl
one "a pad held does not take the step" $UI "s#const int picked = pad_held();#const int picked = 0;#" ctl
one "a step moved by Follow" $UI "s#if (ch.follow\[s\] == FW_FREE || mc.fset >> s \& 1u) return;#return;#" ctl
one "the jog still turns the sound" $UI "s#if (mc.page != MP_SOUND) {#if (0) {#" ctl
one "an empty pattern keeps the chords" $UI "s#sq\[s\].count ? f : FW_FREE;#f;#" ctl
one "the list moves one synth" $UI "s#for (int s = 0; s < DMK_SYNTHS; s++) pat_pick_syn(s, p);#pat_pick_syn(vsyn(), p);#" ctl
one "the chords do not come back" $UI "s#^    chord_join(s);#    ;#" ctl
one "Rec and Play do not wait" $GLUE "s#if (seq_on || !seq_rec || song_row()) return 0;#return 0;#" clock
one "a chord pad starts nothing" $UI "s#if (p >= 16 \&\& stopped_rec \&\& ui.page#if (p >= 16 \&\& !seq_on \&\& seq_rec \&\& ui.page#" clock
one "a take off the steps of Move" $UI "s#if (ready) ck_take_start();#;#" clock
one "a click loads no pattern" $UI "s#if (mc.plist) { plist_click(); return; }#if (mc.plist) { mc.plist = 0; return; }#" ctl
one "a page without its picture" $UI "s#case MP_LFO: pic_lfo(p); break;#case MP_LFO: break;#" "draw /tmp/mut_save"
one "the Roll without its notes" screen/dmk_screen.c "s#ms_note(o, n->x + 1, RY + n->row \\* RH + 1, n->w, RH - 1, n->lit);#;#" "draw /tmp/mut_save"
one "Home stands still" screen/dmk_screen.c "s#ms_motion_land(o, v);#;#" "draw /tmp/mut_save"
one "a save never reaches the file" dmk_move.c "s#if (ok) ok = rename(tmp, path) == 0;#if (ok) ok = 0;#" "save /tmp/mut_save"
one "a project reopened empty" dmk_move.c "s#if (mv.dir\[0\]) { store_load();#if (0) { store_load();#" "save /tmp/mut_save"
one "closing loses the last edits" dmk_move.c "s#mv.save_on_close \&\& store_on() \&\& song_changed()#0#" "save /tmp/mut_save"
SEQ=firmware/src/dmk_seq.c; ST=firmware/src/dmk_store.c; CH=firmware/src/dmk_chord.c; FUI=firmware/src/dmk_ui.c
core "a pattern stops at 96 notes" $SEQ "s#if (q->count >= SEQ_NOTES) return -1;#if (q->count >= 96) return -1;#"
core "a project must fit a sector" $ST "s#define ST_SLOT 0x40000u#define ST_SLOT 0x1000u#"
core "a count saved in one byte" $ST "s#\*p++ = (uint8_t)q->count; \*p++ = (uint8_t)(q->count >> 8);#*p++ = (uint8_t)q->count; *p++ = 0;#"
core "a song of before read with two" $ST "s#if (st_ver >= 16u) { if (e - p < 1) return -1;#if (st_ver >= 1u) { if (e - p < 1) return -1;#"
core "a scene of before keeps its place" $ST "s#if (st_ver < 16u) scn\[g\].prog = prog_moved(scn\[g\].prog);#;#"
core "a User progression moved a place out" $ST "s#return choice_val(i < 16 ? i : i - 16 + PROG_PRE, PROG_N);#return choice_val(i < 16 ? i : i - 15 + PROG_PRE, PROG_N);#"
core "a scale of before keeps its place" $ST "s#ui.val\[s\]\[PG_CHORD\]\[1\]\[1\] = choice_val(ui.val\[s\]\[PG_CHORD\]\[1\]\[1\] \* 6 / 128, MO_COUNT);#;#"
core "the keys type of before keeps its place" $ST "s#ui.val\[s\]\[PG_CHORD\]\[2\]\[1\] = choice_val(ui.val\[s\]\[PG_CHORD\]\[2\]\[1\] \* 8 / 128, KT_COUNT);#;#"
core "the check wrong eight bytes at a time" $ST "s#t\[3\]\[p\[4\]\]#t[3][p[5]]#"
core "twelve voices" firmware/src/dmk.h "s#define DMK_VOICES    40#define DMK_VOICES    12#"
core "a roll sounds eight" $SEQ "s#define SEQ_LIVE 16 #define SEQ_LIVE 8 #"
core "forty drive the limiters" firmware/src/dmk_dsp.c "s#^        poly_run();#        ;#"
core "the trim counts tails as full" firmware/src/dmk_dsp.c "s#const uint32_t a = ((uint32_t)vc\[v\].env >> 12) \* (uint32_t)(vc\[v\].vg >> 1) >> 14;#const uint32_t a = 4096;#"
core "the trim never lets go" firmware/src/dmk_dsp.c "s#else poly_g += (want - poly_g + 15u) >> 4;#;#"
core "the first save faults on the audio thread" move/dmk_move.c "s#^    pages_touch(sv_snap, sizeof sv_snap);#    ;#"
core "the file of before not read" move/dmk_move.c "s#if (!f) { store_old(); return; }#if (!f) return;#"
core "Undo puts no notes back" move/dmk_move_ui.c "s#memcpy(sq\[s\].n, u->r\[s\].n, u->r\[s\].count \* sizeof u->r\[s\].n\[0\]);#;#"
core "the roll counts in a byte" move/dmk_move_view.h "s#    uint16_t count;#    uint8_t count;#"
core "notes out of view crowd the roll" move/dmk_move_ui.c "s#if (row < 0 || row > 6) continue;#;#"
core "the full roll not said" $FUI "s#notice(\"Pattern full: 2048 notes\")#notice(\"The roll is full\")#"
core "every scale counted as seven" $SEQ "s#define SCALE_N MODE_LEN\[seq_mode\]#define SCALE_N 7#"
core "a scale a note short" $SEQ "s#7, 5, 5, 6, 5, 5, 5, 5, 6, 8}#7, 5, 5, 5, 5, 5, 5, 5, 6, 8}#"
core "a chord named as it was stacked" $CH "s#^    q = q_of(iv);#    ;#"
core "a pentatonic thirds counted by degree" $CH "s#if (!seven) { a = tone_of#if (0) { a = tone_of#"
core "the Users where they were" $CH "s#if (ch.prog >= PROG_PRE) return \&uprog\[ch.prog - PROG_PRE\];#if (ch.prog >= 16) return \&uprog[ch.prog - 16];#"
core "a preset without its scale" $CH "s#{\"Bell tower\", MO_IWATO#{\"Bell tower\", MO_MPENT#"
core "a list one name short" $FUI "s#\"Dust storm\", \"Salt flats\", \"Red desert\", \"Sun temple\", \"Bell tower\",#\"Dust storm\", \"Salt flats\", \"Red desert\", \"Sun temple\", \"Bell\",#"
core "the roll lost on a shorter scale" move/dmk_move_ui.c "s#^    if (mc.scale_n == SCALE_N) return;#    return;#"
core "a scale named by the FM-1s six" $FUI "s#str_cat(o, MODENAMES\[choice_index(c, v)\]);#str_cat(o, MODENAMES[choice_index(c, v) % 6]);#" "draw /tmp/mut_save"
core "a new song on another progression" $FUI "s#^    ui.val\[0\]\[PG_CHORD\]\[0\]\[4\] = choice_val(0, PROG_N); ui.val\[0\]\[PG_CHORD\]\[1\]\[1\] = choice_val(MO_AEOL, MO_COUNT);.*#    ;#" hash
exit $bad
'
