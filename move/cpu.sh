#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The render's time on the Move itself (after move/build.sh): build/move/demake-cpu copied over, run, removed.
#   sh move/cpu.sh [SECONDS] [CORE]     (MOVE_HOST=user@host to use another)
# Close Demake on the Move first: it would share the machine, and this measures the render alone.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
HOST=${MOVE_HOST:-move.local}
DEST=/data/UserData/schwung/.demake-cpu
SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=8"
[ -f build/move/demake-cpu ] || { echo "no build/move/demake-cpu: run sh move/build.sh first"; exit 1; }
# shellcheck disable=SC2086
scp $SSH_OPTS build/move/demake-cpu "$HOST:$DEST"
# shellcheck disable=SC2086
ssh $SSH_OPTS "$HOST" "chmod +x /data/UserData/schwung/.demake-cpu; /data/UserData/schwung/.demake-cpu ${1:-20} ${2:-}; rm -f /data/UserData/schwung/.demake-cpu"
