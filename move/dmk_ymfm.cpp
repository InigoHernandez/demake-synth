// SPDX-License-Identifier: GPL-3.0-only
// Demake on the Move: the YM2612 chip for move/dmk_ym.c (the owner's Phase 4, 2026-11-18: ymfm, picked blind against
// Nuked-OPN2 in all three pairs, and a sixth of its cost). ymfm is Aaron Giles's (move/vendor/ymfm, BSD-3-Clause,
// unchanged; its OPN family's file compiled into this unit, the chips Demake does not use dropped at the link). Each
// channel apart, so each note goes through its own voice in dmk_dsp.c: its 9-bit output, the YM2612's DAC step on it
// for GRIT, times three (the clocks of four a real chip's pins show a channel on).
//
// Nothing here touches the system: ymfm makes its channels and operators with new, once, as the chip is built (the
// first ym_chip_reset, from dmk_init at create, off the audio thread); new takes them from ARENA, a fixed block, and
// nothing is ever given back (the chip lives as long as the module). No exceptions, no RTTI, no static destructor:
// dsp.so's imports stay what move/build.sh allows.
#include <cstddef>
#include <new>
#include "vendor/ymfm/ymfm_opn.cpp"

namespace {
constexpr int CHANNELS = 6;
constexpr int WINDOW = 3;                              // a channel's clocks on the pins, of its four
constexpr int RSHIFT = 5, CLIP = 256;                  // the chip's 9 bits a channel (ymfm's own for the OPN2)
constexpr std::size_t ARENA = 64 * 1024;               // ymfm's channels and operators (about 10 KB), and room
constexpr std::size_t ALIGN = 16;

alignas(ALIGN) unsigned char arena[ARENA];
std::size_t arena_used;

struct chip : ymfm::ym2612 {
    bool grit = false;
    explicit chip(ymfm::ymfm_interface &i) : ymfm::ym2612(i) {}
    void channels(int32_t *o)
    {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        for (int ch = 0; ch < CHANNELS; ch++) {
            output_data t;
            t.clear();
            m_fm.output(t, RSHIFT, CLIP, 1u << ch);
            o[ch] = WINDOW * (grit ? dac_discontinuity(t.data[0]) : t.data[0]);
        }
    }
};
struct parts { ymfm::ymfm_interface intf; chip c; parts() : c(intf) {} };
alignas(parts) unsigned char parts_mem[sizeof(parts)];
parts *the;
}

void *operator new(std::size_t n)
{
    n = (n + ALIGN - 1) & ~(ALIGN - 1);
    if (arena_used + n > ARENA) __builtin_trap();      // (ymfm asks for its parts once; more is a bug, found at once)
    void *p = arena + arena_used;
    arena_used += n;
    return p;
}
void *operator new[](std::size_t n) { return operator new(n); }
void operator delete(void *) noexcept {}
void operator delete(void *, std::size_t) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete[](void *, std::size_t) noexcept {}
namespace std {                                         // (a vector's growth past its size: ymfm's unused chips' code)
[[noreturn]] void __throw_length_error(const char *) { __builtin_trap(); }
}

extern "C" {
void ym_chip_reset(void)
{
    if (!the) the = new (parts_mem) parts();          // (built in place, once)
    the->c.reset();
}
void ym_chip_write(int port, int data) { the->c.write(static_cast<uint32_t>(port), static_cast<uint8_t>(data)); }
void ym_chip_channels(int32_t *o) { the->c.channels(o); }
void ym_chip_grit(int on) { the->c.grit = on != 0; }
}
