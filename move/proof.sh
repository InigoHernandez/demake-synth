#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Demake on the Move, the proof (after move/build.sh):
#   1. the core on the Mac (clang) and in the Move's container (gcc 12, aarch64 Linux): every case of move/test/heavy.h
#      for 10 s, FNV-1a, compared bit for bit;
#   2. dsp.so itself, driven through the plugin API as Schwung drives it (move/test/bench.c): the first song against the
#      Mac's; the audio thread's rules with every control and Move's clock (each allocation, file, thread and sleep call
#      made there counted); every control; the transport and the phase; saving; the screen (build/move/proof/screens/*.pbm); the render's time;
#      from inside (move/test/inside.c): every pattern full, a store of before (the FM-1's build writes it), every scale, chord and progression;
#   3. ui.js against a mock of the host (node);
#   4. the FM-1: the package rebuilt to v0.4.24's SHA-256, whatever the hooks under DMK_MOVE in firmware/src (FM1=0 to skip: a minute or two).
#   sh move/proof.sh [SECONDS]      (SECONDS: the timing's length, 20 by default)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
SEC=${1:-20}
IMAGE=demake-move-build:bookworm
DSP=build/move/modules/overtake/demake/dsp.so
P=build/move/proof
FM1_SHA=b496d0efdfc403dba0d447a7987331cd7dbe2a5dbc8908cafbc2154f72f584da
[ -f "$DSP" ] || { echo "no $DSP: run sh move/build.sh first"; exit 1; }
if [ -z "${DOCKER_CONFIG:-}" ] && [ -d "$HOME/.jieli/docker" ]; then
    DOCKER_CONFIG="$HOME/.jieli/docker"
    export DOCKER_CONFIG
fi
mkdir -p "$P/mac" "$P/linux" "$P/screens" "$P/save" "$P/old" "$P/inside"

FL="-O2 -fsigned-char -fwrapv -Wno-unused-function -Ifirmware/src -Ifirmware/gen -Imove/test"
echo "== the Mac: $(uname -m), $(cc --version | head -1)"
cc -std=gnu11 $FL -o "$P/mac/render_hash" move/test/render_hash.c
cc -std=gnu11 $FL -DDMK_MOVE=1 -o "$P/mac/render_move" move/test/render_hash.c
"$P/mac/render_move" 1 > "$P/mac/hash_move.txt"
cc -std=gnu11 $FL -o "$P/mac/old_store" move/test/old_store.c
# a store of format 16 (three synths, 256 KiB a song), written by the commit before four synths: its code, this generator
mkdir -p build/move/v16src "$P/v16"
git archive c6b82ab move firmware | tar -x -C build/move/v16src
cp move/test/store_v16.c build/move/v16src/move/test/
# and one of format 17 (four synths, the sound the song's), by the commit before each pattern kept its sound
mkdir -p build/move/v17src "$P/v17"
git archive 982fde6 move firmware | tar -x -C build/move/v17src
cp move/test/store_v17.c build/move/v17src/move/test/
"$P/mac/old_store" "$P/old/demake.dmks"
"$P/mac/render_hash" > "$P/mac/hash.txt"

echo "== the container: aarch64 Linux, gcc 12, glibc 2.36"
docker run --rm --platform linux/arm64 -v "$ROOT":/w -w /w -e P="$P" -e DSP="$DSP" -e SEC="$SEC" -e FL="$FL" "$IMAGE" sh -eu -c '
    L=$P/linux
    gcc -std=gnu11 $FL -mtune=cortex-a72 -o $L/render_hash move/test/render_hash.c
    $L/render_hash > $L/hash.txt
    gcc -O2 -Wall -Wextra -Wno-unused-parameter -rdynamic -o $L/bench move/test/bench.c -ldl -lpthread
    $L/bench $DSP hash > $L/dsp_hash.txt
    for m in rt ctl clock; do $L/bench $DSP $m > $L/dsp_$m.txt || true; done
    $L/bench $DSP save $P/save $P/old/demake.dmks > $L/dsp_save.txt || true
    gcc -std=gnu11 -O2 -DDMK_MOVE=1 -fsigned-char -fwrapv -w -pthread -Ibuild/move/v16src/firmware/src -Ibuild/move/v16src/firmware/gen \
        -o $L/store_v16 build/move/v16src/move/test/store_v16.c -lm && $L/store_v16 $P/v16 > /dev/null
    gcc -std=gnu11 -O2 -DDMK_MOVE=1 -fsigned-char -fwrapv -w -pthread -Ibuild/move/v17src/firmware/src -Ibuild/move/v17src/firmware/gen \
        -o $L/store_v17 build/move/v17src/move/test/store_v17.c -lm && $L/store_v17 $P/v17 > /dev/null
    build/move/demake-inside $P/inside $P/old/demake.dmks $P/v16/projects.dmks $P/v17/projects4.dmks > $L/dsp_inside.txt || true
    gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DDMK_MOVE=1 \
        -fsigned-char -fwrapv -Ifirmware/src -Ifirmware/gen -w -pthread -o $L/inside_san move/test/inside.c -lm
    $L/inside_san $P/inside $P/old/demake.dmks $P/v16/projects.dmks $P/v17/projects4.dmks > $L/dsp_inside_san.txt 2>&1 || true
    $L/bench $DSP draw $P/screens > $L/dsp_draw.txt || true
    $L/bench $DSP time $SEC > $L/dsp_time.txt
