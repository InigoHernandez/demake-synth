Demake 0.4.25 for the M-VAVE FM-1: every FM-1, the newer chip 0xB36014 too.

- **File:** `demake-0.4.25.fwsc`, identity **FM-1_969**, SHA-256 `426b322abb001271eab63787463884395c73641be35830f6f3ac630bf0b3bd48`, built and signed on GitHub from this commit (`gh attestation verify demake-0.4.25.fwsc --repo InigoHernandez/demake-synth --signer-workflow zvenson/fm1-build/.github/workflows/build.yml`); the same file as the [installer](https://inigohernandez.github.io/demake-synth/install.html)'s, run on FM-1s with both flash chips.
- **Install:** from the [FM-1 Firmware Hub](https://thefirmwarehub.com) or the [installer page](https://inigohernandez.github.io/demake-synth/install.html), over USB in Chrome or Edge. Songs and sounds saved by earlier versions open as they were.

New in 0.4.25:
- **Every FM-1, the newer chip 0xB36014 too.** FM-1s sold since autumn 2026 carry a different flash chip. Up to 0.4.24 Demake took it for an unknown one: your songs were gone at power-off and it refused every update. Now saving and updating work on both chips.
- **Slower chips waited for.** A save or an update gives the chip more time to erase and write, and if it ever gets stuck the chip is reset and the step done again, instead of carrying on.
- **On a chip Demake does not know** it still writes nothing, and now says so at power-on with the chip's id ("FLASH ?? 123456"), for you to report. Holding OCT− while switching on shows the id of any chip.
- **Stuck on 0.4.24 or earlier with the newer chip?** Those versions refuse every update, the way back to V15 included, so put 0.4.25 on once through the chip's download mode (M-Flasher; the hub's rescue page shows how). From 0.4.25 on, updates work as usual.
