#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Demake's build checks, from the repo's root: sh tools/check.sh (after MONO2K's)
#  1. warnings: the host with clang -Wall -Wextra, and with GCC -Wall -Wextra when one is installed; none allowed
#  2. the device rules: the core built for a 32-bit soft-float target (armv7m) calls no float helper and no 64-bit
#     division; its section sizes are printed
#  3. sanitizers: the self-test and every screen of the tour with the text audit (no two texts touch, none is cut),
#     under ASan + UBSan; any report fails the run
#  4. the browser build compiles
set -u
B=build/check
mkdir -p "$B"
FAIL=0
W="-std=c99 -D_DARWIN_C_SOURCE -Wall -Wextra -Wno-unused-function"
I="-Ifirmware/src -Ifirmware/gen"

echo "== warnings"
for cc in clang $(ls /opt/homebrew/bin/gcc-[0-9]* /usr/local/bin/gcc-[0-9]* 2>/dev/null) ${GCC:-}; do
    case "$($cc --version 2>/dev/null | head -1)" in *clang*) [ "$cc" = clang ] || continue ;; esac
    for src in host/dmk_host.c; do
        if ! out=$($cc -O2 $W $I -o "$B/$(basename "$src" .c)" "$src" 2>&1); then
            echo "  $cc $src: DOES NOT BUILD"; printf "%s\n" "$out" | grep "error:" | head -5; FAIL=1; continue
        fi
        n=$(printf "%s" "$out" | grep -c "warning:")
        echo "  $cc $src: $n warnings"
        [ "$n" -eq 0 ] || { printf "%s\n" "$out"; FAIL=1; }
    done
done
ls /opt/homebrew/bin/gcc-[0-9]* /usr/local/bin/gcc-[0-9]* >/dev/null 2>&1 || [ -n "${GCC:-}" ] ||
    echo "  GCC: SKIPPED (none installed; GCC=path to use one)"

echo "== device rules (armv7m, soft float)"
if clang --target=armv7m-none-eabi -mfloat-abi=soft -ffreestanding -O2 -std=c99 -Wall -Wextra -Wno-unused-function \
        -DDMK_FW=1 -DDMK_SAMPLES_N=32 $I -c firmware/src/dmk_core.c -o "$B/core_arm.o"; then
    bad=$(nm -u "$B/core_arm.o" | grep -E "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3|__aeabi_[fd](add|sub|mul|div|cmp|2|cmpeq|cmplt)|__aeabi_[iul]2[fd]|__(add|sub|mul|div)[sd]f3|__float|__fix")
    if [ -n "$bad" ]; then echo "  calls a float helper or 64-bit division:"; echo "$bad"; FAIL=1; else echo "  no float helper, no 64-bit division"; fi
    flash=0; ram=0
    for row in $(objdump -h "$B/core_arm.o" | awk '$1 ~ /^[0-9]+$/ && NF >= 3 {print $2 ":" $3}'); do
        name=${row%%:*}; sz=$((0x${row##*:}))
        case "$name" in .text|.rodata*|.data|.ARM.exidx) flash=$((flash + sz)) ;; esac
        case "$name" in .data|.bss|.pool) ram=$((ram + sz)) ;; esac
    done
    echo "  flash (code, tables, fonts): $((flash / 1024)) KB; RAM (static, POOL included): $((ram / 1024)) KB"
else
    FAIL=1
fi

echo "== sanitizers"
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
rm -f "$B/dmk_host_san"
clang $SAN $W -w $I -o "$B/dmk_host_san" host/dmk_host.c || FAIL=1
rm -rf "$B/screens"; mkdir -p "$B/screens"
{
    "$B/dmk_host_san" --selftest
    "$B/dmk_host_san" "$B/screens"
} > "$B/san.log" 2>&1
rep=$(grep -E "runtime error|ERROR: AddressSanitizer|selftest: FAILED|text audit FAILED|text audit: " "$B/san.log" | sort | uniq -c)
if [ -n "$rep" ]; then echo "$rep"; FAIL=1; else echo "  self-test, $(ls "$B/screens" | wc -l | tr -d ' ') screens and their text: clean"; fi

echo "== the browser build"
if command -v emcc >/dev/null; then sh web/build.sh > "$B/web.log" 2>&1 && echo "  web/dmk.wasm built" || { tail -5 "$B/web.log"; FAIL=1; }
else echo "  SKIPPED (no emcc)"; fi

[ "$FAIL" -eq 0 ] && echo "check: ok" || { echo "check: FAILED"; exit 1; }
