# ymfm

Yamaha FM sound chip emulators by Aaron Giles, from github.com/aaronsgiles/ymfm at 81aec25 (2026-07-27). Only the files
the OPN family needs are here (`ymfm.h`, `ymfm_fm.h`, `ymfm_fm.ipp`, `ymfm_opn.h`, `ymfm_opn.cpp`, `ymfm_adpcm.*`,
`ymfm_ssg.*`), unchanged, with its `LICENSE` (BSD-3-Clause).

Demake on the Move uses its YM2612 (`move/dmk_ymfm.cpp`, which compiles `ymfm_opn.cpp` into its unit); the module's
package carries the licence as `LICENSE-ymfm`. BSD-3-Clause sits inside the GPL-3.0 module as it is. The FM-1's
firmware does not contain it.
