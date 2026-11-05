// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_memory.h -- the M1's internal memory: global data, four drum kits, 100
// combinations and 100 programs, in the layout of an ALL DATA dump (owner's
// manual, MIDI note 12) without the sequencer part.
//
// The whole thing lives in RAM, as the M1's battery-backed memory did, and is
// mirrored to 8 flash sectors (32 KB) below the veeprom and below the
// PicoFaceJ6 patch sector, so a switch of instrument never clobbers either. A first
// boot, or a mirror that does not check out, starts from the factory preload.
// Writing erases and programs only the sectors that changed, and only when
// asked to: an erase stalls the CPU for milliseconds, so the caller picks a
// moment when no voice is sounding.
#ifndef M1_MEMORY_H
#define M1_MEMORY_H

#include <stddef.h>
#include <stdint.h>

namespace m1 {

struct MemoryImage {
    uint8_t global[21];
    uint8_t kits[4 * 210];
    uint8_t combis[100 * 124];
    uint8_t programs[100 * 143];
};
static_assert(sizeof(MemoryImage) == 27561, "ALL DATA layout");

class Memory {
public:
    // factory: the preload in the same layout (programs/combis/kits/global
    // as separate arrays from the ROM conversion).
    void init(const uint8_t* fGlobal, const uint8_t* fKits, const uint8_t* fCombis,
              const uint8_t* fPrograms);

    MemoryImage& image() { return img_; }
    const MemoryImage& image() const { return img_; }
    uint8_t* program(int i) { return img_.programs + 143 * i; }
    uint8_t* combi(int i)   { return img_.combis + 124 * i; }

    // Mark a byte range of the image as changed (offsets into MemoryImage).
    void touch(size_t offset, size_t len);
    void touchAll() { touch(0, sizeof(MemoryImage)); }
    bool dirty() const { return dirty_ != 0; }

    // Write the changed sectors to flash. Returns false if nothing was
    // written (no flash on the host, or the lock hook refused).
    bool store();

    // Back to the factory preload (in RAM; store() makes it permanent).
    void factoryReset();

    bool loadedFromFlash() const { return fromFlash_; }

    // Memory protect (the M1's global switch; not part of the dump).
    bool protect = false;

private:
    MemoryImage img_;
    const uint8_t *fG_ = nullptr, *fK_ = nullptr, *fC_ = nullptr, *fP_ = nullptr;
    uint32_t dirty_ = 0;          // one bit per flash sector
    bool fromFlash_ = false;
};

} // namespace m1

#endif // M1_MEMORY_H
