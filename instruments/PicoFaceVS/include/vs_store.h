// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_store.h -- the flash mirror of what the Prophet VS kept in its
// battery-backed RAM: the program bank and the user waves.
//
// Both live in RAM, in the engine (provs::Engine::vsProgram / storeVsProgram
// and userWave / setUserWave). This keeps a copy of them in five flash
// sectors (20 KB): two for the 32 user waves in the firmware's 192-byte
// records, three for the 100 programs in the VS's own packed form, 82 bytes
// each - both behind a header with a CRC. The sectors sit below the veeprom,
// below PicoFaceJ6's patch sector and below PicoFaceM1's memory, so a switch
// of instrument never clobbers any of them.
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
    // Copies a valid mirror of the programs into the engine's bank. Returns
    // false, and leaves the bank alone, if there is none.
    bool load();
    // The same for the user waves: every slot that was stored is written
    // into the engine, the others are left as the build has them.
    bool loadWaves();

    // Mark one program slot, the whole bank, or the user waves as changed.
    void touch(int slot);
    void touchAll();
    void touchWaves() { wavesDirty_ = true; }
    bool dirty() const { return dirty_ != 0 || wavesDirty_; }

    // Write what changed. Returns false if nothing could be written (a host
    // build without the simulated flash).
    bool store();

private:
    uint32_t dirty_ = 0;       // programs: one bit per flash sector
    bool valid_ = false;       // flash holds a complete program image: load()
                               // found one, or store() has written all sectors
    bool wavesDirty_ = false;
};

} // namespace vs

#ifdef VS_STORE_HOST_TEST
// The five sectors, for the host test: waves in the first two, programs in
// the last three.
extern uint8_t vs_store_sim_flash[5 * 4096];
#endif

#endif // VS_STORE_H
