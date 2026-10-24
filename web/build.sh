#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Demake's browser build: the firmware's C core to web/dmk.wasm (Emscripten, no JS glue: the worklet instantiates it).
#   sh web/build.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
[ -f firmware/gen/dmk_tables.h ] || .venv/bin/python tools/gen_tables.py
[ -f firmware/gen/dmk_font.h ] || .venv/bin/python tools/gen_font.py
emcc -O3 -std=gnu11 -Wall -Wno-unused-function -Ifirmware/src -Ifirmware/gen web/dmk_web.c \
    -sSTANDALONE_WASM=1 --no-entry -sINITIAL_MEMORY=4194304 -sALLOW_MEMORY_GROWTH=0 -sSTACK_SIZE=131072 \
    -sEXPORTED_FUNCTIONS=_w_audio,_w_render,_w_init,_w_frame,_w_palette,_w_leds,_w_key,_w_button,_w_encoder,_w_midi,_w_store,_w_store_gen \
    -o web/dmk.wasm
ls -l web/dmk.wasm
cp "assets/fonts/Geist[wght].ttf" web/fonts/Geist.ttf      # Geist, SIL OFL 1.1 (LICENSES/OFL-1.1-Geist.txt)
# Kiloy is not served: the wordmark and the D are rendered by tools/gen_art.py, as the screen renders them
