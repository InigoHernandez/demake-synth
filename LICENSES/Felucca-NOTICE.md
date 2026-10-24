# Felucca

Demake's MOTION phaser and flanger (`firmware/src/dmk_dsp.c`, `phaser_block`, `flanger_block`) follow Felucca's
(`firmware/src/perform.c`, `firmware/src/fx.c`): the four first-order all-pass stages and their rounding, the law of
their coefficient (`T_PHA`, made by `tools/gen_tables.py`: a = (g - 1) / (g + 1), g = tan(pi f / fs), for a break of
150 Hz * 2^(5 i / 32)), the flanger's squared-triangle sweep from 0.25 ms, its soft feedback and the DC blocker on the
swept copy.

Felucca is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`), as Demake is (its
text: `LICENSE`).

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. <https://github.com/hugelton/Felucca>
