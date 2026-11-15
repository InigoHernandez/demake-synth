#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The FM-1's flash driver on a simulated chip (host/fm1_flash_test.c): both chips (0x856014, 0xB36014), unknown ones,
# slow, overrunning and stuck ones, a song saved and loaded across a power cut; with a package built
# (build/fm1/demake-*.fwsc), the update from the installer's current release to it on each chip. From the repo's root:
#   sh tools/fm1/flash_test.sh          (CC to use another compiler, e.g. CC="clang -fsanitize=address,undefined")
set -eu
mkdir -p build/host
${CC:-cc} -std=c99 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -O2 -Wall -Wno-unused-function \
    -Ifirmware/src -Ifirmware/gen -Ifirmware/fm1/hal -o build/host/fm1_flash_test host/fm1_flash_test.c
NEW=$(ls -t build/fm1/demake-*.fwsc 2>/dev/null | head -1 || true)
OLD=web/firmware/$(sed -n 's/.*"file": *"\([^"]*\)".*/\1/p' web/firmware/demake.json)
if [ -n "$NEW" ] && [ -f "$OLD" ]; then
    echo "the update: $OLD -> $NEW"
    build/host/fm1_flash_test "$OLD" "$NEW"
else
    build/host/fm1_flash_test
fi
