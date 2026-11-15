#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Demake on the Move: dsp.so for the Move (aarch64 Linux, glibc 2.35) built in an aarch64 Debian container, checked,
# and packaged as Schwung installs a module.
#   sh move/build.sh            -> build/move/modules/overtake/demake/ and build/move/demake-module.tar.gz
#   MOVE_NATIVE=1 sh move/build.sh   on an aarch64 Debian bookworm itself, no container
# The tarball holds demake/ at its top: Schwung Manager's "Install Custom Module" (and its catalogue installs) unpack
# it into modules/overtake/, the overtake component's folder. move/deploy.sh copies the folder over SSH instead.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

IMAGE=demake-move-build:bookworm      # debian:bookworm-slim (arm64) + gcc 12; its glibc is 2.36, the Move's 2.35
OPT=${OPT:--O2}
OUT=build/move
MOD=$OUT/modules/overtake/demake
PKG=demake-module.tar.gz                # the name Schwung's catalogue expects: <id>-module.tar.gz

# the default Docker config's credential helper is Docker Desktop's; the FM-1 build's config talks to colima alone
if [ -z "${DOCKER_CONFIG:-}" ] && [ -d "$HOME/.jieli/docker" ]; then
    DOCKER_CONFIG="$HOME/.jieli/docker"
    export DOCKER_CONFIG
fi

for h in dmk_tables dmk_font dmk_samples; do
    [ -f "firmware/gen/$h.h" ] || { echo "firmware/gen/$h.h missing: run the generators (CLAUDE.md)"; exit 1; }
done

if [ "${MOVE_NATIVE:-0}" != 1 ] && ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build --platform linux/arm64 -t "$IMAGE" - <<'DOCKER'
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev binutils file && rm -rf /var/lib/apt/lists/*
DOCKER
fi

rm -rf build/move/modules build/move/demake-module.tar.gz
mkdir -p "$MOD"
cp move/module.json move/ui.js "$MOD/"

INNER='
    [ "$(uname -m)" = aarch64 ] || { echo "not an aarch64 container"; exit 1; }
    # one unit (the core is a unity build); only move_plugin_init_v2 is exported, so nothing of the core can bind to
    # another module'"'"'s symbols or theirs to ours; -mtune keeps the ISA plain ARMv8-A. -fsigned-char: an aarch64 char
    # is unsigned by default, a Mac'"'"'s and the FM-1'"'"'s signed; -fwrapv: the same bits wherever a sum runs over
    gcc -std=gnu11 $OPT -mtune=cortex-a72 -DDMK_MOVE=1 -fsigned-char -fwrapv -fPIC -shared -fvisibility=hidden \
        -Wall -Wextra -Wno-unused-function -Wno-sign-compare -Wno-type-limits -Wno-implicit-fallthrough -Ifirmware/src -Ifirmware/gen \
        -Wl,-z,defs -Wl,-z,now -Wl,--as-needed -s \
        -o "$MOD/dsp.so" move/dmk_move.c
    file -b "$MOD/dsp.so"

    # the Move has glibc 2.35: every versioned symbol must be at or under it
    max=$(objdump -T "$MOD/dsp.so" | grep -o "GLIBC_[0-9][0-9.]*" | sort -uV | tail -1)
    top=$(printf "%s\nGLIBC_2.35\n" "$max" | sort -V | tail -1)
    [ "$top" = GLIBC_2.35 ] || { echo "glibc: $max needs a newer glibc than the Move'"'"'s 2.35"; exit 1; }
    echo "glibc: newest symbol $max (the Move: 2.35)"

    exp=$(nm -D --defined-only "$MOD/dsp.so" | awk "{print \$3}" | grep -v "^_" | tr "\n" " ")
    [ "$exp" = "move_plugin_init_v2 " ] || { echo "exports: $exp(only move_plugin_init_v2 expected)"; exit 1; }
    imp=$(nm -D --undefined-only "$MOD/dsp.so" | awk "{print \$2}" | sed "s/@.*//" | sort -u | tr "\n" " ")
    echo "imports: $imp"
    # only what is known: the memory and string calls the audio thread uses, the clock (the vDSO), and what the drawer,
    # the saver and create / destroy use off the audio thread. Nothing that allocates, locks or logs; that the audio thread
    # reaches none of the rest the bench proves (bench rt, per thread)
    ok="_ITM_deregisterTMCloneTable _ITM_registerTMCloneTable __cxa_finalize __getauxval __gmon_start__ __stack_chk_fail __stack_chk_guard"
    ok="$ok memcpy memmove memset memcmp strlen strstr clock_gettime"
    ok="$ok pthread_create pthread_join sched_setscheduler sched_setaffinity nanosleep"
    ok="$ok fopen fopen64 fread fwrite fflush fileno fsync fclose rename unlink mkdir"
    bad=""
    for s in $imp; do case " $ok " in *" $s "*) ;; *) bad="$bad $s" ;; esac; done
    [ -z "$bad" ] || { echo "imports not on the list:$bad"; exit 1; }

    # the render timed on the device (move/cpu.sh): static, so it runs on the Move as it is
    gcc -std=gnu11 $OPT -mtune=cortex-a72 -DDMK_MOVE=1 -fsigned-char -fwrapv -static -Ifirmware/src -Ifirmware/gen -Imove/test \
        -Wno-unused-function -s -o "$OUT/demake-cpu" move/test/cpu.c
    # what the plugin API cannot reach: every pattern full, the file of before, every scale, chord and progression
    gcc -std=gnu11 $OPT -mtune=cortex-a72 -DDMK_MOVE=1 -fsigned-char -fwrapv -static -Ifirmware/src -Ifirmware/gen \
        -Wno-unused-function -pthread -o "$OUT/demake-inside" move/test/inside.c -lm

    tar --owner=0 --group=0 --sort=name -czf "$OUT/$PKG" -C "$OUT/modules/overtake" demake
    tar -tzvf "$OUT/$PKG"
'
if [ "${MOVE_NATIVE:-0}" = 1 ]; then
    OPT="$OPT" MOD="$MOD" OUT="$OUT" PKG="$PKG" sh -eu -c "$INNER"
else
    docker run --rm --platform linux/arm64 -v "$ROOT":/w -w /w -e OPT="$OPT" -e MOD="$MOD" -e OUT="$OUT" -e PKG="$PKG" "$IMAGE" sh -eu -c "$INNER"
fi
ls -l "$OUT/$PKG" | awk '{print "package: " $NF ", " $5 " bytes"}'
{ shasum -a 256 "$OUT/$PKG" 2>/dev/null || sha256sum "$OUT/$PKG"; } | awk '{print "sha256:  " $1}'