'

fail=0
echo
echo "== every case, 10 s, FNV-1a: mac = linux"
n=$(grep -c "" "$P/mac/hash.txt")
if cmp -s "$P/mac/hash.txt" "$P/linux/hash.txt"; then echo "  $n lines, all equal; first and all:"; sed -n '1p;$p' "$P/mac/hash.txt" | sed 's/^/  /'
else echo "  they differ:"; diff "$P/mac/hash.txt" "$P/linux/hash.txt" | head -20; fail=1; fi

echo "== dsp.so through the plugin API (bench hash), against the Mac's first song"
want=$(sed -n 1p "$P/mac/hash_move.txt" | awk '{print $NF}')
got=$(awk '{print $NF}' "$P/linux/dsp_hash.txt")
if [ "$want" = "$got" ]; then echo "  dsp.so $got = mac $want"; else echo "  dsp.so $got != mac $want"; fail=1; fi

for m in rt ctl clock save draw inside inside_san; do
    case $m in
        rt) echo "== dsp.so on the audio thread's rules (bench rt)" ;;
        ctl) echo "== the controls (bench ctl)" ;;
        clock) echo "== Play, Move's transport and phase (bench clock)" ;;
        save) echo "== saving and opening (bench save)" ;;
        draw) echo "== the screen (bench draw): $P/screens/*.pbm" ;;
        inside) echo "== from inside: every pattern full, a store of before, the scales, chords and progressions" ;;
        inside_san) echo "== the same under ASan + UBSan (every report fails it)"; grep -q -E "ERROR: AddressSanitizer|runtime error" "$P/linux/dsp_$m.txt" && { grep -m3 -E "ERROR: AddressSanitizer|runtime error|SUMMARY" "$P/linux/dsp_$m.txt"; fail=1; } ;;
    esac
    cat "$P/linux/dsp_$m.txt"
    grep -q FAIL "$P/linux/dsp_$m.txt" && fail=1
done

echo "== dsp.so's render a block, in the container on this Mac's cores (the Move's Cortex-A72 is slower)"
cat "$P/linux/dsp_time.txt"

if command -v node >/dev/null 2>&1; then
    echo "== ui.js against a mock of the host (node)"
    node move/test/ui_test.mjs > "$P/ui.txt" || fail=1
    grep -c "^  ok" "$P/ui.txt" | sed 's/$/ checks ok/;s/^/  /'
    grep "FAIL" "$P/ui.txt" || true
fi

if [ "${FM1:-1}" = 1 ]; then
    echo "== the FM-1: the package rebuilt"
    .venv/bin/python tools/fm1/build.py --release 0.4.24 > "$P/fm1.log" 2>&1 || { echo "  the FM-1 build failed: $P/fm1.log"; fail=1; }
    sha=$(shasum -a 256 build/fm1/demake-0.4.24.fwsc | awk '{print $1}')
    if [ "$sha" = "$FM1_SHA" ]; then echo "  $sha = v0.4.24"; else echo "  $sha != v0.4.24 $FM1_SHA"; fail=1; fi
fi

[ "$fail" = 0 ] && echo "== proof: all equal, all checks passed" || { echo "== proof: FAILED"; exit 1; }
