#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Demake onto the Move over Wi-Fi (after move/build.sh): the module copied under a temporary name beside Schwung's
# modules, then renamed into place, so the Move never sees a half-copied module and a dsp.so the Move still has
# mapped is replaced by a rename, never written over.
#   sh move/deploy.sh               (MOVE_HOST=user@host to use another; ~/.ssh/config: Host move.local, User ableton)
# Then on the Move: open Demake from Schwung's overtake menu. If it was open, close it first (Shift + Back, or Shift +
# touch the volume knob + jog click): Back alone only parks it, and the old dsp.so stays loaded.
# The projects live outside the module, in /data/UserData/schwung/demake: a deploy never touches them.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
HOST=${MOVE_HOST:-move.local}
SRC=build/move/modules/overtake/demake
SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=8"

for f in dsp.so module.json ui.js; do
    [ -f "$SRC/$f" ] || { echo "no $SRC/$f: run sh move/build.sh first"; exit 1; }
done

# (.demake-incoming and .demake-old sit outside modules/: the overtake menu's scan never lists them)
# shellcheck disable=SC2086
ssh $SSH_OPTS "$HOST" "rm -rf /data/UserData/schwung/.demake-incoming && mkdir -p /data/UserData/schwung/modules/overtake"
# shellcheck disable=SC2086
scp $SSH_OPTS -r "$SRC" "$HOST:/data/UserData/schwung/.demake-incoming"
# shellcheck disable=SC2086
ssh $SSH_OPTS "$HOST" "set -e
    cd /data/UserData/schwung
    for f in dsp.so module.json ui.js; do [ -s .demake-incoming/\$f ] || { echo \"copy incomplete: \$f\"; exit 1; }; done
    rm -rf .demake-old
    if [ -e modules/overtake/demake ]; then mv modules/overtake/demake .demake-old; fi
    mv .demake-incoming modules/overtake/demake
    rm -rf .demake-old
    ls -l modules/overtake/demake"
echo "Demake is on $HOST at /data/UserData/schwung/modules/overtake/demake"
