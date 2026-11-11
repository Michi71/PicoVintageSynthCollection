// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_store.h -- the flash mirror of the Prophet VS program bank.
//
// The bank itself lives in RAM, in the engine (provs::Engine::vsProgram and
// storeVsProgram). This keeps a copy of it in three flash sectors (12 KB):
// 100 programs in the VS's own packed form, 82 bytes each, behind a header
// with a CRC. The sectors sit below the veeprom, below PicoFaceJ6's patch
// sector and below PicoFaceM1's memory, so a switch of instrument never
// clobbers any of them.
//
// Writing erases and programs only the sectors that changed, and only when
// asked to: an erase stalls the CPU for milliseconds, so the caller picks a
// moment when no voice is sounding.
#ifndef VS_STORE_H
#define VS_STORE_H

#include <cstddef>
#include <cstdint>

namespace vs {

class Store {
public:
    // Copies a valid flash mirror into the engine's bank. Returns false, and
    // leaves the bank alone, if there is none.
    bool load();

    // Mark one program slot, or the whole bank, as changed.
    void touch(int slot);
    void touchAll();
    bool dirty() const { return dirty_ != 0; }

    // Write the changed sectors. Returns false if nothing could be written
    // (a host build without the simulated flash).
    bool store();

private:
    uint32_t dirty_ = 0;      // one bit per flash sector
    bool valid_ = false;      // flash holds a complete image: load() found
                              // one, or store() has written all sectors
};

} // namespace vs

#ifdef VS_STORE_HOST_TEST
// The three sectors, for the host test.
extern uint8_t vs_store_sim_flash[3 * 4096];
#endif

#endif // VS_STORE_H
