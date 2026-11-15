#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What the real-time threads cost on the Move as it runs, Demake open and playing (move/cpu.sh times the render alone,
# outside Schwung): every SCHED_FIFO thread's CPU time over SECONDS, from /proc, as a share of one core. Schwung's
# audio thread (FIFO, the render: Demake's whole block) is the one to read; Move's own are beside it.
#   sh move/cpu_live.sh [SECONDS]      (MOVE_HOST=user@host to use another; reads only, changes nothing)
set -eu
HOST=${MOVE_HOST:-move.local}
SEC=${1:-10}
# shellcheck disable=SC2029
ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" "
    snap() { for f in /proc/[0-9]*/task/[0-9]*/stat; do cat \$f 2>/dev/null; done | awk '{
        tid = \$1; sub(/^[0-9]+ \\(/, \"\"); i = index(\$0, \") \"); comm = substr(\$0, 1, i - 1); gsub(/ /, \"_\", comm); \$0 = substr(\$0, i + 2);
        if (\$39 == 1) print tid, comm, \$37, \$38, \$12 + \$13 }'; }
    hz=\$(getconf CLK_TCK 2>/dev/null || echo 100)
    snap > /tmp/.dmk_a; sleep $SEC; snap > /tmp/.dmk_b
    awk -v hz=\$hz -v s=$SEC 'NR == FNR { t[\$1] = \$5; next }
        { if (\$1 in t) printf \"%7s %-20s core %s  rt %3s  %5.1f %% of a core\\n\", \$1, \$2, \$3, \$4, 100 * (\$5 - t[\$1]) / hz / s }' /tmp/.dmk_a /tmp/.dmk_b | sort -k7 -rn
    rm -f /tmp/.dmk_a /tmp/.dmk_b"
